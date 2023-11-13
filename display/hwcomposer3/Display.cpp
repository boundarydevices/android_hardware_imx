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

#include "Display.h"

#include <android-base/parseint.h>
#include <android-base/unique_fd.h>
#include <pthread.h>
#include <sched.h>
#include <sync/sync.h>
#include <sys/types.h>

#include <algorithm>
#include <atomic>
#include <numeric>
#include <sstream>
#include <thread>

#include "Common.h"
#include "Device.h"

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

bool isValidColorMode(ColorMode mode) {
    switch (mode) {
        case ColorMode::NATIVE:
        case ColorMode::STANDARD_BT601_625:
        case ColorMode::STANDARD_BT601_625_UNADJUSTED:
        case ColorMode::STANDARD_BT601_525:
        case ColorMode::STANDARD_BT601_525_UNADJUSTED:
        case ColorMode::STANDARD_BT709:
        case ColorMode::DCI_P3:
        case ColorMode::SRGB:
        case ColorMode::ADOBE_RGB:
        case ColorMode::DISPLAY_P3:
        case ColorMode::BT2020:
        case ColorMode::BT2100_PQ:
        case ColorMode::BT2100_HLG:
        case ColorMode::DISPLAY_BT2020:
            return true;
        default:
            return false;
    }
}

bool isValidRenderIntent(RenderIntent intent) {
    switch (intent) {
        case RenderIntent::COLORIMETRIC:
        case RenderIntent::ENHANCE:
        case RenderIntent::TONE_MAP_COLORIMETRIC:
        case RenderIntent::TONE_MAP_ENHANCE:
            return true;
        default:
            return false;
    }
}

bool isValidPowerMode(PowerMode mode) {
    switch (mode) {
        case PowerMode::OFF:
        case PowerMode::DOZE:
        case PowerMode::DOZE_SUSPEND:
        case PowerMode::ON:
        case PowerMode::ON_SUSPEND:
            return true;
        default:
            return false;
    }
}

} // namespace

Display::Display(FrameComposer* composer, int64_t id)
      : mComposer(composer), mId(id), mVsyncThread(id) {
    mVsyncStarted = false;
    setLegacyEdid();
}

Display::~Display() {}

HWC3::Error Display::init(const std::vector<DisplayConfig>& configs, int32_t activeConfigId,
                          const std::optional<std::vector<uint8_t>>& edid) {
    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    mConfigs.clear();
    for (const DisplayConfig& config : configs) {
        mConfigs.emplace(config.getId(), config);
    }

    mActiveConfigId = activeConfigId;

    auto bootConfigIdOpt = getBootConfigId();
    if (bootConfigIdOpt) {
        mActiveConfigId = *bootConfigIdOpt;
    }

    if (edid.has_value()) {
        mEdid = *edid;
    }

    auto it = mConfigs.find(*mActiveConfigId);
    if (it == mConfigs.end()) {
        ALOGE("%s: display:%" PRId64 "missing config:%" PRId32, __FUNCTION__, mId, activeConfigId);
        return HWC3::Error::NoResources;
    }

    const auto& activeConfig = it->second;
    const auto activeConfigString = activeConfig.toString();
    ALOGI("%s display:%" PRId64 " with config:%s", __FUNCTION__, mId, activeConfigString.c_str());

    if (!mVsyncStarted) {
        mVsyncThread.start(activeConfig.getVsyncPeriod());
        mVsyncStarted = true;
    }
    return HWC3::Error::None;
}

HWC3::Error Display::updateParameters(uint32_t width, uint32_t height, uint32_t dpiX, uint32_t dpiY,
                                      uint32_t refreshRateHz,
                                      const std::optional<std::vector<uint8_t>>& edid) {
    DEBUG_LOG("%s: updating display:%" PRId64
              " width:%d height:%d dpiX:%d dpiY:%d refreshRateHz:%d",
              __FUNCTION__, mId, width, height, dpiX, dpiY, refreshRateHz);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    auto it = mConfigs.find(*mActiveConfigId);
    if (it == mConfigs.end()) {
        ALOGE("%s: failed to find config %" PRId32, __func__, *mActiveConfigId);
        return HWC3::Error::NoResources;
    }
    it->second.setAttribute(DisplayAttribute::VSYNC_PERIOD, 1000 * 1000 * 1000 / refreshRateHz);
    it->second.setAttribute(DisplayAttribute::WIDTH, width);
    it->second.setAttribute(DisplayAttribute::HEIGHT, height);
    it->second.setAttribute(DisplayAttribute::DPI_X, dpiX);
    it->second.setAttribute(DisplayAttribute::DPI_Y, dpiY);

    if (edid.has_value()) {
        mEdid = *edid;
    }

    return HWC3::Error::None;
}

