/*
 * Copyright (C) 2017 The Android Open Source Project
 * Copyright 2018-2022 NXP
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

#ifndef ANDROID_HARDWARE_USB_V1_3_USB_H
#define ANDROID_HARDWARE_USB_V1_3_USB_H

#include <android-base/file.h>
#include <android-base/properties.h>
#include <android/hardware/usb/1.2/IUsbCallback.h>
#include <android/hardware/usb/1.2/types.h>
#include <android/hardware/usb/1.3/IUsb.h>
#include <hidl/Status.h>
#include <utils/Log.h>

#include <string>

namespace android {
namespace hardware {
namespace usb {
namespace V1_3 {
namespace implementation {

using ::android::sp;
using ::android::base::GetProperty;
using ::android::base::WriteStringToFile;
using ::android::hardware::hidl_array;
using ::android::hardware::hidl_memory;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::usb::V1_0::PortDataRole;
using ::android::hardware::usb::V1_0::PortPowerRole;
using ::android::hardware::usb::V1_0::PortRole;
using ::android::hardware::usb::V1_0::PortRoleType;
using ::android::hardware::usb::V1_0::Status;
using ::android::hardware::usb::V1_1::PortMode_1_1;
using ::android::hardware::usb::V1_1::PortStatus_1_1;
using ::android::hardware::usb::V1_2::IUsbCallback;
using ::android::hardware::usb::V1_2::PortStatus;
using ::android::hardware::usb::V1_3::IUsb;
using ::android::hidl::base::V1_0::DebugInfo;
using ::android::hidl::base::V1_0::IBase;
using ::std::string;

enum class HALVersion { V1_0, V1_1, V1_2, V1_3 };

struct Usb : public IUsb {
    Usb();

    Return<void> switchRole(const hidl_string& portName, const V1_0::PortRole& role) override;
    Return<void> setCallback(const sp<V1_0::IUsbCallback>& callback) override;
    Return<void> queryPortStatus() override;
    Return<void> enableContaminantPresenceDetection(const hidl_string& portName, bool enable);
    Return<void> enableContaminantPresenceProtection(const hidl_string& portName, bool enable);
    Return<bool> enableUsbDataSignal(bool enable) override;

    sp<V1_0::IUsbCallback> mCallback_1_0;
    // Protects mCallback variable
    pthread_mutex_t mLock;
    // Protects roleSwitch operation
    pthread_mutex_t mRoleSwitchLock;
    // Threads waiting for the partner to come back wait here
    pthread_cond_t mPartnerCV;
    // lock protecting mPartnerCV
    pthread_mutex_t mPartnerLock;
    // Variable to signal partner coming back online after type switch
    bool mPartnerUp;

    // Dump apis
    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override;
    void cmdDump(int fd, const hidl_vec<hidl_string>& options);
    void cmdHelp(int fd);
    void cmdList(int fd, const hidl_vec<hidl_string>& options);
    void cmdDumpDevice(int fd, const hidl_vec<hidl_string>& options);

private:
    pthread_t mPoll;
};

} // namespace implementation
} // namespace V1_3
} // namespace usb
} // namespace hardware
} // namespace android

#endif // ANDROID_HARDWARE_USB_V1_3_USB_H