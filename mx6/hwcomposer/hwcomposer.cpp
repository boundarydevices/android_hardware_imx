/*
 * Copyright (C) 2009-2013 Freescale Semiconductor, Inc. All Rights Reserved.
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

#define HWC_REMOVE_DEPRECATED_VERSIONS 1
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
#include "hwc_uevent.h"
#include "hwc_display.h"

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

static void dump_layer(hwc_layer_1_t const* l) {
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
static int hwc_device_close(struct hw_device_t *dev)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (ctx) {
        if (ctx->m_vsync_thread != NULL) {
            ctx->m_vsync_thread->requestExitAndWait();
            ctx->m_uevent_thread.clear();
        }

        if (ctx->m_uevent_thread.get() != NULL) {
            ctx->m_uevent_thread->requestExitAndWait();
            ctx->m_uevent_thread.clear();
        }

        for (int i=0; i<HWC_NUM_DISPLAY_TYPES; i++) {
            if(ctx->mDispInfo[i].connected)
                close(ctx->mDispInfo[i].fd);
        }

        if(ctx->m_viv_hwc) {
            hwc_close_1(ctx->m_viv_hwc);
        }

        free(ctx);
    }
    return 0;
}

static int hwc_prepare(hwc_composer_device_1_t *dev,
        size_t numDisplays, hwc_display_contents_1_t** displays)
{
    if (!numDisplays || !displays || !dev) {
        ALOGI("%s invalid parameter", __FUNCTION__);
        return 0;
    }

    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    hwc_display_contents_1_t *primary_contents = displays[HWC_DISPLAY_PRIMARY];
    hwc_display_contents_1_t *external_contents = displays[HWC_DISPLAY_EXTERNAL];
    if (primary_contents) {
    }

    if (external_contents) {
    }

    if(ctx->m_viv_hwc) {
        char property[PROPERTY_VALUE_MAX];
        property_get("service.bootanim.exit", property, "0");
        if(!atoi(property)) numDisplays = numDisplays >= 1 ? 1 : 0;
        return ctx->m_viv_hwc->prepare(ctx->m_viv_hwc, numDisplays, displays);
    }

    return 0;
}

static int hwc_set(struct hwc_composer_device_1 *dev,
        size_t numDisplays, hwc_display_contents_1_t** displays)
{
    if (!numDisplays || !displays || !dev) {
        ALOGI("%s invalid parameter", __FUNCTION__);
        return 0;
    }

    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    hwc_display_contents_1_t *primary_contents = displays[HWC_DISPLAY_PRIMARY];
    hwc_display_contents_1_t *external_contents = displays[HWC_DISPLAY_EXTERNAL];

    if(ctx->m_viv_hwc) {

        char property[PROPERTY_VALUE_MAX];
        property_get("service.bootanim.exit", property, "0");
        if(!atoi(property)) numDisplays = numDisplays >= 1 ? 1 : 0;

        int err = ctx->m_viv_hwc->set(ctx->m_viv_hwc, numDisplays, displays);

        if(err) return err;
    }

    if (primary_contents && ctx->mDispInfo[HWC_DISPLAY_PRIMARY].blank == 0) {
        hwc_layer_1 *fbt = &primary_contents->hwLayers[primary_contents->numHwLayers - 1];
        if(ctx->mFbDev[HWC_DISPLAY_PRIMARY] != NULL)
        ctx->mFbDev[HWC_DISPLAY_PRIMARY]->post(ctx->mFbDev[HWC_DISPLAY_PRIMARY], fbt->handle);
    }
    
    if (external_contents && ctx->mDispInfo[HWC_DISPLAY_EXTERNAL].blank == 0) {
        hwc_layer_1 *fbt = &external_contents->hwLayers[external_contents->numHwLayers - 1];
        if(ctx->mFbDev[HWC_DISPLAY_EXTERNAL] != NULL)
        ctx->mFbDev[HWC_DISPLAY_EXTERNAL]->post(ctx->mFbDev[HWC_DISPLAY_EXTERNAL], fbt->handle);
    }

    return 0;
}

static void hwc_registerProcs(struct hwc_composer_device_1* dev,
            hwc_procs_t const* procs) {
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if(ctx) {
        ctx->m_callback = (hwc_procs_t*)procs;
    }
}

static int hwc_eventControl(struct hwc_composer_device_1* dev, int dpy, int event, int enabled)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if(ctx && event == HWC_EVENT_VSYNC) {
        ctx->m_vsync_thread->setEnabled(enabled);
    }

    return 0;
}

static int hwc_query(struct hwc_composer_device_1* dev,
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
        value[0] = ctx->mDispInfo[0].vsync_period;
        break;
    default:
        // unsupported query
        return -EINVAL;
    }
    return 0;
}

static int hwc_blank(struct hwc_composer_device_1 *dev, int disp, int blank)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (!ctx || disp < 0 || disp >= HWC_NUM_DISPLAY_TYPES) {
        return 0;
    }

    if (ctx->m_viv_hwc) {
        ctx->m_viv_hwc->blank(ctx->m_viv_hwc, disp, blank);
    }

    ctx->mDispInfo[disp].blank = blank;

    //HDMI need to keep unblank since audio need to be able to output
    //through HDMI cable. Blank the HDMI will lost the HDMI clock
    if (ctx->mDispInfo[disp].type !=  HWC_DISPLAY_HDMI) {
        int fb_blank = blank ? FB_BLANK_POWERDOWN : FB_BLANK_UNBLANK;
        int err = ioctl(ctx->mDispInfo[disp].fd, FBIOBLANK, fb_blank);
        if (err < 0) {
            ALOGE("blank ioctl failed");
            return -errno;
        }
    }

    return 0;
}

static int hwc_getDisplayConfigs(struct hwc_composer_device_1 *dev,
        int disp, uint32_t *configs, size_t *numConfigs)
{
    if (*numConfigs == 0)
        return 0;

    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (!ctx || disp < 0 || disp >= HWC_NUM_DISPLAY_TYPES) {
        return -EINVAL;
    }

    if (ctx->mDispInfo[disp].connected) {
        configs[0] = 0;
        *numConfigs = 1;
        return 0;
    }

    return -EINVAL;
}

static int hwc_getDisplayAttributes(struct hwc_composer_device_1 *dev,
        int disp, uint32_t config, const uint32_t *attributes, int32_t *values)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (!ctx || disp < 0 || disp >= HWC_NUM_DISPLAY_TYPES) {
        return -EINVAL;
    }

    for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
        switch(attributes[i]) {
            case HWC_DISPLAY_VSYNC_PERIOD:
                values[i] = ctx->mDispInfo[disp].vsync_period;
                break;

            case HWC_DISPLAY_WIDTH:
                values[i] = ctx->mDispInfo[disp].xres;
                break;

            case HWC_DISPLAY_HEIGHT:
                values[i] = ctx->mDispInfo[disp].yres;
                break;

            case HWC_DISPLAY_DPI_X:
                if(ctx->mDispInfo[disp].type == HWC_DISPLAY_LDB)
                    values[i] = ctx->mDispInfo[disp].xdpi;
                else
                    values[i] = 0;
                break;

            case HWC_DISPLAY_DPI_Y:
                if(ctx->mDispInfo[disp].type == HWC_DISPLAY_LDB)
                    values[i] = ctx->mDispInfo[disp].ydpi;
                else
                    values[i] = 0;
                break;
            case HWC_DISPLAY_FORMAT:
                values[i] = ctx->mDispInfo[disp].format;
                break;

            default:
                ALOGE("unknown display attribute %u", attributes[i]);
                continue;
        }
    }

    return 0;
}

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
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = hwc_device_close;

        dev->device.prepare = hwc_prepare;
        dev->device.set = hwc_set;
        dev->device.common.version = HWC_DEVICE_API_VERSION_1_1;
        dev->device.registerProcs = hwc_registerProcs;
        dev->device.eventControl = hwc_eventControl;
        dev->device.query = hwc_query;

        dev->device.blank = hwc_blank;
        dev->device.getDisplayConfigs = hwc_getDisplayConfigs;
        dev->device.getDisplayAttributes = hwc_getDisplayAttributes;

        /* our private state goes below here */
        dev->m_vsync_thread = new VSyncThread(dev);
        dev->m_uevent_thread = new UeventThread(dev);
        hwc_get_display_info(dev);

        hw_get_module(GRALLOC_HARDWARE_MODULE_ID, &dev->m_gralloc_module);
        struct private_module_t *priv_m = (struct private_module_t *)dev->m_gralloc_module;

        for(int dispid=0; dispid<HWC_NUM_DISPLAY_TYPES; dispid++) {
            if(dev->mDispInfo[dispid].connected && dev->m_gralloc_module != NULL) {
                int fbid = dev->mDispInfo[dispid].fb_num;
                char fbname[HWC_STRING_LENGTH];
                memset(fbname, 0, sizeof(fbname));
                sprintf(fbname, "fb%d", fbid);
                ALOGI("hwcomposer: open framebuffer %s", fbname);
                dev->mFbDev[dispid] = (framebuffer_device_t*)fbid;
                dev->m_gralloc_module->methods->open(dev->m_gralloc_module, fbname,
                           (struct hw_device_t**)&dev->mFbDev[dispid]);
            }
        }

        const hw_module_t *hwc_module;
        if(hw_get_module(HWC_VIV_HARDWARE_MODULE_ID,
                        (const hw_module_t**)&hwc_module) < 0) {
            ALOGE("Error! hw_get_module viv_hwc failed");
            goto nor_exit;
        }

        if(hwc_open_1(hwc_module, &(dev->m_viv_hwc)) != 0) {
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
