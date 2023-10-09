/*
 * Copyright (C) 2023 The Android Open Source Project
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

#include "dek_extractor.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <binder/ProcessState.h>
#include <binder/IServiceManager.h>

using aidl::android::hardware::imx_dek_extractor::Dek_Extractor;

int main() {
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    std::shared_ptr<Dek_Extractor> dek_exactor = ndk::SharedRefBase::make<Dek_Extractor>();

    const std::string instance = std::string() + Dek_Extractor::descriptor + "/default";
    binder_status_t status =
        AServiceManager_addService(dek_exactor->asBinder().get(), instance.c_str());
    CHECK(status == STATUS_OK);
    ALOGI("addService: %s",instance.c_str());

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE;  // Unreachable
}
