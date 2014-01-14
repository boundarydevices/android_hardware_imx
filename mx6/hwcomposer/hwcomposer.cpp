/*
 * Copyright (C) 2009-2014 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include <EGL/eglext.h>
#include "gralloc_priv.h"
#include "hwc_context.h"
#include "hwc_vsync.h"
#include "hwc_uevent.h"
#include "hwc_display.h"
#include <g2d.h>
#include <sync/sync.h>

/*****************************************************************************/
#define HWC_G2D   100

typedef EGLClientBuffer (EGLAPIENTRYP PFNEGLGETRENDERBUFFERVIVPROC) (EGLClientBuffer Handle);
typedef EGLBoolean (EGLAPIENTRYP PFNEGLPOSTBUFFERVIVPROC) (EGLClientBuffer Buffer);

static PFNEGLGETRENDERBUFFERVIVPROC  _eglGetRenderBufferVIV;
static PFNEGLPOSTBUFFERVIVPROC _eglPostBufferVIV;

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

extern int hwc_composite(struct hwc_context_t* ctx, hwc_layer_1_t* layer,
                    struct private_handle_t *dstHandle, hwc_rect_t* swap, bool firstLayer);
extern int hwc_clearWormHole(struct hwc_context_t* ctx, struct private_handle_t *dstHandle,
                    hwc_display_contents_1_t* list, int disp);
extern int hwc_clearRect(struct hwc_context_t* ctx, struct private_handle_t *dstHandle,
                    hwc_rect_t &rect);
extern int hwc_copyBack(struct hwc_context_t* ctx, struct private_handle_t *dstHandle,
                    struct private_handle_t *srcHandle, int swapIndex, int disp);
extern int hwc_resize(struct hwc_context_t* ctx, struct private_handle_t *dstHandle,
                    struct private_handle_t *srcHandle);
extern int hwc_updateSwapRect(struct hwc_context_t* ctx, int disp,
                 android_native_buffer_t* nbuf);
extern bool hwc_hasSameContent(struct hwc_context_t* ctx, int src,
            int dst, hwc_display_contents_1_t** lists);

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

        if (ctx->g2d_handle != NULL) {
            g2d_close(ctx->g2d_handle);
        }

        if(ctx->m_viv_hwc) {
            hwc_close_1(ctx->m_viv_hwc);
        }

        free(ctx);
    }
    return 0;
}

static bool checkG2dProcs(struct hwc_context_t* ctx,
                    hwc_display_contents_1_t* list)
{
    if (ctx == NULL || list == NULL) {
        return false;
    }

    hwc_layer_1_t* targetLayer = &list->hwLayers[list->numHwLayers-1];
    struct private_handle_t *targetHandle;
    targetHandle = (struct private_handle_t *)targetLayer->handle;
    if (targetHandle == NULL) {
        ALOGI("prepare: targetHandle is null");
        return false;
    }

    hwc_layer_1_t* layer = NULL;
    for (size_t i=0; i<list->numHwLayers-1; i++) {
        layer = &list->hwLayers[i];
        if (layer->flags & HWC_SKIP_LAYER) {
            ALOGV("skip layer");
            return false;
        }

        if ((layer->blending & 0xFFFF) == HWC_BLENDING_DIM) {
            ALOGV("dim layer");
            continue;
        }

        if (layer->handle == NULL) {
            ALOGV("layer handle is null");
            return false;
        }
    }

    return true;
}

static int hwc_prepare_physical(struct hwc_context_t* ctx, int disp,
                         hwc_display_contents_1_t* list)
{
    if (ctx == NULL || ctx->g2d_handle == NULL || list == NULL) {
        ctx->mDispInfo[disp].mG2dProcs = false;
        ALOGV("%s: disp:%d invalid parameter", __FUNCTION__, disp);
        return 0;
    }

    if (!ctx->mDispInfo[disp].connected) {
        ALOGE("physical display:%d is diconnected", disp);
        ctx->mDispInfo[disp].mG2dProcs = false;
        return -EINVAL;
    }

    bool g2dProcs = checkG2dProcs(ctx, list);
    if (!g2dProcs) {
        ALOGV("pass to 3D to handle");
        ctx->mDispInfo[disp].mG2dProcs = false;
        return 0;
    }

    hwc_layer_1_t* layer = NULL;
    for (size_t i=0; i<list->numHwLayers-1; i++) {
        layer = &list->hwLayers[i];
        layer->compositionType = HWC_G2D;
    }
    ctx->mDispInfo[disp].mG2dProcs = true;

    return 0;
}

