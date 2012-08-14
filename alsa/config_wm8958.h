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

#ifndef ANDROID_INCLUDE_IMX_CONFIG_WM8958_H
#define ANDROID_INCLUDE_IMX_CONFIG_WM8958_H

#include "audio_hardware.h"

#define MIXER_WM8958_AIF1DAC_MUX                    "AIF1DAC Mux"
#define MIXER_WM8958_AIF2DAC_MUX                    "AIF2DAC Mux"
#define MIXER_WM8958_AIF2ADC_MUX                    "AIF2ADC Mux"
#define MIXER_WM8958_AIF3ADC_MUX                    "AIF3ADC Mux"

#define MIXER_WM8958_AIF1ADC1L_MIXER_AIF2_SWITCH           "AIF1ADC1L Mixer AIF2 Switch"
#define MIXER_WM8958_AIF1ADC1R_MIXER_AIF2_SWITCH           "AIF1ADC1R Mixer AIF2 Switch"

#define MIXER_WM8958_DAC1L_MIXER_AIF1_1_SWITCH      "DAC1L Mixer AIF1.1 Switch"
#define MIXER_WM8958_DAC1L_MIXER_AIF1_2_SWITCH      "DAC1L Mixer AIF1.2 Switch"
#define MIXER_WM8958_DAC1L_MIXER_AIF2_SWITCH        "DAC1L Mixer AIF2 Switch"
#define MIXER_WM8958_DAC1L_MIXER_LEFT_SIDETONE_SWITCH   "DAC1L Mixer Left Sidetone Switch"
#define MIXER_WM8958_DAC1L_MIXER_RIGHT_SIDETONE_SWITCH  "DAC1L Mixer Right Sidetone Switch"

#define MIXER_WM8958_DAC1R_MIXER_AIF1_1_SWITCH      "DAC1R Mixer AIF1.1 Switch"
#define MIXER_WM8958_DAC1R_MIXER_AIF1_2_SWITCH      "DAC1R Mixer AIF1.2 Switch"
#define MIXER_WM8958_DAC1R_MIXER_AIF2_SWITCH        "DAC1R Mixer AIF2 Switch"
#define MIXER_WM8958_DAC1R_MIXER_LEFT_SIDETONE_SWITCH   "DAC1R Mixer Left Sidetone Switch"
#define MIXER_WM8958_DAC1R_MIXER_RIGHT_SIDETONE_SWITCH  "DAC1R Mixer Right Sidetone Switch"

#define MIXER_WM8958_SPEAKER_SWITCH                 "Speaker Switch"
#define MIXER_WM8958_SPEAKER_VOLUME                 "Speaker Volume"
#define MIXER_WM8958_EARPIECE_SWITCH                "Earpiece Switch"
#define MIXER_WM8958_EARPIECE_VOLUME                "Earpiece Volume"
#define MIXER_WM8958_HEADPHONE_SWITCH               "Headphone Switch"
#define MIXER_WM8958_HEADPHONE_VOLUME               "Headphone Volume"

#define MIXER_WM8958_LEFT_HEADPHONE_MUX             "Left Headphone Mux"
#define MIXER_WM8958_RIGHT_HEADPHONE_MUX            "Right Headphone Mux"

#define MIXER_WM8958_DAC1_SWITCH                    "DAC1 Switch"
#define MIXER_WM8958_DAC2_SWITCH                    "DAC2 Switch"

#define MIXER_WM8958_SPKL_DAC1_SWITCH               "SPKL DAC1 Switch"
#define MIXER_WM8958_SPKL_DAC1_VOLUME               "SPKL DAC1 Volume"
#define MIXER_WM8958_SPKR_DAC1_SWITCH               "SPKR DAC1 Switch"
#define MIXER_WM8958_SPKR_DAC1_VOLUME               "SPKR DAC1 Volume"

#define MIXER_WM8958_SPKL_BOOST_SPKL_SWITCH         "SPKL Boost SPKL Switch"
#define MIXER_WM8958_SPKR_BOOST_SPKR_SWITCH         "SPKR Boost SPKR Switch"
#define MIXER_WM8958_SPEAKER_MIXER_VOLUME           "Speaker Mixer Volume"

#define MIXER_WM8958_IN1R_SWITCH                    "IN1R Switch"
#define MIXER_WM8958_IN1R_VOLUME                    "IN1R Volume"
#define MIXER_WM8958_IN1L_SWITCH                    "IN1L Switch"
#define MIXER_WM8958_IN1L_VOLUME                    "IN1L Volume"

