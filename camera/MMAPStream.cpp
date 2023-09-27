/*
 *  Copyright 2020 NXP.
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

#define LOG_TAG "MMAPStream"

#include "MMAPStream.h"

#include <fcntl.h>
#include <linux/videodev2.h>
#include <log/log.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "CameraUtils.h"

namespace android {

MMAPStream::MMAPStream(CameraDeviceSessionHwlImpl *pSession) : VideoStream(pSession) {
    mPlane = false;
    mV4l2MemType = V4L2_MEMORY_MMAP;
}

MMAPStream::MMAPStream(CameraDeviceSessionHwlImpl *pSession, bool mplane) : VideoStream(pSession) {
    // If driver support V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE, will set mplane as
    // true, else set it as false.
    mPlane = mplane;
    mV4l2MemType = V4L2_MEMORY_MMAP;
}

MMAPStream::~MMAPStream() {}

uint32_t MMAPStream::PickValidFps(int vformat, uint32_t width, uint32_t height,
                                  uint32_t requestFps) {
    uint32_t pickedFps = requestFps;
    uint32_t valid_fps = 0;
    uint32_t fps_diff = 0;
    uint32_t fps_diff_min = 1000;

    struct v4l2_frmivalenum frmival;
    memset(&frmival, 0, sizeof(frmival));
    frmival.pixel_format = vformat;
    frmival.width = width;
    frmival.height = height;

    while (ioctl(mDev, VIDIOC_ENUM_FRAMEINTERVALS, &frmival) >= 0) {
        if ((frmival.discrete.numerator == 0) || (frmival.discrete.denominator == 0)) {
            frmival.index++;
            continue;
        }

        valid_fps = frmival.discrete.denominator / frmival.discrete.numerator;
        ALOGI("%s: res %dx%d, fps[%d] = %d", __func__, width, height, frmival.index, valid_fps);

        fps_diff = (valid_fps >= requestFps) ? (valid_fps - requestFps) : (requestFps - valid_fps);
        if (fps_diff < fps_diff_min) {
            fps_diff_min = fps_diff;
            pickedFps = valid_fps;
        }
        frmival.index++;
    }

    ALOGI("%s: requestFps requested %d, picked %d", __func__, requestFps, pickedFps);

    return pickedFps;
}

// configure device.
int32_t MMAPStream::onDeviceConfigureLocked(uint32_t format, uint32_t width, uint32_t height,
                                            uint32_t fps) {
    ALOGI("%s", __func__);
    int32_t ret = 0;
    uint32_t vfps;

    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    int vformat = convertPixelFormatToV4L2Format(format);

    ALOGI("%s, Width * Height %d x %d format %c%c%c%c, fps: %d", __func__, (int)width, (int)height,
          vformat & 0xFF, (vformat >> 8) & 0xFF, (vformat >> 16) & 0xFF, (vformat >> 24) & 0xFF,
          fps);

    int buf_type = mPlane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int num_planes = mPlane ? 1 : 0;

    vfps = PickValidFps(vformat, width, height, fps);

    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = buf_type;
    param.parm.capture.timeperframe.numerator = 1;
    param.parm.capture.timeperframe.denominator = vfps;
    ret = ioctl(mDev, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_PARM Failed: %s", __func__, strerror(errno));
        return ret;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = buf_type;

    if (buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
        fmt.fmt.pix.pixelformat = vformat;
        fmt.fmt.pix.width = width & 0xFFFFFFF8;
        fmt.fmt.pix.height = height & 0xFFFFFFF8;
    } else {
        fmt.fmt.pix_mp.pixelformat = vformat;
        fmt.fmt.pix_mp.width = width & 0xFFFFFFF8;
        fmt.fmt.pix_mp.height = height & 0xFFFFFFF8;
        fmt.fmt.pix_mp.num_planes = num_planes;
    }

    ret = ioctl(mDev, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_FMT Failed: %s", __func__, strerror(errno));
        return ret;
    }

    ret = postConfigureLocked(format, width, height, fps, vformat);

    return ret;
}

int32_t MMAPStream::onDeviceStartLocked() {
    int32_t ret = 0;

    ALOGI("%s", __func__);
    if (mDev <= 0) {
        ALOGE("%s invalid dev node", __func__);
        return BAD_VALUE;
    }

    //-------register buffers----------
    enum v4l2_buf_type bufType =
            mPlane ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE;
    struct v4l2_buffer buf;
    struct v4l2_requestbuffers req;
    struct v4l2_plane planes;
    memset(&planes, 0, sizeof(struct v4l2_plane));

    memset(&req, 0, sizeof(req));
    req.count = mNumBuffers;
    req.type = bufType;

    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(mDev, VIDIOC_REQBUFS, &req) < 0) {
        ALOGE("%s VIDIOC_REQBUFS failed", __func__);
        return BAD_VALUE;
    }

    for (uint32_t i = 0; i < mNumBuffers && i < MAX_STREAM_BUFFERS; i++) {
        memset(&buf, 0, sizeof(buf));

        if (mPlane) {
            buf.m.planes = &planes;
            buf.length = 1; /* plane num */
        }

        buf.type = bufType;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(mDev, VIDIOC_QUERYBUF, &buf) < 0) {
            ALOGE("%s VIDIOC_QUERYBUF error", __func__);
            ret = BAD_VALUE;
            goto err;
        }

        struct v4l2_exportbuffer expbuf;
        memset(&expbuf, 0, sizeof(expbuf));
        expbuf.type = buf.type;
        expbuf.index = i;
        if (ioctl(mDev, VIDIOC_EXPBUF, &expbuf) < 0) {
            ALOGE("VIDIOC_EXPBUF: %s", strerror(errno));
            ret = BAD_VALUE;
            goto err;
        }

        mBuffers[i] = new ImxStreamBuffer();
        memset(mBuffers[i], 0, sizeof(ImxStreamBuffer));
        mBuffers[i]->mFd = expbuf.fd;
        mBuffers[i]->index = i;
        mBuffers[i]->mStream = this;

        if (mPlane) {
            mBuffers[i]->mPhyAddr = (size_t)buf.m.planes->m.mem_offset;
            mBuffers[i]->mSize = buf.m.planes->length;
        } else {
            mBuffers[i]->mPhyAddr = buf.m.offset;
            mBuffers[i]->mSize = buf.length;
        }

        mBuffers[i]->mFormatSize = getSizeByForamtRes(mFormat, mWidth, mHeight, false);
        if (mBuffers[i]->mFormatSize == 0)
            mBuffers[i]->mFormatSize = mBuffers[i]->mSize;

        mBuffers[i]->mVirtAddr = (void *)mmap(NULL, mBuffers[i]->mSize, PROT_READ | PROT_WRITE,
                                              MAP_SHARED, mDev, mBuffers[i]->mPhyAddr);
        if (mBuffers[i]->mVirtAddr == MAP_FAILED) {
            ALOGE("%s: mmap buf %d, size %zu, fd %d, offset 0x%x failed, errno %d", __func__, i,
                  mBuffers[i]->mSize, mDev, (unsigned int)mBuffers[i]->mPhyAddr, errno);
            ret = BAD_VALUE;
            goto err;
        }

        if (!mPlane) {
            // For mx6s capture driver, it need to query twice to get the phy
            // address
            if (ioctl(mDev, VIDIOC_QUERYBUF, &buf) < 0) {
                ALOGE("%s VIDIOC_QUERYBUF error", __func__);
                ret = BAD_VALUE;
                goto err;
            }
            mBuffers[i]->mPhyAddr = buf.m.offset;
        }

        memset(mBuffers[i]->mVirtAddr, 0xFF, mBuffers[i]->mSize);
        SetBufferHandle(*mBuffers[i]);

        ALOGI("%s, register buffer, phy 0x%lx, virt %p, size %d", __func__, mBuffers[i]->mPhyAddr,
              mBuffers[i]->mVirtAddr, (int)mBuffers[i]->mSize);
    }

    //----------qbuf----------
    struct v4l2_buffer cfilledbuffer;
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        memset(&cfilledbuffer, 0, sizeof(struct v4l2_buffer));

        if (mPlane) {
            memset(&planes, 0, sizeof(planes));
            cfilledbuffer.m.planes = &planes;
            cfilledbuffer.m.planes->length = mBuffers[i]->mSize;
            cfilledbuffer.length = 1;
            cfilledbuffer.m.planes->m.mem_offset = mBuffers[i]->mPhyAddr;
        } else {
            cfilledbuffer.m.offset = mBuffers[i]->mPhyAddr;
            cfilledbuffer.length = mBuffers[i]->mSize;
            ALOGI("%s VIDIOC_QBUF phy:0x%lx", __func__, mBuffers[i]->mPhyAddr);
        }

        cfilledbuffer.type = bufType;
        cfilledbuffer.memory = V4L2_MEMORY_MMAP;
        cfilledbuffer.index = i;

        ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
        if (ret < 0) {
            ALOGE("%s VIDIOC_QBUF Failed", __func__);
            ret = BAD_VALUE;
            goto err;
        }
    }

    //-------stream on-------
    ALOGI("before VIDIOC_STREAMON");
    ret = ioctl(mDev, VIDIOC_STREAMON, &bufType);
    if (ret < 0) {
        ALOGE("%s VIDIOC_STREAMON failed:%s", __func__, strerror(errno));
        ret = BAD_VALUE;
        goto err;
    }
    ALOGI("after VIDIOC_STREAMON");

    mOmitFrames = mOmitFrmCount;
    mbStart = true;

    return 0;