static int hwc_prepare_virtual(struct hwc_context_t* ctx, int disp,
                         hwc_display_contents_1_t* list)
{
    if (ctx == NULL || ctx->g2d_handle == NULL || list == NULL) {
        ctx->mDispInfo[disp].mG2dProcs = false;
        ALOGV("%s: disp:%d invalid parameter", __FUNCTION__, disp);
        return 0;
    }

    bool g2dProcs = checkG2dProcs(ctx, list);
    if (!g2dProcs) {
        ALOGV("pass to 3D to handle");
        ctx->mDispInfo[disp].mG2dProcs = false;
        return 0;
    }

    hwc_layer_1_t* layer = NULL;
    for (size_t i=0; i<list->numHwLayers-1; i++) {
        layer = &list->hwLayers[i];
        layer->compositionType = HWC_G2D;
    }
    ctx->mDispInfo[disp].mG2dProcs = true;

    return 0;
}

static int hwc_set_physical(struct hwc_context_t* ctx, int disp,
                         hwc_display_contents_1_t** contents)
{
    hwc_display_contents_1_t* list = contents[disp];
    if (ctx == NULL || list == NULL) {
        ALOGV("%s: disp:%d invalid parameter", __FUNCTION__, disp);
        return 0;
    }

    // to do copyback when use swap retangle.
    hwc_layer_1_t* targetLayer = &list->hwLayers[list->numHwLayers-1];
    struct private_handle_t *targetHandle;
    targetHandle = (struct private_handle_t *)targetLayer->handle;

    if (!ctx->mDispInfo[disp].mG2dProcs) {
        if (targetHandle != NULL && ctx->mDispInfo[disp].connected) {
            ctx->mFbDev[disp]->post(ctx->mFbDev[disp], targetHandle);
        }
        hwc_updateSwapRect(ctx, disp, NULL);
        return 0;
    }

    hwc_layer_1_t* layer = NULL;
    for (size_t i=0; i<list->numHwLayers-1; i++) {
        layer = &list->hwLayers[i];
        if (layer->compositionType != HWC_G2D) {
            ALOGE("invalid compositionType:%d", layer->compositionType);
            return -EINVAL;
        }
    }

    //framebuffer handle.
    android_native_buffer_t *fbuffer = NULL;
    struct private_handle_t *frameHandle;
    fbuffer = (ANativeWindowBuffer *) _eglGetRenderBufferVIV(targetHandle);
    if (fbuffer == NULL) {
        ALOGE("get render buffer failed!");
        return -EINVAL;
    }

    frameHandle = (struct private_handle_t *)(fbuffer->handle);
    if (frameHandle == NULL) {
        ALOGE("invalid frame buffer handle");
        return -EINVAL;
    }

    int index = hwc_updateSwapRect(ctx, disp, fbuffer);
    if (index < 0) {
        ALOGE("invalid index");
        return -EINVAL;
    }

    hwc_clearWormHole(ctx, frameHandle, list, disp);

    bool resized = false;
    if (disp != HWC_DISPLAY_PRIMARY &&
        hwc_hasSameContent(ctx, HWC_DISPLAY_PRIMARY, disp, contents)) {
        hwc_display_contents_1_t* sList = contents[HWC_DISPLAY_PRIMARY];
        hwc_layer_1_t* primaryLayer = &sList->hwLayers[sList->numHwLayers-1];
        struct private_handle_t *primaryHandle = NULL;
        primaryHandle = (struct private_handle_t *)primaryLayer->handle;
        if (primaryHandle != NULL) {
            hwc_resize(ctx, frameHandle, primaryHandle);
            resized = true;
        }
    }

    if (!resized) {
        hwc_rect_t& swapRect = ctx->mDispInfo[disp].mSwapRect[index];
        for (size_t i=0; i<list->numHwLayers-1; i++) {
            layer = &list->hwLayers[i];
            int fenceFd = layer->acquireFenceFd;
            if (fenceFd > 0) {
                ALOGI("fenceFd:%d", fenceFd);
                sync_wait(fenceFd, -1);
                close(fenceFd);
                layer->acquireFenceFd = -1;
            }
            hwc_composite(ctx, layer, frameHandle, &swapRect, i==0);
        }

        hwc_copyBack(ctx, frameHandle, targetHandle, index, disp);
    }
    g2d_finish(ctx->g2d_handle);

    _eglPostBufferVIV(fbuffer);

    targetHandle = (struct private_handle_t *)targetLayer->handle;
    if (targetHandle != NULL && ctx->mDispInfo[disp].connected && ctx->mDispInfo[disp].blank == 0) {
        ctx->mFbDev[disp]->post(ctx->mFbDev[disp], targetHandle);
    }

    return 0;
}

