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
        return HWC2_ERROR_BAD_PARAMETER;
    }

    Display* pDisplay = NULL;
    DisplayManager* displayManager = DisplayManager::getInstance();
    pDisplay = displayManager->getDisplay(display);
    if (pDisplay == NULL) {
        ALOGE("%s invalid display id:%" PRId64, __func__, display);
        return HWC2_ERROR_BAD_DISPLAY;
    }

    if (mode != HAL_COLOR_MODE_NATIVE) {
        return HWC2_ERROR_UNSUPPORTED;
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
    }

    DisplayManager::getInstance()->setCallback(ctx->mListener);
    if (descriptor == HWC2_CALLBACK_HOTPLUG && pointer != NULL) {
        ctx->mListener->onHotplug(HWC_DISPLAY_PRIMARY, true);
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

    struct hwc2_context_t *ctx = (struct hwc2_context_t*)device;
    if (ctx->checkHDMI && ctx->mHotplug != NULL && ctx->mVsync != NULL) {
        ctx->checkHDMI = false;
        Display* fb = displayManager->getPhysicalDisplay(HWC_DISPLAY_EXTERNAL);
        ctx->mListener->onHotplug(HWC_DISPLAY_EXTERNAL, fb->connected());
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

        default:
            ALOGE("unknown display attribute %u", attribute);
            break;
    }

    return HWC2_ERROR_NONE;
}

static int hwc2_get_color_modes(hwc2_device_t* /*device*/, hwc2_display_t /*display*/,
                               uint32_t* outNumModes, int32_t* outModes)
{
    if (outNumModes != NULL) {
        *outNumModes = 1;
    }

    if (outModes != NULL) {
        *outModes = HAL_COLOR_MODE_NATIVE;
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

    pDisplay->setConfig(width, height, format);
    *outDisplay = pDisplay->index();
    return HWC2_ERROR_NONE;
}

static int hwc2_destroy_layer(hwc2_device_t* device, hwc2_display_t display,
                             hwc2_layer_t layer)
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

void DisplayListener::onVSync(int disp, nsecs_t timestamp)
{
    if (mCtx == NULL || mCtx->mVsync == NULL) {
        return;
    }

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

