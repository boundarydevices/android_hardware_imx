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

#include <android-base/file.h>
#include <android-base/strings.h>
#include <android-base/properties.h>
#include <system/audio-base.h>
#include <log/log.h>
#include <json/json.h>
#include <json/reader.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

#include "audio_card_config_parse.h"

#define LOG_TAG "audio_hw_primary"

static const char* const g_kAudioConfigPath = "/vendor/etc/configs/audio";
static const char* const g_key_driver_name = "driver_name";
static const char* const g_key_bus_name = "bus_name";
static const char* const g_key_supported_out_devices = "supported_out_devices";
static const char* const g_key_supported_in_devices = "supported_in_devices";
static const char* const g_key_init_ctl = "init_ctl";
static const char* const g_key_speaker_ctl = "speaker_ctl";
static const char* const g_key_headphone_ctl = "headphone_ctl";
static const char* const g_key_builtin_mic_ctl = "builtin_mic_ctl";
static const char* const g_key_headset_mic_ctl = "headset_mic_ctl";
static const char* const g_key_out_volume_ctl = "out_volume_ctl";
static const char* const g_key_out_volume_min = "out_volume_min";
static const char* const g_key_out_volume_max = "out_volume_max";
static const char* const g_key_support_dsd = "support_dsd";
static const char* const g_key_support_hfp = "support_hfp";
static const char* const g_key_is_hdmi_card = "is_hdmi_card";
static const char* const g_key_support_multi_chn = "support_multi_chn";
static const char* const g_key_out_period_size = "out_period_size";
static const char* const g_key_out_period_count = "out_period_count";

struct audio_devcie_map {
    char *name;
    unsigned int device;
};

static const struct audio_devcie_map g_out_device_map[] = {
    {"speaker", AUDIO_DEVICE_OUT_SPEAKER},
    {"wired_headset", AUDIO_DEVICE_OUT_WIRED_HEADSET},
    {"wired_headphone", AUDIO_DEVICE_OUT_WIRED_HEADPHONE},
    {"aux_digital", AUDIO_DEVICE_OUT_AUX_DIGITAL},
    {"hdmi", AUDIO_DEVICE_OUT_HDMI},
    {"line", AUDIO_DEVICE_OUT_LINE},
    {"bus", AUDIO_DEVICE_OUT_BUS},
    {"bluetooth_sco_headset", AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET},
    {"bluetooth_sco_carkit", AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT},
    {"bluetooth_sco", AUDIO_DEVICE_OUT_BLUETOOTH_SCO},
};

static const struct audio_devcie_map g_in_device_map[] = {
    {"builtin_mic", AUDIO_DEVICE_IN_BUILTIN_MIC},
    {"wired_headset", AUDIO_DEVICE_IN_WIRED_HEADSET},
    {"aux_digital", AUDIO_DEVICE_IN_AUX_DIGITAL},
    {"bluetooth_sco_headset", AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET},
};

#define ARRAY_SIZE(a) (unsigned int)(sizeof(a)/sizeof(a[0]))

unsigned int map_audio_name_to_device(const struct audio_devcie_map *map, unsigned int size, const char *name) {
    for(int i = 0; i < size; i++) {
        if(!strcmp(name, map[i].name))
            return map[i].device;
    }

    return 0;
}

static unsigned int parse_devices(const struct audio_devcie_map *map, unsigned int size, Json::Value device_array) {
    unsigned int devices = 0;
    unsigned int device_num = device_array.size();

    for(int i = 0; i < device_num; i++) {
        unsigned int device = 0;
        const char *name = device_array[i].asCString();
        device = map_audio_name_to_device(map, size, name);
        devices |= device;
    }

    return devices;
}

static bool parse_control(struct route_setting **pp_route, Json::Value ctrl_array) {
    struct route_setting *p_route = NULL;
    unsigned int ctrl_num = ctrl_array.size();

    // Always "NULL" control at last, so "ctrl_num + 1".
    p_route = (struct route_setting *)calloc(ctrl_num + 1, sizeof(struct route_setting));
    if(p_route == NULL) {
        ALOGE("%s: calloc struct route_setting, %d bytes failed", __func__, sizeof(struct route_setting));
        return false;
    }

    for(int i = 0; i < ctrl_num; i++) {
        p_route[i].ctl_name = strdup(ctrl_array[i]["name"].asCString());

        const char *type = ctrl_array[i]["type"].asCString();
        if(!strcmp(type, "int")) {
            p_route[i].intval = ctrl_array[i]["val"].asInt();
            ALOGI("%s: ctl idx %d, [%s %d]", __func__, i, p_route[i].ctl_name, p_route[i].intval);
        }
        else if(!strcmp(type, "str")) {
            p_route[i].strval = strdup(ctrl_array[i]["val"].asCString());
            ALOGI("%s: ctl idx %d, [%s %s]", __func__, i, p_route[i].ctl_name, p_route[i].strval);
        }
        else {
            ALOGE("%s: control type %s is neither int nor str", __func__, type);
            free(p_route);
            return false;
        }
    }

    *pp_route = p_route;

    return true;
}

