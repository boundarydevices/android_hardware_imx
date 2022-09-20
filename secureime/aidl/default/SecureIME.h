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

#pragma once

#include <aidl/nxp/hardware/secureime/BnSecureIME.h>
#include <cutils/log.h>
#include <trusty/tipc.h>

namespace aidl {
namespace nxp {
namespace hardware {
namespace secureime {

#define SECUREIME_PORT_NAME "com.android.trusty.secureime"

enum secureime_command: uint32_t {
     SECURE_IME_REQ_SHIFT = 1,
     SECURE_IME_RESP_BIT = 1,

     SECURE_IME_CMD_INIT   = (0 << SECURE_IME_REQ_SHIFT),
     SECURE_IME_CMD_INPUT  = (1 << SECURE_IME_REQ_SHIFT),
     SECURE_IME_CMD_EXIT   = (2 << SECURE_IME_REQ_SHIFT),
};

struct secureime_req {
    uint32_t cmd;
    int buffer_size;
    int width;
    int height;
    int stride;
    int x;
    int y;
};

struct secureime_resp {
    uint32_t cmd;
    int key;
    int result;
};

class SecureIME : public BnSecureIME {
	::ndk::ScopedAStatus SecureIMEInit(const ::ndk::ScopedFileDescriptor& in_fd, int32_t in_buffer_size,
						int32_t in_stride, int32_t in_width, int32_t in_height, int32_t* _aidl_return) override;
	::ndk::ScopedAStatus SecureIMEHandleTouch(int32_t in_x, int32_t in_y, int32_t* _aidl_return) override;
	::ndk::ScopedAStatus SecureIMEExit(int32_t* _aidl_return) override;

private:
	int tipc_fd = -1;
	int connectIME();
	int tipc_read(struct secureime_resp *resp, int cmd);
	int secureIMEInit(int fd, int buffer_size, int stride, int width, int height);
	int secureIMEHandleTouch(int x, int y, int *key);
	int secureIMEExit();
	void closeIME();
};

}  // namespace secureime
}  // namespace hardware
}  // namespace nxp
}  // namespace aidl
