/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 * Copyright 2020 NXP.
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

#define LOG_TAG "DMAStream"

#include <Allocator.h>
#include "DMAStream.h"

namespace android {

DMAStream::DMAStream(CameraDeviceSessionHwlImpl *pSession)
    : MMAPStream(pSession), mStreamSize(0)
{
    mV4l2MemType = V4L2_MEMORY_DMABUF;
    mPlane = false;
}

DMAStream::DMAStream(bool mplane, CameraDeviceSessionHwlImpl *pSession)
    : MMAPStream(pSession), mStreamSize(0)
{
    // If driver support V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, will set mplane as
    // true, else set it as false.
    mPlane = mplane;
    mV4l2MemType = V4L2_MEMORY_DMABUF;
}

DMAStream::~DMAStream()
{
}

// configure device.
int32_t DMAStream::onDeviceConfigureLocked(uint32_t format, uint32_t width, uint32_t height, uint32_t fps)
{
    ALOGI("%s", __func__);

    return MMAPStream::onDeviceConfigureLocked(format, width, height, fps);
}

int32_t DMAStream::onDeviceStartLocked()
{
    ALOGI("%s", __func__);

    if (mDev <= 0) {
        ALOGE("----%s invalid fd-----", __func__);
        return BAD_VALUE;
    }

    //-------register buffers----------
    struct v4l2_requestbuffers req;
    struct v4l2_plane planes;
    memset(&planes, 0, sizeof(struct v4l2_plane));

    memset(&req, 0, sizeof (req));
    req.count = mNumBuffers;

    if (mPlane) {
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else {
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }

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
        if (mPlane) {
            memset(&planes, 0, sizeof(planes));
            cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            cfilledbuffer.m.planes = &planes;
            cfilledbuffer.m.planes->length = mStreamSize;
            cfilledbuffer.length = 1;
            cfilledbuffer.m.planes->m.fd = mBuffers[i]->mFd;
        } else {
            cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            cfilledbuffer.m.fd = mBuffers[i]->mFd;
            cfilledbuffer.length = mStreamSize;
            ALOGI("buf[%d] length:%d", i, cfilledbuffer.length);
        }
        cfilledbuffer.memory = V4L2_MEMORY_DMABUF;
        cfilledbuffer.index    = i;


        ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
        if (ret < 0) {
            ALOGE("%s VIDIOC_QBUF Failed: %s", __func__, strerror(errno));
            return BAD_VALUE;
        }
    }

    //-------stream on-------
    enum v4l2_buf_type bufType;

    if (mPlane) {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }

    ALOGI("before VIDIOC_STREAMON");
    ret = ioctl(mDev, VIDIOC_STREAMON, &bufType);
    if (ret < 0) {
        ALOGE("%s VIDIOC_STREAMON failed: %s", __func__, strerror(errno));
        return ret;
    }
    ALOGI(" after VIDIOC_STREAMON");

    mOmitFrames = mOmitFrmCount;
    mbStart = true;

    return 0;
}

int32_t DMAStream::onDeviceStopLocked()
{
    ALOGV("%s", __func__);
    int32_t ret = 0;
    struct v4l2_requestbuffers req;

    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    enum v4l2_buf_type bufType;

    if (mPlane) {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }


    ret = ioctl(mDev, VIDIOC_STREAMOFF, &bufType);
    if (ret < 0) {
        ALOGE("%s VIDIOC_STREAMOFF failed:%s", __func__, strerror(errno));
        return ret;
    }

    memset(&req, 0, sizeof (req));
    req.count = 0;
    req.type = bufType;
    req.memory = V4L2_MEMORY_DMABUF;

    if (ioctl(mDev, VIDIOC_REQBUFS, &req) < 0) {
        ALOGI("%s VIDIOC_REQBUFS failed: %s", __func__, strerror(errno));
        return BAD_VALUE;
    }

    mbStart = false;

    return 0;
}

ImxStreamBuffer* DMAStream::onFrameAcquireLocked()
{
    ALOGV("%s", __func__);
    int32_t ret = 0;
    struct v4l2_buffer cfilledbuffer;
    struct v4l2_plane planes;
    memset(&planes, 0, sizeof(struct v4l2_plane));

capture_data:
    memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));

    if (mPlane) {
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        cfilledbuffer.m.planes = &planes;
        cfilledbuffer.length = 1;
    } else {
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }

    cfilledbuffer.memory = V4L2_MEMORY_DMABUF;

    ret = ioctl(mDev, VIDIOC_DQBUF, &cfilledbuffer);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_DQBUF Failed: %s", __func__, strerror(errno));
        return NULL;
    }

    ALOGV("acquire index:%d", cfilledbuffer.index);

    mFrames++;
    if (mFrames == 1)
        ALOGI("%s: first frame get for %dx%d", __func__, mWidth, mHeight);

    if (mOmitFrames > 0) {
        ALOGI("%s omit frame", __func__);
        ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
        if (ret < 0) {
            ALOGE("%s VIDIOC_QBUF Failed", __func__);
            return NULL;
        }
        mOmitFrames--;
        goto capture_data;
    }

    return mBuffers[cfilledbuffer.index];
}

