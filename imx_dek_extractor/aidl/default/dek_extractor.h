/*
 * Copyright (C) 2015 The Android Open Source Project
 * Copyright 2023 NXP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *              http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <aidl/android/hardware/imx_dek_extractor/BnDek_Extractor.h>
#include <stdint.h>
#include <trusty/tipc.h>

namespace aidl {
namespace android {
namespace hardware {
namespace imx_dek_extractor {

#define HWCRYPTO_PORT "com.android.trusty.hwcrypto"

/**
 * enum hwcrypto_cmd - command identifiers for hwcrypto functions
 */
enum hwcrypto_cmd {
    HWCRYPTO_RESP_BIT = 1,
    HWCRYPTO_REQ_SHIFT = 1,

    HWCRYPTO_HASH = (1 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_ENCAP_BLOB = (2 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_GEN_RNG    = (3 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_GEN_BKEK    = (4 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_LOCK_BOOT_STATE    = (5 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_PROVISION_WV_KEY   = (6 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_PROVISION_WV_KEY_ENC = (7 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_GEN_DEK_BLOB         = (8 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_SET_EMMC_CID         = (9 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_PROVISION_FIRMWARE_SIGN_KEY = (10 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_PROVISION_FIRMWARE_ENCRYPT_KEY = (11 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_PROVISION_SPL_DEK_BLOB            = (12 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_PROVISION_BOOTLOADER_DEK_BLOB     = (13 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_GET_SPL_DEK_BLOB                  = (14 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_GET_SPL_DEK_BLOB_SIZE             = (15 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_GET_BOOTLOADER_DEK_BLOB           = (16 << HWCRYPTO_REQ_SHIFT),
    HWCRYPTO_GET_BOOTLOADER_DEK_BLOB_SIZE      = (17 << HWCRYPTO_REQ_SHIFT),
};

/**
 * enum hwcrypto_err - error codes for hwcrypto protocol
 * @HWCRYPTO_ERROR_NONE:             all OK
 * @HWCRYPTO_ERROR_INVALID:          Invalid input
 * @HWCRYPTO_ERROR_INTERNAL:         Error occurred during an operation in Trusty
 */
enum hwcrypto_err {
    HWCRYPTO_ERROR_NONE     = 0,
    HWCRYPTO_ERROR_INVALID  = 1,
    HWCRYPTO_ERROR_INTERNAL = 2,
};

struct hwcrypto_msg {
    uint32_t cmd;
    uint32_t status;
    uint8_t payload[0];
};

struct hwcrypto_header {
    uint32_t cmd;
} __packed;

typedef enum ERROR_TYPE {
    NO_ERROR = 0,
    FILE_ERROR = -1,
    IMAGE_ERROR = -2,
    MEMORY_ERROR = -3,
    CONNECT_ERROR = -4,
    PARAMETER_ERROR = -5,
    REQUSET_ERROR = -6,
} error_t;

typedef enum IMAGE_TYPE {
    IMAGE_NONE = 0,
    SPL,
    BOOTLOADER,
} image_type_t;

typedef enum REQUEST_TYPE {
    REQUEST_NONE = 0,
    REQUEST_SIZE,
    REQUEST_DATA,
} request_type_t;

class Dek_Extractor : public BnDek_Extractor {
    ::ndk::ScopedAStatus Dek_ExtractorInit(std::vector<unsigned char>* dek_blob, int32_t dek_blob_type,
            int32_t* _aidl_return) override;
private:
    int dek_extractor(std::vector<unsigned char>* dek_blob, image_type_t image_t);
};

}  // namespace imx_dek_extractor
}  // namespace hardware
}  // namespace android
}  // namespace aidl
