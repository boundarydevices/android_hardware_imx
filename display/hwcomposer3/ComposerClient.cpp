/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "ComposerClient.h"

#include <aidlcommonsupport/NativeHandle.h>
#include <android/binder_ibinder_platform.h>

#include "Common.h"
#include "Device.h"

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

#define GET_DISPLAY_OR_RETURN_ERROR()                                        \
    std::shared_ptr<Display> display = getDisplay(hwcId);                    \
    if (display == nullptr) {                                                \
        ALOGE("%s failed to get hwc display:%" PRIu64, __FUNCTION__, hwcId); \
        return ToBinderStatus(HWC3::Error::BadDisplay);                      \
    }

} // namespace

class ComposerClient::CommandResultWriter {
public:
    CommandResultWriter(std::vector<CommandResultPayload>* results)
          : mIndex(0), mResults(results) {}

    void nextCommand() { ++mIndex; }

    void addError(HWC3::Error error) {
        CommandError commandErrorResult;
        commandErrorResult.commandIndex = mIndex;
        commandErrorResult.errorCode = static_cast<int32_t>(error);
        mResults->emplace_back(std::move(commandErrorResult));
    }

    void addPresentFence(int64_t hwcId, ::android::base::unique_fd fence) {
        if (fence >= 0) {
            PresentFence presentFenceResult;
            presentFenceResult.display = hwcId;
            presentFenceResult.fence = ndk::ScopedFileDescriptor(fence.release());
            mResults->emplace_back(std::move(presentFenceResult));
        }
    }

    void addReleaseFences(int64_t hwcId,
                          std::unordered_map<int64_t, ::android::base::unique_fd> layerFences) {
        ReleaseFences releaseFencesResult;
        releaseFencesResult.display = hwcId;
        for (auto& [layer, layerFence] : layerFences) {
            if (layerFence >= 0) {
                ReleaseFences::Layer releaseFencesLayerResult;
                releaseFencesLayerResult.layer = layer;
                releaseFencesLayerResult.fence = ndk::ScopedFileDescriptor(layerFence.release());
                releaseFencesResult.layers.emplace_back(std::move(releaseFencesLayerResult));
            }
        }
        mResults->emplace_back(std::move(releaseFencesResult));
    }

    void addChanges(const DisplayChanges& changes) {
        if (changes.compositionChanges) {
            mResults->emplace_back(*changes.compositionChanges);
        }
        if (changes.displayRequestChanges) {
            mResults->emplace_back(*changes.displayRequestChanges);
        }
    }

    void addPresentOrValidateResult(int64_t hwcId, PresentOrValidate::Result pov) {
        PresentOrValidate result;
        result.display = hwcId;
        result.result = pov;
        mResults->emplace_back(std::move(result));
    }

    void addClientTargetProperty(int64_t hwcId, const ClientTargetProperty& clientTargetProperty,
                                 float brightness, const DimmingStage& dimmingStage) {
        ClientTargetPropertyWithBrightness clientTargetPropertyWithBrightness;
        clientTargetPropertyWithBrightness.display = hwcId;
        clientTargetPropertyWithBrightness.clientTargetProperty = clientTargetProperty;
        clientTargetPropertyWithBrightness.brightness = brightness;
        clientTargetPropertyWithBrightness.dimmingStage = dimmingStage;
        mResults->emplace_back(std::move(clientTargetPropertyWithBrightness));
    }

private:
    int32_t mIndex = 0;
    std::vector<CommandResultPayload>* mResults = nullptr;
};

ComposerClient::ComposerClient() {
    DEBUG_LOG("%s", __FUNCTION__);
}

ComposerClient::~ComposerClient() {
    DEBUG_LOG("%s", __FUNCTION__);

    std::lock_guard<std::mutex> lock(mDisplaysMutex);

    destroyDisplaysLocked();

    Device::getInstance().releaseComposer();
    if (mOnClientDestroyed) {
        mOnClientDestroyed();
    }
}

HWC3::Error ComposerClient::init() {
    DEBUG_LOG("%s", __FUNCTION__);

    HWC3::Error error = HWC3::Error::None;

    std::lock_guard<std::mutex> lock(mDisplaysMutex);

    mResources = std::make_unique<ComposerResources>();
    if (!mResources) {
        ALOGE("%s failed to allocate ComposerResources", __FUNCTION__);
        return HWC3::Error::NoResources;
    }

    error = mResources->init();
    if (error != HWC3::Error::None) {
        ALOGE("%s failed to initialize ComposerResources", __FUNCTION__);
        return error;
    }

    error = Device::getInstance().getComposer(&mComposer);
    if (error != HWC3::Error::None) {
        ALOGE("%s failed to get FrameComposer", __FUNCTION__);
        return error;
    }

    const auto HotplugCallback = [this](bool connected,
                                        std::unique_ptr<HalMultiConfigs> halConfigs) {
        handleHotplug(connected, std::move(halConfigs));
    };
    error = mComposer->registerOnHotplugCallback(HotplugCallback);
    if (error != HWC3::Error::None) {
        ALOGE("%s failed to register hotplug callback", __FUNCTION__);
        return error;
    }

    error = createDisplaysLocked();
    if (error != HWC3::Error::None) {
        ALOGE("%s failed to create displays.", __FUNCTION__);
        return error;
    }

    mCapabilities.push_back(Capability::LAYER_LIFECYCLE_BATCH_COMMAND);

    DEBUG_LOG("%s initialized!", __FUNCTION__);
    return HWC3::Error::None;
}

ndk::ScopedAStatus ComposerClient::createLayer(int64_t hwcId, int32_t bufferSlotCount,
                                               int64_t* layerId) {
    DEBUG_LOG("%s hwc display:%" PRIu64, __FUNCTION__, hwcId);

    GET_DISPLAY_OR_RETURN_ERROR();

    int64_t getLayerId = 0; // 0 means not preset layer Id
    HWC3::Error error = display->createLayer(&getLayerId);
    if (error != HWC3::Error::None) {
        ALOGE("%s: hwc display:%" PRIu64 " failed to create layer", __FUNCTION__, hwcId);
        return ToBinderStatus(error);
    }
    *layerId = getLayerId;

    error = mResources->addLayer(hwcId, *layerId, static_cast<uint32_t>(bufferSlotCount));
    if (error != HWC3::Error::None) {
        ALOGE("%s: hwc display:%" PRIu64 " resources failed to create layer", __FUNCTION__, hwcId);
        return ToBinderStatus(error);
    }

    return ToBinderStatus(HWC3::Error::None);
}

