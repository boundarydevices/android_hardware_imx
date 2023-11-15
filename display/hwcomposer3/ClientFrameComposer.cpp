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

#include <cutils/properties.h>
#include <drm_fourcc.h>

#include "Common.h"
#include "Display.h"
#include "Drm.h"
#include "DrmConnector.h"
#include "Layer.h"

namespace aidl::android::hardware::graphics::composer3::impl {

template <typename T>
HWC3::Error checkClientFromSystem(std::string path, std::string filePrefix,
                                  std::map<uint32_t, std::unique_ptr<DeviceClient>>& clients,
                                  uint32_t* baseId, uint32_t idIncrement) {
    HWC3::Error ret = HWC3::Error::NoResources;
    struct dirent** dirEntry;
#define HWC_PATH_LENGTH 256
    char filePath[HWC_PATH_LENGTH];
    int count = -1;

    count = scandir(path.c_str(), &dirEntry, 0, alphasort);
    if (count < 0) {
        ALOGE("%s: cannot find any %s", __FUNCTION__, filePrefix.c_str());
        return HWC3::Error::NoResources;
    }
    for (int i = 0; i < count; i++) {
        if (strncmp(dirEntry[i]->d_name, filePrefix.c_str(), filePrefix.size())) {
            free(dirEntry[i]);
            continue;
        }
        memset(filePath, 0, sizeof(filePath));
        snprintf(filePath, HWC_PATH_LENGTH, "%s/%s", path.c_str(), dirEntry[i]->d_name);

        std::unique_ptr<T> client = std::make_unique<T>();
        HWC3::Error error = client->init(filePath, baseId);
        if (error != HWC3::Error::None) {
            ALOGE("%s: failed to initialize %s:%s", __FUNCTION__, filePrefix.c_str(), filePath);
        } else {
            ret = error;
            clients.emplace(*baseId, std::move(client));
            *baseId += idIncrement;
        }

        free(dirEntry[i]);
    }

    return ret;
}

HWC3::Error ClientFrameComposer::init() {
    DEBUG_LOG("%s", __FUNCTION__);

    HWC3::Error ret;
    uint32_t baseId = 0;
    ret = checkClientFromSystem<DrmClient>("/dev/dri", "card", mDeviceClients, &baseId, 0);
    if (ret != HWC3::Error::None) {
        ALOGE("%s: Cannot find any DRM client", __FUNCTION__);
    }
    ret = checkClientFromSystem<FbdevClient>("/dev/graphics", "fb", mDeviceClients, &baseId, 1);
    if (ret != HWC3::Error::None) {
        ALOGE("%s: Cannot find any FBDEV client", __FUNCTION__);
    }

    if (mDeviceClients.size() < 1) {
        ALOGE("%s: cannot find any display client", __FUNCTION__);
        return HWC3::Error::NoResources;
    }

    mG2dComposer = std::make_shared<DeviceComposer>();
    if (mG2dComposer->isValid()) {
    }

    mHdcpEnabled = IsHdcpUserEnabled();

    return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::registerOnHotplugCallback(const HotplugCallback& cb) {
    for (const auto& pair : mDeviceClients) {
        pair.second->registerOnHotplugCallback(cb);
    }

    return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::unregisterOnHotplugCallback() {
    for (const auto& pair : mDeviceClients) {
        pair.second->unregisterOnHotplugCallback();
    }

    return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::onDisplayCreate(Display* display) {
    const auto displayId = display->getId();
    DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

    auto [error, client] = getDeviceClient(displayId);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRIu64 " cannot find Drm Client", __FUNCTION__, displayId);
        return error;
    }

    // Ensure created.
    mDisplayBuffers.emplace(displayId, DisplayBuffer{});

    std::vector<DisplayCapability> caps;
    if (client->getDisplayCapability(displayId, caps) == HWC3::Error::None) {
        display->setCapability(caps);
    }

    std::optional<std::vector<uint8_t>> edid = client->getEdid(displayId);
    if (edid) {
        display->setEdid(*edid);
    }

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

HWC3::Error ClientFrameComposer::onDisplayLayerDestroy(Display* display, Layer* layer) {
    const auto displayId = display->getId();
    DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

    auto [error, client] = getDeviceClient(displayId);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRIu64 " cannot find Drm Client", __FUNCTION__, displayId);
        return error;
    }

    if (layer->getHdrMetadataState() == LAYER_HDR_METADATA_STATE_PROCESSED) {
        client->setHdrMetadata(displayId, NULL); // reset the HDR metadata state
    }

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

    auto [error, client] = getDeviceClient(displayId);
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

    auto [error, client] = getDeviceClient(displayId);
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
        const auto composeType = layer->getCompositionType();

        if (overlaySupported && !layerSkiped &&
            (composeType == Composition::DEVICE || composeType == Composition::CLIENT)) {
            common::Rect rectFrame = layer->getDisplayFrame();
            common::Rect rectSource = layer->getSourceCropInt();
            DEBUG_LOG("UI masked rect:left=%d, top=%d, right=%d, bottom=%d", uiMaskedRect.left,
                      uiMaskedRect.top, uiMaskedRect.right, uiMaskedRect.bottom);
            if (checkRectOverlap(uiMaskedRect, rectFrame) || !checkOverlayWorkaround(layer)) {
                mergeRect(uiMaskedRect, rectFrame);
            } else {
                auto handle = layer->waitAndGetBuffer();
                auto [error, planeId] = client->getPlaneForLayerBuffer(displayId, handle);
                if (error == HWC3::Error::None) {
                    auto [createError, drmBuffer] = client->create(handle, rectFrame, rectSource);
                    if (createError != HWC3::Error::None) {
                        ALOGE("%s: display:%" PRIu64 " failed to create client target drm buffer",
                              __FUNCTION__, displayId);
                        return HWC3::Error::NoResources;
                    }
                    displayBuffer.planeDrmBuffer[planeId] = std::move(drmBuffer);
                    mLayersForOverlay.push_back(layerId);

                    if (layer->getHdrMetadataState() == LAYER_HDR_METADATA_STATE_ADDED) {
                        client->setHdrMetadata(displayId, layer->getHdrMetadata());
                        layer->setHdrMetadataState(LAYER_HDR_METADATA_STATE_PROCESSED);
                    }

                    if (mHdcpEnabled) {
                        gralloc_handle_t buff = (gralloc_handle_t)layer->getBuffer().getBuffer();
                        if (buff && (buff->usage & USAGE_PROTECTED)) {
                            client->setSecureMode(displayId, planeId, true);
                        } else {
                            client->setSecureMode(displayId, planeId, false);
                        }
                    }

                    if (composeType != Composition::DEVICE)
                        outChanges->addLayerCompositionChange(displayId, layerId,
                                                              Composition::DEVICE);
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
        bool secure = false;
        auto it = std::find_if(mLayersForComposition.begin(), mLayersForComposition.end(),
                               [&](Layer* layer) {
                                   gralloc_handle_t buff =
                                           (gralloc_handle_t)layer->getBuffer().getBuffer();
                                   if (buff && (buff->usage & USAGE_PROTECTED))
                                       return true;
                                   else
                                       return false;
                               });
        if (it != mLayersForComposition.end()) { // found secure layer
            DEBUG_LOG("%s: found secure layer", __FUNCTION__);
            secure = true;
        }

        auto [error, renderTarget] = client->getComposerTarget(mG2dComposer, displayId, secure);
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

    auto [error, client] = getDeviceClient(displayId);
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

    auto [error, client] = getDeviceClient(displayId);
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

std::tuple<HWC3::Error, DeviceClient*> ClientFrameComposer::getDeviceClient(uint32_t displayId) {
    bool found = false;
    uint32_t candidate;
    for (const auto& pair : mDeviceClients) {
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

    DeviceClient* client = mDeviceClients[candidate].get();

    return std::make_tuple(HWC3::Error::None, client);
}

HWC3::Error ClientFrameComposer::setDisplayBrightness(Display* display, float brightness) {
    const auto displayId = display->getId();
    DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

    auto [error, client] = getDeviceClient(displayId);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRIu64 " cannot find Drm Client", __FUNCTION__, displayId);
        return error;
    }

    client->setBacklightBrightness(displayId, brightness);

    return HWC3::Error::None;
}

HWC3::Error ClientFrameComposer::getDisplayConnectionType(Display* display,
                                                          DisplayConnectionType* outType) {
    const auto displayId = display->getId();
    DEBUG_LOG("%s display:%" PRIu64, __FUNCTION__, displayId);

    auto [error, client] = getDeviceClient(displayId);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRIu64 " cannot find Drm Client", __FUNCTION__, displayId);
        return error;
    }

    client->getDisplayConnectionType(displayId, outType);

    return HWC3::Error::None;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
