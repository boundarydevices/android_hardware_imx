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

#pragma once

#include <mutex>
#include <vector>

#include "core-impl/DevicePortProxy.h"
#include "core-impl/Stream.h"
#include "tinycompress/tinycompress.h"
#include <sound/compress_offload.h>
#include <sound/compress_params.h>

namespace aidl::android::hardware::audio::core {

class StreamCompress : public StreamCommonImpl {
  public:
    StreamCompress(
            StreamContext* context, const Metadata& metadata,
            const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo>&
                    offloadInfo);
    ~StreamCompress();
    // Methods of 'DriverInterface'.
    ::android::status_t init() override;
    ::android::status_t drain(StreamDescriptor::DrainMode) override;
    ::android::status_t flush() override;
    ::android::status_t pause() override;
    ::android::status_t standby() override;
    ::android::status_t start() override;
    ::android::status_t transfer(void* buffer, size_t frameCount, size_t* actualFrameCount,
                                 int32_t* latencyMs) override;
    ::android::status_t refinePosition(StreamDescriptor::Position* position) override;

    void shutdown() override;

    // Overridden methods of 'StreamCommonImpl', called on a Binder thread.
    ndk::ScopedAStatus updateMetadataCommon(const Metadata& metadata) override;
    ndk::ScopedAStatus prepareToClose() override;

    void stop();

  private:
    const size_t mFrameSizeBytes;
    const bool mIsInput;
    const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo> mOffloadInfo;
    struct compr_config mConfig;
    struct compress* mProxy;
    bool mStarted;
};

class StreamOutCompress final : public StreamOut, public StreamCompress {
  public:
    StreamOutCompress(
            StreamContext&& context,
            const ::aidl::android::hardware::audio::common::SourceMetadata& sourceMetadata,
            const std::optional<::aidl::android::media::audio::common::AudioOffloadInfo>&
                    offloadInfo);

  private:
    void onClose(StreamDescriptor::State) override { defaultOnClose(); }
};

}  // namespace aidl::android::hardware::audio::core