HWC3::Error Display::createLayer(int64_t* outLayerId) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    auto layer = std::make_unique<Layer>();

    const int64_t layerId = layer->getId();
    DEBUG_LOG("%s: created layer:%" PRId64, __FUNCTION__, layerId);

    mLayers.emplace(layerId, std::move(layer));

    *outLayerId = layerId;

    return HWC3::Error::None;
}

HWC3::Error Display::destroyLayer(int64_t layerId) {
    DEBUG_LOG("%s: destroy layer:%" PRId64, __FUNCTION__, layerId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    auto it = mLayers.find(layerId);
    if (it == mLayers.end()) {
        ALOGE("%s display:%" PRId64 " has no such layer:%." PRId64, __FUNCTION__, mId, layerId);
        return HWC3::Error::BadLayer;
    }

    mOrderedLayers.erase(std::remove_if(mOrderedLayers.begin(), //
                                        mOrderedLayers.end(),   //
                                        [layerId](Layer* layer) {
                                            return layer->getId() == layerId;
                                        }),
                         mOrderedLayers.end());

    mLayers.erase(it);

    DEBUG_LOG("%s: destroyed layer:%" PRId64, __FUNCTION__, layerId);
    return HWC3::Error::None;
}

HWC3::Error Display::getActiveConfig(int32_t* outConfig) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!mActiveConfigId) {
        ALOGW("%s: display:%" PRId64 " has no active config.", __FUNCTION__, mId);
        return HWC3::Error::BadConfig;
    }

    *outConfig = *mActiveConfigId;
    return HWC3::Error::None;
}

HWC3::Error Display::getDisplayAttribute(int32_t configId, DisplayAttribute attribute,
                                         int32_t* outValue) {
    auto attributeString = toString(attribute);
    DEBUG_LOG("%s: display:%" PRId64 " attribute:%s", __FUNCTION__, mId, attributeString.c_str());

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    auto it = mConfigs.find(configId);
    if (it == mConfigs.end()) {
        ALOGW("%s: display:%" PRId64 " bad config:%" PRId32, __FUNCTION__, mId, configId);
        return HWC3::Error::BadConfig;
    }

    const DisplayConfig& config = it->second;
    *outValue = config.getAttribute(attribute);
    DEBUG_LOG("%s: display:%" PRId64 " attribute:%s value is %" PRIi32, __FUNCTION__, mId,
              attributeString.c_str(), *outValue);
    return HWC3::Error::None;
}

HWC3::Error Display::getColorModes(std::vector<ColorMode>* outModes) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    outModes->clear();
    outModes->insert(outModes->end(), mColorModes.begin(), mColorModes.end());

    return HWC3::Error::None;
}

HWC3::Error Display::getDisplayCapabilities(std::vector<DisplayCapability>* outCapabilities) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    outCapabilities->clear();
    for (auto& cap : mCapability) outCapabilities->push_back(cap);

    return HWC3::Error::None;
}

HWC3::Error Display::getDisplayConfigs(std::vector<int32_t>* outConfigIds) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    outConfigIds->clear();
    outConfigIds->reserve(mConfigs.size());
    for (const auto& [configId, _] : mConfigs) {
        outConfigIds->push_back(configId);
    }

    return HWC3::Error::None;
}

HWC3::Error Display::getDisplayConnectionType(DisplayConnectionType* outType) {
    if (IsAutoDevice()) {
        // Android Auto OS needs to set all displays to INTERNAL since they're used
        // for the passenger displays.
        // Workaround to force all displays to INTERNAL for cf_x86_64_foldable.
        // TODO(b/193568008): Allow configuring internal/external per display.
        *outType = DisplayConnectionType::INTERNAL;
    } else {
        // Other devices default to the first display INTERNAL, others EXTERNAL.
        *outType = mId == 0 ? DisplayConnectionType::INTERNAL : DisplayConnectionType::EXTERNAL;
    }
    return HWC3::Error::None;
}

