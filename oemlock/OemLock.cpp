/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Copyright 2019 NXP
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
#include <memory>
#include <android-base/endian.h>
#include <android-base/logging.h>

namespace android {
namespace hardware {
namespace oemlock {

// libhidl
using ::android::hardware::Void;

// Methods from ::android::hardware::oemlock::V1_0::IOemLock follow.
Return<void> OemLock::getName(getName_cb _hidl_cb) {
    _hidl_cb(OemLockStatus::OK, {"01"});
    return Void();
}

Return<OemLockSecureStatus> OemLock::setOemUnlockAllowedByCarrier(
    bool allowed, const hidl_vec<uint8_t>& signature) {

    /* Carrier unlock is not supported, so we just return here. */
    LOG(INFO) << "Running OemLock::setOemUnlockAllowedByCarrier...";
    LOG(INFO) << "carrier unlock is not supported, return ok here...";
    return OemLockSecureStatus::OK;
}

Return<void> OemLock::isOemUnlockAllowedByCarrier(isOemUnlockAllowedByCarrier_cb _hidl_cb) {

    /* Carrier unlock is not supported, so we always allow the carrier unlock */
    LOG(INFO) << "Running OemLock::isOemUnlockAllowedByCarrier...";
    LOG(INFO) << "carrier unlock is not supported, return ok here...";
    _hidl_cb(OemLockStatus::OK, true);
    return Void();
}

Return<OemLockStatus> OemLock::setOemUnlockAllowedByDevice(bool allowed) {
    LOG(INFO) << "Running OemLock::setOemUnlockAllowedByDevice: " << allowed;

    avbError rc = mAvbOemUnlockIpc->writeDeviceUnlockPermission(allowed ? 1 : 0);
    if (rc != avbError::AVB_ERROR_NONE) {
        LOG(ERROR) << "Failed to set device unlock status!";
        return OemLockStatus::FAILED;
    } else
        return OemLockStatus::OK;
}

Return<void> OemLock::isOemUnlockAllowedByDevice(isOemUnlockAllowedByDevice_cb _hidl_cb) {
    uint8_t status;

    LOG(VERBOSE) << "Running OemLock::isOemUnlockAllowedByDevice";

    avbError rc = mAvbOemUnlockIpc->readDeviceUnlockPermission(&status);
    if (rc != avbError::AVB_ERROR_NONE) {
        LOG(ERROR) << "Failed to set device unlock status!";
       _hidl_cb(OemLockStatus::FAILED, false);
    } else {
       _hidl_cb(OemLockStatus::OK, status? true: false);
    }

    return Void();
}

} // namespace oemlock
} // namespace hardware
} // namespace android
