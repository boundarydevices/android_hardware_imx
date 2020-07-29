/*
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

#include <inttypes.h>
#include <string>
#include <math.h>
#include <hardware/hardware.h>
#include <hardware/hwcomposer2.h>
#include <Memory.h>
#include <MemoryDesc.h>
#include <DisplayManager.h>
#include <sync/sync.h>
#include "context.h"

static Layer* hwc2_get_layer(hwc2_display_t display, hwc2_layer_t layer)
{
    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return NULL;
    }

    Layer* pLayer = pDisplay->getLayer(layer);
    if (pLayer == NULL) {
        ALOGE("%s invalid layer", __func__);
        return NULL;
    }

    return pLayer;
}

static int hwc2_set_layer_zorder(hwc2_device_t* device, hwc2_display_t display,
                                        hwc2_layer_t layer, uint32_t z)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Layer* pLayer = hwc2_get_layer(display, layer);
    if (pLayer == NULL) {
        ALOGE("%s get layer failed", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    pLayer->zorder = z;
    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_visible_region(hwc2_device_t* device, hwc2_display_t display,
                                        hwc2_layer_t layer, hwc_region_t visible)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Layer* pLayer = hwc2_get_layer(display, layer);
    if (pLayer == NULL) {
        ALOGE("%s get layer failed", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    pLayer->visibleRegion.clear();
    for (size_t n=0; n<visible.numRects; n++) {
        Rect rect;
        const hwc_rect_t &hrect = visible.rects[n];
        rect.left = hrect.left;
        rect.top = hrect.top;
        rect.right = hrect.right;
        rect.bottom = hrect.bottom;
        if (rect.isEmpty()) {
            continue;
        }
        pLayer->visibleRegion.orSelf(rect);
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_transform(hwc2_device_t* device, hwc2_display_t display,
                                        hwc2_layer_t layer, int32_t transform)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Layer* pLayer = hwc2_get_layer(display, layer);
    if (pLayer == NULL) {
        ALOGE("%s get layer failed", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    pLayer->transform = transform;

    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_source_crop(hwc2_device_t* device, hwc2_display_t display,
                                        hwc2_layer_t layer, hwc_frect_t crop)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Layer* pLayer = hwc2_get_layer(display, layer);
    if (pLayer == NULL) {
        ALOGE("%s get layer failed", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    pLayer->sourceCrop.left = (int)ceilf(crop.left);
    pLayer->sourceCrop.top = (int)ceilf(crop.top);
    pLayer->sourceCrop.right = (int)floorf(crop.right);
    pLayer->sourceCrop.bottom = (int)floorf(crop.bottom);
    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_sideband_stream(hwc2_device_t* device, hwc2_display_t display,
                                        hwc2_layer_t layer, const native_handle_t* stream)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Layer* pLayer = hwc2_get_layer(display, layer);
    if (pLayer == NULL) {
        ALOGE("%s get layer failed", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    pLayer->handle = (Memory*)stream;

    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_plane_alpha(hwc2_device_t* device, hwc2_display_t display,
                                        hwc2_layer_t layer, float alpha)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Layer* pLayer = hwc2_get_layer(display, layer);
    if (pLayer == NULL) {
        ALOGE("%s get layer failed", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    uint8_t alphas = alpha * 255;
    pLayer->planeAlpha = alphas;
    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_display_frame(hwc2_device_t* device, hwc2_display_t display,
                                        hwc2_layer_t layer, hwc_rect_t frame)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Layer* pLayer = hwc2_get_layer(display, layer);
    if (pLayer == NULL) {
        ALOGE("%s get layer failed", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Rect& rect = pLayer->displayFrame;
    rect.left = frame.left;
    rect.top = frame.top;
    rect.right = frame.right;
    rect.bottom = frame.bottom;
    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_dataspace(hwc2_device_t* /*device*/, hwc2_display_t /*display*/,
                                    hwc2_layer_t /*layer*/, int32_t /*dataspace*/)
{
    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_composition_type(hwc2_device_t* device, hwc2_display_t display,
                                           hwc2_layer_t layer, int32_t type)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Layer* pLayer = hwc2_get_layer(display, layer);
    if (pLayer == NULL) {
        ALOGE("%s get layer failed", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    pLayer->origType = type;
    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_color(hwc2_device_t* device, hwc2_display_t display,
                                hwc2_layer_t layer, hwc_color_t color)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Layer* pLayer = hwc2_get_layer(display, layer);
    if (pLayer == NULL) {
        ALOGE("%s get layer failed", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    int colors = color.a << 24 | color.b << 16 | color.g << 8 | color.r;
    pLayer->color = colors;

    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_blend_mode(hwc2_device_t* device, hwc2_display_t display,
                                     hwc2_layer_t layer, int32_t mode)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Layer* pLayer = hwc2_get_layer(display, layer);
    if (pLayer == NULL) {
        ALOGE("%s get layer failed", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    int blend = BLENDING_NONE;
    if (mode == HWC2_BLEND_MODE_PREMULTIPLIED) {
        blend = BLENDING_PREMULT;
    }
    else if (mode == HWC2_BLEND_MODE_COVERAGE) {
        blend = BLENDING_COVERAGE;
    }

    pLayer->blendMode = blend;
    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_surface_dmage(hwc2_device_t* /*device*/, hwc2_display_t /*display*/,
                                        hwc2_layer_t /*layer*/, hwc_region_t /*damage*/)
{
    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_buffer(hwc2_device_t* device, hwc2_display_t display,
                                 hwc2_layer_t layer, buffer_handle_t buffer,
                                 int32_t acquireFence)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Layer* pLayer = hwc2_get_layer(display, layer);
    if (pLayer == NULL) {
        ALOGE("%s get layer failed", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    if (pLayer->acquireFence != -1) {
        close(pLayer->acquireFence);
    }

    pLayer->handle = (Memory*)buffer;
    pLayer->acquireFence = acquireFence;

    return HWC2_ERROR_NONE;
}

static int hwc2_set_cursor_position(hwc2_device_t* /*device*/, hwc2_display_t /*display*/,
                                    hwc2_layer_t /*layer*/, int32_t /*x*/, int32_t /*y*/)
{
    return HWC2_ERROR_UNSUPPORTED;
}

static int hwc2_validate_display(hwc2_device_t* device, hwc2_display_t display,
                                 uint32_t* outNumTypes, uint32_t* outNumRequests)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }
    struct hwc2_context_t *ctx = (struct hwc2_context_t*)device;

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    pDisplay->setSkipLayer(ctx->color_tranform);

    pDisplay->verifyLayers();
    pDisplay->getRequests(NULL, outNumRequests, NULL, NULL);
    pDisplay->getChangedTypes(outNumTypes, NULL, NULL);

    return HWC2_ERROR_NONE;
}

static int hwc2_set_vsync_enable(hwc2_device_t* device, hwc2_display_t display,
                                 int32_t enabled)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    bool enable = (enabled == HWC2_VSYNC_ENABLE) ? true : false;
    int type = pDisplay->type();
    if (type >= DISPLAY_LDB && type < DISPLAY_VIRTUAL) {
        pDisplay->setVsyncEnabled(enable);
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_get_doze_support(hwc2_device_t* device, hwc2_display_t display,
                                 int32_t* outSupport);

static int hwc2_set_power_mode(hwc2_device_t* device, hwc2_display_t display,
                               int32_t mode)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    int power = POWER_ON;
    if (mode == HWC2_POWER_MODE_ON) {
        power = POWER_ON;
    }
    else if (mode == HWC2_POWER_MODE_OFF) {
        power = POWER_OFF;
    }
    else if (mode == HWC2_POWER_MODE_DOZE) {
        power = POWER_DOZE;
    }
    else if (mode == HWC2_POWER_MODE_DOZE_SUSPEND) {
        power = POWER_DOZE_SUSPEND;
    }
    else {
        ALOGE("%s invalid power mode:%d", __func__, mode);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    //Check DisplayCapability::Doze support
    int32_t isDozeSupport = 0;
    int status = hwc2_get_doze_support(device,display,&isDozeSupport);
    if (status != HWC2_ERROR_NONE) {
        ALOGE("%s failed to get doze support %d",__func__,status);
        return status;
    }
    if ((mode == HWC2_POWER_MODE_DOZE || mode == HWC2_POWER_MODE_DOZE_SUSPEND)
         && isDozeSupport == 0 ) {
        return HWC2_ERROR_UNSUPPORTED;
    }
    int type = pDisplay->type();
    if (type >= DISPLAY_LDB && type < DISPLAY_VIRTUAL) {
        pDisplay->setPowerMode(power);
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_set_output_buffer(hwc2_device_t* device, hwc2_display_t display,
                                  buffer_handle_t buffer, int32_t releaseFence)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if (pDisplay->type() != DISPLAY_VIRTUAL) {
        ALOGE("only virtual display accepts output buffer");
        if (releaseFence != -1) {
            close(releaseFence);
        }
        return HWC2_ERROR_UNSUPPORTED;
    }

    pDisplay->setRenderTarget((Memory*)buffer, releaseFence);

    return HWC2_ERROR_NONE;
}

static int hwc2_set_color_transform(hwc2_device_t* *device, hwc2_display_t /*display*/,
                                    const float* /*matrix*/, int32_t hint)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    struct hwc2_context_t *ctx = (struct hwc2_context_t*)device;

    if( hint == HAL_COLOR_TRANSFORM_IDENTITY )
        ctx->color_tranform = false;
    else
        ctx->color_tranform = true;
    return HWC2_ERROR_NONE;
}

static int hwc2_set_color_mode(hwc2_device_t* device, hwc2_display_t display,
                               int32_t mode)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if ((mode < HAL_COLOR_MODE_NATIVE) || (mode > HAL_COLOR_MODE_DISPLAY_P3)) {
        return HWC2_ERROR_BAD_PARAMETER;
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_set_client_target(hwc2_device_t* device, hwc2_display_t display,
                                  buffer_handle_t target, int32_t acquireFence,
                                  int32_t /*dataspace*/, hwc_region_t /*damage*/)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if (pDisplay->type() == DISPLAY_VIRTUAL) {
        ALOGE("only physical display accepts client target");
        if (acquireFence != -1) {
            sync_wait(acquireFence, -1);
            close(acquireFence);
        }
        return HWC2_ERROR_NONE;
    }

    pDisplay->setRenderTarget((Memory*)target, acquireFence);

    return HWC2_ERROR_NONE;
}

static int hwc2_set_active_config(hwc2_device_t* device, hwc2_display_t display,
                                  hwc2_config_t config)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    return pDisplay->setActiveConfig(config);
}

static int hwc2_register_callback(hwc2_device_t* device, int32_t descriptor,
                                  hwc2_callback_data_t callbackData, hwc2_function_pointer_t pointer)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    struct hwc2_context_t *ctx = (struct hwc2_context_t*)device;
    switch (descriptor) {
        case HWC2_CALLBACK_HOTPLUG:
            ctx->mHotplug = reinterpret_cast<HWC2_PFN_HOTPLUG>(pointer);
            ctx->mHotplugData = callbackData;
            break;
        case HWC2_CALLBACK_REFRESH:
            ctx->mRefresh = reinterpret_cast<HWC2_PFN_REFRESH>(pointer);
            ctx->mRefreshData = callbackData;
            break;
        case HWC2_CALLBACK_VSYNC:
            ctx->mVsync = reinterpret_cast<HWC2_PFN_VSYNC>(pointer);
            ctx->mVsyncData = callbackData;
            break;
        case HWC2_CALLBACK_VSYNC_2_4:
            ctx->mVsync_2_4 = reinterpret_cast<HWC2_PFN_VSYNC_2_4>(pointer);
            ctx->mVsyncData_2_4 = callbackData;
            break;
        case HWC2_CALLBACK_VSYNC_PERIOD_TIMING_CHANGED:
            ctx->mVsyncPeriodTimingChanged = reinterpret_cast<HWC2_PFN_VSYNC_PERIOD_TIMING_CHANGED>(pointer);
            ctx->mVsyncPeriodTimingChangedData = callbackData;
            break;
        case HWC2_CALLBACK_SEAMLESS_POSSIBLE:
            ctx->mSeamlessPossible = reinterpret_cast<HWC2_PFN_SEAMLESS_POSSIBLE>(pointer);
            ctx->mSeamlessPossibleData = callbackData;
            break;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    displayManager->setCallback(ctx->mListener);
    if (descriptor == HWC2_CALLBACK_HOTPLUG && pointer != NULL) {
        ctx->mListener->onHotplug(HWC_DISPLAY_PRIMARY, true);
        for (int i = 1; i < MAX_PHYSICAL_DISPLAY; i++) {
            pDisplay = displayManager->getDisplay(i);
            if (pDisplay->connected())
                ctx->mListener->onHotplug(i, true);
        }
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_present_display(hwc2_device_t* device, hwc2_display_t display,
                                int32_t* outPresentFence)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    pDisplay->composeLayers();
    pDisplay->updateScreen();
    if (outPresentFence != NULL) {
        pDisplay->getPresentFence(outPresentFence);
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_get_release_fences(hwc2_device_t* device, hwc2_display_t display,
                                   uint32_t* outNumElements,
                                   hwc2_layer_t* outLayers, int32_t* outFences)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    return pDisplay->getReleaseFences(outNumElements, outLayers, outFences);
}

static int hwc2_get_max_virtual_display_count(hwc2_device_t* device)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    return 0;
}

static int hwc2_get_hdr_capabilities(hwc2_device_t* device, hwc2_display_t display,
                                     uint32_t* outNumTypes, int32_t* outTypes,
                                     float* outMaxLuminance, float* outMaxAverageLuminance,
                                     float* outMinLuminance)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    HdrMetaData hdrMetaData;
    memset(&hdrMetaData, 0, sizeof(hdrMetaData));
    if (pDisplay->isHdrSupported() && (pDisplay->getHdrMetaData(&hdrMetaData) == 0) ) {
        if (outTypes == NULL) {
            if (outNumTypes != NULL)
                *outNumTypes = 1;
        }
        else {
            *outTypes = HAL_HDR_HDR10;
            if (outMaxLuminance != NULL)
                *outMaxLuminance = hdrMetaData.max_cll;
            if (outMaxAverageLuminance != NULL)
                *outMaxAverageLuminance = hdrMetaData.max_fall;
            if (outMinLuminance != NULL)
                *outMinLuminance = hdrMetaData.min_cll;
        }
    }
    else {
        if (outNumTypes != NULL)
            *outNumTypes = 0;
        if (outTypes != NULL)
            *outTypes = 0;
        if (outMaxLuminance != NULL)
            *outMaxLuminance = 0.0f;
        if (outMaxAverageLuminance != NULL)
            *outMaxAverageLuminance = 0.0f;
        if (outMinLuminance != NULL)
            *outMinLuminance = 0.0f;
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_get_doze_support(hwc2_device_t* device, hwc2_display_t display,
                                 int32_t* outSupport)
{
    if (!device || !outSupport) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    *outSupport = 0;
    return HWC2_ERROR_NONE;
}

static int hwc2_get_display_type(hwc2_device_t* device, hwc2_display_t display,
                                 int32_t* outType)
{
    if (!device || !outType) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    int type = pDisplay->type();
    if (type == DISPLAY_VIRTUAL) {
        *outType = HWC2_DISPLAY_TYPE_VIRTUAL;
    }
    else if (type >= DISPLAY_LDB && type < DISPLAY_VIRTUAL) {
        *outType = HWC2_DISPLAY_TYPE_PHYSICAL;
    }
    else {
        *outType = HWC2_DISPLAY_TYPE_INVALID;
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_get_display_requests(hwc2_device_t* device, hwc2_display_t display,
                                     int32_t* outDisplayRequests, uint32_t* outNumElements,
                                     hwc2_layer_t* outLayers, int32_t* outLayerRequests)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    return pDisplay->getRequests(outDisplayRequests, outNumElements, outLayers, outLayerRequests);
}

static int hwc2_get_display_name(hwc2_device_t* device, hwc2_display_t display,
                                 uint32_t* outSize, char* outName)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    std::string name;
    name = "fsl display";
    if (outSize != NULL) {
        *outSize = 32;
    }

    if (outName != NULL) {
        strncpy(outName, name.c_str(), name.size());
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_get_display_configs(hwc2_device_t* device, hwc2_display_t display,
                                    uint32_t* outNumConfigs, hwc2_config_t* outConfigs)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    int numConfigs = pDisplay->getConfigNum();
    if (outNumConfigs != NULL) {
        *outNumConfigs = numConfigs;
    }

    if (outConfigs != NULL) {
        for (int i=0; i<numConfigs; i++) {
            outConfigs[i] = i;
        }
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_get_display_attribute(hwc2_device_t* device, hwc2_display_t display,
                                      hwc2_config_t hwconfig, int32_t attribute, int32_t* outValue)
{
    if (!device || !outValue) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    const DisplayConfig& config = pDisplay->getConfig(hwconfig);
    switch(attribute) {
        case HWC2_ATTRIBUTE_VSYNC_PERIOD:
            *outValue = config.mVsyncPeriod;
            break;

        case HWC2_ATTRIBUTE_WIDTH:
            *outValue = config.mXres;
            break;

        case HWC2_ATTRIBUTE_HEIGHT:
            *outValue = config.mYres;
            break;

        case HWC2_ATTRIBUTE_DPI_X:
            if(pDisplay->type() == DISPLAY_LDB)
                *outValue = config.mXdpi;
            else
                *outValue = -1;
            break;

        case HWC2_ATTRIBUTE_DPI_Y:
            if(pDisplay->type() == DISPLAY_LDB)
                *outValue = config.mYdpi;
            else
                *outValue = -1;
            break;

        case HWC2_ATTRIBUTE_CONFIG_GROUP:
            *outValue = config.cfgGroupId;
            break;

        default:
            ALOGE("unknown display attribute %u", attribute);
            break;
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_get_color_modes(hwc2_device_t* device, hwc2_display_t display,
                               uint32_t* outNumModes, int32_t* outModes)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if (outNumModes == NULL) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    if (outModes == NULL) {
        *outNumModes = 1;
    } else {
        *outNumModes = 1;
        outModes[0] = HAL_COLOR_MODE_NATIVE;
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_get_client_target_support(hwc2_device_t* device,
                                          hwc2_display_t display,
                                          uint32_t width, uint32_t height,
                                          int32_t format, int32_t dataspace)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    const DisplayConfig& config = pDisplay->getActiveConfig();
    if (config.mXres == (int)width && config.mYres == (int)height &&
        format == HAL_PIXEL_FORMAT_RGBA_8888 &&
        dataspace == HAL_DATASPACE_UNKNOWN) {
        return HWC2_ERROR_NONE;
    }

    return HWC2_ERROR_UNSUPPORTED;
}

static int hwc2_get_changed_composition_types(hwc2_device_t* device,
                                              hwc2_display_t display,
                                              uint32_t* outNumElements,
                                              hwc2_layer_t* outLayers,
                                              int32_t* outTypes)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    return pDisplay->getChangedTypes(outNumElements, outLayers, outTypes);
}

static int hwc2_get_active_config(hwc2_device_t* device,
                                  hwc2_display_t display,
                                  hwc2_config_t* outConfig)
{
    if (!device || !outConfig) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    *outConfig = pDisplay->getActiveId();
    return HWC2_ERROR_NONE;
}

static int hwc2_destroy_virtual_display(hwc2_device_t* device,
                                       hwc2_display_t display)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    DisplayManager* displayManager = DisplayManager::getInstance();
    int ret = displayManager->destroyVirtualDisplay(display);
    if (ret != 0) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_create_virtual_display(hwc2_device_t* device, uint32_t width,
                                       uint32_t height, int32_t* format,
                                       hwc2_display_t* outDisplay)
{
    if (!device || !outDisplay) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->createVirtualDisplay();
    if (pDisplay == NULL) {
        ALOGE("%s create virtual display failed", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    pDisplay->createDisplayConfig(width, height, *format);
    *outDisplay = pDisplay->index();
    return HWC2_ERROR_NONE;
}

static int hwc2_destroy_layer(hwc2_device_t* device, hwc2_display_t display,
                             hwc2_layer_t layer)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    Layer* pLayer = hwc2_get_layer(display, layer);
    if (pLayer == NULL) {
        ALOGE("%s get layer failed", __func__);
        return HWC2_ERROR_BAD_LAYER;
    }

    pDisplay->releaseLayer(layer);
    return HWC2_ERROR_NONE;
}

static int hwc2_create_layer(hwc2_device_t* device, hwc2_display_t display,
                             hwc2_layer_t* outLayer)
{
    if (!device || !outLayer) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    Layer* layer = pDisplay->getFreeLayer();
    if (layer == NULL) {
        ALOGE("%s get free layer failed", __func__);
        return HWC2_ERROR_NO_RESOURCES;
    }

    *outLayer = (hwc2_layer_t)layer->index;

    return HWC2_ERROR_NONE;
}

static int hwc2_accept_display_changes(hwc2_device_t* device, hwc2_display_t display)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    return HWC2_ERROR_NONE;
}

static void hwc2_dump(struct hwc2_device* /*device*/,
                      uint32_t* outSize, char* /*outBuffer*/)
{
    if (outSize != NULL) {
        *outSize = 0;
    }
}

static int hwc2_get_display_brightness_support(hwc2_device_t* device, hwc2_display_t display,
                                               bool* outSupport)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    *outSupport = (pDisplay->getMaxBrightness() != -1);

    return HWC2_ERROR_NONE;
}

static int hwc2_set_display_brightness(hwc2_device_t* device, hwc2_display_t display,
                                       float brightness)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    bool isBrightnessSupport = false;
    int status = hwc2_get_display_brightness_support(device,display,&isBrightnessSupport);
    if (status != HWC2_ERROR_NONE) {
        ALOGE("%s failed to get brightness support %d",__func__,status);
        return status;
    }
    if (!isBrightnessSupport) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    if (brightness == -1.0f) {
        brightness = 0.0f;
    }
    else if (brightness < 0.0f || brightness >1.0f) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    else if (pDisplay->setBrightness(brightness) != HWC2_ERROR_NONE) {
        return HWC2_ERROR_NO_RESOURCES;
    }
    return HWC2_ERROR_NONE;
}

static int hwc2_get_display_capabilities(hwc2_device_t* device, hwc2_display_t display,
                                         uint32_t* outNumCapabilities,uint32_t* outCapabilities)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    //Check DisplayCapability::SkipClientColorTransform support
    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    bool isDeviceComose = pDisplay->isDeviceComposition();
    bool isLowLatencyModeSupport = pDisplay->isLowLatencyModeSupport();

    //Check DisplayCapability::Doze support
    int32_t isDozeSupport = 0;
    int status = hwc2_get_doze_support(device,display,&isDozeSupport);
    if (status != HWC2_ERROR_NONE) {
        ALOGE("%s failed to get doze support %d",__func__,status);
        return status;
    }

    //Check DisplayCapability::Brightness support
    bool isBrightnessSupport = false;
    status = hwc2_get_display_brightness_support(device,display,&isBrightnessSupport);
    if (status != HWC2_ERROR_NONE) {
        ALOGE("%s failed to get brightness support %d",__func__,status);
        return status;
    }


    int numCapabilities = (isDeviceComose ? 1 : 0) + isDozeSupport
                            + (isBrightnessSupport ? 1 : 0) + (isLowLatencyModeSupport ? 1 : 0);

    if (outNumCapabilities == NULL) {
        return HWC2_ERROR_BAD_PARAMETER;
    }

    if (outCapabilities == NULL) {
        *outNumCapabilities = numCapabilities;
        return HWC2_ERROR_NONE;
    }

    int i = 0;
    if (*outNumCapabilities >= numCapabilities) {
        if (isDeviceComose) {
            outCapabilities[i++] = HWC2_DISPLAY_CAPABILITY_SKIP_CLIENT_COLOR_TRANSFORM;
        }
        if (isDozeSupport == 1) {
            outCapabilities[i++] = HWC2_DISPLAY_CAPABILITY_DOZE;
        }
        if (isBrightnessSupport) {
            outCapabilities[i++] = HWC2_DISPLAY_CAPABILITY_BRIGHTNESS;
        }
        if (isLowLatencyModeSupport) {
            outCapabilities[i++] = HWC2_DISPLAY_CAPABILITY_AUTO_LOW_LATENCY_MODE;
        }
    }
    *outNumCapabilities = numCapabilities;

    return HWC2_ERROR_NONE;
}

static int hwc2_get_display_identification_data(hwc2_device_t* device, hwc2_display_t display,
                                          uint8_t* outPort,uint32_t* outDataSize, uint8_t* outData)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if (!outData) {
        *outDataSize = EDID_LENGTH;
    } else {
        int len = pDisplay->getDisplayIdentificationData(outPort, outData, EDID_LENGTH);
        if (len > 0) {
            *outDataSize = len;
        } else {
            return HWC2_ERROR_UNSUPPORTED;
        }
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_function_get_render_intents(hwc2_device_t* device,hwc2_display_t display,
                                        int32_t mode,uint32_t* outNumIntents,
                                        int32_t* /*android_render_intent_v1_1_t*/ outIntents)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if ((mode < HAL_COLOR_MODE_NATIVE) || (mode > HAL_COLOR_MODE_DISPLAY_P3)) {
        return HWC2_ERROR_BAD_PARAMETER;
    }

    if (outNumIntents == NULL) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    if (outIntents == NULL) {
        *outNumIntents = 1;
    } else {
        *outNumIntents = 1;
        outIntents[0] = HAL_RENDER_INTENT_COLORIMETRIC;
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_function_set_color_mode_with_render_intent(hwc2_device_t* device, hwc2_display_t display,
                                                           int32_t /*android_color_mode_t*/ mode,
                                                           int32_t /*android_render_intent_v1_1_t */ intent)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if ((mode < HAL_COLOR_MODE_NATIVE) || (mode > HAL_COLOR_MODE_DISPLAY_P3)) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    if ((intent < HAL_RENDER_INTENT_COLORIMETRIC) || (intent > HAL_RENDER_INTENT_TONE_MAP_ENHANCE)) {
        return HWC2_ERROR_BAD_PARAMETER;
    }

    if ((mode != HAL_COLOR_MODE_NATIVE) || (intent != HAL_RENDER_INTENT_COLORIMETRIC)) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_get_per_frame_metadata_keys(hwc2_device_t* device, hwc2_display_t display,
                                            uint32_t* outNumKeys,int32_t* /*hwc2_per_frame_metadata_key_t*/ outKeys)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    uint32_t count = 0;
    int error = hwc2_get_hdr_capabilities(device,display,&count,NULL,NULL,NULL,NULL);
    if (error != HWC2_ERROR_NONE || count <1) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    if (outNumKeys == NULL) {
        return HWC2_ERROR_BAD_PARAMETER;
    }
    if (outKeys == NULL) {
        *outNumKeys = HWC2_MAX_FRAME_AVERAGE_LIGHT_LEVEL + 1;
    } else {
        *outNumKeys = HWC2_MAX_FRAME_AVERAGE_LIGHT_LEVEL + 1;
        outKeys[0] = HWC2_DISPLAY_RED_PRIMARY_X;
        outKeys[1] = HWC2_DISPLAY_RED_PRIMARY_Y;
        outKeys[2] = HWC2_DISPLAY_GREEN_PRIMARY_X;
        outKeys[3] = HWC2_DISPLAY_GREEN_PRIMARY_Y;
        outKeys[4] = HWC2_DISPLAY_BLUE_PRIMARY_X;
        outKeys[5] = HWC2_DISPLAY_BLUE_PRIMARY_Y;
        outKeys[6] = HWC2_WHITE_POINT_X;
        outKeys[7] = HWC2_WHITE_POINT_Y;
        outKeys[8] = HWC2_MAX_LUMINANCE;
        outKeys[9] = HWC2_MIN_LUMINANCE;
        outKeys[10] = HWC2_MAX_CONTENT_LIGHT_LEVEL;
        outKeys[11] = HWC2_MAX_FRAME_AVERAGE_LIGHT_LEVEL;
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_per_frame_metadata(hwc2_device_t* device, hwc2_display_t display,
                                             hwc2_layer_t layer,uint32_t numElements,
                                             const int32_t* /*hw2_per_frame_metadata_key_t*/ keys,
                                             const float* metadata)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    uint32_t count = 0;
    int error = hwc2_get_per_frame_metadata_keys(device,display,&count,NULL);
    if (error != HWC2_ERROR_NONE || count <1) {
        return HWC2_ERROR_UNSUPPORTED;
    }

    Layer* pLayer = hwc2_get_layer(display, layer);
    if (pLayer == NULL) {
        ALOGE("%s get layer failed", __func__);
        return HWC2_ERROR_BAD_PARAMETER;
    }

    pLayer->hdrMetadata.metadata_type = 0;
    pLayer->hdrMetadata.hdmi_metadata_type1.eotf = SMPTE_ST2084;
    pLayer->hdrMetadata.hdmi_metadata_type1.metadata_type = 1;
    for (uint32_t i = 0; i < numElements; i++) {
        switch (keys[i]) {
        case HWC2_DISPLAY_RED_PRIMARY_X:
            pLayer->hdrMetadata.hdmi_metadata_type1.display_primaries[0].x = (uint16_t)(metadata[i] * 50000);
            break;
        case HWC2_DISPLAY_RED_PRIMARY_Y:
            pLayer->hdrMetadata.hdmi_metadata_type1.display_primaries[0].y = (uint16_t)(metadata[i] * 50000);
            break;
        case HWC2_DISPLAY_GREEN_PRIMARY_X:
            pLayer->hdrMetadata.hdmi_metadata_type1.display_primaries[1].x = (uint16_t)(metadata[i] * 50000);
            break;
        case HWC2_DISPLAY_GREEN_PRIMARY_Y:
            pLayer->hdrMetadata.hdmi_metadata_type1.display_primaries[1].y = (uint16_t)(metadata[i] * 50000);
            break;
        case HWC2_DISPLAY_BLUE_PRIMARY_X:
            pLayer->hdrMetadata.hdmi_metadata_type1.display_primaries[2].x = (uint16_t)(metadata[i] * 50000);
            break;
        case HWC2_DISPLAY_BLUE_PRIMARY_Y:
            pLayer->hdrMetadata.hdmi_metadata_type1.display_primaries[2].y = (uint16_t)(metadata[i] * 50000);
            break;
        case HWC2_WHITE_POINT_X:
            pLayer->hdrMetadata.hdmi_metadata_type1.white_point.x = (uint16_t)(metadata[i] * 50000);
            break;
        case HWC2_WHITE_POINT_Y:
            pLayer->hdrMetadata.hdmi_metadata_type1.white_point.y = (uint16_t)(metadata[i] * 50000);
            break;
        case HWC2_MAX_LUMINANCE:
            pLayer->hdrMetadata.hdmi_metadata_type1.max_display_mastering_luminance = (uint16_t)(metadata[i]);
            break;
        case HWC2_MIN_LUMINANCE:
            pLayer->hdrMetadata.hdmi_metadata_type1.min_display_mastering_luminance = (uint16_t)(metadata[i] * 10000);
            break;
        case HWC2_MAX_CONTENT_LIGHT_LEVEL:
            pLayer->hdrMetadata.hdmi_metadata_type1.max_cll = (uint16_t)(metadata[i]);
            break;
        case HWC2_MAX_FRAME_AVERAGE_LIGHT_LEVEL:
            pLayer->hdrMetadata.hdmi_metadata_type1.max_fall = (uint16_t)(metadata[i]);
            break;
        }
    }
    return HWC2_ERROR_NONE;
}

static int hwc2_get_display_connection_type(hwc2_device_t* device, hwc2_display_t display,
                                            uint32_t* /*hwc2_display_connection_type_t*/ outType)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    *outType = (pDisplay->getDisplayConnectionType() == DISPLAY_LDB ? HWC2_DISPLAY_CONNECTION_TYPE_INTERNAL
                                                                    : HWC2_DISPLAY_CONNECTION_TYPE_EXTERNAL);

    return HWC2_ERROR_NONE;
}

static int hwc2_get_display_vsync_period(hwc2_device_t* device, hwc2_display_t display,
                                         hwc2_vsync_period_t* outVsyncPeriod)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    *outVsyncPeriod = pDisplay->getDisplayVsyncPeroid();

    return HWC2_ERROR_NONE;
}

static int hwc2_set_active_config_with_constraints(hwc2_device_t* device, hwc2_display_t display,
                                                   hwc2_config_t config,
                                                   hwc_vsync_period_change_constraints_t* vsyncPeriodChangeConstraints,
                                                   hwc_vsync_period_change_timeline_t* outTimeline)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if (config >= pDisplay->getConfigNum())
        return HWC2_ERROR_BAD_CONFIG;

    struct hwc2_context_t *ctx = (struct hwc2_context_t*)device;

    nsecs_t appliedTime;
    bool bRefresh;
    nsecs_t refreshTime;
    bool seamlessRequired = vsyncPeriodChangeConstraints->seamlessRequired;
    nsecs_t desiredTime = vsyncPeriodChangeConstraints->desiredTimeNanos;
    int group = pDisplay->getConfigGroup(config);
    int activeCfg = pDisplay->getActiveId();
    if (config == activeCfg) {
        // The same display config, no need to switch.
    } else if (pDisplay->getConfigGroup(activeCfg) == group) {
        // If the new config shares the same config group as the current config,
        // only the vsync period shall change.
        if (pDisplay->changeDisplayConfig(config, desiredTime, seamlessRequired,
                                          &appliedTime, &bRefresh, &refreshTime) != 0)
            return HWC2_ERROR_SEAMLESS_NOT_POSSIBLE;
        outTimeline->newVsyncAppliedTimeNanos = (int64_t)appliedTime;
        outTimeline->refreshRequired = bRefresh ? 1 : 0;
        outTimeline->refreshTimeNanos = (int64_t)refreshTime;
    } else if (seamlessRequired) {
        return HWC2_ERROR_SEAMLESS_NOT_ALLOWED;
    } else {
        pDisplay->changeDisplayConfig(config, desiredTime, false,
                                      &appliedTime, &bRefresh, &refreshTime);
        outTimeline->newVsyncAppliedTimeNanos = (int64_t)appliedTime;
        outTimeline->refreshRequired = bRefresh ? 1 : 0;
        outTimeline->refreshTimeNanos = (int64_t)refreshTime;
    }

    ctx->useVsync_2_4 = true;
    return HWC2_ERROR_NONE;

}

static int hwc2_set_auto_low_latency_mode(hwc2_device_t* device, hwc2_display_t display, bool on)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }
    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if (pDisplay->isLowLatencyModeSupport()) {
        pDisplay->setAutoLowLatencyMode(on);
        return HWC2_ERROR_NONE;
    } else {
        return HWC2_ERROR_UNSUPPORTED;
    }
}

static int hwc2_get_supported_content_types(hwc2_device_t* device, hwc2_display_t display,
                                            uint32_t* outNumSupportedContentTypes, uint32_t* outSupportedContentTypes)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if (outNumSupportedContentTypes == NULL)
        return HWC2_ERROR_BAD_PARAMETER;
    else if (outSupportedContentTypes == NULL)
        *outNumSupportedContentTypes = HWC2_CONTENT_TYPE_GAME;
    else
        *outNumSupportedContentTypes = pDisplay->getSupportedContentTypes(HWC2_CONTENT_TYPE_GAME, outSupportedContentTypes);

    return HWC2_ERROR_NONE;
}

static int hwc2_set_content_type(hwc2_device_t* device, hwc2_display_t display,
                                 int32_t /* hwc2_content_type_t */ contentType)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if ((contentType != HWC2_CONTENT_TYPE_GRAPHICS) && (contentType != HWC2_CONTENT_TYPE_PHOTO)
         && (contentType != HWC2_CONTENT_TYPE_CINEMA) && (contentType != HWC2_CONTENT_TYPE_GAME)
         && (contentType != HWC2_CONTENT_TYPE_NONE)) {
        return HWC2_ERROR_BAD_PARAMETER;
    }

    pDisplay->setContentType(contentType);

    return HWC2_ERROR_NONE;
}

static int hwc2_get_client_target_property(hwc2_device_t* device, hwc2_display_t display,
                                           hwc_client_target_property_t* outClientTargetProperty)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    outClientTargetProperty->pixelFormat = HAL_PIXEL_FORMAT_RGBA_8888;
    outClientTargetProperty->dataspace = HAL_DATASPACE_UNKNOWN;

    return HWC2_ERROR_NONE;
}

static int hwc2_set_layer_generic_metadata(hwc2_device_t* device, hwc2_display_t display,
                                           hwc2_layer_t /*layer*/, uint32_t /*keyLength*/, const char* /*key*/,
                                           bool /*mandatory*/, uint32_t /*valueLength*/, const uint8_t* /*value*/)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    return HWC2_ERROR_UNSUPPORTED;
}

static int hwc2_get_layer_generic_metadata_key(hwc2_device_t* device, uint32_t /*keyIndex*/,
        uint32_t* outKeyLength, char* /*outKey*/, bool* /*outMandatory*/)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    DisplayManager* displayManager = DisplayManager::getInstance();
    if (displayManager == NULL) {
        ALOGE("%s invalid display manager" PRId64, __func__);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    *outKeyLength = 0;

    return HWC2_ERROR_UNSUPPORTED;
}

static hwc2_function_pointer_t hwc_get_function(struct hwc2_device* device,
                                                int32_t descriptor)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return NULL;
    }

    hwc2_function_pointer_t func = NULL;
    switch (descriptor) {
        case HWC2_FUNCTION_ACCEPT_DISPLAY_CHANGES:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_accept_display_changes);
            break;
        case HWC2_FUNCTION_CREATE_LAYER:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_create_layer);
            break;
        case HWC2_FUNCTION_CREATE_VIRTUAL_DISPLAY:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_create_virtual_display);
            break;
        case HWC2_FUNCTION_DESTROY_LAYER:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_destroy_layer);
            break;
        case HWC2_FUNCTION_DESTROY_VIRTUAL_DISPLAY:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_destroy_virtual_display);
            break;
        case HWC2_FUNCTION_GET_ACTIVE_CONFIG:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_active_config);
            break;
        case HWC2_FUNCTION_GET_CHANGED_COMPOSITION_TYPES:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_changed_composition_types);
            break;
        case HWC2_FUNCTION_GET_CLIENT_TARGET_SUPPORT:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_client_target_support);
            break;
        case HWC2_FUNCTION_GET_COLOR_MODES:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_color_modes);
            break;
        case HWC2_FUNCTION_GET_DISPLAY_ATTRIBUTE:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_display_attribute);
            break;
        case HWC2_FUNCTION_GET_DISPLAY_CONFIGS:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_display_configs);
            break;
        case HWC2_FUNCTION_GET_DISPLAY_NAME:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_display_name);
            break;
        case HWC2_FUNCTION_GET_DISPLAY_REQUESTS:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_display_requests);
            break;
        case HWC2_FUNCTION_GET_DISPLAY_TYPE:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_display_type);
            break;
        case HWC2_FUNCTION_GET_DOZE_SUPPORT:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_doze_support);
            break;
        case HWC2_FUNCTION_GET_HDR_CAPABILITIES:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_hdr_capabilities);
            break;
        case HWC2_FUNCTION_GET_MAX_VIRTUAL_DISPLAY_COUNT:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_max_virtual_display_count);
            break;
        case HWC2_FUNCTION_GET_RELEASE_FENCES:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_release_fences);
            break;
        case HWC2_FUNCTION_PRESENT_DISPLAY:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_present_display);
            break;
        case HWC2_FUNCTION_REGISTER_CALLBACK:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_register_callback);
            break;
        case HWC2_FUNCTION_SET_ACTIVE_CONFIG:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_active_config);
            break;
        case HWC2_FUNCTION_SET_CLIENT_TARGET:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_client_target);
            break;
        case HWC2_FUNCTION_SET_COLOR_MODE:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_color_mode);
            break;
        case HWC2_FUNCTION_SET_COLOR_TRANSFORM:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_color_transform);
            break;
        case HWC2_FUNCTION_SET_CURSOR_POSITION:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_cursor_position);
            break;
        case HWC2_FUNCTION_SET_LAYER_BLEND_MODE:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_blend_mode);
            break;
        case HWC2_FUNCTION_SET_LAYER_BUFFER:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_buffer);
            break;
        case HWC2_FUNCTION_SET_LAYER_COLOR:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_color);
            break;
        case HWC2_FUNCTION_SET_LAYER_COMPOSITION_TYPE:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_composition_type);
            break;
        case HWC2_FUNCTION_SET_LAYER_DATASPACE:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_dataspace);
            break;
        case HWC2_FUNCTION_SET_LAYER_DISPLAY_FRAME:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_display_frame);
            break;
        case HWC2_FUNCTION_SET_LAYER_PLANE_ALPHA:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_plane_alpha);
            break;
        case HWC2_FUNCTION_SET_LAYER_SIDEBAND_STREAM:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_sideband_stream);
            break;
        case HWC2_FUNCTION_SET_LAYER_SOURCE_CROP:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_source_crop);
            break;
        case HWC2_FUNCTION_SET_LAYER_SURFACE_DAMAGE:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_surface_dmage);
            break;
        case HWC2_FUNCTION_SET_LAYER_TRANSFORM:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_transform);
            break;
        case HWC2_FUNCTION_SET_LAYER_VISIBLE_REGION:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_visible_region);
            break;
        case HWC2_FUNCTION_SET_LAYER_Z_ORDER:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_zorder);
            break;
        case HWC2_FUNCTION_SET_OUTPUT_BUFFER:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_output_buffer);
            break;
        case HWC2_FUNCTION_SET_POWER_MODE:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_power_mode);
            break;
        case HWC2_FUNCTION_SET_VSYNC_ENABLED:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_vsync_enable);
            break;
        case HWC2_FUNCTION_VALIDATE_DISPLAY:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_validate_display);
            break;
        case HWC2_FUNCTION_DUMP:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_dump);
            break;
        case HWC2_FUNCTION_GET_DISPLAY_CAPABILITIES:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_display_capabilities);
            break;
        case HWC2_FUNCTION_GET_DISPLAY_BRIGHTNESS_SUPPORT:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_display_brightness_support);
            break;
        case HWC2_FUNCTION_SET_DISPLAY_BRIGHTNESS:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_display_brightness);
            break;
        case HWC2_FUNCTION_GET_DISPLAY_IDENTIFICATION_DATA:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_display_identification_data);
            break;
        case HWC2_FUNCTION_GET_RENDER_INTENTS:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_function_get_render_intents);
            break;
        case HWC2_FUNCTION_SET_COLOR_MODE_WITH_RENDER_INTENT:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_function_set_color_mode_with_render_intent);
            break;
        case HWC2_FUNCTION_GET_PER_FRAME_METADATA_KEYS:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_per_frame_metadata_keys);
            break;
        case HWC2_FUNCTION_SET_LAYER_PER_FRAME_METADATA:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_per_frame_metadata);
            break;
        case HWC2_FUNCTION_GET_DISPLAY_CONNECTION_TYPE:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_display_connection_type);
            break;
        case HWC2_FUNCTION_GET_DISPLAY_VSYNC_PERIOD:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_display_vsync_period);
            break;
        case HWC2_FUNCTION_SET_ACTIVE_CONFIG_WITH_CONSTRAINTS:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_active_config_with_constraints);
            break;
        case HWC2_FUNCTION_SET_AUTO_LOW_LATENCY_MODE:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_auto_low_latency_mode);
            break;
        case HWC2_FUNCTION_GET_SUPPORTED_CONTENT_TYPES:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_supported_content_types);
            break;
        case HWC2_FUNCTION_SET_CONTENT_TYPE:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_content_type);
            break;
        case HWC2_FUNCTION_GET_CLIENT_TARGET_PROPERTY:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_client_target_property);
            break;
        case HWC2_FUNCTION_SET_LAYER_GENERIC_METADATA:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_set_layer_generic_metadata);
            break;
        case HWC2_FUNCTION_GET_LAYER_GENERIC_METADATA_KEY:
            func = reinterpret_cast<hwc2_function_pointer_t>(hwc2_get_layer_generic_metadata_key);
            break;
        default:
            func = NULL;
            break;
    }

    return func;
}

