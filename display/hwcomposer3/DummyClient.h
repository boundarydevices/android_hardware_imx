/*
 * Copyright 2023-2024 NXP
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

#include <android-base/unique_fd.h>
#include <cutils/native_handle.h>

#include <memory>
#include <tuple>
#include <vector>

#include "Common.h"
#include "DeviceClient.h"
#include "DeviceComposer.h"
#include "Drm.h"
#include "DrmBuffer.h"
#include "DrmConnector.h"
#include "DrmDisplay.h"

#define DUMMY_DISPLAY_WIDTH 720
#define DUMMY_DISPLAY_HEIGHT 480
#define DUMMY_DISPLAY_ACTIVE_CONFIG_ID 0

namespace aidl::android::hardware::graphics::composer3::impl {

class DummyClient : public DeviceClient {
public:
    DummyClient() = default;
    ~DummyClient();

    DummyClient(const DummyClient&) = delete;
    DummyClient& operator=(const DummyClient&) = delete;

    DummyClient(DummyClient&&) = delete;
    DummyClient& operator=(DummyClient&&) = delete;

    HWC3::Error init(char* path, uint32_t* baseId) override;

    HWC3::Error getDisplayConfigs(std::vector<HalMultiConfigs>* configs) override;

    std::tuple<HWC3::Error, std::shared_ptr<DrmBuffer>> create(const native_handle_t* handle,
                                                               common::Rect displayFrame,
                                                               common::Rect sourceCrop,
                                                               BufferType type) override;
    HWC3::Error destroyDrmFramebuffer(DrmBuffer* buffer) override;

    std::tuple<HWC3::Error, ::android::base::unique_fd> flushToDisplay(
            int display, const DisplayBuffer& buffer,
            ::android::base::borrowed_fd inWaitSyncFd) override;

    HWC3::Error setPowerMode(int displayId, DrmPower power) override { return HWC3::Error::None; }
    HWC3::Error setPrimaryDisplay(int displayId) override { return HWC3::Error::None; }

    uint32_t getDisplayBaseId() override { return mDisplayId; }

    std::tuple<HWC3::Error, buffer_handle_t> getComposerTarget(
            std::shared_ptr<DeviceComposer> composer, int displayId, bool secure) override;
    HWC3::Error getDisplayClientTargetProperty(int displayId,
                                               ClientTargetProperty* outProperty) override;

private:
    uint32_t mDisplayId = 0;
    int32_t mActiveConfigId = -1;
    std::shared_ptr<HalConfig> mConfigs = std::make_shared<HalConfig>();

    std::vector<buffer_handle_t> mComposerTargets;
    int32_t mTargetIndex;

    std::shared_ptr<DeviceComposer> mG2dComposer = nullptr;
};

} // namespace aidl::android::hardware::graphics::composer3::impl