static int hwc_set_virtual(struct hwc_context_t* ctx, int disp,
                         hwc_display_contents_1_t** contents)
{
    hwc_display_contents_1_t* list = contents[disp];
    if (ctx == NULL || list == NULL) {
        return 0;
    }

    if (list->outbuf == NULL) {
        ALOGE("invalid outbuf for virtual display");
        return -EINVAL;
    }

    struct private_handle_t *frameHandle;
    frameHandle = (struct private_handle_t *)(list->outbuf);
    if (frameHandle == NULL) {
        ALOGE("invalid frame buffer handle");
        return -EINVAL;
    }

    if (!ctx->mDispInfo[disp].mG2dProcs) {
        return 0;
    }

    hwc_layer_1_t* layer = NULL;
    for (size_t i=0; i<list->numHwLayers-1; i++) {
        layer = &list->hwLayers[i];
        if (layer->compositionType != HWC_G2D) {
            ALOGE("invalid compositionType:%d", layer->compositionType);
            return -EINVAL;
        }
    }

    int fenceFd = list->outbufAcquireFenceFd;
    if (fenceFd != -1) {
        sync_wait(fenceFd, -1);
        close(fenceFd);
        list->outbufAcquireFenceFd = -1;
    }

    if (disp != HWC_DISPLAY_PRIMARY &&
        hwc_hasSameContent(ctx, HWC_DISPLAY_PRIMARY, disp, contents)) {
        hwc_display_contents_1_t* sList = contents[HWC_DISPLAY_PRIMARY];
        hwc_layer_1_t* primaryLayer = &sList->hwLayers[sList->numHwLayers-1];
        struct private_handle_t *primaryHandle = NULL;
        primaryHandle = (struct private_handle_t *)primaryLayer->handle;
        if (primaryHandle != NULL) {
            hwc_resize(ctx, frameHandle, primaryHandle);
            g2d_finish(ctx->g2d_handle);
            return 0;
        }
    }

    for (size_t i=0; i<list->numHwLayers-1; i++) {
        layer = &list->hwLayers[i];
        int fenceFd = layer->acquireFenceFd;
        if (fenceFd > 0) {
            ALOGI("fenceFd:%d", fenceFd);
            sync_wait(fenceFd, -1);
            close(fenceFd);
            layer->acquireFenceFd = -1;
        }

        hwc_composite(ctx, layer, frameHandle, NULL, i==0);
    }

    g2d_finish(ctx->g2d_handle);

    return 0;
}