static bool parse_volume_control(struct route_setting **pp_route, Json::Value ctrl_array) {
    struct route_setting *p_route = NULL;
    unsigned int ctrl_num = ctrl_array.size();

    // Always "NULL" control at last, so "ctrl_num + 1".
    p_route = (struct route_setting *)calloc(ctrl_num + 1, sizeof(struct route_setting));
    if(p_route == NULL) {
        ALOGE("%s: calloc struct route_setting, %d bytes failed", __func__, sizeof(struct route_setting));
        return false;
    }

    for(int i = 0; i < ctrl_num; i++) {
        p_route[i].ctl_name = strdup(ctrl_array[i].asCString());
        ALOGI("%s: ctl idx %d, name %s", __func__, i, p_route[i].ctl_name);
    }

    *pp_route = p_route;

    return true;
}


static bool parse_one_card(char *config_file, struct audio_card **pp_audio_card) {
    std::string config;
    struct audio_card *p_audio_card = NULL;

    if((config_file == NULL) || (pp_audio_card == NULL))
        return false;

    ALOGI("%s: parse %s\n", __func__, config_file);

    if (!android::base::ReadFileToString(config_file, &config)) {
        ALOGE("%s: Could not open configuration file: %s", __func__, config_file);
        return false;
    }

    Json::Reader config_reader;
    Json::Value root;
    if (!config_reader.parse(config, root)) {
        ALOGE("Could not parse configuration file: %s, %s",
            config_reader.getFormattedErrorMessages().c_str(), config_file);
        return false;
    }

    if (!root.isObject()) {
        ALOGE("%s: Configuration root is not an object", __func__);
        return false;
    }

    p_audio_card = (struct audio_card *)calloc(1, sizeof(struct audio_card));
    if(p_audio_card == NULL) {
        ALOGE("%s: calloc struct audio_card, %d bytes failed", __func__, sizeof(struct audio_card));
        return false;
    }

    if (!root.isMember(g_key_driver_name)) goto parse_error;
        p_audio_card->driver_name = strdup(root[g_key_driver_name].asCString());

    if (root.isMember(g_key_bus_name))
        p_audio_card->bus_name = strdup(root[g_key_bus_name].asCString());

    if(root.isMember(g_key_supported_out_devices))
        p_audio_card->supported_out_devices = \
            parse_devices(g_out_device_map, ARRAY_SIZE(g_out_device_map), root[g_key_supported_out_devices]);

    if(root.isMember(g_key_supported_in_devices))
        p_audio_card->supported_in_devices = \
            parse_devices(g_in_device_map, ARRAY_SIZE(g_in_device_map), root[g_key_supported_in_devices]);

    if( (p_audio_card->supported_out_devices == 0) &&
        (p_audio_card->supported_in_devices == 0) ) {
        ALOGE("%s: card %s is neighter output nor input", __func__, p_audio_card->driver_name);
        goto parse_error;
    }

    if(root.isMember(g_key_init_ctl))
        parse_control(&p_audio_card->init_ctl, root[g_key_init_ctl]);

    if(root.isMember(g_key_speaker_ctl))
        parse_control(&p_audio_card->speaker_ctl, root[g_key_speaker_ctl]);

    if(root.isMember(g_key_headphone_ctl))
        parse_control(&p_audio_card->headphone_ctl, root[g_key_headphone_ctl]);

    if(root.isMember(g_key_builtin_mic_ctl))
        parse_control(&p_audio_card->builtin_mic_ctl, root[g_key_builtin_mic_ctl]);

    if(root.isMember(g_key_headset_mic_ctl))
        parse_control(&p_audio_card->headset_mic_ctl, root[g_key_headset_mic_ctl]);

    if(root.isMember(g_key_out_volume_ctl))
        parse_volume_control(&p_audio_card->out_volume_ctl, root[g_key_out_volume_ctl]);

    p_audio_card->out_volume_min = OUT_VOL_MIN_DFT;
    if(root.isMember(g_key_out_volume_min))
        p_audio_card->out_volume_min = root[g_key_out_volume_min].asUInt();

    p_audio_card->out_volume_max = OUT_VOL_MAX_DFT;
    if(root.isMember(g_key_out_volume_max))
        p_audio_card->out_volume_max = root[g_key_out_volume_max].asUInt();

    *pp_audio_card = p_audio_card;

    if(root.isMember(g_key_support_dsd))
        p_audio_card->support_dsd = root[g_key_support_dsd].asBool();

    if(root.isMember(g_key_support_hfp))
        p_audio_card->support_hfp = root[g_key_support_hfp].asBool();

    if(root.isMember(g_key_is_hdmi_card))
        p_audio_card->is_hdmi_card = root[g_key_is_hdmi_card].asBool();

    if(root.isMember(g_key_support_multi_chn))
        p_audio_card->support_multi_chn = root[g_key_support_multi_chn].asBool();

    if(root.isMember(g_key_out_period_size))
        p_audio_card->out_period_size = root[g_key_out_period_size].asUInt();

    if(root.isMember(g_key_out_period_count))
        p_audio_card->out_period_count = root[g_key_out_period_count].asUInt();

    ALOGI("%s: driver name %s, bus name %s, out_devices 0x%x, in_devices 0x%x, out_vol[%d, %d], dsd %d, hfp %d, hdmi %d, multi_chn %d, period_size %d, period_count %d",
       __func__, p_audio_card->driver_name, p_audio_card->bus_name,
      p_audio_card->supported_out_devices, p_audio_card->supported_in_devices,
      p_audio_card->out_volume_min, p_audio_card->out_volume_max,
      p_audio_card->support_dsd, p_audio_card->support_hfp,
      p_audio_card->is_hdmi_card, p_audio_card->support_multi_chn,
      p_audio_card->out_period_size, p_audio_card->out_period_count);

    return true;

parse_error:
    if(p_audio_card && p_audio_card->driver_name)
        free(p_audio_card->driver_name);

    if(p_audio_card && p_audio_card->bus_name)
        free(p_audio_card->bus_name);

    if(p_audio_card)
        free(p_audio_card);

    return false;
}

