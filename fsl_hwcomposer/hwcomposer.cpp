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
#include <ui/Rect.h>
#include <ui/Region.h>

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
#define HWC_G2D   HWC_OVERLAY

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

extern int hwc_composite(struct fsl_private *priv, hwc_layer_1_t* layer,
                   struct private_handle_t *dstHandle, hwc_rect_t* swap, bool firstLayer);
extern int hwc_clearWormHole(struct fsl_private *priv, struct private_handle_t *dstHandle,
                    hwc_display_contents_1_t* list, int disp, hwc_rect_t* swap);
extern int hwc_clearRect(struct fsl_private *priv, struct private_handle_t *dstHandle,
                    hwc_rect_t &rect);
extern int hwc_copyBack(struct fsl_private *priv, struct private_handle_t *dstHandle,
                    struct private_handle_t *srcHandle, int swapIndex, int disp);
extern int hwc_resize(struct fsl_private *priv, struct private_handle_t *dstHandle,
                    struct private_handle_t *srcHandle);
extern int hwc_updateSwapRect(struct fsl_private *priv, int disp,
                 android_native_buffer_t* nbuf);
extern bool hwc_hasSameContent(struct fsl_private *priv, int src,
            int dst, hwc_display_contents_1_t** lists);

extern "C" void* g2d_getRenderBuffer(void *handle, void *BufferHandle);
extern "C" unsigned int g2d_postBuffer(void *handle, void* PostBuffer);

