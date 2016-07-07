/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
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

#include "MJPGStream.h"

unsigned char* VPUptr;
int32_t mVPUBuffersIndex=0;

static u8 g_hufTab[] = { \
    0xff, 0xc4, 0x00, 0x1f,
          0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00,
          0xb5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00,
          0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51,
          0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52,
          0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26,
          0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47,
          0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67,
          0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87,
          0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
          0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
          0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
          0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
          0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xc4, 0x00, 0x1f, 0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01,
          0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
          0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04,
          0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04,
          0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14,
          0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16,
          0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
          0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56,
          0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76,
          0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94,
          0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2,
          0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
          0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
          0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };

#define MAX_SERCH_SIZE (u32)(64*1024)

typedef struct tagSecHead {
    u8 secStart;
    u8 secID;
    u8 secLenH;
    u8 secLenL;
}TSecHead;
#define SEC_START   0xff

#define ID_SOI  0xd8
#define ID_SOF0 0xc0
#define ID_DHT  0xc4
#define ID_SOS  0xda

static int SearchHufTabPos(u8 *pSrc, u32 nSrcSize, bool *pbExist, u32 *pdwExptDHTPos) {
    u32 dwSearchSize = (MAX_SERCH_SIZE < nSrcSize) ? MAX_SERCH_SIZE : nSrcSize;
    TSecHead tSecHead;
    u32 pos = 0;
    bool bSOF0Found = false;
    u32 dwSecSize = 0;
    u32 dwExpectedHufTabPos = 0;

    if((pSrc == NULL) || (nSrcSize < 4) || (pbExist == NULL) || (pdwExptDHTPos == NULL)) {
        ALOGE("SearchHufTabPos, para err");
        return -1;
    }

    *pbExist = false;

    if( ! ((pSrc[0] == SEC_START) && (pSrc[1] == ID_SOI)) ) {
        return -1;
    }
    pos = 2;

    while(pos + 4 <= dwSearchSize) {
        tSecHead.secStart = pSrc[pos++];
        tSecHead.secID = pSrc[pos++];
        tSecHead.secLenH = pSrc[pos++];
        tSecHead.secLenL = pSrc[pos++];

        if(tSecHead.secStart != SEC_START) {
            ALOGE("SearchHufTabPos, error format, no SEC_START 0x%x in pos 0x%x", SEC_START, pos - 4);
            return -1;
        }

        ALOGI("secID 0x%x at sec pos 0x%x", tSecHead.secID, pos-4);

        if(tSecHead.secID == ID_SOS) {
            ALOGI("SearchHufTabPos, ID_SOS 0x%x found at sec pos 0x%x, break", ID_SOS, pos-4);
            break;
        }

        if(tSecHead.secID == ID_SOF0) {
            ALOGI("SearchHufTabPos, ID_SOF0 0x%x found at sec pos 0x%x", ID_SOF0, pos-4);
            bSOF0Found = true;

        }

        if(tSecHead.secID == ID_DHT) {
            ALOGI("SearchHufTabPos, huf tab found");
            *pbExist = true;
            return 0;
        }

        dwSecSize = (tSecHead.secLenH << 8) | tSecHead.secLenL;
        ALOGI("secID size 0x%x, h 0x%x, l 0x%x", dwSecSize, tSecHead.secLenH, tSecHead.secLenL);

        if(dwSecSize < 2) {
            ALOGE("SearchHufTabPos, err sec size %d < 2, at pos 0x%x", dwSecSize, pos-2);
            return -1;
        }
        pos += dwSecSize - 2;
        ALOGI("pos set to 0x%x", pos);

        if(bSOF0Found)
            dwExpectedHufTabPos = pos;
    }

    if(bSOF0Found == false) {
        ALOGI("SearchHufTabPos, err, no SOF0 found");
        return -1;
    }

    *pdwExptDHTPos = dwExpectedHufTabPos;
    ALOGI("SearchHufTabPos, huf not found, expected pos 0x%x", dwExpectedHufTabPos);

    return 0;

}

static int CheckAndInsertHufTab(u8 *pDst, u8 *pSrc, u32 nDstSize, u32 nSrcSize) {
    bool bExist = 0;
    u32 dwExptDHTPos = 0;
    int ret;
    ret = SearchHufTabPos(pSrc, nSrcSize, &bExist, &dwExptDHTPos);
    if(ret) {
        ALOGE("CheckAndInsertHufTab, SearchHufTabPos failed, ret %d", ret);
        return ret;
    }

    u32 nCopySize = (nDstSize < nSrcSize) ? nDstSize : nSrcSize;
    if(bExist) {
        memcpy(pDst, pSrc, nCopySize);
        return 0;
    }

    //insert huf tab
    if(dwExptDHTPos + sizeof(g_hufTab) > nCopySize) {
        ALOGE("%s, dwExptDHTPos (%d) + sizeof(g_hufTab) (%d) > nCopySize (%d)",
                __FUNCTION__, dwExptDHTPos, sizeof(g_hufTab), nCopySize);
        return -1;
    }

    memcpy(pDst, pSrc, dwExptDHTPos);
    memcpy(pDst + dwExptDHTPos, g_hufTab, sizeof(g_hufTab));
    u32 dwLeftSize = nCopySize - (dwExptDHTPos + sizeof(g_hufTab));
    memcpy(pDst + dwExptDHTPos + sizeof(g_hufTab), pSrc + dwExptDHTPos, dwLeftSize);

    return 0;
}

