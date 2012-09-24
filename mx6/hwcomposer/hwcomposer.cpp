/*
 * Copyright (C) 2009-2012 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include <hardware_legacy/uevent.h>
#include <utils/StrongPointer.h>

#include <linux/mxcfb.h>
#include <linux/ioctl.h>
#include <EGL/egl.h>
#include "gralloc_priv.h"
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
        version_major: 2,
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

static void dump_layer(hwc_layer_t const* l) {
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

static int hwc_prepare(hwc_composer_device_t *dev, hwc_layer_list_t* list) {

    struct hwc_context_t *ctx = (struct hwc_context_t *)dev;
    if(ctx && ctx->m_viv_hwc && ctx->m_viv_hwc->prepare) {
        ctx->m_viv_hwc->prepare(ctx->m_viv_hwc, list);
    }
    return 0;
}

static int hwc_set(hwc_composer_device_t *dev,
        hwc_display_t dpy,
        hwc_surface_t sur,
        hwc_layer_list_t* list)
{
    struct hwc_context_t *ctx = (struct hwc_context_t *)dev;

    EGLBoolean sucess;
    if(ctx && ctx->m_viv_hwc && ctx->m_viv_hwc->set) {
        sucess = !ctx->m_viv_hwc->set(ctx->m_viv_hwc, dpy, sur, list);
    }
    else {
        sucess = eglSwapBuffers((EGLDisplay)dpy, (EGLSurface)sur);
    }
    if (!sucess) {
        return HWC_EGL_ERROR;
    }

    return 0;
}

static int hwc_device_close(struct hw_device_t *dev)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (ctx) {
        if(ctx->m_viv_hwc)
            hwc_close(ctx->m_viv_hwc);

        if(ctx->m_vsync_thread != NULL) {
            ctx->m_vsync_thread->requestExitAndWait();
        }
        free(ctx);
    }
    return 0;
}

#ifdef ENABLE_VSYNC
static void hwc_registerProcs(struct hwc_composer_device* dev,
            hwc_procs_t const* procs) {
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if(ctx) {
        ctx->m_callback = (hwc_procs_t*)procs;
    }
}

static int hwc_eventControl(struct hwc_composer_device* dev, int event, int enabled)
{
    int ret = -EINVAL;
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if(ctx && event == HWC_EVENT_VSYNC) {
        int val = !!enabled;
        ctx->m_vsync_enable = val;
        ret = ioctl(ctx->m_mainfb_fd, MXCFB_ENABLE_VSYNC_EVENT, &val);
        if(ret < 0) {
            ALOGE("ioctl FB_ENABLE_VSYNC_IOCTL failed: %d, %s", ret, strerror(errno));
        }
        else {
            ret = 0;
        }
    }

    return ret;
}

static const struct hwc_methods hwc_methods = {
    eventControl: hwc_eventControl
};

static int hwc_query(struct hwc_composer_device* dev,
        int what, int* value)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;

    switch (what) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
        // we don't support the background layer yet
        value[0] = 0;
        break;
    case HWC_VSYNC_PERIOD:
        // vsync period in nanosecond
        value[0] = 1000000000.0 / ctx->m_mainfb_fps;
        break;
    default:
        // unsupported query
        return -EINVAL;
    }
    return 0;
}

static int hwc_get_framebuffer_info(struct hwc_context_t* ctx)
{
    struct fb_var_screeninfo info;
    if (ioctl(ctx->m_mainfb_fd, FBIOGET_VSCREENINFO, &info) == -1) {
        ALOGE("<%s,%d> FBIOGET_VSCREENINFO failed", __FUNCTION__, __LINE__);
        return -errno;
    }

    int refreshRate = 1000000000000000LLU / (uint64_t(info.upper_margin +
                                                      info.lower_margin +
                                                      info.yres +
                                                      info.vsync_len) *
                                             (info.left_margin  +
                                              info.right_margin +
                                              info.xres +
                                              info.hsync_len) * info.pixclock);
    if (refreshRate == 0)
        refreshRate = 60 * 1000;  // 60 Hz

    ctx->m_mainfb_fps = refreshRate / 1000.0f;
    return 0;
}

#endif
/*****************************************************************************/

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = -EINVAL;
    if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
        struct hwc_context_t *dev;
        dev = (hwc_context_t*)malloc(sizeof(*dev));

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        //dev->device.common.version = HWC_DEVICE_API_VERSION_0_3;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = hwc_device_close;

        dev->device.prepare = hwc_prepare;
        dev->device.set = hwc_set;
#ifdef ENABLE_VSYNC
        dev->device.common.version = HWC_DEVICE_API_VERSION_0_3;
        dev->device.registerProcs = hwc_registerProcs;
        dev->device.methods = &hwc_methods;
        dev->device.query = hwc_query;

        //*device = &dev->device.common;

        /* our private state goes below here */
        dev->m_mainfb_fd = open(HWC_MAIN_FB, O_RDWR);
        dev->m_vsync_thread = new VSyncThread(dev);
        hwc_get_framebuffer_info(dev);
#else
        dev->device.common.version = 0;
#endif
        const hw_module_t *hwc_module;
        if(hw_get_module(HWC_VIV_HARDWARE_MODULE_ID,
                        (const hw_module_t**)&hwc_module) < 0) {
            ALOGE("Error! hw_get_module viv_hwc failed");
            goto nor_exit;
        }
        if(hwc_open(hwc_module, &(dev->m_viv_hwc)) != 0) {
            ALOGE("Error! viv_hwc open failed");
            goto nor_exit;
        }
nor_exit:

        *device = &dev->device.common;
	ALOGI("%s,%d", __FUNCTION__, __LINE__);
        return 0;
err_exit:
	if(dev){
	    free(dev);
	}
        /****************************************/
    }
    return status;
}
