/*
 * Copyright (C) 2021 The Android Open Source Project
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

#define LOG_TAG "android.hardware.power-service.imx"

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include <thread>

#include "Power.h"

using aidl::android::hardware::power::impl::Power;
using ::android::perfmgr::HintManager;

constexpr char kPowerHalConfigPath[] = "/vendor/etc/configs/powerhint";
constexpr char kPowerHalInitProp[] = "vendor.powerhal.init";
constexpr char kSocType[] = "ro.boot.soc_type";

int main() {
    LOG(INFO) << "IMX Power HAL AIDL Service is starting.";

    char name[PATH_MAX] = {0};
    android::base::WaitForProperty(kPowerHalInitProp, "1");
    snprintf(name, PATH_MAX, "%s_%s%s", kPowerHalConfigPath,
             android::base::GetProperty(kSocType, "").c_str(), ".json");

    // Parse config but do not start the looper
    std::shared_ptr<HintManager> hm = HintManager::GetFromJSON(name, true);
    if (!hm) {
        LOG(FATAL) << "Invalid config: " << name;
    }

    // single thread
    ABinderProcess_setThreadPoolMaxThreadCount(0);

    // core service
    std::shared_ptr<Power> pw = ndk::SharedRefBase::make<Power>(hm);
    ndk::SpAIBinder pwBinder = pw->asBinder();

    const std::string instance = std::string() + Power::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(pw->asBinder().get(), instance.c_str());
    CHECK(status == STATUS_OK);
    LOG(INFO) << "IMX Power HAL AIDL Service is started.";

    std::thread initThread([&]() {
        ::android::base::WaitForProperty(kPowerHalInitProp, "1");
        hm->Start();
    });
    initThread.detach();

    ABinderProcess_joinThreadPool();

    // should not reach
    LOG(ERROR) << "IMX Power HAL AIDL Service just died.";
    return EXIT_FAILURE;
}
