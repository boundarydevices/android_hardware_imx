/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc. All Rights Reserved.
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


#include <hardware/hardware.h>

#include <fcntl.h>
#include <errno.h>

#include <cutils/log.h>
#include <cutils/atomic.h>
#include <cutils/properties.h>
#include <utils/threads.h>
#include <hardware/hwcomposer.h>
#include <utils/StrongPointer.h>

#include <linux/mxcfb.h>
#include <linux/ioctl.h>
#include <EGL/egl.h>
#include <GLES/gl.h>

#include "hwc_context.h"
#include "hwc_vsync.h"

/*****************************************************************************/
static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        version_major: 1,
        version_minor: 0,
        id: HWC_HARDWARE_MODULE_ID,
        name: "Freescale i.MX hwcomposer module",
        author: "Freescale Semiconductor, Inc.",
        methods: &hwc_module_methods,
        dso: NULL,
        reserved: {0}
    }
};

/*****************************************************************************/
static void dump_layer(hwc_layer_1_t const* l)
{
    ALOGD("\ttype=%d, flags=%08x, handle=%p, tr=%02x, blend=%04x, {%d,%d,%d,%d}, {%d,%d,%d,%d}",
            l->compositionType, l->flags, l->handle, l->transform, l->blending,
            l->sourceCrop.left,
            l->sourceCrop.top,
            l->sourceCrop.right,
            l->sourceCrop.bottom,
            l->displayFrame.left,
            l->displayFrame.top,
            l->displayFrame.right,
            l->displayFrame.bottom);
}

/***********************************************************************/
static int hwc_prepare(hwc_composer_device_1_t *dev,
        size_t numDisplays, hwc_display_contents_1_t** displays)
{
    struct hwc_context_t *ctx = (struct hwc_context_t *)dev;
    if (ctx == NULL || numDisplays == 0 || displays == NULL) {
        return 0;
    }

    // don't need to do anything here.
    return 0;
}

static int hwc_set(hwc_composer_device_1_t *dev,
        size_t numDisplays, hwc_display_contents_1_t** displays)
{
    struct hwc_context_t *ctx = (struct hwc_context_t *)dev;
    EGLBoolean success = EGL_TRUE;

    if (ctx == NULL || numDisplays == 0 || displays == NULL) {
        return 0;
    }

    // when blanked, return.
    if (ctx->mBlank != 0) {
        return 0;
    }

    // only need to do swap buffers.
    if (displays[0]->dpy != NULL && displays[0]->sur != NULL) {
        success = eglSwapBuffers((EGLDisplay)displays[0]->dpy,
            (EGLSurface)displays[0]->sur);
    }

    if (!success) {
        return HWC_EGL_ERROR;
    }

    return 0;
}

static void hwc_registerProcs(struct hwc_composer_device_1 *dev,
                              hwc_procs_t const* procs)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if(ctx == NULL) {
        return;
    }

    // set callback procs.
    ctx->mCallback = (hwc_procs_t*)procs;
}

static int hwc_eventControl(struct hwc_composer_device_1* dev,
                            int/* disp*/, int event, int enabled)
{
    int ret = 0;
    hwc_context_t *ctx = (hwc_context_t *) dev;
    if (ctx == NULL) {
        return ret;
    }

    switch (event) {
        case HWC_EVENT_VSYNC:
            // enable/disable vsync thread.
            ctx->mVsyncThread->setEnabled(enabled);
            break;
        default:
            ret = -EINVAL;
            break;
    }

    return ret;
}

static int hwc_blank(struct hwc_composer_device_1* dev, int disp, int blank)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;

    if (!ctx || disp < 0 || disp >= HWC_NUM_DISPLAY_TYPES) {
        return 0;
    }

    ctx->mBlank = blank;
    // HDMI and EPDC can't blank.
    if (ctx->mDisplayType == HWC_DISPLAY_HDMI ||
        ctx->mDisplayType == HWC_DISPLAY_EPDC) {
        return 0;
    }

    // HDMI display and audio share the same HDMI module.
    // So blank HDMI display will impact audio playing.
    // IMX7D is hard to differentiate between HDMI and LCD.
    // So comment blank code here now.
    if (false) {
        // blank or unblank screen.
        int fb_blank = blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK;
        int err = ioctl(ctx->mFbFile, FBIOBLANK, fb_blank);
        if (err < 0) {
            ALOGE("blank ioctl failed");
            return -errno;
        }
    }

    return 0;
}

static int hwc_query(struct hwc_composer_device_1 *dev,
                     int what, int* value)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (ctx == NULL || value == NULL) {
        return 0;
    }

    switch (what) {
        case HWC_BACKGROUND_LAYER_SUPPORTED:
            // we don't support the background layer yet
            value[0] = 0;
            break;
        case HWC_VSYNC_PERIOD:
            // vsync period in nanosecond
            value[0] = ctx->mVsyncPeriod;
            break;
        default:
            // unsupported query
            return -EINVAL;
    }

    return 0;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (ctx == NULL) {
        return 0;
    }

    // exit and destroy vsync thread.
    if(ctx->mVsyncThread != NULL) {
        ctx->mVsyncThread->requestExitAndWait();
        ctx->mVsyncThread = 0;
    }

    // close file handle.
    if (ctx->mFbFile > 0) {
        close(ctx->mFbFile);
    }
    free(ctx);

    return 0;
}

/*****************************************************************************/
static int hwc_device_open(const struct hw_module_t* module, const char* name,
                           struct hw_device_t** device)
{
    if (strcmp(name, HWC_HARDWARE_COMPOSER) != 0) {
        return -EINVAL;
    }

    struct hwc_context_t *dev = NULL;
    dev = (hwc_context_t*)malloc(sizeof(*dev));
    if (dev == NULL) {
        ALOGE("%s malloc failed", __FUNCTION__);
        return -EINVAL;
    }

    /* initialize our state here */
    memset(dev, 0, sizeof(*dev));

    /* initialize the procs */
    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.module = const_cast<hw_module_t*>(module);
    dev->device.common.close = hwc_device_close;

    dev->device.prepare = hwc_prepare;
    dev->device.set = hwc_set;
    dev->device.blank = hwc_blank;
    dev->device.common.version = HWC_DEVICE_API_VERSION_1_0;
    dev->device.registerProcs = hwc_registerProcs;
    dev->device.eventControl = hwc_eventControl;
    dev->device.query = hwc_query;

    /* our private state goes below here */
    dev->mFbFile = open(HWC_PRIMARY_DISPLAY, O_RDWR);
    dev->mVsyncThread = new VSyncThread(dev);
    if (dev->mFbFile < 0) {
        dev->mVsyncThread->setFakeVSync(true);
    }

    hwc_get_framebuffer_info(dev);
    hwc_get_display_type(dev);

    *device = &dev->device.common;
    ALOGI("<%s,%d>", __FUNCTION__, __LINE__);

    return 0;
}
