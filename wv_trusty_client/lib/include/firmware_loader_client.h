/*
 * Copyright 2023 NXP.
 */

#pragma once

#include <stdint.h>

#define FIRMWARE_LOADER_PORT "com.android.trusty.firmwareloader"

enum firmware_loader_command : uint32_t {
    FIRMWARE_LOADER_REQ_SHIFT = 1,
    FIRMWARE_LOADER_RESP_BIT = 1,

    FIRMWARE_LOADER_CMD_LOAD_FIRMWARE = (0 << FIRMWARE_LOADER_REQ_SHIFT),
};

enum firmware_loader_error : uint32_t {
    FIRMWARE_LOADER_NO_ERROR = 0,
    FIRMWARE_LOADER_ERR_UNKNOWN_CMD,
    FIRMWARE_LOADER_ERR_INVALID_CMD,
    FIRMWARE_LOADER_ERR_NO_MEMORY,
    FIRMWARE_LOADER_ERR_VERIFICATION_FAILED,
    FIRMWARE_LOADER_ERR_LOADING_FAILED,
    FIRMWARE_LOADER_ERR_ALREADY_EXISTS,
    FIRMWARE_LOADER_ERR_INTERNAL,
    FIRMWARE_LOADER_ERR_INVALID_VERSION,
    FIRMWARE_LOADER_ERR_POLICY_VIOLATION,
    FIRMWARE_LOADER_ERR_NOT_ENCRYPTED,
};

struct firmware_loader_header {
    uint32_t cmd;
} __packed;

struct firmware_loader_load_firmware_req {
    uint64_t package_size;
} __packed;

struct firmware_loader_resp {
    struct firmware_loader_header hdr;
    uint32_t error;
} __packed;

ssize_t load_firmware_package(const char* firmware_file_name);
