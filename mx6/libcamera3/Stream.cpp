/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <hardware/camera3.h>
#include <hardware/gralloc.h>
#include <system/graphics.h>
#include <utils/Mutex.h>
#include <utils/StrongPointer.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <sync/sync.h>

//#define LOG_NDEBUG 0
#include <cutils/log.h>
#include "Camera.h"
#include "Stream.h"
#include "CameraUtils.h"

Stream::Stream(int id, camera3_stream_t *s, Camera* camera)
  : mReuse(false),
    mPreview(false),
    mJpeg(false),
    mCallback(false),
    mId(id),
    mStream(s),
    mType(s->stream_type),
    mWidth(s->width),
    mHeight(s->height),
    mFormat(s->format),
    mUsage(0),
    mNumBuffers(0),
    mRegistered(false),
    mCamera(camera)
{
    if (s->format == HAL_PIXEL_FORMAT_BLOB) {
        ALOGI("%s create capture stream", __func__);
        mJpeg = true;
        mFormat = mCamera->getPicturePixelFormat();
        s->format = mFormat;

        mUsage = CAMERA_GRALLOC_USAGE_JPEG;
        mNumBuffers = NUM_CAPTURE_BUFFER;
    }
    else if (s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
        ALOGI("%s create preview stream", __func__);
        mFormat = mCamera->getPreviewPixelFormat();
        s->format = mFormat;
        mUsage = CAMERA_GRALLOC_USAGE;
        mNumBuffers = NUM_PREVIEW_BUFFER;
        mPreview = true;
        if (s->usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) {
            ALOGI("%s create video recording stream", __func__);
            mPreview = false;
        }
    }
    else {
        ALOGI("create callback stream", __func__);
        mCallback = true;
        mUsage = CAMERA_GRALLOC_USAGE;
        mNumBuffers = NUM_PREVIEW_BUFFER;
    }

    s->usage |= mUsage;
    s->max_buffers = mNumBuffers;
    mNumBuffers += 1;

    ALOGI("stream: w:%d, h:%d, format:0x%x, usage:0x%x, buffers:%d",
          s->width, s->height, s->format, s->usage, mNumBuffers);
    mIpuFd = open("/dev/mxc_ipu", O_RDWR, 0);

    for (uint32_t i=0; i<MAX_STREAM_BUFFERS; i++) {
        mBuffers[i] = NULL;
    }

    mJpegBuilder = new JpegBuilder();
}

Stream::Stream(Camera* camera)
  : mReuse(false),
    mPreview(false),
    mJpeg(false),
    mId(-1),
    mStream(NULL),
    mType(-1),
    mWidth(0),
    mHeight(0),
    mFormat(0),
    mUsage(0),
    mNumBuffers(0),
    mRegistered(false),
    mCamera(camera)
{
    mIpuFd = open("/dev/mxc_ipu", O_RDWR, 0);
    for (uint32_t i=0; i<MAX_STREAM_BUFFERS; i++) {
        mBuffers[i] = NULL;
    }
}

Stream::~Stream()
{
    android::Mutex::Autolock al(mLock);
    if (mIpuFd > 0) {
        close(mIpuFd);
        mIpuFd = -1;
    }
}

