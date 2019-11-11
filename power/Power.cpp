/*
 * Copyright (C) 2018 The Android Open Source Project
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

#define ATRACE_TAG (ATRACE_TAG_POWER | ATRACE_TAG_HAL)
#define LOG_TAG "android.hardware.power@1.3-service.imx"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>

#include <mutex>

#include <utils/Log.h>
#include <utils/Trace.h>

#include "AudioStreaming.h"
#include "Power.h"

namespace android {
namespace hardware {
namespace power {
namespace V1_3 {
namespace implementation {

using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::power::V1_0::Feature;
using ::android::hardware::power::V1_0::Status;

constexpr char kPowerHalStateProp[] = "vendor.powerhal.camera";
constexpr char kPowerHalAudioProp[] = "vendor.powerhal.audio";
constexpr char kPowerHalInitProp[] = "vendor.powerhal.init";
constexpr char kPowerHalRenderingProp[] = "vendor.powerhal.rendering";
constexpr char kPowerHalConfigPath[] = "/vendor/etc/configs/powerhint";
constexpr char kSocType[] = "ro.boot.soc_type";
static struct timespec s_previous_boost_timespec;
static int s_previous_duration;

#define USINSEC 1000000L
#define NSINUS 1000L

static const std::map<enum CameraStreamingMode, std::string> kCamStreamingHint = {
    {CAMERA_STREAMING_OFF, "CAMERA_STREAMING_OFF"},
    {CAMERA_STREAMING, "CAMERA_STREAMING"}};

Power::Power()
    : mHintManager(nullptr),
      mInteractionHandler(nullptr),
      mVRModeOn(false),
      mSustainedPerfModeOn(false),
      mCameraStreamingMode(CAMERA_STREAMING_OFF),
      mReady(false) {
    mInitThread = std::thread([this]() {
        char name[PATH_MAX] = {0};
        android::base::WaitForProperty(kPowerHalInitProp, "1");
        snprintf(name, PATH_MAX, "%s_%s%s", kPowerHalConfigPath, android::base::GetProperty(kSocType, "").c_str(), ".json");
        mHintManager = HintManager::GetFromJSON(name);
        if (!mHintManager) {
            LOG(FATAL) << "Invalid config: " << name;
        }
        mInteractionHandler = std::make_unique<InteractionHandler>(mHintManager);
        mInteractionHandler->Init();
        std::string state = android::base::GetProperty(kPowerHalStateProp, "");
        if (state == "CAMERA_STREAMING") {
            ALOGI("Initialize with CAMERA_STREAMING on");
            mHintManager->DoHint("CAMERA_STREAMING");
            mCameraStreamingMode = CAMERA_STREAMING;
        }

        state = android::base::GetProperty(kPowerHalAudioProp, "");
        if (state == "AUDIO_LOW_LATENCY") {
            ALOGI("Initialize with AUDIO_LOW_LATENCY on");
            mHintManager->DoHint("AUDIO_LOW_LATENCY");
        }

        state = android::base::GetProperty(kPowerHalRenderingProp, "");
        if (state == "EXPENSIVE_RENDERING") {
            ALOGI("Initialize with EXPENSIVE_RENDERING on");
            mHintManager->DoHint("EXPENSIVE_RENDERING");
        }
        // Now start to take powerhint
        mReady.store(true);
        ALOGI("PowerHAL ready to process hints");
    });
    mInitThread.detach();
}

// Methods from ::android::hardware::power::V1_0::IPower follow.
Return<void> Power::setInteractive(bool /* interactive */) {
    return Void();
}

static long long calc_timespan_us(struct timespec start, struct timespec end) {
    long long diff_in_us = 0;
    diff_in_us += (end.tv_sec - start.tv_sec) * USINSEC;
    diff_in_us += (end.tv_nsec - start.tv_nsec) / NSINUS;
    return diff_in_us;
}

