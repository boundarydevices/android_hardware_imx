/*
 * Copyright 2016 The Android Open Source Project
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

#define LOG_TAG "dumpstate"

#include "DumpstateDevice.h"

#include <android-base/properties.h>
#include <android-base/unique_fd.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <android-base/logging.h>
#include <android-base/file.h>
#include <cutils/properties.h>
#include <hidl/HidlBinderSupport.h>

#include <log/log.h>
#include <string.h>
#include <dirent.h>

#include "DumpstateUtil.h"

#define VENDOR_VERBOSE_LOGGING_ENABLED_PROPERTY "persist.vendor.verbose_logging_enabled"

using android::base::EqualsIgnoreCase;
using android::base::StringPrintf;
using android::base::WriteStringToFd;

using android::os::dumpstate::CommandOptions;
using android::os::dumpstate::DumpFileToFd;
using android::os::dumpstate::PropertiesHelper;
using android::os::dumpstate::RunCommandToFd;

namespace android {
namespace hardware {
namespace dumpstate {
namespace V1_1 {
namespace implementation {

// Methods from ::android::hardware::dumpstate::V1_0::IDumpstateDevice follow.
Return<void> DumpstateDevice::dumpstateBoard(const hidl_handle& handle) {
    /* Use default. */
    dumpstateBoard_1_1(handle, DumpstateMode::DEFAULT, 30 * 1000 /* timeoutMillis */);
    return Void();
}

Return<DumpstateStatus> DumpstateDevice::dumpstateBoard_1_1(const hidl_handle& handle,
                                                            const DumpstateMode mode,
                                                            const uint64_t timeoutMillis) {
    // Unused timeoutMillis, fix build errors.
    (void)timeoutMillis;

    // Exit when dump is completed since this is a lazy HAL.
    addPostCommandTask([]() {
        exit(0);
    });

    if (handle == nullptr || handle->numFds < 1) {
        ALOGE("no FDs\n");
        return DumpstateStatus::ILLEGAL_ARGUMENT;
    }

    // read the fd
    int fd = handle->data[0];
    if (fd < 0) {
        ALOGE("invalid FD: %d\n", handle->data[0]);
        return DumpstateStatus::ILLEGAL_ARGUMENT;
    }

    // check the modes, all supported modes will be equal to "DumpstateMode::DEFAULT".
    bool isModeValid = false;
    for (const auto dumpstateMode:hidl_enum_range<DumpstateMode>()) {
        if (mode == dumpstateMode) {
            isModeValid = true;
            break;
        }
    }
    if (!isModeValid) {
        ALOGE("Invalid mode: %d\n", mode);
        return DumpstateStatus::ILLEGAL_ARGUMENT;
    }

    RunCommandToFd(fd, "VENDOR PROPERTIES", {"/vendor/bin/getprop"});
    DumpFileToFd(fd, "CPU present", "/sys/devices/system/cpu/present");
    DumpFileToFd(fd, "CPU online", "/sys/devices/system/cpu/online");

    DumpFileToFd(fd, "INTERRUPTS", "/proc/interrupts");
    DumpFileToFd(fd, "dmabuf info", "/d/dma_buf/bufinfo");
    RunCommandToFd(fd, "Temperatures", {"/vendor/bin/sh", "-c", "for f in /sys/class/thermal/thermal* ; do type=`cat $f/type` ; temp=`cat $f/temp` ; echo \"$type: $temp\" ; done"});
    RunCommandToFd(fd, "Cooling Device Current State", {"/vendor/bin/sh", "-c", "for f in /sys/class/thermal/cooling* ; do type=`cat $f/type` ; temp=`cat $f/cur_state` ; echo \"$type: $temp\" ; done"});
    RunCommandToFd(fd, "CPU time-in-state", {"/vendor/bin/sh", "-c", "for cpu in /sys/devices/system/cpu/cpu*; do f=$cpu/cpufreq/stats/time_in_state; if [ ! -f $f ]; then continue; fi; echo $f:; cat $f; done"});
    RunCommandToFd(fd, "CPU cpuidle", {"/vendor/bin/sh", "-c", "for cpu in /sys/devices/system/cpu/cpu*; do for d in $cpu/cpuidle/state*; do if [ ! -d $d ]; then continue; fi; echo \"$d: `cat $d/name` `cat $d/desc` `cat $d/time` `cat $d/usage`\"; done; done"});
    RunCommandToFd(fd, "USB Device Descriptors", {"/vendor/bin/sh", "-c", "cd /sys/bus/usb/devices/1-1 && cat product && cat bcdDevice; cat descriptors | od -t x1 -w16 -N96"});
    RunCommandToFd(fd, "Power supply properties", {"/vendor/bin/sh", "-c", "for f in `ls /sys/class/power_supply/*/uevent` ; do echo \"------ $f\\n`cat $f`\\n\" ; done"});

    return DumpstateStatus::OK;
}