int32_t Stream::processJpegBuffer(StreamBuffer& src,
                                  sp<Metadata> meta)
{
    int32_t ret = 0;
    int32_t encodeQuality = 100, thumbQuality = 100;
    int32_t thumbWidth, thumbHeight;
    JpegParams *mainJpeg = NULL, *thumbJpeg = NULL;
    void *rawBuf = NULL, *thumbBuf = NULL;
    size_t imageSize = 0;

    StreamBuffer* dstBuf = mCurrent;
    sp<Stream>& srcStream = src.mStream;
    if (dstBuf == NULL || srcStream == NULL) {
        ALOGE("%s invalid param", __FUNCTION__);
        return BAD_VALUE;
    }

    sp<MemoryHeapBase> rawFrame(
                    new MemoryHeapBase(src.mSize, 0, "rawFrame"));
    rawBuf = rawFrame->getBase();
    if (rawBuf == MAP_FAILED) {
        ALOGE("%s new MemoryHeapBase failed", __FUNCTION__);
        return BAD_VALUE;
    }

    sp<MemoryHeapBase> thumbFrame(
                new MemoryHeapBase(src.mSize, 0, "thumbFrame"));
    thumbBuf = thumbFrame->getBase();
    if (thumbBuf == MAP_FAILED) {
        ALOGE("%s new MemoryHeapBase failed", __FUNCTION__);
        return BAD_VALUE;
    }

    ret = meta->getJpegQuality(encodeQuality);
    if (ret != NO_ERROR) {
        ALOGE("%s getJpegQuality failed", __FUNCTION__);
        return BAD_VALUE;
    }

    if ((encodeQuality < 0) || (encodeQuality > 100)) {
        encodeQuality = 100;
    }

    ret = meta->getJpegThumbQuality(thumbQuality);
    if (ret != NO_ERROR) {
        ALOGE("%s getJpegThumbQuality failed", __FUNCTION__);
        return BAD_VALUE;
    }

    if ((thumbQuality < 0) || (thumbQuality > 100)) {
        thumbQuality = 100;
    }

    mainJpeg = new JpegParams((uint8_t *)src.mVirtAddr,
                       (uint8_t *)src.mPhyAddr,
                       src.mSize, (uint8_t *)rawBuf,
                       src.mSize, encodeQuality,
                       srcStream->mWidth, srcStream->mHeight,
                       srcStream->mWidth, srcStream->mHeight,
                       srcStream->format());

    ret = meta->getJpegThumbSize(thumbWidth, thumbHeight);
    if (ret != NO_ERROR) {
        ALOGE("%s getJpegThumbSize failed", __FUNCTION__);
        goto err_out;
    }

    if ((thumbWidth > 0) && (thumbHeight > 0)) {
        int thumbSize   = 0;
        int thumbFormat = convertPixelFormatToV4L2Format(srcStream->format());
        switch (thumbFormat) {
            case v4l2_fourcc('N', 'V', '1', '2'):
                thumbSize = thumbWidth * thumbHeight * 3 / 2;
                break;

            case v4l2_fourcc('Y', 'U', '1', '2'):
                thumbSize = thumbWidth * thumbHeight * 3 / 2;
                break;

            case v4l2_fourcc('Y', 'U', 'Y', 'V'):
                thumbSize = thumbWidth * thumbHeight * 2;
                break;

            default:
                ALOGE("Error: %s format not supported", __FUNCTION__);
                goto err_out;
        }
        thumbSize = src.mSize;
        thumbJpeg = new JpegParams((uint8_t *)src.mVirtAddr,
                           (uint8_t *)src.mPhyAddr,
                           src.mSize,
                           (uint8_t *)thumbBuf,
                           thumbSize,
                           thumbQuality,
                           srcStream->mWidth,
                           srcStream->mHeight,
                           thumbWidth,
                           thumbHeight,
                           srcStream->format());
    }

    mJpegBuilder->prepareImage(&src);
    ret = mJpegBuilder->encodeImage(mainJpeg, thumbJpeg);
    if (ret != NO_ERROR) {
        ALOGE("%s encodeImage failed", __FUNCTION__);
        goto err_out;
    }

    imageSize = mJpegBuilder->getImageSize();
    ret = mJpegBuilder->buildImage(dstBuf);
    if (ret != NO_ERROR) {
        ALOGE("%s buildImage failed", __FUNCTION__);
        goto err_out;
    }

err_out:
    if (mainJpeg) {
        delete mainJpeg;
    }

    if (thumbJpeg) {
        delete thumbJpeg;
    }

    return ret;
}

