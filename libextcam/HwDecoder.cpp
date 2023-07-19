/*
 *  Copyright 2023 NXP.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

//#define LOG_NDEBUG 0
#define LOG_TAG "HwDecoder"

#include "HwDecoder.h"
#include <sys/mman.h>
#include <linux/imx_vpu.h>
#include <utils/Log.h>
#include <inttypes.h>
#include <C2Config.h>

#include "graphics_ext.h"
#include "Imx_ext.h"
#include "Memory.h"

namespace android {

#define Align(ptr, align)    (((uint32_t)(ptr) + (align) - 1) / (align) * (align))
#define AMPHION_FRAME_ALIGN     (512)
#define AMPHION_FRAME_PLUS	(1)

#define HANTRO_FRAME_PLUS	(4)
#define HANTRO_FRAME_ALIGN (8)
#define HANTRO_FRAME_ALIGN_WIDTH (HANTRO_FRAME_ALIGN*2)
#define HANTRO_FRAME_ALIGN_HEIGHT (HANTRO_FRAME_ALIGN_WIDTH)

HwDecoder::HwDecoder(const char* mime, std::condition_variable * FramesSignal):
    mPollThread(0),
    mFetchThread(0),
    pDev(NULL),
    mFd(-1),
    mOutBufType(V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE),
    mCapBufType(V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {

    mFramesSignal = FramesSignal;

    mDecState = UNINITIALIZED;
    mPollState = UNINITIALIZED;
    mFetchState = UNINITIALIZED;

    bInputStreamOn = false;
    bOutputStreamOn = false;

    mOutputBufferUsed = 0;

    bNeedPostProcess = false;

// TODO: use pDev->GetFormatFrameInfo to get align size
#ifdef AMPHION_V4L2
    mFrameAlignW = AMPHION_FRAME_ALIGN;
    mFrameAlignH = AMPHION_FRAME_ALIGN;
#else
    mFrameAlignW = HANTRO_FRAME_ALIGN_WIDTH;
    mFrameAlignH = HANTRO_FRAME_ALIGN_HEIGHT;
#endif

    mInputFormat.bufferNum = DEFAULT_INPUT_BUFFER_COUNT;
    mInputFormat.bufferSize = DEFAULT_INPUT_BUFFER_SIZE_4K;
    mInputFormat.width = DEFAULT_FRM_WIDTH;
    mInputFormat.height = DEFAULT_FRM_HEIGHT;

    mOutputFormat.width = DEFAULT_FRM_WIDTH;
    mOutputFormat.height = DEFAULT_FRM_HEIGHT;
    mOutputFormat.pixelFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
    mOutputFormat.bufferNum = 0;
    mOutputFormat.rect.left = 0;
    mOutputFormat.rect.top = 0;
    mOutputFormat.rect.right = mOutputFormat.width;
    mOutputFormat.rect.bottom = mOutputFormat.height;

    mInMemType = V4L2_MEMORY_MMAP;
    mOutMemType = V4L2_MEMORY_DMABUF;

    mInFormat = V4L2_PIX_FMT_JPEG;
    mOutFormat = V4L2_PIX_FMT_NV12;
}

HwDecoder::~HwDecoder() {
}

status_t HwDecoder::Init() {
    status_t ret = UNKNOWN_ERROR;

    if(pDev == NULL) {
        pDev = new DecoderDev();
    }
    if(pDev == NULL)
        return ret;

    mFd = pDev->Open();
    if(mFd < 0)
        return ret;
    ALOGV("%s: Decoder Opened fd=%d", __FUNCTION__, mFd);

    ret = pDev->GetVideoBufferType(&mOutBufType, &mCapBufType);
    if (ret != OK)
        return ret;

    return OK;
}

status_t HwDecoder::Start() {
    ALOGV("%s: BEGIN", __FUNCTION__);
    status_t ret = UNKNOWN_ERROR;
{
    Mutex::Autolock autoLock(mLock);

    if(!pDev->IsOutputFormatSupported(mInFormat)) {
        ALOGE("%s: input format not suppoted", __FUNCTION__);
        return ret;
    }

    ALOGV("%s: mInputFormat width%d, height%d", __FUNCTION__, mInputFormat.width, mInputFormat.height);

    if (mInputFormat.width >= 3840 && mInputFormat.height >= 2160)
        mInputFormat.bufferSize = DEFAULT_INPUT_BUFFER_SIZE_4K;
    else {
        mInputFormat.bufferSize = Align(mInputFormat.width * mInputFormat.height * 2, 4096);
        if (mInputFormat.bufferSize < DEFAULT_INPUT_BUFFER_SIZE_1080P)
            mInputFormat.bufferSize = DEFAULT_INPUT_BUFFER_SIZE_1080P;
    }
    ALOGV("%s: mInputFormat bufferSize=%d", __FUNCTION__, mInputFormat.bufferSize);
}

    ret = SetInputFormats();
    if(ret != OK) {
        ALOGE("%s: SetInputFormats failed", __FUNCTION__);
        return ret;
    }

    if(mInputBufferMap.empty() || (mInputFormat.bufferSize != mInputBufferMap[0].plane.size)) {
        ret = allocateInputBuffers();
        if(ret != OK)
            return ret;
    }

{
    Mutex::Autolock autoLock(mLock);

    ret = pDev->GetV4l2FormatByColor(mOutputFormat.pixelFormat, &mOutFormat);
    if(ret != OK)
        return ret;

    ALOGV("%s: pixelFormat=0x%x,mOutFormat=0x%x", __FUNCTION__, mOutputFormat.pixelFormat, mOutFormat);

    if(!pDev->IsCaptureFormatSupported(mOutFormat)) {
        return UNKNOWN_ERROR;
    }

    if(mOutputFormat.pixelFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP ||
        mOutputFormat.pixelFormat == HAL_PIXEL_FORMAT_NV12_TILED ) {
        //update output frame width & height
        mOutputFormat.width = Align(mOutputFormat.rect.right, mFrameAlignW);
        mOutputFormat.height = Align(mOutputFormat.rect.bottom, mFrameAlignH);

        if (V4L2_TYPE_IS_MULTIPLANAR(mCapBufType)) {
            mOutputPlaneSize[0] = mOutputFormat.width * mOutputFormat.height;
            mOutputPlaneSize[1] = mOutputPlaneSize[0] / 2;
        } else {
            mOutputPlaneSize[0] = mOutputFormat.width * mOutputFormat.height * 3 / 2;
        }

        ALOGV("%s pixel format =0x%x,success", __FUNCTION__, mOutputFormat.pixelFormat);
    } else
        return UNKNOWN_ERROR;
}

    ret = SetOutputFormats();
    if(ret != OK) {
        ALOGE("%s: SetOutputFormats failed", __FUNCTION__);
        return ret;
    }

    ret = createPollThread();
    if(ret != OK)
        return ret;

    mDecState = RUNNING;

    ALOGV("%s: ret=%d", __FUNCTION__, ret);
    return ret;
}

status_t HwDecoder::SetInputFormats() {
    int result = 0;
    uint32_t alignedWidth;
    Mutex::Autolock autoLock(mLock);

    alignedWidth = Align(mInputFormat.width, mFrameAlignW);

    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = mOutBufType;
    if (V4L2_TYPE_IS_MULTIPLANAR(mOutBufType)) {
        format.fmt.pix_mp.num_planes = 1;
        format.fmt.pix_mp.pixelformat = mInFormat;
        format.fmt.pix_mp.plane_fmt[0].sizeimage = mInputFormat.bufferSize;
        format.fmt.pix_mp.plane_fmt[0].bytesperline = alignedWidth;
        format.fmt.pix_mp.width = mInputFormat.width;
        format.fmt.pix_mp.height = mInputFormat.height;
        format.fmt.pix_mp.field = V4L2_FIELD_NONE;
    } else {
        format.fmt.pix.pixelformat = mInFormat;
		format.fmt.pix.width = mInputFormat.width;
		format.fmt.pix.height = mInputFormat.height;
		format.fmt.pix.bytesperline = mInputFormat.width;
		format.fmt.pix.sizeimage = mInputFormat.bufferSize;
    }

    result = ioctl (mFd, VIDIOC_S_FMT, &format);
    if(result != 0) {
        ALOGE("%s: ioctl VIDIOC_S_FMT failed, result=%d", __FUNCTION__, result);
        return UNKNOWN_ERROR;
    }

    memset(&format, 0, sizeof(format));
    format.type = mOutBufType;

    result = ioctl (mFd, VIDIOC_G_FMT, &format);
    if(result != 0) {
        ALOGE("%s: ioctl VIDIOC_G_FMT failed, result=%d", __FUNCTION__, result);
        return UNKNOWN_ERROR;
    }

    uint32_t retFormat, retWidth, retHeight, retSizeimage;
    if (V4L2_TYPE_IS_MULTIPLANAR(mOutBufType)) {
        retFormat = format.fmt.pix_mp.pixelformat;
        retHeight = format.fmt.pix_mp.height;
        retWidth = format.fmt.pix_mp.width;
        retSizeimage = format.fmt.pix_mp.plane_fmt[0].sizeimage;
    } else {
        retFormat = format.fmt.pix.pixelformat;
        retHeight = format.fmt.pix.height;
        retWidth = format.fmt.pix.width;
        retSizeimage = format.fmt.pix.sizeimage;
    }

    if(retFormat != mInFormat) {
        ALOGE("%s mInFormat mismatch", __FUNCTION__);
        return UNKNOWN_ERROR;
    }

    if(retWidth != mInputFormat.width || retHeight != mInputFormat.height) {
        ALOGE("%s resolution mismatch", __FUNCTION__);
        return UNKNOWN_ERROR;
    }

    if(retSizeimage != mInputFormat.bufferSize) {
        ALOGW("%s bufferSize mismatch retSizeimage %d input bufferSize %d", __FUNCTION__,
                retSizeimage, mInputFormat.bufferSize);
        mInputFormat.bufferSize = retSizeimage;
    }

    return OK;
}

status_t HwDecoder::SetOutputFormats() {
    int result = 0;
    uint32_t alignedWidth;
    Mutex::Autolock autoLock(mLock);

    struct v4l2_format format;
    memset(&format, 0, sizeof(format));
    format.type = mCapBufType;

    alignedWidth = Align(mOutputFormat.width, mFrameAlignW);

    mOutputFormat.width = Align(mOutputFormat.width, mFrameAlignW);
    mOutputFormat.height = Align(mOutputFormat.height, mFrameAlignH);

    if (V4L2_TYPE_IS_MULTIPLANAR(mCapBufType)) {
        format.fmt.pix_mp.num_planes = DEFAULT_OUTPUT_BUFFER_PLANE;
        format.fmt.pix_mp.pixelformat = mOutFormat;
        format.fmt.pix_mp.width = mOutputFormat.width;
        format.fmt.pix_mp.height = mOutputFormat.height;
        format.fmt.pix_mp.plane_fmt[0].sizeimage = mOutputPlaneSize[0];
        format.fmt.pix_mp.plane_fmt[0].bytesperline = alignedWidth;
        format.fmt.pix_mp.plane_fmt[1].sizeimage = mOutputPlaneSize[1];
        format.fmt.pix_mp.plane_fmt[1].bytesperline = alignedWidth;
        format.fmt.pix_mp.field = V4L2_FIELD_NONE;
    } else {
        format.fmt.pix.pixelformat = mOutFormat;
		format.fmt.pix.width = mOutputFormat.width;
		format.fmt.pix.height = mOutputFormat.height;
		format.fmt.pix.bytesperline = alignedWidth;
		format.fmt.pix.sizeimage = mOutputPlaneSize[0];
    }

    ALOGV("%s w=%d,h=%d,fmt=0x%x", __FUNCTION__, mOutputFormat.width, mOutputFormat.height, mOutFormat);

    result = ioctl (mFd, VIDIOC_S_FMT, &format);
    if(result != 0) {
        ALOGE("%s VIDIOC_S_FMT failed", __FUNCTION__);
        return UNKNOWN_ERROR;
    }

    memset(&format, 0, sizeof(struct v4l2_format));
    format.type = mCapBufType;

    result = ioctl (mFd, VIDIOC_G_FMT, &format);
    if(result < 0)
        return UNKNOWN_ERROR;

    uint32_t retFormat;

    if (V4L2_TYPE_IS_MULTIPLANAR(mCapBufType)) {
        retFormat = format.fmt.pix_mp.pixelformat;

        if(format.fmt.pix_mp.plane_fmt[0].sizeimage !=  mOutputPlaneSize[0] ||
            format.fmt.pix_mp.plane_fmt[1].sizeimage !=  mOutputPlaneSize[1]) {
            ALOGE("%s bufferSize mismatch", __FUNCTION__);
            return UNKNOWN_ERROR;
        }
    } else {
        retFormat = format.fmt.pix.pixelformat;

        if(format.fmt.pix.sizeimage !=  mOutputPlaneSize[0]) {
            ALOGW("%s bufferSize mismatch, %d -> %d",
                __FUNCTION__, mOutputPlaneSize[0], format.fmt.pix.sizeimage);
            mOutputPlaneSize[0] = format.fmt.pix.sizeimage;
            mOutputFormat.bufferSize = mOutputPlaneSize[0];
        }
    }

    if(retFormat != mOutFormat) {
        ALOGE("%s mOutFormat mismatch", __FUNCTION__);
        return UNKNOWN_ERROR;
    }

    ALOGV("%s success", __FUNCTION__);
    return OK;
}

status_t HwDecoder::allocateInputBuffers() {
    int result = 0;
    Mutex::Autolock autoLock(mLock);

    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = mInputFormat.bufferNum;
    reqbufs.type = mOutBufType;
    reqbufs.memory = mInMemType;
    ALOGV("%s count=%d", __FUNCTION__, reqbufs.count);

    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);
    if(result != 0) {
        ALOGE("%s VIDIOC_REQBUFS failed result=%d", __FUNCTION__, result);
        return UNKNOWN_ERROR;
    }

    mInputBufferMap.resize(reqbufs.count);
    for (size_t i = 0; i < mInputBufferMap.size(); i++) {
        mInputBufferMap[i].used = false;
        mInputBufferMap[i].plane.vaddr = 0;
        mInputBufferMap[i].plane.paddr = 0;
        mInputBufferMap[i].plane.size = mInputFormat.bufferSize;
        mInputBufferMap[i].plane.length = 0;
        mInputBufferMap[i].plane.offset = 0;
        mInputBufferMap[i].input_id = -1;
    }

    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane planes;
    void * ptr = NULL;
    uint64_t tmp = 0;

    if(mInMemType != V4L2_MEMORY_MMAP)
        return UNKNOWN_ERROR;

    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(&planes, 0, sizeof(planes));

    for (size_t i = 0; i < mInputBufferMap.size(); i++) {
        stV4lBuf.type = mOutBufType;
        stV4lBuf.memory = V4L2_MEMORY_MMAP;
        stV4lBuf.index = i;

        if (V4L2_TYPE_IS_MULTIPLANAR(mOutBufType)) {
            stV4lBuf.length = DEFAULT_INPUT_BUFFER_PLANE;
            stV4lBuf.m.planes = &planes;
        }
        result = ioctl(mFd, VIDIOC_QUERYBUF, &stV4lBuf);
        if(result < 0)
            return UNKNOWN_ERROR;

        planes.length = mInputFormat.bufferSize;

        if (V4L2_TYPE_IS_MULTIPLANAR(mOutBufType)) {
            ptr = mmap(NULL, planes.length,
                    PROT_READ | PROT_WRITE, /* recommended */
                    MAP_SHARED,             /* recommended */
                    mFd, planes.m.mem_offset);
        } else {
            ptr = mmap(NULL, stV4lBuf.length,
                    PROT_READ | PROT_WRITE,
                    MAP_SHARED,
                    mFd, stV4lBuf.m.offset);
        }

        if(ptr != MAP_FAILED) {
            tmp = (uint64_t)ptr;
            mInputBufferMap[i].plane.vaddr = tmp;
        }else
            return NO_MEMORY;
    }

    ALOGV("%s success", __FUNCTION__);
    return OK;
}