HWC3::Error Display::getDisplayIdentificationData(DisplayIdentification* outIdentification) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    if (outIdentification == nullptr) {
        return HWC3::Error::BadParameter;
    }

    outIdentification->port = mId;
    outIdentification->data = mEdid;

    return HWC3::Error::None;
}

HWC3::Error Display::getDisplayName(std::string* outName) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    *outName = mName;
    return HWC3::Error::None;
}

HWC3::Error Display::getDisplayVsyncPeriod(int32_t* outVsyncPeriod) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (!mActiveConfigId) {
        ALOGE("%s : display:%" PRId64 " no active config", __FUNCTION__, mId);
        return HWC3::Error::BadConfig;
    }

    const auto it = mConfigs.find(*mActiveConfigId);
    if (it == mConfigs.end()) {
        ALOGE("%s : display:%" PRId64 " failed to find active config:%" PRId32, __FUNCTION__, mId,
              *mActiveConfigId);
        return HWC3::Error::BadConfig;
    }
    const DisplayConfig& activeConfig = it->second;

    *outVsyncPeriod = activeConfig.getAttribute(DisplayAttribute::VSYNC_PERIOD);
    return HWC3::Error::None;
}

HWC3::Error Display::getDisplayedContentSample(int64_t /*maxFrames*/, int64_t /*timestamp*/,
                                               DisplayContentSample* /*samples*/) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    return HWC3::Error::Unsupported;
}

HWC3::Error Display::getDisplayedContentSamplingAttributes(
        DisplayContentSamplingAttributes* /*outAttributes*/) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    return HWC3::Error::Unsupported;
}

HWC3::Error Display::getDisplayPhysicalOrientation(common::Transform* outOrientation) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    *outOrientation = common::Transform::NONE;

    return HWC3::Error::None;
}

HWC3::Error Display::getHdrCapabilities(HdrCapabilities* outCapabilities) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    // No supported types.
    outCapabilities->types.clear();

    return HWC3::Error::None;
}

HWC3::Error Display::getPerFrameMetadataKeys(std::vector<PerFrameMetadataKey>* outKeys) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    outKeys->clear();

    return HWC3::Error::Unsupported;
}

HWC3::Error Display::getReadbackBufferAttributes(ReadbackBufferAttributes* outAttributes) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    outAttributes->format = common::PixelFormat::RGBA_8888;
    outAttributes->dataspace = common::Dataspace::UNKNOWN;

    return HWC3::Error::Unsupported;
}

HWC3::Error Display::getReadbackBufferFence(ndk::ScopedFileDescriptor* /*outAcquireFence*/) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    return HWC3::Error::Unsupported;
}

HWC3::Error Display::getRenderIntents(ColorMode mode, std::vector<RenderIntent>* outIntents) {
    const auto modeString = toString(mode);
    DEBUG_LOG("%s: display:%" PRId64 "for mode:%s", __FUNCTION__, mId, modeString.c_str());

    outIntents->clear();

    if (!isValidColorMode(mode)) {
        DEBUG_LOG("%s: display:%" PRId64 "invalid mode:%s", __FUNCTION__, mId, modeString.c_str());
        return HWC3::Error::BadParameter;
    }

    outIntents->push_back(RenderIntent::COLORIMETRIC);

    return HWC3::Error::None;
}

HWC3::Error Display::getSupportedContentTypes(std::vector<ContentType>* outTypes) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    outTypes->clear();

    return HWC3::Error::None;
}

HWC3::Error Display::getDecorationSupport(
        std::optional<common::DisplayDecorationSupport>* outSupport) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    outSupport->reset();

    return HWC3::Error::Unsupported;
}

HWC3::Error Display::registerCallback(const std::shared_ptr<IComposerCallback>& callback) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    mVsyncThread.setCallbacks(callback);

    return HWC3::Error::Unsupported;
}

HWC3::Error Display::setActiveConfig(int32_t configId) {
    DEBUG_LOG("%s: display:%" PRId64 " setting active config to %" PRId32, __FUNCTION__, mId,
              configId);

    VsyncPeriodChangeConstraints constraints;
    constraints.desiredTimeNanos = 0;
    constraints.seamlessRequired = false;

    VsyncPeriodChangeTimeline timeline;

    return setActiveConfigWithConstraints(configId, constraints, &timeline);
}

