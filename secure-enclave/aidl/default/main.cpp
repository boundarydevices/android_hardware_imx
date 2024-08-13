/*
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

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <binder/IServiceManager.h>
#include <binder/ProcessState.h>

#include "SecureEnclave.h"

using aidl::nxp::hardware::ele::SecureEnclave;

int main() {
    // only one thread would be created when joining the thread pool.
    ABinderProcess_setThreadPoolMaxThreadCount(0);
    std::shared_ptr<SecureEnclave> secureenclave = ndk::SharedRefBase::make<SecureEnclave>();

    const std::string instance = std::string() + SecureEnclave::descriptor + "/default";
    binder_status_t status =
            AServiceManager_addService(secureenclave->asBinder().get(), instance.c_str());
    CHECK(status == STATUS_OK);
    ALOGI("addService: %s", instance.c_str());

    ABinderProcess_joinThreadPool();
    return EXIT_FAILURE; // Unreachable
}
