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

#include "DummyClient.h"

#include "BufferInfo.h"
#include "Common.h"
#include "DeviceComposer.h"
#include "Drm.h"

namespace aidl::android::hardware::graphics::composer3::impl {

DummyClient::~DummyClient() {
    if (mComposerTargets.size() > 0) {
        // free device composer target buffers when decontruct
        mG2dComposer->freeDeviceFrameBuffer(mComposerTargets);
        mComposerTargets.clear();
    }
}

HWC3::Error DummyClient::init(char* path, uint32_t* baseId) {
    DEBUG_LOG("%s", __FUNCTION__);

    HalDisplayConfig newConfig;
    memset(&newConfig, 0, sizeof(newConfig));
    newConfig.width = DUMMY_DISPLAY_WIDTH;
    newConfig.height = DUMMY_DISPLAY_HEIGHT;
    newConfig.dpiX = 160;
    newConfig.dpiY = 160;
    newConfig.refreshRateHz = 60;
    newConfig.blobId = 0;
    newConfig.modeWidth = DUMMY_DISPLAY_WIDTH;
    newConfig.modeHeight = DUMMY_DISPLAY_HEIGHT;

    mDisplayId = *baseId;
    mActiveConfigId = DUMMY_DISPLAY_ACTIVE_CONFIG_ID;

    mConfigs->emplace(mActiveConfigId, newConfig);

    uint32_t format = static_cast<uint32_t>(common::PixelFormat::RGBA_8888);
    ALOGI("Dummy Client used, only support one display\n"
          "Display Id   = %d \n"
          "configId     = %d \n"
          "xres         = %d px\n"
          "yres         = %d px\n"
          "format       = %d\n"
          "xdpi         = %d ppi\n"
          "ydpi         = %d ppi\n"
          "fps          = %d Hz\n"
          "mode.width   = %d px\n"
          "mode.height  = %d px\n",
          mDisplayId, mActiveConfigId, newConfig.width, newConfig.height, format, newConfig.dpiX,
          newConfig.dpiY, newConfig.refreshRateHz, newConfig.modeWidth, newConfig.modeHeight);

    DEBUG_LOG("%s: Successfully initialized.", __FUNCTION__);
    return HWC3::Error::None;
}

HWC3::Error DummyClient::getDisplayConfigs(std::vector<HalMultiConfigs>* configs) {
    DEBUG_LOG("%s", __FUNCTION__);

    configs->clear();
    configs->emplace_back(HalMultiConfigs{
            .displayId = mDisplayId,
            .activeConfigId = mActiveConfigId,
            .configs = mConfigs,
    });

    ALOGI("%s: only support 1 display in Dummy Client, get %zu configs", __FUNCTION__,
          mConfigs->size());
    return HWC3::Error::None;
}

std::tuple<HWC3::Error, std::shared_ptr<DrmBuffer>> DummyClient::create(
        const native_handle_t* handle, common::Rect displayFrame, common::Rect sourceCrop,
        BufferType type) {
    HandleInfo info;
    if (handle == nullptr || (getInfoFromHandle(handle, &info) != 0)) {
        ALOGE("%s: invalid native handle", __FUNCTION__);
        return std::make_tuple(HWC3::Error::BadParameter, nullptr);
    }

    auto buffer = std::shared_ptr<DrmBuffer>(new DrmBuffer(*this));
    buffer->mBufferAddress = info.phys;
    DEBUG_LOG("%s: get framebuffer address 0x%" PRIx64, __FUNCTION__, *buffer->mBufferAddress);

    return std::make_tuple(HWC3::Error::None, std::move(buffer));
}

HWC3::Error DummyClient::destroyDrmFramebuffer(DrmBuffer* buffer) {
    buffer->mBufferAddress = std::nullopt;

    return HWC3::Error::None;
}

std::tuple<HWC3::Error, ::android::base::unique_fd> DummyClient::flushToDisplay(
        int displayId, const DisplayBuffer& buffer, ::android::base::borrowed_fd inSyncFd) {
    return std::make_tuple(HWC3::Error::None, ::android::base::unique_fd());
}

std::tuple<HWC3::Error, buffer_handle_t> DummyClient::getComposerTarget(
        std::shared_ptr<DeviceComposer> composer, int displayId, bool secure) {
    if (mComposerTargets.size() > 0) {
        if (++mTargetIndex >= mMaxComposerTargetsPerDisplay) {
            mTargetIndex = 0;
        }
        DEBUG_LOG("%s: get pre-allocated %s buffer:%d", __FUNCTION__,
                  secure ? "secure" : "nonsecure", mTargetIndex);
        return std::make_tuple(HWC3::Error::None, mComposerTargets[mTargetIndex]);
    }

    uint32_t width = DUMMY_DISPLAY_WIDTH;
    uint32_t height = DUMMY_DISPLAY_HEIGHT;
    uint32_t format = static_cast<int>(common::PixelFormat::RGBA_8888);
    mComposerTargets.reserve(mMaxComposerTargetsPerDisplay);
    auto ret = composer->prepareDeviceFrameBuffer(width, height, format, mComposerTargets,
                                                  mMaxComposerTargetsPerDisplay, false);
    if (ret) {
        ALOGE("%s: create framebuffer failed", __FUNCTION__);
        return std::make_tuple(HWC3::Error::NoResources, nullptr);
    }

    mTargetIndex = 0;
    composer->freeSolidColorBuffer();
    mG2dComposer = std::move(composer); // destructor need device composer to free buffers

    return std::make_tuple(HWC3::Error::None, mComposerTargets[mTargetIndex]);
}

HWC3::Error DummyClient::getDisplayClientTargetProperty(int displayId,
                                                        ClientTargetProperty* outProperty) {
    outProperty->pixelFormat = common::PixelFormat::RGBA_8888;
    outProperty->dataspace = common::Dataspace::SRGB_LINEAR;

    return HWC3::Error::None;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
