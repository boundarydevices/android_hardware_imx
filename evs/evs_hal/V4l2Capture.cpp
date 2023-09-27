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
#include "V4l2Capture.h"

#include <android-base/file.h>
#include <errno.h>
#include <error.h>
#include <fcntl.h>
#include <inttypes.h>
#include <log/log.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "assert.h"

// #define DEBUG_V4L2_CAMERA
#ifdef DEBUG_V4L2_CAMERA
static int nub;
#endif
V4l2Capture::V4l2Capture(const char* deviceName, const char* videoName, __u32 width, __u32 height,
                         int format, const camera_metadata_t* metadata)
      : EvsCamera(videoName, metadata) {
    mV4lFormat = getV4lFormat(format);
    mWidth = width;
    mHeight = height;
    mDecreasenum = 0;

    // check whether the camera is logic camera
    mIslogicCamera = isLogicalCamera(metadata);
    if (mIslogicCamera)
        mPhysicalCamera = getPhysicalCameraInLogic(metadata);
    else
        mPhysicalCamera.emplace(deviceName);
}

V4l2Capture::~V4l2Capture() {}

int V4l2Capture::getCaptureMode(int fd, int width, int height) {
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
            (vid_frmsize.discrete.height == (uint32_t)height) && (ret == 0)) {
            capturemode = vid_frmsize.index;
            break;
        }
    }

    return capturemode;
}

int V4l2Capture::getV4lFormat(int format) {
    int v4lFormat = -1;
    switch (format) {
        case HAL_PIXEL_FORMAT_YCBCR_422_I:
            v4lFormat = V4L2_PIX_FMT_YUYV;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
            v4lFormat = V4L2_PIX_FMT_NV12;
            break;
        case HAL_PIXEL_FORMAT_RGB_888:
            v4lFormat = V4L2_PIX_FMT_RGB24;
            break;
        default:
            ALOGE("%s unsupported format:0x%x", __func__, format);
            break;
    }

    return v4lFormat;
}

// support both logic camera and physical camera.
bool V4l2Capture::onOpen(const char* deviceName) {
    // If we want polling interface for getting frames, we would use O_NONBLOCK
    // int mDeviceFd = open(deviceName, O_RDWR | O_NONBLOCK, 0);
    int pyhic_cam_fd;
    if (mIslogicCamera) {
        for (const auto& phsical_cam : mPhysicalCamera) {
            ALOGI("onOpen physical camera %s", phsical_cam.c_str());
            pyhic_cam_fd = onOpenSingleCamera(phsical_cam.c_str());

            if (pyhic_cam_fd < 0) {
                ALOGE("open phsical_cam %s failed", phsical_cam.c_str());
                return false;
            }
            mDeviceFd[phsical_cam] = pyhic_cam_fd;
        }
    } else {
        pyhic_cam_fd = onOpenSingleCamera(deviceName);
        if (pyhic_cam_fd < 0) {
            ALOGE("open phsical_cam %s failed", deviceName);
            return false;
        }
        mDeviceFd[deviceName] = pyhic_cam_fd;
    }

    // init v4l2 buffer index to -1, which mean it do not map one grolloc buffer
    std::unordered_map<int, int> init_index;
    for (int i = 0; i < V4L2_BUFFER_NUM; i++) {
        init_index[i] = -1;
    }

    for (const auto& physical_cam : mPhysicalCamera) {
        mBufferMap[physical_cam] = init_index;
    }

    return true;
}

