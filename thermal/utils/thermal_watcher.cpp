/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include "thermal_watcher.h"

namespace aidl::android::hardware::thermal::impl::imx {

using std::chrono_literals::operator""ms;

void ThermalWatcher::registerFilesToWatch(const std::set<std::string> &sensors_to_watch,
                                          bool uevent_monitor) {
    monitored_sensors_.insert(sensors_to_watch.begin(), sensors_to_watch.end());
    if (!uevent_monitor) {
        is_polling_ = true;
        return;
    }
    uevent_fd_.reset((TEMP_FAILURE_RETRY(uevent_open_socket(64 * 1024, true))));
    if (uevent_fd_.get() < 0) {
        LOG(ERROR) << "failed to open uevent socket";
        is_polling_ = true;
        return;
    }

    if (fcntl(uevent_fd_, F_SETFL, O_NONBLOCK) < 0) {
        LOG(ERROR) << "failed to manipulate uevent socket";
        is_polling_ = true;
        return;
    }

    looper_->addFd(uevent_fd_.get(), 0, ::android::Looper::EVENT_INPUT, nullptr, nullptr);
    is_polling_ = false;
    thermal_triggered_ = true;
}

bool ThermalWatcher::startWatchingDeviceFiles() {
    if (cb_) {
        auto ret = this->run("FileWatcherThread", ::android::PRIORITY_HIGHEST);
        if (ret != ::android::NO_ERROR) {
            LOG(ERROR) << "ThermalWatcherThread start fail";
            return false;
        } else {
            LOG(INFO) << "ThermalWatcherThread started";
            return true;
        }
    }
    return false;
}
void ThermalWatcher::parseUevent(std::set<std::string> *sensors_set) {
    bool thermal_event = false;
    constexpr int kUeventMsgLen = 2048;
    char msg[kUeventMsgLen + 2];
    char *cp;

    while (true) {
        int n = uevent_kernel_multicast_recv(uevent_fd_.get(), msg, kUeventMsgLen);
        if (n <= 0) {
            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                LOG(ERROR) << "Error reading from Uevent Fd";
            }
            break;
        }

        if (n >= kUeventMsgLen) {
            LOG(ERROR) << "Uevent overflowed buffer, discarding";
            continue;
        }

        msg[n] = '\0';
        msg[n + 1] = '\0';

        cp = msg;
        while (*cp) {
            std::string uevent = cp;
            if (!thermal_event) {
                if (uevent.find("SUBSYSTEM=") == 0) {
                    if (uevent.find("SUBSYSTEM=thermal") != std::string::npos) {
                        thermal_event = true;
                    } else {
                        break;
                    }
                }
            } else {
                auto start_pos = uevent.find("NAME=");
                if (start_pos != std::string::npos) {
                    start_pos += 5;
                    std::string name = uevent.substr(start_pos);
                    if (std::find(monitored_sensors_.begin(), monitored_sensors_.end(), name) !=
                        monitored_sensors_.end()) {
                        sensors_set->insert(name);
                    }
                    break;
                }
            }
            while (*cp++) {
            }
        }
    }
}

void ThermalWatcher::wake() {
    looper_->wake();
}

bool ThermalWatcher::threadLoop() {
    LOG(VERBOSE) << "ThermalWatcher polling...";
    // Polling interval 2s
    static constexpr int kMinPollIntervalMs = 2000;
    // Max uevent timeout 5mins
    static constexpr int kUeventPollTimeoutMs = 300000;
    int fd;
    std::set<std::string> sensors;

    int timeout = (thermal_triggered_ || is_polling_) ? kMinPollIntervalMs : kUeventPollTimeoutMs;
    if (looper_->pollOnce(timeout, &fd, nullptr, nullptr) >= 0) {
        if (fd != uevent_fd_.get()) {
            return true;
        }
        parseUevent(&sensors);
        // Ignore cb_ if uevent is not from monitored sensors
        if (sensors.size() == 0) {
            return true;
        }
    }
    thermal_triggered_ = cb_(sensors);
    return true;
}

} // namespace aidl::android::hardware::thermal::impl::imx
