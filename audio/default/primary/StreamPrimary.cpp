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

#include <cstdio>

#include <android-base/logging.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <audio_utils/clock.h>
#include <audio_utils/primitives.h>
#include <cutils/properties.h>
#include <error/Result.h>
#include <error/expected_utils.h>

#include "PrimaryMixer.h"
#include "core-impl/StreamPrimary.h"
#include "core-impl/StreamStub.h"

#include <cutils/properties.h>
#include <fstream>
#include "core-impl/AudioCardManager.h"
extern "C" {
#include "alsa_device_profile.h"
}

#define DEFAULT_PERIOD_COUNT 4
#define LPA_PERIOD_MS 500
#define LPA_BUFFER_SECOND 20

using aidl::android::hardware::audio::common::isBitPositionFlagSet;
using aidl::android::media::audio::common::AudioIoFlags;
using aidl::android::media::audio::common::AudioOutputFlags;

using aidl::android::hardware::audio::common::SinkMetadata;
using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::media::audio::common::AudioDevice;
using aidl::android::media::audio::common::AudioDeviceAddress;
using aidl::android::media::audio::common::AudioDeviceDescription;
using aidl::android::media::audio::common::AudioDeviceType;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::media::audio::common::MicrophoneInfo;
using android::base::GetBoolProperty;