#define MIXER_WM8958_MIXINL_IN1L_VOLUME             "MIXINL IN1L Volume"
#define MIXER_WM8958_MIXINL_IN1L_SWITCH             "MIXINL IN1L Switch"
#define MIXER_WM8958_MIXINR_IN1R_VOLUME             "MIXINR IN1R Volume"
#define MIXER_WM8958_MIXINR_IN1R_SWITCH             "MIXINR IN1R Switch"

#define MIXER_WM8958_IN1L_PGA_IN1LN_SWITCH          "IN1L PGA IN1LN Switch"
#define MIXER_WM8958_IN1L_PGA_IN1LP_SWITCH          "IN1L PGA IN1LP Switch"
#define MIXER_WM8958_IN1R_PGA_IN1RN_SWITCH          "IN1R PGA IN1RN Switch"
#define MIXER_WM8958_IN1R_PGA_IN1RP_SWITCH          "IN1R PGA IN1RP Switch"

#define MIXER_WM8958_ADCL_MUX                       "ADCL Mux"
#define MIXER_WM8958_ADCR_MUX                       "ADCR Mux"

#define MIXER_WM8958_ADC_OSR                        "ADC OSR"

#define MIXER_WM8958_DAC2_LEFT_SIDETONE_VOLUME      "DAC2 Left Sidetone Volume"
#define MIXER_WM8958_DAC2_RIGHT_SIDETONE_VOLUME     "DAC2 Right Sidetone Volume"

#define MIXER_WM8958_AIF2DAC2L_MIXER_LEFT_SIDETONE_SWITCH   "AIF2DAC2L Mixer Left Sidetone Switch"
#define MIXER_WM8958_AIF2DAC2R_MIXER_RIGHT_SIDETONE_SWITCH  "AIF2DAC2R Mixer Right Sidetone Switch"

#define MIXER_WM8958_AIF1ADC1L_MIXER_ADCDMIC_SWITCH     "AIF1ADC1L Mixer ADC/DMIC Switch"
#define MIXER_WM8958_AIF1ADC1R_MIXER_ADCDMIC_SWITCH     "AIF1ADC1R Mixer ADC/DMIC Switch"

#define MIXER_WM8958_AIF1ADC1L_MIXER_AIF2_SWITCH        "AIF1ADC1L Mixer AIF2 Switch"
#define MIXER_WM8958_AIF1ADC1R_MIXER_AIF2_SWITCH        "AIF1ADC1R Mixer AIF2 Switch"

#define MIXER_WM8958_LEFT_SIDETONE                      "Left Sidetone"
#define MIXER_WM8958_RIGHT_SIDETONE                     "Right Sidetone"



/* These are values that never change */
static struct route_setting defaults_wm8958[] = {
    /* general */
    {
        .ctl_name = MIXER_WM8958_AIF1DAC_MUX,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_AIF2DAC_MUX,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_DAC1L_MIXER_AIF1_1_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_DAC1L_MIXER_AIF1_2_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_DAC1L_MIXER_AIF2_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_DAC1L_MIXER_LEFT_SIDETONE_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_DAC1L_MIXER_RIGHT_SIDETONE_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_DAC1R_MIXER_AIF1_1_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_DAC1R_MIXER_AIF1_2_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_DAC1R_MIXER_AIF2_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_DAC1R_MIXER_LEFT_SIDETONE_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_DAC1R_MIXER_RIGHT_SIDETONE_SWITCH,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_DAC1_SWITCH,
        .intval = 1,
    },
    /* headset */

    /* bt */
    {
        .ctl_name = NULL,
    },
};

