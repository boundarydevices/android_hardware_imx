/*
 * Copyright 2022 The Android Open Source Project
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

#include "HDCPThread.h"
#include <utils/ThreadDefs.h>
#include <thread>
#include "Display.h"
#

using android::base::ReadFileToString;
using android::base::WriteStringToFd;

namespace aidl::android::hardware::graphics::composer3::impl {

HDCPThread::HDCPThread(Display* display) : mHwcId(display->getHwcId()),
                                           mDisplay(display),
                                           mHdcpStatusPath(getHdcpStatusPath()),
                                           mPattern("(\\d+)\\s*:") {}

HDCPThread::~HDCPThread() {
    stop();
}

HWC3::Error HDCPThread::start() {
    DEBUG_LOG("%s HDCP Thread for hwc display:%" PRIu64, __FUNCTION__, mHwcId);

    mThread = std::thread([this]() { threadLoop(); });

    const std::string name = "display_" + std::to_string(mHwcId) + "_hdcp";

    int ret = pthread_setname_np(mThread.native_handle(), name.c_str());
    if (ret != 0) {
        ALOGE("%s: failed to set HDCP thread name: %s", __FUNCTION__, strerror(ret));
    }

    struct sched_param param = {
            .sched_priority = 2,
    };
    ret = pthread_setschedparam(mThread.native_handle(), SCHED_FIFO, &param);
    if (ret != 0) {
        ALOGE("%s: failed to set HDCP thread priority: %s", __FUNCTION__, strerror(ret));
    }

    return HWC3::Error::None;
}

HWC3::Error HDCPThread::stop() {
    mShuttingDown.store(true);
    if (mThread.joinable()) {
        mThread.join();
    }

    return HWC3::Error::None;
}

HWC3::Error HDCPThread::setCallbacks(const HDCPThreadCallback& callback) {
    DEBUG_LOG("%s HDCP Thread for hwc display:%" PRIu64, __FUNCTION__, mHwcId);

    std::unique_lock<std::mutex> lock(mStateMutex);
    if (!mCallbacks.has_value()) {
        mCallbacks = callback;
    }

    return HWC3::Error::None;
}

HWC3::Error HDCPThread::setHDCPThreadEnabled(bool enabled) {
    DEBUG_LOG("%s HDCP Thread for hwc display:%" PRIu64 " enabled:%d", __FUNCTION__, mHwcId, enabled);

    std::lock_guard<std::mutex> lock(mStateMutex);
    mThreadEnabled = enabled;

    return HWC3::Error::None;
}

void HDCPThread::threadLoop() {
    std::string mAuthResult;
    std::smatch mMatch;
    while (!mShuttingDown.load()) {
        if (mThreadEnabled && ReadFileToString(mHdcpStatusPath, &mAuthResult)) {
            if (!mAuthResult.empty()) {
                if (std::regex_search(mAuthResult, mMatch, mPattern)) {
                    if(std::stoi(mMatch[1].str()) == 5) {
                        if (mCallbacks) {
                            DEBUG_LOG("%s: for hwc display:%" PRIu64 " calling hdcp", __FUNCTION__, mHwcId);
                            (*mCallbacks)(mDisplay);
                        }
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace aidl::android::hardware::graphics::composer3::impl
