#include <unistd.h>
#include <fstab/fstab.h>
#include <android-base/logging.h>
#include <android-base/properties.h>
#include <gtest/gtest.h>

using android::fs_mgr::Fstab;

constexpr char kDefaultFstabPath[] = "/vendor/etc/fstab.";

bool usb_disk_mount_point_valid()
{
    bool udisk_accessiable = false;
    std::string udisk_block_device;
    Fstab fstab_entries;
    auto hardware = android::base::GetProperty("ro.hardware", "");
    ReadFstabFromFile(kDefaultFstabPath + hardware, &fstab_entries);
    if(fstab_entries.empty()) {
        LOG(ERROR) << "Fail to read the fstab file";
        return udisk_accessiable;
    }
    for (auto it : fstab_entries) {
        /* 1. The path for mount udisk often has "usb" in it
         * 2. The value for "voldmanaged" parameter, it often has "usb" string,
         *    it's a label name give by us.
         */
        if((it.blk_device.find("usb") != it.blk_device.npos) || it.label == "usb") {
            udisk_block_device = it.blk_device;
            /* In vold, when usb disk is plugged, vold compares the udisk block device uevent path
             * with the "blk_device" found in the fstab file, with fnmatch() library function.
             *
             * udisk block device uevent is not triggered with this test, and the "blk_device" in
             * fstab file for udisk now has only one wildcard at the end, remove that wildcard
             * and check whether it can be accessible.
             */
            udisk_block_device.erase(udisk_block_device.begin() + (udisk_block_device.size()-1));
            if( access(("/sys" + udisk_block_device).c_str(), F_OK) == 0)
                udisk_accessiable = true;
        }
    }

    return udisk_accessiable;
}

// check whether a udisk can be well mounted
TEST(StorageModuleTest, UsbDiskTest)
{
    ASSERT_TRUE(usb_disk_mount_point_valid());
}