ndk::ScopedAStatus ComposerClient::createVirtualDisplay(int32_t /*width*/, int32_t /*height*/,
                                                        common::PixelFormat /*formatHint*/,
                                                        int32_t /*outputBufferSlotCount*/,
                                                        VirtualDisplay* /*display*/) {
    DEBUG_LOG("%s", __FUNCTION__);

    return ToBinderStatus(HWC3::Error::Unsupported);
}

ndk::ScopedAStatus ComposerClient::destroyLayer(int64_t hwcId, int64_t layerId) {
    DEBUG_LOG("%s hwc display:%" PRIu64, __FUNCTION__, hwcId);

    GET_DISPLAY_OR_RETURN_ERROR();

    HWC3::Error error = display->destroyLayer(layerId);
    if (error != HWC3::Error::None) {
        ALOGE("%s: hwc display:%" PRIu64 " failed to destroy layer:%" PRIu64, __FUNCTION__, hwcId,
              layerId);
        return ToBinderStatus(error);
    }

    error = mResources->removeLayer(hwcId, layerId);
    if (error != HWC3::Error::None) {
        ALOGE("%s: hwc display:%" PRIu64 " resources failed to destroy layer:%" PRIu64,
              __FUNCTION__, hwcId, layerId);
        return ToBinderStatus(error);
    }

    return ToBinderStatus(HWC3::Error::None);
}

ndk::ScopedAStatus ComposerClient::destroyVirtualDisplay(int64_t /*hwcId*/) {
    DEBUG_LOG("%s", __FUNCTION__);

    return ToBinderStatus(HWC3::Error::Unsupported);
}

ndk::ScopedAStatus ComposerClient::executeCommands(
        const std::vector<DisplayCommand>& commands,
        std::vector<CommandResultPayload>* commandResultPayloads) {
    DEBUG_LOG("%s", __FUNCTION__);

    CommandResultWriter commandResults(commandResultPayloads);

    for (const DisplayCommand& command : commands) {
        executeDisplayCommand(commandResults, command);
        commandResults.nextCommand();
    }

    return ToBinderStatus(HWC3::Error::None);
}

ndk::ScopedAStatus ComposerClient::getActiveConfig(int64_t hwcId, int32_t* config) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getActiveConfig(config));
}

ndk::ScopedAStatus ComposerClient::getColorModes(int64_t hwcId,
                                                 std::vector<ColorMode>* colorModes) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getColorModes(colorModes));
}

ndk::ScopedAStatus ComposerClient::getDataspaceSaturationMatrix(common::Dataspace dataspace,
                                                                std::vector<float>* matrix) {
    DEBUG_LOG("%s", __FUNCTION__);

    if (dataspace != common::Dataspace::SRGB_LINEAR) {
        return ToBinderStatus(HWC3::Error::BadParameter);
    }

    // clang-format off
  constexpr std::array<float, 16> kUnit {
    1.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 1.0f,
  };
    // clang-format on
    matrix->clear();
    matrix->insert(matrix->begin(), kUnit.begin(), kUnit.end());

    return ToBinderStatus(HWC3::Error::None);
}

ndk::ScopedAStatus ComposerClient::getDisplayAttribute(int64_t hwcId, int32_t config,
                                                       DisplayAttribute attribute, int32_t* value) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getDisplayAttribute(config, attribute, value));
}

ndk::ScopedAStatus ComposerClient::getDisplayCapabilities(int64_t hwcId,
                                                          std::vector<DisplayCapability>* outCaps) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getDisplayCapabilities(outCaps));
}

ndk::ScopedAStatus ComposerClient::getDisplayConfigs(int64_t hwcId,
                                                     std::vector<int32_t>* outConfigs) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getDisplayConfigs(outConfigs));
}

ndk::ScopedAStatus ComposerClient::getDisplayConnectionType(int64_t hwcId,
                                                            DisplayConnectionType* outType) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getDisplayConnectionType(outType));
}

ndk::ScopedAStatus ComposerClient::getDisplayIdentificationData(
        int64_t hwcId, DisplayIdentification* outIdentification) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getDisplayIdentificationData(outIdentification));
}

ndk::ScopedAStatus ComposerClient::getDisplayName(int64_t hwcId, std::string* outName) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getDisplayName(outName));
}

ndk::ScopedAStatus ComposerClient::getDisplayVsyncPeriod(int64_t hwcId, int32_t* outVsyncPeriod) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getDisplayVsyncPeriod(outVsyncPeriod));
}

ndk::ScopedAStatus ComposerClient::getDisplayedContentSample(int64_t hwcId, int64_t maxFrames,
                                                             int64_t timestamp,
                                                             DisplayContentSample* outSamples) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getDisplayedContentSample(maxFrames, timestamp, outSamples));
}

ndk::ScopedAStatus ComposerClient::getDisplayedContentSamplingAttributes(
        int64_t hwcId, DisplayContentSamplingAttributes* outAttributes) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getDisplayedContentSamplingAttributes(outAttributes));
}

ndk::ScopedAStatus ComposerClient::getDisplayPhysicalOrientation(
        int64_t hwcId, common::Transform* outOrientation) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getDisplayPhysicalOrientation(outOrientation));
}

ndk::ScopedAStatus ComposerClient::getHdrCapabilities(int64_t hwcId,
                                                      HdrCapabilities* outCapabilities) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getHdrCapabilities(outCapabilities));
}

ndk::ScopedAStatus ComposerClient::getOverlaySupport(OverlayProperties* /*properties*/) {
    DEBUG_LOG("%s", __FUNCTION__);

    return ToBinderStatus(HWC3::Error::Unsupported);
}

ndk::ScopedAStatus ComposerClient::getMaxVirtualDisplayCount(int32_t* outCount) {
    DEBUG_LOG("%s", __FUNCTION__);

    // Not supported.
    *outCount = 0;

    return ToBinderStatus(HWC3::Error::None);
}

