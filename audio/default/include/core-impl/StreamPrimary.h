/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include <vector>
#include <core-impl/AudioCardManager.h>

#include "StreamAlsa.h"
#include "StreamSwitcher.h"

namespace aidl::android::hardware::audio::core {

class StreamPrimary : public StreamAlsa {
  public:
    StreamPrimary(StreamContext* context, const Metadata& metadata,
                  const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices);

    ::android::status_t pause() override;
    ::android::status_t start() override;
    ::android::status_t standby() override;
    void shutdown() override;
    ::android::status_t transfer(void* buffer, size_t frameCount, size_t* actualFrameCount,
                                 int32_t* latencyMs) override;
    ::android::status_t refinePosition(StreamDescriptor::Position* position) override;

  protected:
    std::vector<alsa::DeviceProfile> getDeviceProfiles() override;

    const bool mIsAsynchronous;
    int64_t mStartTimeNs = 0;
    int16_t mStartRetryCount = 0;
    const int16_t kMaxStartRetryCount = 8;
    long mFramesSinceStart = 0;
    bool mSkipNextTransfer = false;
    bool mIsStereoToMono = false;
    bool mIsS32ToS16 = false;
    bool mIsS16ToS24 = false;
    bool mHardwarePause = false;
    bool mStarted = false;
    bool mPrimaryOutput = false;
    bool mDirectOutput = false;
    struct audio_card *mCard = NULL;
    std::optional<struct pcm_config> mSavedConfig;

  private:
    /*
      Enable audio dump feature:
        setprop persist.vendor.audio.dump 1
        touch /data/out.pcm
        touch /data/in.pcm
        chmod 777 /data/out.pcm
        chmod 777 /data/in.pcm
      Each boot:
        setenforce 0
        pkill audioserver
    */
    bool mDump = false;
    const char* kDumpOutputFile = "/data/out.pcm";
    const char* kDumpInputFile = "/data/in.pcm";
    void dump(const void *buffer, size_t size, const char* name);
    void tryStart();
    void stop();

  private:
    static std::pair<int, int> getCardAndDeviceId(
            const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices);
    const std::pair<int, int> mCardAndDeviceId;
};

class StreamInPrimary final : public StreamIn, public StreamSwitcher, public StreamInHwGainHelper {
  public:
    friend class ndk::SharedRefBase;
    StreamInPrimary(
            StreamContext&& context,
            const ::aidl::android::hardware::audio::common::SinkMetadata& sinkMetadata,
            const std::vector<::aidl::android::media::audio::common::MicrophoneInfo>& microphones);

  private:
    static bool useStubStream(const ::aidl::android::media::audio::common::AudioDevice& device);

    DeviceSwitchBehavior switchCurrentStream(
            const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices)
            override;
    std::unique_ptr<StreamCommonInterfaceEx> createNewStream(
            const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices,
            StreamContext* context, const Metadata& metadata) override;
    void onClose(StreamDescriptor::State) override { defaultOnClose(); }

    ndk::ScopedAStatus getHwGain(std::vector<float>* _aidl_return) override;
    ndk::ScopedAStatus setHwGain(const std::vector<float>& in_channelGains) override;
};

class StreamOutPrimary final : public StreamOut,
                               public StreamSwitcher,
                               public StreamOutHwVolumeHelper {
  public:
    friend class ndk::SharedRefBase;
    StreamOutPrimary(StreamContext&& context,
                     const ::aidl::android::hardware::audio::common::SourceMetadata& sourceMetadata,
                     const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo>&
                             offloadInfo);

  private:
    static bool useStubStream(const ::aidl::android::media::audio::common::AudioDevice& device);

    DeviceSwitchBehavior switchCurrentStream(
            const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices)
            override;
    std::unique_ptr<StreamCommonInterfaceEx> createNewStream(
            const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices,
            StreamContext* context, const Metadata& metadata) override;
    void onClose(StreamDescriptor::State) override { defaultOnClose(); }

    ndk::ScopedAStatus getHwVolume(std::vector<float>* _aidl_return) override;
    ndk::ScopedAStatus setHwVolume(const std::vector<float>& in_channelVolumes) override;

    ndk::ScopedAStatus setConnectedDevices(
            const std::vector<::aidl::android::media::audio::common::AudioDevice>& devices)
            override;
};

}  // namespace aidl::android::hardware::audio::core