namespace aidl::android::hardware::audio::core {

const static constexpr std::pair<int, int> kDefaultCardAndDeviceId = {
        primary::PrimaryMixer::kAlsaCard, primary::PrimaryMixer::kAlsaDevice};

StreamPrimary::StreamPrimary(
        StreamContext* context, const Metadata& metadata,
        const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices)
    : StreamAlsa(context, metadata, 3 /*readWriteRetries*/),
      mIsAsynchronous(!!getContext().getAsyncCallback()),
      mCardAndDeviceId(getCardAndDeviceId(devices)) {
    context->startStreamDataProcessor();
    mSavedConfig = mConfig;
    auto flags = getContext().getFlags();
    if (flags.getTag() == AudioIoFlags::Tag::output) {
        if (isBitPositionFlagSet(flags.template get<AudioIoFlags::Tag::output>(),
                               AudioOutputFlags::PRIMARY)) {
            mPrimaryOutput = true;
            mDirectOutput = false;
        } else if (isBitPositionFlagSet(flags.template get<AudioIoFlags::Tag::output>(),
                               AudioOutputFlags::DIRECT)) {
            mPrimaryOutput = false;
            mDirectOutput = true;
        }
    }
    ALOGD("%s: mPrimaryOutput: %d, mDirectOutput: %d", __func__, mPrimaryOutput, mDirectOutput);
    mDump = property_get_bool("persist.vendor.audio.dump", false);
    if (mDump) {
        std::ofstream ifile(kDumpInputFile, std::ios::trunc);
        std::ofstream ofile(kDumpOutputFile, std::ios::trunc);
    }
}

::android::status_t StreamPrimary::pause() {
    if (mHardwarePause && mStarted) {
        proxy_pause(mAlsaDeviceProxies[0].get());
    }
    return ::android::OK;
}

void StreamPrimary::dump(const void *buffer, size_t bytes, const char *name) {
    if ((buffer == NULL) || (bytes == 0) || (name == NULL))
        return;

    int fdDump = open(name, O_CREAT | O_APPEND | O_WRONLY, S_IRWXU | S_IRWXG);
    if (fdDump < 0) {
        ALOGW("%s: file open error, srcFile: %s, fd %d", __func__, name, fdDump);
        return;
    }

    write(fdDump, buffer, bytes);
    ::close(fdDump);

    return;
}

void StreamPrimary::tryStart(){
    auto status = StreamAlsa::start();
    if (status != ::android::OK) {
        mStarted = false;
    } else {
        mStarted = true;
    }
}

::android::status_t StreamPrimary::start() {
    if (!mAlsaDeviceProxies.empty()) {
        // This is a resume after a pause.
        if (mHardwarePause && mStarted) {
            proxy_resume(mAlsaDeviceProxies[0].get());
        }
        return ::android::OK;
    }
    mCard = AudioCardManager::getCardForDevice(getConnectedDevices().at(0));
    if (!mCard) {
        return ::android::NO_INIT;
    }
    if (mPrimaryOutput) {
        if (!mCard->locked) {
            tryStart();
        }
    } else {
        if (mDirectOutput) {
            mCard->locked = true;
            LOG(DEBUG) << __func__ << ": lock the card";
        }
        tryStart();
    }
    mStartTimeNs = ::android::uptimeNanos();
    mStartRetryCount = 0;
    mFramesSinceStart = 0;
    mSkipNextTransfer = false;
    return ::android::OK;
}

::android::status_t StreamPrimary::transfer(void* buffer, size_t frameCount,
                                            size_t* actualFrameCount, int32_t* latencyMs) {
    if (mPrimaryOutput) {
        if (mStarted && mCard->locked) {
            LOG(DEBUG) << __func__ << ": standby the primary stream to release the card.";
            standby();
        } else if (!mStarted && !mCard->locked) {
            tryStart();
        }
    } else if (mDirectOutput) {
        if (!mStarted) {
            if (mStartRetryCount < kMaxStartRetryCount) {
                tryStart();
                if (mStarted)
                    mStartRetryCount = 0;
                else {
                    mStartRetryCount ++;
                    if (mStartRetryCount >= kMaxStartRetryCount)
                        LOG(DEBUG) << __func__ << ": stop trying to start after " << mStartRetryCount << " times";
                }
            }
        }
        if (!mCard->locked) {
            LOG(WARNING) << __func__ << ": error state, direct transfer without lock.";
            mCard->locked = true;
        }
    }

    mFramesSinceStart += frameCount;
    if (!mStarted) {
        *actualFrameCount = frameCount;
        const long bufferDurationUs =
                (*actualFrameCount) * MICROS_PER_SECOND / mContext.getSampleRate();
        const auto totalDurationUs =
                (::android::uptimeNanos() - mStartTimeNs) / NANOS_PER_MICROSECOND;
        const long totalOffsetUs =
                mFramesSinceStart * MICROS_PER_SECOND / mContext.getSampleRate() - totalDurationUs;
        if (totalOffsetUs > 0) {
            const long sleepTimeUs = std::min(totalOffsetUs, bufferDurationUs);
            if (sleepTimeUs > 500000) {
                LOG(WARNING) << __func__ << ": sleeping for " << sleepTimeUs << " us";
            }
            usleep(sleepTimeUs);
        } else {
            LOG(WARNING) << __func__ << ": Wrong sleep time: totalOffsetUs " << totalOffsetUs
                << ", bufferDurationUs " << bufferDurationUs
                << ", totalDurationUs " << totalDurationUs
                << ", mFramesSinceStart " << mFramesSinceStart
                << ", actualFrameCount " << *actualFrameCount;
        }
        return ::android::OK;
    }

    if (mDump && mIsInput)
        dump(buffer, frameCount * mFrameSizeBytes, kDumpInputFile);
    else if (mDump && !mIsInput)
        dump(buffer, frameCount * mFrameSizeBytes, kDumpOutputFile);

    if (mIsStereoToMono) {
        if (mIsInput) {
            auto dst = static_cast<int16_t*>(buffer);
            std::unique_ptr<int16_t[]> src{new int16_t[frameCount]};

            RETURN_STATUS_IF_ERROR(
                    StreamAlsa::transfer(src.get(), frameCount / 2, actualFrameCount, latencyMs));
            upmix_to_stereo_i16_from_mono_i16(dst, src.get(), frameCount);
        } else {
            auto src = static_cast<const int16_t*>(buffer);
            std::unique_ptr<int16_t[]> dst{new int16_t[frameCount]};

            downmix_to_mono_i16_from_stereo_i16(dst.get(), src, frameCount);
            RETURN_STATUS_IF_ERROR(
                    StreamAlsa::transfer(dst.get(), frameCount / 2, actualFrameCount, latencyMs));
        }
        *actualFrameCount *= 2;

        return ::android::OK;
    }

    if (mIsS32ToS16) {
        auto channels = aidl::android::hardware::audio::common::getChannelCount(getContext().getChannelLayout());
        auto dst = static_cast<int16_t*>(buffer);
        std::unique_ptr<int32_t[]> src{new int32_t[frameCount * channels]};

        RETURN_STATUS_IF_ERROR(
                StreamAlsa::transfer(src.get(), frameCount * 2, actualFrameCount, latencyMs));
        memcpy_to_i16_from_i32(dst, src.get(), frameCount);

        *actualFrameCount /= 2;

        return ::android::OK;
    }

    if (mIsS16ToS24) {
        auto channels = aidl::android::hardware::audio::common::getChannelCount(getContext().getChannelLayout());
        auto src = static_cast<int16_t*>(buffer);
        std::unique_ptr<int32_t[]> dst{new int32_t[frameCount * channels]};

        memcpy_to_q8_23_from_i16(dst.get(), src, frameCount * channels);
        RETURN_STATUS_IF_ERROR(
                StreamAlsa::transfer(dst.get(), frameCount * channels, actualFrameCount, latencyMs));

        *actualFrameCount /= 2;

        return ::android::OK;
    }

    RETURN_STATUS_IF_ERROR(
            StreamAlsa::transfer(buffer, frameCount, actualFrameCount, latencyMs));
    return ::android::OK;
}

void StreamPrimary::stop() {
    if (mDirectOutput && mCard) {
        mCard->locked = false;
        LOG(DEBUG) << __func__ << ": unlock the card.";
    }
    mStarted = false;
}

::android::status_t StreamPrimary::standby() {
    StreamAlsa::standby();
    stop();
    return ::android::OK;
}

void StreamPrimary::shutdown() {
    StreamAlsa::shutdown();
    stop();
}

::android::status_t StreamPrimary::refinePosition(StreamDescriptor::Position* position) {
    if ((property_get_int32("vendor.audio.lpa.enable", 0) && mDirectOutput) ||
            (getContext().getFormat().encoding == "audio/vnd.sony.dsd")) {
        return StreamAlsa::refinePosition(position);
    }
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

        if (strstr(card->driver_name, "sco-audio")) {
            if (mSavedConfig.has_value() && mSavedConfig->channels == 2) {
                mConfig->channels = 1;
                mIsStereoToMono = true;
                LOG(INFO) << __func__ << ": Force set mono channel for bt_sco";
            }
        } else if (strstr(card->driver_name, "micfil") && !card->support_s16) {
            mConfig->format = PCM_FORMAT_S32_LE;
            mIsS32ToS16 = true;
            LOG(INFO) << __func__ << ": Force set S32 format for micfil";
        } else if (mIsStereoToMono || mIsS32ToS16) {
            mIsStereoToMono = false;
            mIsS32ToS16 = false;
            mConfig = mSavedConfig;
        }

        char soc_name[PROPERTY_VALUE_MAX];
        property_get("ro.boot.soc_type", soc_name, NULL);
        if ((property_get_int32("vendor.persist.audio.pass.through", 0) == 2000) &&
                ((0 == strcmp(soc_name, "imx8mp")) || (0 == strcmp(soc_name, "imx8ulp")))) {
            mIsS16ToS24 = true;
            LOG(INFO) << __func__ << ": Force set S24 format for passthrough on imx8mp/imx8ulp";
            mConfig->format = PCM_FORMAT_S24_LE;
        } else if (mIsS16ToS24) {
            mIsS16ToS24 = false;
            mConfig = mSavedConfig;
        }

        mConfig->period_size = mBufferSizeFrames;
        mConfig->period_count = DEFAULT_PERIOD_COUNT;

        if (card->out_period_size) {
            mConfig->period_size = card->out_period_size;
        }
        if (card->out_period_count) {
            mConfig->period_count = card->out_period_count;
        }

        if (property_get_int32("vendor.audio.lpa.enable", 0) && mDirectOutput) {
            mConfig->period_size = mConfig->rate * LPA_PERIOD_MS / 1000;
            mConfig->period_count = LPA_BUFFER_SECOND * 1000 / LPA_PERIOD_MS;
            mHardwarePause = true;
            LOG(INFO) << __func__ << ": Force set period size as " << LPA_PERIOD_MS << "ms for LPA";
        }
    }