status_t HwDecoder::destroyInputBuffers() {
    Mutex::Autolock autoLock(mLock);
    if (mInputBufferMap.empty())
        return OK;

    for (size_t i = 0; i < mInputBufferMap.size(); i++) {
        mInputBufferMap[i].used = false;
        mInputBufferMap[i].input_id = -1;
        if(mInputBufferMap[i].plane.vaddr != 0)
            munmap((void*)(uintptr_t)mInputBufferMap[i].plane.vaddr, mInputBufferMap[i].plane.size);

        mInputBufferMap[i].plane.vaddr = 0;
        mInputBufferMap[i].plane.paddr = 0;
        mInputBufferMap[i].plane.size = 0;
        mInputBufferMap[i].plane.length = 0;
        mInputBufferMap[i].plane.offset = 0;
    }

    int result = 0;
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = 0;
    reqbufs.type = mOutBufType;
    reqbufs.memory = V4L2_MEMORY_MMAP;//use mmap to free buffer
    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);
    if(result != 0) {
        ALOGV("ignore the result");
    }

    mInputBufferMap.clear();

    ALOGV("%s success", __FUNCTION__);
    return OK;
}

status_t HwDecoder::destroyOutputBuffers() {
    Mutex::Autolock autoLock(mLock);
    if (mOutputBufferMap.empty())
        return OK;

    int result = 0;
    struct v4l2_requestbuffers reqbufs;
    memset(&reqbufs, 0, sizeof(reqbufs));
    reqbufs.count = 0;
    reqbufs.type = mCapBufType;
    reqbufs.memory = V4L2_MEMORY_MMAP;//use mmap to free buffer

    result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);
    if(result != 0) {
        ALOGV("ignore VIDIOC_REQBUFS result");
    }

    mOutputBufferMap.clear();
    return OK;
}

