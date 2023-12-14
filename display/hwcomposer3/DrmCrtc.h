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
#include "DrmProperty.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class DrmCrtc {
public:
    static std::unique_ptr<DrmCrtc> create(::android::base::borrowed_fd drmFd, uint32_t crtcId,
                                           uint32_t crtcIndexInResourcesArray);
    ~DrmCrtc() {}

    uint32_t getId() const { return mId; }
    uint32_t getIndex() const { return mIndexInResourcesArray; }

    const DrmProperty& getActiveProperty() const { return mActive; }
    const DrmProperty& getModeProperty() const { return mMode; }
    const DrmProperty& getOutFenceProperty() const { return mOutFence; }
    const DrmProperty& getDisplayXferProperty() const { return mDisplayXfer; }

    bool setLowPowerDisplay(::android::base::borrowed_fd drmFd, DrmPower power) const;

private:
    DrmCrtc(uint32_t id, uint32_t index) : mId(id), mIndexInResourcesArray(index) {}

    friend class DrmPlane;
    friend class DrmConnector;

    const uint32_t mId = -1;
    const uint32_t mIndexInResourcesArray = -1;

    DrmProperty mActive;
    DrmProperty mMode;
    DrmProperty mOutFence;
    DrmProperty mDisplayXfer;

    static const auto& GetPropertiesMap() {
        static const auto* sMap = []() {
            return new DrmPropertyMemberMap<DrmCrtc>{
                    {"ACTIVE", &DrmCrtc::mActive},
                    {"MODE_ID", &DrmCrtc::mMode},
                    {"OUT_FENCE_PTR", &DrmCrtc::mOutFence},
                    {"DISPLAY_TRANSFER", &DrmCrtc::mDisplayXfer},
            };
        }();
        return *sMap;
    }
};

} // namespace aidl::android::hardware::graphics::composer3::impl
