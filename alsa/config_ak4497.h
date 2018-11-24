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

#ifndef ANDROID_INCLUDE_IMX_CONFIG_AK4497_H
#define ANDROID_INCLUDE_IMX_CONFIG_AK4497_H

#include "audio_hardware.h"

#define AK4497_CARD_NAME                        "ak4497-audio"

#define MIXER_AK4497_DAC_ENABLE                 "AK4497 DAC Enable"
#define MIXER_AK4497_AUTO_DETECT_MODE           "AK4497 Read FS Auto Detect Mode"
#define MIXER_AK4497_DSD_DATA_INPUT_PIN         "AK4497 DSD Data Input Pin"
#define MIXER_AK4497_SOFT_MUTE_CONTROL          "AK4497 Soft Mute Control"

static struct route_setting default_output_ak4497[] = {
    {
        .ctl_name = MIXER_AK4497_DAC_ENABLE,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_AK4497_AUTO_DETECT_MODE,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_AK4497_DSD_DATA_INPUT_PIN,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_AK4497_SOFT_MUTE_CONTROL,
        .intval = 0,
    },
    {
        .ctl_name = NULL,
    },
};

/* ALSA cards for IMX, these must be defined according different board / kernel config*/
static struct audio_card  ak4497_card = {
    .name = "ak4497-audio",
    .driver_name = "ak4497-audio",
    .supported_out_devices  = AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_LINE,
    .supported_in_devices   = 0,
    .defaults            = default_output_ak4497,
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

#endif  /* ANDROID_INCLUDE_IMX_CONFIG_AK4497_H */
