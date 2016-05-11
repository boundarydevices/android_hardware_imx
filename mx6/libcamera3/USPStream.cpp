/*
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc.
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

#include "USPStream.h"

USPStream::USPStream(Camera* device)
    : MMAPStream(device), mIonFd(-1)
{
    mIonFd = ion_open();
}

USPStream::~USPStream()
{
    if (mIonFd > 0) {
        close(mIonFd);
        mIonFd = -1;
    }
}

// configure device.
int32_t USPStream::onDeviceConfigureLocked()
{
    ALOGI("%s", __func__);
    int32_t ret = 0;
    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    return MMAPStream::onDeviceConfigureLocked();
}

int32_t USPStream::onDeviceStartLocked()
{
    ALOGV("%s", __func__);
    if (mDev <= 0) {
        ALOGE("%s invalid dev node", __func__);
        return BAD_VALUE;
    }

    //-------register buffers----------
    struct v4l2_buffer buf;
    struct v4l2_requestbuffers req;

    memset(&req, 0, sizeof (req));
    req.count = mNumBuffers;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;
    if (ioctl(mDev, VIDIOC_REQBUFS, &req) < 0) {
        ALOGE("%s VIDIOC_REQBUFS failed", __func__);
        return BAD_VALUE;
    }

    for (uint32_t i = 0; i < mNumBuffers; i++) {
        memset(&buf, 0, sizeof (buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.index = i;
        buf.memory = V4L2_MEMORY_USERPTR;
        buf.m.offset = mBuffers[i]->mPhyAddr;
        buf.length   = mBuffers[i]->mSize;
        if (ioctl(mDev, VIDIOC_QUERYBUF, &buf) < 0) {
            ALOGE("%s VIDIOC_QUERYBUF error", __func__);
            return BAD_VALUE;
        }
    }

    int32_t ret = 0;
    //----------qbuf----------
    struct v4l2_buffer cfilledbuffer;
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cfilledbuffer.memory = V4L2_MEMORY_USERPTR;
        cfilledbuffer.index    = i;
        cfilledbuffer.m.offset = mBuffers[i]->mPhyAddr;
        ALOGI("%s VIDIOC_QBUF phy:0x%x", __func__, mBuffers[i]->mPhyAddr);
        ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
        if (ret < 0) {
            ALOGE("%s VIDIOC_QBUF Failed", __func__);
            return BAD_VALUE;
        }
    }

    //-------stream on-------
    enum v4l2_buf_type bufType;
    bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(mDev, VIDIOC_STREAMON, &bufType);
    if (ret < 0) {
        ALOGE("%s VIDIOC_STREAMON failed:%s", __func__, strerror(errno));
        return ret;
    }

    return 0;
}

int32_t USPStream::onDeviceStopLocked()
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
        ALOGE("%s VIDIOC_STREAMOFF failed: %s", __func__, strerror(errno));
        return ret;
    }

    return 0;
}

int32_t USPStream::onFrameAcquireLocked()
{
    ALOGV("%s", __func__);
    int32_t ret = 0;
    struct v4l2_buffer cfilledbuffer;
    memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_USERPTR;

    ret = ioctl(mDev, VIDIOC_DQBUF, &cfilledbuffer);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_DQBUF Failed", __func__);
        return -1;
    }

    return cfilledbuffer.index;
}

int32_t USPStream::onFrameReturnLocked(int32_t index, StreamBuffer& buf)
{
    ALOGV("%s", __func__);
    int32_t ret = 0;
    struct v4l2_buffer cfilledbuffer;
    memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_USERPTR;
    cfilledbuffer.index    = index;
    cfilledbuffer.m.offset = buf.mPhyAddr;

    ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
    if (ret < 0) {
        ALOGE("%s VIDIOC_QBUF Failed", __func__);
        return BAD_VALUE;
    }

    return ret;
}

int32_t USPStream::getFormatSize()
{
    int32_t size = 0;
    int alignedw, alignedh, c_stride;
    switch (mFormat) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            alignedw = ALIGN_PIXEL_16(mWidth);
            alignedh = ALIGN_PIXEL_16(mHeight);
            size = alignedw * alignedh * 3 / 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_420_P: {
            alignedw = ALIGN_PIXEL_32(mWidth);
            alignedh = ALIGN_PIXEL_4(mHeight);
            c_stride = (alignedw/2+15)/16*16;
            size = (alignedw + c_stride) * alignedh;
            break;
        }
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            alignedw = ALIGN_PIXEL_16(mWidth);
            alignedh = ALIGN_PIXEL_16(mHeight);
            size = alignedw * alignedh * 2;
            break;

        default:
            ALOGE("Error: %s format not supported", __func__);
            break;
    }

    return size;
}

int32_t USPStream::allocateBuffersLocked()
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

    unsigned char *ptr = NULL;
    int32_t sharedFd;
    int32_t phyAddr;
    ion_user_handle_t ionHandle;
    int32_t ionSize = (size + PAGE_SIZE) & (~(PAGE_SIZE - 1));

    ALOGI("allocateBufferFromIon buffer num:%d", mNumBuffers);
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
                      &ptr,
                      &sharedFd);

        if (err) {
            ALOGE("ion_map failed.");
            ion_free(mIonFd, ionHandle);
            if (ptr != MAP_FAILED) {
                munmap(ptr, ionSize);
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
            if (ptr != MAP_FAILED) {
                munmap(ptr, ionSize);
            }
            close(sharedFd);
            goto err;
        }
        ALOGI("phyalloc ptr:0x%x, phy:0x%x, ionSize:%d", (int32_t)ptr, phyAddr, ionSize);
        mBuffers[i] = new StreamBuffer();
        mBuffers[i]->mVirtAddr  = ptr;
        mBuffers[i]->mPhyAddr   = phyAddr;
        mBuffers[i]->mSize      =  ionSize;
        mBuffers[i]->mBufHandle = (buffer_handle_t*)ionHandle;
        mBuffers[i]->mFd = sharedFd;
        mBuffers[i]->mStream = this;
    }

    mRegistered = true;
    mAllocatedBuffers = mNumBuffers;

    return 0;

err:
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        if (mBuffers[i] == NULL) {
            continue;
        }

        ion_user_handle_t ionHandle =
            (ion_user_handle_t)mBuffers[i]->mBufHandle;
        munmap(mBuffers[i]->mVirtAddr, mBuffers[i]->mSize);
        close(mBuffers[i]->mFd);
        ion_free(mIonFd, ionHandle);
        delete mBuffers[i];
        mBuffers[i] = NULL;
    }

    return BAD_VALUE;
}

int32_t USPStream::freeBuffersLocked()
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

    ALOGI("freeBufferToIon buffer num:%d", mAllocatedBuffers);
    for (uint32_t i = 0; i < mAllocatedBuffers; i++) {
        ion_user_handle_t ionHandle =
            (ion_user_handle_t)mBuffers[i]->mBufHandle;
        munmap(mBuffers[i]->mVirtAddr, mBuffers[i]->mSize);
        close(mBuffers[i]->mFd);
        ion_free(mIonFd, ionHandle);
        delete mBuffers[i];
        mBuffers[i] = NULL;
    }

    mRegistered = false;
    mAllocatedBuffers = 0;

    return 0;
}

