/*
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

#include <algorithm>

#define LOG_TAG "AHAL_StreamCompress"
#include <Utils.h>
#include <android-base/logging.h>
#include <audio_utils/clock.h>

#include "core-impl/AudioCardManager.h"
#include "core-impl/StreamCompress.h"

using aidl::android::hardware::audio::common::SourceMetadata;
using aidl::android::media::audio::common::AudioOffloadInfo;
using aidl::android::hardware::audio::common::getChannelCount;

namespace aidl::android::hardware::audio::core {

#define COMPRESS_OFFLOAD_DEFAULT_CHANNELS   2
#define COMPRESS_OFFLOAD_FRAGMENT_SIZE      3940
#define COMPRESS_OFFLOAD_NUM_FRAGMENTS      2
#define COMPRESS_OFFLOAD_LATENCY_MS         96

StreamCompress::StreamCompress(StreamContext* context, const Metadata& metadata,
        const std::optional<AudioOffloadInfo>& offloadInfo)
    : StreamCommonImpl(context, metadata),
      mFrameSizeBytes(getContext().getFrameSize()),
      mIsInput(isInput(metadata)),
      mOffloadInfo(offloadInfo) {
    size_t channels = getChannelCount(offloadInfo->base.channelMask);
    mConfig.codec = new snd_codec();
    mConfig.codec->id = SND_AUDIOCODEC_MP3;
    mConfig.fragment_size = COMPRESS_OFFLOAD_FRAGMENT_SIZE;
    mConfig.fragments = COMPRESS_OFFLOAD_NUM_FRAGMENTS;
    mConfig.codec->sample_rate = offloadInfo->base.sampleRate;
    mConfig.codec->bit_rate = offloadInfo->bitRatePerSecond;
    if (channels) {
        mConfig.codec->ch_in = channels;
    } else {
        mConfig.codec->ch_in = COMPRESS_OFFLOAD_DEFAULT_CHANNELS;
    }
    mConfig.codec->ch_out = mConfig.codec->ch_in;
    mProxy = NULL;
    mStarted = false;
}

StreamCompress::~StreamCompress() {
    if (mConfig.codec) {
        delete mConfig.codec;
    }
}

::android::status_t StreamCompress::init() {
    return ::android::OK;
}

::android::status_t StreamCompress::drain(StreamDescriptor::DrainMode) {
    return ::android::OK;
}

::android::status_t StreamCompress::flush() {
    LOG(INFO) << __func__;
    stop();
    return ::android::OK;
}

::android::status_t StreamCompress::pause() {
    int ret = 0;
    if (mProxy) {
        ret = compress_pause(mProxy);
    }
    LOG(INFO) << __func__ << ": " << ret;
    return ::android::OK;
}

::android::status_t StreamCompress::transfer(void* buffer, size_t frameCount,
                                              size_t* actualFrameCount, int32_t* latencyMs) {
    *latencyMs = COMPRESS_OFFLOAD_LATENCY_MS;
    int ret = compress_write(mProxy, buffer, frameCount);
    if (ret > 0 && !mStarted) {
        compress_start(mProxy);
        mStarted = true;
    }
    *actualFrameCount = ret;
    return ::android::OK;
}

ndk::ScopedAStatus StreamCompress::prepareToClose() {
    return ndk::ScopedAStatus::ok();
}

::android::status_t StreamCompress::standby() {
    LOG(INFO) << __func__;
    stop();
    if (mProxy) {
        compress_close(mProxy);
        mProxy = NULL;
    }
    return ::android::OK;
}

void StreamCompress::stop() {
    LOG(INFO) << __func__;
    mStarted = false;
    if (mProxy) {
        compress_stop(mProxy);
    }
}

::android::status_t StreamCompress::start() {
    if (mProxy) {
        int ret = compress_resume(mProxy);
        LOG(INFO) << __func__ << ": compress resume: " << ret;
        return ::android::OK;
    }
    const ConnectedDevices& connectedDevices = getConnectedDevices();
    struct audio_card *card = AudioCardManager::getCardForDevice(connectedDevices[0]);
    if (!card) {
        LOG(INFO) << __func__ << ": no devices";
        return ::android::NO_INIT;
    }

    LOG(INFO) << "  channels: " << mConfig.codec->ch_in;
    LOG(INFO) << "  rate: " << mConfig.codec->sample_rate;
    LOG(INFO) << "  fragment size: " << mConfig.fragment_size;
    LOG(INFO) << "  fragment count: " << mConfig.fragments;
    LOG(INFO) << "  id: " << mConfig.codec->id;
    LOG(INFO) << "  proxy_open(card: " << card->card << " device: 0 COMPRESS)";

    mProxy = compress_open(card->card, 0, COMPRESS_IN, &mConfig);
    if (mProxy && !is_compress_ready(mProxy)) {
        LOG(ERROR) << __func__ << ": " << compress_get_error(mProxy);
        compress_close(mProxy);
        mProxy = NULL;
        return ::android::NO_INIT;
    }

    LOG(INFO) << __func__;
    return ::android::OK;
}

void StreamCompress::shutdown() {
    LOG(INFO) << __func__;
    StreamCompress::standby();
}

ndk::ScopedAStatus StreamCompress::updateMetadataCommon(const Metadata& metadata) {
    auto a = metadata;
    LOG(INFO) << __func__ ;
    return ndk::ScopedAStatus::ok();
}

::android::status_t StreamCompress::refinePosition(StreamDescriptor::Position* position) {
    if (!mProxy) {
        LOG(WARNING) << __func__ << ": no opened devices";
        return ::android::NO_INIT;
    }
    unsigned long dsp_frames;
    unsigned int rate;
    struct timespec timestamp;
    compress_get_tstamp(mProxy, &dsp_frames, &rate);
    position->frames = dsp_frames;
    clock_gettime(CLOCK_MONOTONIC, &timestamp);
    position->timeNs = audio_utils_ns_from_timespec(&timestamp);

    return ::android::OK;
}

// static
StreamOutCompress::StreamOutCompress(StreamContext&& context,
                                       const SourceMetadata& sourceMetadata,
                                       const std::optional<AudioOffloadInfo>& offloadInfo)
    : StreamOut(std::move(context), offloadInfo),
      StreamCompress(&mContextInstance, sourceMetadata, offloadInfo) {}

}  // namespace aidl::android::hardware::audio::core