Return<void> DumpstateDevice::setVerboseLoggingEnabled(const bool enable) {
    android::base::SetProperty(VENDOR_VERBOSE_LOGGING_ENABLED_PROPERTY, enable ? "true" : "false");
    return Void();
}

Return<bool> DumpstateDevice::getVerboseLoggingEnabled() {
    return android::base::GetBoolProperty(VENDOR_VERBOSE_LOGGING_ENABLED_PROPERTY, false);
}

Return<void> DumpstateDevice::debug(const hidl_handle& fd , const hidl_vec<hidl_string>& options) {
    if (fd.getNativeHandle() != nullptr && fd->numFds > 0) {
        cmdDump(fd->data[0], options);
    } else {
        LOG(ERROR) << "Given file descriptor is not valid.";
    }

    return {};
}

void DumpstateDevice::cmdDump(int fd, const hidl_vec<hidl_string>& options) {
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
        WriteStringToFd(StringPrintf("Invalid option: %s\n", option.c_str()),fd);
        cmdHelp(fd);
    }
}

void DumpstateDevice::cmdHelp(int fd) {
    WriteStringToFd("--help: shows this help.\n"
                    "--list: [option1|option2|...|all]: lists all the dump options: option1 or option2 or ... or all\n"
                    "available to DumpstateDevice Hal.\n"
                    "--dump option1: shows current status of the option1\n"
                    "--dump option2: shows current status of the option2\n"
                    "--dump all: shows current status of all the options\n", fd);
    return;
}

void DumpstateDevice::cmdList(int fd, const hidl_vec<hidl_string>& options) {
    bool listoption1 = false;
    bool listoption2 = false;
    if (options.size() > 1) {
        const std::string option = options[1];
        const bool listAll = EqualsIgnoreCase(option, "all");
        listoption1 = listAll || EqualsIgnoreCase(option, "option1");
        listoption2 = listAll || EqualsIgnoreCase(option, "option2");
        if (!listoption1 && !listoption2) {
            WriteStringToFd(StringPrintf("Unrecognized option is ignored.\n\n"),fd);
            cmdHelp(fd);
            return;
        }
        if(listoption1) {
            WriteStringToFd(StringPrintf("list option1 dump options, default is --list listoption1.\n"),fd);
         }

        if(listoption2) {
            WriteStringToFd(StringPrintf("list option2 dump options, default is --list listoption2.\n"),fd);
        }
    } else {
        WriteStringToFd(StringPrintf("Invalid input, need to append list option.\n\n"),fd);
        cmdHelp(fd);
     }
}

void DumpstateDevice::cmdDumpDevice(int fd, const hidl_vec<hidl_string>& options) {
    bool listoption1 = false;
    bool listoption2 = false;
    if (options.size() > 1) {
        const std::string option = options[1];
        const bool listAll = EqualsIgnoreCase(option, "all");
        listoption1 = listAll || EqualsIgnoreCase(option, "option1");
        listoption2 = listAll || EqualsIgnoreCase(option, "option2");
        if (!listoption1 && !listoption2) {
            WriteStringToFd(StringPrintf("Unrecognized option is ignored.\n\n"),fd);
            cmdHelp(fd);
            return;
        }
        if(listoption1) {
            WriteStringToFd(StringPrintf("dump option1 info.\n"),fd);
        }
        if(listoption2) {
            WriteStringToFd(StringPrintf("dump option2 info.\n"),fd);
        }
    } else {
        WriteStringToFd(StringPrintf("Invalid input, need to append dump option.\n\n"),fd);
        cmdHelp(fd);
    }
}

}  // namespace implementation
}  // namespace V1_1
}  // namespace dumpstate
}  // namespace hardware
}  // namespace android
