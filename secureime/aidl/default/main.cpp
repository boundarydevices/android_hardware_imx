/*
 * Copyright (C) 2020 The Android Open Source Project
 * Copyright 2022 NXP
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
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>

#include "SecureIME.h"

using aidl::nxp::hardware::secureime::SecureIME;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    std::shared_ptr<SecureIME> secureime = ndk::SharedRefBase::make<SecureIME>();

    const std::string instance = std::string() + SecureIME::descriptor + "/default";
    binder_status_t status =
            AServiceManager_addService(secureime->asBinder().get(), instance.c_str());
    CHECK(status == STATUS_OK);
    ALOGI("addService: %s", instance.c_str());

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE; // Unreachable
}