ndk::ScopedAStatus ComposerClient::getPerFrameMetadataKeys(
        int64_t hwcId, std::vector<PerFrameMetadataKey>* outKeys) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getPerFrameMetadataKeys(outKeys));
}

ndk::ScopedAStatus ComposerClient::getReadbackBufferAttributes(
        int64_t hwcId, ReadbackBufferAttributes* outAttributes) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getReadbackBufferAttributes(outAttributes));
}

ndk::ScopedAStatus ComposerClient::getReadbackBufferFence(
        int64_t hwcId, ndk::ScopedFileDescriptor* outAcquireFence) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getReadbackBufferFence(outAcquireFence));
}

ndk::ScopedAStatus ComposerClient::getRenderIntents(int64_t hwcId, ColorMode mode,
                                                    std::vector<RenderIntent>* outIntents) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getRenderIntents(mode, outIntents));
}

ndk::ScopedAStatus ComposerClient::getSupportedContentTypes(int64_t hwcId,
                                                            std::vector<ContentType>* outTypes) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getSupportedContentTypes(outTypes));
}

ndk::ScopedAStatus ComposerClient::getDisplayDecorationSupport(
        int64_t hwcId, std::optional<common::DisplayDecorationSupport>* outSupport) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getDecorationSupport(outSupport));
}

ndk::ScopedAStatus ComposerClient::registerCallback(
        const std::shared_ptr<IComposerCallback>& callback) {
    DEBUG_LOG("%s", __FUNCTION__);

    const bool isFirstRegisterCallback = mCallbacks == nullptr;

    mCallbacks = callback;

    {
    std::lock_guard<std::mutex> lock(mDisplaysMutex);
    for (auto& [_, display] : mDisplays) {
        display->registerCallback(callback);
    }
    }

    if (isFirstRegisterCallback) {
        std::vector<int64_t> hwcIds;
        {
          std::lock_guard<std::mutex> lock(mDisplaysMutex);
          for (auto& [hwcId, _] : mDisplays) {
            hwcIds.push_back(hwcId);
          }
        }

        DEBUG_LOG("%s there are %zu displays when boot up", __FUNCTION__, hwcIds.size());
        for (auto& hwcId : hwcIds) {
            mCallbacks->onHotplug(hwcId, /*connected=*/true);
        }
    }

    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ComposerClient::setActiveConfig(int64_t hwcId, int32_t configId) {
    DEBUG_LOG("%s hwc display:%" PRIu64 " config:%" PRIu32, __FUNCTION__, hwcId, configId);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->setActiveConfig(configId));
}

ndk::ScopedAStatus ComposerClient::setActiveConfigWithConstraints(
        int64_t hwcId, int32_t configId, const VsyncPeriodChangeConstraints& constraints,
        VsyncPeriodChangeTimeline* outTimeline) {
    DEBUG_LOG("%s hwc display:%" PRIu64 " config:%" PRIu32, __FUNCTION__, hwcId, configId);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(
            display->setActiveConfigWithConstraints(configId, constraints, outTimeline));
}

ndk::ScopedAStatus ComposerClient::setBootDisplayConfig(int64_t hwcId, int32_t configId) {
    DEBUG_LOG("%s hwc display:%" PRIu64 " config:%" PRIu32, __FUNCTION__, hwcId, configId);

    GET_DISPLAY_OR_RETURN_ERROR();

    bool supported = std::any_of(mCapabilities.begin(), mCapabilities.end(), [&](Capability cap) {
        return cap == Capability::BOOT_DISPLAY_CONFIG;
    });
    if (!supported)
        return ToBinderStatus(HWC3::Error::Unsupported);

    return ToBinderStatus(display->setBootConfig(configId));
}

ndk::ScopedAStatus ComposerClient::clearBootDisplayConfig(int64_t hwcId) {
    DEBUG_LOG("%s hwc display:%" PRIu64, __FUNCTION__, hwcId);

    GET_DISPLAY_OR_RETURN_ERROR();

    bool supported = std::any_of(mCapabilities.begin(), mCapabilities.end(), [&](Capability cap) {
        return cap == Capability::BOOT_DISPLAY_CONFIG;
    });
    if (!supported)
        return ToBinderStatus(HWC3::Error::Unsupported);

    return ToBinderStatus(display->clearBootConfig());
}

ndk::ScopedAStatus ComposerClient::getPreferredBootDisplayConfig(int64_t hwcId,
                                                                 int32_t* outConfigId) {
    DEBUG_LOG("%s hwc display:%" PRIu64, __FUNCTION__, hwcId);

    GET_DISPLAY_OR_RETURN_ERROR();

    bool supported = std::any_of(mCapabilities.begin(), mCapabilities.end(), [&](Capability cap) {
        return cap == Capability::BOOT_DISPLAY_CONFIG;
    });
    if (!supported)
        return ToBinderStatus(HWC3::Error::Unsupported);

    return ToBinderStatus(display->getPreferredBootConfig(outConfigId));
}

ndk::ScopedAStatus ComposerClient::getHdrConversionCapabilities(
        std::vector<aidl::android::hardware::graphics::common::HdrConversionCapability>*
                capabilities) {
    DEBUG_LOG("%s", __FUNCTION__);
    capabilities->clear();
    return ToBinderStatus(HWC3::Error::None);
}

ndk::ScopedAStatus ComposerClient::setHdrConversionStrategy(
        const aidl::android::hardware::graphics::common::HdrConversionStrategy& conversionStrategy,
        aidl::android::hardware::graphics::common::Hdr* preferredHdrOutputType) {
    DEBUG_LOG("%s", __FUNCTION__);
    using HdrConversionStrategyTag =
            aidl::android::hardware::graphics::common::HdrConversionStrategy::Tag;
    switch (conversionStrategy.getTag()) {
        case HdrConversionStrategyTag::autoAllowedHdrTypes: {
            auto& autoHdrTypes =
                    conversionStrategy.get<HdrConversionStrategyTag::autoAllowedHdrTypes>();
            if (autoHdrTypes.size() != 0) {
                return ToBinderStatus(HWC3::Error::Unsupported);
            }
            break;
        }
        case HdrConversionStrategyTag::passthrough:
        case HdrConversionStrategyTag::forceHdrConversion: {
            break;
        }
    }
    *preferredHdrOutputType = aidl::android::hardware::graphics::common::Hdr::INVALID;
    return ToBinderStatus(HWC3::Error::None);
}