static struct hw_module_methods_t hwc_module_methods = {
    open: hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    common: {
        tag: HARDWARE_MODULE_TAG,
        version_major: 2,
        version_minor: 0,
        id: HWC_FSL_HARDWARE_MODULE_ID,
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
static int hwc_device_close(struct hwc_operations* ctx)
{
    if (!ctx) {
        return 0;
    }
    struct fsl_private *priv = (struct fsl_private*)ctx;
    if (priv->tmp_buf != NULL) {
        g2d_free(priv->tmp_buf);
    }
    if (priv->g2d_handle != NULL) {
        g2d_close(priv->g2d_handle);
    }

    free(priv);
    return 0;
}

static bool checkG2dProcs(struct fsl_private *priv, int disp,
                    hwc_display_contents_1_t* list)
{
    if (priv == NULL || list == NULL) {
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

static int hwc_prepare_physical(struct fsl_private *priv, int disp,
                         hwc_display_contents_1_t* list)
{
    if (priv == NULL || priv->g2d_handle == NULL || list == NULL) {
        priv->mDispInfo[disp].mG2dProcs = false;
        ALOGV("%s: disp:%d invalid parameter", __FUNCTION__, disp);
        return 0;
    }

    if (!priv->mDispInfo[disp].connected) {
        ALOGE("physical display:%d is diconnected", disp);
        hwc_layer_1_t* layer = NULL;
        for (size_t i=0; i<list->numHwLayers-1; i++) {
            layer = &list->hwLayers[i];
            layer->compositionType = HWC_FRAMEBUFFER;
        }
        priv->mDispInfo[disp].mG2dProcs = false;
        return -EINVAL;
    }

    bool g2dProcs = checkG2dProcs(priv, disp, list);
    if (!g2dProcs) {
        ALOGV("pass to 3D to handle");
        hwc_layer_1_t* layer = NULL;
        for (size_t i=0; i<list->numHwLayers-1; i++) {
            layer = &list->hwLayers[i];
            layer->compositionType = HWC_FRAMEBUFFER;
        }
        priv->mDispInfo[disp].mG2dProcs = false;
        return 0;
    }

    hwc_layer_1_t* layer = NULL;
    for (size_t i=0; i<list->numHwLayers-1; i++) {
        layer = &list->hwLayers[i];
        layer->compositionType = HWC_G2D;
    }
    priv->mDispInfo[disp].mG2dProcs = true;

    return 0;
}

static int hwc_prepare_virtual(struct fsl_private *priv, int disp,
                         hwc_display_contents_1_t* list)
{
    if (priv == NULL || priv->g2d_handle == NULL || list == NULL) {
        priv->mDispInfo[disp].mG2dProcs = false;
        ALOGV("%s: disp:%d invalid parameter", __FUNCTION__, disp);
        return 0;
    }

    bool g2dProcs = checkG2dProcs(priv, disp, list);
    if (!g2dProcs) {
        ALOGV("pass to 3D to handle");
        hwc_layer_1_t* layer = NULL;
        for (size_t i=0; i<list->numHwLayers-1; i++) {
            layer = &list->hwLayers[i];
            layer->compositionType = HWC_FRAMEBUFFER;
        }
        priv->mDispInfo[disp].mG2dProcs = false;
        return 0;
    }

    hwc_layer_1_t* layer = NULL;
    for (size_t i=0; i<list->numHwLayers-1; i++) {
        layer = &list->hwLayers[i];
        layer->compositionType = HWC_G2D;
    }
    priv->mDispInfo[disp].mG2dProcs = true;

    return 0;
}

static int hwc_set_physical(struct fsl_private* priv, int disp,
                         hwc_display_contents_1_t** contents)
{
    hwc_display_contents_1_t* list = contents[disp];
    if (priv == NULL || list == NULL) {
        ALOGV("%s: disp:%d invalid parameter", __FUNCTION__, disp);
        return 0;
    }

    // to do copyback when use swap retangle.
    hwc_layer_1_t* targetLayer = &list->hwLayers[list->numHwLayers-1];
    struct private_handle_t *targetHandle;
    targetHandle = (struct private_handle_t *)targetLayer->handle;

    for (size_t i=0; i<list->numHwLayers; i++) {
        hwc_layer_1_t* layer = NULL;
        layer = &list->hwLayers[i];
        int fenceFd = layer->acquireFenceFd;
        if (fenceFd != -1) {
            ALOGV("fenceFd:%d", fenceFd);
            sync_wait(fenceFd, -1);
            close(fenceFd);
            layer->acquireFenceFd = -1;
        }
    }

    if (!priv->mDispInfo[disp].mG2dProcs) {
        hwc_updateSwapRect(priv, disp, NULL);
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
    fbuffer = (ANativeWindowBuffer *) g2d_getRenderBuffer(priv->g2d_handle, targetHandle);
    if (fbuffer == NULL) {
        ALOGE("get render buffer failed!");
        return -EINVAL;
    }

    frameHandle = (struct private_handle_t *)(fbuffer->handle);
    if (frameHandle == NULL) {
        ALOGE("invalid frame buffer handle");
        return -EINVAL;
    }

    int index = hwc_updateSwapRect(priv, disp, fbuffer);
    if (index < 0) {
        ALOGE("invalid index");
        return -EINVAL;
    }

    hwc_rect_t& swapRect = priv->mDispInfo[disp].mSwapRect[index];
    hwc_clearWormHole(priv, frameHandle, list, disp, &swapRect);

    bool resized = false;
    if (disp != HWC_DISPLAY_PRIMARY &&
        hwc_hasSameContent(priv, HWC_DISPLAY_PRIMARY, disp, contents)) {
        hwc_display_contents_1_t* sList = contents[HWC_DISPLAY_PRIMARY];
        hwc_layer_1_t* primaryLayer = &sList->hwLayers[sList->numHwLayers-1];
        struct private_handle_t *primaryHandle = NULL;
        primaryHandle = (struct private_handle_t *)primaryLayer->handle;
        if (primaryHandle != NULL) {
            hwc_resize(priv, frameHandle, primaryHandle);
            resized = true;
        }
    }

    if (!resized) {
        for (size_t i=0; i<list->numHwLayers-1; i++) {
            layer = &list->hwLayers[i];
            hwc_composite(priv, layer, frameHandle, &swapRect, i==0);
        }

        hwc_copyBack(priv, frameHandle, targetHandle, index, disp);
    }
    g2d_finish(priv->g2d_handle);

    g2d_postBuffer(priv->g2d_handle, fbuffer);

    return 0;
}

static int hwc_set_virtual(struct fsl_private* priv, int disp,
                         hwc_display_contents_1_t** contents)
{
    hwc_display_contents_1_t* list = contents[disp];
    if (priv == NULL || list == NULL) {
        return 0;
    }

    for (size_t i=0; i<list->numHwLayers; i++) {
        hwc_layer_1_t* layer = NULL;
        layer = &list->hwLayers[i];
        int fenceFd = layer->acquireFenceFd;
        if (fenceFd != -1) {
            ALOGV("fenceFd:%d", fenceFd);
            sync_wait(fenceFd, -1);
            close(fenceFd);
            layer->acquireFenceFd = -1;
        }
    }

    int fenceFd = list->outbufAcquireFenceFd;
    if (fenceFd != -1) {
        sync_wait(fenceFd, -1);
        close(fenceFd);
        list->outbufAcquireFenceFd = -1;
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

    if (!priv->mDispInfo[disp].mG2dProcs) {
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

    hwc_clearWormHole(priv, frameHandle, list, disp, NULL);
    for (size_t i=0; i<list->numHwLayers-1; i++) {
        layer = &list->hwLayers[i];
        hwc_composite(priv, layer, frameHandle, NULL, i==0);
    }

    g2d_finish(priv->g2d_handle);

    return 0;
}

static int hwc_prepare(struct hwc_operations* ctx,
        size_t numDisplays, hwc_display_contents_1_t** displays)
{
    if (!numDisplays || !displays || !ctx) {
        ALOGI("%s invalid parameter", __FUNCTION__);
        return 0;
    }

    int ret = 0;
    struct fsl_private *priv = (struct fsl_private *)ctx;

    if (priv->g2d_handle == NULL) {
        g2d_open(&priv->g2d_handle);
        if (priv->g2d_handle == NULL) {
            ALOGE("%s invalid g2d_handle", __FUNCTION__);
            return 0;
        }
        priv->vg_engine = !g2d_make_current(priv->g2d_handle, G2D_HARDWARE_VG);
        g2d_make_current(priv->g2d_handle, G2D_HARDWARE_2D);
    }

    for (size_t i = 0; i < numDisplays; i++) {
        hwc_display_contents_1_t *list = displays[i];
        switch(i) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_prepare_physical(priv, i, displays[i]);
                break;
            case HWC_DISPLAY_EXTERNAL:
                ret = hwc_prepare_physical(priv, i, displays[i]);
                break;
            case HWC_DISPLAY_VIRTUAL:
                ret = hwc_prepare_virtual(priv, i, displays[i]);
                break;
            default:
                ALOGI("invalid display id:%d", i);
                break;
        }
    }

    return ret;
}

static int hwc_set(struct hwc_operations* ctx,
        size_t numDisplays, hwc_display_contents_1_t** displays)
{
    if (!numDisplays || !displays || !ctx) {
        ALOGI("%s invalid parameter", __FUNCTION__);
        return 0;
    }

    int ret = 0;
    struct fsl_private *priv = (struct fsl_private *)ctx;

    if (priv->g2d_handle == NULL) {
        ALOGI("%s invalid g2d_handle", __FUNCTION__);
        return 0;
    }

    for (size_t i = 0; i < numDisplays;i++) {
        hwc_display_contents_1_t *list = displays[i];
        switch(i) {
            case HWC_DISPLAY_PRIMARY:
                ret = hwc_set_physical(priv, i, displays);
                break;
            case HWC_DISPLAY_EXTERNAL:
                ret = hwc_set_physical(priv, i, displays);
                break;
            case HWC_DISPLAY_VIRTUAL:
                ret = hwc_set_virtual(priv, i, displays);
                break;
            default:
                ALOGI("invalid display id:%d", i);
                break;
        }
    }

    return ret;
}

static int hwc_blank(struct hwc_operations* ctx, int disp, int blank)
{
    if (!ctx || disp < 0 || disp >= HWC_NUM_DISPLAY_TYPES) {
        return 0;
    }

    return 0;
}

static int hwc_setDisplaySurface(struct hwc_operations* ctx,
        int disp, ANativeWindow* window)
{
    if (!ctx || disp < 0 || disp >= HWC_NUM_DISPLAY_TYPES) {
        return -EINVAL;
    }

    ALOGI("%s: disp:%d", __FUNCTION__, disp);
    struct fsl_private *priv = (struct fsl_private *)ctx;
    struct disp_private *dispInfo = &priv->mDispInfo[disp];
    dispInfo->mDisplaySurface = window;

    return 0;
}

static int hwc_setDisplayWormHole(struct hwc_operations* ctx,
        int disp, hwc_region_t& hole)
{
    if (!ctx || disp < 0 || disp >= HWC_NUM_DISPLAY_TYPES) {
        return -EINVAL;
    }

    ALOGI("%s: disp:%d", __FUNCTION__, disp);

    struct fsl_private *priv = (struct fsl_private *)ctx;
    struct disp_private *dispInfo = &priv->mDispInfo[disp];
    dispInfo->mWormHole.numRects = hole.numRects;
    if (hole.numRects > 0 && hole.rects != NULL) {
        dispInfo->mWormHole.rects = (const hwc_rect_t*)malloc(
                               hole.numRects*sizeof(hwc_rect_t));
        memcpy((void*)dispInfo->mWormHole.rects, hole.rects,
               hole.numRects*sizeof(hwc_rect_t));
    }

    return 0;
}

void hwc_setDisplayInfo(struct hwc_operations *ctx, int disp,
                        int xres, int yres, bool connected)
{
    if (ctx != NULL) {
        struct fsl_private *priv = (struct fsl_private *)ctx;
        struct disp_private *dispInfo = &priv->mDispInfo[disp];

        dispInfo->xres = xres;
        dispInfo->yres = yres;
        dispInfo->connected = connected;
        dispInfo->mSwapIndex = 0;
        for (int i=0; i<HWC_MAX_FRAMEBUFFER; i++) {
            dispInfo->mSwapRect[i].left = 0;
            dispInfo->mSwapRect[i].top = 0;
            dispInfo->mSwapRect[i].right = xres;
            dispInfo->mSwapRect[i].bottom = yres;
        }
    }
}

/*****************************************************************************/

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = -EINVAL;
    struct fsl_private *priv = NULL;
    if (!strcmp(name, HWC_HARDWARE_COMPOSER)) {
        priv = (struct fsl_private*)malloc(sizeof(*priv));
        /* initialize our state here */
        memset(priv, 0, sizeof(*priv));

        priv->prepare = hwc_prepare;
        priv->set = hwc_set;
        priv->blank = hwc_blank;
        priv->setDisplayInfo = hwc_setDisplayInfo;
        priv->close = hwc_device_close;

        ALOGI("using fsl hwc!!!");

nor_exit:
        *device = (struct hw_device_t*)priv;
        ALOGI("%s,%d", __FUNCTION__, __LINE__);
        return 0;
    }

err_exit:
    if(priv){
        free(priv);
    }
    /****************************************/
    return status;
}