static void hwc_get_capabilities(struct hwc2_device* device, uint32_t* outCount,
                                int32_t* outCapabilities)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return;
    }

    if (outCapabilities) {
        if (outCount != NULL && *outCount >= 1) {
            outCapabilities[0] = HWC2_CAPABILITY_SIDEBAND_STREAM;
        }
    }
    else if (outCount) {
        *outCount = 1;
    }
}

static int hwc_device_close(struct hw_device_t* device)
{
    struct hwc_context_t* ctx = (struct hwc_context_t*)device;
    if (ctx) {
        free(ctx);
    }
    return 0;
}

static int hwc_device_open(const struct hw_module_t* module, const char* name,
        struct hw_device_t** device)
{
    int status = -EINVAL;
    struct hwc2_context_t *dev = NULL;
    if (strcmp(name, HWC_HARDWARE_COMPOSER)) {
        return status;
    }

    dev = (hwc2_context_t*)malloc(sizeof(*dev));

    /* initialize our state here */
    memset(dev, 0, sizeof(*dev));
    /* initialize the procs */
    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.module = const_cast<hw_module_t*>(module);
    dev->device.common.close = hwc_device_close;
    dev->device.common.version = HWC_DEVICE_API_VERSION_2_0;
    dev->device.getCapabilities = hwc_get_capabilities;
    dev->device.getFunction = hwc_get_function;

