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

#include "PollThread.h"

#include <utils/ThreadDefs.h>

#include <thread>

namespace aidl::android::hardware::graphics::composer3::impl {

PollThread::PollThread() {}

PollThread::~PollThread() {
    stop();
}

HWC3::Error PollThread::start(std::string path) {
    DEBUG_LOG("%s: check if DRM driver(%s) ready!", __FUNCTION__, path.c_str());

    mPollPath = path;

    mINotifyFd = inotify_init();
    if (mINotifyFd < 0) {
        ALOGE("%s: Fail to initialize inotify fd, error:%s", __FUNCTION__, strerror(errno));
        return HWC3::Error::BadParameter;
    }

    mINotifyWd = inotify_add_watch(mINotifyFd, mPollPath.c_str(), IN_CREATE);
    if (mINotifyWd < 0) {
        ALOGE("%s: Fail to add watch for %s, error:%s", __FUNCTION__, mPollPath.c_str(),
              strerror(errno));
        close(mINotifyFd);
        return HWC3::Error::BadParameter;
    }

    mEpollFd = epoll_create(1);
    if (mEpollFd < 0) {
        ALOGE("%s: Fail to create epoll instance, error:%s", __FUNCTION__, strerror(errno));
        inotify_rm_watch(mINotifyFd, mINotifyWd);
        close(mINotifyFd);
        return HWC3::Error::BadParameter;
    }
    epoll_event eventItem;
    memset(&eventItem, 0, sizeof(epoll_event));
    eventItem.events = EPOLLIN;
    eventItem.data.fd = mINotifyFd;
    int result = epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mINotifyFd, &eventItem);
    if (result < 0) {
        ALOGE("%s: Fail to add inotify to epoll instance, error:%s", __FUNCTION__, strerror(errno));
        inotify_rm_watch(mINotifyFd, mINotifyWd);
        close(mINotifyFd);
        close(mEpollFd);
        return HWC3::Error::BadParameter;
    }

    mThread = std::thread([this]() { threadLoop(); });

    const std::string name = "hwc_poll_drm";

    int ret = pthread_setname_np(mThread.native_handle(), name.c_str());
    if (ret != 0) {
        ALOGE("%s: failed to set Poll Drm thread name: %s", __FUNCTION__, strerror(ret));
    }

    struct sched_param param = {
            .sched_priority = 2,
    };
    ret = pthread_setschedparam(mThread.native_handle(), SCHED_FIFO, &param);
    if (ret != 0) {
        ALOGE("%s: failed to set Poll Drm thread priority: %s", __FUNCTION__, strerror(ret));
    }

    return HWC3::Error::None;
}

HWC3::Error PollThread::stop() {
    mShuttingDown.store(true);
    mThread.join();

    return HWC3::Error::None;
}

HWC3::Error PollThread::setCallback(const PollCallback& cb) {
    DEBUG_LOG("%s for Poll thread", __FUNCTION__);

    mPollCallbacks = cb;

    return HWC3::Error::None;
}

void PollThread::threadLoop() {
    ALOGI("%s: Poll thread for path:%s starting", __FUNCTION__, mPollPath.c_str());

    while (!mShuttingDown.load()) {
        int numEpollEvent = 0;
        epoll_event epollItems[EPOLL_MAX_EVENTS];
        numEpollEvent = epoll_wait(mEpollFd, epollItems, EPOLL_MAX_EVENTS, -1);
        if (numEpollEvent <= 0) {
            ALOGE("%s: Fail to wait requested events, error:%s", __FUNCTION__, strerror(errno));
            continue;
        }

        for (int i = 0; i < numEpollEvent; i++) {
            if (epollItems[i].events & (EPOLLERR | EPOLLHUP)) {
                continue;
            }
            if (epollItems[i].events & EPOLLIN) {
                char buf[EPOLL_BUFFER_SIZE];
                int numItem = read(mINotifyFd, buf, EPOLL_BUFFER_SIZE);
                if (numItem < 0) {
                    ALOGE("%s: Fail to read buffer from INotifyFd, error:%s", strerror(errno));
                    continue;
                }

                // Each successful read returns a buffer containing one or more of struct
                // inotify_event The length of each inotify_event structure is sizeof(struct
                // inotify_event)+len.
                for (char* itemBuf = buf; itemBuf < buf + numItem;) {
                    struct inotify_event* inotifyItem =
                            reinterpret_cast<struct inotify_event*>(itemBuf);
                    if (mPollCallbacks &&
                        (*mPollCallbacks)(inotifyItem->name) == HWC3::Error::None) {
                        inotify_rm_watch(mINotifyFd, mINotifyWd);
                        close(mEpollFd);
                        close(mINotifyFd);
                        mShuttingDown.store(true);
                        break;
                    }
                    itemBuf += sizeof(struct inotify_event) + inotifyItem->len;
                }
            }
        }
    }

    ALOGI("%s: Poll thread for path:%s finished", __FUNCTION__, mPollPath.c_str());
}

} // namespace aidl::android::hardware::graphics::composer3::impl