int32_t DMAStream::onFrameReturnLocked(ImxStreamBuffer& buf)
{
    //ALOGV("%s: index:%d", __func__, index);
    int32_t ret = 0;
    struct v4l2_buffer cfilledbuffer;
    struct v4l2_plane planes;

    memset(&planes, 0, sizeof(struct v4l2_plane));
    memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));

    if (mPlane) {
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        cfilledbuffer.m.planes = &planes;
        cfilledbuffer.m.planes->length = mStreamSize;
        cfilledbuffer.length = 1;
        cfilledbuffer.m.planes->m.fd = buf.mFd;
    } else {
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cfilledbuffer.m.fd = buf.mFd;
        cfilledbuffer.length = mStreamSize;
    }

    cfilledbuffer.memory = V4L2_MEMORY_DMABUF;
    cfilledbuffer.index = buf.index;

    ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_QBUF Failed: %s", __func__, strerror(errno));
        return BAD_VALUE;
    }

    ALOGV("return index:%d", cfilledbuffer.index);

    return 0;
}

int32_t DMAStream::getDeviceBufferSize()
{
    return getSizeByForamtRes(mFormat, mWidth, mHeight, true);
}

int32_t DMAStream::allocateBuffersLocked()
{
    ALOGI("%s", __func__);

    mStreamSize = getDeviceBufferSize();
    fsl::Allocator *allocator = fsl::Allocator::getInstance();
    if (allocator == NULL) {
        ALOGE("%s allocator invalid", __func__);
        return BAD_VALUE;
    }

    if (mRegistered) {
        ALOGI("%s but buffer is already registered", __func__);
        return 0;
    }

    int32_t size = ALIGN_PIXEL_16(mWidth) * ALIGN_PIXEL_16(mHeight) * 4;
    if ((mWidth == 0) || (mHeight == 0) || (size == 0)) {
        ALOGE("%s: width, height or size is 0", __func__);
        return BAD_VALUE;
    }

    uint64_t ptr = 0;
    int32_t sharedFd;
    uint64_t phyAddr;
    int32_t memSize = (size + PAGE_SIZE) & (~(PAGE_SIZE - 1));

    ALOGI("allocate buffer num:%d", mNumBuffers);
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        sharedFd = allocator->allocMemory(memSize,
                        MEM_ALIGN, fsl::MFLAGS_CONTIGUOUS);
        if (sharedFd < 0) {
            ALOGE("allocMemory failed.");
            goto err;
        }

        int err = allocator->getVaddrs(sharedFd, memSize, ptr);
        if (err != 0) {
            ALOGE("getVaddrs failed.");
            close(sharedFd);
            goto err;
        }

        err = allocator->getPhys(sharedFd, memSize, phyAddr);
        if (err != 0) {
            ALOGE("getPhys failed.");
            munmap((void*)(uintptr_t)ptr, memSize);
            close(sharedFd);
            goto err;
        }

        mBuffers[i] = new ImxStreamBuffer();
        mBuffers[i]->mVirtAddr  = (void*)(uintptr_t)ptr;
        mBuffers[i]->mSize      =  memSize;
        mBuffers[i]->buffer = NULL;
        mBuffers[i]->mPhyAddr   = phyAddr;
        mBuffers[i]->mFd = sharedFd;
        mBuffers[i]->mStream = this;
        mBuffers[i]->index = i;
        mBuffers[i]->mFormatSize = getSizeByForamtRes(mFormat, mWidth, mHeight, false);
        if(mBuffers[i]->mFormatSize == 0)
            mBuffers[i]->mFormatSize = mBuffers[i]->mSize;
    }

    mRegistered = true;
    mAllocatedBuffers = mNumBuffers;

    return 0;

err:
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        if (mBuffers[i]->mVirtAddr == NULL) {
            continue;
        }

        munmap(mBuffers[i]->mVirtAddr, mBuffers[i]->mSize);
        close(mBuffers[i]->mFd);
        delete mBuffers[i];
        mBuffers[i] = NULL;
    }

    return BAD_VALUE;
}

int32_t DMAStream::freeBuffersLocked()
{
    ALOGV("%s", __func__);
    if (!mRegistered) {
        ALOGI("%s but buffer is not registered", __func__);
        return 0;
    }

    ALOGI("freeBufferToIon buffer num:%d", mAllocatedBuffers);
    for (uint32_t i = 0; i < mAllocatedBuffers; i++) {
        munmap(mBuffers[i]->mVirtAddr, mBuffers[i]->mSize);
        close(mBuffers[i]->mFd);
        delete mBuffers[i];
        mBuffers[i] = NULL;
    }

    mRegistered = false;
    mAllocatedBuffers = 0;

    return 0;
}

} // namespace android