    dev->mListener = new DisplayListener(dev);
    dev->checkHDMI = true;
    dev->color_tranform = false;
    dev->useVsync_2_4 = false;

    *device = &dev->device.common;
    ALOGI("%s,%d", __FUNCTION__, __LINE__);
    return 0;
}

static struct hw_module_methods_t hwc_module_methods = {
    .open = hwc_device_open
};

hw_module_t HAL_MODULE_INFO_SYM = {
    .tag = HARDWARE_MODULE_TAG,
    .version_major = 3,
    .version_minor = 0,
    .id = HWC_HARDWARE_MODULE_ID,
    .name = "Freescale i.MX hwcomposer module",
    .author = "Freescale Semiconductor, Inc.",
    .methods = &hwc_module_methods,
    .dso = NULL,
    .reserved = {0}
};

DisplayListener::DisplayListener(struct hwc2_context_t* ctx)
{
    mCtx = ctx;
}

void DisplayListener::onVSync(int disp, nsecs_t timestamp, int vsyncPeriodNanos)
{
    if (mCtx == NULL || mCtx->mVsync == NULL) {
        return;
    }

    if ((mCtx->mVsync_2_4 != NULL) && mCtx->useVsync_2_4)
        mCtx->mVsync_2_4(mCtx->mVsyncData_2_4, disp, timestamp, vsyncPeriodNanos);
    else
        mCtx->mVsync(mCtx->mVsyncData, disp, timestamp);
}

