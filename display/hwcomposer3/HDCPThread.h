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

#ifndef ANDROID_HWC_HDCPTHREAD_H
#define ANDROID_HWC_HDCPTHREAD_H

#include <android/hardware/graphics/common/1.0/types.h>

#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

#include "Common.h"
#include <regex>
#include <android-base/unique_fd.h>
#include <android-base/file.h>


namespace aidl::android::hardware::graphics::composer3::impl {

class Display;
class HDCPThread {
public:
    HDCPThread(Display* display);
    virtual ~HDCPThread();

    HDCPThread(const HDCPThread&) = delete;
    HDCPThread& operator=(const HDCPThread&) = delete;

    HDCPThread(HDCPThread&&) = delete;
    HDCPThread& operator=(HDCPThread&&) = delete;

    HWC3::Error start();

    using HDCPThreadCallback = std::function<void (Display*)>;

    HWC3::Error setCallbacks(const HDCPThreadCallback& callback);

    HWC3::Error setHDCPThreadEnabled(bool enabled);

private:
    HWC3::Error stop();

    void threadLoop();

    const int64_t mHwcId;

    Display* mDisplay = nullptr;

    std::thread mThread;

    std::mutex mStateMutex;

    std::atomic<bool> mShuttingDown{false};

    std::optional<HDCPThreadCallback> mCallbacks;

    bool mThreadEnabled = false;
    std::string mHdcpStatusPath;
    std::regex mPattern;

};

} // namespace aidl::android::hardware::graphics::composer3::impl

#endif