MJPGStream::MJPGStream(Camera* device): DMAStream(device), mStreamSize(0)
{
    mVPUHandle = 0;
    memset(&mDecMemInfo,0,sizeof(DecMemInfo));
    memset(&mDecContxt, 0, sizeof(mDecContxt));
}

MJPGStream::~MJPGStream()
{

}

// configure device.
int32_t MJPGStream::onDeviceConfigureLocked()
{
    ALOGI("%s", __func__);

    int32_t ret = 0;
    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    int32_t fps = mFps;
    int32_t vformat;
    vformat = v4l2_fourcc('M', 'J', 'P', 'G');

    if ((mWidth > 1920) || (mHeight > 1080)) {
        fps = 15;
    }

    ALOGI("Width * Height %d x %d format %c%c%c%c, fps: %d",
            mWidth, mHeight, vformat&0xFF, (vformat>>8)&0xFF,
            (vformat>>16)&0xFF, (vformat>>24)&0xFF, fps);

    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    param.parm.capture.timeperframe.numerator   = 1;
    param.parm.capture.timeperframe.denominator = fps;
    param.parm.capture.capturemode = mCamera->getCaptureMode(mWidth, mHeight);
    ret = ioctl(mDev, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_PARM Failed: %s", __func__, strerror(errno));
        return ret;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width        = mWidth & 0xFFFFFFF8;
    fmt.fmt.pix.height       = mHeight & 0xFFFFFFF8;
    fmt.fmt.pix.pixelformat  = vformat;
    fmt.fmt.pix.priv         = 0;
    fmt.fmt.pix.sizeimage    = 0;
    fmt.fmt.pix.bytesperline = 0;

    if(vformat == v4l2_fourcc('M', 'J', 'P', 'G')){
        int32_t stride = (mWidth+31)/32*32;
        int32_t c_stride = (stride/2+15)/16*16;
        fmt.fmt.pix.bytesperline = stride;
        fmt.fmt.pix.sizeimage    = stride*mHeight+c_stride * mHeight;
        ALOGI("Special handling for MJPG on Stride %d, size %d",
                fmt.fmt.pix.bytesperline,
                fmt.fmt.pix.sizeimage);
    }
    ret = ioctl(mDev, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_FMT Failed: %s", __func__, strerror(errno));
        return ret;
    }

    return 0;
}

int32_t MJPGStream::onDeviceStartLocked()
{
    ALOGV("%s", __func__);

    if (mDev <= 0) {
        ALOGE("----%s invalid fd-----", __func__);
        return BAD_VALUE;
    }

    int32_t ret = allocateSensorBuffersLocked();
    //-------init vpu----------
    int vpuRet;
    vpuRet = VPUInit();
    if(vpuRet) {
        ALOGE("VPUInit failed, vpuRet %d", vpuRet);
        return BAD_VALUE;
    }

    //-------register buffers----------
    struct v4l2_buffer buf;
    struct v4l2_requestbuffers req;

    memset(&req, 0, sizeof (req));
    req.count = mNumBuffers;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_DMABUF;
    if (ioctl(mDev, VIDIOC_REQBUFS, &req) < 0) {
        ALOGE("%s: VIDIOC_REQBUFS failed", __func__);
        return BAD_VALUE;
    }

    ret = 0;
    //----------qbuf----------
    struct v4l2_buffer cfilledbuffer;
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cfilledbuffer.memory = V4L2_MEMORY_DMABUF;
        cfilledbuffer.m.fd = mSensorBuffers[i]->mFd;
        cfilledbuffer.index = i;
        cfilledbuffer.length = mSensorBuffers[i]->mSize;
        ALOGI("buf[%d] length:%d", i, cfilledbuffer.length);
        ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
        if (ret < 0) {
            ALOGE("%s VIDIOC_QBUF Failed: %s", __func__, strerror(errno));
            return BAD_VALUE;
        }
    }

    //-------stream on-------
    enum v4l2_buf_type bufType;
    bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(mDev, VIDIOC_STREAMON, &bufType);
    if (ret < 0) {
        ALOGE("%s VIDIOC_STREAMON failed: %s", __func__, strerror(errno));
        return ret;
    }

    return 0;
}