    return deviceProfile;
}

std::pair<int, int> StreamPrimary::getCardAndDeviceId(const std::vector<AudioDevice>& devices) {
    if (devices.empty() || devices[0].address.getTag() != AudioDeviceAddress::id) {
        return kDefaultCardAndDeviceId;
    }
    std::string deviceAddress = devices[0].address.get<AudioDeviceAddress::id>();
    std::pair<int, int> cardAndDeviceId;
    if (const size_t suffixPos = deviceAddress.rfind("CARD_");
        suffixPos == std::string::npos ||
        sscanf(deviceAddress.c_str() + suffixPos, "CARD_%d_DEV_%d", &cardAndDeviceId.first,
               &cardAndDeviceId.second) != 2) {
        return kDefaultCardAndDeviceId;
    }
    LOG(DEBUG) << __func__ << ": parsed with card id " << cardAndDeviceId.first << ", device id "
               << cardAndDeviceId.second;
    return cardAndDeviceId;
}

StreamInPrimary::StreamInPrimary(StreamContext&& context, const SinkMetadata& sinkMetadata,
                                 const std::vector<MicrophoneInfo>& microphones)
    : StreamIn(std::move(context), microphones),
      StreamSwitcher(&mContextInstance, sinkMetadata),
      StreamInHwGainHelper(&mContextInstance) {}

