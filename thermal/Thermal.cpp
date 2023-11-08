/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include "Thermal.h"

namespace aidl::android::hardware::thermal::impl::imx {

using ndk::ScopedAStatus;

namespace {

ndk::ScopedAStatus initErrorStatus() {
    return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_STATE,
                                                            "ThermalHAL not initialized properly.");
}

ndk::ScopedAStatus readErrorStatus() {
    return ndk::ScopedAStatus::fromExceptionCodeWithMessage(
            EX_ILLEGAL_STATE, "ThermalHal cannot read any sensor data");
}

bool interfacesEqual(const std::shared_ptr<::ndk::ICInterface> left,
                     const std::shared_ptr<::ndk::ICInterface> right) {
    if (left == nullptr || right == nullptr || !left->isRemote() || !right->isRemote()) {
        return left == right;
    }
    return left->asBinder() == right->asBinder();
}

} // namespace

// On init we will spawn a thread which will continually watch for
// throttling.  When throttling is seen, if we have a callback registered
// the thread will call notifyThrottling() else it will log the dropped
// throttling event and do nothing.  The thread is only killed when
// Thermal() is killed.


Thermal::Thermal()
      : thermal_helper_(
                std::bind(&Thermal::sendThermalChangedCallback, this, std::placeholders::_1)) {}

ndk::ScopedAStatus Thermal::getTemperatures(std::vector<Temperature> *_aidl_return) {
    return getFilteredTemperatures(false, TemperatureType::UNKNOWN, _aidl_return);
}

ndk::ScopedAStatus Thermal::getTemperaturesWithType(TemperatureType type,
                                                    std::vector<Temperature> *_aidl_return) {
    return getFilteredTemperatures(true, type, _aidl_return);
}

ndk::ScopedAStatus Thermal::getFilteredTemperatures(bool filterType, TemperatureType type,
                                                    std::vector<Temperature> *_aidl_return) {
    *_aidl_return = {};
    if (!thermal_helper_.isInitializedOk()) {
        return initErrorStatus();
    }
    if (!thermal_helper_.fillCurrentTemperatures(filterType, false, type, _aidl_return)) {
        return readErrorStatus();
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Thermal::getCoolingDevices(std::vector<CoolingDevice> *_aidl_return) {
    return getFilteredCoolingDevices(false, CoolingType::BATTERY, _aidl_return);
}

ndk::ScopedAStatus Thermal::getCoolingDevicesWithType(CoolingType type,
                                                      std::vector<CoolingDevice> *_aidl_return) {
    return getFilteredCoolingDevices(true, type, _aidl_return);
}

ndk::ScopedAStatus Thermal::getFilteredCoolingDevices(bool filterType, CoolingType type,
                                                      std::vector<CoolingDevice> *_aidl_return) {
    *_aidl_return = {};
    if (!thermal_helper_.isInitializedOk()) {
        return initErrorStatus();
    }
    if (!thermal_helper_.fillCurrentCoolingDevices(filterType, type, _aidl_return)) {
        return readErrorStatus();
    }
    return ndk::ScopedAStatus::ok();
}

ndk::ScopedAStatus Thermal::getTemperatureThresholds(
        std::vector<TemperatureThreshold> *_aidl_return) {
    *_aidl_return = {};
    return getFilteredTemperatureThresholds(false, TemperatureType::UNKNOWN, _aidl_return);
}

ndk::ScopedAStatus Thermal::getTemperatureThresholdsWithType(
        TemperatureType type, std::vector<TemperatureThreshold> *_aidl_return) {
    return getFilteredTemperatureThresholds(true, type, _aidl_return);
}

ndk::ScopedAStatus Thermal::getFilteredTemperatureThresholds(
        bool filterType, TemperatureType type, std::vector<TemperatureThreshold> *_aidl_return) {
    *_aidl_return = {};
    if (!thermal_helper_.isInitializedOk()) {
        return initErrorStatus();
    }
    if (!thermal_helper_.fillTemperatureThresholds(filterType, type, _aidl_return)) {
        return readErrorStatus();
    }
    return ndk::ScopedAStatus::ok();
}


ScopedAStatus Thermal::registerThermalChangedCallback(
        const std::shared_ptr<IThermalChangedCallback>& callback) {
    return registerThermalChangedCallback(callback, false, TemperatureType::UNKNOWN);
}

ScopedAStatus Thermal::registerThermalChangedCallbackWithType(
        const std::shared_ptr<IThermalChangedCallback>& callback, TemperatureType type) {
    return registerThermalChangedCallback(callback, true, type);
}

ScopedAStatus Thermal::unregisterThermalChangedCallback(
        const std::shared_ptr<IThermalChangedCallback>& callback) {
    LOG(VERBOSE) << __func__ << " IThermalChangedCallback: " << callback;
    if (callback == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                "Invalid nullptr callback");
    }
    bool removed = false;
    std::lock_guard<std::mutex> _lock(thermal_callback_mutex_);
    callbacks_.erase(
            std::remove_if(
                    callbacks_.begin(), callbacks_.end(),
                    [&](const CallbackSetting &c) {
                        if (interfacesEqual(c.callback, callback)) {
                            LOG(INFO)
                                    << "a callback has been unregistered to ThermalHAL, isFilter: "
                                    << c.is_filter_type << " Type: " << toString(c.type);
                            removed = true;
                            return true;
                        }
                        return false;
                    }),
            callbacks_.end());
    if (!removed) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                "Callback wasn't registered");
    }
    return ScopedAStatus::ok();
}