HWC3::Error Display::setActiveConfigWithConstraints(int32_t configId,
                                                    const VsyncPeriodChangeConstraints& constraints,
                                                    VsyncPeriodChangeTimeline* outTimeline) {
    DEBUG_LOG("%s: display:%" PRId64 " config:%" PRId32, __FUNCTION__, mId, configId);

    if (outTimeline == nullptr) {
        return HWC3::Error::BadParameter;
    }

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (mActiveConfigId == configId) {
        return HWC3::Error::None;
    }

    DisplayConfig* newConfig = getConfig(configId);
    if (newConfig == nullptr) {
        ALOGE("%s: display:%" PRId64 " bad config:%" PRId32, __FUNCTION__, mId, configId);
        return HWC3::Error::BadConfig;
    }

    if (constraints.seamlessRequired) {
        if (mActiveConfigId) {
            DisplayConfig* oldConfig = getConfig(*mActiveConfigId);
            if (oldConfig == nullptr) {
                ALOGE("%s: display:%" PRId64 " missing config:%" PRId32, __FUNCTION__, mId,
                      *mActiveConfigId);
                return HWC3::Error::NoResources;
            }

            const int32_t newConfigGroup = newConfig->getConfigGroup();
            const int32_t oldConfigGroup = oldConfig->getConfigGroup();
            if (newConfigGroup != oldConfigGroup) {
                DEBUG_LOG("%s: display:%" PRId64 " config:%" PRId32
                          " seamless not supported between different config groups "
                          "old:%d vs new:%d",
                          __FUNCTION__, mId, configId, oldConfigGroup, newConfigGroup);
                return HWC3::Error::SeamlessNotAllowed;
            }
        }
    }

    mActiveConfigId = configId;

    if (mComposer == nullptr) {
        ALOGE("%s: display:%" PRId64 " missing composer", __FUNCTION__, mId);
        return HWC3::Error::NoResources;
    }

    HWC3::Error error = mComposer->onActiveConfigChange(this);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRId64 " composer failed to handle config change", __FUNCTION__, mId);
        return error;
    }

    int32_t vsyncPeriod;
    error = getDisplayVsyncPeriod(&vsyncPeriod);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRId64 " composer failed to handle config change", __FUNCTION__, mId);
        return error;
    }

    return mVsyncThread.scheduleVsyncUpdate(vsyncPeriod, constraints, outTimeline);
}

std::optional<int32_t> Display::getBootConfigId() {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    std::string val;
    HWC3::Error error = Device::getInstance().getPersistentKeyValue(std::to_string(mId), "", &val);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRId64 " failed to get persistent boot config", __FUNCTION__, mId);
        return std::nullopt;
    }

    if (val.empty()) {
        return std::nullopt;
    }

    int32_t configId = 0;
    if (!::android::base::ParseInt(val, &configId)) {
        ALOGE("%s: display:%" PRId64 " failed to parse persistent boot config from: %s",
              __FUNCTION__, mId, val.c_str());
        return std::nullopt;
    }

    if (!hasConfig(configId)) {
        ALOGE("%s: display:%" PRId64 " invalid persistent boot config:%" PRId32, __FUNCTION__, mId,
              configId);
        return std::nullopt;
    }

    return configId;
}

HWC3::Error Display::setBootConfig(int32_t configId) {
    DEBUG_LOG("%s: display:%" PRId64 " config:%" PRId32, __FUNCTION__, mId, configId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    DisplayConfig* newConfig = getConfig(configId);
    if (newConfig == nullptr) {
        ALOGE("%s: display:%" PRId64 " bad config:%" PRId32, __FUNCTION__, mId, configId);
        return HWC3::Error::BadConfig;
    }

    const std::string key = std::to_string(mId);
    const std::string val = std::to_string(configId);
    HWC3::Error error = Device::getInstance().setPersistentKeyValue(key, val);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRId64 " failed to save persistent boot config", __FUNCTION__, mId);
        return error;
    }

    return HWC3::Error::None;
}

HWC3::Error Display::clearBootConfig() {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    const std::string key = std::to_string(mId);
    const std::string val = "";
    HWC3::Error error = Device::getInstance().setPersistentKeyValue(key, val);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRId64 " failed to save persistent boot config", __FUNCTION__, mId);
        return error;
    }

    return HWC3::Error::None;
}