int32_t MJPGStream::onDeviceStopLocked()
{
    ALOGV("%s", __func__);
    int32_t ret = 0;

    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    enum v4l2_buf_type bufType;
    bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(mDev, VIDIOC_STREAMOFF, &bufType);
    if (ret < 0) {
        ALOGE("%s VIDIOC_STREAMOFF failed:%s", __func__, strerror(errno));
        return ret;
    }

    ret = freeSensorBuffersLocked();
    if (ret != 0) {
        ALOGE("%s freeBuffersLocked failed", __func__);
        return -1;
    }


    return 0;
}

int32_t MJPGStream::onFrameAcquireLocked()
{
    ALOGV("%s", __func__);
    int32_t ret = 0;
    int32_t ret2 = 0;
    struct v4l2_buffer cfilledbuffer;
    memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_DMABUF;
    ret = ioctl(mDev, VIDIOC_DQBUF, &cfilledbuffer);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_DQBUF Failed: %s", __func__, strerror(errno));
        return -1;
    }

    int32_t index = cfilledbuffer.index;
    ALOGV("acquire index:%d", cfilledbuffer.index);
    int JPGLen;
    int VPUIndex;
    JPGLen = cfilledbuffer.length;
    VPUIndex = VPUDec((u8 *)mSensorBuffers[index]->mVirtAddr, JPGLen, index);
    ret2 = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_QBUF Failed: %s", __func__, strerror(errno));
        return BAD_VALUE;
    }
    return VPUIndex;

}

int32_t MJPGStream::onFrameReturnLocked(int32_t index, StreamBuffer& buf)
{
    ALOGV("%s: index:%d", __func__, index);
    int32_t ret = 0;

    Mutex::Autolock lock(mVPULock);

    if(buf.mpFrameBuf) {
        VpuDecRetCode retCode;
        retCode = VPU_DecOutFrameDisplayed(mVPUHandle, (VpuFrameBuffer *)buf.mpFrameBuf);
        if(VPU_DEC_RET_SUCCESS != retCode) {
            ALOGI("%s: vpu clear frame display failure: ret=%d \r\n",__FUNCTION__,ret);
            ret = BAD_VALUE;
        }
    }


    return 0;
}

int32_t MJPGStream::getDeviceBufferSize()
{
    return getFormatSize();
}

int32_t MJPGStream::allocateSensorBuffersLocked()
{
    ALOGV("%s", __func__);
    if (mIonFd <= 0) {
        ALOGE("%s ion invalid", __func__);
        return BAD_VALUE;
    }

    if (mRegistered) {
        ALOGI("%s but buffer is already registered", __func__);
        return 0;
    }

    int32_t size = getFormatSize();
    if ((mWidth == 0) || (mHeight == 0) || (size == 0)) {
        ALOGE("%s: width, height or size is 0", __func__);
        return BAD_VALUE;
    }


    mStreamSize = getDeviceBufferSize();
    unsigned char *Sensorptr = NULL;
    int32_t sharedFd;
    int32_t phyAddr;
    ion_user_handle_t ionHandle;
    int32_t ionSize = mStreamSize;
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        ionHandle = -1;
        int32_t err = ion_alloc(mIonFd, ionSize, 8, 1, 0, &ionHandle);
        if (err) {
            ALOGE("ion_alloc failed.");
            return BAD_VALUE;
        }

        err = ion_map(mIonFd,
                ionHandle,
                ionSize,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                0,
                &Sensorptr,
                &sharedFd);

        if (err) {
            ALOGE("ion_map failed.");
            ion_free(mIonFd, ionHandle);
            if (VPUptr != MAP_FAILED) {
                munmap(Sensorptr, ionSize);
            }
            if (sharedFd > 0) {
                close(sharedFd);
            }
            goto err;
        }
        phyAddr = ion_phys(mIonFd, ionHandle);
        if (phyAddr == 0) {
            ALOGE("ion_phys failed.");
            ion_free(mIonFd, ionHandle);
            if (VPUptr != MAP_FAILED) {
                munmap(Sensorptr, ionSize);
            }
            close(sharedFd);
            goto err;
        }
        mSensorBuffers[i] = new StreamBuffer();
        mSensorBuffers[i]->mVirtAddr  = Sensorptr;
        mSensorBuffers[i]->mPhyAddr   = phyAddr;
        mSensorBuffers[i]->mSize      = ionSize;
        mSensorBuffers[i]->mBufHandle = (buffer_handle_t*)(uintptr_t)ionHandle;
        mSensorBuffers[i]->mFd = sharedFd;
        mSensorBuffers[i]->mStream = this;
        mSensorBuffers[i]->mpFrameBuf  = NULL;
    }

    mRegistered = true;
    mAllocatedBuffers = mNumBuffers;

    return 0;

