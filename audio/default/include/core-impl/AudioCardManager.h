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
#pragma once

#include <vector>

#include <aidl/android/hardware/audio/core/BnModule.h>
#include <android-base/logging.h>
#include "system/audio-hal-enums.h"

#include <tinyalsa/asoundlib.h>
#include <media/AidlConversionCppNdk.h>

#define OUT_VOL_MIN_DFT 0
#define OUT_VOL_MAX_DFT 255

#define MAX_SUPPORT_CARD_NUM 16
#define MAX_SUPPORT_CARD_LIST_SIZE \
    (unsigned int)(MAX_SUPPORT_CARD_NUM + 1) // "NULL" point to end the array

namespace aidl::android::hardware::audio::core {

class AudioCardManager {
public:
    // Parse the audio json files under /vendor/etc/configs/audio
    // Scan available cards on the system (/proc/asound/cards)
    // Initialize card's mixer controls
    static void init();
    static void release();
    // Get the first card which supports audioDevice; NULL if not found.
    static struct audio_card* getCardForDevice(const ::aidl::android::media::audio::common::AudioDevice& audioDevice);
    static struct audio_card* getCardForDevice(const audio_devices_t& audioDevice);

private:
    static std::vector<struct audio_card *>mCards;
    static std::vector<struct mixer *>mMixers;
    static int set_route_by_array(struct mixer *mixer, struct route_setting *route, int enable);
    static void scanAvailableCard();
    static void parseCardFromJson(void);
    static void releaseCard(struct audio_card* audio_card);
    static struct audio_card* audio_card_get_by_name(const char* name);
};

struct route_setting {
    char *ctl_name;
    int intval;
    char *strval;
};

struct audio_card {
    char *driver_name;
    char *card_name;
    char *bus_name;           // Used in Android Automotive
    char *secondary_bus_name; // Used for some sound cards which support multiple pcm device
    unsigned int supported_out_devices;
    unsigned int supported_in_devices;
    struct route_setting *init_ctl;
    struct route_setting *speaker_ctl;
    struct route_setting *headphone_ctl;
    struct route_setting *builtin_mic_ctl;
    struct route_setting *headset_mic_ctl;
    struct route_setting *out_volume_ctl;
    int card;
    int out_format;
    int in_format;
    unsigned int out_volume_min;
    unsigned int out_volume_max;
    bool support_hfp;
    bool support_dsd;
    bool support_compress;
    bool is_hdmi_card;
    bool support_multi_chn;
    bool support_lpa;
    bool support_s16;
    unsigned int out_period_size;
    unsigned int out_period_count;
    unsigned int in_period_size;
    unsigned int in_period_count;
    bool locked;
};

}  // namespace aidl::android::hardware::audio::core
