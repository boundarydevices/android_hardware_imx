/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Copyright 2023 NXP
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gralloc_helpers.h"

#include <cutils/log.h>
#include <errno.h>
#include <sync/sync.h>
#include <unistd.h>

gralloc_handle_t gralloc_convert_handle(buffer_handle_t handle) {
    auto hnd = reinterpret_cast<const gralloc_handle_t>(handle);
    if (!hnd || hnd->magic != gralloc_handle::sMagic)
        return nullptr;

    return hnd;
}

int32_t gralloc_sync_wait(int32_t fence, bool close_fence) {
    if (fence < 0)
        return 0;

    /*
     * Wait initially for 1000 ms, and then wait indefinitely. The SYNC_IOC_WAIT
     * documentation states the caller waits indefinitely on the fence if timeout < 0.
     */
    int err = sync_wait(fence, 1000);
    if (err < 0) {
        ALOGE("%s Timed out on sync wait, err = %s", __func__, strerror(errno));
        err = sync_wait(fence, -1);
        if (err < 0) {
            ALOGE("%s sync wait error = %s", __func__, strerror(errno));
            return -errno;
        }
    }

    if (close_fence) {
        err = close(fence);
        if (err) {
            ALOGE("%s Unable to close fence fd, err = %s", __func__, strerror(errno));
            return -errno;
        }
    }

    return 0;
}