bool parse_all_cards(struct audio_card **audio_card_list)
{
    DIR *vidDir = NULL;
    struct dirent *dirEntry;
    bool parse_ok = false;
    int card_idx = 0;

    ALOGI("enter parse_all_cards");

    if(audio_card_list == NULL)
        return false;

    ALOGI("parse audio cards config under %s", g_kAudioConfigPath);
    vidDir = opendir(g_kAudioConfigPath);
    if (vidDir == NULL) {
        ALOGE("!!!!!! %s, opendir %s failed", __func__, g_kAudioConfigPath);
        return false;
    }

    while ((dirEntry = readdir(vidDir)) != NULL) {
        char config_file[PATH_MAX] = {0};
        if(!strstr(dirEntry->d_name, ".json"))
            continue;

        snprintf(config_file, PATH_MAX, "%s/%s", g_kAudioConfigPath, dirEntry->d_name);
        parse_ok = parse_one_card(config_file, &audio_card_list[card_idx]);
        if(parse_ok == false) {
            ALOGW("!!!!!! parse %s failed", config_file);
            continue;
        }

        card_idx++;
        if(card_idx >= MAX_SUPPORT_CARD_NUM)
            break;
   }

    ALOGI("leave parse_all_cards, supported card num %d", card_idx);

    return (card_idx > 0) ? true : false;
}

bool release_route(struct route_setting *route) {
    int i = 0;

    if(route == NULL)
        return false;

    while(route[i].ctl_name) {
        free(route[i].ctl_name);
        if(route[i].strval)
            free(route[i].strval);
        i++;
    }

    free(route);

    return true;
}

bool release_one_card(struct audio_card *audio_card) {
    if(audio_card == NULL)
        return false;

    if(audio_card->driver_name)
        free(audio_card->driver_name);

    if(audio_card->bus_name)
        free(audio_card->bus_name);

    if(audio_card->init_ctl)
        release_route(audio_card->init_ctl);

    if(audio_card->speaker_ctl)
        release_route(audio_card->speaker_ctl);

    if(audio_card->headphone_ctl)
        release_route(audio_card->headphone_ctl);

    if(audio_card->builtin_mic_ctl)
        release_route(audio_card->builtin_mic_ctl);

    if(audio_card->headset_mic_ctl)
        release_route(audio_card->headset_mic_ctl);

    if(audio_card->out_volume_ctl)
        release_route(audio_card->out_volume_ctl);

    free(audio_card);

    return true;
}

bool release_all_cards(struct audio_card **audio_card_list) {
    int i = 0;

    if(audio_card_list == NULL)
        return false;

    while(audio_card_list[i]) {
        release_one_card(audio_card_list[i]);
        i++;
    }

    return true;
}
