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

#include "UvcStream.h"

UvcStream::UvcStream(Camera* device, const char* name)
    : DeviceStream(device), mIonFd(-1), mUvcSize(0)
{
    mIonFd = ion_open();
    strncpy(mUvcPath, name, CAMAERA_FILENAME_LENGTH);
}

UvcStream::~UvcStream()
{
    if (mIonFd > 0) {
        close(mIonFd);
        mIonFd = -1;
    }
}

// configure device.
int32_t UvcStream::onDeviceConfigureLocked()
{
    ALOGV("%s", __func__);

    int32_t ret = 0;
    if (mDev <= 0) {
        // usb camera should open dev node again.
        // because when stream off, the dev node must close.
        mDev = open(mUvcPath, O_RDWR);
        if (mDev <= 0) {
            ALOGE("%s invalid fd handle", __func__);
            return BAD_VALUE;
        }
    }

    int32_t fps = 30;
    int32_t vformat;
    vformat = convertPixelFormatToV4L2Format(mFormat);

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
    ret = ioctl(mDev, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        ALOGE("%s VIDIOC_S_PARM Failed: %s", __func__, strerror(errno));
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

    ret = ioctl(mDev, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ALOGE("%s VIDIOC_S_FMT Failed: %s", __func__, strerror(errno));
        return ret;
    }


    return 0;
}

int32_t UvcStream::onDeviceStartLocked()
{
    ALOGV("%s", __func__);

    if (mDev <= 0) {
        ALOGE("----%s invalid fd-----", __func__);
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

    int32_t ret = 0;
    //----------qbuf----------
    struct v4l2_buffer cfilledbuffer;
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cfilledbuffer.memory = V4L2_MEMORY_DMABUF;
        cfilledbuffer.m.fd = mBuffers[i]->mFd;
        cfilledbuffer.index    = i;
        cfilledbuffer.length = mUvcSize;
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

int32_t UvcStream::onDeviceStopLocked()
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

    // usb camera must close device after stream off.
    if (mDev > 0) {
        close(mDev);
        mDev = -1;
    }

    return 0;
}

int32_t UvcStream::onFrameAcquireLocked()
{
    ALOGV("%s", __func__);
    int32_t ret = 0;
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
    return cfilledbuffer.index;
}

int32_t UvcStream::onFrameReturnLocked(int32_t index, StreamBuffer& buf)
{
    ALOGV("%s: index:%d", __func__, index);
    int32_t ret = 0;
    struct v4l2_buffer cfilledbuffer;
    memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_DMABUF;
    cfilledbuffer.m.fd = buf.mFd;
    cfilledbuffer.index = index;
    cfilledbuffer.length = mUvcSize;

    ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_QBUF Failed: %s", __func__, strerror(errno));
        return BAD_VALUE;
    }

    return 0;
}

int32_t UvcStream::allocateBuffersLocked()
{
    ALOGI("%s", __func__);
    if (mIonFd <= 0) {
        ALOGE("%s ion invalid", __func__);
        return BAD_VALUE;
    }

    if (mRegistered) {
        ALOGI("%s but buffer is already registered", __func__);
        return 0;
    }

    int32_t size = 0;
    if ((mWidth == 0) || (mHeight == 0)) {
        ALOGE("%s: width or height = 0", __func__);
        return BAD_VALUE;
    }

    switch (mFormat) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            size = mWidth * ((mHeight + 16) & (~15)) * 3 / 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            size = mWidth * mHeight * 3 / 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            size = mWidth * mHeight * 2;
            break;

        default:
            ALOGE("Error: format:0x%x not supported int ion alloc", mFormat);
            return BAD_VALUE;
    }
    mUvcSize = size;

    unsigned char *ptr = NULL;
    int32_t sharedFd;
    int32_t phyAddr;
    ion_user_handle_t ionHandle;
    size = (size + PAGE_SIZE) & (~(PAGE_SIZE - 1));

    ALOGI("allocateBufferFromIon buffer num:%d", mNumBuffers);
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        ionHandle = -1;
        int32_t err = ion_alloc(mIonFd, size, 8, 1, 0, &ionHandle);
        if (err) {
            ALOGE("ion_alloc failed.");
            return BAD_VALUE;
        }

        err = ion_map(mIonFd,
                      ionHandle,
                      size,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      0,
                      &ptr,
                      &sharedFd);

        if (err) {
            ALOGE("ion_map failed.");
            return BAD_VALUE;
        }
        phyAddr = ion_phys(mIonFd, ionHandle);
        if (phyAddr == 0) {
            ALOGE("ion_phys failed.");
            return BAD_VALUE;
        }
        ALOGI("phyalloc ptr:0x%x, phy:0x%x, size:%d", (int32_t)ptr, phyAddr, size);
        mBuffers[i] = new StreamBuffer();
        mBuffers[i]->mVirtAddr  = ptr;
        mBuffers[i]->mPhyAddr   = phyAddr;
        mBuffers[i]->mSize      =  size;
        mBuffers[i]->mBufHandle = (buffer_handle_t*)ionHandle;
        mBuffers[i]->mStream = this;
        mBuffers[i]->mFd = sharedFd;
    }

    mRegistered = true;
    mAllocatedBuffers = mNumBuffers;

    return 0;
}

int32_t UvcStream::freeBuffersLocked()
{
    ALOGI("%s", __func__);
    if (mIonFd <= 0) {
        ALOGE("%s ion invalid", __func__);
        return BAD_VALUE;
    }

    if (!mRegistered) {
        ALOGI("%s but buffer is not registered", __func__);
        return 0;
    }

    ALOGI("freeBufferToIon buffer num:%d", mAllocatedBuffers);
    for (uint32_t i = 0; i < mAllocatedBuffers; i++) {
        ion_user_handle_t ionHandle =
            (ion_user_handle_t)mBuffers[i]->mBufHandle;
        close(mBuffers[i]->mFd);
        ion_free(mIonFd, ionHandle);
        munmap(mBuffers[i]->mVirtAddr, mBuffers[i]->mSize);
        delete mBuffers[i];
        mBuffers[i] = NULL;
    }

    mRegistered = false;
    mAllocatedBuffers = 0;

    return 0;
}