status_t HwDecoder::HandlePollThread() {
    status_t ret = OK;
    int32_t poll_ret = 0;

    while(mPollState == RUNNING) {
        poll_ret = pDev->Poll();
        if(poll_ret & V4L2_DEV_POLL_EVENT) {
            ALOGV("%s: handleDequeueEvent", __FUNCTION__);
            ret = handleDequeueEvent();
        }

        if(poll_ret & V4L2_DEV_POLL_OUTPUT) {
            ALOGV("%s: dequeueInputBuffer", __FUNCTION__);
            ret = dequeueInputBuffer();
        }

        if(poll_ret & V4L2_DEV_POLL_CAPTURE) {
            ALOGV("%s: dequeueOutputBuffer", __FUNCTION__);
            ret = dequeueOutputBuffer();
        }
    }
    mPollState = STOPPED;
    ALOGV("%s: stopped", __FUNCTION__);

    return OK;
}

status_t HwDecoder::HandleFetchThread() {
    while(mFetchState == RUNNING) {
        if(mOutputBufferUsed >= mOutputFormat.bufferNum || RUNNING != mDecState) {
            usleep(5000);
            ALOGV("%s: mOutputBufferUsed %d mOutputFormat.bufferNum %d",
                    __FUNCTION__, mOutputBufferUsed, mOutputFormat.bufferNum);
            continue;
        }

        ALOGV("%s: getFreeDecoderBuffer begin with mOutputBufferUsed %d mOutputFormat.bufferNum %d",
                __FUNCTION__, mOutputBufferUsed, mOutputFormat.bufferNum);

        DecoderBufferInfo *mDBInfo = getFreeDecoderBuffer();
        if((bNeedPostProcess && (mDBInfo->mDBInfoId >= mOutputFormat.bufferNum)) || !mDBInfo) {
            usleep(3000);
            continue;
        }

        ALOGV("%s: queueOutputBuffer BEGIN, bufId=%d", __FUNCTION__, mDBInfo->mDBInfoId);
        queueOutputBuffer(mDBInfo);
    }

    mFetchState = STOPPED;
    ALOGV("%s: stopped", __FUNCTION__);

    return OK;
}