HWC3::Error Display::getPreferredBootConfig(int32_t* outConfigId) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    std::vector<int32_t> configIds;
    for (const auto& [configId, _] : mConfigs) {
        configIds.push_back(configId);
    }
    *outConfigId = *std::min_element(configIds.begin(), configIds.end());

    return HWC3::Error::None;
}

HWC3::Error Display::setAutoLowLatencyMode(bool /*on*/) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    return HWC3::Error::Unsupported;
}

HWC3::Error Display::setColorMode(ColorMode mode, RenderIntent intent) {
    const std::string modeString = toString(mode);
    const std::string intentString = toString(intent);
    DEBUG_LOG("%s: display:%" PRId64 " setting color mode:%s intent:%s", __FUNCTION__, mId,
              modeString.c_str(), intentString.c_str());

    if (!isValidColorMode(mode)) {
        ALOGE("%s: display:%" PRId64 " invalid color mode:%s", __FUNCTION__, mId,
              modeString.c_str());
        return HWC3::Error::BadParameter;
    }

    if (!isValidRenderIntent(intent)) {
        ALOGE("%s: display:%" PRId64 " invalid intent:%s", __FUNCTION__, mId, intentString.c_str());
        return HWC3::Error::BadParameter;
    }

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    if (mColorModes.count(mode) == 0) {
        ALOGE("%s: display %" PRId64 " mode %s not supported", __FUNCTION__, mId,
              modeString.c_str());
        return HWC3::Error::Unsupported;
    }

    mActiveColorMode = mode;
    return HWC3::Error::None;
}

HWC3::Error Display::setContentType(ContentType contentType) {
    auto contentTypeString = toString(contentType);
    DEBUG_LOG("%s: display:%" PRId64 " content type:%s", __FUNCTION__, mId,
              contentTypeString.c_str());

    if (contentType != ContentType::NONE) {
        return HWC3::Error::Unsupported;
    }

    return HWC3::Error::None;
}

HWC3::Error Display::setDisplayedContentSamplingEnabled(bool /*enable*/,
                                                        FormatColorComponent /*componentMask*/,
                                                        int64_t /*maxFrames*/) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    return HWC3::Error::Unsupported;
}

HWC3::Error Display::setPowerMode(PowerMode mode) {
    auto modeString = toString(mode);
    DEBUG_LOG("%s: display:%" PRId64 " to mode:%s", __FUNCTION__, mId, modeString.c_str());

    if (!isValidPowerMode(mode)) {
        ALOGE("%s: display:%" PRId64 " invalid mode:%s", __FUNCTION__, mId, modeString.c_str());
        return HWC3::Error::BadParameter;
    }

    if (mode == PowerMode::DOZE || mode == PowerMode::DOZE_SUSPEND ||
        mode == PowerMode::ON_SUSPEND) {
        ALOGE("%s display %" PRId64 " mode:%s not supported", __FUNCTION__, mId,
              modeString.c_str());
        return HWC3::Error::Unsupported;
    }

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    HWC3::Error error = mComposer->setPowerMode(this, mode);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRId64 " composer failed to set power mode", __FUNCTION__, mId);
        return error;
    }

    mPowerMode = mode;
    return HWC3::Error::None;
}

HWC3::Error Display::setReadbackBuffer(const buffer_handle_t buffer,
                                       const ndk::ScopedFileDescriptor& fence) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    mReadbackBuffer.set(buffer, fence);

    return HWC3::Error::Unsupported;
}

HWC3::Error Display::setVsyncEnabled(bool enabled) {
    DEBUG_LOG("%s: display:%" PRId64 " setting vsync %s", __FUNCTION__, mId,
              (enabled ? "on" : "off"));

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    return mVsyncThread.setVsyncEnabled(enabled);
}

HWC3::Error Display::setIdleTimerEnabled(int32_t timeoutMs) {
    DEBUG_LOG("%s: display:%" PRId64 " timeout:%" PRId32, __FUNCTION__, mId, timeoutMs);

    (void)timeoutMs;

    return HWC3::Error::Unsupported;
}

HWC3::Error Display::setColorTransform(const std::vector<float>& transformMatrix) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    if (transformMatrix.size() < 16) {
        ALOGE("%s: display:%" PRId64 " has non 4x4 matrix, size:%zu", __FUNCTION__, mId,
              transformMatrix.size());
        return HWC3::Error::BadParameter;
    }

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    auto& colorTransform = mColorTransform.emplace();
    std::copy_n(transformMatrix.data(), colorTransform.size(), colorTransform.begin());

    return HWC3::Error::None;
}

