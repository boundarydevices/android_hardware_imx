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

#ifndef ANDROID_HARDWARE_OEMLOCK_OEMLOCK_H
#define ANDROID_HARDWARE_OEMLOCK_OEMLOCK_H

#include <android/hardware/oemlock/1.0/IOemLock.h>

#include "avb_ipc.h"

namespace android {
namespace hardware {
namespace oemlock {

using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::oemlock::V1_0::IOemLock;
using ::android::hardware::oemlock::V1_0::OemLockSecureStatus;
using ::android::hardware::oemlock::V1_0::OemLockStatus;

struct OemLock : public IOemLock {
    // Methods from ::android::hardware::oemlock::V1_0::IOemLock follow.
    Return<void> getName(getName_cb _hidl_cb) override;
    Return<OemLockSecureStatus> setOemUnlockAllowedByCarrier(
            bool allowed, const hidl_vec<uint8_t>& signature) override;
    Return<void> isOemUnlockAllowedByCarrier(isOemUnlockAllowedByCarrier_cb _hidl_cb) override;
    Return<OemLockStatus> setOemUnlockAllowedByDevice(bool allowed) override;
    Return<void> isOemUnlockAllowedByDevice(isOemUnlockAllowedByDevice_cb _hidl_cb) override;

    // Dump apis
    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override;
    void cmdDump(int fd, const hidl_vec<hidl_string>& options);
    void cmdHelp(int fd);
    void cmdList(int fd, const hidl_vec<hidl_string>& options);
    void cmdDumpDevice(int fd, const hidl_vec<hidl_string>& options);

private:
    avbOemUnlockIpc mAvbOemUnlockIpc;
};

} // namespace oemlock
} // namespace hardware
} // namespace android

#endif // ANDROID_HARDWARE_OEMLOCK_OEMLOCK_H