int V4l2Capture::onOpenSingleCamera(const char* deviceName) {
    int fd = -1;

    if (deviceName == nullptr) {
        ALOGE("device name is null");
        return -EINVAL;
    }
    fd = ::open(deviceName, O_RDWR, 0);
    if (fd < 0) {
        ALOGE("failed to open device %s (%d = %s)", deviceName, errno, strerror(errno));
        return fd;
    }

    v4l2_capability caps;
    {
        int result = ioctl(fd, VIDIOC_QUERYCAP, &caps);
        if (result < 0) {
            ALOGE("failed to get device caps for %s (%d = %s)", deviceName, errno, strerror(errno));
            return -EINVAL;
        }
    }

    // Report device properties
    ALOGV("Open Device: %s (fd=%d)", deviceName, fd);
    ALOGV("  Driver: %s", caps.driver);
    ALOGV("  Card: %s", caps.card);
    ALOGV("  Version: %u.%u.%u", (caps.version >> 16) & 0xFF, (caps.version >> 8) & 0xFF,
          (caps.version) & 0xFF);
    ALOGV("  All Caps: %08X", caps.capabilities);
    ALOGV("  Dev Caps: %08X", caps.device_caps);

    // Enumerate the available capture formats (if any)
    ALOGI("Supported capture formats:");
    v4l2_fmtdesc formatDescriptions;
    formatDescriptions.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    for (int i = 0; true; i++) {
        formatDescriptions.index = i;
        if (ioctl(fd, VIDIOC_ENUM_FMT, &formatDescriptions) == 0) {
            ALOGI("  %2d: %s 0x%08X 0x%X", i, formatDescriptions.description,
                  formatDescriptions.pixelformat, formatDescriptions.flags);
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
        return -EINVAL;
        ;
    }

    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    param.parm.capture.timeperframe.numerator = 1;
    param.parm.capture.timeperframe.denominator = 30;
    param.parm.capture.capturemode = getCaptureMode(fd, mWidth, mHeight);
    int ret = ioctl(fd, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_PARM Failed: %s", __func__, strerror(errno));
        return -EINVAL;
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
    ALOGI("Requesting format %c%c%c%c (0x%08X)", ((char*)&format.fmt.pix.pixelformat)[0],
          ((char*)&format.fmt.pix.pixelformat)[1], ((char*)&format.fmt.pix.pixelformat)[2],
          ((char*)&format.fmt.pix.pixelformat)[3], format.fmt.pix.pixelformat);
    if (ioctl(fd, VIDIOC_S_FMT, &format) < 0) {
        ALOGE("VIDIOC_S_FMT: %s", strerror(errno));
    }

    // Report the current output format
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    if (ioctl(fd, VIDIOC_G_FMT, &format) == 0) {
        mV4lFormat = format.fmt.pix_mp.pixelformat;
        mWidth = format.fmt.pix_mp.width;
        mHeight = format.fmt.pix_mp.height;
        ALOGI("Current output format:  fmt=0x%X, %dx%d", format.fmt.pix_mp.pixelformat,
              format.fmt.pix_mp.width, format.fmt.pix_mp.height);
    } else {
        ALOGE("VIDIOC_G_FMT: %s", strerror(errno));
        return -EINVAL;
    }

    // Tell the L4V2 driver to prepare our streaming buffers
    v4l2_requestbuffers bufrequest;
    bufrequest.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    bufrequest.memory = V4L2_MEMORY_DMABUF;
    bufrequest.count = V4L2_BUFFER_NUM;
    if (ioctl(fd, VIDIOC_REQBUFS, &bufrequest) < 0) {
        ALOGE("VIDIOC_REQBUFS: %s", strerror(errno));
    }

    std::unique_lock<std::mutex> lock(mLock);
    // Make sure we're initialized to the STOPPED state
    mRunMode = STOPPED;

    // Ready to go!
    return fd;
}

std::set<uint32_t> V4l2Capture::enumerateCameraControls() {
    // Retrieve available camera controls
    struct v4l2_queryctrl ctrl = {.id = V4L2_CTRL_FLAG_NEXT_CTRL};

    std::set<uint32_t> ctrlIDs;

    // enum the last camera control info, need refine this part later
    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            continue;
        while (0 == ioctl(mDeviceFd[physical_cam], VIDIOC_QUERYCTRL, &ctrl)) {
            if (!(ctrl.flags & V4L2_CTRL_FLAG_DISABLED)) {
                ctrlIDs.emplace(ctrl.id);
            }

            ctrl.id |= V4L2_CTRL_FLAG_NEXT_CTRL;
        }

        if (errno != EINVAL) {
            ALOGE("Failed to run VIDIOC_QUERYCTRL");
        }
    }
    return std::move(ctrlIDs);
}

int V4l2Capture::setParameter(v4l2_control& control) {
    int status = 0;

    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            continue;

        int status = ioctl(mDeviceFd[physical_cam], VIDIOC_S_CTRL, &control);
        if (status < 0) {
            ALOGE("Failed to program a parameter value id = %d", control.id);
        }
    }
    return status;
}