err:
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        if (mSensorBuffers[i] == NULL) {
            continue;
        }

        ion_user_handle_t ionHandle =
            (ion_user_handle_t)(uintptr_t)mSensorBuffers[i]->mBufHandle;
        munmap(mSensorBuffers[i]->mVirtAddr, mSensorBuffers[i]->mSize);
        close(mSensorBuffers[i]->mFd);
        ion_free(mIonFd, ionHandle);
        delete mSensorBuffers[i];
        mSensorBuffers[i] = NULL;
    }

    return BAD_VALUE;
}

int32_t MJPGStream::freeSensorBuffersLocked()
{
    ALOGV("%s", __func__);
    if (mIonFd <= 0) {
        ALOGE("%s ion invalid", __func__);
        return BAD_VALUE;
    }

    if (!mRegistered) {
        ALOGI("%s but buffer is not registered", __func__);
        return 0;
    }

    ALOGI("freeSensorBufferToIon buffer num:%d", mAllocatedBuffers);
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        ion_user_handle_t ionHandle =
            (ion_user_handle_t)(uintptr_t)mSensorBuffers[i]->mBufHandle;
        munmap(mSensorBuffers[i]->mVirtAddr, mSensorBuffers[i]->mSize);
        close(mSensorBuffers[i]->mFd);
        ion_free(mIonFd, ionHandle);
        delete mSensorBuffers[i];
        mSensorBuffers[i] = NULL;
    }

    mRegistered = false;
    mAllocatedBuffers = 0;

    return 0;
}


int MJPGStream::VPUInit()
{
    VpuVersionInfo ver;
    VpuDecRetCode ret;
    VpuWrapperVersionInfo w_ver;
    VpuMemInfo memInfo;
    VpuDecOpenParam decOpenParam;
    int capability=0;

    //Initial decode context
    mDecContxt.nCodec = 11; // -f (Fixed as JPG)
    mDecContxt.nChromaInterleave = 1;
    mDecContxt.nMapType = 0;
    mDecContxt.nTile2LinearEnable = 0;


    //clear 0
    memset(&memInfo,0,sizeof(memInfo));
    memset(&mDecMemInfo,0,sizeof(mDecMemInfo));

    ALOGI("UvcMJPGDevice::VPUInit");

    //load vpu
    ret = VPU_DecLoad();
    if (ret != VPU_DEC_RET_SUCCESS)
    {
        ALOGE("%s: vpu load failure: ret=%d \r\n",__FUNCTION__,ret);
        return 1;
    }
    ret = VPU_DecGetVersionInfo(&ver);
    if (ret != VPU_DEC_RET_SUCCESS){
        ALOGE("%s: vpu get version failure: ret=%d \r\n",  __FUNCTION__, ret);
        goto bail;
    }

    ALOGI("vpu lib version : major.minor.rel=%d.%d.%d \r\n",ver.nLibMajor,ver.nLibMinor,ver.nLibRelease);
    ALOGI("vpu fw version : major.minor.rel_rcode=%d.%d.%d_r%d \r\n",ver.nFwMajor,ver.nFwMinor,ver.nFwRelease,ver.nFwCode);

    //wrapper version info
    ret = VPU_DecGetWrapperVersionInfo(&w_ver);
    if (ret != VPU_DEC_RET_SUCCESS)
    {
        ALOGE("%s: vpu get wrapper version failure: ret=%d \r\n",__FUNCTION__,ret);
        goto bail;
    }
    ALOGI("vpu wrapper version : major.minor.rel=%d.%d.%d: %s \r\n",w_ver.nMajor,w_ver.nMinor,w_ver.nRelease,w_ver.pBinary);

    //query memory
    ret = VPU_DecQueryMem(&memInfo);
    if (ret != VPU_DEC_RET_SUCCESS)
    {
        ALOGE("%s: vpu query memory failure: ret=%d \r\n",__FUNCTION__,ret);
        goto bail;
    }

    //malloc memory for vpu wrapper
    if(MallocMemBlock(&memInfo,&mDecMemInfo) == 0)
    {
        ALOGE("%s: malloc memory failure: \r\n",__FUNCTION__);
        goto bail;
    }

    memset(&decOpenParam, 0, sizeof(decOpenParam));
    //set open params
    if(ConvertCodecFormat(mDecContxt.nCodec, &decOpenParam.CodecFormat) == 0)
    {
        ALOGE("%s: unsupported codec format: id=%d \r\n",__FUNCTION__, mDecContxt.nCodec);
        goto bail;
    }

    decOpenParam.nReorderEnable=1;  //for H264
    decOpenParam.nEnableFileMode=0; //unit test: using stream mode

    //check capabilities
    VPU_DecGetCapability((VpuDecHandle)NULL, VPU_DEC_CAP_FILEMODE, &capability);
    ALOGI("capability: file mode supported: %d \r\n",capability);
    VPU_DecGetCapability((VpuDecHandle)NULL, VPU_DEC_CAP_TILE, &capability);
    ALOGI("capability: tile format supported: %d \r\n",capability);
    if((capability==0)&&(mDecContxt.nMapType!=0))
    {
        ALOGW("WARNING: tile format is not supported \r\n");
    }

    decOpenParam.nChromaInterleave=mDecContxt.nChromaInterleave;
    decOpenParam.nMapType=mDecContxt.nMapType;
    decOpenParam.nTiled2LinearEnable=mDecContxt.nTile2LinearEnable;

    // open vpu
    ret = VPU_DecOpen(&mVPUHandle, &decOpenParam, &memInfo);
    if (ret != VPU_DEC_RET_SUCCESS)
    {
        ALOGE("%s: vpu open failure: ret=%d \r\n", __FUNCTION__, ret);
        return 1;
    }

    return 0;

bail:
    //release mem
    if(0==FreeMemBlock(&mDecMemInfo))
    {
        ALOGE("%s: mmfree memory failure:  \r\n",__FUNCTION__);
    }


    //unload
    ret = VPU_DecUnLoad();
    if (ret != VPU_DEC_RET_SUCCESS)
    {
        ALOGE("%s: vpu unload failure: ret=%d \r\n",__FUNCTION__,ret);
    }

    return 1;
}

