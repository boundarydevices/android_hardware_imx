/*
 * Copyright (C) 2009-2016 Freescale Semiconductor, Inc. All Rights Reserved.
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
#include <utils/KeyedVector.h>

#include <linux/mxcfb.h>
#include <linux/ioctl.h>
#include "context.h"
#include <Display.h>
#include <DisplayManager.h>
#include <g2dExt.h>
#include <sync/sync.h>

#define HWC_G2D   HWC_OVERLAY

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device);

static struct hw_module_methods_t hwc_module_methods = {
    .open = hwc_device_open
};

hwc_module_t HAL_MODULE_INFO_SYM = {
    .common= {
        .tag = HARDWARE_MODULE_TAG,
        .version_major = 2,
        .version_minor = 0,
        .id = HWC_HARDWARE_MODULE_ID,
        .name = "Freescale i.MX hwcomposer module",
        .author = "Freescale Semiconductor, Inc.",
        .methods = &hwc_module_methods,
        .dso = NULL,
        .reserved = {0}
    }
};

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
        free(ctx);
    }
    return 0;
}

static int getLayerType(hwc_layer_1_t* hwlayer)
{
    if (hwlayer->flags & HWC_SKIP_LAYER) {
        return LAYER_TYPE_CLIENT;
    }

    if ((hwlayer->blending & 0xFFFF) == BLENDING_DIM) {
        return LAYER_TYPE_SOLID_COLOR;
    }

    if (hwlayer->handle == NULL) {
        return LAYER_TYPE_CLIENT;
    }

    if (hwlayer->compositionType == HWC_SIDEBAND) {
        return LAYER_TYPE_SIDEBAND;
    }

    return LAYER_TYPE_DEVICE;
}

static int setLayer(hwc_layer_1_t* hwlayer, Layer* layer, int index)
{
    layer->zorder = index;
    layer->origType = getLayerType(hwlayer);
    layer->handle = (Memory*)(hwlayer->handle);
    layer->transform = hwlayer->transform;
    layer->blendMode = hwlayer->blending;
    layer->planeAlpha = hwlayer->planeAlpha;
    layer->color = hwlayer->planeAlpha << 24;
    layer->sourceCrop.left = hwlayer->sourceCropf.left;
    layer->sourceCrop.top = hwlayer->sourceCropf.top;
    layer->sourceCrop.right = hwlayer->sourceCropf.right;
    layer->sourceCrop.bottom = hwlayer->sourceCropf.bottom;
    memcpy(&layer->displayFrame, &hwlayer->displayFrame, sizeof(Rect));
    layer->visibleRegion.clear();
    for (size_t n=0; n<hwlayer->visibleRegionScreen.numRects; n++) {
        Rect rect;
        const hwc_rect_t &hrect = hwlayer->visibleRegionScreen.rects[n];
        memcpy(&rect, &hrect, sizeof(Rect));
        if (rect.isEmpty()) {
            continue;
        }
        layer->visibleRegion.orSelf(rect);
    }
    layer->acquireFence = hwlayer->acquireFenceFd;
    hwlayer->acquireFenceFd = -1;
    layer->releaseFence = -1;
    layer->priv = hwlayer;

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
    DisplayManager* displayManager = DisplayManager::getInstance();
    Display* display = NULL;

    for (size_t i = 0; i < numDisplays; i++) {
        hwc_display_contents_1_t *list = displays[i];
        display = NULL;
        if (list == NULL) {
            continue;
        }

        switch(i) {
            case HWC_DISPLAY_PRIMARY:
            case HWC_DISPLAY_EXTERNAL:
                display = displayManager->getPhysicalDisplay(i);
                break;
            case HWC_DISPLAY_VIRTUAL:
                display = displayManager->getVirtualDisplay(
                                i-HWC_DISPLAY_VIRTUAL+MAX_PHYSICAL_DISPLAY);
                break;
            default:
                ALOGI("invalid display id:%zu", i);
                break;
        }

        if (!display || (!display->connected() && i != HWC_DISPLAY_PRIMARY)) {
            continue;
        }

        display->invalidLayers();
        hwc_layer_1_t* hwlayer = NULL;
        for (size_t k=0; k<list->numHwLayers-1; k++) {
            hwlayer = &list->hwLayers[k];
            Layer* layer = display->getFreeLayer();
            if (layer == NULL) {
                ALOGE("%s get free layer failed", __func__);
                return -ENOSR;
            }
            setLayer(hwlayer, layer, k);
        }
        if (!display->verifyLayers()) {
            ALOGV("pass to 3D to handle");
            // set overlay here.
            for (size_t k=0; k<list->numHwLayers-1; k++) {
                hwlayer = &list->hwLayers[k];
                Layer* layer = display->getLayerByPriv(hwlayer);
                if (layer->type == LAYER_TYPE_DEVICE) {
                    hwlayer->compositionType = HWC_G2D;
                }
            }
            continue;
        }

        for (size_t k=0; k<list->numHwLayers-1; k++) {
            hwlayer = &list->hwLayers[k];
            hwlayer->compositionType = HWC_G2D;
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
    int fenceFd;
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    DisplayManager* displayManager = DisplayManager::getInstance();
    Display* display = NULL;

    for (size_t i = 0; i < numDisplays; i++) {
        display = NULL;
        Memory* target = NULL;
        int fenceFd;
        hwc_layer_1 *fbt = NULL;
        hwc_display_contents_1_t *list = displays[i];
        if (list == NULL) {
            continue;
        }

        switch(i) {
            case HWC_DISPLAY_PRIMARY:
            case HWC_DISPLAY_EXTERNAL:
                fbt = &list->hwLayers[list->numHwLayers-1];
                display = displayManager->getPhysicalDisplay(i);
                if(fbt != NULL) {
                    target = (Memory*)fbt->handle;
                    fenceFd = fbt->acquireFenceFd;
                    fbt->acquireFenceFd = -1;
                }
                break;

            case HWC_DISPLAY_VIRTUAL:
                display = displayManager->getVirtualDisplay(
                                i-HWC_DISPLAY_VIRTUAL+MAX_PHYSICAL_DISPLAY);
                target = (Memory*)list->outbuf;
                fenceFd = list->outbufAcquireFenceFd;
                list->outbufAcquireFenceFd= -1;
                break;
            default:
                ALOGI("invalid display id:%zu", i);
                break;
        }

        if (!display || (!display->connected() && i != HWC_DISPLAY_PRIMARY)) {
            continue;
        }

        hwc_layer_1_t* hwlayer = NULL;
        for (size_t k=0; k<list->numHwLayers-1; k++) {
            hwlayer = &list->hwLayers[k];
            if (hwlayer->acquireFenceFd != -1) {
                sync_wait(hwlayer->acquireFenceFd, -1);
                close(hwlayer->acquireFenceFd);
                hwlayer->acquireFenceFd = -1;
            }
        }

        display->setRenderTarget(target, fenceFd);
        display->composeLayers();
        display->updateScreen();

        // set release fence here.
        for (size_t k=0; k<list->numHwLayers-1; k++) {
            hwlayer = &list->hwLayers[k];
            Layer* layer = display->getLayerByPriv(hwlayer);
            if (layer->type == LAYER_TYPE_DEVICE) {
                hwlayer->releaseFenceFd = layer->releaseFence;
                layer->releaseFence = -1;
            }
        }
    }

    return 0;
}

static void hwc_registerProcs(struct hwc_composer_device_1* dev,
            hwc_procs_t const* procs) {
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if(ctx) {
        ctx->m_callback = (hwc_procs_t*)procs;
    }

    DisplayManager::getInstance()->setCallback(ctx->mListener);
}

static int hwc_eventControl(struct hwc_composer_device_1* dev, int /*dpy*/, int event, int enabled)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if(event == HWC_EVENT_VSYNC) {
        Display* display = DisplayManager::getInstance()->getPhysicalDisplay(HWC_DISPLAY_PRIMARY);
        display->setVsyncEnabled(enabled);
    }

    return 0;
}