ndk::ScopedAStatus ComposerClient::setAutoLowLatencyMode(int64_t hwcId, bool on) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->setAutoLowLatencyMode(on));
}

ndk::ScopedAStatus ComposerClient::setClientTargetSlotCount(int64_t hwcId, int32_t count) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(
            mResources->setDisplayClientTargetCacheSize(hwcId, static_cast<uint32_t>(count)));
}

ndk::ScopedAStatus ComposerClient::setColorMode(int64_t hwcId, ColorMode mode,
                                                RenderIntent intent) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->setColorMode(mode, intent));
}

ndk::ScopedAStatus ComposerClient::setContentType(int64_t hwcId, ContentType type) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->setContentType(type));
}

ndk::ScopedAStatus ComposerClient::setDisplayedContentSamplingEnabled(
        int64_t hwcId, bool enable, FormatColorComponent componentMask, int64_t maxFrames) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(
            display->setDisplayedContentSamplingEnabled(enable, componentMask, maxFrames));
}

ndk::ScopedAStatus ComposerClient::setPowerMode(int64_t hwcId, PowerMode mode) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->setPowerMode(mode));
}

ndk::ScopedAStatus ComposerClient::setReadbackBuffer(
        int64_t hwcId, const aidl::android::hardware::common::NativeHandle& buffer,
        const ndk::ScopedFileDescriptor& releaseFence) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    // Owned by mResources.
    buffer_handle_t importedBuffer = nullptr;

    auto releaser = mResources->createReleaser(true /* isBuffer */);
    auto error =
            mResources->getDisplayReadbackBuffer(hwcId, buffer, &importedBuffer, releaser.get());
    if (error != HWC3::Error::None) {
        ALOGE("%s: failed to get readback buffer from resources.", __FUNCTION__);
        return ToBinderStatus(error);
    }

    error = display->setReadbackBuffer(importedBuffer, releaseFence);
    if (error != HWC3::Error::None) {
        ALOGE("%s: failed to set readback buffer to display.", __FUNCTION__);
        return ToBinderStatus(error);
    }

    return ToBinderStatus(HWC3::Error::None);
}

ndk::ScopedAStatus ComposerClient::setVsyncEnabled(int64_t hwcId, bool enabled) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->setVsyncEnabled(enabled));
}

ndk::ScopedAStatus ComposerClient::setIdleTimerEnabled(int64_t hwcId, int32_t timeoutMs) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->setIdleTimerEnabled(timeoutMs));
}

ndk::ScopedAStatus ComposerClient::setRefreshRateChangedCallbackDebugEnabled(int64_t hwcId, bool) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(HWC3::Error::Unsupported);
}

ndk::ScopedAStatus ComposerClient::getDisplayConfigurations(
        int64_t hwcId, int32_t maxFrameIntervalNs, std::vector<DisplayConfiguration>* configs) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->getDisplayConfigurations(maxFrameIntervalNs, configs));
}

ndk::ScopedAStatus ComposerClient::notifyExpectedPresent(
        int64_t hwcId, const ClockMonotonicTimestamp& expectedPresentTime,
        int32_t frameIntervalNs) {
    DEBUG_LOG("%s", __FUNCTION__);

    GET_DISPLAY_OR_RETURN_ERROR();

    return ToBinderStatus(display->notifyExpectedPresent(expectedPresentTime, frameIntervalNs));
}

ndk::SpAIBinder ComposerClient::createBinder() {
    auto binder = BnComposerClient::createBinder();
    AIBinder_setInheritRt(binder.get(), true);
    return binder;
}

namespace {

#define DISPATCH_LAYER_COMMAND(layerCmd, commandResults, display, layer, field, funcName)         \
    do {                                                                                          \
        if (layerCmd.field) {                                                                     \
            ComposerClient::executeLayerCommandSetLayer##funcName(commandResults, display, layer, \
                                                                  *layerCmd.field);               \
        }                                                                                         \
    } while (0)

#define DISPATCH_DISPLAY_COMMAND(displayCmd, commandResults, display, field, funcName)   \
    do {                                                                                 \
        if (displayCmd.field) {                                                          \
            executeDisplayCommand##funcName(commandResults, display, *displayCmd.field); \
        }                                                                                \
    } while (0)

#define DISPATCH_DISPLAY_BOOL_COMMAND(displayCmd, commandResults, display, field, funcName) \
    do {                                                                                    \
        if (displayCmd.field) {                                                             \
            executeDisplayCommand##funcName(commandResults, display);                       \
        }                                                                                   \
    } while (0)

#define DISPATCH_DISPLAY_BOOL_COMMAND_AND_DATA(displayCmd, commandResults, display, field, data, \
                                               funcName)                                         \
    do {                                                                                         \
        if (displayCmd.field) {                                                                  \
            executeDisplayCommand##funcName(commandResults, display, displayCmd.data);           \
        }                                                                                        \
    } while (0)

#define LOG_DISPLAY_COMMAND_ERROR(display, error)                                              \
    do {                                                                                       \
        const std::string errorString = toString(error);                                       \
        ALOGE("%s: hwc display:%" PRId64 " failed with:%s", __FUNCTION__, display.getHwcId(), \
              errorString.c_str());                                                            \
    } while (0)

#define LOG_LAYER_COMMAND_ERROR(display, layer, error)                                      \
    do {                                                                                    \
        const std::string errorString = toString(error);                                    \
        ALOGE("%s: hwc display:%" PRId64 " layer:%" PRId64 " failed with:%s", __FUNCTION__, \
              display.getHwcId(), layer->getId(), errorString.c_str());                    \
    } while (0)

} // namespace