Return<void> Power::powerHint(PowerHint_1_0 hint, int32_t data) {
    if (!mReady) {
        return Void();
    }
    ATRACE_INT(android::hardware::power::V1_0::toString(hint).c_str(), data);
    ALOGD_IF(hint != PowerHint_1_0::INTERACTION, "%s: %d",
             android::hardware::power::V1_0::toString(hint).c_str(), static_cast<int>(data));
    switch (hint) {
        case PowerHint_1_0::INTERACTION:
            if (mSustainedPerfModeOn) {
                 ALOGV("%s: ignoring due to other active perf hints", __func__);
            } else {
            int duration = 1500; // 1.5s by default
            if (data) {
                int input_duration = data + 750;
                if (input_duration > duration) {
                    duration = (input_duration > 5750) ? 5750 : input_duration;
                }
            }
            struct timespec cur_boost_timespec;
            clock_gettime(CLOCK_MONOTONIC, &cur_boost_timespec);

            long long elapsed_time = calc_timespan_us(s_previous_boost_timespec, cur_boost_timespec);
            // don't hint if previous hint's duration covers this hint's duration
            if ((s_previous_duration * 1000) > (elapsed_time + duration * 1000)) {
                break;
            }

            s_previous_boost_timespec = cur_boost_timespec;
            s_previous_duration = duration;

            mHintManager->DoHint("INTERACTION", std::chrono::seconds(1));
            }
            break;
        case PowerHint_1_0::SUSTAINED_PERFORMANCE:
            if (data) {
                ALOGE(" SUSTAINED_PERFORMANCE");
                mHintManager->DoHint("SUSTAINED_PERFORMANCE");
                mSustainedPerfModeOn = true;
            } else if (!data) {
                mHintManager->EndHint("SUSTAINED_PERFORMANCE");
                mSustainedPerfModeOn = true;
            }
            break;
        case PowerHint_1_0::VR_MODE:
            // TODO: will implement in future
            break;
        case PowerHint_1_0::LAUNCH:
            if (mSustainedPerfModeOn) {
                 ALOGV("%s: ignoring due to other active lauch hints", __func__);
            } else {
                if (data) {
                    // Hint until canceled
                    mHintManager->DoHint("LAUNCH");
                } else {
                    mHintManager->EndHint("LAUNCH");
                }
            }
            break;
        case PowerHint_1_0::LOW_POWER:
            if (mSustainedPerfModeOn) {
                 ALOGV("%s: ignoring due to other active low power hints", __func__);
            } else {
                if (data) {
                    // Hint until canceled
                    mHintManager->DoHint("LOW_POWER");
                } else {
                    mHintManager->EndHint("LOW_POWER");
                }
            }
            break;
        default:
            break;
    }
    return Void();
}

Return<void> Power::setFeature(Feature /*feature*/, bool /*activate*/) {
    // Nothing to do
    return Void();
}

Return<void> Power::getPlatformLowPowerStats(getPlatformLowPowerStats_cb _hidl_cb) {
    LOG(ERROR) << "getPlatformLowPowerStats not supported. Use IPowerStats HAL.";
    _hidl_cb({}, Status::SUCCESS);
    return Void();
}

// Methods from ::android::hardware::power::V1_1::IPower follow.
Return<void> Power::getSubsystemLowPowerStats(getSubsystemLowPowerStats_cb _hidl_cb) {
    LOG(ERROR) << "getSubsystemLowPowerStats not supported. Use IPowerStats HAL.";
    _hidl_cb({}, Status::SUCCESS);
    return Void();
}

Return<void> Power::powerHintAsync(PowerHint_1_0 hint, int32_t data) {
    // just call the normal power hint in this oneway function
    return powerHint(hint, data);
}

