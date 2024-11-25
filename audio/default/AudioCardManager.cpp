/*
 * Copyright 2020-2024 NXP
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
#define LOG_TAG "AHAL_AudioCardManager"
#include <core-impl/AudioCardManager.h>
#include <android-base/file.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <dirent.h>
#include <json/json.h>
#include <json/reader.h>
#include <log/log.h>
#include <stdlib.h>
#include <string.h>
#include <system/audio-base.h>
#include <system/audio-hal-enums.h>

namespace aidl::android::hardware::audio::core {
static struct audio_card* s_audio_card_list[MAX_SUPPORT_CARD_LIST_SIZE];

void AudioCardManager::init()
{
    parseCardFromJson();
    scanAvailableCard();

    for (size_t i = 0; i < mCards.size(); i++)
        set_route_by_array(mMixers[i], mCards[i]->init_ctl, 1);

    for (size_t i = 0; i < mCards.size(); i++)
        set_route_by_array(mMixers[i], mCards[i]->builtin_mic_ctl, 1);
}

void AudioCardManager::release()
{
    int i = 0;

    while (s_audio_card_list[i]) {
        releaseCard(s_audio_card_list[i]);
        s_audio_card_list[i] = NULL;
        i++;
    }
}

struct audio_card* AudioCardManager::getCardForDevice(const ::aidl::android::media::audio::common::AudioDevice& audioDevice)
{
    struct audio_card* card = NULL;
    audio_devices_t audio_device = VALUE_OR_FATAL(
            aidl2legacy_AudioDeviceDescription_audio_devices_t(audioDevice.type));

    LOG(INFO) << __func__ << ": device: " << audioDevice.toString();
    return getCardForDevice(audio_device);
}

struct audio_card* AudioCardManager::getCardForDevice(const audio_devices_t& audioDevice)
{
    struct audio_card* card = NULL;
    bool input = audioDevice & AUDIO_DEVICE_BIT_IN ? true : false;

    if (!input) {
        for (const auto& c : mCards) {
            if (c->supported_out_devices & audioDevice) {
                card = c;
                break;
            }
        }
    } else {
        for (const auto& c : mCards) {
            if (c->supported_in_devices & (audioDevice & ~AUDIO_DEVICE_BIT_IN)) {
                card = c;
                break;
            }
        }
    }
    if (card)
        ALOGI("%s: device: %x, card%d: %s", __func__, audioDevice, card->card, card->driver_name);
    else
        ALOGE("%s: device: %x, card not found.", __func__, audioDevice);

    return card;
}

std::vector<struct audio_card *>AudioCardManager::mCards;
std::vector<struct mixer *>AudioCardManager::mMixers;

void AudioCardManager::scanAvailableCard()
{
    for (int card = 0; card < MAX_SUPPORT_CARD_NUM; card++) {
        struct mixer *mixer = mixer_open(card);
        if (!mixer) {
            ALOGV("%s: Failed to open mixer for card%d", __func__, card);
            continue;
        }

        const char *card_name = mixer_get_name(mixer);
        ALOGI("%s: card%d: %s", __func__, card, card_name);

        struct audio_card *audio_card = audio_card_get_by_name(card_name);
        if (!audio_card) {
            ALOGV("%s: card %s is not supported", __func__, card_name);
            continue;
        }
        audio_card->card = card;
        audio_card->card_name = strdup(card_name);
        audio_card->locked = false;
        mCards.push_back(audio_card);
        mMixers.push_back(mixer);
    }

    ALOGI("Total %zu cards match", mCards.size());
    for (const auto& card : mCards)
        ALOGI("card%d: %s", card->card, card->driver_name);

    if (mCards.empty()) {
        ALOGE("no supported sound card found.");
    }
}

int AudioCardManager::set_route_by_array(struct mixer *mixer, struct route_setting *route, int enable) {
    struct mixer_ctl *ctl = NULL;
    unsigned int i, j;

    if (!mixer)
        return 0;
    if (!route)
        return 0;
    /* Go through the route array and set each value */
    i = 0;
    while (route[i].ctl_name) {
        ctl = mixer_get_ctl_by_name(mixer, route[i].ctl_name);
        if (!ctl) {
            ALOGE("%s, no such ctrl in %s", __func__, route[i].ctl_name);
            return -EINVAL;
        }

        if (route[i].strval) {
            if (enable)
                mixer_ctl_set_enum_by_string(ctl, route[i].strval);
            else
                mixer_ctl_set_enum_by_string(ctl, "Off");
        } else {
            /* This ensures multiple (i.e. stereo) values are set jointly */
            for (j = 0; j < mixer_ctl_get_num_values(ctl); j++) {
                if (enable)
                    mixer_ctl_set_value(ctl, j, route[i].intval);
                else
                    mixer_ctl_set_value(ctl, j, 0);
            }
        }
        i++;
    }

    return 0;
}