int V4l2Capture::getParameter(v4l2_control& control) {
    int status = 0;

    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            continue;

        // TODO: just get the last camera control info, currently getIntParameter do not support
        // multi-camera.
        int status = ioctl(mDeviceFd[physical_cam], VIDIOC_G_CTRL, &control);
        if (status < 0) {
            ALOGE("Failed to read a parameter value id = %d", control.id);
        }
    }

    return status;
}

bool V4l2Capture::isOpen() {
    std::unique_lock<std::mutex> lock(mLock);

    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            return false;
    }

    return true;
}

void V4l2Capture::onClose() {
    ALOGD("V4l2Capture close logic/physical camera");

    if (!isOpen()) {
        return;
    }

    {
        std::unique_lock<std::mutex> lock(mLock);
        // Stream should be stopped first!
        assert(mRunMode == STOPPED);
        for (const auto& physical_cam : mPhysicalCamera) {
            ::close(mDeviceFd[physical_cam]);
            mDeviceFd[physical_cam] = -1;
        }
    }

    ALOGD("closing video device file handled");
}

bool V4l2Capture::onStart() {
    // if user space do not allocate buffer when start,
    // allocate 3 buffer for every camera
    // evs_app allocate buffer through setMaxFramesInFlight in app,
    // while sv_app do not allocate by default. So set one default value here
    if (mFramesAllowed == 0)
        setMaxFramesInFlight(3);

    int fd = -1;
    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            continue;
        {
            std::unique_lock<std::mutex> lock(mLock);
            fd = mDeviceFd[physical_cam];
        }

        if (fd < 0) {
            // The device is not opened.
            ALOGE("%s device not opened", __func__);
            return false;
        }

        // Start the video stream
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        if (ioctl(fd, VIDIOC_STREAMON, &type) < 0) {
            ALOGE("VIDIOC_STREAMON: %s", strerror(errno));
            return false;
        }
    }

    ALOGD("Stream started.");
    return true;
}

void V4l2Capture::onStop() {
    int fd = -1;

    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            return;

        {
            std::unique_lock<std::mutex> lock(mLock);
            fd = mDeviceFd[physical_cam];
        }

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
}

Return<EvsResult> V4l2Capture::setMaxFramesInFlight(uint32_t bufferCount) {
    ALOGD("setMaxFramesInFlight");

    if (!isOpen()) {
        ALOGE("Ignoring, because the camera is closed");
        return EvsResult::OWNERSHIP_LOST;
    }

    // We cannot function without at least one video buffer to send data
    if (bufferCount < 1) {
        ALOGE("Ignoring with less than one buffer requested");
        return EvsResult::INVALID_ARG;
    }

    if (bufferCount > MAX_BUFFERS_IN_FLIGHT)
        return EvsResult::BUFFER_NOT_AVAILABLE;

    if (mFramesAllowed < bufferCount) {
        unsigned needed = bufferCount - mFramesAllowed;
        ALOGI("Allocating %d buffers for camera frames", needed);

        onIncreaseMemoryBuffer(needed);
    } else if (mFramesAllowed > bufferCount) {
        mDecreasenum = mFramesAllowed - bufferCount;
        ALOGI("Decrease %d buffers for camera frames", mDecreasenum);
        // if neet reduce the buffer, it drop the return buffer in onFrameReturn
        // this buffer do not call QBUF again.
        // it will return until mDecreasenum = 0
        {
            std::unique_lock<std::mutex> lock(mLock);
            mFramesSignal.wait(lock);
            if (mDecreasenum > 0)
                ALOGE("mDecreasenum should been 0 ! ");
        }
    }

    // Update our internal state
    return EvsResult::OK;
}

