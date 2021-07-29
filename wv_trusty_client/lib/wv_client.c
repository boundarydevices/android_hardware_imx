#define LOG_TAG "wv_client"
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/uio.h>
#include <unistd.h>
#include <hwoemcrypto.h>

#include <wv_client.h>
#include <log/log.h>

#define TRUSTY_DEVICE_NAME "/dev/trusty-ipc-dev0"
#define UNUSED __attribute__((unused))
#ifdef SUPPORT_WIDEVINE_L1
static int handle_ = -1;
static int wv_smc_call(uint32_t cmd, void *req, uint32_t req_len, void *rsp, uint32_t rsp_len) {
    if (handle_ < 0) {
        ALOGE("TIPC not inited!");
        return -EINVAL;
    }
    ssize_t rc;

    struct oemcrypto_message *msg = (struct oemcrypto_message*) malloc(sizeof(struct oemcrypto_message));
    msg->cmd = cmd;

    struct iovec in_iov[2] = {
        { .iov_base = msg, .iov_len = sizeof(struct oemcrypto_message) },
        { .iov_base = req, .iov_len = req_len },
    };

    rc = writev(handle_, in_iov, req ? 2:1);

    if (rc < 0) {
        ALOGE("failed to send cmd (%d) to %s: %s\n", cmd, OEMCRYPTO_PORT, strerror(errno));
        rc = -(errno);
        goto out;
    }

    struct iovec out_iov[2] = {
        { .iov_base = msg, .iov_len = sizeof(struct oemcrypto_message) },
        { .iov_base = rsp, .iov_len = rsp_len },
    };

    rc = readv(handle_, out_iov, rsp ? 2:1);
    if (rc < 0) {
        ALOGE("failed to retrieve response for cmd (%d) to %s: %s\n", cmd, OEMCRYPTO_PORT, strerror(errno));
        rc = -(errno);
        goto out;
    }

out:
    free(msg);
    return rc;
}

static int wv_tipc_connect() {
    int rc = tipc_connect(TRUSTY_DEVICE_NAME, OEMCRYPTO_PORT);
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

void set_secure_pipe(int enable UNUSED) {
#ifdef SUPPORT_WIDEVINE_L1
    ALOGE("will set secure pipe mode: %d", enable);
    if (wv_tipc_connect()) {
        return;
    }
    if (enable) {
        wv_smc_call(OEMCRYPTO_ENABLE_SECURE_MODE, NULL, sizeof(struct oemcrypto_message), NULL, 0);
    } else {
        wv_smc_call(OEMCRYPTO_DISABLE_SECURE_MODE, NULL, sizeof(struct oemcrypto_message), NULL, 0);
    }
    wv_tipc_disconnect();
#endif
}

void set_g2d_secure_pipe(int enable UNUSED) {
#ifdef SUPPORT_WIDEVINE_L1
    ALOGE("will set g2d secure pipe mode: %d", enable);
    if (wv_tipc_connect()) {
        return;
    }
    if (enable) {
        wv_smc_call(OEMCRYPTO_ENABLE_G2D_SECURE_MODE, NULL, sizeof(struct oemcrypto_message), NULL, 0);
    } else {
        wv_smc_call(OEMCRYPTO_DISABLE_G2D_SECURE_MODE, NULL, sizeof(struct oemcrypto_message), NULL, 0);
    }
    wv_tipc_disconnect();
#endif

}

enum g2d_secure_mode get_g2d_secure_pipe() {
#ifdef SUPPORT_WIDEVINE_L1
    if (wv_tipc_connect()) {
        return -1;
    }
    int secure_mode = 0;
    wv_smc_call(OEMCRYPTO_G2D_SECURE_MODE, NULL, sizeof(struct oemcrypto_message), &secure_mode, sizeof(secure_mode));
    ALOGE("will get g2d secure pipe mode: %d", secure_mode);
    wv_tipc_disconnect();
    return secure_mode;
#else
    return NON_SECURE;
#endif
}

