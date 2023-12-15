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

#ifndef ANDROID_HWC_VSYNCTHREAD_H
#define ANDROID_HWC_VSYNCTHREAD_H

#include <aidl/android/hardware/graphics/composer3/VsyncPeriodChangeConstraints.h>
#include <aidl/android/hardware/graphics/composer3/VsyncPeriodChangeTimeline.h>
#include <android/hardware/graphics/common/1.0/types.h>

#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

#include "Common.h"

namespace aidl::android::hardware::graphics::composer3::impl {

class Display;
// Generates Vsync signals in software.
class VsyncThread {
public:
    VsyncThread(Display* display);
    virtual ~VsyncThread();

    VsyncThread(const VsyncThread&) = delete;
    VsyncThread& operator=(const VsyncThread&) = delete;

    VsyncThread(VsyncThread&&) = delete;
    VsyncThread& operator=(VsyncThread&&) = delete;

    HWC3::Error start(int32_t periodNanos);

    HWC3::Error setCallbacks(const std::shared_ptr<IComposerCallback>& callback);

    HWC3::Error setVsyncEnabled(bool enabled);

    HWC3::Error scheduleVsyncUpdate(
            int32_t configId, int32_t newVsyncPeriod,
            const VsyncPeriodChangeConstraints& newVsyncPeriodChangeConstraints,
            VsyncPeriodChangeTimeline* timeline);

private:
    HWC3::Error stop();

    void threadLoop();

    std::chrono::nanoseconds updateVsyncPeriodLocked(
            std::chrono::time_point<std::chrono::steady_clock> now);

    const int64_t mDisplayId;
    Display* mDisplay = nullptr;

    std::thread mThread;

    std::mutex mStateMutex;

    std::atomic<bool> mShuttingDown{false};

    std::shared_ptr<IComposerCallback> mCallbacks;

    bool mVsyncEnabled = false;
    std::chrono::nanoseconds mVsyncPeriod{16600000};
    std::chrono::time_point<std::chrono::steady_clock> mPreviousVsync;

    struct PendingUpdate {
        std::chrono::nanoseconds period;
        std::chrono::time_point<std::chrono::steady_clock> updateAfter;
        int32_t configId;
    };
    std::optional<PendingUpdate> mPendingUpdate;
};

} // namespace aidl::android::hardware::graphics::composer3::impl

#endif