void ComposerClient::dispatchBatchCreateDestroyLayerCommand(CommandResultWriter& commandResults,
                                                            Display& display,
                                                            const LayerCommand& layerCmd) {
    auto cmdType = layerCmd.layerLifecycleBatchCommandType;
    auto hwcId = display.getHwcId();
    auto layerId = layerCmd.layer;
    HWC3::Error error = HWC3::Error::None;

    if (cmdType == LayerLifecycleBatchCommandType::CREATE) {
        error = display.createLayer(&layerId); // preset layer Id as layerCmd.layer
        if (error != HWC3::Error::None) {
            ALOGE("%s: hwc display:%" PRIu64 " failed to create layer:%" PRIu64, __FUNCTION__,
                  hwcId, layerId);
            commandResults.addError(error);
            return;
        }

        error = mResources->addLayer(hwcId, layerId,
                                     static_cast<uint32_t>(layerCmd.newBufferSlotCount));
        if (error != HWC3::Error::None) {
            ALOGE("%s: hwc display:%" PRIu64 " resources failed to create layer%" PRIu64,
                  __FUNCTION__, hwcId, layerId);
            commandResults.addError(error);
            return;
        }
    } else if (cmdType == LayerLifecycleBatchCommandType::DESTROY) {
        Layer* layer = display.getLayer(layerId);
        if (layer == nullptr) {
            commandResults.addError(HWC3::Error::BadLayer);
            return;
        }

        error = display.destroyLayer(layerId);
        if (error != HWC3::Error::None) {
            ALOGE("%s: hwc display:%" PRIu64 " failed to destroy layer:%" PRIu64, __FUNCTION__,
                  hwcId, layerId);
            commandResults.addError(error);
            return;
        }

        error = mResources->removeLayer(hwcId, layerId);
        if (error != HWC3::Error::None) {
            ALOGE("%s: hwc display:%" PRIu64 " resources failed to destroy layer:%" PRIu64,
                  __FUNCTION__, hwcId, layerId);
            commandResults.addError(error);
            return;
        }
    }
}

void ComposerClient::executeDisplayCommand(CommandResultWriter& commandResults,
                                           const DisplayCommand& displayCommand) {
    std::shared_ptr<Display> display = getDisplay(displayCommand.display);
    if (display == nullptr) {
        commandResults.addError(HWC3::Error::BadDisplay);
        return;
    }

    for (const auto& layerCmd : displayCommand.layers) {
        if (layerCmd.layerLifecycleBatchCommandType == LayerLifecycleBatchCommandType::CREATE ||
            layerCmd.layerLifecycleBatchCommandType == LayerLifecycleBatchCommandType::DESTROY) {
            dispatchBatchCreateDestroyLayerCommand(commandResults, *display, layerCmd);
        }
    }
    DISPATCH_DISPLAY_COMMAND(displayCommand, commandResults, *display, brightness, SetBrightness);
    for (const LayerCommand& layerCmd : displayCommand.layers) {
        // ignore layer data update if command is DESTROY
        if (layerCmd.layerLifecycleBatchCommandType != LayerLifecycleBatchCommandType::DESTROY) {
            executeLayerCommand(commandResults, *display, layerCmd);
        }
    }

    DISPATCH_DISPLAY_COMMAND(displayCommand, commandResults, *display, colorTransformMatrix,
                             SetColorTransform);
    DISPATCH_DISPLAY_COMMAND(displayCommand, commandResults, *display, clientTarget,
                             SetClientTarget);
    DISPATCH_DISPLAY_COMMAND(displayCommand, commandResults, *display, virtualDisplayOutputBuffer,
                             SetOutputBuffer);
    DISPATCH_DISPLAY_BOOL_COMMAND_AND_DATA(displayCommand, commandResults, *display, validateDisplay,
                                           expectedPresentTime, ValidateDisplay);
    DISPATCH_DISPLAY_BOOL_COMMAND(displayCommand, commandResults, *display, acceptDisplayChanges,
                                  AcceptDisplayChanges);
    DISPATCH_DISPLAY_BOOL_COMMAND(displayCommand, commandResults, *display, presentDisplay,
                                  PresentDisplay);
    DISPATCH_DISPLAY_BOOL_COMMAND_AND_DATA(displayCommand, commandResults, *display,
                                           presentOrValidateDisplay, expectedPresentTime,
                                           PresentOrValidateDisplay);
}

void ComposerClient::executeLayerCommand(CommandResultWriter& commandResults, Display& display,
                                         const LayerCommand& layerCommand) {
    Layer* layer = display.getLayer(layerCommand.layer);
    if (layer == nullptr) {
        ALOGW("%s:get layer failed, %s", __FUNCTION__, layerCommand.toString().c_str());
        commandResults.addError(HWC3::Error::BadLayer);
        return;
    }

    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, cursorPosition,
                           CursorPosition);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, buffer, Buffer);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, damage, SurfaceDamage);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, blendMode, BlendMode);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, color, Color);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, composition, Composition);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, dataspace, Dataspace);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, displayFrame,
                           DisplayFrame);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, planeAlpha, PlaneAlpha);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, sidebandStream,
                           SidebandStream);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, sourceCrop, SourceCrop);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, transform, Transform);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, visibleRegion,
                           VisibleRegion);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, z, ZOrder);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, colorTransform,
                           ColorTransform);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, brightness, Brightness);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, perFrameMetadata,
                           PerFrameMetadata);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, perFrameMetadataBlob,
                           PerFrameMetadataBlobs);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, blockingRegion,
                           BlockingRegion);
    DISPATCH_LAYER_COMMAND(layerCommand, commandResults, display, layer, bufferSlotsToClear,
                           BufferSlotsToClear);
}

