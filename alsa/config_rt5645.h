/*
 * Copyright (C) 2018 The Android Open Source Project
 * Copyright 2018 NXP
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

#ifndef NXP_CONFIG_RT5645_H_
#define NXP_CONFIG_RT5645_H_

#include "audio_hardware.h"

// default
#define MIXER_RT5645_DAC1_PLAYBACK_VOL "DAC1 Playback Volume"
#define MIXER_RT5645_DAC1_MIXL_DAC1_SWITCH "DAC1 MIXL DAC1 Switch"
#define MIXER_RT5645_DAC1_MIXR_DAC1_SWITCH "DAC1 MIXR DAC1 Switch"
#define MIXER_RT5645_STEREO_DAC_MIXL_DAC_L1 "Stereo DAC MIXL DAC L1 Switch"
#define MIXER_RT5645_STEREO_DAC_MIXR_DAC_R1 "Stereo DAC MIXR DAC R1 Switch"

// headphone
#define MIXER_RT5645_HP_CHN_SWITCH "Headphone Channel Switch"
#define MIXER_RT5645_HP_PLAYBACK_VOL "Headphone Playback Volume"
#define MIXER_RT5645_HPOVOL_MIXL_DAC1_SWITCH "HPOVOL MIXL DAC1 Switch"
#define MIXER_RT5645_HPOVOL_MIXL_INL_SWITCH "HPOVOL MIXL INL Switch"
#define MIXER_RT5645_HPOVOL_MIXL_BST1_SWITCH "HPOVOL MIXL BST1 Switch"
#define MIXER_RT5645_HPOVOL_MIXR_DAC1_SWITCH "HPOVOL MIXR DAC1 Switch"
#define MIXER_RT5645_HPOVOL_MIXR_INR_SWITCH "HPOVOL MIXR INR Switch"
#define MIXER_RT5645_HPOVOL_MIXR_BST2_SWITCH "HPOVOL MIXR BST2 Switch"
#define MIXER_RT5645_HPOVOL_L_SWITCH "HPOVOL L Switch"
#define MIXER_RT5645_HPOVOL_R_SWITCH "HPOVOL R Switch"
#define MIXER_RT5645_HPO_MIX_HPVOL_SWITCH "HPO MIX HPVOL Switch"
#define MIXER_RT5645_HPO_MIX_DAC1_SWITCH "HPO MIX DAC1 Switch"

// speaker
#define MIXER_RT5645_SPK_CHN_SWITCH "Speaker Channel Switch"
#define MIXER_RT5645_SPKVOL_L_SWITCH "SPKVOL L Switch"
#define MIXER_RT5645_SPKVOL_R_SWITCH "SPKVOL R Switch"
#define MIXER_RT5645_SPK_MIXL_DAC_L1_SWITCH "SPK MIXL DAC L1 Switch"
#define MIXER_RT5645_SPK_MIXR_DAC_R1_SWITCH "SPK MIXR DAC R1 Switch"
#define MIXER_RT5645_SPOL_MIX_SPKVOL_L_SWITCH "SPOL MIX SPKVOL L Switch"
#define MIXER_RT5645_SPOR_MIX_SPKVOL_R_SWITCH "SPOR MIX SPKVOL R Switch"
#define MIXER_RT5645_SPK_PLAYBACK_VOL "Speaker Playback Volume"

// headset mic
#define MIXER_RT5645_ADC_CAPTURE_SWITCH "ADC Capture Switch"
#define MIXER_RT5645_ADC_CAPTURE_VOL "ADC Capture Volume"
#define MIXER_RT5645_ADC_BOOST_CAPTURE_VOL "ADC Boost Capture Volume"
#define MIXER_RT5645_RECMIXL_BST1_SWITCH "RECMIXL BST1 Switch"
#define MIXER_RT5645_RECMIXR_BST1_SWITCH "RECMIXR BST1 Switch"
#define MIXER_RT5645_STO1_ADC_MIXL_ADC1_SWITCH "Sto1 ADC MIXL ADC1 Switch"
#define MIXER_RT5645_STO1_ADC_MIXR_ADC1_SWITCH "Sto1 ADC MIXR ADC1 Switch"

// build-in mic
#define MIXER_SLOT1_ADC_MIXR_ADC2_SWITCH "Sto1 ADC MIXR ADC2 Switch"
#define MIXER_SLOT1_ADC_MIXL_ADC2_SWITCH "Sto1 ADC MIXL ADC2 Switch"
#define MIXER_STEREO1_ADC2_MUX "Stereo1 ADC2 Mux"
#define MIXER_I2S2_FUNC_SWITCH "I2S2 Func Switch"

/* These are values that never change */
static struct route_setting defaults_rt5645[] = {
    /* general */
    {
        .ctl_name = MIXER_RT5645_DAC1_PLAYBACK_VOL, .intval = 80,
    },
    {
        .ctl_name = MIXER_RT5645_DAC1_MIXL_DAC1_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_DAC1_MIXR_DAC1_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_STEREO_DAC_MIXL_DAC_L1, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_STEREO_DAC_MIXR_DAC_R1, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_SPKVOL_L_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_SPKVOL_R_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_SPK_MIXL_DAC_L1_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_SPK_MIXR_DAC_R1_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_SPOL_MIX_SPKVOL_L_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_SPOR_MIX_SPKVOL_R_SWITCH, .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting speaker_output_rt5645[] = {
    {
        .ctl_name = MIXER_RT5645_SPK_CHN_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_SPK_PLAYBACK_VOL, .intval = 39,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting headset_output_rt5645[] = {
    {
        .ctl_name = MIXER_RT5645_HP_CHN_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_HP_PLAYBACK_VOL, .intval = 30,
    },
    {
        .ctl_name = MIXER_RT5645_HPOVOL_MIXL_DAC1_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_HPOVOL_MIXL_INL_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_HPOVOL_MIXL_BST1_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_HPOVOL_MIXR_DAC1_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_HPOVOL_MIXR_INR_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_HPOVOL_MIXR_BST2_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_HPOVOL_L_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_HPOVOL_R_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_HPO_MIX_HPVOL_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_HPO_MIX_DAC1_SWITCH, .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting mm_headset_input_rt5645[] = {
    {
        .ctl_name = MIXER_RT5645_ADC_CAPTURE_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_ADC_CAPTURE_VOL, .intval = 60,
    },
    {
        .ctl_name = MIXER_RT5645_ADC_BOOST_CAPTURE_VOL, .intval = 3,
    },
    {
        .ctl_name = MIXER_RT5645_RECMIXL_BST1_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_RECMIXR_BST1_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_STO1_ADC_MIXL_ADC1_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_RT5645_STO1_ADC_MIXR_ADC1_SWITCH, .intval = 1,
    },
    {
        .ctl_name = NULL,
    },
};

static struct route_setting mm_built_in_mic_input_rt5645[] = {
    {
        .ctl_name = MIXER_SLOT1_ADC_MIXR_ADC2_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_SLOT1_ADC_MIXL_ADC2_SWITCH, .intval = 1,
    },
    {
        .ctl_name = MIXER_STEREO1_ADC2_MUX, .strval = "DMIC",
    },
    {
        .ctl_name = MIXER_I2S2_FUNC_SWITCH, .intval = 0,
    },
    {
        .ctl_name = NULL,
    },
};

/* ALSA cards for IMX, these must be defined according different board / kernel
 * config*/
static struct audio_card rt5645_card = {
    .name = "rt5645-audio",
    .driver_name = "edgetpu-audio",
    .bus_name = "I2S2",
    .supported_out_devices =
        (AUDIO_DEVICE_OUT_EARPIECE | AUDIO_DEVICE_OUT_SPEAKER |
         AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE |
         AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET | AUDIO_DEVICE_OUT_ALL_SCO |
         AUDIO_DEVICE_OUT_DEFAULT),
    .supported_in_devices =
        (AUDIO_DEVICE_IN_COMMUNICATION | AUDIO_DEVICE_IN_AMBIENT |
         AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_WIRED_HEADSET |
         AUDIO_DEVICE_IN_BACK_MIC | AUDIO_DEVICE_IN_ALL_SCO |
         AUDIO_DEVICE_IN_DEFAULT),
    .defaults = defaults_rt5645,
    .bt_output = NULL,
    .speaker_output = speaker_output_rt5645,
    .hs_output = headset_output_rt5645,
    .earpiece_output = NULL,
    .vx_hs_mic_input = NULL,
    .mm_main_mic_input = mm_built_in_mic_input_rt5645,
    .vx_main_mic_input = NULL,
    .mm_hs_mic_input = mm_headset_input_rt5645,
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

#endif /* NXP_CONFIG_RT5645_H_ */
