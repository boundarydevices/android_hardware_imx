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
/* Copyright (C) 2015 Freescale Semiconductor, Inc. */

#ifndef ANDROID_INCLUDE_IMX_CONFIG_WM8960_H
#define ANDROID_INCLUDE_IMX_CONFIG_WM8960_H

#include "audio_hardware.h"


#define MIXER_WM8960_SPEAKER_VOLUME		"Speaker Playback Volume"
#define MIXER_WM8960_HEADPHONE_VOLUME		"Headphone Playback Volume"
#define MIXER_WM8960_PLAYBACK_VOLUME		"Playback Volume"

#define MIXER_WM8960_LEFT_OUTPUT_SWITCH		"Left Output Mixer PCM Playback Switch"
#define MIXER_WM8960_RIGHT_OUTPUT_SWITCH	"Right Output Mixer PCM Playback Switch"

#define MIXER_WM8960_CAPTURE_SWITCH		"Capture Switch"
#define MIXER_WM8960_CAPTURE_VOLUME		"Capture Volume"

#define MIXER_WM8960_ALC_FUNCTION		"ALC Function"

#define MIXER_WM8960_LINPUT_SWITCH		"Left Input Mixer Boost Switch"

#define MIXER_WM8960_LINPUT1_SWITCH		"Left Boost Mixer LINPUT1 Switch"
#define MIXER_WM8960_LINPUT2_SWITCH		"Left Boost Mixer LINPUT2 Switch"
#define MIXER_WM8960_LINPUT3_SWITCH		"Left Boost Mixer LINPUT3 Switch"
#define MIXER_WM8960_LINPUT1_VOLUME		"Left Input Boost Mixer LINPUT1 Volume"
#define MIXER_WM8960_LINPUT2_VOLUME		"Left Input Boost Mixer LINPUT2 Volume"
#define MIXER_WM8960_LINPUT3_VOLUME		"Left Input Boost Mixer LINPUT3 Volume"
#define MIXER_WM8960_LEFT_OUTPUT_LINPUT3_SWITCH	"Left Output Mixer LINPUT3 Switch"
#define MIXER_WM8960_LEFT_OUTPUT_LINPUT3_VOLUME	"Left Output Mixer LINPUT3 Volume"

#define MIXER_WM8960_RINPUT_SWITCH		"Right Input Mixer Boost Switch"

#define MIXER_WM8960_RINPUT1_SWITCH		"Right Boost Mixer RINPUT1 Switch"
#define MIXER_WM8960_RINPUT2_SWITCH		"Right Boost Mixer RINPUT2 Switch"
#define MIXER_WM8960_RINPUT3_SWITCH		"Right Boost Mixer RINPUT3 Switch"
#define MIXER_WM8960_RINPUT1_VOLUME		"Right Input Boost Mixer RINPUT1 Volume"
#define MIXER_WM8960_RINPUT2_VOLUME		"Right Input Boost Mixer RINPUT2 Volume"
#define MIXER_WM8960_RINPUT3_VOLUME		"Right Input Boost Mixer RINPUT3 Volume"
#define MIXER_WM8960_RIGHT_OUTPUT_RINPUT3_SWITCH "Right Output Mixer RINPUT3 Switch"
#define MIXER_WM8960_RIGHT_OUTPUT_RINPUT3_VOLUME "Right Output Mixer RINPUT3 Volume"

#define MIXER_WM8960_ADC_PCM_CAPTURE_VOLUME         "ADC PCM Capture Volume"

/* These are values that never change */
static struct route_setting defaults_wm8960[] = {
    {
        .ctl_name = MIXER_WM8960_LEFT_OUTPUT_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8960_RIGHT_OUTPUT_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8960_PLAYBACK_VOLUME,
        .intval = 255,
    },
    {
        .ctl_name = MIXER_WM8960_SPEAKER_VOLUME,
        .intval = 127,
    },
    {
        .ctl_name = MIXER_WM8960_LINPUT1_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8960_LINPUT2_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8960_LINPUT3_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8960_LINPUT1_VOLUME,
        .intval = 3,
    },
    {
        .ctl_name = MIXER_WM8960_LINPUT2_VOLUME,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8960_LINPUT3_VOLUME,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8960_LEFT_OUTPUT_LINPUT3_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8960_LEFT_OUTPUT_LINPUT3_VOLUME,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8960_RINPUT1_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8960_RINPUT2_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8960_RINPUT3_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8960_RINPUT1_VOLUME,
        .intval = 3,
    },
    {
        .ctl_name = MIXER_WM8960_RINPUT2_VOLUME,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8960_RINPUT3_VOLUME,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8960_RIGHT_OUTPUT_RINPUT3_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8960_RIGHT_OUTPUT_RINPUT3_VOLUME,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8960_RINPUT_SWITCH,
        .intval = 1,
    },
#if 0
    {
        .ctl_name = MIXER_WM8960_LINPUT_SWITCH,
        .intval = 1,
    },
#endif
};

static struct route_setting speaker_output_wm8960[] = {
    {
        .ctl_name = NULL,
    },
};

static struct route_setting headphone_output_wm8960[] = {
    {
        .ctl_name = MIXER_WM8960_HEADPHONE_VOLUME,
        .intval = 127,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting mm_main_mic_input_wm8960[] = {
    {
        .ctl_name = MIXER_WM8960_ALC_FUNCTION,
        .intval = 3,
    },
    {
        .ctl_name = MIXER_WM8960_LINPUT_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8960_ADC_PCM_CAPTURE_VOLUME,
        .intval = 230,
    },
    {
        .ctl_name = MIXER_WM8960_CAPTURE_VOLUME,
        .intval = 60,
    },
    {
        .ctl_name = NULL,
    },
};

/* ALSA cards for IMX, these must be defined according different board / kernel config*/
static struct audio_card  wm8960_card = {
    .name = "wm8960-audio",
    .driver_name = "wm8960-audio",
    .supported_out_devices = (
            AUDIO_DEVICE_OUT_SPEAKER |
            AUDIO_DEVICE_OUT_WIRED_HEADSET |
            AUDIO_DEVICE_OUT_WIRED_HEADPHONE |
            AUDIO_DEVICE_OUT_DEFAULT ),
    .supported_in_devices = (
            AUDIO_DEVICE_IN_BUILTIN_MIC |
            AUDIO_DEVICE_IN_WIRED_HEADSET |
            AUDIO_DEVICE_IN_DEFAULT),
    .defaults            = defaults_wm8960,
    .bt_output           = NULL,
    .speaker_output      = speaker_output_wm8960,
    .hs_output		 = headphone_output_wm8960,
    .vx_hs_mic_input     = NULL,
    .mm_main_mic_input   = mm_main_mic_input_wm8960,
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

#endif  /* ANDROID_INCLUDE_IMX_CONFIG_WM8960_H */
