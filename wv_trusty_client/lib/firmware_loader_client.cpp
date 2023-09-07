/*
 * Copyright 2023 NXP.
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

#define LOG_TAG "firmware_loader_client"
#include <BufferAllocator/BufferAllocator.h>
#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <errno.h>
#include <firmware_loader_client.h>
#include <log/log.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <trusty/tipc.h>
#include <unistd.h>

#include <algorithm>

#define TRUSTY_DEVICE_NAME "/dev/trusty-ipc-dev0"

using android::base::unique_fd;
using std::string;

static unique_fd read_file(const char* file_name, off64_t* out_file_size) {
    int rc;
    long page_size = sysconf(_SC_PAGESIZE);
    off64_t file_size, file_page_offset, file_page_size;
    struct stat64 st;

    unique_fd file_fd(TEMP_FAILURE_RETRY(open(file_name, O_RDONLY)));
    if (!file_fd.ok()) {
        ALOGE("Error opening file =%s", file_name);
        return {};
    }

    rc = fstat64(file_fd, &st);
    if (rc < 0) {
        ALOGE("Error calling stat on file %s", file_name);
        return {};
    }

    assert(st.st_size >= 0);
    file_size = st.st_size;

    /* The dmabuf size needs to be a multiple of the page size */
    file_page_offset = file_size & (page_size - 1);
    if (file_page_offset) {
        file_page_offset = page_size - file_page_offset;
    }
    if (__builtin_add_overflow(file_size, file_page_offset, &file_page_size)) {
        ALOGE("Failed to page-align file size");
        return {};
    }

    BufferAllocator alloc;
    unique_fd dmabuf_fd(alloc.Alloc("reserved", file_page_size));
    if (!dmabuf_fd.ok()) {
        ALOGE("Error creating dmabuf: %d", dmabuf_fd.get());
        return dmabuf_fd;
    }

    void* shm = mmap(0, file_page_size, PROT_READ | PROT_WRITE, MAP_SHARED, dmabuf_fd, 0);
    if (shm == MAP_FAILED) {
        return {};
    }

    off64_t file_offset = 0;
    while (file_offset < file_size) {
        ssize_t num_read = TEMP_FAILURE_RETRY(
                pread(file_fd, (char*)shm + file_offset, file_size - file_offset, file_offset));

        if (num_read < 0) {
            ALOGE("Error reading firmware file %s", file_name);
            break;
        }

        if (num_read == 0) {
            ALOGE("Unexpected end of file %s", file_name);
            break;
        }

        file_offset += (off64_t)num_read;
    }

    munmap(shm, file_page_size);

    if (file_offset < file_size) {
        return {};
    }

    assert(file_offset == file_size);
    if (out_file_size) {
        *out_file_size = file_size;
    }

    return dmabuf_fd;
}

static ssize_t send_load_message(int tipc_fd, int package_fd, off64_t package_size) {
    struct firmware_loader_header hdr = {
            .cmd = FIRMWARE_LOADER_CMD_LOAD_FIRMWARE,
    };
    struct firmware_loader_load_firmware_req req = {
            .package_size = static_cast<uint64_t>(package_size),
    };
    struct iovec tx[2] = {{&hdr, sizeof(hdr)}, {&req, sizeof(req)}};
    struct trusty_shm shm = {
            .fd = package_fd,
            .transfer = TRUSTY_SHARE,
    };
    return tipc_send(tipc_fd, tx, 2, &shm, 1);
}

static ssize_t read_response(int tipc_fd) {
    struct firmware_loader_resp resp;
    ssize_t rc = read(tipc_fd, &resp, sizeof(resp));
    if (rc < 0) {
        ALOGE("Failed to read response");
        return rc;
    }

    if (rc < sizeof(resp)) {
        ALOGE("Not enough data in response: %zd", rc);
        return -EIO;
    }

    if (resp.hdr.cmd != (FIRMWARE_LOADER_CMD_LOAD_FIRMWARE | FIRMWARE_LOADER_RESP_BIT)) {
        ALOGE("Invalid command in response: %d", resp.hdr.cmd);
        return -EINVAL;
    }

    switch (resp.error) {
        case FIRMWARE_LOADER_NO_ERROR:
            break;
        case FIRMWARE_LOADER_ERR_UNKNOWN_CMD:
            ALOGE("Error: unknown command");
            break;
        default:
            ALOGE("Unrecognized error: %d", resp.error);
            break;
    }
    return static_cast<ssize_t>(resp.error);
}

ssize_t load_firmware_package(const char* firmware_file_name) {
    ssize_t rc = 0;
    int tipc_fd = -1;
    off64_t firmware_size;

    unique_fd firmware_fd = read_file(firmware_file_name, &firmware_size);
    if (!firmware_fd.ok()) {
        rc = -1;
        goto err_read_file;
    }

    tipc_fd = tipc_connect(TRUSTY_DEVICE_NAME, FIRMWARE_LOADER_PORT);
    if (tipc_fd < 0) {
        ALOGE("Failed to connect to firmware loader: %s", strerror(-tipc_fd));
        ALOGE("Failed to connect to firmware loader: %s", strerror(-tipc_fd));
        rc = tipc_fd;
        goto err_tipc_connect;
    }

    rc = send_load_message(tipc_fd, firmware_fd, firmware_size);
    if (rc < 0) {
        ALOGE("Failed to send firmware package: %zd", rc);
        goto err_send;
    }

    rc = read_response(tipc_fd);

err_send:
    tipc_close(tipc_fd);
err_tipc_connect:
err_read_file:
    return rc;
}