void *HwDecoder::PollThreadWrapper(void *me) {
    return (void *)(uintptr_t)static_cast<HwDecoder *>(me)->HandlePollThread();
}

void *HwDecoder::FetchThreadWrapper(void *me) {
    return (void *)(uintptr_t)static_cast<HwDecoder *>(me)->HandleFetchThread();
}

status_t HwDecoder::createPollThread() {
    Mutex::Autolock autoLock(mLock);

    if(mPollState == UNINITIALIZED) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        mPollState = RUNNING;

        pthread_create(&mPollThread, &attr, PollThreadWrapper, this);
        pthread_attr_destroy(&attr);
    }

    return OK;
}

status_t HwDecoder::destroyPollThread() {
    if(mPollState == RUNNING) {
        int cnt = 0;
        mPollState = STOPPING;
        pDev->StopDecoder();
        do {
            usleep(1000);
            cnt ++;
        } while (mPollState != STOPPED && cnt < 20);

        pDev->SetPollInterrupt();
        ALOGV("%s: call pthread_join", __FUNCTION__);
        pthread_join(mPollThread, NULL);
        pDev->ClearPollInterrupt();
    }
    return OK;
}

status_t HwDecoder::createFetchThread() {
    Mutex::Autolock autoLock(mLock);
    if(mFetchState == UNINITIALIZED) {
        pthread_attr_t attr;
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

        mFetchState = RUNNING;
        pthread_create(&mFetchThread, &attr, FetchThreadWrapper, this);
        pthread_attr_destroy(&attr);

    }
    return OK;
}

status_t HwDecoder::destroyFetchThread() {
    if(mFetchState == RUNNING) {
        int cnt = 0;
        mFetchState = STOPPING;
        do {
            usleep(1000);
            cnt ++;
        } while (mFetchState != STOPPED && cnt < 20);

        pthread_join(mFetchThread, NULL);
    }
    return OK;
}

status_t HwDecoder::queueInputBuffer(std::unique_ptr<DecoderInputBuffer> input) {
    int result = 0;
    int32_t index = -1;
    uint32_t buf_length = 0;

    if(input == nullptr)
        return BAD_VALUE;

    if(STOPPED == mDecState || UNINITIALIZED == mDecState) {
        if (OK != Start())
            return BAD_VALUE;
    }

    if(input->size > mInputFormat.bufferSize) {
        ALOGE("%s: invalid buffer size=%d,cap=%d", __FUNCTION__, input->size, mInputFormat.bufferSize);
        return UNKNOWN_ERROR;
    }

    mLock.lock();
    for(int32_t i = 0; i < mInputBufferMap.size(); i++) {
        if(mInputBufferMap[i].input_id == -1 && !mInputBufferMap[i].used) {
            index = i;
            break;
        }
    }

    mInputBufferMap[index].input_id = input->id;
    ALOGV("%s: input->BUF=%p, index=%d, len=%zu", __FUNCTION__, input->pInBuffer, index, (size_t)input->size);

    uint32_t offset = 0;
    memcpy((void*)(uintptr_t)(mInputBufferMap[index].plane.vaddr + offset), input->pInBuffer, input->size);
    buf_length += input->size;
    dumpStream((void*)(uintptr_t)(mInputBufferMap[index].plane.vaddr + offset), buf_length, 0);

    struct v4l2_buffer stV4lBuf;
    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    struct v4l2_plane plane;//DEFAULT_INPUT_BUFFER_PLANE
    memset(&plane, 0, sizeof(plane));

    stV4lBuf.index = index;
    stV4lBuf.type = mOutBufType;

    stV4lBuf.timestamp.tv_sec = -1;
    stV4lBuf.timestamp.tv_usec = 0;

    stV4lBuf.memory = mInMemType;

    if (V4L2_TYPE_IS_MULTIPLANAR(mOutBufType)) {
        plane.bytesused = buf_length;
        plane.length = mInputFormat.bufferSize;
        plane.data_offset = 0;
        plane.m.mem_offset = 0;//mInMemType == V4L2_MEMORY_MMAP
        stV4lBuf.m.planes = &plane;
        stV4lBuf.length = DEFAULT_INPUT_BUFFER_PLANE;
    } else {
        stV4lBuf.bytesused = buf_length;
        stV4lBuf.length = mInputFormat.bufferSize;
    }

    ALOGV("%s: VIDIOC_QBUF OUTPUT BEGIN index=%d,len=%d\n", __FUNCTION__, stV4lBuf.index, buf_length);

    result = ioctl(mFd, VIDIOC_QBUF, &stV4lBuf);
    if(result < 0) {
        ALOGE("%s: VIDIOC_QBUF OUTPUT failed, index=%d", __FUNCTION__, index);
        mLock.unlock();
        return UNKNOWN_ERROR;
    }

    mInputBufferMap[index].used = true;
    ALOGV("%s: VIDIOC_QBUF OUTPUT END index=%d,len=%d\n", __FUNCTION__, stV4lBuf.index, buf_length);

    mLock.unlock();

    if(!bInputStreamOn) {
        startInputStream();
    }

    return OK;
}

status_t HwDecoder::dequeueInputBuffer() {
    int result = 0;
    int input_id = -1;
    struct v4l2_buffer stV4lBuf;

    if(!bInputStreamOn || mDecState != RUNNING )
        return OK;
    {
        Mutex::Autolock autoLock(mLock);

        if(!bInputStreamOn || mDecState != RUNNING)
            return OK;

        memset(&stV4lBuf, 0, sizeof(stV4lBuf));
        stV4lBuf.type = mOutBufType;
        stV4lBuf.memory = mInMemType;

        if (V4L2_TYPE_IS_MULTIPLANAR(mOutBufType)) {
            struct v4l2_plane planes[DEFAULT_INPUT_BUFFER_PLANE];
            memset(planes, 0, sizeof(planes));
            stV4lBuf.m.planes = planes;
            stV4lBuf.length = DEFAULT_INPUT_BUFFER_PLANE;
        }

        ALOGV("%s VIDIOC_DQBUF OUTPUT BEGIN", __FUNCTION__);
        result = ioctl(mFd, VIDIOC_DQBUF, &stV4lBuf);
        if(result < 0)
            return UNKNOWN_ERROR;

        if(stV4lBuf.index >= mInputFormat.bufferNum)
            return BAD_INDEX;

        ALOGV("%s VIDIOC_DQBUF OUTPUT END index=%d", __FUNCTION__, stV4lBuf.index);
        if(!mInputBufferMap[stV4lBuf.index].used) {
            ALOGV("%s index=%d, not used", __FUNCTION__, stV4lBuf.index);
        }

        input_id = mInputBufferMap[stV4lBuf.index].input_id;
        mInputBufferMap[stV4lBuf.index].input_id = -1;
        mInputBufferMap[stV4lBuf.index].used = false;
    }

    return OK;
}

