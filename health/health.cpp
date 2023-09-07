/*
 * Copyright (C) 2020 The Android Open Source Project
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
#define LOG_TAG "android.hardware.health@2.1-impl-imx"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <cutils/properties.h>
#include <health/utils.h>
#include <health2impl/Health.h>

#include <memory>
#include <string_view>

using ::android::sp;
using ::android::base::EqualsIgnoreCase;
using ::android::base::StringPrintf;
using ::android::base::WriteStringToFd;
using ::android::hardware::Return;
using ::android::hardware::Void;
using ::android::hardware::health::InitHealthdConfig;
using ::android::hardware::health::V1_0::BatteryStatus;
using ::android::hardware::health::V2_0::DiskStats;
using ::android::hardware::health::V2_0::Result;
using ::android::hardware::health::V2_1::IHealth;
using namespace std::literals;

static constexpr size_t kDiskStatsSize = 11;
#define PROP_BOOT_DEVICE_ROOT "ro.boot.boot_device_root"

void get_disk_stats(std::vector<struct DiskStats>& vec_stats) {
    char boot_device_root[PROP_VALUE_MAX];
    char kDiskStatsFile[PROP_VALUE_MAX];
    DiskStats stats = {};
    std::string mmcblk_link_path;

    stats.attr.isInternal = true;
    stats.attr.isBootDevice = true;
    stats.attr.name = "micron";

    property_get(PROP_BOOT_DEVICE_ROOT, boot_device_root, "mmcblk0");
    sprintf(kDiskStatsFile, "/sys/block/%s/stat", boot_device_root);
    std::string buffer;
    if (!android::base::ReadFileToString(std::string(kDiskStatsFile), &buffer)) {
        LOG(ERROR) << kDiskStatsFile << ": ReadFileToString failed.";
        return;
    }

    // Regular diskstats entries
    std::stringstream ss(buffer);
    for (uint i = 0; i < kDiskStatsSize; i++) {
        ss >> *(reinterpret_cast<uint64_t*>(&stats) + i);
    }

    vec_stats.resize(1);
    vec_stats[0] = stats;

    return;
}

namespace android {
namespace hardware {
namespace health {
namespace V2_1 {
namespace implementation {

// Health HAL implementation for imx. Note that in this implementation, imx
// pretends to be a device with a battery being charged. Implementations on real devices
// should not insert these fake values. For example, a battery-less device should report
// batteryPresent = false and batteryStatus = UNKNOWN.

class HealthImpl : public Health {
public:
    HealthImpl(std::unique_ptr<healthd_config>&& config) : Health(std::move(config)) {}
    Return<void> getChargeCounter(getChargeCounter_cb _hidl_cb) override;
    Return<void> getCurrentNow(getCurrentNow_cb _hidl_cb) override;
    Return<void> getCapacity(getCapacity_cb _hidl_cb) override;
    Return<void> getChargeStatus(getChargeStatus_cb _hidl_cb) override;
    Return<void> shouldKeepScreenOn(Health::shouldKeepScreenOn_cb _hidl_cb) override;
    Return<void> getDiskStats(Health::getDiskStats_cb _hidl_cb) override;
    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override;
    void cmdDump(int fd, const hidl_vec<hidl_string>& options);
    void cmdHelp(int fd);
    void cmdList(int fd, const hidl_vec<hidl_string>& options);
    void cmdDumpDevice(int fd, const hidl_vec<hidl_string>& options);

protected:
    void UpdateHealthInfo(HealthInfo* health_info) override;
};

void HealthImpl::UpdateHealthInfo(HealthInfo* health_info) {
    auto* battery_props = &health_info->legacy.legacy;
    battery_props->maxChargingCurrent = 500000;
    battery_props->maxChargingVoltage = 5000000;
    battery_props->batteryStatus = V1_0::BatteryStatus::CHARGING;
    battery_props->batteryHealth = V1_0::BatteryHealth::GOOD;
    battery_props->batteryPresent = true;
    battery_props->batteryLevel = 85;
    battery_props->batteryVoltage = 3600;
    battery_props->batteryTemperature = 350;
    battery_props->batteryCurrent = 400000;
    battery_props->batteryCycleCount = 32;
    battery_props->batteryFullCharge = 4000000;
    battery_props->batteryChargeCounter = 1900000;
    battery_props->batteryTechnology = "Li-ion";
}

Return<void> HealthImpl::getChargeCounter(getChargeCounter_cb _hidl_cb) {
    _hidl_cb(Result::SUCCESS, 1900000);
    return Void();
}

Return<void> HealthImpl::getCurrentNow(getCurrentNow_cb _hidl_cb) {
    _hidl_cb(Result::SUCCESS, 400000);
    return Void();
}

Return<void> HealthImpl::getCapacity(getCapacity_cb _hidl_cb) {
    _hidl_cb(Result::SUCCESS, 85);
    return Void();
}

Return<void> HealthImpl::getChargeStatus(getChargeStatus_cb _hidl_cb) {
    _hidl_cb(Result::SUCCESS, BatteryStatus::CHARGING);
    return Void();
}

Return<void> HealthImpl::shouldKeepScreenOn(Health::shouldKeepScreenOn_cb _hidl_cb) {
    _hidl_cb(Result::SUCCESS, true);
    return Void();
}

Return<void> HealthImpl::getDiskStats(getDiskStats_cb _hidl_cb) {
    std::vector<struct DiskStats> stats;
    get_disk_stats(stats);
    hidl_vec<struct DiskStats> stats_vec(stats);
    if (!stats.size()) {
        _hidl_cb(Result::NOT_SUPPORTED, stats_vec);
    } else {
        _hidl_cb(Result::SUCCESS, stats_vec);
    }
    return Void();
}

Return<void> HealthImpl::debug(const hidl_handle& fd, const hidl_vec<hidl_string>& options) {
    if (fd.getNativeHandle() != nullptr && fd->numFds > 0) {
        cmdDump(fd->data[0], options);
    } else {
        LOG(ERROR) << "Given file descriptor is not valid.";
    }

    return {};
}

void HealthImpl::cmdDump(int fd, const hidl_vec<hidl_string>& options) {
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

void HealthImpl::cmdHelp(int fd) {
    WriteStringToFd("--help: shows this help.\n"
                    "--list: [HealthInfo|DiskStats|all]: lists all the dump options: HealthInfo or "
                    "DiskStats or all\n"
                    "available to Health Hal.\n"
                    "--dump HealthInfo: shows current status of the HealthInfo\n"
                    "--dump DiskStats: shows current status of the DiskStats\n",
                    fd);
    return;
}

void HealthImpl::cmdList(int fd, const hidl_vec<hidl_string>& options) {
    bool listHealthInfo = false;
    bool listDiskStats = false;
    if (options.size() > 1) {
        const std::string option = options[1];
        const bool listAll = EqualsIgnoreCase(option, "all");
        listHealthInfo = listAll || EqualsIgnoreCase(option, "HealthInfo");
        listDiskStats = listAll || EqualsIgnoreCase(option, "DiskStats");
        if (!listHealthInfo && !listDiskStats) {
            WriteStringToFd(StringPrintf("Unrecognized option is ignored.\n\n"), fd);
            cmdHelp(fd);
            return;
        }
        if (listHealthInfo) {
            WriteStringToFd(StringPrintf("list listHealthInfo dump options, default is --list "
                                         "listHealthInfo.\n"),
                            fd);
        }
        if (listDiskStats) {
            WriteStringToFd(StringPrintf("list listDiskStats dump options, default is --list "
                                         "listDiskStats.\n"),
                            fd);
        }
    } else {
        WriteStringToFd(StringPrintf("Invalid input, need to append list option.\n\n"), fd);
        cmdHelp(fd);
    }
}

void HealthImpl::cmdDumpDevice(int fd, const hidl_vec<hidl_string>& options) {
    bool listHealthInfo = false;
    bool listDiskStats = false;
    if (options.size() > 1) {
        const std::string option = options[1];
        const bool listAll = EqualsIgnoreCase(option, "all");
        listHealthInfo = listAll || EqualsIgnoreCase(option, "HealthInfo");
        listDiskStats = listAll || EqualsIgnoreCase(option, "DiskStats");
        if (!listHealthInfo && !listDiskStats) {
            WriteStringToFd(StringPrintf("Unrecognized option is ignored.\n\n"), fd);
            cmdHelp(fd);
            return;
        }
        if (listHealthInfo) {
            Health::getHealthInfo_2_1([fd](auto res, const auto& info) {
                WriteStringToFd("\ngetHealthInfo -> ", fd);
                if (res == Result::SUCCESS) {
                    WriteStringToFd(toString(info), fd);
                } else {
                    WriteStringToFd(toString(res), fd);
                }
                WriteStringToFd("\n", fd);
            });
        }
        if (listDiskStats) {
            getDiskStats([fd](auto res, const auto& info) {
                WriteStringToFd("\ngetDiskStats -> ", fd);
                if (res == Result::SUCCESS) {
                    WriteStringToFd(toString(info), fd);
                } else {
                    WriteStringToFd(toString(res), fd);
                }
                WriteStringToFd("\n", fd);
            });
        }
    } else {
        WriteStringToFd(StringPrintf("Invalid input, need to append dump option.\n\n"), fd);
        cmdHelp(fd);
    }
}

} // namespace implementation
} // namespace V2_1
} // namespace health
} // namespace hardware
} // namespace android

extern "C" IHealth* HIDL_FETCH_IHealth(const char* instance) {
    using ::android::hardware::health::V2_1::implementation::HealthImpl;
    if (instance != "default"sv) {
        return nullptr;
    }
    auto config = std::make_unique<healthd_config>();
    InitHealthdConfig(config.get());

    return new HealthImpl(std::move(config));
}
