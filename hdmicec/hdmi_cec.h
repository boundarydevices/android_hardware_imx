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

#define CEC_MSG_USER_CONTROL_PRESSED    0x44
#define CEC_MSG_USER_CONTROL_RELEASED   0x45

#define CEC_KEYCODE_SELECT      0x00
#define CEC_KEYCODE_UP          0x01
#define CEC_KEYCODE_DOWN        0x02
#define CEC_KEYCODE_LEFT        0x03
#define CEC_KEYCODE_RIGHT       0x04
#define CEC_KEYCODE_CENTER      0x01
#define CEC_KEYCODE_EXIT        0x0d
#define CEC_KEYCODE_PLAY        0x44
#define CEC_KEYCODE_PAUSE       0x46
#define CEC_KEYCODE_ROOT_MENU       0x09
#define CEC_KEYCODE_CONTENTS_MENU   0x0b

extern int open_hdmi_cec(const char *id, struct hw_device_t **device);

#ifdef _cplusplus
}
#endif
#endif