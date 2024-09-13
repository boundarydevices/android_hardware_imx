/*
 * Copyright (C) 2023 The Android Open Source Project
 * Copyright 2024 NXP
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

#define LOG_TAG "AHAL_StreamPrimary"
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <audio_utils/clock.h>
#include <error/Result.h>
#include <error/expected_utils.h>

#include "PrimaryMixer.h"
#include "core-impl/AudioCardManager.h"
#include "core-impl/StreamPrimary.h"
#include "core-impl/StreamStub.h"

extern "C" {
#include "alsa_device_profile.h"
}

using aidl::android::hardware::audio::common::SinkMetadata;
using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::media::audio::common::AudioDevice;
using aidl::android::media::audio::common::AudioDeviceDescription;
using aidl::android::media::audio::common::AudioDeviceType;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::media::audio::common::MicrophoneInfo;
using android::base::GetBoolProperty;

namespace aidl::android::hardware::audio::core {

StreamPrimary::StreamPrimary(StreamContext* context, const Metadata& metadata)
    : StreamAlsa(context, metadata, 3 /*readWriteRetries*/),
      mIsAsynchronous(!!getContext().getAsyncCallback()) {
    context->startStreamDataProcessor();
}

::android::status_t StreamPrimary::start() {
    RETURN_STATUS_IF_ERROR(StreamAlsa::start());
    mStartTimeNs = ::android::uptimeNanos();
    mFramesSinceStart = 0;
    mSkipNextTransfer = false;
    return ::android::OK;
}

::android::status_t StreamPrimary::transfer(void* buffer, size_t frameCount,
                                            size_t* actualFrameCount, int32_t* latencyMs) {
    RETURN_STATUS_IF_ERROR(
            StreamAlsa::transfer(buffer, frameCount, actualFrameCount, latencyMs));
    return ::android::OK;
}

::android::status_t StreamPrimary::refinePosition(StreamDescriptor::Position*) {
    // Since not all data is actually sent to the HAL, use the position maintained by Stream class
    // which accounts for all frames passed from / to the client.
    return ::android::OK;
}

std::vector<alsa::DeviceProfile> StreamPrimary::getDeviceProfiles() {
    std::vector<alsa::DeviceProfile> deviceProfile{
        alsa::DeviceProfile{.card = 0,
            .device = 0,
            .direction = mIsInput ? PCM_IN : PCM_OUT,
            .isExternal = false}};
    const ConnectedDevices& connectedDevices = getConnectedDevices();
    if (connectedDevices.size() > 1)
        LOG(WARNING) << __func__ << ": size of ConnectedDevices is larger than 1";

    struct audio_card *card = AudioCardManager::getCardForDevice(connectedDevices[0]);
    if (card) {
        deviceProfile[0].card = card->card;
    }

    return deviceProfile;
}

StreamInPrimary::StreamInPrimary(StreamContext&& context, const SinkMetadata& sinkMetadata,
                                 const std::vector<MicrophoneInfo>& microphones)
    : StreamIn(std::move(context), microphones),
      StreamSwitcher(&mContextInstance, sinkMetadata),
      StreamInHwGainHelper(&mContextInstance) {}

bool StreamInPrimary::useStubStream(const AudioDevice& device) {
    static const bool kSimulateInput =
            GetBoolProperty("ro.boot.audio.tinyalsa.simulate_input", false);
    return kSimulateInput || device.type.type == AudioDeviceType::IN_TELEPHONY_RX ||
           device.type.type == AudioDeviceType::IN_FM_TUNER ||
           device.type.connection == AudioDeviceDescription::CONNECTION_BUS /*deprecated */ ||
           (device.type.type == AudioDeviceType::IN_BUS && device.type.connection.empty());
}

StreamSwitcher::DeviceSwitchBehavior StreamInPrimary::switchCurrentStream(
        const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices) {
    LOG(DEBUG) << __func__;
    if (devices.size() > 1) {
        LOG(ERROR) << __func__ << ": primary stream can only be connected to one device, got: "
                   << devices.size();
        return DeviceSwitchBehavior::UNSUPPORTED_DEVICES;
    }
    if (devices.empty() || useStubStream(devices[0]) == isStubStream()) {
        return DeviceSwitchBehavior::USE_CURRENT_STREAM;
    }
    return DeviceSwitchBehavior::CREATE_NEW_STREAM;
}

std::unique_ptr<StreamCommonInterfaceEx> StreamInPrimary::createNewStream(
        const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices,
        StreamContext* context, const Metadata& metadata) {
    if (devices.empty()) {
        LOG(FATAL) << __func__ << ": called with empty devices";  // see 'switchCurrentStream'
    }
    if (useStubStream(devices[0])) {
        return std::unique_ptr<StreamCommonInterfaceEx>(
                new InnerStreamWrapper<StreamStub>(context, metadata));
    }
    return std::unique_ptr<StreamCommonInterfaceEx>(
            new InnerStreamWrapper<StreamPrimary>(context, metadata));
}

