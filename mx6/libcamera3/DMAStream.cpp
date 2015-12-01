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

#include "DMAStream.h"

DMAStream::DMAStream(Camera* device)
    : USPStream(device), mStreamSize(0)
{
}

DMAStream::~DMAStream()
{
}

// configure device.
int32_t DMAStream::onDeviceConfigureLocked()
{
    ALOGI("%s", __func__);

    return USPStream::onDeviceConfigureLocked();
}

int32_t DMAStream::onDeviceStartLocked()
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
        cfilledbuffer.length = mStreamSize;
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

int32_t DMAStream::onDeviceStopLocked()
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

    return 0;
}

int32_t DMAStream::onFrameAcquireLocked()
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

int32_t DMAStream::onFrameReturnLocked(int32_t index, StreamBuffer& buf)
{
    ALOGV("%s: index:%d", __func__, index);
    int32_t ret = 0;
    struct v4l2_buffer cfilledbuffer;
    memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_DMABUF;
    cfilledbuffer.m.fd = buf.mFd;
    cfilledbuffer.index = index;
    cfilledbuffer.length = mStreamSize;

    ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_QBUF Failed: %s", __func__, strerror(errno));
        return BAD_VALUE;
    }

    return 0;
}

int32_t DMAStream::getDeviceBufferSize()
{
    return getFormatSize();
}

int32_t DMAStream::allocateBuffersLocked()
{
    ALOGI("%s", __func__);

    int32_t ret = USPStream::allocateBuffersLocked();
    mStreamSize = getDeviceBufferSize();

    return ret;
}

int32_t DMAStream::freeBuffersLocked()
{
    ALOGI("%s", __func__);

    return USPStream::freeBuffersLocked();
}

