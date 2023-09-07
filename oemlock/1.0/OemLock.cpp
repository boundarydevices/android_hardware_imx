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

#include <android-base/endian.h>
#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>

#include <memory>

using android::base::EqualsIgnoreCase;
using android::base::StringPrintf;
using android::base::WriteStringToFd;

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

    avbError rc = mAvbOemUnlockIpc.writeDeviceUnlockPermission(allowed ? 1 : 0);
    if (rc != avbError::AVB_ERROR_NONE) {
        LOG(ERROR) << "Failed to set device unlock status!";
        return OemLockStatus::FAILED;
    } else
        return OemLockStatus::OK;
}

Return<void> OemLock::isOemUnlockAllowedByDevice(isOemUnlockAllowedByDevice_cb _hidl_cb) {
    uint8_t status;

    LOG(VERBOSE) << "Running OemLock::isOemUnlockAllowedByDevice";

    avbError rc = mAvbOemUnlockIpc.readDeviceUnlockPermission(&status);
    if (rc != avbError::AVB_ERROR_NONE) {
        LOG(ERROR) << "Failed to set device unlock status!";
        _hidl_cb(OemLockStatus::FAILED, false);
    } else {
        _hidl_cb(OemLockStatus::OK, status ? true : false);
    }

    return Void();
}

Return<void> OemLock::debug(const hidl_handle& fd, const hidl_vec<hidl_string>& options) {
    if (fd.getNativeHandle() != nullptr && fd->numFds > 0) {
        cmdDump(fd->data[0], options);
    } else {
        LOG(ERROR) << "Given file descriptor is not valid.";
    }

    return {};
}

void OemLock::cmdDump(int fd, const hidl_vec<hidl_string>& options) {
    if (options.size() == 0) {
        WriteStringToFd("No option is given.\n", fd);
        cmdHelp(fd);
        return;
    }

    const std::string option = options[0];
    if (EqualsIgnoreCase(option, "--help")) {
        cmdHelp(fd);
    } else if (EqualsIgnoreCase(option, "--list")) {
        cmdList(fd, options);
    } else if (EqualsIgnoreCase(option, "--dump")) {
        cmdDumpDevice(fd, options);
    } else {
        WriteStringToFd(StringPrintf("Invalid option: %s\n", option.c_str()), fd);
        cmdHelp(fd);
    }
}

void OemLock::cmdHelp(int fd) {
    WriteStringToFd("--help: shows this help.\n"
                    "--list: [option1|option2|...|all]: lists all the dump options: option1 or "
                    "option2 or ... or all\n"
                    "available to OemLock Hal.\n"
                    "--dump option1: shows current status of the option1\n"
                    "--dump option2: shows current status of the option2\n"
                    "--dump all: shows current status of all the options\n",
                    fd);
    return;
}

void OemLock::cmdList(int fd, const hidl_vec<hidl_string>& options) {
    bool listoption1 = false;
    bool listoption2 = false;
    if (options.size() > 1) {
        const std::string option = options[1];
        const bool listAll = EqualsIgnoreCase(option, "all");
        listoption1 = listAll || EqualsIgnoreCase(option, "option1");
        listoption2 = listAll || EqualsIgnoreCase(option, "option2");
        if (!listoption1 && !listoption2) {
            WriteStringToFd(StringPrintf("Unrecognized option is ignored.\n\n"), fd);
            cmdHelp(fd);
            return;
        }
        if (listoption1) {
            WriteStringToFd(StringPrintf(
                                    "list option1 dump options, default is --list listoption1.\n"),
                            fd);
        }

        if (listoption2) {
            WriteStringToFd(StringPrintf(
                                    "list option2 dump options, default is --list listoption2.\n"),
                            fd);
        }
    } else {
        WriteStringToFd(StringPrintf("Invalid input, need to append list option.\n\n"), fd);
        cmdHelp(fd);
    }
}

void OemLock::cmdDumpDevice(int fd, const hidl_vec<hidl_string>& options) {
    bool listoption1 = false;
    bool listoption2 = false;
    if (options.size() > 1) {
        const std::string option = options[1];
        const bool listAll = EqualsIgnoreCase(option, "all");
        listoption1 = listAll || EqualsIgnoreCase(option, "option1");
        listoption2 = listAll || EqualsIgnoreCase(option, "option2");
        if (!listoption1 && !listoption2) {
            WriteStringToFd(StringPrintf("Unrecognized option is ignored.\n\n"), fd);
            cmdHelp(fd);
            return;
        }
        if (listoption1) {
            WriteStringToFd(StringPrintf("dump option1 info.\n"), fd);
        }
        if (listoption2) {
            WriteStringToFd(StringPrintf("dump option2 info.\n"), fd);
        }
    } else {
        WriteStringToFd(StringPrintf("Invalid input, need to append dump option.\n\n"), fd);
        cmdHelp(fd);
    }
}

} // namespace oemlock
} // namespace hardware
} // namespace android