HWC3::Error Display::setBrightness(float brightness) {
    DEBUG_LOG("%s: display:%" PRId64 " brightness:%f", __FUNCTION__, mId, brightness);

    if (brightness < 0.0f) {
        ALOGE("%s: display:%" PRId64 " invalid brightness:%f", __FUNCTION__, mId, brightness);
        return HWC3::Error::BadParameter;
    }

    mComposer->setDisplayBrightness(this, brightness);

    return HWC3::Error::None;
}

HWC3::Error Display::setClientTarget(buffer_handle_t buffer, const ndk::ScopedFileDescriptor& fence,
                                     common::Dataspace /*dataspace*/,
                                     const std::vector<common::Rect>& /*damage*/) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    mClientTarget.set(buffer, fence);

    mComposer->onDisplayClientTargetSet(this);
    return HWC3::Error::None;
}

HWC3::Error Display::setOutputBuffer(buffer_handle_t /*buffer*/,
                                     const ndk::ScopedFileDescriptor& /*fence*/) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    // TODO: for virtual display
    return HWC3::Error::None;
}

HWC3::Error Display::setExpectedPresentTime(
        const std::optional<ClockMonotonicTimestamp>& expectedPresentTime) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    if (!expectedPresentTime.has_value()) {
        return HWC3::Error::None;
    }

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    mExpectedPresentTime.emplace(asTimePoint(expectedPresentTime->timestampNanos));

    return HWC3::Error::None;
}

HWC3::Error Display::validate(DisplayChanges* outChanges) {
    ATRACE_CALL();

    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    mPendingChanges.reset();

    mOrderedLayers.clear();
    mOrderedLayers.reserve(mLayers.size());
    for (auto& [_, layerPtr] : mLayers) {
        mOrderedLayers.push_back(layerPtr.get());
    }
    std::sort(mOrderedLayers.begin(), mOrderedLayers.end(),
              [](const Layer* layerA, const Layer* layerB) {
                  const auto zA = layerA->getZOrder();
                  const auto zB = layerB->getZOrder();
                  if (zA != zB) {
                      return zA < zB;
                  }
                  return layerA->getId() < layerB->getId();
              });

    if (mComposer == nullptr) {
        ALOGE("%s: display:%" PRId64 " missing composer", __FUNCTION__, mId);
        return HWC3::Error::NoResources;
    }

    HWC3::Error error = mComposer->validateDisplay(this, &mPendingChanges);
    if (error != HWC3::Error::None) {
        ALOGE("%s: display:%" PRId64 " failed to validate", __FUNCTION__, mId);
        return error;
    }

    if (mPendingChanges.hasAnyChanges()) {
        mPresentFlowState = PresentFlowState::WAITING_FOR_ACCEPT;
        DEBUG_LOG("%s: display:%" PRId64 " now WAITING_FOR_ACCEPT", __FUNCTION__, mId);
    } else {
        mPresentFlowState = PresentFlowState::WAITING_FOR_PRESENT;
        DEBUG_LOG("%s: display:%" PRId64 " now WAITING_FOR_PRESENT", __FUNCTION__, mId);
    }

    *outChanges = mPendingChanges;
    return HWC3::Error::None;
}

HWC3::Error Display::acceptChanges() {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    switch (mPresentFlowState) {
        case PresentFlowState::WAITING_FOR_VALIDATE: {
            ALOGE("%s: display %" PRId64 " failed, not validated", __FUNCTION__, mId);
            return HWC3::Error::NotValidated;
        }
        case PresentFlowState::WAITING_FOR_ACCEPT:
        case PresentFlowState::WAITING_FOR_PRESENT: {
            break;
        }
    }

    if (mPendingChanges.compositionChanges) {
        const ChangedCompositionTypes& compositionChanges = *mPendingChanges.compositionChanges;
        for (const ChangedCompositionLayer& compositionChange : compositionChanges.layers) {
            const auto layerId = compositionChange.layer;
            const auto layerComposition = compositionChange.composition;
            auto* layer = getLayer(layerId);
            if (layer == nullptr) {
                ALOGE("%s: display:%" PRId64 " layer:%" PRId64 " dropped before acceptChanges()?",
                      __FUNCTION__, mId, layerId);
                continue;
            }

            layer->setCompositionType(layerComposition);
        }
    }
    mPendingChanges.reset();

    mPresentFlowState = PresentFlowState::WAITING_FOR_PRESENT;
    DEBUG_LOG("%s: display:%" PRId64 " now WAITING_FOR_PRESENT", __FUNCTION__, mId);

    return HWC3::Error::None;
}

