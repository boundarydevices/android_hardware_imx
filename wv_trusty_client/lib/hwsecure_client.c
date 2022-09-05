/*
 * Copyright 2022 NXP.
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

#define LOG_TAG "hwsecure_client"
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
#include <hwsecure_client_ipc.h>

#include <hwsecure_client.h>
#include <log/log.h>

#define TRUSTY_DEVICE_NAME "/dev/trusty-ipc-dev0"
#define UNUSED __attribute__((unused))
#ifdef SUPPORT_WIDEVINE_L1
static int handle_ = -1;
static int hwsecure_client_smc_call(uint32_t cmd, void *rsp, uint32_t rsp_len) {
    if (handle_ < 0) {
        ALOGE("TIPC not inited!");
        return -EINVAL;
    }
    ssize_t rc;

    struct hwsecure_client_req* req = (struct hwsecure_client_req*) malloc(sizeof(struct hwsecure_client_req));
    req->cmd = cmd;

    struct iovec in_iov[1] = {
        { .iov_base = req, .iov_len = sizeof(struct hwsecure_client_req) },
    };

    rc = writev(handle_, in_iov, 1);

    if (rc < 0) {
        ALOGE("failed to send cmd (%d) to %s: %s\n", cmd, HWSECURE_CLIENT_PORT, strerror(errno));
        rc = -(errno);
        goto out;
    }

    struct iovec out_iov[1] = {
        { .iov_base = rsp, .iov_len = rsp_len },
    };

    rc = readv(handle_, out_iov, 1);
    if (rc < 0) {
        ALOGE("failed to retrieve response for cmd (%d) to %s: %s\n", cmd, HWSECURE_CLIENT_PORT, strerror(errno));
        rc = -(errno);
        goto out;
    }

    if (((struct hwsecure_client_resp* )rsp)->cmd != (cmd | HWSECURE_CLIENT_RESP_BIT)) {
        ALOGE("unkonwn response command");
        rc = -(errno);
        goto out;
    }

    if (((struct hwsecure_client_resp* )rsp)->result < 0) {
        ALOGE("response result is error");
        rc = -(errno);
        goto out;
    }

out:
    free(req);
    return rc;
}

static int wv_tipc_connect() {
    int rc = tipc_connect(TRUSTY_DEVICE_NAME, HWSECURE_CLIENT_PORT);
    if (rc < 0) {
        ALOGE("TIPC Connect failed (%d)!", rc);
        return rc;
    }

    handle_ = rc;
    return 0;
}

static void wv_tipc_disconnect() {
    if (handle_ >= 0) {
        tipc_close(handle_);
    }
    handle_ = -1;
}
#endif


void set_g2d_secure_pipe(int enable UNUSED) {
#ifdef SUPPORT_WIDEVINE_L1
    ALOGD("will set g2d secure pipe mode: %d", enable);
    if (wv_tipc_connect()) {
        return;
    }
    struct hwsecure_client_resp resp;
    if (enable) {
        hwsecure_client_smc_call(ENABLE_G2D_SECURE_MODE, &resp, sizeof(struct hwsecure_client_resp));
    } else {
        hwsecure_client_smc_call(DISABLE_G2D_SECURE_MODE, &resp, sizeof(struct hwsecure_client_resp));
    }
    wv_tipc_disconnect();
#endif
}

enum g2d_secure_mode get_g2d_secure_pipe() {
#ifdef SUPPORT_WIDEVINE_L1
    if (wv_tipc_connect()) {
        return -1;
    }
    struct hwsecure_client_resp resp;
    resp.mode.g2d_secure_mode = 0;
    hwsecure_client_smc_call(GET_G2D_SECURE_MODE, &resp, sizeof(struct hwsecure_client_resp));
    ALOGD("will get g2d secure pipe mode: %d", resp.mode.g2d_secure_mode);
    wv_tipc_disconnect();
    return resp.mode.g2d_secure_mode;
#else
    return NON_SECURE;
#endif
}

