/*
 * Copyright 2024 NXP
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

#include <android-base/logging.h>
#include <android-base/file.h>
#include <android-base/properties.h>
#include <fbmiscdata_rw.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>

#define FASTBOOT_PARTITION_FBMISC "/dev/block/by-name/fbmisc"

fbmiscError fbmiscDataBlockLock::readDeviceUnlockPermission(uint8_t* status) {
    int file_fd = -1;
    std::string mDataBlockFile;
    ssize_t ret = -1;

    mDataBlockFile = FASTBOOT_PARTITION_FBMISC;

    file_fd = open(mDataBlockFile.c_str(), O_RDWR);
    if (file_fd < 0) {
        LOG(INFO) << "OemLock: can not open: " << mDataBlockFile;
        return fbmiscError::FBMISC_ERROR_INTERNAL;
    }

    if (lseek(file_fd, -1, SEEK_END) == -1) {
        LOG(INFO) << "OemLock: lseek error: " << mDataBlockFile;
        return fbmiscError::FBMISC_ERROR_INTERNAL;
    }

    ret = read(file_fd, status, 1);

    close(file_fd);

    if (ret == 1)
        return fbmiscError::FBMISC_ERROR_NONE;
    else
        return fbmiscError::FBMISC_ERROR_INTERNAL;
}

fbmiscError fbmiscDataBlockLock::writeDeviceUnlockPermission(uint8_t status) {
    int file_fd = -1;
    std::string mDataBlockFile;
    ssize_t ret = -1;

    mDataBlockFile = FASTBOOT_PARTITION_FBMISC;

    file_fd = open(mDataBlockFile.c_str(), O_RDWR);
    if (file_fd < 0) {
        LOG(INFO) << "OemLock: can not open: " << mDataBlockFile;
        return fbmiscError::FBMISC_ERROR_INTERNAL;
    }

    if (lseek(file_fd, -1, SEEK_END) == -1) {
        LOG(INFO) << "OemLock: lseek error: " << mDataBlockFile;
        return fbmiscError::FBMISC_ERROR_INTERNAL;
    }

    ret = write(file_fd, &status, 1);

    close(file_fd);

    if (ret == 1)
        return fbmiscError::FBMISC_ERROR_NONE;
    else
        return fbmiscError::FBMISC_ERROR_INTERNAL;
}