HWC3::Error Display::present(
        ::android::base::unique_fd* outDisplayFence,
        std::unordered_map<int64_t, ::android::base::unique_fd>* outLayerFences) {
    ATRACE_CALL();

    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    outDisplayFence->reset();
    outLayerFences->clear();

    std::unique_lock<std::recursive_mutex> lock(mStateMutex);

    switch (mPresentFlowState) {
        case PresentFlowState::WAITING_FOR_VALIDATE: {
            ALOGE("%s: display %" PRId64 " failed, not validated", __FUNCTION__, mId);
            return HWC3::Error::NotValidated;
        }
        case PresentFlowState::WAITING_FOR_ACCEPT: {
            ALOGE("%s: display %" PRId64 " failed, changes not accepted", __FUNCTION__, mId);
            return HWC3::Error::NotValidated;
        }
        case PresentFlowState::WAITING_FOR_PRESENT: {
            break;
        }
    }
    mPresentFlowState = PresentFlowState::WAITING_FOR_VALIDATE;
    DEBUG_LOG("%s: display:%" PRId64 " now WAITING_FOR_VALIDATE", __FUNCTION__, mId);

    if (mComposer == nullptr) {
        ALOGE("%s: display:%" PRId64 " missing composer", __FUNCTION__, mId);
        return HWC3::Error::NoResources;
    }

    return mComposer->presentDisplay(this, outDisplayFence, outLayerFences);
}

bool Display::hasConfig(int32_t configId) const {
    return mConfigs.find(configId) != mConfigs.end();
}

DisplayConfig* Display::getConfig(int32_t configId) {
    auto it = mConfigs.find(configId);
    if (it != mConfigs.end()) {
        return &it->second;
    }
    return nullptr;
}

HWC3::Error Display::setEdid(std::vector<uint8_t> edid) {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    mEdid = std::move(edid);
    return HWC3::Error::None;
}

void Display::setLegacyEdid() {
    static constexpr const std::array<uint8_t, 128> defaultEdid = {
            // Basic info of the default edid:
            // Vendor ID: NXP, Product ID: 0, Serial Number: 0, Mfg Week: 1, Mfg Year: 2019
            // EDID Structure Version: 1.3, Monitor Name: NXP Android
            0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x3B, 0x10, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x01, 0x1D, 0x01, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
            0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
            0x01, 0x01, 0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFC, 0x00, 0x4E,
            0x58, 0x50, 0x20, 0x41, 0x6E, 0x64, 0x72, 0x6F, 0x69, 0x64, 0x0A, 0x0A, 0x00,
            0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E,
    };
    mEdid.clear();
    mEdid.insert(mEdid.end(), defaultEdid.begin(), defaultEdid.end());
    // TODO: update the EDID according to display ID
    // const uint32_t size = mEdid.size();
    // Update the name to NXP Android_<mID>
    // mEdid[size - 3] = '0' + (uint8_t)mId;
    // Update the checksum byte
    // uint8_t checksum = -(uint8_t)std::accumulate(mEdid.data(), mEdid.data() + size - 1,
    //                                              static_cast<uint8_t>(0));
    // mEdid[size - 1] = checksum;
}

Layer* Display::getLayer(int64_t layerId) {
    auto it = mLayers.find(layerId);
    if (it == mLayers.end()) {
        ALOGE("%s Unknown layer:%" PRId64, __FUNCTION__, layerId);
        return nullptr;
    }

    return it->second.get();
}

buffer_handle_t Display::waitAndGetClientTargetBuffer() {
    DEBUG_LOG("%s: display:%" PRId64, __FUNCTION__, mId);

    ::android::base::unique_fd fence = mClientTarget.getFence();
    if (fence.ok()) {
        int err = sync_wait(fence.get(), 3000);
        if (err < 0 && errno == ETIME) {
            ALOGE("%s waited on fence %" PRId32 " for 3000 ms", __FUNCTION__, fence.get());
        }
    }

    return mClientTarget.getBuffer();
}

} // namespace aidl::android::hardware::graphics::composer3::impl