ndk::ScopedAStatus StreamInPrimary::getHwGain(std::vector<float>* _aidl_return) {
    if (isStubStream()) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    float gain;
    RETURN_STATUS_IF_ERROR(primary::PrimaryMixer::getInstance().getMicGain(&gain));
    _aidl_return->resize(0);
    _aidl_return->resize(mChannelCount, gain);
    RETURN_STATUS_IF_ERROR(setHwGainImpl(*_aidl_return));
    return getHwGainImpl(_aidl_return);
}

ndk::ScopedAStatus StreamInPrimary::setHwGain(const std::vector<float>& in_channelGains) {
    if (isStubStream()) {
        LOG(DEBUG) << __func__ << ": gains " << ::android::internal::ToString(in_channelGains);
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    auto currentGains = mHwGains;
    RETURN_STATUS_IF_ERROR(setHwGainImpl(in_channelGains));
    if (in_channelGains.size() < 1) {
        LOG(FATAL) << __func__ << ": unexpected gain vector size: " << in_channelGains.size();
    }
    if (auto status = primary::PrimaryMixer::getInstance().setMicGain(in_channelGains[0]);
        !status.isOk()) {
        mHwGains = currentGains;
        return status;
    }
    return ndk::ScopedAStatus::ok();
}

StreamOutPrimary::StreamOutPrimary(StreamContext&& context, const SourceMetadata& sourceMetadata,
                                   const std::optional<AudioOffloadInfo>& offloadInfo)
    : StreamOut(std::move(context), offloadInfo),
      StreamSwitcher(&mContextInstance, sourceMetadata),
      StreamOutHwVolumeHelper(&mContextInstance) {}

bool StreamOutPrimary::useStubStream(const AudioDevice& device) {
    static const bool kSimulateOutput =
            GetBoolProperty("ro.boot.audio.tinyalsa.ignore_output", false);
    return kSimulateOutput || device.type.type == AudioDeviceType::OUT_TELEPHONY_TX ||
           device.type.connection == AudioDeviceDescription::CONNECTION_BUS /*deprecated*/ ||
           (device.type.type == AudioDeviceType::OUT_BUS && device.type.connection.empty());
}

StreamSwitcher::DeviceSwitchBehavior StreamOutPrimary::switchCurrentStream(
        const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices) {
    LOG(DEBUG) << __func__;
    if (devices.size() > 1) {
        LOG(ERROR) << __func__ << ": primary stream can only be connected to one device, got: "
                   << devices.size();
        return DeviceSwitchBehavior::UNSUPPORTED_DEVICES;
    }
    if (devices.empty() || useStubStream(devices[0]) == isStubStream()) {
        return DeviceSwitchBehavior::USE_CURRENT_STREAM;
    }
    return DeviceSwitchBehavior::CREATE_NEW_STREAM;
}

std::unique_ptr<StreamCommonInterfaceEx> StreamOutPrimary::createNewStream(
        const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices,
        StreamContext* context, const Metadata& metadata) {
    if (devices.empty()) {
        LOG(FATAL) << __func__ << ": called with empty devices";  // see 'switchCurrentStream'
    }
    if (useStubStream(devices[0])) {
        return std::unique_ptr<StreamCommonInterfaceEx>(
                new InnerStreamWrapper<StreamStub>(context, metadata));
    }
    return std::unique_ptr<StreamCommonInterfaceEx>(
            new InnerStreamWrapper<StreamPrimary>(context, metadata));
}

ndk::ScopedAStatus StreamOutPrimary::getHwVolume(std::vector<float>* _aidl_return) {
    if (isStubStream()) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    RETURN_STATUS_IF_ERROR(primary::PrimaryMixer::getInstance().getVolumes(_aidl_return));
    _aidl_return->resize(mChannelCount);
    RETURN_STATUS_IF_ERROR(setHwVolumeImpl(*_aidl_return));
    return getHwVolumeImpl(_aidl_return);
}

ndk::ScopedAStatus StreamOutPrimary::setHwVolume(const std::vector<float>& in_channelVolumes) {
    if (isStubStream()) {
        LOG(DEBUG) << __func__ << ": volumes " << ::android::internal::ToString(in_channelVolumes);
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    auto currentVolumes = mHwVolumes;
    RETURN_STATUS_IF_ERROR(setHwVolumeImpl(in_channelVolumes));
    if (auto status = primary::PrimaryMixer::getInstance().setVolumes(in_channelVolumes);
        !status.isOk()) {
        mHwVolumes = currentVolumes;
        return status;
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus StreamOutPrimary::setConnectedDevices(
        const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices) {
    if (!devices.empty()) {
        auto streamDataProcessor = mContextInstance.getStreamDataProcessor().lock();
        if (streamDataProcessor != nullptr) {
            streamDataProcessor->setAudioDevice(devices[0]);
        }
    }
    return StreamSwitcher::setConnectedDevices(devices);
}

}  // namespace aidl::android::hardware::audio::core
