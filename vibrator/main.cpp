/*
 * Copyright (C) 2019 The Android Open Source Project
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

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <utils/Log.h>

#include "vibrator-impl/Vibrator.h"
#include "vibrator-impl/VibratorManager.h"
using aidl::android::hardware::vibrator::Vibrator;
using aidl::android::hardware::vibrator::VibratorManager;

int main() {
    ALOGI("Vibrator service is starting.");
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    // make a default vibrator service
    auto vib = ndk::SharedRefBase::make<Vibrator>();
    const std::string vibName = std::string() + Vibrator::descriptor + "/default";
    binder_status_t status = AServiceManager_addService(vib->asBinder().get(), vibName.c_str());
    CHECK(status == STATUS_OK);
    ALOGI("Vibrator service vibName:%s", vibName.c_str());
    // make the vibrator manager service with a different vibrator
    ALOGI("Vibrator manager service is starting.");
    auto managedVib = ndk::SharedRefBase::make<Vibrator>();
    auto vibManager = ndk::SharedRefBase::make<VibratorManager>(std::move(managedVib));
    const std::string vibManagerName = std::string() + VibratorManager::descriptor + "/default";
    status = AServiceManager_addService(vibManager->asBinder().get(), vibManagerName.c_str());
    CHECK(status == STATUS_OK);
    ALOGI("Vibrator manager service vibManagerName:%s", vibManagerName.c_str());
    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE; // should not reach
}
