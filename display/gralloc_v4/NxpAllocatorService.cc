/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef LOG_TAG
#define LOG_TAG "AllocatorService"
#endif

#include <hidl/LegacySupport.h>

#include "NxpAllocator.h"

using android::sp;
using android::hardware::configureRpcThreadpool;
using android::hardware::joinRpcThreadpool;
using android::hardware::graphics::allocator::V4_0::IAllocator;

int main(int, char**) {
    // sched policy is same as SF main thread
    struct sched_param param = {0};
    param.sched_priority = 2;
    if (sched_setscheduler(0, SCHED_FIFO | SCHED_RESET_ON_FORK, &param) != 0) {
        ALOGE("Couldn't set SCHED_FIFO: %d", errno);
    }

    sp<IAllocator> allocator = new NxpAllocator();
    configureRpcThreadpool(4, true /* callerWillJoin */);
    if (allocator->registerAsService() != android::NO_ERROR) {
        ALOGE("failed to register graphics IAllocator 4.0 service");
        return -EINVAL;
    }

    ALOGI("graphics IAllocator 4.0 service is initialized");
    android::hardware::joinRpcThreadpool();
    ALOGI("graphics IAllocator 4.0 service is terminating");
    return 0;
}
