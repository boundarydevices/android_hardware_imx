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

#pragma once

#include <stdint.h>
#include <trusty/tipc.h>
#define HWSECURE_CLIENT_PORT "com.android.trusty.hwsecure.client"

enum hwsecure_client_cmd : uint32_t {
    HWSECURE_CLIENT_REQ_SHIFT = 1,
    HWSECURE_CLIENT_RESP_BIT = 1,

    ENABLE_G2D_SECURE_MODE = (1 << HWSECURE_CLIENT_REQ_SHIFT),
    DISABLE_G2D_SECURE_MODE = (2 << HWSECURE_CLIENT_REQ_SHIFT),
    GET_G2D_SECURE_MODE = (3 << HWSECURE_CLIENT_REQ_SHIFT),

    SECURE_IME_ENABLE_SECURE_POLICY = (4 << HWSECURE_CLIENT_REQ_SHIFT),
    SECURE_IME_DISABLE_SECURE_POLICY = (5 << HWSECURE_CLIENT_REQ_SHIFT),
    SECURE_IME_GET_SECURE_MODE = (6 << HWSECURE_CLIENT_REQ_SHIFT),
};

struct hwsecure_client_req {
    uint32_t cmd;
};

struct hwsecure_client_resp {
    uint32_t cmd;
    uint32_t result;
    union secure_mode {
        uint32_t g2d_secure_mode;
        uint32_t secureime_secure_mode;
    } mode;
};
