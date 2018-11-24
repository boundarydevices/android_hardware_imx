/*
 * Copyright (C) 2011 The Android Open Source Project
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
/* Copyright 2018 NXP */

#ifndef ANDROID_INCLUDE_IMX_CONFIG_AK5558_H
#define ANDROID_INCLUDE_IMX_CONFIG_AK5558_H

#include "audio_hardware.h"

#define MIXER_AK5558_INPUT1_SWITCH              "AK5558 Ch1 Enable"
#define MIXER_AK5558_INPUT2_SWITCH              "AK5558 Ch2 Enable"
#define MIXER_AK5558_INPUT3_SWITCH              "AK5558 Ch3 Enable"
#define MIXER_AK5558_INPUT4_SWITCH              "AK5558 Ch4 Enable"
#define MIXER_AK5558_INPUT5_SWITCH              "AK5558 Ch5 Enable"
#define MIXER_AK5558_INPUT6_SWITCH              "AK5558 Ch6 Enable"
#define MIXER_AK5558_INPUT7_SWITCH              "AK5558 Ch7 Enable"
#define MIXER_AK5558_INPUT8_SWITCH              "AK5558 Ch8 Enable"

static struct route_setting default_input_ak5558[] = {
    {
        .ctl_name = MIXER_AK5558_INPUT1_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_AK5558_INPUT2_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_AK5558_INPUT3_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_AK5558_INPUT4_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_AK5558_INPUT5_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_AK5558_INPUT6_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_AK5558_INPUT7_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_AK5558_INPUT8_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

/* ALSA cards for IMX, these must be defined according different board / kernel config*/
static struct audio_card  ak5558_card = {
    .name = "ak5558-audio",
    .driver_name = "ak5558-audio",
    .supported_out_devices  = 0,
    .supported_in_devices   = AUDIO_DEVICE_IN_BUILTIN_MIC,
    .defaults            = default_input_ak5558,
    .bt_output           = NULL,
    .speaker_output      = NULL,
    .hs_output           = NULL,
    .earpiece_output     = NULL,
    .vx_hs_mic_input     = NULL,
    .mm_main_mic_input   = NULL,
    .vx_main_mic_input   = NULL,
    .mm_hs_mic_input     = NULL,
    .vx_bt_mic_input     = NULL,
    .mm_bt_mic_input     = NULL,
    .card                = 0,
    .out_rate            = 0,
    .out_channels        = 0,
    .out_format          = 0,
    .in_rate             = 0,
    .in_channels         = 0,
    .in_format           = 0,
};

#endif  /* ANDROID_INCLUDE_IMX_CONFIG_AK5558_H */
