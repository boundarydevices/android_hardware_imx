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

#include "ClientFrameComposer.h"

#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android/hardware/graphics/common/1.0/types.h>
#include <cutils/properties.h>
#include <drm_fourcc.h>
#include <libyuv.h>
#include <sync/sync.h>
#include <ui/GraphicBuffer.h>
#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>

#include "Common.h"
#include "Display.h"
#include "Drm.h"
#include "DrmConnector.h"
#include "Layer.h"

namespace aidl::android::hardware::graphics::composer3::impl {

HWC3::Error ClientFrameComposer::init() {
    DEBUG_LOG("%s", __FUNCTION__);

    HWC3::Error ret = HWC3::Error::NoResources;
#define HWC_PATH_LENGTH 256
    struct dirent** dirEntry;
    char dri[PROPERTY_VALUE_MAX];
    char path[HWC_PATH_LENGTH];
    int count = -1;
    uint32_t baseId = 0;

    property_get("vendor.hwc.drm.device", dri, "/dev/dri");
    count = scandir(dri, &dirEntry, 0, alphasort);
    if (count < 0) {
        ALOGE("%s: cannot find any DRM card", __FUNCTION__);
        return HWC3::Error::NoResources;
    }
    for (int i = 0; i < count; i++) {
        if (strncmp(dirEntry[i]->d_name, "card", 4)) {
            free(dirEntry[i]);
            continue;
        }
        memset(path, 0, sizeof(path));
        snprintf(path, HWC_PATH_LENGTH, "%s/%s", dri, dirEntry[i]->d_name);

        std::unique_ptr<DrmClient> client = std::make_unique<DrmClient>();
        HWC3::Error error = client->init(path, &baseId);
        if (error != HWC3::Error::None) {
            ALOGE("%s: failed to initialize DrmClient:%s", __FUNCTION__, path);
        } else {
            ret = error;
            mDrmClients.emplace(baseId, std::move(client));
        }

        free(dirEntry[i]);
    }

    mG2dComposer = std::make_unique<DeviceComposer>();
    if (mG2dComposer->isValid()) {
    }
    return ret;
}

HWC3::Error ClientFrameComposer::registerOnHotplugCallback(const HotplugCallback& cb) {
    for (const auto& pair : mDrmClients) {
        pair.second->registerOnHotplugCallback(cb);
    }

    return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::unregisterOnHotplugCallback() {
    for (const auto& pair : mDrmClients) {
        pair.second->unregisterOnHotplugCallback();
    }

    return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::onDisplayCreate(Display* display) {
    const auto displayId = display->getId();
    DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

    // Ensure created.
    mDisplayBuffers.emplace(displayId, DisplayBuffer{});

    return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::onDisplayDestroy(Display* display) {
    const auto displayId = display->getId();
    DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

    auto it = mDisplayBuffers.find(displayId);
    if (it == mDisplayBuffers.end()) {
        ALOGE("%s: display:%" PRIu64 " missing display buffers?", __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    mDisplayBuffers.erase(it);

    return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::onDisplayClientTargetSet(Display* display) {
    const auto displayId = display->getId();
    DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

    auto it = mDisplayBuffers.find(displayId);
    if (it == mDisplayBuffers.end()) {
        ALOGE("%s: display:%" PRIu64 " missing display buffers?", __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    auto [error, client] = getDrmClient(displayId);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRIu64 " cannot find Drm Client", __FUNCTION__, displayId);
        return error;
    }
    DisplayBuffer& displayBuffer = it->second;

    common::Rect displayFrame = {0};
    common::Rect sourceCrop = {0};
    auto [createError, drmBuffer] =
            client->create(display->getClientTarget().getBuffer(), displayFrame, sourceCrop);
    if (createError != HWC3::Error::None) {
        ALOGE("%s: display:%" PRIu64 " failed to create client target drm buffer", __FUNCTION__,
              displayId);
        return HWC3::Error::NoResources;
    }
    displayBuffer.clientTargetDrmBuffer = std::move(drmBuffer);

    return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::onActiveConfigChange(Display* /*display*/) {
    return HWC3::Error::None;
};

HWC3::Error ClientFrameComposer::validateDisplay(Display* display, DisplayChanges* outChanges) {
    const auto displayId = display->getId();
    DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

    auto it = mDisplayBuffers.find(displayId);
    if (it == mDisplayBuffers.end()) {
        ALOGE("%s: display:%" PRIu64 " missing display buffers?", __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    auto [error, client] = getDrmClient(displayId);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRIu64 " cannot find Drm Client", __FUNCTION__, displayId);
        return error;
    }
    client->prepareDrmPlanesForValidate(displayId);

    DisplayBuffer& displayBuffer = it->second;
    const std::vector<Layer*>& layers = display->getOrderedLayers();

    auto [_, overlaySupported] = client->isOverlaySupport(displayId);
    bool deviceComposition = true;
    bool mustDeviceComposition = false;

    mLayersForOverlay.clear();
    mLayersForComposition.clear();
    bool layerSkiped = false; // check if need overlay checking for layer or not
    int32_t activeConfigId;
    int32_t width = INT_MAX, height = INT_MAX;
    if (display->getActiveConfig(&activeConfigId) == HWC3::Error::None) {
        display->getDisplayAttribute(activeConfigId, DisplayAttribute::WIDTH, &width);
        display->getDisplayAttribute(activeConfigId, DisplayAttribute::HEIGHT, &height);
    }

    common::Rect uiMaskedRect = {width, height, 0, 0};
    for (Layer* layer : layers) {
        const auto layerId = layer->getId();
        const auto layerCompositionType = layer->getCompositionType();

        if (overlaySupported && !layerSkiped && (layerCompositionType == Composition::DEVICE)) {
            common::Rect rectFrame = layer->getDisplayFrame();
            DEBUG_LOG("UI masked rect:left=%d, top=%d, right=%d, bottom=%d", uiMaskedRect.left,
                      uiMaskedRect.top, uiMaskedRect.right, uiMaskedRect.bottom);
            if (checkRectOverlap(uiMaskedRect, rectFrame)) {
                mergeRect(uiMaskedRect, rectFrame);
            } else {
                auto handle = layer->waitAndGetBuffer();
                auto [error, planeId] = client->getPlaneForLayerBuffer(displayId, handle);
                if (error == HWC3::Error::None) {
                    auto [createError, drmBuffer] =
                            client->create(handle, rectFrame, layer->getSourceCropInt());
                    if (createError != HWC3::Error::None) {
                        ALOGE("%s: display:%" PRIu64 " failed to create client target drm buffer",
                              __FUNCTION__, displayId);
                        return HWC3::Error::NoResources;
                    }
                    displayBuffer.planeDrmBuffer[planeId] = std::move(drmBuffer);
                    mLayersForOverlay.push_back(layerId);
                    continue;
                } else {
                    mergeRect(uiMaskedRect, rectFrame);
                    if ((uiMaskedRect.right - uiMaskedRect.left >= width) &&
                        (uiMaskedRect.bottom - uiMaskedRect.top >= height))
                        layerSkiped = true;
                }
            }
        }

        mLayersForComposition.push_back(layer);
        if (mG2dComposer->isValid()) {
            // if some layer cannot support, not use device composition
            deviceComposition = deviceComposition && mG2dComposer->checkDeviceComposition(layer);
            mustDeviceComposition =
                    mustDeviceComposition || mG2dComposer->checkMustDeviceComposition(layer);
        }
    }

    if (mG2dComposer->isValid() && (mustDeviceComposition || deviceComposition)) {
        auto [error, renderTarget] = client->getComposerTarget(mG2dComposer.get(), displayId);
        if (error != HWC3::Error::None) {
            ALOGE("%s: display:%" PRIu64 " failed to get composer target", __FUNCTION__, displayId);
            return error;
        }
        mG2dComposer->composeLayers(mLayersForComposition, renderTarget);

        common::Rect displayFrame = {0}; // don't care it for framebuffer
        common::Rect sourceCrop = {0};   // don't care it for framebuffer
        auto [createError, drmBuffer] = client->create(renderTarget, displayFrame, sourceCrop);
        if (createError != HWC3::Error::None) {
            ALOGE("%s: display:%" PRIu64 " failed to create composer target drm buffer",
                  __FUNCTION__, displayId);
            return HWC3::Error::NoResources;
        }
        displayBuffer.clientTargetDrmBuffer = std::move(drmBuffer);
    } else {
        for (Layer* layer : mLayersForComposition) {
            const auto layerId = layer->getId();
            const auto layerCompositionType = layer->getCompositionType();

            if (layerCompositionType != Composition::CLIENT) {
                outChanges->addLayerCompositionChange(displayId, layerId, Composition::CLIENT);
                continue;
            }
        }
    }

    return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::presentDisplay(
        Display* display, ::android::base::unique_fd* outDisplayFence,
        std::unordered_map<int64_t, ::android::base::unique_fd>* outLayerFences) {
    const auto displayId = display->getId();
    DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

    auto displayBufferIt = mDisplayBuffers.find(displayId);
    if (displayBufferIt == mDisplayBuffers.end()) {
        ALOGE("%s: failed to find display buffers for display:%" PRIu64, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    auto [error, client] = getDrmClient(displayId);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRIu64 " cannot find Drm Client", __FUNCTION__, displayId);
        return error;
    }

    DisplayBuffer& displayBuffer = displayBufferIt->second;
    ::android::base::unique_fd fence = display->getClientTarget().getFence();

    auto [flushError, flushCompleteFence] = client->flushToDisplay(displayId, displayBuffer, fence);
    if (flushError != HWC3::Error::None) {
        ALOGE("%s: display:%" PRIu64 " failed to flush drm buffer" PRIu64, __FUNCTION__, displayId);
    }

    for (auto& id : mLayersForOverlay) {
        outLayerFences->emplace(id, ::android::base::unique_fd(dup(flushCompleteFence.get())));
    }
    *outDisplayFence = std::move(flushCompleteFence);

    displayBuffer.clientTargetDrmBuffer = nullptr;
    displayBuffer.planeDrmBuffer.clear();
    mLayersForOverlay.clear();

    return flushError;
}

HWC3::Error ClientFrameComposer::setPowerMode(Display* display, PowerMode mode) {
    const auto displayId = display->getId();
    DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

    auto [error, client] = getDrmClient(displayId);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRIu64 " cannot find Drm Client", __FUNCTION__, displayId);
        return error;
    }
    DrmPower power;
    switch (mode) {
        case PowerMode::OFF:
            power = DrmPower::kPowerOff;
            break;
        case PowerMode::ON:
            power = DrmPower::kPowerOn;
            break;
        case PowerMode::DOZE:
        case PowerMode::DOZE_SUSPEND:
        case PowerMode::ON_SUSPEND:
        default:
            power = DrmPower::kPowerOn;
            break;
    };

    auto err = client->setPowerMode(displayId, power);
    if (err != HWC3::Error::None) {
        ALOGE("%s: display:%" PRIu64 " failed to set power mode:%d" PRIu64, __FUNCTION__, displayId,
              mode);
    }

    return HWC3::Error::None;
}

std::tuple<HWC3::Error, DrmClient*> ClientFrameComposer::getDrmClient(uint32_t displayId) {
    bool found = false;
    uint32_t candidate;
    for (const auto& pair : mDrmClients) {
        if (pair.first <= displayId) {
            candidate = pair.first;
            found = true;
        }
        if (pair.first > displayId)
            break;
    }
    if (!found) {
        return std::make_tuple(HWC3::Error::BadDisplay, nullptr);
    }

    DrmClient* client = mDrmClients[candidate].get();

    return std::make_tuple(HWC3::Error::None, client);
}

} // namespace aidl::android::hardware::graphics::composer3::impl
