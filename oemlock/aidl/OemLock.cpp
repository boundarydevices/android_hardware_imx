/*
 * Copyright (C) 2020 The Android Open Source Project
 *
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

#include "OemLock.h"
#include <android-base/logging.h>
#include <android-base/strings.h>

namespace aidl {
namespace android {
namespace hardware {
namespace oemlock {

// Methods from ::android::hardware::oemlock::IOemLock follow.

::ndk::ScopedAStatus OemLock::getName(std::string *out_name) {
    *out_name = "imx_oemlock_aidl";
    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus OemLock::setOemUnlockAllowedByCarrier(bool in_allowed, const std::vector<uint8_t> &in_signature, OemLockSecureStatus *_aidl_return) {
    (void)in_signature;
    (void)in_allowed;

    /* Carrier unlock is not supported, so we just return here. */
    LOG(INFO) << "Running OemLock::setOemUnlockAllowedByCarrier...";
    LOG(INFO) << "carrier unlock is not supported, return ok here...";
    *_aidl_return = OemLockSecureStatus::OK;

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus OemLock::isOemUnlockAllowedByCarrier(bool *out_allowed) {

    /* Carrier unlock is not supported, so we always allow the carrier unlock */
    LOG(INFO) << "Running OemLock::isOemUnlockAllowedByCarrier...";
    LOG(INFO) << "carrier unlock is not supported, return ok here...";
    *out_allowed = true;

    return ::ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus OemLock::setOemUnlockAllowedByDevice(bool in_allowed) {
    LOG(INFO) << "Running OemLock::setOemUnlockAllowedByDevice: " << in_allowed;

    avbError rc = mAvbOemUnlockIpc.writeDeviceUnlockPermission(in_allowed ? 1 : 0);
    if (rc != avbError::AVB_ERROR_NONE) {
        LOG(ERROR) << "Failed to set device unlock status!";
        return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(-1));
    } else {
        return ::ndk::ScopedAStatus::ok();
    }
}

::ndk::ScopedAStatus OemLock::isOemUnlockAllowedByDevice(bool *out_allowed) {
    uint8_t status;

    LOG(INFO) << "Running OemLock::isOemUnlockAllowedByDevice";

    avbError rc = mAvbOemUnlockIpc.readDeviceUnlockPermission(&status);
    if (rc != avbError::AVB_ERROR_NONE) {
        LOG(ERROR) << "Failed to get device unlock status!";

        return ndk::ScopedAStatus(AStatus_fromServiceSpecificError(-1));
    } else {
        *out_allowed = status ? true : false;
        LOG(INFO) << "OemLock: Get device unlock status: " << *out_allowed;

        return ::ndk::ScopedAStatus::ok();
    }
}

} // namespace oemlock
} // namespace hardware
} // namespace android
} // aidl
