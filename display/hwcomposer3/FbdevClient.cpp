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

#include "FbdevClient.h"

#include <RWLock.h>
#include <gralloc_handle.h>
#include <linux/fb.h>
#include <linux/mxcfb.h>

#include "Common.h"
#include "Drm.h"

using android::RWLock;

namespace aidl::android::hardware::graphics::composer3::impl {

FbdevClient::~FbdevClient() {}

HWC3::Error FbdevClient::init(char* path, uint32_t* baseId) {
    DEBUG_LOG("%s", __FUNCTION__);

    mFd = ::android::base::unique_fd(open(path, O_RDWR | O_CLOEXEC));
    if (mFd < 0) {
        ALOGE("%s: failed to open fbdev device: %s", __FUNCTION__, strerror(errno));
        return HWC3::Error::NoResources;
    }

    uint32_t displayBaseId = *baseId;
    struct fb_fix_screeninfo finfo;
    if (ioctl(mFd, FBIOGET_FSCREENINFO, &finfo) == -1) {
        ALOGE("%s: FBIOGET_FSCREENINFO failed", __FUNCTION__);
        return HWC3::Error::NoResources;
    }
    if (!strcmp("mxc_epdc_fb", finfo.id)) {
        ALOGI("%s: Found EPDC Display panel!", __FUNCTION__);
    }

    {
        ::android::RWLock::AutoWLock lock(mDisplaysMutex);
        bool success = loadFbdevDisplays(displayBaseId);
        if (success) {
            DEBUG_LOG("%s: Successfully initialized FBDEV backend", __FUNCTION__);
        } else {
            ALOGE("%s: Failed to initialize FBDEV backend", __FUNCTION__);
            return HWC3::Error::NoResources;
        }
    }

    /* TODO: Add hotplug event listener if needed */

    mDisplayBaseId = displayBaseId;
    *baseId = displayBaseId;
    DEBUG_LOG("%s: Successfully initialized.", __FUNCTION__);
    return HWC3::Error::None;
}

HWC3::Error FbdevClient::getDisplayConfigs(std::vector<HalMultiConfigs>* configs) {
    DEBUG_LOG("%s", __FUNCTION__);

    ::android::RWLock::AutoRLock lock(mDisplaysMutex);

    configs->clear();

    for (const auto& pair : mDisplays) {
        FbdevDisplay* display = pair.second.get();
        if (!display->isConnected() && !display->isPrimary()) {
            continue;
        }

        configs->emplace_back(HalMultiConfigs{
                .displayId = display->getId(),
                .activeConfigId = display->getActiveConfigId(),
                .configs = display->getDisplayConfigs(),
        });
    }

    ALOGI("%s: %zu displays in Fbdev Client:fb%d, get %zu configs", __FUNCTION__, mDisplays.size(),
          mFd.get(), configs->size());
    if (configs->size() > 0)
        return HWC3::Error::None;
    else
        return HWC3::Error::NoResources;
}

HWC3::Error FbdevClient::registerOnHotplugCallback(const HotplugCallback& cb) {
    return HWC3::Error::None;
}

HWC3::Error FbdevClient::unregisterOnHotplugCallback() {
    return HWC3::Error::None;
}

bool FbdevClient::loadFbdevDisplays(uint32_t displayBaseId) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto display = FbdevDisplay::create(displayBaseId, mFd);
    if (!display) {
        return false;
    }
    display->updateDisplayConfigs();
    mDisplays.emplace(display->getId(), std::move(display));

    return true;
}

std::tuple<HWC3::Error, std::shared_ptr<DrmBuffer>> FbdevClient::create(
        const native_handle_t* handle, common::Rect displayFrame, common::Rect sourceCrop) {
    gralloc_handle_t memHandle = (gralloc_handle_t)handle;
    if (memHandle == nullptr) {
        ALOGE("%s: invalid gralloc_handle", __FUNCTION__);
        return std::make_tuple(HWC3::Error::NoResources, nullptr);
    }

    auto buffer = std::shared_ptr<DrmBuffer>(new DrmBuffer(*this));
    buffer->mBufferAddress = memHandle->phys;
    DEBUG_LOG("%s: get framebuffer address 0x%" PRIx64, __FUNCTION__, *buffer->mBufferAddress);

    return std::make_tuple(HWC3::Error::None, std::move(buffer));
}

HWC3::Error FbdevClient::destroyDrmFramebuffer(DrmBuffer* buffer) {
    buffer->mBufferAddress = std::nullopt;

    return HWC3::Error::None;
}

bool FbdevClient::handleHotplug() {
    DEBUG_LOG("%s", __FUNCTION__);

    return true;
}

