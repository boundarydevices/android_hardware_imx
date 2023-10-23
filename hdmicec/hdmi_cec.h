/*
 *  Copyright 2023 NXP.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef HDMI_CEC_NXP_H
#define HDMI_CEC_NXP_H

#include <hardware/hdmi_cec.h>

#ifdef _cplusplus
extern "C"{
#endif

enum MaxLength : int32_t {
    MESSAGE_BODY = 15,
};

extern int open_hdmi_cec(const char *id, struct hw_device_t **device);

#ifdef _cplusplus
}
#endif
#endif