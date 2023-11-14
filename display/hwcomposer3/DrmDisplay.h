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
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "Common.h"
#include "DeviceComposer.h"
#include "DrmAtomicRequest.h"
#include "DrmBuffer.h"
#include "DrmConnector.h"
#include "DrmCrtc.h"
#include "DrmPlane.h"

#define MAX_COMMIT_RETRY_COUNT 32

namespace aidl::android::hardware::graphics::composer3::impl {

enum class DrmHotplugChange {
    kNoChange,
    kConnected,
    kDisconnected,
};

struct DisplayBuffer {
    std::shared_ptr<DrmBuffer> clientTargetDrmBuffer;
    std::unordered_map<uint32_t, std::shared_ptr<DrmBuffer>> planeDrmBuffer;
};

class DrmDisplay {
public:
    static std::unique_ptr<DrmDisplay> create(
            uint32_t id, std::unique_ptr<DrmConnector> connector, std::unique_ptr<DrmCrtc> crtc,
            std::unordered_map<uint32_t, std::unique_ptr<DrmPlane>>& planes,
            ::android::base::borrowed_fd drmFd);

    uint32_t getId() const { return mId; }

    uint32_t getWidth() const { return mConnector->getWidth(); }
    uint32_t getHeight() const { return mConnector->getHeight(); }

    uint32_t getDpiX() const { return mConnector->getDpiX(); }
    uint32_t getDpiY() const { return mConnector->getDpiY(); }

    uint32_t getRefreshRateUint() const { return mConnector->getRefreshRateUint(); }

    bool isConnected() const { return mConnector->isConnected(); }

    std::optional<std::vector<uint8_t>> getEdid(::android::base::borrowed_fd drmFd) const {
        return mConnector->getEdid(drmFd);
    }

    std::tuple<HWC3::Error, std::unique_ptr<DrmAtomicRequest>> flushOverlay(
            uint32_t planeId, std::unique_ptr<DrmAtomicRequest> request,
            const std::shared_ptr<DrmBuffer>& buffer);

    std::tuple<HWC3::Error, std::unique_ptr<DrmAtomicRequest>> flushPrimary(
            uint32_t planeId, std::unique_ptr<DrmAtomicRequest> request,
            ::android::base::borrowed_fd inWaitSyncFd, const std::shared_ptr<DrmBuffer>& buffer);

    std::tuple<HWC3::Error, ::android::base::unique_fd> commit(
            std::unique_ptr<DrmAtomicRequest> request, ::android::base::borrowed_fd drmFd);

    DrmHotplugChange checkAndHandleHotplug(::android::base::borrowed_fd drmFd);

    bool setPowerMode(::android::base::borrowed_fd drmFd, DrmPower power);
    uint32_t getPlaneNum() const { return mPlanes.size(); }
    uint32_t findDrmPlane(const native_handle_t* handle);
    int32_t getActiveConfigId() { return mActiveConfigId; }
    HalDisplayConfig& getActiveConfig() { return mActiveConfig; }
    std::shared_ptr<HalConfig> getDisplayConfigs();
    uint32_t getPrimaryPlaneId();
    bool updateDisplayConfigs();
    void placeholderDisplayConfigs();
    void buildPlaneIdPool();
    int getFramebufferInfo(uint32_t* width, uint32_t* height, uint32_t* format);

    void setAsPrimary(bool enable) { mIsPrimary = enable; }
    bool isPrimary() { return mIsPrimary; }

    bool isSecureDisplay() const { return mConnector->getHDCPSupported(); }
    bool isSecureEnabled() const { return mConnector->isHDCPEnabled(); }
    bool setSecureMode(::android::base::borrowed_fd drmFd, bool secure);

    bool setHdrMetadataBlobId(uint32_t bolbId);

private:
    DrmDisplay(uint32_t id, std::unique_ptr<DrmConnector> connector, std::unique_ptr<DrmCrtc> crtc,
               std::unordered_map<uint32_t, std::unique_ptr<DrmPlane>> planes)
          : mId(id),
            mConnector(std::move(connector)),
            mCrtc(std::move(crtc)),
            mPlanes(std::move(planes)) {}

    bool onConnect(::android::base::borrowed_fd drmFd);

    bool onDisconnect(::android::base::borrowed_fd drmFd);

    void updateActiveConfig(std::shared_ptr<HalConfig> configs);

    bool mIsPrimary = false;
    const uint32_t mId;
    std::unique_ptr<DrmConnector> mConnector;
    std::unique_ptr<DrmCrtc> mCrtc;
    std::unordered_map<uint32_t, std::unique_ptr<DrmPlane>> mPlanes;

    // The last presented buffer / DRM framebuffer is cached until
    // the next present to avoid toggling the display on and off.
    DisplayBuffer mPreviousBuffers;
    DisplayBuffer mTempBuffers;

    int32_t mActiveConfigId = -1;
    int32_t mStartConfigId = 0;
    HalDisplayConfig mActiveConfig;
    std::shared_ptr<HalConfig> mConfigs = std::make_shared<HalConfig>();
    uint32_t mUiScaleType = UI_SCALE_NONE;
    std::vector<uint32_t> mPlaneIdPool;
    bool mModeSet = true;

    uint32_t mHdrMetadataBlobId = 0;
};

} // namespace aidl::android::hardware::graphics::composer3::impl
