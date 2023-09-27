/*
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

#include <android-base/logging.h>
#include <avb_ipc.h>
#include <stdio.h>
#include <string.h>
#include <sys/uio.h>
#include <trusty/tipc.h>

#define TRUSTY_DEVICE_NAME "/dev/trusty-ipc-dev0"
#define AVB_PORT "com.android.trusty.avb"

avbError avbOemUnlockIpc::avbCall(int handler, avbCommand cmd, void* in, uint32_t in_size,
                                  void* out, uint32_t* out_size) {
    struct iovec iov[2];
    struct avbMessage msg;
    int rc = 0;

    if (handler < 0) {
        LOG(ERROR) << "TIPC not inited!";
        return avbError::AVB_ERROR_INTERNAL;
    }

    /* construct input command buffer */
    msg.cmd = cmd;
    iov[0].iov_base = &msg;
    iov[0].iov_len = sizeof(struct avbMessage);
    iov[1].iov_base = in;
    iov[1].iov_len = in_size;

    rc = writev(handler, iov, 2);
    if (rc < 0) {
        LOG(ERROR) << "failed to send cmd" << cmd << "to:" << AVB_PORT;
        return avbError::AVB_ERROR_INTERNAL;
    }

    /* construct output command buffer */
    memset(&msg, 0, sizeof(struct avbMessage));
    iov[0].iov_base = &msg;
    iov[0].iov_len = sizeof(struct avbMessage);

    iov[1].iov_base = out;
    iov[1].iov_len = AVB_MAX_BUFFER_LENGTH;
    rc = readv(handler, iov, 2);
    if ((rc < 0) || (msg.result != avbError::AVB_ERROR_NONE)) {
        LOG(ERROR) << "failed to retrieve response for cmd:" << cmd << "to:" << AVB_PORT;
        return avbError::AVB_ERROR_INTERNAL;
    }
    if (out_size != NULL)
        *out_size = ((int)rc - sizeof(struct avbMessage));

    /* everything goes well, return OK. */
    return avbError::AVB_ERROR_NONE;
}

avbError avbOemUnlockIpc::readDeviceUnlockPermission(uint8_t* status) {
    int handler;
    uint32_t out_size;
    avbError rc;

    /* Prepare tipc channel for connect. */
    handler = tipc_connect(TRUSTY_DEVICE_NAME, AVB_PORT);
    if (handler < 0) {
        LOG(ERROR) << "failed to init tipc channel:" << handler;
        return avbError::AVB_ERROR_INTERNAL;
    }

    rc = avbCall(handler, avbCommand::READ_OEM_UNLOCK_DEVICE_PERMISSION, NULL, 0, status,
                 &out_size);

    /* Close Tipc channel */
    tipc_close(handler);

    if (rc != avbError::AVB_ERROR_NONE) {
        LOG(ERROR) << "failed to read device unlock status from trusty:" << rc;
        return avbError::AVB_ERROR_INTERNAL;
    } else
        return avbError::AVB_ERROR_NONE;
}

avbError avbOemUnlockIpc::writeDeviceUnlockPermission(uint8_t status) {
    int handler;
    avbError rc;

    /* Prepare tipc channel for connect. */
    handler = tipc_connect(TRUSTY_DEVICE_NAME, AVB_PORT);
    if (handler < 0) {
        LOG(ERROR) << "failed to init tipc channel:" << handler;
        return avbError::AVB_ERROR_INTERNAL;
    }

    rc = avbCall(handler, avbCommand::WRITE_OEM_UNLOCK_DEVICE_PERMISSION, &status, sizeof(uint8_t),
                 NULL, 0);

    /* Close Tipc channel */
    tipc_close(handler);

    if (rc != avbError::AVB_ERROR_NONE) {
        LOG(ERROR) << "failed to read device unlock status from trusty:" << rc;
        return avbError::AVB_ERROR_INTERNAL;
    } else
        return avbError::AVB_ERROR_NONE;
}
