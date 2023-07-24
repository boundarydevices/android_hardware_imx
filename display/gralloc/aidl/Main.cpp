/*
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Copyright 2023 NXP
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

#include "NxpAllocator.h"

using aidl::android::hardware::graphics::allocator::impl::NxpAllocator;

int main(int /*argc*/, char** /*argv*/) {
    ALOGI("NXP AIDL allocator starting up...");

    // same as SF main thread
    struct sched_param param = {0};
    param.sched_priority = 2;
    if (sched_setscheduler(0, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) != 0) {
        ALOGI("%s: failed to set priority: %s", __FUNCTION__, strerror(errno));
    }

    auto allocator = ndk::SharedRefBase::make<NxpAllocator>();
    CHECK(allocator != nullptr);

    if (!allocator->init()) {
        ALOGE("Failed to initialize NXP AIDL allocator.");
        return EXIT_FAILURE;
    }

    const std::string instance = std::string() + NxpAllocator::descriptor + "/default";
    binder_status_t status =
            AServiceManager_addService(allocator->asBinder().get(), instance.c_str());
    CHECK_EQ(status, STATUS_OK);

    ABinderProcess_setThreadPoolMaxThreadCount(4);
    ABinderProcess_startThreadPool();
    ABinderProcess_joinThreadPool();

    return EXIT_FAILURE;
}
