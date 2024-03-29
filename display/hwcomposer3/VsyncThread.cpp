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

#include "VsyncThread.h"

#include <utils/ThreadDefs.h>

#include <thread>

#include "Display.h"
#include "Time.h"

namespace aidl::android::hardware::graphics::composer3::impl {
namespace {

// Returns the timepoint of the next vsync after the 'now' timepoint that is
// a multiple of 'vsyncPeriod' in-phase/offset-from 'previousSync'.
//
// Some examples:
//  * vsyncPeriod=50ns previousVsync=500ns now=510ns => 550ns
//  * vsyncPeriod=50ns previousVsync=300ns now=510ns => 550ns
//  * vsyncPeriod=50ns previousVsync=500ns now=550ns => 550ns
TimePoint GetNextVsyncInPhase(Nanoseconds vsyncPeriod, TimePoint previousVsync, TimePoint now) {
    const auto elapsed = Nanoseconds(now - previousVsync);
    const auto nextMultiple = (elapsed / vsyncPeriod) + 1;
    return previousVsync + (nextMultiple * vsyncPeriod);
}

} // namespace

VsyncThread::VsyncThread(Display* display) : mDisplayId(display->getId()), mDisplay(display) {}

VsyncThread::~VsyncThread() {
    stop();
}

HWC3::Error VsyncThread::start(int32_t vsyncPeriodNanos) {
    DEBUG_LOG("%s for display:%" PRIu64, __FUNCTION__, mDisplayId);

    mVsyncPeriod = Nanoseconds(vsyncPeriodNanos);
    mPreviousVsync = std::chrono::steady_clock::now() - mVsyncPeriod;

    mThread = std::thread([this]() { threadLoop(); });

    const std::string name = "display_" + std::to_string(mDisplayId) + "_vsync";

    int ret = pthread_setname_np(mThread.native_handle(), name.c_str());
    if (ret != 0) {
        ALOGE("%s: failed to set Vsync thread name: %s", __FUNCTION__, strerror(ret));
    }

    struct sched_param param = {
            .sched_priority = 2,
    };
    ret = pthread_setschedparam(mThread.native_handle(), SCHED_FIFO, &param);
    if (ret != 0) {
        ALOGE("%s: failed to set Vsync thread priority: %s", __FUNCTION__, strerror(ret));
    }

    return HWC3::Error::None;
}

HWC3::Error VsyncThread::stop() {
    mShuttingDown.store(true);
    mThread.join();

    return HWC3::Error::None;
}

HWC3::Error VsyncThread::setCallbacks(const std::shared_ptr<IComposerCallback>& callback) {
    DEBUG_LOG("%s for display:%" PRIu64, __FUNCTION__, mDisplayId);

    std::unique_lock<std::mutex> lock(mStateMutex);
    mCallbacks = callback;

    return HWC3::Error::None;
}

HWC3::Error VsyncThread::setVsyncEnabled(bool enabled) {
    DEBUG_LOG("%s for display:%" PRIu64 " enabled:%d", __FUNCTION__, mDisplayId, enabled);

    std::lock_guard<std::mutex> lock(mStateMutex);
    mVsyncEnabled = enabled;

    return HWC3::Error::None;
}

HWC3::Error VsyncThread::scheduleVsyncUpdate(int32_t configId, int32_t newVsyncPeriod,
                                             const VsyncPeriodChangeConstraints& constraints,
                                             VsyncPeriodChangeTimeline* outTimeline) {
    DEBUG_LOG("%s for display:%" PRIu64, __FUNCTION__, mDisplayId);

    std::chrono::time_point<std::chrono::steady_clock> updateTime;
    if (constraints.desiredTimeNanos == 0) { // take effect immediately
        mVsyncPeriod = Nanoseconds(newVsyncPeriod);
        mDisplay->takeEffectConfig(configId);
        updateTime = mPreviousVsync;
    } else {
        PendingUpdate update;
        update.period = Nanoseconds(newVsyncPeriod);
        update.updateAfter = asTimePoint(constraints.desiredTimeNanos);
        update.configId = configId;
        updateTime = update.updateAfter;

        std::unique_lock<std::mutex> lock(mStateMutex);
        mPendingUpdate.emplace(std::move(update));
    }

    TimePoint nextVsync = GetNextVsyncInPhase(mVsyncPeriod, mPreviousVsync, updateTime);
    outTimeline->newVsyncAppliedTimeNanos = asNanosTimePoint(nextVsync);
    outTimeline->refreshRequired = false;
    outTimeline->refreshTimeNanos = 0;

    return HWC3::Error::None;
}

Nanoseconds VsyncThread::updateVsyncPeriodLocked(TimePoint now) {
    if (mPendingUpdate && now > mPendingUpdate->updateAfter) {
        mVsyncPeriod = mPendingUpdate->period;
        mDisplay->takeEffectConfig(mPendingUpdate->configId);
        mPendingUpdate.reset();
    }

    return mVsyncPeriod;
}

void VsyncThread::threadLoop() {
    ALOGI("Vsync thread for display:%" PRId64 " starting", mDisplayId);

    Nanoseconds vsyncPeriod = mVsyncPeriod;

    int vsyncs = 0;
    TimePoint previousLog = std::chrono::steady_clock::now();
    int64_t lasttime = 0;

    while (!mShuttingDown.load()) {
        int64_t timestamp = 0;
        TimePoint vsyncTime;
        TimePoint now = std::chrono::steady_clock::now();
        if (mVsyncEnabled && (mDisplay->checkAndWaitNextVsync(&timestamp) == HWC3::Error::None)) {
            if (timestamp == 0)
                vsyncTime = now;
            else
                vsyncTime = asTimePoint(timestamp);

            if (lasttime != 0) {
                DEBUG_LOG("hardware vsync period: %" PRIu64, timestamp - lasttime);
            }
            lasttime = timestamp;
        } else {
            TimePoint nextVsync = GetNextVsyncInPhase(vsyncPeriod, mPreviousVsync, now);
            std::this_thread::sleep_until(nextVsync);
            vsyncTime = nextVsync;
        }
        {
            std::unique_lock<std::mutex> lock(mStateMutex);
            mPreviousVsync = vsyncTime;

            // Display has finished refreshing at previous vsync period. Update the
            // vsync period if there was a pending update.
            vsyncPeriod = updateVsyncPeriodLocked(mPreviousVsync);
        }

        if (mVsyncEnabled) {
            if (mCallbacks) {
                ALOGV("%s: for display:%" PRIu64 " calling vsync", __FUNCTION__, mDisplayId);
                mCallbacks->onVsync(mDisplayId, asNanosTimePoint(mPreviousVsync),
                                    asNanosDuration(vsyncPeriod));
            }
        }

        static constexpr const int kLogIntervalSeconds = 60;
        if (now > (previousLog + std::chrono::seconds(kLogIntervalSeconds))) {
            DEBUG_LOG("%s: for display:%" PRIu64 " send %" PRIu32 " in last %d seconds",
                      __FUNCTION__, mDisplayId, vsyncs, kLogIntervalSeconds);
            previousLog = now;
            vsyncs = 0;
        }
        ++vsyncs;
    }

    ALOGI("Vsync thread for display:%" PRId64 " finished", mDisplayId);
}

} // namespace aidl::android::hardware::graphics::composer3::impl