static int hwc_prepare(hwc_composer_device_1_t *dev,
        size_t numDisplays, hwc_display_contents_1_t** displays)
{
    if (!numDisplays || !displays || !dev) {
        ALOGI("%s invalid parameter", __FUNCTION__);
        return 0;
    }

    int ret = 0;
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;

    if(ctx->m_viv_hwc) {
        char property[PROPERTY_VALUE_MAX];
        property_get("service.bootanim.exit", property, "0");
        if(!atoi(property)) numDisplays = numDisplays >= 1 ? 1 : 0;
        return ctx->m_viv_hwc->prepare(ctx->m_viv_hwc, numDisplays, displays);
    }

    if (ctx->g2d_handle == NULL) {
        g2d_open(&ctx->g2d_handle);
        if (ctx->g2d_handle == NULL) {
            ALOGE("%s invalid g2d_handle", __FUNCTION__);
            return 0;
        }
    }

    char property[PROPERTY_VALUE_MAX];
    property_get("service.bootanim.exit", property, "0");
    if(!atoi(property)) numDisplays = numDisplays >= 1 ? 1 : 0;

    for (size_t i = 0; i < numDisplays; i++) {
        hwc_display_contents_1_t *list = displays[i];
        switch(i) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_prepare_physical(ctx, i, displays[i]);
                break;
            case HWC_DISPLAY_EXTERNAL:
                ret = hwc_prepare_physical(ctx, i, displays[i]);
                break;
            case HWC_DISPLAY_VIRTUAL:
                ret = hwc_prepare_virtual(ctx, i, displays[i]);
                break;
            default:
                ALOGI("invalid display id:%d", i);
                break;
        }
    }

    return ret;
}

static int hwc_set(struct hwc_composer_device_1 *dev,
        size_t numDisplays, hwc_display_contents_1_t** displays)
{
    if (!numDisplays || !displays || !dev) {
        ALOGI("%s invalid parameter", __FUNCTION__);
        return 0;
    }

    int ret = 0;
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;

    if(ctx->m_viv_hwc) {
        char property[PROPERTY_VALUE_MAX];
        property_get("service.bootanim.exit", property, "0");
        if(!atoi(property)) numDisplays = numDisplays >= 1 ? 1 : 0;

        int err = ctx->m_viv_hwc->set(ctx->m_viv_hwc, numDisplays, displays);
        if(err) return err;

        hwc_display_contents_1_t *primary_contents = displays[HWC_DISPLAY_PRIMARY];
        hwc_display_contents_1_t *external_contents = displays[HWC_DISPLAY_EXTERNAL];
        if (primary_contents && ctx->mDispInfo[HWC_DISPLAY_PRIMARY].blank == 0) {
            hwc_layer_1 *fbt = &primary_contents->hwLayers[primary_contents->numHwLayers - 1];
            if(ctx->mFbDev[HWC_DISPLAY_PRIMARY] != NULL)
                ctx->mFbDev[HWC_DISPLAY_PRIMARY]->post(ctx->mFbDev[HWC_DISPLAY_PRIMARY],  fbt->handle);
        }

        if (external_contents && ctx->mDispInfo[HWC_DISPLAY_EXTERNAL].blank == 0) {
            hwc_layer_1 *fbt = &external_contents->hwLayers[external_contents->numHwLayers - 1];
            if(ctx->mFbDev[HWC_DISPLAY_EXTERNAL] != NULL)
                ctx->mFbDev[HWC_DISPLAY_EXTERNAL]->post(ctx->mFbDev[HWC_DISPLAY_EXTERNAL], fbt->handle);
        }

        return 0;
    }

    if (ctx->g2d_handle == NULL) {
        ALOGI("%s invalid g2d_handle", __FUNCTION__);
        return 0;
    }

    char property[PROPERTY_VALUE_MAX];
    property_get("service.bootanim.exit", property, "0");
    if(!atoi(property)) numDisplays = numDisplays >= 1 ? 1 : 0;

    for (size_t i = 0; i < numDisplays;i++) {
        hwc_display_contents_1_t *list = displays[i];
        switch(i) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_set_physical(ctx, i, displays);
                break;
            case HWC_DISPLAY_EXTERNAL:
                ret = hwc_set_physical(ctx, i, displays);
                break;
            case HWC_DISPLAY_VIRTUAL:
                ret = hwc_set_virtual(ctx, i, displays);
                break;
            default:
                ALOGI("invalid display id:%d", i);
                break;
        }
    }

    return ret;
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