int32_t Stream::processBufferWithIPU(StreamBuffer& src)
{
    ALOGV("%s", __func__);
    sp<Stream>& device = src.mStream;
    if (device == NULL) {
        ALOGE("%s invalid device stream", __func__);
        return 0;
    }

    StreamBuffer* out = mCurrent;
    if (out == NULL || out->mBufHandle == NULL) {
        ALOGE("%s invalid buffer handle", __func__);
        return 0;
    }

    struct ipu_task mTask;
    memset(&mTask, 0, sizeof(mTask));

    mTask.input.width = device->mWidth;
    mTask.input.height = device->mHeight;
    mTask.input.crop.pos.x = 0;
    mTask.input.crop.pos.y = 0;
    mTask.input.crop.w = device->mWidth;
    mTask.input.crop.h = device->mHeight;
    mTask.input.format = convertPixelFormatToV4L2Format(device->mFormat);
    mTask.input.paddr = src.mPhyAddr;

    if (!mCallback) {
        mTask.output.format = convertPixelFormatToV4L2Format(mFormat);
    }
    else {
        mTask.output.format = convertPixelFormatToV4L2Format(mFormat, true);
    }
    mTask.output.width = mWidth;
    mTask.output.height = mHeight;
    mTask.output.crop.pos.x = 0;
    mTask.output.crop.pos.y = 0;
    mTask.output.crop.w = mWidth;
    mTask.output.crop.h = mHeight;
    mTask.output.rotate = 0;
    mTask.output.paddr = out->mPhyAddr;

    int32_t ret = IPU_CHECK_ERR_INPUT_CROP;
    while(ret != IPU_CHECK_OK && ret > IPU_CHECK_ERR_MIN) {
        ret = ioctl(mIpuFd, IPU_CHECK_TASK, &mTask);
        ALOGV("%s:%d, IPU_CHECK_TASK ret=%d", __FUNCTION__, __LINE__, ret);
        switch(ret) {
            case IPU_CHECK_OK:
                break;
            case IPU_CHECK_ERR_SPLIT_INPUTW_OVER:
                mTask.input.crop.w -= 8;
                break;
            case IPU_CHECK_ERR_SPLIT_INPUTH_OVER:
                mTask.input.crop.h -= 8;
                break;
            case IPU_CHECK_ERR_SPLIT_OUTPUTW_OVER:
                mTask.output.crop.w -= 8;
                break;
            case IPU_CHECK_ERR_SPLIT_OUTPUTH_OVER:
                mTask.output.crop.h -= 8;;
                break;
            default:
                ALOGE("%s:%d, IPU_CHECK_TASK ret=%d", __FUNCTION__, __LINE__, ret);
                return ret;
        }
    }

    ret = ioctl(mIpuFd, IPU_QUEUE_TASK, &mTask);
    if(ret < 0) {
        ALOGE("%s:%d, IPU_QUEUE_TASK failed %d", __FUNCTION__, __LINE__ ,ret);
        return ret;
    }

    return ret;
}

static void bufferDump(StreamBuffer *frame, bool in)
{
    // for test code
    char value[100];
    char name[100];
    memset(value, 0, sizeof(value));
    bool vflg = false;
    static int dump_num = 1;
    property_get("rw.camera.test", value, "");
    if (strcmp(value, "true") == 0)
        vflg = true;

    if (vflg) {
        FILE *pf = NULL;
        memset(name, 0, sizeof(name));
        snprintf(name, 100, "/data/dump/camera_dump_%s_%d.data",
                   in ? "in" : "out", dump_num++);
        pf = fopen(name, "wb");
        if (pf == NULL) {
            ALOGI("open %s failed", name);
        }
        else {
            ALOGV("write yuv data");
            fwrite(frame->mVirtAddr, frame->mSize, 1, pf);
            fclose(pf);
        }
    }
}

int32_t Stream::processBufferWithGPU(StreamBuffer& src)
{
    sp<Stream>& device = src.mStream;
    if (device == NULL) {
        ALOGE("%s invalid device stream", __func__);
        return 0;
    }

    StreamBuffer* out = mCurrent;
    if (out == NULL || out->mBufHandle == NULL) {
        ALOGE("%s invalid buffer handle", __func__);
        return 0;
    }

    void* g2dHandle = device->getG2dHandle();
    if (g2dHandle == NULL) {
        ALOGE("%s invalid g2d handle", __func__);
        return 0;
    }
    struct g2d_buf s_buf, d_buf;
    s_buf.buf_paddr = src.mPhyAddr;
    s_buf.buf_vaddr = src.mVirtAddr;
    d_buf.buf_paddr = out->mPhyAddr;
    d_buf.buf_vaddr = out->mVirtAddr;
    g2d_copy(g2dHandle, &d_buf, &s_buf, out->mSize);
    g2d_finish(g2dHandle);

    bufferDump(&src, true);
    bufferDump(out, false);

    return 0;
}