status_t HwDecoder::queueOutputBuffer(DecoderBufferInfo* pInfo) {
    int result = 0;
    int32_t fd[DEFAULT_OUTPUT_BUFFER_PLANE];
    uint64_t vaddr[DEFAULT_OUTPUT_BUFFER_PLANE];
    uint64_t paddr[DEFAULT_OUTPUT_BUFFER_PLANE];
    uint32_t offset[DEFAULT_OUTPUT_BUFFER_PLANE];
    int32_t index = -1;

    if(mFetchState != RUNNING || STOPPING == mDecState || FLUSHING == mDecState || RES_CHANGING == mDecState) {
        ALOGV("%s returned", __FUNCTION__);
        return OK;
    }

    ALOGV("%s BEGIN id=%d", __FUNCTION__, pInfo->mDBInfoId);
    mLock.lock();

    if(mFetchState != RUNNING || STOPPING == mDecState || FLUSHING == mDecState || RES_CHANGING == mDecState) {
        mLock.unlock();
        ALOGV("%s: returned", __FUNCTION__);
        return OK;
    }

    vaddr[0] = pInfo->mVirtAddr;
    vaddr[1] = pInfo->mVirtAddr;

    paddr[0] = pInfo->mPhysAddr;
    paddr[1] = pInfo->mPhysAddr;

    offset[0] = 0;
    offset[1] = mOutputPlaneSize[0];

    fd[0] = pInfo->mDMABufFd;
    fd[1] = pInfo->mDMABufFd;

    //try to get index
    for(int32_t i = 0; i < mOutputBufferMap.size(); i++) {
        if(pInfo->mPhysAddr == mOutputBufferMap[i].planes[0].paddr) {
            index = i;
            break;
        }
    }

    //index not found
    if(index < 0) {
        for(int32_t i = 0; i < mOutputBufferMap.size(); i++) {
            if(0 == mOutputBufferMap[i].planes[0].paddr) {
                mOutputBufferMap[i].planes[0].fd = fd[0];
                mOutputBufferMap[i].planes[0].vaddr = vaddr[0];
                mOutputBufferMap[i].planes[0].paddr = paddr[0];
                mOutputBufferMap[i].planes[0].offset = offset[0];

                mOutputBufferMap[i].planes[1].fd = fd[1];
                mOutputBufferMap[i].planes[1].vaddr = vaddr[1];
                mOutputBufferMap[i].planes[1].paddr = paddr[1];
                mOutputBufferMap[i].planes[1].offset = offset[1];

                mOutputBufferMap[i].buf_id = pInfo->mDBInfoId;
                mOutputBufferMap[i].used = false;
                index = i;
                break;
            }
        }
    }

    if(index < 0) {
        ALOGE("%s: could not create index", __FUNCTION__);
        mLock.unlock();
        return UNKNOWN_ERROR;
    }

    if(mOutputBufferMap[index].used) {
        ALOGV("%s: used,index=%d, pInfo->mDBInfoId=%d", __FUNCTION__, index, pInfo->mDBInfoId);
    }

    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane planes[DEFAULT_OUTPUT_BUFFER_PLANE];
    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(&planes, 0, sizeof(planes));

    stV4lBuf.index = index;
    stV4lBuf.type = mCapBufType;
    stV4lBuf.memory = mOutMemType;
    stV4lBuf.flags = 0;

    if (V4L2_TYPE_IS_MULTIPLANAR(mCapBufType)) {
        if (mOutMemType == V4L2_MEMORY_DMABUF) {
            planes[0].m.fd = fd[0];
            planes[1].m.fd = fd[1];
        } else if(mOutMemType == V4L2_MEMORY_USERPTR) {
            planes[0].m.userptr = vaddr[0];
            planes[1].m.userptr = vaddr[1];
        }

        planes[0].length = mOutputBufferMap[index].planes[0].size;
        planes[1].length = mOutputBufferMap[index].planes[1].size;

        planes[0].data_offset = mOutputBufferMap[index].planes[0].offset;
        planes[1].data_offset = mOutputBufferMap[index].planes[1].offset;

        stV4lBuf.m.planes = &planes[0];
        stV4lBuf.length = DEFAULT_OUTPUT_BUFFER_PLANE;
    } else {
        if (mOutMemType == V4L2_MEMORY_USERPTR) {
            stV4lBuf.length = mOutputPlaneSize[0];
            stV4lBuf.m.userptr = (unsigned long)vaddr[0];
        } else if (mOutMemType == V4L2_MEMORY_DMABUF) {
            stV4lBuf.length = mOutputPlaneSize[0];
            stV4lBuf.m.fd = fd[0];
        }
    }

    ALOGV("%s: VIDIOC_QBUF CAPTURE BEGIN index=%d bufId=%d fd=%d\n", __FUNCTION__, index, pInfo->mDBInfoId, fd[0]);

    result = ioctl(mFd, VIDIOC_QBUF, &stV4lBuf);
    if(result < 0) {
        ALOGE("%s: VIDIOC_QBUF CAPTURE failed, index=%d, result=%d", __FUNCTION__, index, result);
        mLock.unlock();
        return UNKNOWN_ERROR;
    }

    ALOGV("%s: VIDIOC_QBUF CAPTURE END index=%d bufId=%d\n", __FUNCTION__, index, pInfo->mDBInfoId);
    SetDecoderBufferState(pInfo->mDBInfoId, true);

    mOutputBufferUsed++;
    mOutputBufferMap[index].used = true;
    mLock.unlock();

    if(!bOutputStreamOn) {
        startOutputStream();
    }
    return OK;
}

status_t HwDecoder::startInputStream() {
    Mutex::Autolock autoLock(mLock);
    if(!bInputStreamOn) {
        enum v4l2_buf_type buf_type = mOutBufType;
        if(0 == ioctl(mFd, VIDIOC_STREAMON, &buf_type)) {
            bInputStreamOn = true;
            ALOGV("%s OK", __FUNCTION__);
        }
    }
    return OK;
}

