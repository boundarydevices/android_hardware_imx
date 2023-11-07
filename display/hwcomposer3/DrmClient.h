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
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <memory>
#include <tuple>
#include <vector>

#include "Common.h"
#include "DeviceComposer.h"
#include "Drm.h"
#include "DrmAtomicRequest.h"
#include "DrmBuffer.h"
#include "DrmConnector.h"
#include "DrmCrtc.h"
#include "DrmDisplay.h"
#include "DrmEventListener.h"
#include "DrmMode.h"
#include "DrmPlane.h"
#include "DrmProperty.h"
#include "LruCache.h"

#define MAX_COMPOSER_TARGETS_PER_DISPLAY 3

using android::RWLock;

namespace aidl::android::hardware::graphics::composer3::impl {

class DrmClient {
public:
    DrmClient() = default;
    ~DrmClient();

    DrmClient(const DrmClient&) = delete;
    DrmClient& operator=(const DrmClient&) = delete;

    DrmClient(DrmClient&&) = delete;
    DrmClient& operator=(DrmClient&&) = delete;

    HWC3::Error init(char* path, uint32_t* baseId);

    HWC3::Error getDisplayConfigs(std::vector<HalMultiConfigs>* configs) const;

    using HotplugCallback =
            std::function<void(bool /*connected*/, std::unique_ptr<HalMultiConfigs> /*configs*/)>;

    HWC3::Error registerOnHotplugCallback(const HotplugCallback& cb);
    HWC3::Error unregisterOnHotplugCallback();

    //    uint32_t refreshRate() const { return mDisplays[0]->getRefreshRateUint(); }

    std::tuple<HWC3::Error, std::shared_ptr<DrmBuffer>> create(const native_handle_t* handle,
                                                               common::Rect displayFrame,
                                                               common::Rect sourceCrop);

    std::tuple<HWC3::Error, ::android::base::unique_fd> flushToDisplay(
            int display, const DisplayBuffer& buffer, ::android::base::borrowed_fd inWaitSyncFd);

    std::optional<std::vector<uint8_t>> getEdid(uint32_t id);

    HWC3::Error setPowerMode(int displayId, DrmPower power);

    std::tuple<HWC3::Error, bool> isOverlaySupport(int displayId);

    HWC3::Error prepareDrmPlanesForValidate(int displayId);

    std::tuple<HWC3::Error, uint32_t> getPlaneForLayerBuffer(int displayId,
                                                             const native_handle_t* handle);

    uint32_t getDisplayBaseId() { return mDisplayBaseId; }

    HWC3::Error setPrimaryDisplay(int displayId);
    HWC3::Error fakeDisplayConfig(int displayId);

    std::tuple<HWC3::Error, buffer_handle_t> getComposerTarget(
            std::shared_ptr<DeviceComposer> composer, int displayId, bool secure);
    HWC3::Error setSecureMode(int displayId, uint32_t planeId, bool secure);

private:
    using DrmPrimeBufferHandle = uint32_t;
    using DrmBufferCache = LruCache<DrmPrimeBufferHandle, std::shared_ptr<DrmBuffer>>;
    std::unique_ptr<DrmBufferCache> mBufferCache;

    // Grant visibility to destroyDrmFramebuffer to DrmBuffer.
    friend class DrmBuffer;
    HWC3::Error destroyDrmFramebuffer(DrmBuffer* buffer);

    // Grant visibility for handleHotplug to DrmEventListener.
    bool handleHotplug();

    bool loadDrmDisplays(uint32_t displayBaseId);

    // Drm device.
    ::android::base::unique_fd mFd;

    mutable RWLock mDisplaysMutex;
    std::unordered_map<uint32_t, std::unique_ptr<DrmDisplay>> mDisplays; //<displayId, ptr>
    uint32_t mDisplayBaseId;
    std::unordered_map<uint32_t, std::vector<gralloc_handle_t>> mComposerTargets;
    std::unordered_map<uint32_t, int32_t> mTargetIndex; //<displayId, index>
    std::unordered_map<uint32_t, bool> mTargetSecurity; //<displayId, secure>

    std::unordered_map<uint32_t, int> mSecureMode;

    std::shared_ptr<DeviceComposer> mG2dComposer = nullptr;

    std::optional<HotplugCallback> mHotplugCallback;

    std::unique_ptr<DrmEventListener> mDrmEventListener;
};

} // namespace aidl::android::hardware::graphics::composer3::impl
