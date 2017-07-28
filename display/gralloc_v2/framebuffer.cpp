/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2010-2016 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP.
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

#include <sys/mman.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdlib.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>
#include <utils/String8.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include <DisplayManager.h>
#include <FbDisplay.h>

using namespace fsl;

struct fb_context_t {
    framebuffer_device_t device;
    Display* display;
};

int fb_setSwapInterval(struct framebuffer_device_t* dev, int interval)
{
    fb_context_t* ctx = (fb_context_t*)dev;
    if (interval < dev->minSwapInterval || interval > dev->maxSwapInterval)
        return -EINVAL;
    // FIXME: implement fb_setSwapInterval
    return 0;
}

int fb_setUpdateRect(struct framebuffer_device_t* dev,
        int l, int t, int w, int h)
{
    if (!dev || ((w|h) <= 0) || ((l|t)<0))
        return -EINVAL;

    fb_context_t* ctx = (fb_context_t*)dev;

    return 0;
}

int fb_post(struct framebuffer_device_t* dev, buffer_handle_t buffer)
{
    if (!buffer || !dev) {
        ALOGE("%s invalid parameters", __FUNCTION__);
        return -EINVAL;
    }

    fb_context_t* ctx = (fb_context_t*)dev;

    return ctx->display->updateScreen();
}

int fb_compositionComplete(struct framebuffer_device_t* dev)
{
    fb_context_t* ctx = (fb_context_t*)dev;

    return 0;
}

int fb_close(struct hw_device_t *dev)
{
    fb_context_t* ctx = (fb_context_t*)dev;
    if(ctx == NULL) {
        return 0;
    }

    ctx->display = NULL;
    free(ctx);

    return 0;
}

int fb_device_open(hw_module_t const* module, const char* name,
        hw_device_t** device)
{
    if (strcmp(name, GRALLOC_HARDWARE_FB0)) {
        return -EINVAL;
    }

    /* initialize our state here */
    fb_context_t *dev = (fb_context_t*)malloc(sizeof(*dev));
    memset(dev, 0, sizeof(*dev));

    /* initialize the procs */
    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = 0;
    dev->device.common.module = const_cast<hw_module_t*>(module);
    dev->device.common.close = fb_close;
    dev->device.setSwapInterval = fb_setSwapInterval;
    dev->device.post = fb_post;
    dev->device.compositionComplete = fb_compositionComplete;

    DisplayManager* pDisplayManager = DisplayManager::getInstance();
    if (pDisplayManager == NULL) {
        ALOGE("%s get display manager failed.", __FUNCTION__);
        free(dev);
        return -EINVAL;
    }

    ALOGI("fb_device_open open primary display");
    Display* display = pDisplayManager->getPhysicalDisplay(DISPLAY_PRIMARY);
    if (display == NULL) {
        ALOGE("%s can't get valid display", __FUNCTION__);
        free(dev);
        return -EINVAL;
    }

    const DisplayConfig& config = display->getActiveConfig();
    const_cast<uint32_t&>(dev->device.flags) = 0xfb0;
    const_cast<uint32_t&>(dev->device.width) = config.mXres;
    const_cast<uint32_t&>(dev->device.height) = config.mYres;
    const_cast<int&>(dev->device.stride) = config.mStride / config.mBytespixel;
    const_cast<int&>(dev->device.format) = config.mFormat;
    const_cast<float&>(dev->device.xdpi) = config.mXdpi/1000;
    const_cast<float&>(dev->device.ydpi) = config.mYdpi/1000;
    const_cast<float&>(dev->device.fps) = config.mFps;
    const_cast<int&>(dev->device.minSwapInterval) = 1;
    const_cast<int&>(dev->device.maxSwapInterval) = 1;
    const_cast<int &>(dev->device.numFramebuffers) = MAX_FRAMEBUFFERS;

    dev->device.reserved[0] = MAX_FRAMEBUFFERS;
    dev->display = display;
    *device = &dev->device.common;

    return 0;
}