status_t HwDecoder::stopInputStream() {
    Mutex::Autolock autoLock(mLock);
    if(bInputStreamOn) {
        enum v4l2_buf_type buf_type = mOutBufType;
        if(0 == ioctl(mFd, VIDIOC_STREAMOFF, &buf_type)) {
            bInputStreamOn = false;
            ALOGV("%s OK", __FUNCTION__);
        }
    }

    for (size_t i = 0; i < mInputBufferMap.size(); i++) {
        mInputBufferMap[i].used = false;
        mInputBufferMap[i].input_id = -1;
    }

    bInputStreamOn = false;
    return OK;
}

status_t HwDecoder::startOutputStream() {
    Mutex::Autolock autoLock(mLock);
    if(!bOutputStreamOn) {
        enum v4l2_buf_type buf_type = mCapBufType;
        if(0 == ioctl(mFd, VIDIOC_STREAMON, &buf_type)) {
            bOutputStreamOn = true;
            ALOGV("%s OK", __FUNCTION__);
        }
    }
    return OK;
}

status_t HwDecoder::stopOutputStream() {
    Mutex::Autolock autoLock(mLock);
    enum v4l2_buf_type buf_type = mCapBufType;
    (void)ioctl(mFd, VIDIOC_STREAMOFF, &buf_type);
    bOutputStreamOn = false;

    // return capture buffer to component
    for(int32_t i = 0; i < mOutputBufferMap.size(); i++) {
        if(mOutputBufferMap[i].planes[0].paddr > 0 && mOutputBufferMap[i].used) {
            SetDecoderBufferState(mOutputBufferMap[i].buf_id, false);
            mOutputBufferMap[i].used = false;
            ALOGV("%s: return capture buffer %d ", __FUNCTION__, mOutputBufferMap[i].buf_id);
        }
    }

    mOutputBufferUsed = 0;

    return OK;
}

status_t HwDecoder::dequeueOutputBuffer() {
    int result = 0;
    int bufsize = 0;
    struct v4l2_buffer stV4lBuf;
    struct v4l2_plane planes[DEFAULT_OUTPUT_BUFFER_PLANE];

    if (!bOutputStreamOn || mDecState != RUNNING)
        return OK;

    Mutex::Autolock autoLock(mLock);

    if (!bOutputStreamOn || mDecState != RUNNING)
        return OK;

    memset(&stV4lBuf, 0, sizeof(stV4lBuf));
    memset(planes, 0, sizeof(planes));
    stV4lBuf.type = mCapBufType;
    stV4lBuf.memory = mOutMemType;

    if (V4L2_TYPE_IS_MULTIPLANAR(mCapBufType)) {
        stV4lBuf.m.planes = planes;
        stV4lBuf.length = DEFAULT_OUTPUT_BUFFER_PLANE;
    }

    ALOGV("%s VIDIOC_DQBUF CAPTURE BEGIN", __FUNCTION__);
    result = ioctl(mFd, VIDIOC_DQBUF, &stV4lBuf);
    if (result < 0) {
        ALOGV("%s VIDIOC_DQBUF err=%d", __FUNCTION__, result);
        return UNKNOWN_ERROR;
    }

    if (stV4lBuf.index >= 32 /*mOutputFormat.bufferNum*/) {
        ALOGI("%s: error, index exceed 32", __FUNCTION__);
        return BAD_INDEX;
    }

    if (V4L2_TYPE_IS_MULTIPLANAR(mCapBufType))
        bufsize = stV4lBuf.m.planes[0].bytesused + stV4lBuf.m.planes[1].bytesused;
    else
        bufsize = stV4lBuf.bytesused;

    ALOGV("%s: VIDIOC_DQBUF CAPTURE END index=%d bufsize=%d flags %x",
            __FUNCTION__, stV4lBuf.index, bufsize, stV4lBuf.flags);

    mOutputBufferMap[stV4lBuf.index].used = false;
    mOutputBufferUsed--;

    if (bufsize > 0) {
        dumpStream((void*)(uintptr_t)mOutputBufferMap[stV4lBuf.index].planes[0].vaddr, bufsize, 1);

        notifyDecodeReady(mOutputBufferMap[stV4lBuf.index].buf_id);
    } else {
        returnOutputBufferToDecoder(mOutputBufferMap[stV4lBuf.index].buf_id);
    }

    return OK;
}

status_t HwDecoder::handleDequeueEvent() {
    int result = 0;
    struct v4l2_event event;
    memset(&event, 0, sizeof(struct v4l2_event));

    result = ioctl(mFd, VIDIOC_DQEVENT, &event);
    if(result == 0) {
        switch(event.type) {
            case V4L2_EVENT_SOURCE_CHANGE:
                if(event.u.src_change.changes & V4L2_EVENT_SRC_CH_RESOLUTION) {
                    if(STOPPING != mDecState)
                        handleFormatChanged();
                }
                break;
            case V4L2_EVENT_CODEC_ERROR:
                ALOGE("%s: get V4L2_EVENT_DECODE_ERROR", __FUNCTION__);
                break;
            default:
                ALOGD("%s: get unexpected event type=%x", __FUNCTION__, event.type);
                break;
        }
    }

    return OK;
}

status_t HwDecoder::allocateOutputBuffers() {
    int ret;

    if(mDecState == STOPPING)
        return OK;

    Mutex::Autolock autoLock(mLock);

    if(mDecState == STOPPING)
        return OK;

    ALOGD("%s: mOutputFormat.bufferNum=%d", __FUNCTION__, mOutputFormat.bufferNum);

    mOutputBufferMap.resize(mOutputFormat.bufferNum);

    for (int i = 0; i < mOutputFormat.bufferNum; i++) {
        if (!bNeedPostProcess) {
            do {
                ret = allocateOutputBuffer(i);
            } while (BAD_VALUE == ret);

            if (ret != OK) {
                return ret;
            } else
                continue;
        }
    }

    return OK;
}

status_t HwDecoder::freeOutputBuffers() {
    Mutex::Autolock autoLock(mLock);

    if(mDecoderBuffers.empty())
        return OK;

    for (auto& info : mDecoderBuffers) {
        if (info.mVirtAddr > 0 && info.mCapacity > 0)
        {
            munmap((void*)info.mVirtAddr, info.mCapacity);
        }

        if(info.mDMABufFd > 0)
            close(info.mDMABufFd);
    }

    mDecoderBuffers.clear();

    return OK;
}

