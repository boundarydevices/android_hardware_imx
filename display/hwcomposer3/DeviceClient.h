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

#pragma once

#include <RWLock.h>
#include <android-base/unique_fd.h>
#include <cutils/native_handle.h>

#include <memory>
#include <tuple>
#include <vector>

#include "Common.h"
#include "DeviceComposer.h"
#include "Drm.h"
#include "DrmBuffer.h"
#include "DrmConnector.h"
#include "DrmDisplay.h"

#define MAX_COMPOSER_TARGETS_PER_DISPLAY 3

namespace aidl::android::hardware::graphics::composer3::impl {

class DeviceClient {
public:
    virtual ~DeviceClient() {}

    virtual HWC3::Error init(char* path, uint32_t* baseId) = 0;

    virtual HWC3::Error getDisplayConfigs(std::vector<HalMultiConfigs>* configs) = 0;

    using HotplugCallback =
            std::function<void(bool /*connected*/, std::unique_ptr<HalMultiConfigs> /*configs*/)>;

    virtual HWC3::Error registerOnHotplugCallback(const HotplugCallback& cb) = 0;
    virtual HWC3::Error unregisterOnHotplugCallback() = 0;

    virtual std::tuple<HWC3::Error, std::shared_ptr<DrmBuffer>> create(
            const native_handle_t* handle, common::Rect displayFrame, common::Rect sourceCrop) = 0;
    virtual HWC3::Error destroyDrmFramebuffer(DrmBuffer* buffer) = 0;

    virtual std::tuple<HWC3::Error, ::android::base::unique_fd> flushToDisplay(
            int display, const DisplayBuffer& buffer,
            ::android::base::borrowed_fd inWaitSyncFd) = 0;

    virtual std::optional<std::vector<uint8_t>> getEdid(uint32_t id) = 0;

    virtual HWC3::Error setPowerMode(int displayId, DrmPower power) = 0;

    virtual std::tuple<HWC3::Error, bool> isOverlaySupport(int displayId) = 0;
    virtual HWC3::Error checkOverlayLimitation(int displayId, Layer* layer) {
        return HWC3::Error::None;
    }

    virtual HWC3::Error prepareDrmPlanesForValidate(int displayId, uint32_t* uiPlaneBackup) = 0;

    virtual std::tuple<HWC3::Error, uint32_t> getPlaneForLayerBuffer(
            int displayId, const native_handle_t* handle) = 0;

    virtual uint32_t getDisplayBaseId() = 0;

    virtual HWC3::Error setPrimaryDisplay(int displayId) = 0;
    virtual HWC3::Error fakeDisplayConfig(int displayId) = 0;

    virtual std::tuple<HWC3::Error, buffer_handle_t> getComposerTarget(
            std::shared_ptr<DeviceComposer> composer, int displayId, bool secure) = 0;
    virtual HWC3::Error setSecureMode(int displayId, uint32_t planeId, bool secure) = 0;

    virtual HWC3::Error setBacklightBrightness(int displayId, float brightness) {
        return HWC3::Error::None;
    }
    virtual HWC3::Error getDisplayCapability(int displayId, std::vector<DisplayCapability>& caps) {
        return HWC3::Error::None;
    }
    virtual HWC3::Error setHdrMetadata(int displayId, hdr_output_metadata* metadata) {
        return HWC3::Error::None;
    }
    virtual HWC3::Error getDisplayConnectionType(int displayId, DisplayConnectionType* outType) {
        *outType = DisplayConnectionType::INTERNAL;
        return HWC3::Error::None;
    }
    virtual HWC3::Error getDisplayClientTargetProperty(int displayId,
                                                       ClientTargetProperty* outProperty) = 0;
};

} // namespace aidl::android::hardware::graphics::composer3::impl