static int hwc_query(struct hwc_composer_device_1* dev,
        int what, int* value)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    Display* display = DisplayManager::getInstance()->getPhysicalDisplay(HWC_DISPLAY_PRIMARY);
    const DisplayConfig& config = display->getActiveConfig();

    switch (what) {
    case HWC_BACKGROUND_LAYER_SUPPORTED:
        // we don't support the background layer yet
        value[0] = 0;
        break;
    case HWC_VSYNC_PERIOD:
        // vsync period in nanosecond
        value[0] = config.mVsyncPeriod;
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
    if (!ctx || disp < 0 || disp >= HWC_NUM_PHYSICAL_DISPLAY_TYPES) {
        return 0;
    }

    int mode = POWER_ON;
    if (blank == 0) {
        mode = POWER_ON;
    }
    else {
        mode = POWER_OFF;
    }

    Display* display = DisplayManager::getInstance()->getPhysicalDisplay(disp);
    return display->setPowerMode(mode);
}

static int hwc_getDisplayConfigs(struct hwc_composer_device_1 *dev,
        int disp, uint32_t *configs, size_t *numConfigs)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (!ctx || disp < 0 || disp >= HWC_NUM_PHYSICAL_DISPLAY_TYPES) {
        return -EINVAL;
    }

    if (!configs || !numConfigs || *numConfigs == 0) {
        return 0;
    }

    Display* display = DisplayManager::getInstance()->getPhysicalDisplay(disp);
    if (display->connected()) {
        configs[0] = 0;
        *numConfigs = 1;
        return 0;
    }

    return -EINVAL;
}

