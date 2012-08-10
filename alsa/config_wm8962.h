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
/* Copyright (C) 2012 Freescale Semiconductor, Inc. */

#ifndef ANDROID_INCLUDE_IMX_CONFIG_WM8962_H
#define ANDROID_INCLUDE_IMX_CONFIG_WM8962_H

#include "audio_hardware.h"


#define MIXER_WM8962_SPEAKER_VOLUME                 "Speaker Volume"
#define MIXER_WM8962_SPEAKER_SWITCH                 "Speaker Switch"
#define MIXER_WM8962_HEADPHONE_VOLUME               "Headphone Volume"
#define MIXER_WM8962_HEADPHONE_SWITCH               "Headphone Switch"

#define MIXER_WM8962_CAPTURE_SWITCH                 "Capture Switch"
#define MIXER_WM8962_CAPTURE_VOLUME                 "Capture Volume"

#define MIXER_WM8962_INPGAR_IN3R_SWITCH             "INPGAR IN3R Switch"
#define MIXER_WM8962_MIXINR_IN3R_SWITCH             "MIXINR IN3R Switch"
#define MIXER_WM8962_MIXINR_IN3R_VOLUME             "MIXINR IN3R Volume"

#define MIXER_WM8962_MIXINR_PGA_SWITCH              "MIXINR PGA Switch"
#define MIXER_WM8962_MIXINR_PGA_VOLUME              "MIXINR PGA Volume"

#define MIXER_WM8962_DIGITAL_CAPTURE_VOLUME         "Digital Capture Volume"

#define MIXER_WM8962_DIGITAL_PLAYBACK_VOLUME        "Digital Playback Volume"


/* These are values that never change */
static struct route_setting defaults_wm8962[] = {
    /* general */
    {
        .ctl_name = MIXER_WM8962_DIGITAL_PLAYBACK_VOLUME,
        .intval = 96,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting bt_output_wm8962[] = {
    {
        .ctl_name = NULL,
    },
};

static struct route_setting speaker_output_wm8962[] = {
    {
        .ctl_name = MIXER_WM8962_SPEAKER_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8962_SPEAKER_VOLUME,
        .intval = 121,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting hs_output_wm8962[] = {
    {
        .ctl_name = MIXER_WM8962_HEADPHONE_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8962_HEADPHONE_VOLUME,
        .intval = 121,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting earpiece_output_wm8962[] = {
    {
        .ctl_name = NULL,
    },
};

static struct route_setting vx_hs_mic_input_wm8962[] = {
    {
        .ctl_name = NULL,
    },
};


static struct route_setting mm_main_mic_input_wm8962[] = {
    {
        .ctl_name = MIXER_WM8962_CAPTURE_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8962_CAPTURE_VOLUME,
        .intval = 63,
    },
    {
        .ctl_name = MIXER_WM8962_DIGITAL_CAPTURE_VOLUME,
        .intval = 127,
    },/*
    {
        .ctl_name = MIXER_WM8962_INPGAR_IN3R_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8962_MIXINR_PGA_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8962_MIXINR_PGA_VOLUME,
        .intval = 7,
    },*/
    {
        .ctl_name = MIXER_WM8962_MIXINR_IN3R_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8962_MIXINR_IN3R_VOLUME,
        .intval = 7,
    },
    {
        .ctl_name = NULL,
    },
};


static struct route_setting vx_main_mic_input_wm8962[] = {
    {
        .ctl_name = NULL,
    },
};

/*hs_mic exchanged with main mic for sabresd, because the the main is no implemented*/
static struct route_setting mm_hs_mic_input_wm8962[] = {
    {
        .ctl_name = MIXER_WM8962_CAPTURE_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8962_CAPTURE_VOLUME,
        .intval = 63,
    },
    {
        .ctl_name = MIXER_WM8962_DIGITAL_CAPTURE_VOLUME,
        .intval = 127,
    },/*
    {
        .ctl_name = MIXER_WM8962_INPGAR_IN3R_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8962_MIXINR_PGA_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8962_MIXINR_PGA_VOLUME,
        .intval = 7,
    },*/
    {
        .ctl_name = MIXER_WM8962_MIXINR_IN3R_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8962_MIXINR_IN3R_VOLUME,
        .intval = 7,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting vx_bt_mic_input_wm8962[] = {
    {
        .ctl_name = NULL,
    },
};


static struct route_setting mm_bt_mic_input_wm8962[] = {
    {
        .ctl_name = NULL,
    },
};

/* ALSA cards for IMX, these must be defined according different board / kernel config*/
static struct audio_card  wm8962_card = {
    .name = "wm8962-audio",
    .supported_devices = (AUDIO_DEVICE_OUT_EARPIECE |
            AUDIO_DEVICE_OUT_SPEAKER |
            AUDIO_DEVICE_OUT_WIRED_HEADSET |
            AUDIO_DEVICE_OUT_WIRED_HEADPHONE |
            AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET |
            AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET |
            AUDIO_DEVICE_OUT_ALL_SCO |
            AUDIO_DEVICE_OUT_DEFAULT |
            /* IN */
            AUDIO_DEVICE_IN_COMMUNICATION |
            AUDIO_DEVICE_IN_AMBIENT |
            AUDIO_DEVICE_IN_BUILTIN_MIC |
            AUDIO_DEVICE_IN_WIRED_HEADSET |
            AUDIO_DEVICE_IN_BACK_MIC |
            AUDIO_DEVICE_IN_ALL_SCO |
            AUDIO_DEVICE_IN_DEFAULT),
    .defaults            = defaults_wm8962,
    .bt_output           = bt_output_wm8962,
    .speaker_output      = speaker_output_wm8962,
    .hs_output           = hs_output_wm8962,
    .earpiece_output     = earpiece_output_wm8962,
    .vx_hs_mic_input     = vx_hs_mic_input_wm8962,
    .mm_main_mic_input   = mm_main_mic_input_wm8962,
    .vx_main_mic_input   = vx_main_mic_input_wm8962,
    .mm_hs_mic_input     = mm_hs_mic_input_wm8962,
    .vx_bt_mic_input     = vx_bt_mic_input_wm8962,
    .mm_bt_mic_input     = mm_bt_mic_input_wm8962,
    .card                = 0,
};

#endif  /* ANDROID_INCLUDE_IMX_CONFIG_WM8962_H */