int MJPGStream::VPUExit()
{
    VpuDecRetCode ret;

    ALOGI("UvcMJPGDevice::VPUExit");

    // close vpu
    ret = VPU_DecClose(mVPUHandle);
    if (ret != VPU_DEC_RET_SUCCESS)
    {
        ALOGE("%s: vpu close failure: ret=%d \r\n", __FUNCTION__, ret);
        return 1;
    }

    //release mem
    ALOGI("FreeMemBlock");
    if(0==FreeMemBlock(&mDecMemInfo))
    {
        ALOGE("%s: mmfree memory failure:  \r\n",__FUNCTION__);
        return 1;
    }


    return 0;
}

int MJPGStream::VPUDec(u8 *InVirAddr, u32 inLen, unsigned int nUVCBufIdx)
{
DecLogic:
    VpuDecRetCode ret;
    int bufRetCode = 0;
    int err = 0;

    //DecContxt * decContxt;
    DecMemInfo pDecMemInfo;

    VpuBufferNode InData;

    unsigned int outIdx = 0;
    unsigned int VPUIndex = 0;

    Mutex::Autolock lock(mVPULock);

    memset(&InData, 0, sizeof(InData));
    InData.nSize = inLen;
    InData.pPhyAddr = NULL;
    InData.pVirAddr = InVirAddr;
    InData.sCodecData.pData = NULL;
    InData.sCodecData.nSize = 0;

    ret = VPU_DecDecodeBuf(mVPUHandle, &InData, &bufRetCode);

    // check init info
    if(bufRetCode & VPU_DEC_INIT_OK) {
        ALOGI("%s: vpu & VPU_DEC_INIT_OK \r\n", __FUNCTION__);
        int nFrmNum;
        VpuDecInitInfo InitInfo;

        //process init info
        if(ProcessInitInfo(&InitInfo, &pDecMemInfo, &nFrmNum, &VPUptr, &mVPUBuffersIndex) == 0)
        {
        ALOGI("%s: vpu process init info failure: \r\n", __FUNCTION__);
            return 0;
        }

        goto DecLogic;
    }

    //check output buff
    if((bufRetCode & VPU_DEC_OUTPUT_DIS) ||(bufRetCode & VPU_DEC_OUTPUT_MOSAIC_DIS))
    {
        VpuDecOutFrameInfo frameInfo;

        // get output frame
        ret = VPU_DecGetOutputFrame(mVPUHandle, &frameInfo);
        if(ret != VPU_DEC_RET_SUCCESS)
        {
            ALOGE("%s: vpu get output frame failure: ret=%d \r\n",__FUNCTION__,ret);
            return 0;
        }

        unsigned int i;
        for(i = 0; i < mNumBuffers; i++) {
            if(frameInfo.pDisplayFrameBuf->pbufY == (unsigned char* )(uintptr_t)mBuffers[i]->mPhyAddr) {
                VPUIndex = i;
                break;
            }
        }

            mBuffers[VPUIndex]->mpFrameBuf = (void *)frameInfo.pDisplayFrameBuf;

    } else if(bufRetCode & VPU_DEC_NO_ENOUGH_BUF) {
        mVPULock.unlock();
        usleep(10000);
        ALOGI("VPU_DEC_NO_ENOUGH_BUF, wait 10ms, goto DecLogic");
        goto DecLogic;
    }

      return VPUIndex;
}