void DisplayListener::onHotplug(int disp, bool connected)
{
    if (mCtx == NULL || mCtx->mHotplug == NULL) {
        return;
    }

    hwc2_connection_t connection = connected ? HWC2_CONNECTION_CONNECTED
                                             : HWC2_CONNECTION_DISCONNECTED;
    mCtx->mHotplug(mCtx->mHotplugData, disp, connection);
}

void DisplayListener::onRefresh(int disp)
{
    if (mCtx == NULL || mCtx->mRefresh == NULL) {
        return;
    }

    mCtx->mRefresh(mCtx->mRefreshData, disp);
}

void DisplayListener::onVSyncPeriodTimingChanged(int disp, nsecs_t newVsyncAppliedTimeNanos,
                                                 bool refreshRequired, nsecs_t refreshTimeNanos)
{
    if (mCtx == NULL || mCtx->mVsyncPeriodTimingChanged == NULL) {
        return;
    }

    hwc_vsync_period_change_timeline_t updated_timeline = {newVsyncAppliedTimeNanos, refreshRequired, refreshTimeNanos};
    mCtx->mVsyncPeriodTimingChanged(mCtx->mVsyncPeriodTimingChangedData, disp, &updated_timeline);
}

void DisplayListener::onSeamlessPossible(int disp)
{
    if (mCtx == NULL || mCtx->mSeamlessPossible == NULL) {
        return;
    }

    mCtx->mSeamlessPossible(mCtx->mSeamlessPossibleData, disp);
}
