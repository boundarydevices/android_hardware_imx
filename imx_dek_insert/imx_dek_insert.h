/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <stdint.h>

typedef enum ERROR_TYPE {
    NO_ERROR = 0,
    FILE_ERROR = -1,
    IMAGE_ERROR = -2,
    MEMORY_ERROR = -3,
    CONNECT_ERROR = -4,
    PARAMETER_ERROR = -5,
    REQUSET_ERROR = -6,
    OTHER_ERROR = -7,
} error_t;

typedef enum SOC_TYPE {
    SOC_NONE = 0,
    MM,
    MN,
    MP,
    MQ,
    QX,
    QM,
    DXL,
    ULP,
    IMX9
} soc_type_t;

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

typedef struct __attribute__ ((packed)) ahab_container_header_s {
    /* offset 0x0*/
    uint8_t version;
    uint16_t length;
    uint8_t tag;
    /* offset 0x4*/
    uint32_t flags;
    /* offset 0x8*/
    uint16_t sw_version;
    uint8_t fuse_version;
    uint8_t nrImages;
    /* offset 0xc*/
    uint16_t signature_block_offset;
    uint16_t reserved;
} ahab_container_header_t;

typedef struct __attribute__ ((packed)) ahab_container_signature_block_s {
    /* offset 0x0 */
    uint8_t version;
    uint16_t length;
    uint8_t tag;
    /* offset 0x4 */
    uint16_t certificate_offset;
    uint16_t srk_table_offset;
    /* offset 0x8 */
    uint16_t signature_offset;
    uint16_t blob_offset;
    /* offset 0xc */
    uint32_t key_identifier;
    /* offset 0x10 */
} ahab_container_signature_block_t;

typedef struct __attribute__ ((packed)) byte_str_s
{
    uint8_t version;
    uint16_t length;
    uint8_t tag;
} byte_str_t;

struct trusty_ipc_iovec {
    void *base;
    size_t len;
};