static int hwc_getDisplayAttributes(struct hwc_composer_device_1 *dev,
        int disp, uint32_t /*config*/, const uint32_t *attributes, int32_t *values)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)dev;
    if (!ctx || disp < 0 || disp >= HWC_NUM_PHYSICAL_DISPLAY_TYPES) {
        return -EINVAL;
    }

    Display* display = DisplayManager::getInstance()->getPhysicalDisplay(disp);
    const DisplayConfig& config = display->getActiveConfig();
    for (int i = 0; attributes[i] != HWC_DISPLAY_NO_ATTRIBUTE; i++) {
        switch(attributes[i]) {
            case HWC_DISPLAY_VSYNC_PERIOD:
                values[i] = config.mVsyncPeriod;
                break;

            case HWC_DISPLAY_WIDTH:
                values[i] = config.mXres;
                break;

            case HWC_DISPLAY_HEIGHT:
                values[i] = config.mYres;
                break;

            case HWC_DISPLAY_DPI_X:
                if(display->type() == DISPLAY_LDB)
                    values[i] = config.mXdpi;
                else
                    values[i] = 0;
                break;

            case HWC_DISPLAY_DPI_Y:
                if(display->type() == DISPLAY_LDB)
                    values[i] = config.mYdpi;
                else
                    values[i] = 0;
                break;
            default:
                ALOGE("unknown display attribute %u", attributes[i]);
                continue;
        }
    }

    return 0;
}

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = -EINVAL;
    struct hwc_context_t *dev = NULL;
    if (strcmp(name, HWC_HARDWARE_COMPOSER)) {
        return status;
    }

    dev = (hwc_context_t*)malloc(sizeof(*dev));

    /* initialize our state here */
    memset(dev, 0, sizeof(*dev));
    dev->mListener = new DisplayListener(dev);

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

    *device = &dev->device.common;
    ALOGI("%s,%d", __FUNCTION__, __LINE__);
    return 0;
}

DisplayListener::DisplayListener(struct hwc_context_t* ctx)
{
    mCtx = ctx;
}

void DisplayListener::onVSync(int disp, nsecs_t timestamp)
{
    if (mCtx == NULL || mCtx->m_callback == NULL) {
        return;
    }

    mCtx->m_callback->vsync(mCtx->m_callback, disp, timestamp);
}

void DisplayListener::onHotplug(int disp, bool connected)
{
    if (mCtx == NULL || mCtx->m_callback == NULL) {
        return;
    }

    mCtx->m_callback->hotplug(mCtx->m_callback, disp, connected);
}

void DisplayListener::onRefresh(int /*disp*/)
{
    if (mCtx == NULL || mCtx->m_callback == NULL) {
        return;
    }

    mCtx->m_callback->invalidate(mCtx->m_callback);
}