int  MJPGStream::ProcessInitInfo(VpuDecInitInfo* pInitInfo, DecMemInfo* /*pDecMemInfo*/, int*pOutFrmNum, unsigned char** rptr, int32_t* vpuindex)
{
    VpuDecRetCode ret;
    VpuFrameBuffer frameBuf[MAX_FRAME_NUM];
    VpuMemDesc vpuMem;
    int requestedBufNum;
    int i;
    int totalSize=0;
    int mvSize=0;
    int ySize=0;
    int uSize=0;
    int vSize=0;
    int yStride=0;
    int uStride=0;
    int vStride=0;
    unsigned char* ptr;
    unsigned char* ptrVirt;
    unsigned char* pPhyAddr;
    unsigned char* pVirtAddr;
    int nAlign;
    int multifactor=1;

    ALOGI("enter ProcessInitInfo");

    //get init info
    ret=VPU_DecGetInitialInfo(mVPUHandle, pInitInfo);
    if(VPU_DEC_RET_SUCCESS!=ret)
    {
        ALOGE("%s: vpu get init info failure: ret=%d \r\n",__FUNCTION__,ret);
        return 0;
    }

    //malloc frame buffs
    requestedBufNum=pInitInfo->nMinFrameBufferCount+FRAME_SURPLUS;
    ALOGI("VPU requested requestedBufNum %d, minCount %d", requestedBufNum, pInitInfo->nMinFrameBufferCount);

    if(requestedBufNum>MAX_FRAME_NUM)
    {
        ALOGE("%s: vpu request too many frames : num=0x%X \r\n",__FUNCTION__,pInitInfo->nMinFrameBufferCount);
        return 0;
    }

    unsigned char *VPUptr = NULL;
    int32_t sharedFd;
    unsigned char *phyAddr;
    ion_user_handle_t ionHandle;
    int32_t ionSize = getDeviceBufferSize();
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        ionHandle = -1;
        int32_t err = ion_alloc(mIonFd, ionSize, 8, 1, 0, &ionHandle);
        if (err) {
            ALOGE("ion_alloc failed.");
            return BAD_VALUE;
        }

        err = ion_map(mIonFd,
                ionHandle,
                ionSize,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                0,
                &VPUptr,
                &sharedFd);

        if (err) {
            ALOGE("ion_map failed.");
            return BAD_VALUE;
        }
        phyAddr = (unsigned char *)ion_phys(mIonFd, ionHandle);
        if (phyAddr == 0) {
            ALOGE("ion_phys failed.");
            return BAD_VALUE;
        }
        pPhyAddr=phyAddr;
        pVirtAddr=VPUptr;
        mBuffers[i] = new StreamBuffer();
        mBuffers[i]->mVirtAddr  = VPUptr;
        mBuffers[i]->mPhyAddr   = (uintptr_t)phyAddr;
        mBuffers[i]->mSize      =  ionSize;
        mBuffers[i]->mBufHandle = (buffer_handle_t*)(uintptr_t)ionHandle;
        mBuffers[i]->mFd = sharedFd;
        mBuffers[i]->mStream = this;
        mBuffers[i]->mpFrameBuf  = NULL;
    }

    yStride=Align(pInitInfo->nPicWidth,FRAME_ALIGN);

    if(pInitInfo->nInterlace)
    {
        ySize = Align(pInitInfo->nPicWidth,FRAME_ALIGN)*Align(pInitInfo->nPicHeight,(2*FRAME_ALIGN));
    }
    else
    {
        ySize = Align(pInitInfo->nPicWidth,FRAME_ALIGN)*Align(pInitInfo->nPicHeight,FRAME_ALIGN);
    }

    ALOGI("nInterlace %d, ySize %d", pInitInfo->nInterlace, ySize);


    //for MJPG: we need to check 4:4:4/4:2:2/4:2:0/4:0:0
    VpuCodStd vpuCodec = VPU_V_MPEG4;

    ConvertCodecFormat(mDecContxt.nCodec, &vpuCodec);
    if(VPU_V_MJPG==vpuCodec)
    {
        switch(pInitInfo->nMjpgSourceFormat)
        {
            case 0: //4:2:0
                ALOGI("MJPG: 4:2:0 \r\n");
                uStride=yStride/2;
                vStride=uStride;
                uSize=ySize/4;
                vSize=uSize;
                mvSize=uSize;
                mDecContxt.eOutColorFmt=DEC_OUT_420;
                break;
            case 1: //4:2:2 hor
                ALOGI("MJPG: 4:2:2 hor \r\n");
                uStride=yStride/2;
                vStride=uStride;
                uSize=ySize/2;
                vSize=uSize;
                mvSize=uSize;
                mDecContxt.eOutColorFmt=DEC_OUT_422H;
                break;
            case 2: //4:2:2 ver
                ALOGI("MJPG: 4:2:2 ver \r\n");
                uStride=yStride;
                vStride=uStride;
                uSize=ySize/2;
                vSize=uSize;
                mvSize=uSize;
                mDecContxt.eOutColorFmt=DEC_OUT_422V;
                break;
            case 3: //4:4:4
                ALOGI("MJPG: 4:4:4 \r\n");
                uStride=yStride;
                vStride=uStride;
                uSize=ySize;
                vSize=uSize;
                mvSize=uSize;
                mDecContxt.eOutColorFmt=DEC_OUT_444;
                break;
            case 4: //4:0:0
                ALOGI("MJPG: 4:0:0 \r\n");
                uStride=0;
                vStride=uStride;
                uSize=0;
                vSize=uSize;
                mvSize=uSize;
                mDecContxt.eOutColorFmt=DEC_OUT_400;
                break;
            default:        //4:2:0
                ALOGI("unknown color format: %d \r\n",vpuCodec);
                uStride=yStride/2;
                vStride=uStride;
                uSize=ySize/4;
                vSize=uSize;
                mvSize=uSize;
                mDecContxt.eOutColorFmt=DEC_OUT_420;
                break;
        }
    }
    else
    {
        //4:2:0 for all video
        uStride=yStride/2;
        vStride=uStride;
        uSize=ySize/4;
        vSize=uSize;
        mvSize=uSize;
        mDecContxt.eOutColorFmt=DEC_OUT_420;
    }


    meOutColorFmt = mDecContxt.eOutColorFmt;


    nAlign = pInitInfo->nAddressAlignment;
    if(mDecContxt.nMapType == 2)
    {
        //only consider Y since interleave must be enabled
        multifactor = 2;        //for field, we need to consider alignment for top and bot
    }
    if(nAlign > 1)
    {
        ySize=Align(ySize,multifactor*nAlign);
        uSize=Align(uSize,nAlign);
        vSize=Align(vSize,nAlign);
    }

    ALOGI("ySize %d", ySize);

    for(uint32_t i=0;i<mNumBuffers;i++)
    {
        totalSize=(ySize+uSize+vSize+mvSize+nAlign)*1;

        ptr=(unsigned char*)(uintptr_t)(mBuffers[i]->mPhyAddr);
        ptrVirt=(unsigned char*)(mBuffers[i]->mVirtAddr);

        /*align the base address*/
        if(nAlign>1)
        {
            ptr=(unsigned char*)Align(ptr,nAlign);
            ptrVirt=(unsigned char*)Align(ptrVirt,nAlign);
        }

        ALOGI("VPU reg buf, idx %d, ptr phy %p, vir %p", i, ptr, ptrVirt);

        /* fill stride info */
        frameBuf[i].nStrideY=yStride;
        frameBuf[i].nStrideC=uStride;

        /* fill phy addr*/
        frameBuf[i].pbufY=ptr;
        frameBuf[i].pbufCb=ptr+ySize;
        frameBuf[i].pbufCr=ptr+ySize+uSize;
        frameBuf[i].pbufMvCol=ptr+ySize+uSize+vSize;

        /* fill virt addr */
        frameBuf[i].pbufVirtY=ptrVirt;
        frameBuf[i].pbufVirtCb=ptrVirt+ySize;
        frameBuf[i].pbufVirtCr=ptrVirt+ySize+uSize;
        frameBuf[i].pbufVirtMvCol=ptrVirt+ySize+uSize+vSize;

        /* fill bottom address for field tile*/
        if(mDecContxt.nMapType==2)
        {
            frameBuf[i].pbufY_tilebot=frameBuf[i].pbufY+ySize/2;
            frameBuf[i].pbufCb_tilebot=frameBuf[i].pbufCr;
            frameBuf[i].pbufVirtY_tilebot=frameBuf[i].pbufVirtY+ySize/2;
            frameBuf[i].pbufVirtCb_tilebot=frameBuf[i].pbufVirtCr;
        }
        else
        {
            frameBuf[i].pbufY_tilebot=0;
            frameBuf[i].pbufCb_tilebot=0;
            frameBuf[i].pbufVirtY_tilebot=0;
            frameBuf[i].pbufVirtCb_tilebot=0;
        }
        *vpuindex = i;
    }

    //register frame buffs
    ret=VPU_DecRegisterFrameBuffer(mVPUHandle, frameBuf, mNumBuffers);
    if(VPU_DEC_RET_SUCCESS!=ret)
    {
        ALOGE("%s: vpu register frame failure: ret=%d \r\n",__FUNCTION__,ret);
        return 0;
    }

    *pOutFrmNum=requestedBufNum;
    return 1;
}

