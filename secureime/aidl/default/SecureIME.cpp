/*
 * Copyright (C) 2020 The Android Open Source Project
 * Copyright 2022 NXP
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

#include "SecureIME.h"

namespace aidl {
namespace nxp {
namespace hardware {
namespace secureime {

using ::ndk::ScopedFileDescriptor;

constexpr const char kTrustyDefaultDeviceName[] = "/dev/trusty-ipc-dev0";

int SecureIME::connectIME() {
    tipc_fd = tipc_connect(kTrustyDefaultDeviceName, SECUREIME_PORT_NAME);
    if (tipc_fd < 0) {
        ALOGE("Failed to connect to Trusty secureime app! ret: %d", tipc_fd);
        return -1;
    }

    return 0;
}

void SecureIME::closeIME() {
    tipc_close(tipc_fd);
    tipc_fd = -1;
}

int SecureIME::tipc_read(struct secureime_resp* resp, int cmd) {
    int rc = 0;

    rc = read(tipc_fd, resp, sizeof(struct secureime_resp));
    if ((rc < 0) || (rc != sizeof(struct secureime_resp))) {
        ALOGE("Failed to read message!");
        goto err;
    }

    if (resp->cmd != (cmd | SECURE_IME_RESP_BIT)) {
        ALOGE("command is invalid!");
        rc = -1;
        goto err;
    }

    if (resp->result != 0) {
        ALOGE("tipc get error: %d!", resp->result);
        rc = resp->result;
        goto err;
    }

err:
    return rc;
}

int SecureIME::secureIMEInit(int fd, int buffer_size, int stride, int width, int height) {
    int rc;
    struct secureime_resp resp;

    struct secureime_req req = {
            .cmd = SECURE_IME_CMD_INIT,
            .buffer_size = buffer_size,
            .stride = stride,
            .width = width,
            .height = height,
            .x = 0,
            .y = 0,
    };

    struct iovec tx = {&req, sizeof(req)};
    struct trusty_shm shm = {
            .fd = fd,
            .transfer = TRUSTY_LEND,
    };

    rc = tipc_send(tipc_fd, &tx, 1, &shm, 1);
    if (rc < 0) {
        ALOGE("Failed to send message!");
        goto err;
    }

    rc = tipc_read(&resp, SECURE_IME_CMD_INIT);

err:
    return rc;
}

int SecureIME::secureIMEHandleTouch(int x, int y, int* key) {
    int rc = 0;
    struct secureime_resp resp;

    struct secureime_req req = {
            .cmd = SECURE_IME_CMD_INPUT,
            .x = x,
            .y = y,
    };
    struct iovec tx = {&req, sizeof(req)};

    rc = tipc_send(tipc_fd, &tx, 1, NULL, 0);
    if (rc < 0) {
        ALOGE("Failed to send message!");
        goto err;
    }

    rc = tipc_read(&resp, SECURE_IME_CMD_INPUT);
    if (rc < 0) {
        ALOGE("Failed to read message!");
        goto err;
    }

    *key = resp.key;
    return 0;

err:
    return -1;
}

int SecureIME::secureIMEExit() {
    struct secureime_req req = {
            .cmd = SECURE_IME_CMD_EXIT,
    };

    struct iovec tx = {&req, sizeof(req)};

    return tipc_send(tipc_fd, &tx, 1, NULL, 0);
}

::ndk::ScopedAStatus SecureIME::SecureIMEInit(const ::ndk::ScopedFileDescriptor& in_fd,
                                              int32_t in_buffer_size, int32_t in_stride,
                                              int32_t in_width, int32_t in_height,
                                              int32_t* _aidl_return) {
    if (connectIME()) {
        *_aidl_return = -1;
        return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
    }

    int fd = in_fd.get();
    if ((fd == -1) || (secureIMEInit(fd, in_buffer_size, in_stride, in_width, in_height) < 0)) {
        *_aidl_return = -1;
        return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
    }

    *_aidl_return = 0;
    return ndk::ScopedAStatus::ok();
}

::ndk::ScopedAStatus SecureIME::SecureIMEHandleTouch(int32_t in_x, int32_t in_y,
                                                     int32_t* _aidl_return) {
    if (secureIMEHandleTouch(in_x, in_y, _aidl_return) < 0) {
        return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
    } else {
        return ndk::ScopedAStatus::ok();
    }
}

::ndk::ScopedAStatus SecureIME::SecureIMEExit(int32_t* _aidl_return) {
    if (secureIMEExit() < 0) {
        *_aidl_return = -1;
        return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
    } else {
        closeIME();

        *_aidl_return = 0;
        return ndk::ScopedAStatus::ok();
    }
}

} // namespace secureime
} // namespace hardware
} // namespace nxp
} // namespace aidl