int32_t Stream::processFrameBuffer(StreamBuffer& src,
                                   sp<Metadata> meta)
{
    ALOGV("%s", __func__);
    sp<Stream>& device = src.mStream;
    if (device == NULL) {
        ALOGE("%s invalid device stream", __func__);
        return 0;
    }

    int32_t ret = 0;
    // IPU can't support NV12->NV21 conversion.
    if ((mWidth != device->mWidth) || (mHeight != device->mHeight) ||
         (mFormat != device->mFormat) || ((mFormat == device->mFormat) &&
         (mCallback && (mFormat != HAL_PIXEL_FORMAT_YCbCr_420_SP)))) {
        ret = processBufferWithIPU(src);
    }
    else {
        ret = processBufferWithGPU(src);
    }

    return ret;
}

int32_t Stream::processCaptureBuffer(StreamBuffer& src,
                                     sp<Metadata> meta)
{
    int32_t res = 0;

    ALOGV("%s", __func__);
    StreamBuffer* out = mCurrent;
    if (out == NULL || out->mBufHandle == NULL) {
        ALOGE("%s invalid buffer handle", __func__);
        return 0;
    }

    if (out->mAcquireFence != -1) {
        res = sync_wait(out->mAcquireFence, CAMERA_SYNC_TIMEOUT);
        if (res == -ETIME) {
            ALOGE("%s: Timeout waiting on buffer acquire fence",
                    __func__);
            return res;
        } else if (res) {
            ALOGE("%s: Error waiting on buffer acquire fence: %s(%d)",
                    __func__, strerror(-res), res);
            ALOGV("fence id:%d", out->mAcquireFence);
        }
        close(out->mAcquireFence);
    }

    if (mJpeg) {
        mJpegBuilder->reset();
        mJpegBuilder->setMetadata(meta);

        res = processJpegBuffer(src, meta);
        mJpegBuilder->setMetadata(NULL);
    }
    else {
        res = processFrameBuffer(src, meta);
    }

    return res;
}

int Stream::getType()
{
    return mType;
}

bool Stream::isInputType()
{
    return mType == CAMERA3_STREAM_INPUT ||
        mType == CAMERA3_STREAM_BIDIRECTIONAL;
}

bool Stream::isOutputType()
{
    return mType == CAMERA3_STREAM_OUTPUT ||
        mType == CAMERA3_STREAM_BIDIRECTIONAL;
}

bool Stream::isRegistered()
{
    return mRegistered;
}

bool Stream::isValidReuseStream(int id, camera3_stream_t *s)
{
    if (id != mId) {
        ALOGE("%s:%d: Invalid camera id for reuse. Got %d expect %d",
                __func__, mId, id, mId);
        return false;
    }

    if (s != mStream || s->stream_type != mType) {
        ALOGE("%s:%d: Invalid stream handle for reuse. Got %p expect %p",
                __func__, mId, s, mStream);
        return false;
    }

    if (s->width != mWidth || s->height != mHeight || s->format != mFormat) {
        ALOGE("%s:%d: Mismatched reused stream."
              "Got w:%d, h:%d, f:%d expect w:%d, h:%d, f:%d",
                __func__, mId, s->width, s->height, s->format,
                mWidth, mHeight, mFormat);
        return false;
    }

    return true;
}

void Stream::dump(int fd)
{
    android::Mutex::Autolock al(mLock);

    dprintf(fd, "Stream ID: %d (%p)\n", mId, mStream);
    dprintf(fd, "Stream Type: (%d)\n", mType);
    dprintf(fd, "Width: %d Height: %d\n", mWidth, mHeight);
    dprintf(fd, "Stream Format: (%d)", mFormat);
    // ToDo: prettyprint usage mask flags
    dprintf(fd, "Gralloc Usage Mask: %d\n", mUsage);
    dprintf(fd, "Buffers Registered: %s\n", mRegistered ? "true" : "false");
    dprintf(fd, "Number of Buffers: %d\n", mNumBuffers);
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        dprintf(fd, "Buffer %d %d : %p\n", i, mNumBuffers,
                mBuffers[i]->mBufHandle);
    }
}