status_t HwDecoder::handleFormatChanged() {
    status_t ret = OK;
    Mutex::Autolock autoThreadLock(mThreadLock);
    ALOGV("%s BEGIN\n", __FUNCTION__);
    {
        Mutex::Autolock autoLock(mLock);

        mDecState = RES_CHANGING;
        int result = 0;
        struct v4l2_format format;
        uint32_t pixel_format = 0;
        uint32_t v4l2_pixel_format = 0;
        uint32_t newWidth, newHeight, newBytesperline;
        memset(&format, 0, sizeof(struct v4l2_format));

        format.type = mCapBufType;
        result = ioctl(mFd, VIDIOC_G_FMT, &format);
        if(result < 0)
            return UNKNOWN_ERROR;

        if (V4L2_TYPE_IS_MULTIPLANAR(mCapBufType)) {
            v4l2_pixel_format = format.fmt.pix_mp.pixelformat;
            newWidth = format.fmt.pix_mp.width;
            newHeight = format.fmt.pix_mp.height;
            newBytesperline = format.fmt.pix_mp.plane_fmt[0].bytesperline;
            mOutputPlaneSize[0] = format.fmt.pix_mp.plane_fmt[0].sizeimage;
            mOutputPlaneSize[1] = format.fmt.pix_mp.plane_fmt[1].sizeimage;
            mOutputFormat.bufferSize = mOutputPlaneSize[0] + mOutputPlaneSize[1];
        } else {
            v4l2_pixel_format = format.fmt.pix.pixelformat;
            newWidth = format.fmt.pix.width;
            newHeight = format.fmt.pix.height;
            newBytesperline = format.fmt.pix.bytesperline;
            mOutputPlaneSize[0] = format.fmt.pix.sizeimage;
            mOutputFormat.bufferSize = mOutputPlaneSize[0];
        }

        ret = pDev->GetColorFormatByV4l2(v4l2_pixel_format, &pixel_format);
        if(ret != OK)
            return ret;

        mOutFormat = v4l2_pixel_format;
        mOutputFormat.pixelFormat = static_cast<int>(pixel_format);

#ifdef AMPHION_V4L2
        if(mOutputFormat.pixelFormat == HAL_PIXEL_FORMAT_P010_TILED) {
            bNeedPostProcess = true;
            ALOGV("%s: 10bit video stride=%d", __FUNCTION__, newBytesperline);
        }
#endif

        mOutputFormat.width = Align(newWidth, mFrameAlignW);
        mOutputFormat.height = Align(newHeight, mFrameAlignH);
        mOutputFormat.stride = mOutputFormat.width;

        //for 10bit video, stride is larger than width, should use stride to allocate buffer
        if(mOutputFormat.width < newBytesperline) {
            mOutputFormat.stride = newBytesperline;
        }

        struct v4l2_control ctl;
        memset(&ctl, 0, sizeof(struct v4l2_control));
        ctl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
        result = ioctl(mFd, VIDIOC_G_CTRL, &ctl);
        if(result < 0)
            return UNKNOWN_ERROR;

        mOutputFormat.bufferNum = ctl.value;

#ifdef AMPHION_V4L2
        mOutputFormat.bufferNum += AMPHION_FRAME_PLUS;
#endif
#ifdef HANTRO_V4L2
        mOutputFormat.bufferNum += HANTRO_FRAME_PLUS;
#endif

        struct v4l2_crop crop;
        crop.type = mCapBufType;
        result = ioctl (mFd, VIDIOC_G_CROP, &crop);
        if(result < 0)
            return UNKNOWN_ERROR;

        if(crop.c.width == 0 && crop.c.height == 0) {
            ALOGE("%s flushed return", __FUNCTION__);
            return OK;
        }

        mOutputFormat.rect.right = crop.c.width;
        mOutputFormat.rect.bottom = crop.c.height;
        mOutputFormat.rect.top = crop.c.top;
        mOutputFormat.rect.left = crop.c.left;
    }

    ALOGD("%s w=%d,h=%d, bufferNum=%d, buffer size[0]=%d,size[1]=%d, pixelFormat=0x%x",
        __FUNCTION__, mOutputFormat.width, mOutputFormat.height,
        mOutputFormat.bufferNum, mOutputPlaneSize[0], mOutputPlaneSize[1], mOutputFormat.pixelFormat);

    if (mFetchState == RUNNING) {
        destroyFetchThread();
    }

    if (bOutputStreamOn) {
        stopOutputStream();
        mOutputBufferMap.clear();
    }

    ret = SetOutputFormats();
    if(ret != OK) {
        ALOGE("%s: SetOutputFormats failed", __FUNCTION__);
        return ret;
    }

    onOutputFormatChanged();

    ALOGV("%s END", __FUNCTION__);
    return OK;
}

status_t HwDecoder::Flush() {
    ALOGV("%s", __FUNCTION__);
    int pre_state;
    {
        Mutex::Autolock autoLock(mLock);
        pre_state = mDecState;
        if(mDecState != STOPPING)
            mDecState = FLUSHING;
    }

    status_t ret = UNKNOWN_ERROR;
    ret = stopInputStream();
    if(ret != OK)
        return ret;

    ret = stopOutputStream();
    if(ret != OK)
        return ret;

    Mutex::Autolock autoLock(mLock);
    mDecState = pre_state;

    ALOGV("%s end", __FUNCTION__);
    return ret;
}

status_t HwDecoder::Stop() {
    ALOGV("%s", __FUNCTION__);
    status_t ret = UNKNOWN_ERROR;
    Mutex::Autolock autoThreadLock(mThreadLock);
    {
        Mutex::Autolock autoLock(mLock);
        mDecState = STOPPING;
    }
    ret = Flush();

    ret |= destroyPollThread();

    ret |= destroyFetchThread();

    ret |= destroyInputBuffers();

    ret |= destroyOutputBuffers();

    Mutex::Autolock autoLock(mLock);
    if (OK == ret)
        mDecState = STOPPED;

    if(pDev != NULL)
        pDev->ResetDecoder();

    return OK;
}

status_t HwDecoder::Destroy() {
    ALOGV("%s", __FUNCTION__);

    if(mDecState != STOPPED) {
        Stop();
        mDecState = STOPPED;
    }

    Mutex::Autolock autoLock(mLock);

    if(mFd > 0) {
        pDev->Close();
        ALOGV("pDev Closed %d",mFd);
        mFd = 0;
    }

    if(pDev != NULL)
        delete pDev;
    pDev = NULL;

    return OK;
}