ndk::ScopedAStatus Thermal::registerThermalChangedCallback(
        const std::shared_ptr<IThermalChangedCallback> &callback, bool filterType,
        TemperatureType type) {
    if (callback == nullptr) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                "Invalid nullptr callback");
    }
    if (!thermal_helper_.isInitializedOk()) {
        return initErrorStatus();
    }
    std::lock_guard<std::mutex> _lock(thermal_callback_mutex_);
    if (std::any_of(callbacks_.begin(), callbacks_.end(), [&](const CallbackSetting &c) {
            return interfacesEqual(c.callback, callback);
        })) {
        return ndk::ScopedAStatus::fromExceptionCodeWithMessage(EX_ILLEGAL_ARGUMENT,
                                                                "Callback already registered");
    }
    auto c = callbacks_.emplace_back(callback, filterType, type);
    LOG(INFO) << "a callback has been registered to ThermalHAL, isFilter: " << c.is_filter_type
              << " Type: " << toString(c.type);
    // Send notification right away after successful thermal callback registration
    std::function<void()> handler = [this, c(std::move(c)), filterType, type]() {
        std::vector<Temperature> temperatures;
        if (thermal_helper_.fillCurrentTemperatures(filterType, true, type, &temperatures)) {
            for (const auto &t : temperatures) {
                if (!filterType || t.type == type) {
                    LOG(INFO) << "Sending notification: "
                              << " Type: " << toString(t.type) << " Name: " << t.name
                              << " CurrentValue: " << t.value
                              << " ThrottlingStatus: " << toString(t.throttlingStatus);
                    c.callback->notifyThrottling(t);
                }
            }
        }
    };
    looper_.addEvent(Looper::Event{std::move(handler)});
    return ndk::ScopedAStatus::ok();
}

void Thermal::sendThermalChangedCallback(const std::vector<Temperature> &temps) {
    std::lock_guard<std::mutex> _lock(thermal_callback_mutex_);
    for (auto &t : temps) {
        if (toString(t.type) == "CPU") {
            std::vector<TemperatureThreshold> tempThresholds;
            std::vector<CpuUsage> cpu_usages;
            static std::string kConfigDefaultFileName;
            static std::vector<std::string> CPUInfo;
            static bool tempThrottling = false;
            if (!thermal_helper_.fillCpuUsages(&cpu_usages)) {
                LOG(ERROR) << "Failed to get CPU usages." << std::endl;
                return;
            }
            if (!thermal_helper_.fillTemperatureThresholds(true, TemperatureType::CPU,
                                                           &tempThresholds)) {
                LOG(ERROR) << "Failed to getTemperatureThresholds." << std::endl;
                return;
            }
            const auto &tempThreshold = tempThresholds[0];
            if (t.value >= tempThreshold.hotThrottlingThresholds[3] && !tempThrottling) {
                kConfigDefaultFileName =
                        ::android::base::StringPrintf("%s_%s%s", "thermal_info_config",
                                                    ::android::base::GetProperty(kSocType, "")
                                                            .c_str(),
                                                    ".json");
                CPUInfo = ParseHotplugCPUInfo(
                        "/vendor/etc/configs/" +
                        ::android::base::GetProperty(kConfigProperty, kConfigDefaultFileName.data()));
                for (std::vector<std::string>::iterator it = CPUInfo.begin(); it != CPUInfo.end();
                     it++) {
                    thermal_helper_.enableCPU(*it, false);
                }
                tempThrottling = true;
            } else if (t.value < tempThreshold.hotThrottlingThresholds[2] && tempThrottling) {
                for(std::vector<std::string>::iterator it = CPUInfo.begin(); it!=CPUInfo.end(); it++) {
                    thermal_helper_.enableCPU(*it, true);
                }
                tempThrottling = false;
            }
        }
        callbacks_.erase(std::remove_if(callbacks_.begin(), callbacks_.end(),
                                    [&](const CallbackSetting &c) {
                                        if (!c.is_filter_type || t.type == c.type) {
                                            ::ndk::ScopedAStatus ret =
                                                    c.callback->notifyThrottling(t);
                                            if (!ret.isOk()) {
                                                LOG(ERROR) << "a Thermal callback is dead, removed "
                                                              "from callback list.";
                                                return true;
                                            }
                                            return false;
                                        }
                                        return false;
                                    }),
                     callbacks_.end());
    }
}

void Thermal::Looper::addEvent(const Thermal::Looper::Event &e) {
    std::unique_lock<std::mutex> lock(mutex_);
    events_.push(e);
    cv_.notify_all();
}

void Thermal::Looper::loop() {
    while (true) {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&] { return !events_.empty(); });
        Event event = events_.front();
        events_.pop();
        lock.unlock();
        event.handler();
    }
}
} // namespace aidl::android::hardware::thermal::impl::imx
