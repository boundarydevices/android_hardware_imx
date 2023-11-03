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

#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "Common.h"
#include "DrmCrtc.h"
#include "DrmMode.h"
#include "DrmProperty.h"

namespace aidl::android::hardware::graphics::composer3::impl {

enum class DrmPower {
    kPowerOff,
    kPowerOn,
};

// A "cable" to the display (HDMI, DisplayPort, etc).
class DrmConnector {
public:
    static std::unique_ptr<DrmConnector> create(::android::base::borrowed_fd drmFd,
                                                uint32_t connectorId);
    ~DrmConnector(){};

    uint32_t getId() const { return mId; }

    uint32_t getWidth() const;
    uint32_t getHeight() const;

    int32_t getDpiX() const;
    int32_t getDpiY() const;

    float getRefreshRate() const;
    uint32_t getRefreshRateUint() const { return (uint32_t)(getRefreshRate() + 0.5f); }

    bool isConnected() const { return mStatus == DRM_MODE_CONNECTED; }

    std::optional<std::vector<uint8_t>> getEdid() const { return mEdid; }

    const DrmProperty& getCrtcProperty() const { return mCrtc; }
    const DrmMode* getDefaultMode() const { return mModes[0].get(); }
    bool isCompatibleWith(const DrmCrtc& crtc) {
        return ((0x1 << crtc.mIndexInResourcesArray) & mPossibleCrtcsMask);
    }

    bool update(::android::base::borrowed_fd drmFd);

    bool setPowerMode(::android::base::borrowed_fd drmFd, DrmPower power) const;

    bool buildConfigs(std::shared_ptr<HalConfig> configs, uint32_t startConfigId);

private:
    DrmConnector(uint32_t id) : mId(id) {}

    bool loadEdid(::android::base::borrowed_fd drmFd);

    const uint32_t mId;
    uint32_t mPossibleCrtcsMask = 0; // get from encoder

    drmModeConnection mStatus = DRM_MODE_UNKNOWNCONNECTION;
    uint32_t mWidthMillimeters = 0;
    uint32_t mHeightMillimeters = 0;
    std::vector<std::unique_ptr<DrmMode>> mModes;

    DrmProperty mCrtc;
    DrmProperty mEdidProp;
    DrmProperty mDpms;
    DrmProperty mHdrMetadata;
    DrmProperty mProtection;
    std::optional<std::vector<uint8_t>> mEdid;

    static const auto& GetPropertiesMap() {
        static const auto* sMap = []() {
            return new DrmPropertyMemberMap<DrmConnector>{
                    {"CRTC_ID", &DrmConnector::mCrtc},
                    {"EDID", &DrmConnector::mEdidProp},
                    {"DPMS", &DrmConnector::mDpms},
                    {"HDR_OUTPUT_METADATA", &DrmConnector::mHdrMetadata},
                    {"Content Protection", &DrmConnector::mProtection},
            };
        }();
        return *sMap;
    }
};

} // namespace aidl::android::hardware::graphics::composer3::impl
