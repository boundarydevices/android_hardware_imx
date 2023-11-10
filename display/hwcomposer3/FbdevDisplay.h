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
#include <cutils/native_handle.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "Common.h"
#include "DeviceComposer.h"
#include "DrmBuffer.h"
#include "DrmConnector.h"
#include "DrmDisplay.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class FbdevDisplay {
public:
    static std::unique_ptr<FbdevDisplay> create(uint32_t id, ::android::base::borrowed_fd devFd);

    uint32_t getId() const { return mId; }
    bool isConnected() const { return true; }

    std::tuple<HWC3::Error, ::android::base::unique_fd> present(
            ::android::base::borrowed_fd devFd, ::android::base::borrowed_fd inSyncFd,
            uint64_t addr);

    DrmHotplugChange checkAndHandleHotplug(::android::base::borrowed_fd devFd);

    bool setPowerMode(::android::base::borrowed_fd devFd, DrmPower power);

    int32_t getActiveConfigId() { return mActiveConfigId; }
    HalDisplayConfig& getActiveConfig() { return mActiveConfig; }
    std::shared_ptr<HalConfig> getDisplayConfigs() { return mConfigs; }

    bool updateDisplayConfigs();
    void placeholderDisplayConfigs();
    int getFramebufferInfo(uint32_t* width, uint32_t* height, uint32_t* format);

    void setAsPrimary(bool enable) { mIsPrimary = enable; }
    bool isPrimary() { return mIsPrimary; }

private:
    FbdevDisplay(uint32_t id, int devFd) : mId(id), mFbdevFd(devFd) {}

    bool onConnect(::android::base::borrowed_fd devFd);
    bool onDisconnect(::android::base::borrowed_fd devFd);

    int setDefaultDisplayMode();
    void updateActiveConfig(std::shared_ptr<HalConfig> configs);

    bool mIsPrimary = false;
    const uint32_t mId;
    const int mFbdevFd; // just a copy here, owned by FbdevClient

    // The last presented buffer / DRM framebuffer is cached until
    // the next present to avoid toggling the display on and off.
    DisplayBuffer mPreviousBuffers;
    DisplayBuffer mTempBuffers;

    uint32_t mBufferFormat;
    uint32_t mBytesPerPixel;
    uint32_t mStrideInBytes;
    int32_t mActiveConfigId = -1;
    int32_t mStartConfigId = 0;
    HalDisplayConfig mActiveConfig;
    std::shared_ptr<HalConfig> mConfigs = std::make_shared<HalConfig>();
};

} // namespace aidl::android::hardware::graphics::composer3::impl