static struct route_setting bt_output_wm8958[] = {
    {
        .ctl_name = MIXER_WM8958_AIF2ADC_MUX,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_AIF3ADC_MUX,
        .intval = 2,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting speaker_output_wm8958[] = {
    {
        .ctl_name = MIXER_WM8958_SPKL_DAC1_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_SPKL_DAC1_VOLUME,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_SPKR_DAC1_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_SPKR_DAC1_VOLUME,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_SPKL_BOOST_SPKL_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_SPKR_BOOST_SPKR_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_SPEAKER_MIXER_VOLUME,
        .intval = 3,
    },
    {
        .ctl_name = MIXER_WM8958_SPEAKER_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_SPEAKER_VOLUME,
        .intval = 60,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting hs_output_wm8958[] = {
    {
        .ctl_name = MIXER_WM8958_LEFT_HEADPHONE_MUX,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_RIGHT_HEADPHONE_MUX,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_HEADPHONE_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_HEADPHONE_VOLUME,
        .intval = 57,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting earpiece_output_wm8958[] = {
    {
        .ctl_name = MIXER_WM8958_EARPIECE_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_EARPIECE_VOLUME,
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting vx_hs_mic_input_wm8958[] = {
    {
        .ctl_name = MIXER_WM8958_IN1L_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_IN1L_VOLUME,
        .intval = 27,
    },
    {
        .ctl_name = MIXER_WM8958_MIXINL_IN1L_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_MIXINL_IN1L_VOLUME,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_IN1L_PGA_IN1LN_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_IN1L_PGA_IN1LP_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_ADCL_MUX,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_ADC_OSR,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_DAC2_LEFT_SIDETONE_VOLUME,
        .intval = 12,
    },
    {
        .ctl_name = MIXER_WM8958_DAC2_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_AIF2ADC_MUX,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_AIF2DAC2L_MIXER_LEFT_SIDETONE_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};


static struct route_setting mm_main_mic_input_wm8958[] = {
    {
        .ctl_name = MIXER_WM8958_IN1L_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_IN1L_VOLUME,
        .intval = 27,
    },
    {
        .ctl_name = MIXER_WM8958_MIXINL_IN1L_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_MIXINL_IN1L_VOLUME,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_IN1L_PGA_IN1LN_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_IN1L_PGA_IN1LP_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_ADCL_MUX,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_AIF1ADC1L_MIXER_ADCDMIC_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};


static struct route_setting vx_main_mic_input_wm8958[] = {
    {
        .ctl_name = MIXER_WM8958_IN1R_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_IN1R_VOLUME,
        .intval = 27,
    },
    {
        .ctl_name = MIXER_WM8958_MIXINR_IN1R_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_MIXINR_IN1R_VOLUME,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_IN1R_PGA_IN1RN_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_IN1R_PGA_IN1RP_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_ADCR_MUX,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_ADC_OSR,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_DAC2_RIGHT_SIDETONE_VOLUME,
        .intval = 12,
    },
    {
        .ctl_name = MIXER_WM8958_DAC2_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_AIF2ADC_MUX,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_AIF2DAC2R_MIXER_RIGHT_SIDETONE_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

/*hs_mic exchanged with main mic for sabresd, because the the main is no implemented*/
static struct route_setting mm_hs_mic_input_wm8958[] = {
    {
        .ctl_name = MIXER_WM8958_IN1R_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_IN1R_VOLUME,
        .intval = 27,
    },
    {
        .ctl_name = MIXER_WM8958_MIXINR_IN1R_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_MIXINR_IN1R_VOLUME,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_IN1R_PGA_IN1RN_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_IN1R_PGA_IN1RP_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_ADCR_MUX,
        .intval = 0,
    },
    {
        .ctl_name = MIXER_WM8958_AIF1ADC1L_MIXER_ADCDMIC_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting vx_bt_mic_input_wm8958[] = {
    {
        .ctl_name = MIXER_WM8958_AIF1ADC1L_MIXER_AIF2_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_AIF1ADC1R_MIXER_AIF2_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};


static struct route_setting mm_bt_mic_input_wm8958[] = {
    {
        .ctl_name = MIXER_WM8958_AIF1ADC1L_MIXER_AIF2_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = MIXER_WM8958_AIF1ADC1R_MIXER_AIF2_SWITCH,
        .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

/* ALSA cards for IMX, these must be defined according different board / kernel config*/
static struct audio_card  wm8958_card = {
    .name = "wm8958-audio",
    .driver_name = "wm8958-audio",
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
    .defaults            = defaults_wm8958,
    .bt_output           = bt_output_wm8958,
    .speaker_output      = speaker_output_wm8958,
    .hs_output           = hs_output_wm8958,
    .earpiece_output     = earpiece_output_wm8958,
    .vx_hs_mic_input     = vx_hs_mic_input_wm8958,
    .mm_main_mic_input   = mm_main_mic_input_wm8958,
    .vx_main_mic_input   = vx_main_mic_input_wm8958,
    .mm_hs_mic_input     = mm_hs_mic_input_wm8958,
    .vx_bt_mic_input     = vx_bt_mic_input_wm8958,
    .mm_bt_mic_input     = mm_bt_mic_input_wm8958,
    .card                = 0,
    .out_rate            = 0,
    .in_rate             = 0,
};

#endif  /* ANDROID_INCLUDE_IMX_CONFIG_WM8958_H */