err:
    ALOGI("%s: clean up before return error", __func__);

    for (uint32_t i = 0; i < MAX_STREAM_BUFFERS; i++) {
        if (mBuffers[i] != NULL && mBuffers[i]->mVirtAddr != NULL && mBuffers[i]->mSize > 0) {
            munmap(mBuffers[i]->mVirtAddr, mBuffers[i]->mSize);
            if (mBuffers[i]->mFd > 0)
                close(mBuffers[i]->mFd);

            fsl::Memory *handle = (fsl::Memory *)mBuffers[i]->buffer;
            if (handle)
                delete handle;

            delete mBuffers[i];
            mBuffers[i] = NULL;
        }
    }

    memset(&req, 0, sizeof(req));
    req.count = 0;
    req.type = bufType;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(mDev, VIDIOC_REQBUFS, &req) < 0) {
        ALOGW("%s VIDIOC_REQBUFS with count 0 failed: %s", __func__, strerror(errno));
    }

    return ret;
}

int32_t MMAPStream::onDeviceStopLocked() {
    ALOGI("%s", __func__);
    int32_t ret = 0;
    enum v4l2_buf_type bufType;
    struct v4l2_requestbuffers req;

    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    for (uint32_t i = 0; i < MAX_STREAM_BUFFERS; i++) {
        if (mBuffers[i] != NULL && mBuffers[i]->mVirtAddr != NULL && mBuffers[i]->mSize > 0) {
            munmap(mBuffers[i]->mVirtAddr, mBuffers[i]->mSize);
            if (mBuffers[i]->mFd > 0)
                close(mBuffers[i]->mFd);

            fsl::Memory *handle = (fsl::Memory *)mBuffers[i]->buffer;
            if (handle)
                delete handle;

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

    memset(&req, 0, sizeof(req));
    req.count = 0;
    req.type = bufType;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(mDev, VIDIOC_REQBUFS, &req) < 0) {
        ALOGI("%s VIDIOC_REQBUFS failed: %s", __func__, strerror(errno));
        return BAD_VALUE;
    }

    mbStart = false;

    return 0;
}

int32_t MMAPStream::onFrameReturn(ImxStreamBuffer &buf) {
    ALOGV("%s", __func__);

    Mutex::Autolock _l(mV4l2Lock);

    int32_t ret = 0;
    struct v4l2_buffer cfilledbuffer;
    struct v4l2_plane planes;

    memset(&planes, 0, sizeof(struct v4l2_plane));
    memset(&cfilledbuffer, 0, sizeof(struct v4l2_buffer));

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
    cfilledbuffer.index = buf.index;

    ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
    if (ret < 0) {
        ALOGE("%s VIDIOC_QBUF Failed", __func__);
        return BAD_VALUE;
    }

    ALOGV("VIDIOC_QBUF idx %d, ret %d", buf.index, ret);

    return ret;
}

} // namespace android
