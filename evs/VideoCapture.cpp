/*
 * Copyright (C) 2016 The Android Open Source Project
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
#include <cutils/log.h>

#include "assert.h"

#include "VideoCapture.h"


// NOTE:  This developmental code does not properly clean up resources in case of failure
//        during the resource setup phase.  Of particular note is the potential to leak
//        the file descriptor.  This must be fixed before using this code for anything but
//        experimentation.
bool VideoCapture::open(const char* deviceName) {
    // If we want a polling interface for getting frames, we would use O_NONBLOCK
//    int mDeviceFd = open(deviceName, O_RDWR | O_NONBLOCK, 0);
    mDeviceFd = ::open(deviceName, O_RDWR, 0);
    if (mDeviceFd < 0) {
        ALOGE("failed to open device %s (%d = %s)", deviceName, errno, strerror(errno));
        return false;
    }

    v4l2_capability caps;
    {
        int result = ioctl(mDeviceFd, VIDIOC_QUERYCAP, &caps);
        if (result  < 0) {
            ALOGE("failed to get device caps for %s (%d = %s)", deviceName, errno, strerror(errno));
            return false;
        }
    }

    // Report device properties
    ALOGI("Open Device: %s (fd=%d)", deviceName, mDeviceFd);
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
        if (ioctl(mDeviceFd, VIDIOC_ENUM_FMT, &formatDescriptions) == 0) {
            ALOGI("  %2d: %s 0x%08X 0x%X",
                   i,
                   formatDescriptions.description,
                   formatDescriptions.pixelformat,
                   formatDescriptions.flags
            );
        } else {
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

    // Set our desired output format
    v4l2_format format;
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    format.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_YUYV; // Could/should we request V4L2_PIX_FMT_NV21?
    format.fmt.pix_mp.width = 640;                     // TODO:  Can we avoid hard coding dimensions?
    format.fmt.pix_mp.height = 480;                    // For now, this works with available hardware
    format.fmt.pix_mp.field = V4L2_FIELD_ALTERNATE;    // TODO:  Do we need to specify this?
    format.fmt.pix_mp.num_planes = 1;    // TODO:  Do we need to specify this?
    ALOGI("Requesting format %c%c%c%c (0x%08X)",
          ((char*)&format.fmt.pix.pixelformat)[0],
          ((char*)&format.fmt.pix.pixelformat)[1],
          ((char*)&format.fmt.pix.pixelformat)[2],
          ((char*)&format.fmt.pix.pixelformat)[3],
          format.fmt.pix.pixelformat);
    if (ioctl(mDeviceFd, VIDIOC_S_FMT, &format) < 0) {
        ALOGE("VIDIOC_S_FMT: %s", strerror(errno));
    }

    // Report the current output format
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(mDeviceFd, VIDIOC_G_FMT, &format) == 0) {

        mFormat = format.fmt.pix_mp.pixelformat;
        mWidth  = format.fmt.pix_mp.width;
        mHeight = format.fmt.pix_mp.height;
        mStride = format.fmt.pix_mp.plane_fmt[0].bytesperline;

        ALOGI("Current output format:  fmt=0x%X, %dx%d, mStride=%d",
               format.fmt.pix_mp.pixelformat,
               format.fmt.pix_mp.width,
               format.fmt.pix_mp.height,
               mStride
        );
    } else {
        ALOGE("VIDIOC_G_FMT: %s", strerror(errno));
        return false;
    }

    // Make sure we're initialized to the STOPPED state
    mRunMode = STOPPED;
    mFrameReady = false;

    // Ready to go!
    return true;
}


void VideoCapture::close() {
    ALOGD("VideoCapture::close");
    // Stream should be stopped first!
    assert(mRunMode == STOPPED);

    if (isOpen()) {
        ALOGD("closing video device file handled %d", mDeviceFd);
        ::close(mDeviceFd);
        mDeviceFd = -1;
    }
}


bool VideoCapture::startStream(std::function<void(VideoCapture*, imageBuffer*, void*)> callback) {
    // Set the state of our background thread
    int prevRunMode = mRunMode.fetch_or(RUN);
    if (prevRunMode & RUN) {
        // The background thread is already running, so we can't start a new stream
        ALOGE("Already in RUN state, so we can't start a new streaming thread");
        return false;
    }

    // Tell the L4V2 driver to prepare our streaming buffers
    v4l2_requestbuffers bufrequest;
    bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    bufrequest.memory = V4L2_MEMORY_MMAP;
    bufrequest.count = 2;
    if (ioctl(mDeviceFd, VIDIOC_REQBUFS, &bufrequest) < 0) {
        ALOGE("VIDIOC_REQBUFS: %s", strerror(errno));
        return false;
    }

    for (int i = 0; i < 2; i++) {
        // Get the information on the buffer that was created for us
        struct v4l2_plane planes;
        memset(&mBufferInfo[i], 0, sizeof(mBufferInfo[0]));
        mBufferInfo[i].type     = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        mBufferInfo[i].memory   = V4L2_MEMORY_MMAP;
        mBufferInfo[i].m.planes = &planes;
        mBufferInfo[i].length   = 1;
        mBufferInfo[i].index    = i;
        if (ioctl(mDeviceFd, VIDIOC_QUERYBUF, &mBufferInfo[i]) < 0) {
            ALOGE("VIDIOC_QUERYBUF: %s", strerror(errno));
            return false;
        }

        ALOGI("Buffer description:");
        ALOGI("offset: %d", mBufferInfo[i].m.planes[0].m.mem_offset);
        ALOGI("length: %d", mBufferInfo[i].m.planes[0].length);

        // Get a pointer to the buffer contents by mapping into our address space
        mPixelBuffer[i] = mmap(
                NULL,
                mBufferInfo[i].m.planes[0].length,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                mDeviceFd,
                mBufferInfo[i].m.planes[0].m.mem_offset
        );
        if( mPixelBuffer[i] == MAP_FAILED) {
            ALOGE("mmap: %s", strerror(errno));
            return false;
        }

        memset(mPixelBuffer[i], 0, mBufferInfo[i].length);
        ALOGI("Buffer mapped at %p", mPixelBuffer[i]);
        // Queue the first capture buffer
        if (ioctl(mDeviceFd, VIDIOC_QBUF, &mBufferInfo[i]) < 0) {
            ALOGE("VIDIOC_QBUF: %s", strerror(errno));
            return false;
        }
    }
    // Start the video stream
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(mDeviceFd, VIDIOC_STREAMON, &type) < 0) {
        ALOGE("VIDIOC_STREAMON: %s", strerror(errno));
        return false;
    }

    // Remember who to tell about new frames as they arrive
    mCallback = callback;
    // Fire up a thread to receive and dispatch the video frames
    mCaptureThread = std::thread([this](){ collectFrames(); });

    ALOGD("Stream started.");
    return true;
}


void VideoCapture::stopStream() {
    // Tell the background thread to stop
    int i;
    int prevRunMode = mRunMode.fetch_or(STOPPING);
    if (prevRunMode == STOPPED) {
        // The background thread wasn't running, so set the flag back to STOPPED
        mRunMode = STOPPED;
    } else if (prevRunMode & STOPPING) {
        ALOGE("stopStream called while stream is already stopping.  Reentrancy is not supported!");
        return;
    } else {
        // Block until the background thread is stopped
        if (mCaptureThread.joinable()) {
            mCaptureThread.join();
        }

        // Stop the underlying video stream (automatically empties the buffer queue)
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (ioctl(mDeviceFd, VIDIOC_STREAMOFF, &type) < 0) {
            ALOGE("VIDIOC_STREAMOFF: %s", strerror(errno));
        }

        ALOGD("Capture thread stopped.");
    }


    // Unmap the buffers we allocated
    for (i = 0; i < 2; i++)
        munmap(mPixelBuffer[i], mBufferInfo[i].length);

    // Tell the L4V2 driver to release our streaming buffers
    v4l2_requestbuffers bufrequest;
    bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    bufrequest.memory = V4L2_MEMORY_MMAP;
    bufrequest.count = 0;
    ioctl(mDeviceFd, VIDIOC_REQBUFS, &bufrequest);

    // Drop our reference to the frame delivery callback interface
    mCallback = nullptr;
}


void VideoCapture::markFrameReady() {
    mFrameReady = true;
};


bool VideoCapture::returnFrame() {
    // We're giving the frame back to the system, so clear the "ready" flag
    ALOGI("returnFrame  index %d", currentIndex);
    mFrameReady = false;
    struct v4l2_buffer buf;
    struct v4l2_plane planes;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = &planes;
    buf.index = currentIndex;
    buf.length = 1;
    buf.m.planes[0].length = mBufferInfo[currentIndex].m.planes[0].length;

    // Requeue the buffer to capture the next available frame
    if (ioctl(mDeviceFd, VIDIOC_QBUF, &buf) < 0) {
        ALOGE("VIDIOC_QBUF: %s", strerror(errno));
        return false;
    }
    return true;
}


// This runs on a background thread to receive and dispatch video frames
void VideoCapture::collectFrames() {
    struct v4l2_buffer buf;
    struct v4l2_plane planes;

    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.m.planes = &planes;
    buf.length = 1;

    // Run until our atomic signal is cleared
    while (mRunMode == RUN) {
        // Wait for a buffer to be ready

        if (ioctl(mDeviceFd, VIDIOC_DQBUF, &buf) < 0) {
            ALOGE("VIDIOC_DQBUF: %s", strerror(errno));
            break;
        }
        markFrameReady();

        currentIndex = buf.index ;
        // If a callback was requested per frame, do that now
        if (mCallback) {
            mCallback(this, &mBufferInfo[currentIndex], mPixelBuffer[currentIndex]);
        }
    }

    // Mark ourselves stopped
    ALOGD("VideoCapture thread ending");
    mRunMode = STOPPED;
}
