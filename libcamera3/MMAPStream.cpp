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

#include "MMAPStream.h"

MMAPStream::MMAPStream(Camera* device)
    : VideoStream(device)
{
    mPlane = false;
}

MMAPStream::MMAPStream(Camera *device, bool mplane) : VideoStream(device) {
    // If driver support V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, will set mplane as
    // true, else set it as false.
    mPlane = mplane;
    mV4l2MemType = V4L2_MEMORY_MMAP;
}

MMAPStream::~MMAPStream()
{
}

// configure device.
int32_t MMAPStream::onDeviceConfigureLocked()
{
    ALOGI("%s", __func__);
    int32_t ret = 0;
    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    int32_t vformat;
    vformat = convertPixelFormatToV4L2Format(mFormat);

    ALOGI("Width * Height %d x %d format %c%c%c%c, fps: %d",
          mWidth, mHeight, vformat&0xFF, (vformat>>8)&0xFF,
          (vformat>>16)&0xFF, (vformat>>24)&0xFF, mCamera->getFps(mWidth, mHeight, mFps));

    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    param.parm.capture.timeperframe.numerator   = 1;
    param.parm.capture.timeperframe.denominator = mCamera->getFps(mWidth, mHeight, mFps);
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

    // Special stride alignment for YU12
    if (vformat == v4l2_fourcc('Y', 'U', '1', '2')){
        // Goolge define the the stride and c_stride for YUV420 format
        // y_size = stride * height
        // c_stride = ALIGN(stride/2, 16)
        // c_size = c_stride * height/2
        // size = y_size + c_size * 2
        // cr_offset = y_size
        // cb_offset = y_size + c_size
        // int stride = (width+15)/16*16;
        // int c_stride = (stride/2+16)/16*16;
        // y_size = stride * height
        // c_stride = ALIGN(stride/2, 16)
        // c_size = c_stride * height/2
        // size = y_size + c_size * 2
        // cr_offset = y_size
        // cb_offset = y_size + c_size

        // GPU and IPU take below stride calculation
        // GPU has the Y stride to be 32 alignment, and UV stride to be
        // 16 alignment.
        // IPU have the Y stride to be 2x of the UV stride alignment
        int32_t stride = (mWidth+31)/32*32;
        int32_t c_stride = (stride/2+15)/16*16;
        fmt.fmt.pix.bytesperline = stride;
        fmt.fmt.pix.sizeimage    = stride*mHeight+c_stride * mHeight;
        ALOGI("Special handling for YV12 on Stride %d, size %d",
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

int32_t MMAPStream::onDeviceStartLocked()
{
    ALOGI("%s", __func__);
    if (mDev <= 0) {
        ALOGE("%s invalid dev node", __func__);
        return BAD_VALUE;
    }

    //-------register buffers----------
    struct v4l2_buffer buf;
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

    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(mDev, VIDIOC_REQBUFS, &req) < 0) {
        ALOGE("%s VIDIOC_REQBUFS failed", __func__);
        return BAD_VALUE;
    }

    for (uint32_t i = 0; i < mNumBuffers; i++) {
        memset(&buf, 0, sizeof (buf));

        if (mPlane) {
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.m.planes = &planes;
            buf.length = 1; /* plane num */
        } else {
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        }

        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(mDev, VIDIOC_QUERYBUF, &buf) < 0) {
            ALOGE("%s VIDIOC_QUERYBUF error", __func__);
            return BAD_VALUE;
        }

        mBuffers[i] = new StreamBuffer();

        if (mPlane) {
            mBuffers[i]->mPhyAddr = (size_t)buf.m.planes->m.mem_offset;
            mBuffers[i]->mSize = buf.m.planes->length;
        } else {
            mBuffers[i]->mPhyAddr = buf.m.offset;
            mBuffers[i]->mSize = buf.length;
        }

        mBuffers[i]->mVirtAddr = (void *)mmap(NULL, mBuffers[i]->mSize,
                    PROT_READ | PROT_WRITE, MAP_SHARED, mDev,
                    mBuffers[i]->mPhyAddr);
        mBuffers[i]->mStream = this;

        if (!mPlane) {
            // For mx6s capture driver, it need to query twice to get the phy
            // address
            if (ioctl(mDev, VIDIOC_QUERYBUF, &buf) < 0) {
                ALOGE("%s VIDIOC_QUERYBUF error", __func__);
                return BAD_VALUE;
            }
            mBuffers[i]->mPhyAddr = buf.m.offset;
        }

        memset(mBuffers[i]->mVirtAddr, 0xFF, mBuffers[i]->mSize);
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
            cfilledbuffer.m.planes->length = mBuffers[i]->mSize;
            cfilledbuffer.length = 1;
            cfilledbuffer.m.planes->m.mem_offset = mBuffers[i]->mPhyAddr;
        } else {
            cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            cfilledbuffer.m.offset = mBuffers[i]->mPhyAddr;
            cfilledbuffer.length = mBuffers[i]->mSize;
            ALOGI("%s VIDIOC_QBUF phy:0x%x", __func__, mBuffers[i]->mPhyAddr);
        }

        cfilledbuffer.memory = V4L2_MEMORY_MMAP;
        cfilledbuffer.index = i;

        ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
        if (ret < 0) {
            ALOGE("%s VIDIOC_QBUF Failed", __func__);
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

    ret = ioctl(mDev, VIDIOC_STREAMON, &bufType);
    if (ret < 0) {
        ALOGE("%s VIDIOC_STREAMON failed:%s", __func__, strerror(errno));
        return ret;
    }

    mOmitFrames = mOmitFrmCount;

    return 0;
}

int32_t MMAPStream::onDeviceStopLocked()
{
    ALOGI("%s", __func__);
    int32_t ret = 0;
    enum v4l2_buf_type bufType;
    struct v4l2_requestbuffers req;

    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    for (uint32_t i = 0; i < MAX_STREAM_BUFFERS; i++) {
        if (mBuffers[i] != NULL && mBuffers[i]->mVirtAddr != NULL
                                && mBuffers[i]->mSize > 0) {
            munmap(mBuffers[i]->mVirtAddr, mBuffers[i]->mSize);
            delete mBuffers[i];
            mBuffers[i] = NULL;
        }
    }

    if (mPlane) {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    }

    ret = ioctl(mDev, VIDIOC_STREAMOFF, &bufType);
    if (ret < 0) {
        ALOGE("%s VIDIOC_STREAMOFF failed: %s", __func__, strerror(errno));
        return ret;
    }

    memset(&req, 0, sizeof (req));
    req.count = 0;
    req.type = bufType;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(mDev, VIDIOC_REQBUFS, &req) < 0) {
        ALOGI("%s VIDIOC_REQBUFS failed: %s", __func__, strerror(errno));
        return BAD_VALUE;
    }

    return 0;
}

int32_t MMAPStream::onFrameAcquireLocked()
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

    cfilledbuffer.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(mDev, VIDIOC_DQBUF, &cfilledbuffer);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_DQBUF Failed", __func__);
        return -1;
    }

    if (mOmitFrames > 0) {
        ALOGI("%s omit frame", __func__);
        ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
        if (ret < 0) {
          ALOGE("%s VIDIOC_QBUF Failed", __func__);
          return BAD_VALUE;
        }
        mOmitFrames--;
        goto capture_data;
    }

    return cfilledbuffer.index;
}

int32_t MMAPStream::onFrameReturnLocked(int32_t index, StreamBuffer& buf)
{
    ALOGV("%s", __func__);
    int32_t ret = 0;
    struct v4l2_buffer cfilledbuffer;
    struct v4l2_plane planes;
    memset(&planes, 0, sizeof(struct v4l2_plane));

    memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));

    if (mPlane) {
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        cfilledbuffer.m.planes = &planes;
        cfilledbuffer.m.planes->m.mem_offset = buf.mPhyAddr;
        cfilledbuffer.m.planes->length = buf.mSize;
        cfilledbuffer.length = 1;
    } else {
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cfilledbuffer.m.offset = buf.mPhyAddr;
        cfilledbuffer.length = buf.mSize;
    }

    cfilledbuffer.memory = V4L2_MEMORY_MMAP;
    cfilledbuffer.index = index;

    ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
    if (ret < 0) {
        ALOGE("%s VIDIOC_QBUF Failed", __func__);
        return BAD_VALUE;
    }

    return ret;
}

