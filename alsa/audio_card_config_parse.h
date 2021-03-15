/*
 * Copyright (C) 2020 The Android Open Source Project
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
/* Copyright 2020 NXP */

#ifndef ANDROID_INCLUDE_IMX_AUDIO_CARD_CONFIG_H
#define ANDROID_INCLUDE_IMX_AUDIO_CARD_CONFIG_H

struct route_setting
{
    char *ctl_name;
    int intval;
    char *strval;
};

#define OUT_VOL_MIN_DFT               0
#define OUT_VOL_MAX_DFT               255

#define MAX_SUPPORT_CARD_NUM          16
#define MAX_SUPPORT_CARD_LIST_SIZE    (unsigned int)(MAX_SUPPORT_CARD_NUM + 1) // "NULL" point to end the array

struct audio_card {
    char * driver_name;
    char* bus_name; // Used in Android Automotive
    char* secondary_bus_name; // Used for some sound cards which support multiple pcm device
    unsigned int  supported_out_devices;
    unsigned int  supported_in_devices;
    struct route_setting *init_ctl;
    struct route_setting *speaker_ctl;
    struct route_setting *headphone_ctl;
    struct route_setting *builtin_mic_ctl;
    struct route_setting *headset_mic_ctl;
    struct route_setting *out_volume_ctl;
    int  card;
    int  out_format;
    int  in_format;
    unsigned int out_volume_min;
    unsigned int out_volume_max;
    bool support_hfp;
    bool support_dsd;
    bool is_hdmi_card;
    bool support_multi_chn;
    bool support_lpa;
    unsigned int out_period_size;
    unsigned int out_period_count;
    unsigned int in_period_size;
    unsigned int in_period_count;
};

bool parse_all_cards();
bool release_all_cards();
struct audio_card *audio_card_get_by_name(const char *name);

#endif
