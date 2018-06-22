/*
 * Copyright (C) 2017 The Android Open Source Project
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

#ifndef NXP_CONFIG_XTOR_PICO_H_
#define NXP_CONFIG_XTOR_PICO_H_

#include "audio_hardware.h"

#define PICO_SAI_CARD_NAME "xtor-audio-pico"

/* ALSA cards for IMX, these must be defined according different board / kernel
 * config*/
static struct audio_card xtor_pico_card = {
    .name = "xtor-audio-pico",
    .driver_name = "xtor-audio",
    .bus_name = "I2S3",
    .supported_out_devices = AUDIO_DEVICE_OUT_SPEAKER,
    .supported_in_devices = AUDIO_DEVICE_IN_BUILTIN_MIC,
    .defaults = NULL,
    .bt_output = NULL,
    .speaker_output = NULL,
    .hs_output = NULL,
    .earpiece_output = NULL,
    .vx_hs_mic_input = NULL,
    .mm_main_mic_input = NULL,
    .vx_main_mic_input = NULL,
    .mm_hs_mic_input = NULL,
    .vx_bt_mic_input = NULL,
    .mm_bt_mic_input = NULL,
    .card = 0,
    .out_rate = 0,
    .out_channels = 0,
    .out_format = 0,
    .in_rate = 0,
    .in_channels = 0,
    .in_format = 0,
};

#endif /* NXP_CONFIG_XTOR_PICO_H_ */