int MJPGStream::FreeMemBlock(DecMemInfo* pDecMem)
{
    int i;
    VpuMemDesc vpuMem;
    VpuDecRetCode vpuRet;
    int retOk=1;

    //free virtual mem
    for(i=0;i<pDecMem->nVirtNum;i++)
    {
        if((void*)(uintptr_t)pDecMem->virtMem[i]) free((void*)(uintptr_t)pDecMem->virtMem[i]);
    }
    pDecMem->nVirtNum=0;

    //free physical mem
    for(i=0;i<pDecMem->nPhyNum;i++)
    {
        vpuMem.nPhyAddr=pDecMem->phyMem_phyAddr[i];
        vpuMem.nVirtAddr=pDecMem->phyMem_virtAddr[i];
        vpuMem.nCpuAddr=pDecMem->phyMem_cpuAddr[i];
        vpuMem.nSize=pDecMem->phyMem_size[i];
        vpuRet=VPU_DecFreeMem(&vpuMem);
        if(vpuRet!=VPU_DEC_RET_SUCCESS)
        {
        ALOGE("%s: free vpu memory failure : ret=%d \r\n",__FUNCTION__,vpuRet);
            retOk=0;
        }
    }
    pDecMem->nPhyNum    =0;

    return retOk;
}

int  MJPGStream::MallocMemBlock(VpuMemInfo* pMemBlock,DecMemInfo* pDecMem)
{
    int i;
    unsigned char * ptr=NULL;
    int size;

    for(i=0;i<pMemBlock->nSubBlockNum;i++)
    {
        size=pMemBlock->MemSubBlock[i].nAlignment+pMemBlock->MemSubBlock[i].nSize;
        if(pMemBlock->MemSubBlock[i].MemType==VPU_MEM_VIRT)
        {
            ptr=(unsigned char *)malloc(size);
            if(ptr==NULL)
            {
                ALOGE("%s: get virtual memory failure, size=%d \r\n",__FUNCTION__,size);
                goto failure;
            }
            pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char*)Align(ptr,pMemBlock->MemSubBlock[i].nAlignment);

            //record virtual base addr
            pDecMem->virtMem[pDecMem->nVirtNum]=(uintptr_t)ptr;
            pDecMem->nVirtNum++;
        }
        else// if(memInfo.MemSubBlock[i].MemType==VPU_MEM_PHY)
        {
            VpuMemDesc vpuMem;
            VpuDecRetCode ret;
            vpuMem.nSize=size;
            ret=VPU_DecGetMem(&vpuMem);
            if(ret!=VPU_DEC_RET_SUCCESS)
            {
                ALOGE("%s: get vpu memory failure, size=%d, ret=%d \r\n",__FUNCTION__,size,ret);
                goto failure;
            }
            pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char*)Align(vpuMem.nVirtAddr,pMemBlock->MemSubBlock[i].nAlignment);
            pMemBlock->MemSubBlock[i].pPhyAddr=(unsigned char*)Align(vpuMem.nPhyAddr,pMemBlock->MemSubBlock[i].nAlignment);

            //record physical base addr
            pDecMem->phyMem_phyAddr[pDecMem->nPhyNum]=(unsigned int)vpuMem.nPhyAddr;
            pDecMem->phyMem_virtAddr[pDecMem->nPhyNum]=(unsigned int)vpuMem.nVirtAddr;
            pDecMem->phyMem_cpuAddr[pDecMem->nPhyNum]=(unsigned int)vpuMem.nCpuAddr;
            pDecMem->phyMem_size[pDecMem->nPhyNum]=size;
            pDecMem->nPhyNum++;
        }
    }

    return 1;