void ComposerClient::executeDisplayCommandSetColorTransform(CommandResultWriter& commandResults,
                                                            Display& display,
                                                            const std::vector<float>& matrix) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = display.setColorTransform(matrix);
    if (error != HWC3::Error::None) {
        LOG_DISPLAY_COMMAND_ERROR(display, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeDisplayCommandSetBrightness(CommandResultWriter& commandResults,
                                                        Display& display,
                                                        const DisplayBrightness& brightness) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = display.setBrightness(brightness.brightness);
    if (error != HWC3::Error::None) {
        LOG_DISPLAY_COMMAND_ERROR(display, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeDisplayCommandSetClientTarget(CommandResultWriter& commandResults,
                                                          Display& display,
                                                          const ClientTarget& clientTarget) {
    DEBUG_LOG("%s", __FUNCTION__);

    // Owned by mResources.
    buffer_handle_t importedBuffer = nullptr;

    auto releaser = mResources->createReleaser(/*isBuffer=*/true);
    auto error = mResources->getDisplayClientTarget(display.getHwcId(), clientTarget.buffer,
                                                    &importedBuffer, releaser.get());
    if (error != HWC3::Error::None) {
        LOG_DISPLAY_COMMAND_ERROR(display, error);
        commandResults.addError(error);
        return;
    }

    error = display.setClientTarget(importedBuffer, clientTarget.buffer.fence,
                                     clientTarget.dataspace, clientTarget.damage);
    if (error != HWC3::Error::None) {
        LOG_DISPLAY_COMMAND_ERROR(display, error);
        commandResults.addError(error);
        return;
    }
}

void ComposerClient::executeDisplayCommandSetOutputBuffer(CommandResultWriter& commandResults,
                                                          Display& display, const Buffer& buffer) {
    DEBUG_LOG("%s", __FUNCTION__);

    // Owned by mResources.
    buffer_handle_t importedBuffer = nullptr;

    auto releaser = mResources->createReleaser(/*isBuffer=*/true);
    auto error = mResources->getDisplayOutputBuffer(display.getHwcId(), buffer, &importedBuffer,
                                                    releaser.get());
    if (error != HWC3::Error::None) {
        LOG_DISPLAY_COMMAND_ERROR(display, error);
        commandResults.addError(error);
        return;
    }

    error = display.setOutputBuffer(importedBuffer, buffer.fence);
    if (error != HWC3::Error::None) {
        LOG_DISPLAY_COMMAND_ERROR(display, error);
        commandResults.addError(error);
        return;
    }
}

void ComposerClient::executeDisplayCommandValidateDisplay(
        CommandResultWriter& commandResults, Display& display,
        const std::optional<ClockMonotonicTimestamp> expectedPresentTime) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = display.setExpectedPresentTime(expectedPresentTime);
    if (error != HWC3::Error::None) {
        LOG_DISPLAY_COMMAND_ERROR(display, error);
        commandResults.addError(error);
    }

    DisplayChanges changes;

    error = display.validate(&changes);
    if (error != HWC3::Error::None) {
        LOG_DISPLAY_COMMAND_ERROR(display, error);
        commandResults.addError(error);
    } else {
        commandResults.addChanges(changes);
    }

    mResources->setDisplayMustValidateState(display.getHwcId(), false);
}

void ComposerClient::executeDisplayCommandAcceptDisplayChanges(CommandResultWriter& commandResults,
                                                               Display& display) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = display.acceptChanges();
    if (error != HWC3::Error::None) {
        LOG_DISPLAY_COMMAND_ERROR(display, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeDisplayCommandPresentOrValidateDisplay(
        CommandResultWriter& commandResults, Display& display,
        const std::optional<ClockMonotonicTimestamp> expectedPresentTime) {
    DEBUG_LOG("%s", __FUNCTION__);

    // TODO: Support SKIP_VALIDATE.

    auto error = display.setExpectedPresentTime(expectedPresentTime);
    if (error != HWC3::Error::None) {
        LOG_DISPLAY_COMMAND_ERROR(display, error);
        commandResults.addError(error);
    }

    DisplayChanges changes;

    error = display.validate(&changes);
    if (error != HWC3::Error::None) {
        LOG_DISPLAY_COMMAND_ERROR(display, error);
        commandResults.addError(error);
    } else {
        const int64_t hwcId = display.getHwcId();
        commandResults.addChanges(changes);
        static constexpr float kBrightness = 1.f;
        DimmingStage dimmingStage{DimmingStage::NONE};
        commandResults.addClientTargetProperty(hwcId, display.getClientTargetProperty(),
                                               kBrightness, dimmingStage);
        commandResults.addPresentOrValidateResult(hwcId, PresentOrValidate::Result::Validated);
    }

    mResources->setDisplayMustValidateState(display.getHwcId(), false);
}

void ComposerClient::executeDisplayCommandPresentDisplay(CommandResultWriter& commandResults,
                                                         Display& display) {
    DEBUG_LOG("%s", __FUNCTION__);

    if (mResources->mustValidateDisplay(display.getHwcId())) {
        ALOGE("%s: hwc display:%" PRIu64 " not validated", __FUNCTION__, display.getHwcId());
        commandResults.addError(HWC3::Error::NotValidated);
        return;
    }

    ::android::base::unique_fd displayFence;
    std::unordered_map<int64_t, ::android::base::unique_fd> layerFences;

    auto error = display.present(&displayFence, &layerFences);
    if (error != HWC3::Error::None) {
        LOG_DISPLAY_COMMAND_ERROR(display, error);
        commandResults.addError(error);
    } else {
        const int64_t hwcId = display.getHwcId();
        commandResults.addPresentFence(hwcId, std::move(displayFence));
        commandResults.addReleaseFences(hwcId, std::move(layerFences));
    }
}

void ComposerClient::executeLayerCommandSetLayerCursorPosition(
        CommandResultWriter& commandResults, Display& display, Layer* layer,
        const common::Point& cursorPosition) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setCursorPosition(cursorPosition);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerBuffer(CommandResultWriter& commandResults,
                                                       Display& display, Layer* layer,
                                                       const Buffer& buffer) {
    DEBUG_LOG("%s", __FUNCTION__);

    // Owned by mResources.
    buffer_handle_t importedBuffer = nullptr;

    auto releaser = mResources->createReleaser(/*isBuffer=*/true);
    auto error = mResources->getLayerBuffer(display.getHwcId(), layer->getId(), buffer,
                                            &importedBuffer, releaser.get());
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
        return;
    }

    error = layer->setBuffer(importedBuffer, buffer.fence);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerSurfaceDamage(
        CommandResultWriter& commandResults, Display& display, Layer* layer,
        const std::vector<std::optional<common::Rect>>& damage) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setSurfaceDamage(damage);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerBlendMode(CommandResultWriter& commandResults,
                                                          Display& display, Layer* layer,
                                                          const ParcelableBlendMode& blendMode) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setBlendMode(blendMode.blendMode);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerColor(CommandResultWriter& commandResults,
                                                      Display& display, Layer* layer,
                                                      const Color& color) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setColor(color);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerComposition(
        CommandResultWriter& commandResults, Display& display, Layer* layer,
        const ParcelableComposition& composition) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setCompositionType(composition.composition);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }

    if (mCallbacks && (int(composition.composition) == Composition_NXP_PRIVATE))
        mCallbacks->onRefresh(display.getHwcId());
}

void ComposerClient::executeLayerCommandSetLayerDataspace(CommandResultWriter& commandResults,
                                                          Display& display, Layer* layer,
                                                          const ParcelableDataspace& dataspace) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setDataspace(dataspace.dataspace);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerDisplayFrame(CommandResultWriter& commandResults,
                                                             Display& display, Layer* layer,
                                                             const common::Rect& rect) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setDisplayFrame(rect);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerPlaneAlpha(CommandResultWriter& commandResults,
                                                           Display& display, Layer* layer,
                                                           const PlaneAlpha& planeAlpha) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setPlaneAlpha(planeAlpha.alpha);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerSidebandStream(
        CommandResultWriter& commandResults, Display& display, Layer* layer,
        const aidl::android::hardware::common::NativeHandle& handle) {
    DEBUG_LOG("%s", __FUNCTION__);

    // Owned by mResources.
    buffer_handle_t importedStream = nullptr;

    auto releaser = mResources->createReleaser(/*isBuffer=*/false);
    auto error = mResources->getLayerSidebandStream(display.getHwcId(), layer->getId(), handle,
                                                    &importedStream, releaser.get());
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
        return;
    }

    error = layer->setSidebandStream(importedStream);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerSourceCrop(CommandResultWriter& commandResults,
                                                           Display& display, Layer* layer,
                                                           const common::FRect& sourceCrop) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setSourceCrop(sourceCrop);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerTransform(CommandResultWriter& commandResults,
                                                          Display& display, Layer* layer,
                                                          const ParcelableTransform& transform) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setTransform(transform.transform);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerVisibleRegion(
        CommandResultWriter& commandResults, Display& display, Layer* layer,
        const std::vector<std::optional<common::Rect>>& visibleRegion) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setVisibleRegion(visibleRegion);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerZOrder(CommandResultWriter& commandResults,
                                                       Display& display, Layer* layer,
                                                       const ZOrder& zOrder) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setZOrder(zOrder.z);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerPerFrameMetadata(
        CommandResultWriter& commandResults, Display& display, Layer* layer,
        const std::vector<std::optional<PerFrameMetadata>>& perFrameMetadata) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setPerFrameMetadata(perFrameMetadata);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerColorTransform(
        CommandResultWriter& commandResults, Display& display, Layer* layer,
        const std::vector<float>& colorTransform) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setColorTransform(colorTransform);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerBrightness(CommandResultWriter& commandResults,
                                                           Display& display, Layer* layer,
                                                           const LayerBrightness& brightness) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setBrightness(brightness.brightness);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerPerFrameMetadataBlobs(
        CommandResultWriter& commandResults, Display& display, Layer* layer,
        const std::vector<std::optional<PerFrameMetadataBlob>>& perFrameMetadataBlob) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setPerFrameMetadataBlobs(perFrameMetadataBlob);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerBlockingRegion(
        CommandResultWriter& commandResults, Display& display, Layer* layer,
        const std::vector<std::optional<common::Rect>>& blockingRegion) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto error = layer->setBlockingRegion(blockingRegion);
    if (error != HWC3::Error::None) {
        LOG_LAYER_COMMAND_ERROR(display, layer, error);
        commandResults.addError(error);
    }
}

void ComposerClient::executeLayerCommandSetLayerBufferSlotsToClear(
        CommandResultWriter& commandResults, Display& display, Layer* layer,
        const std::vector<int32_t>& bufferSlotsToClear) {
    DEBUG_LOG("%s", __FUNCTION__);

    auto powerMode = display.getPowerMode();
    if (powerMode != PowerMode::OFF)
        return;

    buffer_handle_t cachedBuffer = nullptr;
    auto bufferReleaser = mResources->createReleaser(true);

    // get all cached buffers
    std::vector<buffer_handle_t> cachedBuffers;
    std::map<buffer_handle_t, int32_t> handle2Slots;
    for (int32_t slot : bufferSlotsToClear) {
        auto error = mResources->getLayerInternalBuffer(display.getHwcId(), layer->getId(),
                                                        static_cast<uint32_t>(slot),
                                                        /*fromCache=*/true, nullptr, cachedBuffer,
                                                        bufferReleaser.get());
        if (cachedBuffer) {
            cachedBuffers.push_back(cachedBuffer);
            handle2Slots[cachedBuffer] = slot;
        } else {
            ALOGE("%s: Buffer slot %d is null", __FUNCTION__, slot);
        }
        if (error != HWC3::Error::None) {
            ALOGE("%s: failed to getLayerBuffer err:%d", __FUNCTION__, error);
            commandResults.addError(error);
            return;
        }
    }

    // clear any other cache in composer
    std::vector<buffer_handle_t> clearableBuffers;
    auto error = layer->uncacheLayerBuffers(cachedBuffers, clearableBuffers);
    if (error != HWC3::Error::None) {
        ALOGE("%s: layer %" PRIu64 " uncacheLayerBuffers fail with err:%d", __FUNCTION__,
              layer->getId(), error);
        commandResults.addError(error);
        return;
    }

    for (auto buffer : clearableBuffers) {
        auto slot = handle2Slots[buffer];
        // replace the slot with nullptr and release the buffer by bufferReleaser
        auto error = mResources->getLayerInternalBuffer(display.getHwcId(), layer->getId(),
                                                        static_cast<uint32_t>(slot),
                                                        /*fromCache=*/false, nullptr, cachedBuffer,
                                                        bufferReleaser.get());
        if (error != HWC3::Error::None) {
            ALOGE("%s: failed to clear buffer cache err:%d", __FUNCTION__, error);
            commandResults.addError(error);
            return;
        }
    }
}

std::shared_ptr<Display> ComposerClient::getDisplay(int64_t hwcId) {
    std::lock_guard<std::mutex> lock(mDisplaysMutex);

    auto it = mDisplays.find(hwcId);
    if (it == mDisplays.end()) {
        ALOGE("%s: no hwc display:%" PRIi64, __FUNCTION__, hwcId);
        return nullptr;
    }
    return it->second;
}

HWC3::Error ComposerClient::createDisplaysLocked() {
    DEBUG_LOG("%s", __FUNCTION__);

    if (!mComposer) {
        ALOGE("%s composer not initialized!", __FUNCTION__);
        return HWC3::Error::NoResources;
    }

    std::vector<DisplayMultiConfigs> displays;

    HWC3::Error error = findDisplays(mComposer, &displays);
    if (error != HWC3::Error::None) {
        ALOGE("%s failed to find display configs", __FUNCTION__);
        return error;
    }

    for (const auto& iter : displays) {
        error = createDisplayLocked(iter.hwcId, iter.displayId, iter.activeConfigId, iter.configs);
        if (error != HWC3::Error::None) {
            ALOGE("%s failed to create display from config", __FUNCTION__);
            return error;
        }
    }

    return HWC3::Error::None;
}

HWC3::Error ComposerClient::createDisplayLocked(int64_t hwcId, uint32_t displayId,
                                                int32_t activeConfigId,
                                                const std::vector<DisplayConfig>& configs) {
    DEBUG_LOG("%s", __FUNCTION__);

    if (!mComposer) {
        ALOGE("%s composer not initialized!", __FUNCTION__);
        return HWC3::Error::NoResources;
    }

    bool created = false;
    Display* display;
    std::shared_ptr<Display> hwcDisplay;
    if (mDisplays.find(hwcId) == mDisplays.end()) {
        hwcDisplay = std::make_shared<Display>(mComposer, hwcId, displayId);
        display = hwcDisplay.get();
        if (display == nullptr) {
            ALOGE("%s failed to allocate hwc display", __FUNCTION__);
            return HWC3::Error::NoResources;
        }
        created = true;
    } else {
        display = mDisplays[hwcId].get();
    }

    HWC3::Error error = display->init(configs, activeConfigId);
    if (error != HWC3::Error::None) {
        ALOGE("%s failed to initialize hwc display:%" PRIu64, __FUNCTION__, hwcId);
        return error;
    }

    error = mComposer->onDisplayCreate(display);
    if (error != HWC3::Error::None) {
        ALOGE("%s failed to register hwc display:%" PRIu64 " with composer", __FUNCTION__, hwcId);
        return error;
    }

    display->setPowerMode(PowerMode::ON);

    if (created) {
        DEBUG_LOG("%s: adding hwc display:%" PRIu64, __FUNCTION__, hwcId);
        mDisplays.emplace(hwcId, std::move(hwcDisplay));

        error = mResources->addPhysicalDisplay(hwcId);
        if (error != HWC3::Error::None) {
            ALOGE("%s failed to initialize hwc display:%" PRIu64 " resources", __FUNCTION__, hwcId);
            return error;
        }
    }

    return HWC3::Error::None;
}

HWC3::Error ComposerClient::destroyDisplaysLocked() {
    DEBUG_LOG("%s", __FUNCTION__);

    std::vector<int64_t> displayIds;
    for (const auto& [hwcId, _] : mDisplays) {
        displayIds.push_back(hwcId);
    }
    for (const int64_t hwcId : displayIds) {
        destroyDisplayLocked(hwcId);
    }

    return HWC3::Error::None;
}

HWC3::Error ComposerClient::destroyDisplayLocked(int64_t hwcId) {
    DEBUG_LOG("%s hwc display:%" PRId64, __FUNCTION__, hwcId);

    auto it = mDisplays.find(hwcId);
    if (it == mDisplays.end()) {
        ALOGE("%s: hwc display:%" PRId64 " no such display?", __FUNCTION__, hwcId);
        return HWC3::Error::BadDisplay;
    }

    Display* display = it->second.get();

    display->setPowerMode(PowerMode::OFF);

    HWC3::Error error = mComposer->onDisplayDestroy(it->second.get());
    if (error != HWC3::Error::None) {
        ALOGE("%s: hwc display:%" PRId64 " failed to destroy with frame composer", __FUNCTION__,
              hwcId);
    }

    error = mResources->removeDisplay(hwcId);
    if (error != HWC3::Error::None) {
        ALOGE("%s: hwc display:%" PRId64 " failed to destroy with resources", __FUNCTION__, hwcId);
    }

    mDisplays.erase(it);

    return HWC3::Error::None;
}

HWC3::Error ComposerClient::handleHotplug(bool connected,
                                          std::unique_ptr<HalMultiConfigs> halConfigs) {
    if (!mCallbacks) {
        return HWC3::Error::None;
    }

    const int64_t hwcId = static_cast<int64_t>(halConfigs->hwcId);
    const uint32_t displayId = halConfigs->displayId;

    if (connected) {
        const int32_t configId = halConfigs->activeConfigId;
        std::vector<DisplayConfig> configs;
        for (const auto& pair : *(halConfigs->configs)) {
            HalDisplayConfig config = pair.second;
            configs.emplace_back(DisplayConfig(static_cast<int32_t>(pair.first),
                                               static_cast<int32_t>(config.width),
                                               static_cast<int32_t>(config.height),
                                               static_cast<int32_t>(config.dpiX),
                                               static_cast<int32_t>(config.dpiY),
                                               HertzToPeriodNanos(config.refreshRateHz)));
        }
        DisplayConfig::addConfigGroups(&configs);

        {
            std::lock_guard<std::mutex> lock(mDisplaysMutex);
            createDisplayLocked(hwcId, displayId, configId, configs);
        }

        auto& cfg = (*(halConfigs->configs))[static_cast<uint32_t>(configId)];
        ALOGI("Connecting display:%d hwcId:%ld, w:%d, h:%d, dpiX:%d, dpiY:%d, fps:%d", displayId,
              hwcId, cfg.width, cfg.height, cfg.dpiX, cfg.dpiY, cfg.refreshRateHz);
        mCallbacks->onHotplug(hwcId, /*connected=*/true);
    } else {
        ALOGI("Disconnecting display:%d", displayId);
        mCallbacks->onHotplug(hwcId, /*connected=*/false);

        {
            std::lock_guard<std::mutex> lock(mDisplaysMutex);
            destroyDisplayLocked(hwcId);
        }
    }

    return HWC3::Error::None;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