static int hwc_setDisplaySurface(struct hwc_composer_device_1 *dev,
        int disp, ANativeWindow* window)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (!ctx || disp < 0 || disp >= HWC_NUM_DISPLAY_TYPES) {
        return -EINVAL;
    }

    ALOGI("%s: disp:%d", __FUNCTION__, disp);
    ctx->mDispInfo[disp].mDisplaySurface = window;

    return 0;
}

static int hwc_setDisplayWormHole(struct hwc_composer_device_1 *dev,
        int disp, hwc_region_t& hole)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (!ctx || disp < 0 || disp >= HWC_NUM_DISPLAY_TYPES) {
        return -EINVAL;
    }

    ALOGI("%s: disp:%d", __FUNCTION__, disp);
    ctx->mDispInfo[disp].mWormHole.numRects = hole.numRects;
    if (hole.numRects > 0 && hole.rects != NULL) {
        ctx->mDispInfo[disp].mWormHole.rects = (const hwc_rect_t*)malloc(hole.numRects*sizeof(hwc_rect_t));
        memcpy((void*)ctx->mDispInfo[disp].mWormHole.rects, hole.rects, hole.numRects*sizeof(hwc_rect_t));
    }

    return 0;
}
/*****************************************************************************/

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = -EINVAL;
    struct hwc_context_t *dev = NULL;
    if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
        //struct hwc_context_t *dev;
        dev = (hwc_context_t*)malloc(sizeof(*dev));

        /* initialize our state here */
        memset(dev, 0, sizeof(*dev));

        /* initialize the procs */
        dev->device.common.tag = HARDWARE_DEVICE_TAG;
        dev->device.common.module = const_cast<hw_module_t*>(module);
        dev->device.common.close = hwc_device_close;

        dev->device.prepare = hwc_prepare;
        dev->device.set = hwc_set;
        dev->device.common.version = HWC_DEVICE_API_VERSION_1_3;
        dev->device.registerProcs = hwc_registerProcs;
        dev->device.eventControl = hwc_eventControl;
        dev->device.query = hwc_query;

        dev->device.blank = hwc_blank;
        dev->device.getDisplayConfigs = hwc_getDisplayConfigs;
        dev->device.getDisplayAttributes = hwc_getDisplayAttributes;
        //dev->device.setDisplaySurface = hwc_setDisplaySurface;

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

        char property[PROPERTY_VALUE_MAX];
        property_get("sys.fsl.hwc", property, "0");
        if(!atoi(property)) {
            const hw_module_t *hwc_module;
            if(hw_get_module(HWC_VIV_HARDWARE_MODULE_ID,
                        (const hw_module_t**)&hwc_module) < 0) {
                ALOGE("Error! hw_get_module viv_hwc failed");
            }
            else if(hwc_open_1(hwc_module, &(dev->m_viv_hwc)) != 0) {
                ALOGE("Error! viv_hwc open failed");
            }
            else {
                ALOGI("using viv hwc!");
                goto nor_exit;
            }
        }

        if (_eglGetRenderBufferVIV == NULL || _eglPostBufferVIV == NULL)
        {
            _eglGetRenderBufferVIV = (PFNEGLGETRENDERBUFFERVIVPROC)
                eglGetProcAddress("eglGetRenderBufferVIV");

            if (_eglGetRenderBufferVIV == NULL)
            {
                ALOGE("eglGetRenderBufferVIV not found!");
                status = -EINVAL;
                goto err_exit;
            }

            _eglPostBufferVIV = (PFNEGLPOSTBUFFERVIVPROC)
                eglGetProcAddress("eglPostBufferVIV");

            if (_eglPostBufferVIV == NULL)
            {
                ALOGE("eglPostBufferVIV not found!");
                status = -EINVAL;
                goto err_exit;
            }
        }
        ALOGI("using fsl hwc!");

nor_exit:
        *device = &dev->device.common;
	    ALOGI("%s,%d", __FUNCTION__, __LINE__);
        return 0;
    }

err_exit:
	if(dev){
	    free(dev);
	}
    /****************************************/
    return status;
}