void HwDecoder::dumpStream(void *src, size_t srcSize, int32_t id) {
    char value[PROPERTY_VALUE_MAX];
    int fdSrc = -1;

    if ((src == NULL) || (srcSize == 0))
        return;

    property_get("vendor.rw.camera.ext.test", value, "false");
    if (!strcmp(value, "false"))
        return;

    ALOGI("%s: src size %zu, id %d", __FUNCTION__, srcSize, id);

    char srcFile[32];
    snprintf(srcFile, 32, "/data/%d-vpu-dump.data", id);
    srcFile[31] = 0;

    fdSrc = open(srcFile, O_CREAT|O_APPEND|O_WRONLY, S_IRWXU|S_IRWXG);

    if (fdSrc < 0) {
        ALOGW("%s: file open error, srcFile: %s, fd %d", __FUNCTION__, srcFile, fdSrc);
        return;
    }

    write(fdSrc, src, srcSize);

    close(fdSrc);

    return;
}

status_t HwDecoder::onOutputFormatChanged() {
    ALOGI("%s: New Output format(pixelfmt 0x%x, request buffer num %d, w*h=%d x %d, crop(%d %d %d %d))",
        __FUNCTION__, mOutputFormat.pixelFormat, mOutputFormat.bufferNum,
        mOutputFormat.width, mOutputFormat.height,
        mOutputFormat.rect.left, mOutputFormat.rect.top,
        mOutputFormat.rect.right, mOutputFormat.rect.bottom);

    status_t err;
    for (auto& info : mDecoderBuffers) {
        if (info.bInUse)
            info.bInUse = false;
    }

    err = freeOutputBuffers();
    if (err) {
        return err;
    }

    err = allocateOutputBuffers();
    if (err) {
        return err;
    }

    {
        Mutex::Autolock autoLock(mLock);
        if(mDecState == STOPPING)
            return OK;

        mDecState = RUNNING;

        int result = 0;
        struct v4l2_requestbuffers reqbufs;
        memset(&reqbufs, 0, sizeof(reqbufs));
        reqbufs.count = 32;
        reqbufs.type = mCapBufType;
        reqbufs.memory = mOutMemType;

        result = ioctl(mFd, VIDIOC_REQBUFS, &reqbufs);
        if(result != 0) {
            return UNKNOWN_ERROR;
        }
        if (!bNeedPostProcess)
            mOutputBufferMap.resize(32);
    }

    createFetchThread();

    return OK;
}

status_t HwDecoder::allocateOutputBuffer(int bufId) {
    int fd = 0;
    uint64_t phys_addr = 0;
    uint64_t virt_addr = 0;

    fd = IMXAllocMem(mOutputFormat.bufferSize);
    if (fd <= 0) {
        ALOGE("%s: Ion allocate failed bufId=%d,size=%d", __FUNCTION__, bufId, mOutputFormat.bufferSize);
        return BAD_VALUE;
    }

    int ret = IMXGetBufferAddr(fd, mOutputFormat.bufferSize, phys_addr, false);
    if (ret != 0) {
        ALOGE("%s: DmaBuffer getPhys failed", __FUNCTION__);
        return BAD_VALUE;
    }

    ret = IMXGetBufferAddr(fd, mOutputFormat.bufferSize, virt_addr, true);
    if (ret != 0) {
        ALOGE("%s: DmaBuffer getVaddrs failed", __FUNCTION__);
        return BAD_VALUE;
    }

    DecoderBufferInfo mInfo;
    memset(&mInfo, 0, sizeof(DecoderBufferInfo));
    mInfo.mDBInfoId = bufId;
    mInfo.mDMABufFd = fd;
    mInfo.mPhysAddr = phys_addr;
    mInfo.mVirtAddr = virt_addr;
    mInfo.mCapacity = mOutputFormat.bufferSize;
    mInfo.bInUse = false;
    mDecoderBuffers.push_back(std::move(mInfo));

    ALOGV("%s: Allocated fd=%d phys_addr=%p vaddr=%p", __FUNCTION__, fd, (void*)phys_addr, (void*)virt_addr);

    return OK;
}

void HwDecoder::SetDecoderBufferState(int32_t bufId, bool state) {
    DecoderBufferInfo* pInfo = getDecoderBufferById(bufId);
    if (pInfo) {
        pInfo->bInUse = state;
    }
}

DecoderBufferInfo* HwDecoder::getDecoderBufferById(int32_t bufId) {
    if (bufId < 0 || bufId >= static_cast<int32_t>(mDecoderBuffers.size())) {
        ALOGE("%s: invalid bufId %d", __FUNCTION__, bufId);
        return nullptr;
    }
    auto bufIter = std::find_if(mDecoderBuffers.begin(), mDecoderBuffers.end(),
                                    [bufId](const DecoderBufferInfo& db) {
                                        return db.mDBInfoId == bufId;
                                    });

    if (bufIter == mDecoderBuffers.end()) {
        ALOGE("%s: bufId %d not found", __FUNCTION__, bufId);
        return nullptr;
    }
    return &(*bufIter);
}

DecoderBufferInfo* HwDecoder::getFreeDecoderBuffer() {
    Mutex::Autolock autoLock(mLock);
    auto bufIter = std::find_if(mDecoderBuffers.begin(), mDecoderBuffers.end(),
                                    [](const DecoderBufferInfo& db) {
                                        return db.bInUse == false;
                                    });

    if (bufIter == mDecoderBuffers.end()) {
        ALOGV("%s: no free DecoderBuffer available", __FUNCTION__);
        return nullptr;
    }

    ALOGV("%s: get free bufId %d", __FUNCTION__, bufIter->mDBInfoId);
    return &(*bufIter);
}

void HwDecoder::returnOutputBufferToDecoder(int32_t bufId) {
    DecoderBufferInfo *mDBInfo = getDecoderBufferById(bufId);
    if (mDBInfo) {
        ALOGV("%s: bufId %d", __FUNCTION__, bufId);
        mDBInfo->bInUse = false;
    } else {
        ALOGE("%s: invalid bufId %d", __FUNCTION__, bufId);
    }
}

void HwDecoder::notifyDecodeReady(int32_t mOutbufId) {
    DecoderBufferInfo *info = getDecoderBufferById(mOutbufId);
    if (!info) {
        /* notify error */
        ALOGE("%s: wrong mOutbufId %d", __FUNCTION__, mOutbufId);
        return;
    }

    if (!info->bInUse) {
        ALOGE("%s  error state, expect true but get %d", __FUNCTION__, info->bInUse);
        return;
    }

    mData.data = (uint8_t *)info->mVirtAddr;
    mData.width = mOutputFormat.width;
    mData.height = mOutputFormat.height;

    SetDecoderBufferState(mOutbufId, false);

    mFramesSignal->notify_all();
}

DecodedData HwDecoder::exportDecodedBuf() {
    return mData;
}

}

