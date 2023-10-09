/*
 * Copyright (C) 2023 The Android Open Source Project
 * Copyright 2023 NXP
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

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <getopt.h>
#include <trusty/tipc.h>
#include <sys/types.h>
#include <sys/types.h>
#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <string>
#include <android-base/file.h>

#include "dek_extractor.h"
#include <cutils/log.h>

namespace aidl {
namespace android {
namespace hardware {
namespace imx_dek_extractor {

using ::ndk::ScopedFileDescriptor;

constexpr const char kTrustyDefaultDeviceName[] = "/dev/trusty-ipc-dev0";
static const char* dev_name = kTrustyDefaultDeviceName;
#define HWCRYPTO_PORT "com.android.trusty.hwcrypto"

static ssize_t send_request_message(int tipc_fd, image_type_t part, request_type_t rt) {
	struct hwcrypto_msg hdr;

	if (rt == REQUEST_SIZE) {
		switch (part) {
			case SPL:
				hdr.cmd = HWCRYPTO_GET_SPL_DEK_BLOB_SIZE;
				break;
			case BOOTLOADER:
				hdr.cmd = HWCRYPTO_GET_BOOTLOADER_DEK_BLOB_SIZE;
				break;
			default:
				return PARAMETER_ERROR;
		}
	} else if (rt == REQUEST_DATA) {
		switch (part) {
			case SPL:
				hdr.cmd = HWCRYPTO_GET_SPL_DEK_BLOB;
				break;
			case BOOTLOADER:
				hdr.cmd = HWCRYPTO_GET_BOOTLOADER_DEK_BLOB;
				break;
			default:
				return PARAMETER_ERROR;
		}
	} else {
		return PARAMETER_ERROR;
	}
	struct iovec tx[2] = {
		{.iov_base = &hdr, .iov_len = sizeof(hdr)},
		{.iov_base = NULL, .iov_len = 0},
	};
	return tipc_send(tipc_fd, tx, 2, NULL, 0);
}

static ssize_t recv_request_message(int tipc_fd, void *resp, size_t resp_len) {
	ssize_t rc;
	struct hwcrypto_msg hdr;

	struct iovec rx[2] = {
		{.iov_base = &hdr, .iov_len = sizeof(hdr)},
		{.iov_base = resp, .iov_len = resp_len}
	};

	rc = readv(tipc_fd, rx, 2);

	if (rc < 0) {
		LOG(ERROR) << "failed to read request";
		return rc;
	}

	return rc - sizeof(hdr);
}

int Dek_Extractor::dek_extractor(std::vector<unsigned char>* dek_blob, image_type_t image_t) {
	int tipc_fd = -1;
	error_t ret = NO_ERROR;
	uint32_t dek_blob_size = 0;
	int rc;

	tipc_fd = tipc_connect(dev_name, HWCRYPTO_PORT);
	if (tipc_fd < 0) {
		LOG(ERROR) << "Failed to connect to HWCRYPTO" << tipc_fd;
		ret = CONNECT_ERROR;
		goto err_tipc_connect;
	}

	rc = send_request_message(tipc_fd, image_t, REQUEST_SIZE);
	if (rc < 0) {
		LOG(ERROR) << "Failed to send package: " << rc;
		ret = REQUSET_ERROR;
		goto err_send;
	}

	rc = recv_request_message(tipc_fd, (void *)&dek_blob_size, sizeof(uint32_t));
	if (rc < 0) {
		LOG(ERROR) << "Failed to receive package: " << rc;
		ret = REQUSET_ERROR;
		goto err_send;
	}

	rc = send_request_message(tipc_fd, image_t, REQUEST_DATA);
	if (rc < 0) {
		LOG(ERROR) << "Failed to send package: " << rc;
		ret = REQUSET_ERROR;
		goto err_send;
	}

	dek_blob->resize(dek_blob_size);
	rc = recv_request_message(tipc_fd, dek_blob->data(), dek_blob_size);
	if (rc < 0) {
		LOG(ERROR) << "Failed to receive package: " << rc;
		ret = REQUSET_ERROR;
		goto err_send;
	}

err_send:
	tipc_close(tipc_fd);
err_tipc_connect:
	return ret;
}

::ndk::ScopedAStatus Dek_Extractor::Dek_ExtractorInit(std::vector<unsigned char>* dek_blob,
		int dek_blob_type, int32_t* _aidl_return) {
	image_type_t image_t = IMAGE_NONE;
	image_t = (image_type_t)dek_blob_type;

	if (dek_extractor(dek_blob, image_t)) {
		*_aidl_return = -1;
		return ndk::ScopedAStatus(AStatus_fromExceptionCode(EX_UNSUPPORTED_OPERATION));
	}

	*_aidl_return = 0;
	return ndk::ScopedAStatus::ok();
}

}  // namespace imx_dek_extractor
}  // namespace hardware
}  // namespace android
}  // namespace aidl