failure:
    FreeMemBlock(pDecMem);
    return 0;
}

int MJPGStream::ConvertCodecFormat(int codec, VpuCodStd* pCodec)
{
    switch (codec)
    {
        case 1:
            *pCodec=VPU_V_MPEG2;
            break;
        case 2:
            *pCodec=VPU_V_MPEG4;
            break;
        case 3:
            *pCodec=VPU_V_DIVX3;
            break;
        case 4:
            *pCodec=VPU_V_DIVX4;
            break;
        case 5:
            *pCodec=VPU_V_DIVX56;
            break;
        case 6:
            *pCodec=VPU_V_XVID;
            break;
        case 7:
            *pCodec=VPU_V_H263;
            break;
        case 8:
            *pCodec=VPU_V_AVC;
            break;
        case 9:
            *pCodec=VPU_V_VC1; //VPU_V_VC1_AP
            break;
        case 10:
            *pCodec=VPU_V_RV;
            break;
        case 11:
            *pCodec=VPU_V_MJPG;
            break;
        case 12:
            *pCodec=VPU_V_AVS;
            break;
        case 13:
            *pCodec=VPU_V_VP8;
            break;
        case 14:
            *pCodec=VPU_V_AVC_MVC;
            break;
        default:
            return 0;
    }
    return 1;
}