bool StreamInPrimary::useStubStream(const AudioDevice& device) {
    static const bool kSimulateInput =
            GetBoolProperty("ro.boot.audio.tinyalsa.simulate_input", false);
    if (device.type.type == AudioDeviceType::IN_HEADSET &&
            device.type.connection == AudioDeviceDescription::CONNECTION_BT_SCO)
        return false;

    return kSimulateInput || device.type.type == AudioDeviceType::IN_TELEPHONY_RX ||
           device.type.type == AudioDeviceType::IN_FM_TUNER ||
           device.type.connection == AudioDeviceDescription::CONNECTION_BUS /*deprecated */;
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
            new InnerStreamWrapper<StreamPrimary>(context, metadata, devices));
}

ndk::ScopedAStatus StreamInPrimary::getHwGain(std::vector<float>* _aidl_return) {
    if (isStubStream()) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    if (mHwGains.empty()) {
        float gain;
        RETURN_STATUS_IF_ERROR(primary::PrimaryMixer::getInstance().getMicGain(&gain));
        _aidl_return->resize(mChannelCount, gain);
        RETURN_STATUS_IF_ERROR(setHwGainImpl(*_aidl_return));
    }
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
    float gain;
    RETURN_STATUS_IF_ERROR(primary::PrimaryMixer::getInstance().getMicGain(&gain));
    // Due to rounding errors, round trip conversions between percents and indexed values may not
    // match.
    if (gain != in_channelGains[0]) {
        LOG(WARNING) << __func__ << ": unmatched gain: set: " << in_channelGains[0]
                     << ", from mixer: " << gain;
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
           device.type.connection == AudioDeviceDescription::CONNECTION_BUS /*deprecated*/;
}

StreamSwitcher::DeviceSwitchBehavior StreamOutPrimary::switchCurrentStream(
        const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices) {
    LOG(DEBUG) << __func__;
    if (devices.size() > 1) {
        LOG(ERROR) << __func__ << ": primary stream can only be connected to one device, got: "
                   << devices.size();
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
            new InnerStreamWrapper<StreamPrimary>(context, metadata, devices));
}

ndk::ScopedAStatus StreamOutPrimary::getHwVolume(std::vector<float>* _aidl_return) {
    if (isStubStream()) {
        return ndk::ScopedAStatus::fromExceptionCode(EX_UNSUPPORTED_OPERATION);
    }
    if (mHwVolumes.empty()) {
        RETURN_STATUS_IF_ERROR(primary::PrimaryMixer::getInstance().getVolumes(_aidl_return));
        _aidl_return->resize(mChannelCount);
        RETURN_STATUS_IF_ERROR(setHwVolumeImpl(*_aidl_return));
    }
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
    std::vector<float> volumes;
    RETURN_STATUS_IF_ERROR(primary::PrimaryMixer::getInstance().getVolumes(&volumes));
    // Due to rounding errors, round trip conversions between percents and indexed values may not
    // match.
    if (volumes != in_channelVolumes) {
        LOG(WARNING) << __func__ << ": unmatched volumes: set: "
                     << ::android::internal::ToString(in_channelVolumes)
                     << ", from mixer: " << ::android::internal::ToString(volumes);
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
