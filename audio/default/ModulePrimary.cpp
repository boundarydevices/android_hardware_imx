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

#include <vector>

#define LOG_TAG "AHAL_ModulePrimary"
#include <Utils.h>
#include <android-base/logging.h>
#include <media/stagefright/foundation/MediaDefs.h>

#include "core-impl/AudioCardManager.h"
#include "core-impl/ModulePrimary.h"
#include "core-impl/StreamCompress.h"
#include "core-impl/StreamPrimary.h"
#include "core-impl/Telephony.h"

using aidl::android::hardware::audio::common::SinkMetadata;
using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::media::audio::common::AudioPort;
using aidl::android::media::audio::common::AudioPortConfig;
using aidl::android::media::audio::common::MicrophoneInfo;

namespace aidl::android::hardware::audio::core {

ModulePrimary::ModulePrimary(std::unique_ptr<Configuration>&& config)
    : Module(Type::DEFAULT, std::move(config)) {
    AudioCardManager::init();
}

ModulePrimary::~ModulePrimary() {
    AudioCardManager::release();
}

ndk::ScopedAStatus ModulePrimary::getTelephony(std::shared_ptr<ITelephony>* _aidl_return) {
    if (!mTelephony) {
        mTelephony = ndk::SharedRefBase::make<Telephony>();
    }
    *_aidl_return = mTelephony.getInstance();
    LOG(DEBUG) << __func__
               << ": returning instance of ITelephony: " << _aidl_return->get()->asBinder().get();
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus ModulePrimary::createInputStream(StreamContext&& context,
                                                    const SinkMetadata& sinkMetadata,
                                                    const std::vector<MicrophoneInfo>& microphones,
                                                    std::shared_ptr<StreamIn>* result) {
    return createStreamInstance<StreamInPrimary>(result, std::move(context), sinkMetadata,
                                                 microphones);
}

ndk::ScopedAStatus ModulePrimary::createOutputStream(
        StreamContext&& context, const SourceMetadata& sourceMetadata,
        const std::optional<AudioOffloadInfo>& offloadInfo, std::shared_ptr<StreamOut>* result) {
    if (context.getFormat().encoding == ::android::MEDIA_MIMETYPE_AUDIO_MPEG) {
        const auto& c = AudioCardManager::getCardForDevice(AUDIO_DEVICE_OUT_LINE);
        if (c && strstr(c->card_name, "sof")) {
            return createStreamInstance<StreamOutCompress>(result, std::move(context), sourceMetadata, offloadInfo);
        } else {
            LOG(INFO) << "reject creating compress offload stream.";
            return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);
        }
    }

    return createStreamInstance<StreamOutPrimary>(result, std::move(context), sourceMetadata,
                                                  offloadInfo);
}

int32_t ModulePrimary::getNominalLatencyMs(const AudioPortConfig&) {
    static constexpr int32_t kLatencyMs = 10;
    return kLatencyMs;
}

ndk::ScopedAStatus ModulePrimary::populateConnectedDevicePort(
        ::aidl::android::media::audio::common::AudioPort* audioPort, int32_t nextPortId) {
    LOG(INFO) << __func__ << ": " << audioPort->name << ", id: " << nextPortId;
    auto& audioDevice = audioPort->ext.get<aidl::android::media::audio::common::AudioPortExt::Tag::device>().device;
    const auto& c = AudioCardManager::getCardForDevice(audioDevice);
    if (!c)
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);

    if (audioDevice.type.type == ::aidl::android::media::audio::common::AudioDeviceType::OUT_DEVICE &&
            audioDevice.type.connection == "hdmi")
        return ndk::ScopedAStatus::fromExceptionCode(EX_ILLEGAL_STATE);

    return ndk::ScopedAStatus::ok();
}

}  // namespace aidl::android::hardware::audio::core