// Methods from ::android::hardware::power::V1_2::IPower follow.
Return<void> Power::powerHintAsync_1_2(PowerHint_1_2 hint, int32_t data) {
    if (!mReady) {
        return Void();
    }

    ATRACE_INT(android::hardware::power::V1_2::toString(hint).c_str(), data);
    ALOGD_IF(hint >= PowerHint_1_2::AUDIO_STREAMING, "%s: %d",
             android::hardware::power::V1_2::toString(hint).c_str(), static_cast<int>(data));

    switch (hint) {
        case PowerHint_1_2::AUDIO_LOW_LATENCY:
            if (data) {
                // Hint until canceled
                mHintManager->DoHint("AUDIO_LOW_LATENCY");
            } else {
                mHintManager->EndHint("AUDIO_LOW_LATENCY");
            }
            break;
        case PowerHint_1_2::AUDIO_STREAMING:
            if (data == static_cast<int32_t>(AUDIO_STREAMING_HINT::AUDIO_STREAMING_ON)) {
                mHintManager->DoHint("AUDIO_STREAMING");
            } else if (data ==
                       static_cast<int32_t>(AUDIO_STREAMING_HINT::AUDIO_STREAMING_OFF)) {
                mHintManager->EndHint("AUDIO_STREAMING");
            } else {
                ALOGE("AUDIO STREAMING INVALID DATA: %d", data);
            }
            break;
        case PowerHint_1_2::CAMERA_LAUNCH:
            if (data > 0) {
                mHintManager->DoHint("CAMERA_LAUNCH");
            } else if (data == 0) {
                mHintManager->EndHint("CAMERA_LAUNCH");
            } else {
                ALOGE("CAMERA LAUNCH INVALID DATA: %d", data);
            }
            break;
        case PowerHint_1_2::CAMERA_STREAMING: {
            const enum CameraStreamingMode mode = static_cast<enum CameraStreamingMode>(data);
            if (mode < CAMERA_STREAMING_OFF || mode >= CAMERA_STREAMING_MAX) {
                ALOGE("CAMERA STREAMING INVALID Mode: %d", mode);
                break;
            }

            if (mCameraStreamingMode == mode)
                break;

            // turn it off first if any previous hint.
            if ((mCameraStreamingMode != CAMERA_STREAMING_OFF)) {
                const auto modeValue = kCamStreamingHint.at(mCameraStreamingMode);
                mHintManager->EndHint(modeValue);
                // Boost 1s for tear down
                mHintManager->DoHint("CAMERA_LAUNCH");
            }

            if (mode != CAMERA_STREAMING_OFF) {
                const auto hintValue = kCamStreamingHint.at(mode);
                mHintManager->DoHint(hintValue);
            }

            mCameraStreamingMode = mode;
            const auto prop = (mCameraStreamingMode == CAMERA_STREAMING_OFF)
                                  ? ""
                                  : kCamStreamingHint.at(mode).c_str();
            if (!android::base::SetProperty(kPowerHalStateProp, prop)) {
                ALOGE("%s: could set powerHAL state %s property", __func__, prop);
            }
            break;
        }
        case PowerHint_1_2::CAMERA_SHOT:
            if (data > 0) {
                mHintManager->DoHint("CAMERA_SHOT");
            } else if (data == 0) {
                mHintManager->EndHint("CAMERA_SHOT");
            } else {
                ALOGE("CAMERA SHOT INVALID DATA: %d", data);
            }
            break;
        default:
            return powerHint(static_cast<PowerHint_1_0>(hint), data);
    }
    return Void();
}

// Methods from ::android::hardware::power::V1_3::IPower follow.
Return<void> Power::powerHintAsync_1_3(PowerHint_1_3 hint, int32_t data) {
    if (!mReady) {
        return Void();
    }

    if (hint == PowerHint_1_3::EXPENSIVE_RENDERING) {
        if (data > 0) {
            mHintManager->DoHint("EXPENSIVE_RENDERING");
        } else {
            mHintManager->EndHint("EXPENSIVE_RENDERING");
        }
    } else {
        return powerHintAsync_1_2(static_cast<PowerHint_1_2>(hint), data);
    }
    return Void();
}

constexpr const char *boolToString(bool b) {
    return b ? "true" : "false";
}

Return<void> Power::debug(const hidl_handle &handle, const hidl_vec<hidl_string> &) {
    if (handle != nullptr && handle->numFds >= 1 && mReady) {
        int fd = handle->data[0];

        std::string buf(android::base::StringPrintf(
            "HintManager Running: %s\n"
            "VRMode: %s\n"
            "CameraStreamingMode: %s\n"
            "SustainedPerformanceMode: %s\n",
            boolToString(mHintManager->IsRunning()), boolToString(mVRModeOn),
            kCamStreamingHint.at(mCameraStreamingMode).c_str(),
            boolToString(mSustainedPerfModeOn)));
        // Dump nodes through libperfmgr
        mHintManager->DumpToFd(fd);
        if (!android::base::WriteStringToFd(buf, fd)) {
            PLOG(ERROR) << "Failed to dump state to fd";
        }
        fsync(fd);
    }
    return Void();
}

}  // namespace implementation
}  // namespace V1_3
}  // namespace power
}  // namespace hardware
}  // namespace android