/*-------------------------------------------------------------------------------*/

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
static const char* const g_key_support_compress = "support_compress";
static const char* const g_key_support_hfp = "support_hfp";
static const char* const g_key_is_hdmi_card = "is_hdmi_card";
static const char* const g_key_support_multi_chn = "support_multi_chn";
static const char* const g_key_support_lpa = "support_lpa";
static const char* const g_key_out_period_size = "out_period_size";
static const char* const g_key_out_period_count = "out_period_count";
static const char* const g_key_in_period_size = "in_period_size";
static const char* const g_key_in_period_count = "in_period_count";
static const char* const g_key_secondary_bus_name = "secondary_bus_name";
static const char* const g_key_support_s16 = "support_s16";

struct audio_devcie_map {
    char const* name;
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

#define ARRAY_SIZE(a) (unsigned int)(sizeof(a) / sizeof(a[0]))

unsigned int map_audio_name_to_device(const struct audio_devcie_map* map, unsigned int size,
                                      const char* name) {
    for (unsigned int i = 0; i < size; i++) {
        if (!strcmp(name, map[i].name))
            return map[i].device;
    }

    return 0;
}

static unsigned int parse_devices(const struct audio_devcie_map* map, unsigned int size,
                                  Json::Value device_array) {
    unsigned int devices = 0;
    unsigned int device_num = device_array.size();

    for (unsigned int i = 0; i < device_num; i++) {
        unsigned int device = 0;
        const char* name = device_array[i].asCString();
        device = map_audio_name_to_device(map, size, name);
        devices |= device;
    }

    return devices;
}

static bool parse_control(struct route_setting** pp_route, Json::Value ctrl_array) {
    struct route_setting* p_route = NULL;
    unsigned int ctrl_num = ctrl_array.size();

    // Always "NULL" control at last, so "ctrl_num + 1".
    p_route = (struct route_setting*)calloc(ctrl_num + 1, sizeof(struct route_setting));
    if (p_route == NULL) {
        ALOGE("%s: calloc struct route_setting, %zu bytes failed", __func__,
              sizeof(struct route_setting));
        return false;
    }

    for (unsigned int i = 0; i < ctrl_num; i++) {
        p_route[i].ctl_name = strdup(ctrl_array[i]["name"].asCString());

        const char* type = ctrl_array[i]["type"].asCString();
        if (!strcmp(type, "int")) {
            p_route[i].intval = ctrl_array[i]["val"].asInt();
            ALOGI("%s: ctl idx %d, [%s %d]", __func__, i, p_route[i].ctl_name, p_route[i].intval);
        } else if (!strcmp(type, "str")) {
            p_route[i].strval = strdup(ctrl_array[i]["val"].asCString());
            ALOGI("%s: ctl idx %d, [%s %s]", __func__, i, p_route[i].ctl_name, p_route[i].strval);
        } else {
            ALOGE("%s: control type %s is neither int nor str", __func__, type);
            free(p_route);
            return false;
        }
    }

    *pp_route = p_route;

    return true;
}

static bool parse_volume_control(struct route_setting** pp_route, Json::Value ctrl_array) {
    struct route_setting* p_route = NULL;
    unsigned int ctrl_num = ctrl_array.size();

    // Always "NULL" control at last, so "ctrl_num + 1".
    p_route = (struct route_setting*)calloc(ctrl_num + 1, sizeof(struct route_setting));
    if (p_route == NULL) {
        ALOGE("%s: calloc struct route_setting, %zu bytes failed", __func__,
              sizeof(struct route_setting));
        return false;
    }

    for (unsigned int i = 0; i < ctrl_num; i++) {
        p_route[i].ctl_name = strdup(ctrl_array[i].asCString());
        ALOGI("%s: ctl idx %d, name %s", __func__, i, p_route[i].ctl_name);
    }

    *pp_route = p_route;

    return true;
}

static bool parse_one_card(char* config_file, struct audio_card** pp_audio_card) {
    std::string config;
    struct audio_card* p_audio_card = NULL;

    if ((config_file == NULL) || (pp_audio_card == NULL))
        return false;

    ALOGI("%s: parse %s\n", __func__, config_file);

    if (!::android::base::ReadFileToString(config_file, &config)) {
        ALOGE("%s: Could not open configuration file: %s", __func__, config_file);
        return false;
    }

    Json::CharReaderBuilder builder;
    std::unique_ptr<Json::CharReader> config_reader(builder.newCharReader());
    std::string errorMessage;
    Json::Value root;
    if (!config_reader->parse(config.data(), config.data() + config.length(), &root,
                              &errorMessage)) {
        ALOGE("Could not parse configuration file: %s, %s", errorMessage.c_str(), config_file);
        return false;
    }

    if (!root.isObject()) {
        ALOGE("%s: Configuration root is not an object", __func__);
        return false;
    }

    p_audio_card = (struct audio_card*)calloc(1, sizeof(struct audio_card));
    if (p_audio_card == NULL) {
        ALOGE("%s: calloc struct audio_card, %zu bytes failed", __func__,
              sizeof(struct audio_card));
        return false;
    }

    if (!root.isMember(g_key_driver_name))
        goto parse_error;
    p_audio_card->driver_name = strdup(root[g_key_driver_name].asCString());

    if (root.isMember(g_key_bus_name))
        p_audio_card->bus_name = strdup(root[g_key_bus_name].asCString());

    if (root.isMember(g_key_supported_out_devices))
        p_audio_card->supported_out_devices =
                parse_devices(g_out_device_map, ARRAY_SIZE(g_out_device_map),
                              root[g_key_supported_out_devices]);

    if (root.isMember(g_key_supported_in_devices))
        p_audio_card->supported_in_devices =
                parse_devices(g_in_device_map, ARRAY_SIZE(g_in_device_map),
                              root[g_key_supported_in_devices]);

    if ((p_audio_card->supported_out_devices == 0) && (p_audio_card->supported_in_devices == 0)) {
        ALOGE("%s: card %s is neighter output nor input", __func__, p_audio_card->driver_name);
        goto parse_error;
    }

    if (root.isMember(g_key_init_ctl))
        parse_control(&p_audio_card->init_ctl, root[g_key_init_ctl]);

    if (root.isMember(g_key_speaker_ctl))
        parse_control(&p_audio_card->speaker_ctl, root[g_key_speaker_ctl]);

    if (root.isMember(g_key_headphone_ctl))
        parse_control(&p_audio_card->headphone_ctl, root[g_key_headphone_ctl]);

    if (root.isMember(g_key_builtin_mic_ctl))
        parse_control(&p_audio_card->builtin_mic_ctl, root[g_key_builtin_mic_ctl]);

    if (root.isMember(g_key_headset_mic_ctl))
        parse_control(&p_audio_card->headset_mic_ctl, root[g_key_headset_mic_ctl]);

    if (root.isMember(g_key_out_volume_ctl))
        parse_volume_control(&p_audio_card->out_volume_ctl, root[g_key_out_volume_ctl]);

    p_audio_card->out_volume_min = OUT_VOL_MIN_DFT;
    if (root.isMember(g_key_out_volume_min))
        p_audio_card->out_volume_min = root[g_key_out_volume_min].asUInt();

    p_audio_card->out_volume_max = OUT_VOL_MAX_DFT;
    if (root.isMember(g_key_out_volume_max))
        p_audio_card->out_volume_max = root[g_key_out_volume_max].asUInt();

    *pp_audio_card = p_audio_card;

    if (root.isMember(g_key_support_dsd))
        p_audio_card->support_dsd = root[g_key_support_dsd].asBool();

    if (root.isMember(g_key_support_compress))
        p_audio_card->support_compress = root[g_key_support_compress].asBool();

    if (root.isMember(g_key_support_hfp))
        p_audio_card->support_hfp = root[g_key_support_hfp].asBool();

    if (root.isMember(g_key_is_hdmi_card))
        p_audio_card->is_hdmi_card = root[g_key_is_hdmi_card].asBool();

    if (root.isMember(g_key_support_multi_chn))
        p_audio_card->support_multi_chn = root[g_key_support_multi_chn].asBool();

    if (root.isMember(g_key_support_lpa))
        p_audio_card->support_lpa = root[g_key_support_lpa].asBool();

    /* It is assumed that all cards support s16 format */
    p_audio_card->support_s16 = true;
    if (root.isMember(g_key_support_s16))
        p_audio_card->support_s16 = root[g_key_support_s16].asBool();

    if (root.isMember(g_key_out_period_size))
        p_audio_card->out_period_size = root[g_key_out_period_size].asUInt();

    if (root.isMember(g_key_secondary_bus_name))
        p_audio_card->secondary_bus_name = strdup(root[g_key_secondary_bus_name].asCString());

    if (root.isMember(g_key_out_period_count))
        p_audio_card->out_period_count = root[g_key_out_period_count].asUInt();

    if (root.isMember(g_key_in_period_size))
        p_audio_card->in_period_size = root[g_key_in_period_size].asUInt();

    if (root.isMember(g_key_in_period_count))
        p_audio_card->in_period_count = root[g_key_in_period_count].asUInt();

    ALOGI("%s: driver name %s, bus name %s, out_devices 0x%x, in_devices 0x%x, out_vol[%d, %d], "
          "dsd %d, compress %d, hfp %d, hdmi %d, multi_chn %d, out period_size %d, out "
          "period_count %d, in period_size %d, in period_count %d, secondary_bus_name: %s",
          __func__, p_audio_card->driver_name, p_audio_card->bus_name,
          p_audio_card->supported_out_devices, p_audio_card->supported_in_devices,
          p_audio_card->out_volume_min, p_audio_card->out_volume_max, p_audio_card->support_dsd,
          p_audio_card->support_compress, p_audio_card->support_hfp, p_audio_card->is_hdmi_card,
          p_audio_card->support_multi_chn, p_audio_card->out_period_size,
          p_audio_card->out_period_count, p_audio_card->in_period_size,
          p_audio_card->in_period_count, p_audio_card->secondary_bus_name);

    return true;

parse_error:
    if (p_audio_card && p_audio_card->driver_name)
        free(p_audio_card->driver_name);

    if (p_audio_card && p_audio_card->bus_name)
        free(p_audio_card->bus_name);

    if (p_audio_card)
        free(p_audio_card);

    return false;
}

void AudioCardManager::parseCardFromJson(void) {
    DIR* vidDir = NULL;
    struct dirent* dirEntry;
    int card_idx = 0;

    ALOGI("%s: %s", __func__, g_kAudioConfigPath);
    vidDir = opendir(g_kAudioConfigPath);
    if (vidDir == NULL) {
        ALOGE("!!!!!! %s, opendir %s failed", __func__, g_kAudioConfigPath);
        return;
    }

    while ((dirEntry = readdir(vidDir)) != NULL) {
        char config_file[PATH_MAX] = {0};
        if (!strstr(dirEntry->d_name, ".json"))
            continue;

        snprintf(config_file, PATH_MAX, "%s/%s", g_kAudioConfigPath, dirEntry->d_name);
        if (!parse_one_card(config_file, &s_audio_card_list[card_idx])) {
            ALOGW("!!!!!! parse %s failed", config_file);
            continue;
        }

        card_idx++;
        if (card_idx >= MAX_SUPPORT_CARD_NUM)
            break;
    }

    closedir(vidDir);
}

void release_route(struct route_setting* route) {
    int i = 0;

    if (route == NULL)
        return;

    while (route[i].ctl_name) {
        free(route[i].ctl_name);
        if (route[i].strval)
            free(route[i].strval);
        i++;
    }

    free(route);
}

void AudioCardManager::releaseCard(struct audio_card* audio_card) {
    if (audio_card == NULL)
        return;

    if (audio_card->driver_name)
        free(audio_card->driver_name);

    if (audio_card->card_name)
        free(audio_card->card_name);

    if (audio_card->bus_name)
        free(audio_card->bus_name);

    if (audio_card->init_ctl)
        release_route(audio_card->init_ctl);

    if (audio_card->speaker_ctl)
        release_route(audio_card->speaker_ctl);

    if (audio_card->headphone_ctl)
        release_route(audio_card->headphone_ctl);

    if (audio_card->builtin_mic_ctl)
        release_route(audio_card->builtin_mic_ctl);

    if (audio_card->headset_mic_ctl)
        release_route(audio_card->headset_mic_ctl);

    if (audio_card->out_volume_ctl)
        release_route(audio_card->out_volume_ctl);

    free(audio_card);
}

struct audio_card* AudioCardManager::audio_card_get_by_name(const char* name) {
    struct audio_card* audio_card;

    if (!name)
        return NULL;

    for (int i = 0; i < MAX_SUPPORT_CARD_NUM; i++) {
        audio_card = s_audio_card_list[i];
        if (!audio_card) {
            return NULL;
        }
        if (strstr(name, audio_card->driver_name) != NULL) {
            return audio_card;
        }
    }
    return NULL;
}
}  // namespace aidl::android::hardware::audio::core