std::tuple<HWC3::Error, ::android::base::unique_fd> FbdevClient::flushToDisplay(
        int displayId, const DisplayBuffer& buffer, ::android::base::borrowed_fd inSyncFd) {
    ATRACE_CALL();

    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return std::make_tuple(HWC3::Error::BadDisplay, ::android::base::unique_fd());
    }
    if (!mDisplays[displayId]->isConnected()) {
        ALOGI("%s: %d display is disconnected, avoid present framebuffer", __FUNCTION__, displayId);
        return std::make_tuple(HWC3::Error::None, ::android::base::unique_fd());
    }

    ::android::RWLock::AutoRLock lock(mDisplaysMutex);
    if (!buffer.clientTargetDrmBuffer->mBufferAddress) {
        return std::make_tuple(HWC3::Error::NoResources, ::android::base::unique_fd());
    }

    return mDisplays[displayId]->present(mFd, inSyncFd,
                                         *buffer.clientTargetDrmBuffer->mBufferAddress);
}

std::optional<std::vector<uint8_t>> FbdevClient::getEdid(uint32_t displayId) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return std::nullopt;
    }

    return std::nullopt;
}

HWC3::Error FbdevClient::setPowerMode(int displayId, DrmPower power) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    if (!mDisplays[displayId]->isConnected() && power == DrmPower::kPowerOn)
        return HWC3::Error::None;

    mDisplays[displayId]->setPowerMode(mFd, power);

    return HWC3::Error::None;
}

HWC3::Error FbdevClient::setPrimaryDisplay(int displayId) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    mDisplays[displayId]->setAsPrimary(true);

    return HWC3::Error::None;
}

HWC3::Error FbdevClient::fakeDisplayConfig(int displayId) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    mDisplays[displayId]->placeholderDisplayConfigs();

    return HWC3::Error::None;
}

std::tuple<HWC3::Error, buffer_handle_t> FbdevClient::getComposerTarget(
        std::shared_ptr<DeviceComposer> composer, int displayId, bool secure) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return std::make_tuple(HWC3::Error::BadDisplay, nullptr);
    }

    if (mComposerTargets.find(displayId) != mComposerTargets.end()) {
        int32_t index = mTargetIndex[displayId];
        if (++index >= MAX_COMPOSER_TARGETS_PER_DISPLAY) {
            index = 0;
        }
        mTargetIndex[displayId] = index;
        DEBUG_LOG("%s: get pre-allocated %s buffer:%d", __FUNCTION__,
                  secure ? "secure" : "nonsecure", index);
        return std::make_tuple(HWC3::Error::None, mComposerTargets[displayId][index]);
    }

    uint32_t width, height, format;
    gralloc_handle_t bufferHandles[MAX_COMPOSER_TARGETS_PER_DISPLAY];
    mDisplays[displayId]->getFramebufferInfo(&width, &height, &format);
    auto ret = composer->prepareDeviceFrameBuffer(width, height, format, bufferHandles,
                                                  MAX_COMPOSER_TARGETS_PER_DISPLAY, false);
    if (ret) {
        ALOGE("%s: create framebuffer failed", __FUNCTION__);
        return std::make_tuple(HWC3::Error::NoResources, nullptr);
    }

    std::vector<gralloc_handle_t> buffers;
    for (int i = 0; i < MAX_COMPOSER_TARGETS_PER_DISPLAY; i++) {
        buffers.push_back(bufferHandles[i]);
    }

    mComposerTargets.emplace(displayId, buffers);
    mTargetIndex.emplace(displayId, 0);

    composer->freeSolidColorBuffer();

    mG2dComposer = composer; // hotplug callback function need device composer to free buffers

    return std::make_tuple(HWC3::Error::None, mComposerTargets[displayId][0]);
}

HWC3::Error FbdevClient::setSecureMode(int displayId, uint32_t planeId, bool secure) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    return HWC3::Error::None;
}

HWC3::Error FbdevClient::getDisplayClientTargetProperty(int displayId,
                                                        ClientTargetProperty* outProperty) {
    if (mDisplays.find(displayId) == mDisplays.end()) {
        DEBUG_LOG("%s: invalid display:%" PRIu32, __FUNCTION__, displayId);
        return HWC3::Error::BadDisplay;
    }

    uint32_t width, height, format;
    mDisplays[displayId]->getFramebufferInfo(&width, &height, &format);

    outProperty->pixelFormat = (common::PixelFormat)format;
    outProperty->dataspace = common::Dataspace::SRGB_LINEAR;

    return HWC3::Error::None;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
