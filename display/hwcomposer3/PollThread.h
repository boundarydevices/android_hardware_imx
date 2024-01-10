/*
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

#ifndef ANDROID_HWC_POLLTHREAD_H
#define ANDROID_HWC_POLLTHREAD_H

#include <sys/epoll.h>
#include <sys/inotify.h>

#include <chrono>
#include <mutex>
#include <optional>
#include <thread>

#include "Common.h"

#define EPOLL_MAX_EVENTS 8
#define EPOLL_BUFFER_SIZE 512

namespace aidl::android::hardware::graphics::composer3::impl {

class PollThread {
public:
    PollThread();
    virtual ~PollThread();

    PollThread(const PollThread&) = delete;
    PollThread& operator=(const PollThread&) = delete;

    PollThread(PollThread&&) = delete;
    PollThread& operator=(PollThread&&) = delete;

    HWC3::Error start(std::string path);

    using PollCallback = std::function<HWC3::Error(char*)>;
    HWC3::Error setCallback(const PollCallback& cb);

private:
    HWC3::Error stop();

    void threadLoop();

    std::optional<PollCallback> mPollCallbacks;

    int mINotifyFd = -1;
    int mEpollFd = -1;
    int mINotifyWd = -1;

    std::thread mThread;
    std::mutex mStateMutex;
    std::atomic<bool> mShuttingDown{false};

    std::string mPollPath;
};

} // namespace aidl::android::hardware::graphics::composer3::impl

#endif