void V4l2Capture::onIncreaseMemoryBuffer(unsigned number) {
    unsigned added = 0;
    fsl::Memory* buffer = nullptr;
    fsl::MemoryManager* allocator = fsl::MemoryManager::getInstance();
    fsl::MemoryDesc desc;
    desc.mWidth = mWidth;
    desc.mHeight = mHeight;
    desc.mFormat = mFormat;
    desc.mFslFormat = mFormat;
    desc.mProduceUsage |=
            fsl::USAGE_HW_TEXTURE | fsl::USAGE_HW_RENDER | fsl::USAGE_HW_VIDEO_ENCODER;
    desc.mFlag = 0;
    int ret = desc.checkFormat();
    if (ret != 0) {
        ALOGE("%s checkFormat failed", __func__);
        return;
    }

    while (added < number) {
        // allocate mFramesAllowed buffer for every physical camera
        for (const auto& physical_cam : mPhysicalCamera) {
            if (mDeviceFd[physical_cam] < 0)
                return;

            buffer = nullptr;
            allocator->allocMemory(desc, &buffer);

            std::vector<fsl::Memory*> fsl_mem;
            fsl_mem = mCamBuffers[mDeviceFd[physical_cam]];

            bool stored = false;
            for (auto mem_singal : fsl_mem) {
                {
                    std::unique_lock<std::mutex> lock(mLock);
                    if (mem_singal == nullptr) {
                        mem_singal = buffer;
                        stored = true;
                        break;
                    }
                }
            }

            if (!stored) {
                fsl_mem.push_back(buffer);
            }

            struct v4l2_buffer buf;
            struct v4l2_plane planes;
            int v4l2_index = 0;
            memset(&buf, 0, sizeof(buf));
            memset(&planes, 0, sizeof(struct v4l2_plane));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
            buf.memory = V4L2_MEMORY_DMABUF;
            buf.m.planes = &planes;

            // find the first unmap buffer in v4l2 buffer
            for (v4l2_index = 0; v4l2_index < V4L2_BUFFER_NUM; v4l2_index++) {
                if (mBufferMap[physical_cam].at(v4l2_index) == -1)
                    break;
            }

            // map the v4l2 buffer and gralloc buffer
            if (mFramesAllowed == 0)
                mBufferMap[physical_cam].at(v4l2_index) = 0;
            else {
                int i, j;
                // find the first unmap gralloc buffer
                // map v4l2 buffer with gralloc buffer through
                // mBufferMap[physical_cam].at(v4l2_index) = i;
                for (i = 0; i < (mFramesAllowed + 1); i++) {
                    for (j = 0; j < V4L2_BUFFER_NUM; j++) {
                        if (mBufferMap[physical_cam].at(j) == i)
                            break;
                    }
                    if (j == V4L2_BUFFER_NUM) {
                        mBufferMap[physical_cam].at(v4l2_index) = i;
                        break;
                    }
                }
            }
            buf.index = v4l2_index;

            buf.length = 1;
            buf.m.planes->length = buffer->size;
            buf.m.planes->m.fd = buffer->fd;

            // Requeue the buffer to capture the next available frame
            if (ioctl(mDeviceFd[physical_cam], VIDIOC_QBUF, &buf) < 0) {
                ALOGE("%s VIDIOC_QBUF: %s", __func__, strerror(errno));
            }

#ifdef DEBUG_V4L2_CAMERA
            void* vaddr = NULL;
            // handle->base which is instore virtual addr is only set after lock
            // need lock the addr here, so that it can been used in onFrameCollect
            allocator->lock(buffer,
                            buffer->usage | fsl::USAGE_SW_READ_OFTEN | fsl::USAGE_SW_WRITE_OFTEN, 0,
                            0, buffer->width, buffer->height, &vaddr);
#endif
            mCamBuffers[mDeviceFd[physical_cam]] = fsl_mem;
        }
        mFramesAllowed++;
        added++;
    }
}

// release the gralloc buffer which map the v4l2 buffer whose index is parameter "index"
// it only been called when return buffer and the mDecreasenum is not 0
void V4l2Capture::onDecreaseMemoryBuffer(unsigned index) {
    fsl::Memory* buffer = nullptr;
    fsl::MemoryManager* allocator = fsl::MemoryManager::getInstance();

    // destroy CAMERA_BUFFER_NUM buffer for every physical camera
    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            continue;

        std::vector<fsl::Memory*> fsl_mem;

        {
            std::unique_lock<std::mutex> lock(mLock);
            buffer = mCamBuffers[mDeviceFd[physical_cam]].at(mBufferMap[physical_cam].at(index));
            allocator->releaseMemory(buffer);
            mCamBuffers[mDeviceFd[physical_cam]].at(mBufferMap[physical_cam].at(index)) = nullptr;
            mBufferMap[physical_cam].at(index) = -1;
            mFramesAllowed--;
        }
    }
}

