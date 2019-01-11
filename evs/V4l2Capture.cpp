/*
 * Copyright 2019 NXP.
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
#include <stdlib.h>
#include <error.h>
#include <errno.h>
#include <memory.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <log/log.h>
#include <inttypes.h>

#include "assert.h"

#include "V4l2Capture.h"

V4l2Capture::V4l2Capture(const char *deviceName)
    : EvsCamera(deviceName)
{
    mV4lFormat = getV4lFormat(mFormat);
}

V4l2Capture::~V4l2Capture()
{
}

int V4l2Capture::getCaptureMode(int fd, int width, int height)
{
    int index = 0;
    int ret = 0;
    int capturemode = 0;
    struct v4l2_frmsizeenum vid_frmsize;

    if (fd < 0) {
        ALOGE("%s invalid fd", __func__);
        return -1;
    }

    while (ret == 0) {
        vid_frmsize.index = index++;
        vid_frmsize.pixel_format = mV4lFormat;
        ret = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &vid_frmsize);
        if ((vid_frmsize.discrete.width == (uint32_t)width) &&
            (vid_frmsize.discrete.height == (uint32_t)height)
            && (ret == 0)) {
            capturemode = vid_frmsize.index;
            break;
        }
    }

    return capturemode;
}

int V4l2Capture::getV4lFormat(int format)
{
    int v4lFormat = -1;
    switch (format) {
        case fsl::FORMAT_YUYV:
            v4lFormat = V4L2_PIX_FMT_YUYV;
            break;
        case fsl::FORMAT_NV12:
            v4lFormat = V4L2_PIX_FMT_NV12;
            break;
        default:
            ALOGE("%s unsupported format:0x%x", __func__, format);
            break;
    }

    return v4lFormat;
}

// open v4l2 device.
bool V4l2Capture::onOpen(const char* deviceName)
{
    // If we want polling interface for getting frames, we would use O_NONBLOCK
    // int mDeviceFd = open(deviceName, O_RDWR | O_NONBLOCK, 0);
    int fd = -1;
    fd = ::open(deviceName, O_RDWR, 0);
    if (fd < 0) {
        ALOGE("failed to open device %s (%d = %s)",
                deviceName, errno, strerror(errno));
        return false;
    }

    v4l2_capability caps;
    {
        int result = ioctl(fd, VIDIOC_QUERYCAP, &caps);
        if (result  < 0) {
            ALOGE("failed to get device caps for %s (%d = %s)",
                    deviceName, errno, strerror(errno));
            return false;
        }
    }

    // Report device properties
    ALOGI("Open Device: %s (fd=%d)", deviceName, fd);
    ALOGI("  Driver: %s", caps.driver);
    ALOGI("  Card: %s", caps.card);
    ALOGI("  Version: %u.%u.%u",
            (caps.version >> 16) & 0xFF,
            (caps.version >> 8)  & 0xFF,
            (caps.version)       & 0xFF);
    ALOGI("  All Caps: %08X", caps.capabilities);
    ALOGI("  Dev Caps: %08X", caps.device_caps);

    // Enumerate the available capture formats (if any)
    ALOGI("Supported capture formats:");
    v4l2_fmtdesc formatDescriptions;
    formatDescriptions.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    for (int i=0; true; i++) {
        formatDescriptions.index = i;
        if (ioctl(fd, VIDIOC_ENUM_FMT, &formatDescriptions) == 0) {
            ALOGI("  %2d: %s 0x%08X 0x%X",
                   i,
                   formatDescriptions.description,
                   formatDescriptions.pixelformat,
                   formatDescriptions.flags
            );
        }
        else {
            // No more formats available
            break;
        }
    }

    // Verify we can use this device for video capture
    if (!(caps.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) ||
        !(caps.capabilities & V4L2_CAP_STREAMING)) {
        // Can't do streaming capture.
        ALOGE("Streaming capture not supported by %s.", deviceName);
        return false;
    }

    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    param.parm.capture.timeperframe.numerator   = 1;
    param.parm.capture.timeperframe.denominator = 30;
    param.parm.capture.capturemode = getCaptureMode(fd, mWidth, mHeight);
    int ret = ioctl(fd, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_PARM Failed: %s", __func__, strerror(errno));
        return false;
    }

    // Set our desired output format
    v4l2_format format;
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    format.fmt.pix_mp.pixelformat = mV4lFormat;
    format.fmt.pix_mp.width = mWidth;
    format.fmt.pix_mp.height = mHeight;
    // TODO:  Do we need to specify this?
    format.fmt.pix_mp.field = V4L2_FIELD_ALTERNATE;
    format.fmt.pix_mp.num_planes = 1;
    ALOGI("Requesting format %c%c%c%c (0x%08X)",
          ((char*)&format.fmt.pix.pixelformat)[0],
          ((char*)&format.fmt.pix.pixelformat)[1],
          ((char*)&format.fmt.pix.pixelformat)[2],
          ((char*)&format.fmt.pix.pixelformat)[3],
          format.fmt.pix.pixelformat);
    if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
        ALOGE("VIDIOC_S_FMT: %s", strerror(errno));
    }

    // Report the current output format
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd, VIDIOC_G_FMT, &format) == 0) {

        mV4lFormat = format.fmt.pix_mp.pixelformat;
        mWidth  = format.fmt.pix_mp.width;
        mHeight = format.fmt.pix_mp.height;
        ALOGI("Current output format:  fmt=0x%X, %dx%d",
               format.fmt.pix_mp.pixelformat,
               format.fmt.pix_mp.width,
               format.fmt.pix_mp.height
        );
    }
    else {
        ALOGE("VIDIOC_G_FMT: %s", strerror(errno));
        return false;
    }

    std::unique_lock <std::mutex> lock(mLock);
    mDeviceFd = fd;
    // Make sure we're initialized to the STOPPED state
    mRunMode = STOPPED;

    // Ready to go!
    return true;
}

bool V4l2Capture::isOpen()
{
    std::unique_lock <std::mutex> lock(mLock);
    return mDeviceFd >= 0;
}

void V4l2Capture::onClose()
{
    ALOGD("V4l2Capture::close");

    if (!isOpen()) {
        return;
    }

    int fd = -1;
    {
        std::unique_lock <std::mutex> lock(mLock);
        // Stream should be stopped first!
        assert(mRunMode == STOPPED);
        fd = mDeviceFd;
        mDeviceFd = -1;
    }

    ALOGD("closing video device file handled %d", fd);
    ::close(fd);
}

bool V4l2Capture::onStart()
{
    int fd = -1;
    {
        std::unique_lock <std::mutex> lock(mLock);
        fd = mDeviceFd;
    }

    if (fd < 0) {
        // The device is not opened.
        ALOGE("%s device not opened", __func__);
        return false;
    }

    // Tell the L4V2 driver to prepare our streaming buffers
    v4l2_requestbuffers bufrequest;
    bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    bufrequest.memory = V4L2_MEMORY_DMABUF;
    bufrequest.count = CAMERA_BUFFER_NUM;
    if (ioctl(fd, VIDIOC_REQBUFS, &bufrequest) < 0) {
        ALOGE("VIDIOC_REQBUFS: %s", strerror(errno));
        return false;
    }

    fsl::Memory *buffer = nullptr;
    for (int i = 0; i < CAMERA_BUFFER_NUM; i++) {
        // Get the information on the buffer that was created for us
        {
            std::unique_lock <std::mutex> lock(mLock);
            buffer = mBuffers[i];
        }
        if (buffer == nullptr) {
            ALOGE("%s buffer not ready!", __func__);
            return false;
        }

        struct v4l2_plane planes;
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        memset(&planes, 0, sizeof(planes));
        buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        buf.memory   = V4L2_MEMORY_DMABUF;
        buf.m.planes = &planes;
        buf.m.planes->m.fd = buffer->fd;
        buf.length = 1;
        buf.m.planes->length = buffer->size;
        buf.index    = i;
        // Queue the first capture buffer
        if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
            ALOGE("%s VIDIOC_QBUF: %s", __func__, strerror(errno));
            return false;
        }

        ALOGI("Buffer description:");
        ALOGI("phys: 0x%" PRIx64, buffer->phys);
        ALOGI("length: %d", buf.m.planes[0].length);
    }
    // Start the video stream
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
        ALOGE("VIDIOC_STREAMON: %s", strerror(errno));
        return false;
    }

    ALOGD("Stream started.");
    return true;
}


void V4l2Capture::onStop()
{
    int fd = -1;
    int index;
    {
        std::unique_lock <std::mutex> lock(mLock);
        fd = mDeviceFd;
        index = mDeqIdx;
    }

    onFrameReturn(index);
    // Stop the underlying video stream (automatically empties the buffer queue)
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd, VIDIOC_STREAMOFF, &type) < 0) {
        ALOGE("VIDIOC_STREAMOFF: %s", strerror(errno));
    }

    // Tell the L4V2 driver to release our streaming buffers
    v4l2_requestbuffers bufrequest;
    bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    bufrequest.memory = V4L2_MEMORY_DMABUF;
    bufrequest.count = 0;
    if (ioctl(fd, VIDIOC_REQBUFS, &bufrequest) < 0) {
        ALOGE("VIDIOC_REQBUFS count = 0 failed: %s", strerror(errno));
    }
}

bool V4l2Capture::onFrameReturn(int index)
{
    // We're giving the frame back to the system, so clear the "ready" flag
    ALOGV("%s  index: %d", __func__, index);
    if (index < 0 || index >= CAMERA_BUFFER_NUM) {
        ALOGE("%s invalid index:%d", __func__, index);
        return false;
    }

    int fd = -1;
    fsl::Memory *buffer = nullptr;
    {
        std::unique_lock <std::mutex> lock(mLock);
        fd = mDeviceFd;
        buffer = mBuffers[index];
    }

    if (fd < 0 || buffer == nullptr) {
        ALOGE("%s invalid fd or buffer", __func__);
        return false;
    }

    struct v4l2_buffer buf;
    struct v4l2_plane planes;
    memset(&buf, 0, sizeof(buf));
    memset(&planes, 0, sizeof(struct v4l2_plane));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.m.planes = &planes;
    buf.index = index;
    buf.length = 1;
    buf.m.planes->length = buffer->size;
    buf.m.planes->m.fd = buffer->fd;

    // Requeue the buffer to capture the next available frame
    if (ioctl(fd, VIDIOC_QBUF, &buf) < 0) {
        ALOGE("%s VIDIOC_QBUF: %s", __func__, strerror(errno));
        return false;
    }
    return true;
}


// This runs on a background thread to receive and dispatch video frames
fsl::Memory* V4l2Capture::onFrameCollect(int &index)
{
    int fd = -1;
    fsl::Memory *buffer = nullptr;
    {
        std::unique_lock <std::mutex> lock(mLock);
        fd = mDeviceFd;
    }

    if (fd < 0) {
        ALOGE("%s invalid fd", __func__);
        return nullptr;
    }

    struct v4l2_buffer buf;
    struct v4l2_plane planes;

    memset(&buf, 0, sizeof(buf));
    memset(&planes, 0, sizeof(struct v4l2_plane));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_DMABUF;
    buf.m.planes = &planes;
    buf.length = 1;

    // Wait for a buffer to be ready
    if (ioctl(fd, VIDIOC_DQBUF, &buf) < 0) {
        ALOGE("VIDIOC_DQBUF: %s", strerror(errno));
        return nullptr;
    }

    {
        std::unique_lock <std::mutex> lock(mLock);
        buffer = mBuffers[buf.index];
        mDeqIdx = buf.index;
    }

    index = buf.index;
    ALOGV("%s index: %d", __func__, index);
    return buffer;
}
