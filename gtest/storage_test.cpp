/*
 * Copyright 2023 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <dirent.h>
#include <fnmatch.h>
#include <fstab/fstab.h>
#include <gtest/gtest.h>
#include <sys/types.h>
#include <unistd.h>

#include <cstring>
#include <string>

using android::fs_mgr::Fstab;

constexpr char kDefaultFstabPath[] = "/vendor/etc/fstab.";

bool udisk_accessiable = false;
std::string global_pattern;

std::string find_next_sub_pattern(std::string::size_type start_idx) {
    if (start_idx > global_pattern.size()) {
        return std::string();
    }
    std::string chop_head = global_pattern.substr(start_idx);
    auto end_idx = chop_head.find("/");
    if (end_idx == std::string::npos) {
        end_idx = chop_head.size();
    }

    auto ret = chop_head.substr(0, end_idx);
    return ret;
}

void entry_exist_or_not(std::string dir, std::string entry_pattern,
                        std::string::size_type entry_pattern_idx) {
    if (entry_pattern.empty()) {
        udisk_accessiable = true;
        return;
    }

    auto dir_instance = opendir(dir.c_str());
    if (!dir_instance) {
        return;
    }

    struct dirent *dir_entry;
    std::string next_entry_pattern;
    while ((dir_entry = readdir(dir_instance))) {
        if (!strncmp(dir_entry->d_name, ".", 1) || !strncmp(dir_entry->d_name, "..", 2))
            continue;

        if (!fnmatch(entry_pattern.c_str(), dir_entry->d_name, 0)) {
            next_entry_pattern =
                    find_next_sub_pattern(entry_pattern_idx + entry_pattern.size() + 1);
            entry_exist_or_not(dir + dir_entry->d_name + "/", next_entry_pattern,
                               entry_pattern_idx + entry_pattern.size() + 1);
        }
    }
    closedir(dir_instance);
}

bool usb_disk_mount_point_valid() {
    Fstab fstab_entries;
    auto hardware = android::base::GetProperty("ro.hardware", "");
    ReadFstabFromFile(kDefaultFstabPath + hardware, &fstab_entries);
    if (fstab_entries.empty()) {
        LOG(ERROR) << "Fail to read the fstab file";
        return udisk_accessiable;
    }
    for (auto it : fstab_entries) {
        /* 1. The path for mount udisk often has "usb" in it
         * 2. The value for "voldmanaged" parameter, it often has "usb" string,
         *    it's a label name give by us.
         */
        if ((it.blk_device.find("usb") != it.blk_device.npos) || it.label == "usb") {
            global_pattern = it.blk_device;
            /* In vold, when usb disk is plugged, vold compares the udisk block device uevent path
             * with the "blk_device" found in the fstab file, with fnmatch() library function.
             *
             * udisk block device uevent is not triggered with this test, and the "blk_device" in
             * fstab file for udisk now has regex in each filesystem entry name. iterate on each
             * entry to see whether the "blk_device" have corresponding path under sysfs.
             */
            auto next_sub_pattern = find_next_sub_pattern(1);
            entry_exist_or_not(std::string("/sys/"), next_sub_pattern, 1);
        }
    }

    return udisk_accessiable;
}

// check whether a udisk can be well mounted
TEST(StorageModuleTest, UsbDiskTest) {
    ASSERT_TRUE(usb_disk_mount_point_valid());
}