void V4l2Capture::onMemoryDestroy() {
    fsl::Memory* buffer = nullptr;
    fsl::MemoryManager* allocator = fsl::MemoryManager::getInstance();
    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            continue;

        std::vector<fsl::Memory*> fsl_mem;
        fsl_mem = mCamBuffers[mDeviceFd[physical_cam]];

        for (auto mem_singal : fsl_mem) {
            {
                std::unique_lock<std::mutex> lock(mLock);
                if (mem_singal == nullptr) {
                    continue;
                }

                buffer = mem_singal;
                mem_singal = nullptr;
            }
            allocator->releaseMemory(buffer);
        }
        fsl_mem.clear();
    }
}

bool V4l2Capture::onFrameReturn(int index, std::string deviceid) {
    // We're giving the frame back to the system, so clear the "ready" flag
    std::string devicename = deviceid;
    if (index < 0 || index >= V4L2_BUFFER_NUM) {
        ALOGE("%s invalid index:%d", __func__, index);
        return false;
    }

    int fd = -1;

    if (devicename == "" && mPhysicalCamera.size() == 1) {
        devicename = *mPhysicalCamera.begin();
    }

    // mDecreasenum is set in setMaxFramesInFlight which means app request hal decrease buffer.
    // do not call QBUF for this frame.
    // when mDecreasenum = 0, return setMaxFramesInFlight
    if (mDecreasenum > 0) {
        onDecreaseMemoryBuffer(index);
        mDecreasenum--;

        if (mDecreasenum == 0)
            mFramesSignal.notify_all();

        ALOGI("onFrameReturn drop one frame");
        return true;
    }

    fsl::Memory* buffer = nullptr;
    {
        std::unique_lock<std::mutex> lock(mLock);
        fd = mDeviceFd[devicename];
        // devicename means the pyhsical camera name
        // mDeviceFd[devicename] means the pyhsical camera fd
        // mCamBuffers[mDeviceFd[devicename]]: every pyhsical camera fd have
        // three buffer, onFrameReturn return index buffer
        buffer = mCamBuffers[mDeviceFd[devicename]].at(mBufferMap[devicename].at(index));
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
void V4l2Capture::onFrameCollect(std::vector<struct forwardframe>& frames) {
    int fd = -1;
    fsl::Memory* buffer = nullptr;
    struct forwardframe frame;
#ifdef DEBUG_V4L2_CAMERA
    int index = 0;
#endif
    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            return;

        {
            std::unique_lock<std::mutex> lock(mLock);
            fd = mDeviceFd[physical_cam];
        }

        if (fd < 0) {
            ALOGE("%s invalid fd", __func__);
            return;
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
            return;
        }

        {
            std::unique_lock<std::mutex> lock(mLock);
            // buf.index means the index of v4l2 buffer,
            // mBufferMap[mDeviceFd[physical_cam]][buf.index] means the index of grolloc buffer
            buffer =
                    mCamBuffers[mDeviceFd[physical_cam]].at(mBufferMap[physical_cam].at(buf.index));
        }
#ifdef DEBUG_V4L2_CAMERA
        char filename[128];
        memset(filename, 0, 128);
        if (nub % 20 == 0) {
            sprintf(filename, "/data/%s-frame_out-%d-%d.rgb", "EVS", nub, index);
            int fd = 0;
            int len = 0;
            fd = open(filename, O_CREAT | O_RDWR, 0666);

            if (fd < 0) {
                ALOGE("failed to open");
            } else {
                len = write(fd, (void*)buffer->base, buffer->stride * buffer->height * 3);
                close(fd);
            }
        }
        index++;
#endif
        frame.buf = buffer;
        frame.index = buf.index;
        frame.deviceid = physical_cam;
        frames.push_back(frame);
    }
#ifdef DEBUG_V4L2_CAMERA
    nub++;
#endif
}
