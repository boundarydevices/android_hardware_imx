/*
 * Copyright 2022 The Android Open Source Project
 * Copyright 2023 NXP
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

#include "FbdevDisplay.h"

#include <fcntl.h>
#include <gralloc_handle.h>
#include <linux/fb.h>
#include <linux/mxcfb.h>
#include <linux/videodev2.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include "Common.h"
#include "Drm.h" // some pixel format support

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

template <typename T>
uint64_t addressAsUint(T* pointer) {
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer));
}

} // namespace

std::unique_ptr<FbdevDisplay> FbdevDisplay::create(uint32_t id,
                                                   ::android::base::borrowed_fd devFd) {
    std::unique_ptr<FbdevDisplay> display(new FbdevDisplay(id, devFd.get()));

    return std::move(display);
}

std::tuple<HWC3::Error, ::android::base::unique_fd> FbdevDisplay::present(
        ::android::base::borrowed_fd devFd, ::android::base::borrowed_fd inSyncFd, uint64_t addr) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    struct mxcfb_datainfo mxcbuf;
    int presentFenceFd = -1;

    memset(&mxcbuf, 0, sizeof(mxcbuf));
    mxcbuf.screeninfo.xoffset = 0;
    mxcbuf.screeninfo.yoffset = 0;

    mxcbuf.smem_start = (unsigned long)addr;
    mxcbuf.fence_fd = inSyncFd.get();
    mxcbuf.fence_ptr = (intptr_t)&presentFenceFd;

    if (ioctl(devFd.get(), MXCFB_PRESENT_SCREEN, &mxcbuf) < 0) {
        ALOGE("%s: MXCFB_PRESENT_SCREEN failed: %s", __FUNCTION__, strerror(errno));
        struct fb_var_screeninfo info;
        if (ioctl(devFd.get(), FBIOGET_VSCREENINFO, &info) < 0) {
            ALOGE("%s: FBIOGET_VSCREENINFO failed: %s", __FUNCTION__, strerror(errno));
            return std::make_tuple(HWC3::Error::NoResources, ::android::base::unique_fd());
        }

        info.xoffset = info.yoffset = 0;
        info.reserved[0] = static_cast<uint32_t>(addr);
        info.reserved[1] = static_cast<uint32_t>(addr >> 32);
        info.activate = FB_ACTIVATE_VBL;
        if (ioctl(devFd.get(), FBIOPAN_DISPLAY, &info) < 0) {
            ALOGE("%s: FBIOPAN_DISPLAY failed errno:%d", __FUNCTION__, errno);
            return std::make_tuple(HWC3::Error::NoResources, ::android::base::unique_fd());
        }
    }

    DEBUG_LOG("%s: present FBDEV display %d with buffer addr=0x%lx, present fence=%d", __FUNCTION__,
              mId, addr, presentFenceFd);
    return std::make_tuple(HWC3::Error::None, ::android::base::unique_fd(presentFenceFd));
}

bool FbdevDisplay::onConnect(::android::base::borrowed_fd devFd) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    updateDisplayConfigs();

    return true;
}

bool FbdevDisplay::onDisconnect(::android::base::borrowed_fd devFd) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    if (!isPrimary()) {
        // primary display cannot be disconnected, fake display config is generated according
        // to current active config
        mActiveConfigId = -1;
        mConfigs->clear();
    }

    return true;
}

DrmHotplugChange FbdevDisplay::checkAndHandleHotplug(::android::base::borrowed_fd devFd) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    return DrmHotplugChange::kNoChange;
}

bool FbdevDisplay::setPowerMode(::android::base::borrowed_fd devFd, DrmPower power) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    int mode;
    switch (power) {
        case DrmPower::kPowerOn:
            mode = FB_BLANK_UNBLANK;
            break;
        case DrmPower::kPowerOff:
            mode = FB_BLANK_POWERDOWN;
            break;
        default:
            mode = FB_BLANK_UNBLANK;
            break;
    }

    int err = ioctl(devFd.get(), FBIOBLANK, mode);
    if (err < 0) {
        ALOGE("%s: FBDEV blank/unblank failed: %s", __FUNCTION__, strerror(errno));
        return false;
    }

    return true;
}

bool FbdevDisplay::updateDisplayConfigs() {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    mStartConfigId = mStartConfigId + mConfigs->size();
    mConfigs->clear();

    struct fb_var_screeninfo info;
    if (ioctl(mFbdevFd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("%s: FBIOGET_VSCREENINFO failed: %s", __FUNCTION__, strerror(errno));
        return -errno;
    }

    struct fb_fix_screeninfo finfo;
    if (ioctl(mFbdevFd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        ALOGE("%s: FBIOGET_FSCREENINFO failed: %s", __FUNCTION__, strerror(errno));
        return -errno;
    }

    int refreshRate = 1000000000000LLU /
            (uint64_t(info.upper_margin + info.lower_margin + info.yres + info.vsync_len) *
             (info.left_margin + info.right_margin + info.xres + info.hsync_len) * info.pixclock);

    if (refreshRate == 0) {
        // bad info from the driver
        refreshRate = 60; // 60 Hz
    }

    if (int(info.width) <= 0 || int(info.height) <= 0) {
        // the driver doesn't return that information default to 160 dpi
        info.width = ((info.xres * 25.4f) / 160.0f + 0.5f);
        info.height = ((info.yres * 25.4f) / 160.0f + 0.5f);
    }

    mConfigs->emplace(mStartConfigId,
                      HalDisplayConfig{
                              .width = info.xres,
                              .height = info.yres,
                              .dpiX = static_cast<uint32_t>(1000 * (info.xres * 25.4f) /
                                                            info.width),
                              .dpiY = static_cast<uint32_t>(1000 * (info.yres * 25.4f) /
                                                            info.height),
                              .refreshRateHz = static_cast<uint32_t>(refreshRate),
                              .blobId = 0,
                              .modeType = 0,
                              .modeWidth = 0,
                              .modeHeight = 0,
                      });

    mActiveConfigId = mStartConfigId;
    mActiveConfig = (*mConfigs)[mActiveConfigId];

    if (info.grayscale == 0) {
        mBufferFormat = (info.bits_per_pixel == 32)
                ? ((info.red.offset == 0) ? FORMAT_RGBA8888 : FORMAT_BGRA8888)
                : FORMAT_RGB565;
    } else {
        mBufferFormat = (info.grayscale == V4L2_PIX_FMT_ARGB32) ? FORMAT_RGBA8888 : FORMAT_BGRA8888;
    }
    mBytesPerPixel = info.bits_per_pixel >> 3;
    mStrideInBytes = finfo.line_length;

    ALOGW("FBDEV Display index= %d \n"
          "xres         = %d px\n"
          "yres         = %d px\n"
          "fps          = %d Hz\n"
          "format       = 0x%x\n"
          "bpp          = %d\n"
          "r            = %2u:%u\n"
          "g            = %2u:%u\n"
          "b            = %2u:%u\n",
          mId, mActiveConfig.width, mActiveConfig.height, mActiveConfig.refreshRateHz,
          mBufferFormat, info.bits_per_pixel, info.red.offset, info.red.length, info.green.offset,
          info.green.length, info.blue.offset, info.blue.length);

    setDefaultDisplayMode();

    return true;
}

void FbdevDisplay::placeholderDisplayConfigs() {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    mStartConfigId = mStartConfigId + mConfigs->size();
    mConfigs->clear();

    HalDisplayConfig newConfig;
    memset(&newConfig, 0, sizeof(newConfig));
    if (mActiveConfigId >= 0) {
        memcpy(&newConfig, &mActiveConfig, sizeof(HalDisplayConfig));
        newConfig.blobId = 0;
        newConfig.modeWidth = 0;
        newConfig.modeHeight = 0;
    } else {
        newConfig.width = 1080;
        newConfig.height = 1920;
        newConfig.dpiX = 320;
        newConfig.dpiY = 320;
        newConfig.refreshRateHz = 60;
    }

    mConfigs->emplace(mStartConfigId, newConfig);
    mActiveConfigId = mStartConfigId;

    mActiveConfig = (*mConfigs)[mActiveConfigId];
}

int FbdevDisplay::getFramebufferInfo(uint32_t* width, uint32_t* height, uint32_t* format) {
    DEBUG_LOG("%s: display:%" PRIu32, __FUNCTION__, mId);

    *width = mActiveConfig.width;
    *height = mActiveConfig.height;

    *format = mBufferFormat;

    return 0;
}

int FbdevDisplay::setDefaultDisplayMode() {
    struct fb_var_screeninfo info;
    if (ioctl(mFbdevFd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("%s: FBIOGET_VSCREENINFO failed: %s", __FUNCTION__, strerror(errno));
        return -errno;
    }

    info.bits_per_pixel = 16;
    info.grayscale = 0;
    info.yoffset = 0;
    info.rotate = FB_ROTATE_UR;
    info.activate = FB_ACTIVATE_FORCE;

    if (ioctl(mFbdevFd, FBIOPUT_VSCREENINFO, &info) == -1) {
        ALOGE("%s: set default fbdev parameters failed: %s", __FUNCTION__, strerror(errno));
        return -errno;
    }

    return 0;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
