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
/* Copyright (C) 2012-2016 Freescale Semiconductor, Inc. */
/* Copyright 2017-2020 NXP */

#define LOG_TAG "audio_hw_primary"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <stdlib.h>
#include <unistd.h>
#include <math.h>


#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>
#include <cutils/hashmap.h>

#include <hardware/hardware.h>
#include <hardware_legacy/power.h>
#include <system/audio.h>
#include <hardware/audio.h>
#include <sound/asound.h>

#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>
#include <audio_utils/echo_reference.h>
#include <hardware/audio_effect.h>
#include <audio_effects/effect_aec.h>

#include "audio_hardware.h"
#include "pcm_ext.h"


/*align the definition in kernel for hdmi audio*/
#define HDMI_PERIOD_SIZE       192
#define PLAYBACK_HDMI_PERIOD_COUNT      8

#define ESAI_PERIOD_SIZE       1024
#define PLAYBACK_ESAI_PERIOD_COUNT      4

#define DSD_PERIOD_SIZE       1024
#define PLAYBACK_DSD_PERIOD_COUNT       8

/* number of frames per short period (low latency) */
/* align other card with hdmi, same latency*/
#define SHORT_PERIOD_SIZE       384
/* number of short periods in a long period (low power) */
#define LONG_PERIOD_MULTIPLIER  2
/* number of frames per long period (low power) */
#define LONG_PERIOD_SIZE        192
/* number of periods for low power playback */
#define PLAYBACK_LONG_PERIOD_COUNT  8
/* number of periods for capture */
#define CAPTURE_PERIOD_SIZE  256
/* number of periods for capture */
#define CAPTURE_PERIOD_COUNT 32
/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 5000

#define DSD64_SAMPLING_RATE 2822400
#define DSD_RATE_TO_PCM_RATE 32
// DSD pcm param: 2 channel, 32 bit
#define DSD_FRAMESIZE_BYTES 8

// Limit LPA max latency to 300ms
#define LPA_LATENCY_MS 300

#ifndef PCM_LPA
#define PCM_LPA 0
#endif

#ifndef PCM_FLAG_DSD
#define PCM_FLAG_DSD 0
#endif

#define SCO_RATE 16000
/* audio input device for hfp */
#define SCO_IN_DEVICE AUDIO_DEVICE_IN_BUILTIN_MIC

// sample rate requested by BT firmware on HSP
static uint32_t g_hsp_sample_rate = 16000;
static int g_hsp_chns = 2;

/* product-specific defines */
#define PRODUCT_DEVICE_PROPERTY "ro.product.device"
#define PASSTHROUGH_PROPERTY "vendor.persist.audio.pass.through"

#define PASSTHROUGH_PROPERTY_ENABLE 2000

#define DEFAULT_ERROR_NAME_str "0"

struct pcm_config pcm_config_mm_out = {
    .channels = DEFAULT_OUTPUT_CHANNEL_COUNT,
    .rate = DEFAULT_OUTPUT_SAMPLE_RATE,
    .period_size = LONG_PERIOD_SIZE,
    .period_count = PLAYBACK_LONG_PERIOD_COUNT,
    .format = DEFAULT_OUTPUT_FORMAT_PCM,
    .start_threshold = 0,
    .avail_min = 0,
};

struct pcm_config pcm_config_hdmi_multi = {
    .channels = 8, /* changed when the stream is opened */
    .rate = DEFAULT_OUTPUT_SAMPLE_RATE, /* changed when the stream is opened */
    .period_size = HDMI_PERIOD_SIZE,
    .period_count = PLAYBACK_HDMI_PERIOD_COUNT,
    .format = DEFAULT_OUTPUT_FORMAT_PCM,
    .start_threshold = 0,
    .avail_min = 0,
};
struct pcm_config pcm_config_esai_multi = {
    .channels = 8, /* changed when the stream is opened */
    .rate = DEFAULT_OUTPUT_SAMPLE_RATE, /* changed when the stream is opened */
    .period_size = ESAI_PERIOD_SIZE,
    .period_count = PLAYBACK_ESAI_PERIOD_COUNT,
    .format = DEFAULT_OUTPUT_FORMAT_PCM,
    .start_threshold = 0,
    .avail_min = 0,
};

int lpa_enable = 0;
bool passthrough_enabled = false;
bool passthrough_for_s24 = false;

struct pcm_config pcm_config_dsd = {
    .channels = 2,
    .rate = DSD64_SAMPLING_RATE / DSD_RATE_TO_PCM_RATE, /* changed when the stream is opened */
    .period_size = DSD_PERIOD_SIZE,
    .period_count = PLAYBACK_DSD_PERIOD_COUNT,
    .format = PCM_FORMAT_S32_LE, // This will be converted to SNDRV_PCM_FORMAT_DSD_U32_LE in tinyalsa
    .start_threshold = 0,
    .avail_min = 0,
};
struct pcm_config pcm_config_sco_out = {
    .channels = 1,
    .rate = SCO_RATE,
    .period_size = LONG_PERIOD_SIZE,
    .period_count = PLAYBACK_LONG_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .avail_min = 0,
};

struct pcm_config pcm_config_sco_in = {
    .channels = 1,
    .rate = SCO_RATE,
    .period_size = CAPTURE_PERIOD_SIZE,
    .period_count = CAPTURE_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .avail_min = 0,
};

struct pcm_config pcm_config_mm_in = {
    .channels = DEFAULT_INPUT_CHANNEL_COUNT,
    .rate = DEFAULT_INPUT_SAMPLE_RATE,
    .period_size = CAPTURE_PERIOD_SIZE,
    .period_count = CAPTURE_PERIOD_COUNT,
    .format = DEFAULT_INPUT_FORMAT_PCM,
    .start_threshold = 0,
    .avail_min = 0,
};

const struct string_to_enum out_channels_name_to_enum_table[] = {
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_STEREO),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_5POINT1),
    STRING_TO_ENUM(AUDIO_CHANNEL_OUT_7POINT1),
};

/**
 * NOTE: when multiple mutexes have to be acquired, always respect the following order:
 *        hw device > in stream > out stream
 */
static void select_output_device(struct imx_audio_device *adev);
static void select_input_device(struct imx_audio_device *adev);
static int adev_set_voice_volume(struct audio_hw_device *dev, float volume);
static int do_input_standby(struct imx_stream_in *in);
static int do_output_standby(struct imx_stream_out *out, int force_standby);
static int scan_available_device(struct imx_audio_device *adev);
static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                                   struct resampler_buffer* buffer);
static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                                  struct resampler_buffer* buffer);
static int adev_get_format_for_device(struct imx_audio_device *adev, uint32_t devices, unsigned int flag);
static void in_update_aux_channels(struct imx_stream_in *in, effect_handle_t effect);
static int pcm_read_wrapper(struct pcm *pcm, const void * buffer, size_t bytes, bool dump);
static void convert_output_for_esai(const void* buffer, size_t bytes, int channels);

extern "C" int pcm_state(struct pcm *pcm);

static enum pcm_format pcm_format_from_audio_format(audio_format_t format)
{
    switch (format) {
    case AUDIO_FORMAT_PCM_16_BIT:
        return PCM_FORMAT_S16_LE;
    case AUDIO_FORMAT_PCM_24_BIT_PACKED:
        return PCM_FORMAT_S24_3LE;
    case AUDIO_FORMAT_PCM_32_BIT:
        return PCM_FORMAT_S32_LE;
    case AUDIO_FORMAT_PCM_8_24_BIT:
        return PCM_FORMAT_S24_LE;
    case AUDIO_FORMAT_PCM_FLOAT:  /* there is no equivalent for float */
    default:
        return PCM_FORMAT_INVALID;
    }
}

/* Standard passthrough data format is PCM_FORMAT_S16_LE */
/* Wrap it to PCM_FORMAT_S24_LE for evk_8mp */
static void convert_passthrough_data_for_s24(void *src, void *dst,
        unsigned int frames)
{
    unsigned int i;
    short *src_t = (short *)src; // source is 16 bit
    int *dst_t = (int *)dst; // target is 24 bit
    short left, right;

    for (i = 0; i < frames; i++) {
        left = *src_t++;
        right = *src_t++;
        *dst_t++ = (int)(left << 8);
        *dst_t++ = (int)(right << 8);
    }
}

static int convert_record_data(void *src, void *dst, unsigned int frames, bool bit_24b_2_16b, bool bit_32b_2_16b, bool mono2stereo, bool stereo2mono)
{
    unsigned int i;
    short *dst_t = (short *)dst;

    if (bit_24b_2_16b && mono2stereo && !stereo2mono) {
        int data;
        int *src_t = (int *)src;
        for (i = 0; i < frames; i++) {
            data = *src_t++;
            *dst_t++ = (short)(data >> 8);
            *dst_t++ = (short)(data >> 8);
        }

        return 0;
    }

    if (bit_24b_2_16b && !mono2stereo && stereo2mono) {
        int data1 = 0, data2 = 0;
        int *src_t = (int *)src;
        for (i = 0; i < frames; i++) {
            data1 = *src_t++;
            data2 = *src_t++;
            *dst_t++ = (short)(((data1 << 8) >> 17) + ((data2 << 8) >> 17));
        }

        return 0;
    }

    if (bit_24b_2_16b && !mono2stereo && !stereo2mono) {
        int data1, data2;
        int *src_t = (int *)src;
        for (i = 0; i < frames; i++) {
            data1 = *src_t++;
            data2 = *src_t++;
            *dst_t++ = (short)(data1 >> 8);
            *dst_t++ = (short)(data2 >> 8);
        }

        return 0;
    }

    if (bit_32b_2_16b && mono2stereo && !stereo2mono) {
        int data;
        int *src_t = (int *)src;
        for (i = 0; i < frames; i++) {
            data = *src_t++;
            *dst_t++ = (short)(data >> 16);
            *dst_t++ = (short)(data >> 16);
        }

        return 0;
    }

    if (bit_32b_2_16b && !mono2stereo && stereo2mono) {
        int data1 = 0, data2 = 0;
        int *src_t = (int *)src;
        for (i = 0; i < frames; i++) {
            data1 = *src_t++;
            data2 = *src_t++;
            *dst_t++ = (short)((data1 >> 17) + (data2 >> 17));
        }

        return 0;
    }

    if (bit_32b_2_16b && !mono2stereo && !stereo2mono) {
        int data1, data2;
        int *src_t = (int *)src;
        for (i = 0; i < frames; i++) {
            data1 = *src_t++;
            data2 = *src_t++;
            *dst_t++ = (short)(data1 >> 16);
            *dst_t++ = (short)(data2 >> 16);
        }

        return 0;
    }

    if (mono2stereo && !stereo2mono) {
        short data;
        short *src_t = (short *)src;
        for (i = 0; i < frames; i++) {
            data = *src_t++;
            *dst_t++ = data;
            *dst_t++ = data;
        }

        return 0;
    }

    if (!mono2stereo && stereo2mono) {
        short data1, data2;
        short *src_t = (short *)src;
        for (i = 0; i < frames; i++) {
            data1 = *src_t++;
            data2 = *src_t++;
            *dst_t++ = (data1 >> 1) + (data2 >> 1);
        }

        return 0;
    }

    return 0;
}

/* The enable flag when 0 makes the assumption that enums are disabled by
 * "Off" and integers/booleans by 0 */
static int set_route_by_array(struct mixer *mixer, struct route_setting *route,
                              int enable)
{
    struct mixer_ctl *ctl = NULL;
    unsigned int i, j;

    if(!mixer) return 0;
    if(!route) return 0;
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

static void select_mode(struct imx_audio_device *adev)
{
    if (adev->mode == AUDIO_MODE_IN_CALL) {
        ALOGW("Entering IN_CALL state, in_call=%d", adev->in_call);
        if (!adev->in_call) {
            /* force earpiece route for in call state if speaker is the
            only currently selected route. This prevents having to tear
            down the modem PCMs to change route from speaker to earpiece
            after the ringtone is played, but doesn't cause a route
            change if a headset or bt device is already connected. If
            speaker is not the only thing active, just remove it from
            the route. We'll assume it'll never be used initally during
            a call. This works because we're sure that the audio policy
            manager will update the output device after the audio mode
            change, even if the device selection did not change. */
            if (adev->out_device == AUDIO_DEVICE_OUT_SPEAKER) {
                adev->out_device = AUDIO_DEVICE_OUT_EARPIECE;
                adev->in_device = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;
            } else
                adev->out_device &= ~AUDIO_DEVICE_OUT_SPEAKER;
            select_output_device(adev);

            adev_set_voice_volume(&adev->hw_device, adev->voice_volume);
            adev->in_call = 1;
        }
    } else {
        ALOGW("Leaving IN_CALL state, in_call=%d, mode=%d",
             adev->in_call, adev->mode);
        if (adev->in_call) {
            adev->in_call = 0;
            select_output_device(adev);
            select_input_device(adev);
        }
    }
}

static void select_output_device(struct imx_audio_device *adev)
{
    int headset_on;
    int headphone_on;
    int speaker_on;
    int i;

    headset_on      = adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADSET;
    headphone_on    = adev->out_device & (AUDIO_DEVICE_OUT_WIRED_HEADPHONE | AUDIO_DEVICE_OUT_BUS);
    speaker_on      = adev->out_device & (AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_BUS);

    ALOGI("%s(), headphone %d ,headset %d ,speaker %d\n", __func__, headphone_on, headset_on, speaker_on);
    /* select output stage */
    for(i = 0; i < adev->audio_card_num; i++)
        set_route_by_array(adev->mixer[i], adev->card_list[i]->headphone_ctl, headset_on | headphone_on);
    for(i = 0; i < adev->audio_card_num; i++)
        set_route_by_array(adev->mixer[i], adev->card_list[i]->speaker_ctl, speaker_on);
}

static void select_input_device(struct imx_audio_device *adev)
{
    int i;
    int headset_on = 0;
    int main_mic_on = 0;
    int sub_mic_on = 0;
    int bt_on = adev->in_device & AUDIO_DEVICE_IN_ALL_SCO;

    if(bt_on)
        return;

    if ((adev->mode != AUDIO_MODE_IN_CALL) && (adev->active_input != 0)) {
        /* sub mic is used for camcorder or VoIP on speaker phone */
        sub_mic_on = (adev->active_input->source == AUDIO_SOURCE_CAMCORDER) ||
                     ((adev->out_device & AUDIO_DEVICE_OUT_SPEAKER) &&
                      (adev->active_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION));
    }

    headset_on = adev->in_device & AUDIO_DEVICE_IN_WIRED_HEADSET;
    main_mic_on = adev->in_device & AUDIO_DEVICE_IN_BUILTIN_MIC;

    /* Select front end */
    if (headset_on)
        for(i = 0; i < adev->audio_card_num; i++)
            set_route_by_array(adev->mixer[i], adev->card_list[i]->headset_mic_ctl, 1);
    else if (main_mic_on || sub_mic_on)
        for(i = 0; i < adev->audio_card_num; i++)
            set_route_by_array(adev->mixer[i], adev->card_list[i]->builtin_mic_ctl, 1);
    else
        for(i = 0; i < adev->audio_card_num; i++)
            set_route_by_array(adev->mixer[i], adev->card_list[i]->builtin_mic_ctl, 0);
}

static int get_card_for_dsd(struct imx_audio_device *adev, int *card_index)
{
    int i;
    int card = -1;

    if (!adev) {
        ALOGE("%s: Invalid audio device", __func__);
        return -1;
    }

    for (i = 0; i < adev->audio_card_num; i++) {
        if (adev->card_list[i]->support_dsd) {
              card = adev->card_list[i]->card;
              break;
        }
    }

    if (card_index && (i != adev->audio_card_num))
        *card_index = i;

    ALOGD("%s: card: %d", __func__, card);
    return card;
}

static int get_card_for_hfp(struct imx_audio_device *adev, int *card_index)
{
    int i;
    int card = -1;

    if (!adev) {
        ALOGE("%s: Invalid audio device", __func__);
        return -1;
    }

    for (i = 0; i < adev->audio_card_num; i++) {
        if (adev->card_list[i]->support_hfp) {
              card = adev->card_list[i]->card;
              break;
        }
    }

    if (card_index && (i != adev->audio_card_num))
        *card_index = i;

    ALOGD("%s: card: %d", __func__, card);
    return card;
}

static int get_card_for_bus(struct imx_audio_device* adev, const char* bus, int *p_array_index, int *pcm_device_id) {
    if (!adev || !bus) {
        ALOGE("Invalid audio device or bus");
        return -1;
    }

    int card = -1;
    int i = 0;
    for (i = 0; i < adev->audio_card_num; i++) {
        if (adev->card_list[i]->bus_name) {
            if (!strcmp(bus, adev->card_list[i]->bus_name)) {
                card = adev->card_list[i]->card;
                break;
            }
        }
        if (adev->card_list[i]->secondary_bus_name) {
            if (!strcmp(bus, adev->card_list[i]->secondary_bus_name)) {
                card = adev->card_list[i]->card;
                if (pcm_device_id)
                    *pcm_device_id = 1; // Hardcode the device index as 1 for secondary_bus_name
                break;
            }
        }
    }

    if (card == -1) {
        ALOGE("Failed to find card from bus '%s'", bus);
    }

    if(p_array_index && (i != adev->audio_card_num))
        *p_array_index = i;

    return card;
}

static int get_card_for_device(struct imx_audio_device *adev, int device, unsigned int flag, int *card_index)
{
    int i;
    int card = -1;

    if (device == AUDIO_DEVICE_NONE)
        device = AUDIO_DEVICE_OUT_SPEAKER;

    if (flag == PCM_OUT ) {
        for(i = 0; i < adev->audio_card_num; i++) {
            if(adev->card_list[i]->supported_out_devices & device) {
                  card = adev->card_list[i]->card;
                  break;
            }
        }
    } else {
        for(i = 0; i < adev->audio_card_num; i++) {
            if(adev->card_list[i]->supported_in_devices & device & ~AUDIO_DEVICE_BIT_IN) {
                  card = adev->card_list[i]->card;
                  break;
            }
        }
    }
    if (card_index && (i != adev->audio_card_num))
        *card_index = i;
    return card;
}
static int refine_input_parameters(uint32_t *sample_rate, audio_format_t *format,
        audio_channel_mask_t *channel_mask)
{
    static const uint32_t sample_rates [] = {8000, 11025, 16000, 22050, 24000, 32000, 44100, 48000};
    static const int sample_rates_count = sizeof(sample_rates)/sizeof(uint32_t);
    bool inval = false;
    ALOGI("%s: enter: sample_rate(%d) channel_mask(%#x) format(%#x)",
              __func__, *sample_rate, *channel_mask, *format);
    if (*format != DEFAULT_INPUT_FORMAT) {
        *format = DEFAULT_INPUT_FORMAT;
        inval = true;
    }

    int channel_count = popcount(*channel_mask);
    if (channel_count != 1 && channel_count != 2) {
        *channel_mask = DEFAULT_INPUT_CHANNEL_MASK;
        inval = true;
    }

    int i;
    for (i = 0; i < sample_rates_count; i++) {
        if (*sample_rate < sample_rates[i]) {
            *sample_rate = sample_rates[i];
            inval=true;
            break;
        }
        else if (*sample_rate == sample_rates[i]) {
            break;
        }
        else if (i == sample_rates_count-1) {
            // Cap it to the highest rate we support
            *sample_rate = sample_rates[i];
            inval=true;
        }
    }

    if (inval) {
        ALOGI("%s: unsupported input parameters.", __func__);
        return -EINVAL;
    }
    return 0;
}

static void update_passthrough_status(void)
{
    if (property_get_int32(PASSTHROUGH_PROPERTY, 0) ==
            PASSTHROUGH_PROPERTY_ENABLE)
        passthrough_enabled = true;
    else
        passthrough_enabled = false;
}

/* must be called with hw device and output stream mutexes locked */
static int start_output_stream(struct imx_stream_out *out)
{
    struct imx_audio_device *adev = out->dev;
    struct pcm_config *config = &out->config;
    int card = -1;
    int pcm_device_id = 0;
    bool success = false;
    unsigned int flags = PCM_OUT | PCM_MONOTONIC;
    static int first = 1;
    struct listnode *node;

    // Consider such use case: opening a direct output media with touch sound
    // They need use same pcm device at the same time
    // Let direct stream have high priority than non-direct stream

    // First: Standby non-direct stream if direct stream need open same device
    if (out->flags & AUDIO_OUTPUT_FLAG_DIRECT) {
        list_for_each(node, &adev->out_streams) {
            struct imx_stream_out *stream = node_to_item(
                    node, struct imx_stream_out, stream_node);
            if ((stream->device == out->device) &&
                    (!(stream->flags & AUDIO_OUTPUT_FLAG_DIRECT)) &&
                    (!stream->standby)) {
                pthread_mutex_lock(&stream->lock);
                do_output_standby(stream, true);
                pthread_mutex_unlock(&stream->lock);
                ALOGI("%s: standby non-direct stream(%p) while " \
                        "direct stream need open the same pcm device: %d.",
                        __func__, stream, out->device);
            }
        }
    }
    // Second: Just return for non-direct stream if device is opened by other stream
    if ((!(out->flags & AUDIO_OUTPUT_FLAG_DIRECT)) &&
            (out->device != AUDIO_DEVICE_OUT_BUS)) {
        list_for_each(node, &adev->out_streams) {
            struct imx_stream_out *stream = node_to_item(
                    node, struct imx_stream_out, stream_node);
            if ((stream->device == out->device) && (!stream->standby)) {
                if (first) {
                    first = 0;
                    ALOGI("%s: disable non-direct stream(%p) while " \
                            "other stream opened the same pcm device: %d.",
                            __func__, out, out->device);
                }
                usleep(2000);
                return -EBUSY;
            }
        }
    }
    update_passthrough_status();
    if (passthrough_enabled && !(out->flags & AUDIO_OUTPUT_FLAG_DIRECT)) {
        if (first) {
            first = 0;
            ALOGI("%s: disable non-direct stream(%p) while " \
                    "passthrough enabled", __func__, out);
        }
        usleep(2000);
        return -EBUSY;
    }
    first = 1;

    ALOGI("%s: primary: %d, out: %p, device: %d, address: %s, mode: %d, flags 0x%x", __func__,
            (out == adev->primary_output), out, out->device, out->address, adev->mode, out->flags);

    if (adev->mode != AUDIO_MODE_IN_CALL) {
        select_output_device(adev);
    }

    if (lpa_enable)
        flags |= PCM_LPA;
    if (out->format == AUDIO_FORMAT_DSD)
        flags |= PCM_FLAG_DSD;
    if ((out->flags & AUDIO_OUTPUT_FLAG_PRIMARY) &&
        ((out->device & AUDIO_DEVICE_OUT_AUX_DIGITAL) == 0))
        flags |= PCM_MMAP;

    if (out->device == AUDIO_DEVICE_OUT_BUS)
        card = get_card_for_bus(adev, out->address, NULL, &pcm_device_id);
    else if (out->format == AUDIO_FORMAT_DSD)
        card = get_card_for_dsd(adev, &out->card_index);
    else
        card = get_card_for_device(adev, out->device, PCM_OUT, &out->card_index);

    ALOGD("%s: pcm_open: card: %d, pcm_device_id: %d, rate: %d, channel: %d, format: %d, period_size: 0x%x, flag: %x",
          __func__, card, pcm_device_id, config->rate, config->channels, config->format, config->period_size, flags);

    if (card < 0) {
        ALOGE("%s: Invalid PCM card id: %d", __func__, card);
        return -EINVAL;
    }
    out->pcm = pcm_open(card, pcm_device_id, flags, config);
    out->write_flags = flags;
    out->first_frame_written = false;

    success = true;

    /* Close any PCMs that could not be opened properly and return an error */
    if (out->pcm && !pcm_is_ready(out->pcm)) {
        ALOGE("%s: pcm_open error: %s", __func__, pcm_get_error(out->pcm));
        if (out->pcm != NULL) {
            pcm_close(out->pcm);
            out->pcm = NULL;
        }
        success = false;
    }

    if (success) {
        if ((out->flags & AUDIO_OUTPUT_FLAG_PRIMARY) || (out->flags == AUDIO_OUTPUT_FLAG_NONE)) {
            out->buffer_frames = pcm_config_mm_out.period_size * 2;
            if (out->buffer == NULL)
                out->buffer = (char *)malloc(out->buffer_frames * audio_stream_out_frame_size((const struct audio_stream_out *)&out->stream.common));

            if (adev->echo_reference != NULL)
                out->echo_reference = adev->echo_reference;

            if (out->resampler)
                out->resampler->reset(out->resampler);
        }

        if (passthrough_for_s24) {
            out->buffer_frames = out->config.period_size *
                out->config.period_count;
            if (out->buffer == NULL)
                out->buffer = (char *)malloc(out->buffer_frames * \
                        audio_stream_out_frame_size((const struct audio_stream_out *)&out->stream.common));
        }

        return 0;
    }

    return -ENOMEM;
}

static int check_input_parameters(uint32_t sample_rate, audio_format_t format,
        audio_channel_mask_t channel_mask)
{
    return refine_input_parameters(&sample_rate, &format, &channel_mask);
}

static size_t get_input_buffer_size(uint32_t sample_rate, audio_format_t format,
        audio_channel_mask_t channel_mask)
{
    size_t size;
    int channel_count = popcount(channel_mask);
    if (check_input_parameters(sample_rate, format, channel_mask) != 0)
        return 0;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size = (pcm_config_mm_in.period_size * sample_rate) / pcm_config_mm_in.rate;
    size = ((size + 15) / 16) * 16;

    ALOGW("get_input_buffer_size size = %zu, channel_count = %d",size,channel_count);
    return size * channel_count * sizeof(short);
}

static void add_echo_reference(struct imx_stream_out *out,
                               struct echo_reference_itfe *reference)
{
    pthread_mutex_lock(&out->lock);
    out->echo_reference = reference;
    pthread_mutex_unlock(&out->lock);
}

static void remove_echo_reference(struct imx_stream_out *out,
                                  struct echo_reference_itfe *reference)
{
    pthread_mutex_lock(&out->lock);
    if (out->echo_reference == reference) {
        /* stop writing to echo reference */
        reference->write(reference, NULL);
        out->echo_reference = NULL;
    }
    pthread_mutex_unlock(&out->lock);
}

static void put_echo_reference(struct imx_audio_device *adev,
                          struct echo_reference_itfe *reference)
{
    if (adev->echo_reference != NULL &&
            reference == adev->echo_reference) {

        if (adev->primary_output != NULL &&
             !adev->primary_output->standby)
                remove_echo_reference(adev->primary_output, reference);
        release_echo_reference(reference);
        adev->echo_reference = NULL;
    }
}

static struct echo_reference_itfe *get_echo_reference(struct imx_audio_device *adev,
                                               audio_format_t format __unused,
                                               uint32_t channel_count,
                                               uint32_t sampling_rate)
{
    put_echo_reference(adev, adev->echo_reference);
    /*only for mixer output, only one output*/
    if (adev->primary_output != NULL &&
             !adev->primary_output->standby) {
        struct audio_stream *stream = &adev->primary_output->stream.common;
        uint32_t wr_channel_count = popcount(stream->get_channels(stream));
        uint32_t wr_sampling_rate = stream->get_sample_rate(stream);

        int status = create_echo_reference(AUDIO_FORMAT_PCM_16_BIT,
                                           channel_count,
                                           sampling_rate,
                                           AUDIO_FORMAT_PCM_16_BIT,
                                           wr_channel_count,
                                           wr_sampling_rate,
                                           &adev->echo_reference);
        if (status == 0)
            add_echo_reference(adev->primary_output, adev->echo_reference);
    }

    return adev->echo_reference;
}

static int get_playback_delay(struct imx_stream_out *out,
                       size_t frames,
                       struct echo_reference_buffer *buffer)
{
    unsigned int kernel_frames;
    int status;

    /* Find the first active PCM to act as primary */
    if (out->pcm) {
        status = pcm_get_htimestamp(out->pcm, &kernel_frames, &buffer->time_stamp);
        if (status < 0) {
            buffer->time_stamp.tv_sec  = 0;
            buffer->time_stamp.tv_nsec = 0;
            buffer->delay_ns           = 0;
            ALOGV("get_playback_delay(): pcm_get_htimestamp error,"
                    "setting playbackTimestamp to 0");
            return status;
        }

        kernel_frames = pcm_get_buffer_size(out->pcm) - kernel_frames;

        /* adjust render time stamp with delay added by current driver buffer.
         * Add the duration of current frame as we want the render time of the last
         * sample being written. */
        buffer->delay_ns = (long)(((int64_t)(kernel_frames + frames)* 1000000000)/
                out->sample_rate);

        return 0;
    }
    return -1;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;

    return out->sample_rate;
}

static int out_set_sample_rate(struct audio_stream *stream __unused, uint32_t rate)
{
    ALOGW("out_set_sample_rate %d", rate);
    return 0;
}

static size_t out_get_buffer_size(const struct audio_stream *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    int multiplier = 1;

    // In HAL, AUDIO_FORMAT_DSD doesn't have proportional frames, audio_stream_frame_size will return 1
    // But in driver, frame_size is 8 byte (DSD_FRAMESIZE_BYTES: 2 channel && 32 bit)
    if (out->format == AUDIO_FORMAT_DSD) {
        multiplier = DSD_FRAMESIZE_BYTES;
    }

    return out->config.period_size * multiplier *
                audio_stream_out_frame_size((const struct audio_stream_out *)stream);
}

static audio_channel_mask_t out_get_channels(const struct audio_stream *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    return out->channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    return out->format;
}

static int out_set_format(struct audio_stream *stream __unused, audio_format_t format)
{
    ALOGW("out_set_format %d", format);
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby(struct imx_stream_out *out, int force_standby)
{
    struct imx_audio_device *adev = out->dev;

    if ( (adev->mode == AUDIO_MODE_IN_CALL) || (adev->b_sco_rx_running == true) ||
        (!force_standby && out->card_index != -1 && !strcmp(adev->card_list[out->card_index]->driver_name, "wm8962-audio")) ) {
        ALOGW("no standby");
        return 0;
    }
    if (!out->standby) {

        if (out->pcm) {
            pcm_close(out->pcm);
            out->pcm = NULL;
        }

        out->writeContiFailCount = 0;

        ALOGW("do_out_standby... %p",out);

        /* stop writing to echo reference */
        if (out->echo_reference != NULL) {
            out->echo_reference->write(out->echo_reference, NULL);
            out->echo_reference = NULL;
        }

        /* Clear written for passthrough only */
        if (passthrough_enabled)
            out->written = 0;

        out->standby = 1;
    }
    return 0;
}

static int out_standby(struct audio_stream *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    int status;

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    status = do_output_standby(out, false);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);
    return status;
}

static int out_dump(const struct audio_stream *stream, int fd)
{
    if ((stream == NULL) || (fd < 0))
        return 0;

    struct imx_stream_out *out = (struct imx_stream_out *)stream;

    dprintf(fd, "audio write to HAL: rate %d, chns %d, audio format 0x%x\n", out->sample_rate, popcount(out->channel_mask), out->format);
    dprintf(fd, "audio write to ALSA: rate %d, chns %d, alsa format 0x%x\n", out->config.rate, out->config.channels, out->config.format);
    dprintf(fd, "device 0x%x, card index %d, frames round %d, total written %llu, first_frame_written %d, writeContiFailCount %d\n",
        out->device, out->card_index, out->frames_round, out->written, out->first_frame_written, out->writeContiFailCount);

    if (out->pcm)
      dprintf(fd, "pcm fd %d\n", pcm_get_poll_fd(out->pcm));

    return 0;
}

static int out_pause_dummy(struct audio_stream_out* stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;

    ALOGI("%s", __func__);

    if (!out->pcm || out->paused)
        return -ENODATA;
    else {
        out->paused = true;
        return 0;
    }
}

static int out_resume_dummy(struct audio_stream_out* stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;

    ALOGI("%s", __func__);

    if (out->paused) {
        out->paused = false;
        return 0;
    } else
        return -ENODATA;
}

#define PCM_IOCTL_PAUSE  1
#define PCM_IOCTL_RESUME 0
static int out_pause(struct audio_stream_out* stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    int status = 0;

    ALOGI("%s", __func__);

    pthread_mutex_lock(&out->lock);
    if ((!out->paused) && (out->pcm)) {
        status = pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_PAUSE, PCM_IOCTL_PAUSE);
        if (!status)
            out->paused = true;
    }
    pthread_mutex_unlock(&out->lock);

    return status;
}

static int out_resume(struct audio_stream_out* stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    int status = 0;

    ALOGI("%s", __func__);

    pthread_mutex_lock(&out->lock);
    if ((out->paused) && (out->pcm)) {
        status= pcm_ioctl(out->pcm, SNDRV_PCM_IOCTL_PAUSE, PCM_IOCTL_RESUME);
        if (!status)
            out->paused = false;
    }
    pthread_mutex_unlock(&out->lock);

    return status;
}
#undef PCM_IOCTL_PAUSE
#undef PCM_IOCTL_RESUME

static int out_flush(struct audio_stream_out* stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    struct imx_audio_device *adev = out->dev;
    unsigned int pcm_flags = PCM_OUT | PCM_MONOTONIC;
    int status = 0;

    ALOGI("%s", __func__);
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);

    out->written = 0;
    if(out->pcm) {
        pcm_close(out->pcm);
        out->pcm = NULL;
    } else {
        ALOGE("%s: no need flush as card not opened yet", __func__);
        goto exit;
    }

    if (out->format == AUDIO_FORMAT_DSD)
        pcm_flags |= PCM_FLAG_DSD;
    out->pcm = pcm_open(adev->card_list[out->card_index]->card, 0, pcm_flags, &out->config);
    if(out->pcm)
        out->standby = 0;

    out->paused = false;

exit:
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&adev->lock);

    return status;
}

static int out_set_parameters(struct audio_stream *stream __unused, const char *kvpairs)
{
    struct str_parms *parms;
    char value[32];
    int ret;
    int status = 0;

    ALOGD("%s: enter: kvpairs: %s", __func__, kvpairs);
    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        ALOGE("%s: Must not use set parameters API to set audio devices", __func__);
    }

    str_parms_destroy(parms);
    ALOGD("%s: exit: code(%d)", __func__, status);

    return status;
}

static bool stream_get_parameter_formats(struct str_parms *query,
                                         struct str_parms *reply,
                                         audio_format_t *supported_formats) {
    int ret = -1;
    char value[256];

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        ret = 0;
        value[0] = '\0';
        switch (supported_formats[0]) {
            case AUDIO_FORMAT_PCM_16_BIT:
                strcat(value, "AUDIO_FORMAT_PCM_16_BIT");
                break;
            case AUDIO_FORMAT_PCM_24_BIT_PACKED:
                strcat(value, "AUDIO_FORMAT_PCM_24_BIT_PACKED");
                break;
            case AUDIO_FORMAT_PCM_32_BIT:
                strcat(value, "AUDIO_FORMAT_PCM_32_BIT");
                break;
            case AUDIO_FORMAT_DSD:
                strcat(value, "AUDIO_FORMAT_DSD");
                break;
            default:
                ALOGE("%s: unsupported format %#x", __func__,
                      supported_formats[0]);
                break;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_FORMATS, value);
    }
    return ret >= 0;
}

static char * out_get_parameters(const struct audio_stream *stream, const char *keys)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;

    struct str_parms *query = str_parms_create_str(keys);
    char *str = NULL;
    char value[256];
    struct str_parms *reply = str_parms_create();
    size_t i, j;
    int ret;
    bool first = true;
    bool checked = false;
    char temp[10];

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        i = 0;
        while (out->sup_channel_masks[i] != 0) {
            for (j = 0; j < ARRAY_SIZE(out_channels_name_to_enum_table); j++) {
                if (out_channels_name_to_enum_table[j].value == out->sup_channel_masks[i]) {
                    if (!first) {
                        strcat(value, "|");
                    }
                    strcat(value, out_channels_name_to_enum_table[j].name);
                    first = false;
                    break;
                }
            }
            i++;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, value);
        checked = true;
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        i = 0;
        while (out->sup_rates[i] != 0) {
            if (!first) {
                strcat(value, "|");
            }
            sprintf(temp, "%d", out->sup_rates[i]);
            strcat(value, temp);
            first = false;
            i++;
        }
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, value);
        checked = true;
    }

    checked |= stream_get_parameter_formats(query, reply, &out->format);
    if (checked) {
        str = str_parms_to_str(reply);
    } else {
        str = strdup("");
    }

    ALOGW("out get parameters query %s, reply %s",str_parms_to_str(query), str_parms_to_str(reply));
    str_parms_destroy(query);
    str_parms_destroy(reply);
    return str;
}

static uint32_t out_get_latency(const struct audio_stream_out *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    uint32_t latency;

    latency = (out->config.period_count * out->config.period_size * 1000) /
              (out->config.rate);
    if (latency > LPA_LATENCY_MS) {
        ALOGD("%s: Original latency is %dms; Force it to be %dms for LPA", __func__, latency, LPA_LATENCY_MS);
        latency = LPA_LATENCY_MS;
    }
    return latency;
}

static int out_set_control_volume(struct mixer *mixer, struct route_setting *route, int left, int right)
{
    struct mixer_ctl *ctl;
    int i = 0;

    if (!mixer || !route) {
        ALOGE("%s: mixer or route_setting is NULL", __func__);
        return -EINVAL;
    }

    while (route[i].ctl_name) {
        ctl = mixer_get_ctl_by_name(mixer, route[i].ctl_name);
        if (!ctl)
            return -ENOSYS;
        mixer_ctl_set_value(ctl, 0, left);
        mixer_ctl_set_value(ctl, 1, right);
        i++;
    }
    return 0;
}

static int out_set_volume(struct audio_stream_out *stream, float left, float right)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    struct imx_audio_device *adev = out->dev;

    // Update out->card_index before start_output_stream
    if (out->card_index < 0){
        int card = -1;
        if (out->format == AUDIO_FORMAT_DSD) {
            card = get_card_for_dsd(adev, &out->card_index);
        } else
            get_card_for_device(adev, out->device, PCM_OUT, &out->card_index);
    }

    // If min/max volume is scaled to avoid volume jump after seek/pause/resume, ok to set volume.
    if (out->card_index >= 0 && out->card_index < adev->audio_card_num &&
        ((adev->card_list[out->card_index]->out_volume_min != OUT_VOL_MIN_DFT) ||
        (adev->card_list[out->card_index]->out_volume_max != OUT_VOL_MAX_DFT))) {
        struct route_setting *route = adev->card_list[out->card_index]->out_volume_ctl;
        struct mixer *mixer = adev->mixer[out->card_index];
        int volume[2];
        int out_vol_min = adev->card_list[out->card_index]->out_volume_min;
        int out_vol_max = adev->card_list[out->card_index]->out_volume_max;

        if (!mixer) {
            return -ENOSYS;
        }

        if (left == 0 && right == 0) {
            volume[0] = 0;
            volume[1] = 0;
        } else {
            // After factory reset, the original MUSIC stream volume is 5 (headphone)
            // Here left/right value is 0.023646, caculated volume is 172, which is just
            // the default volume defined in config_ak4458.h, this will avoid the
            // volume jump after seek/pause/resume operation.
            volume[0] = (int)(out_vol_min + left * (out_vol_max - out_vol_min));
            volume[1] = (int)(out_vol_min + right * (out_vol_max - out_vol_min));
        }

        out_set_control_volume(mixer, route, volume[0], volume[1]);
        ALOGD("%s: float: %f, integer: %d", __func__, left, volume[0]);

        return 0;
    }

    return -ENOSYS;
}

static int pcm_read_convert(struct imx_stream_in *in, struct pcm *pcm, void *data, unsigned int count)
{
    bool bit_24b_2_16b = false;
    bool bit_32b_2_16b = false;
    bool mono2stereo = false;
    bool stereo2mono = false;
    size_t frames_rq = count / audio_stream_in_frame_size((const struct audio_stream_in *)&in->stream.common);

    if (in->config.format == PCM_FORMAT_S24_LE && in->requested_format == AUDIO_FORMAT_PCM_16_BIT) bit_24b_2_16b = true;
    if (in->config.format == PCM_FORMAT_S32_LE && in->requested_format == AUDIO_FORMAT_PCM_16_BIT) bit_32b_2_16b = true;
    if (in->config.channels == 2 && in->requested_channel == 1) stereo2mono = true;
    if (in->config.channels == 1 && in->requested_channel == 2) mono2stereo = true;

    if (bit_24b_2_16b || bit_32b_2_16b ||  mono2stereo || stereo2mono) {
        size_t size_in_bytes_tmp = pcm_frames_to_bytes(in->pcm, frames_rq);
        if (in->read_tmp_buf_size < in->config.period_size) {
            in->read_tmp_buf_size = in->config.period_size;
            in->read_tmp_buf = (int32_t *) realloc(in->read_tmp_buf, size_in_bytes_tmp);
            ALOG_ASSERT((in->read_tmp_buf != NULL),
                        "get_next_buffer() failed to reallocate read_tmp_buf");
            ALOGV("get_next_buffer(): read_tmp_buf %p extended to %zu bytes",
                     in->read_tmp_buf, size_in_bytes_tmp);
        }

        in->read_status = pcm_read_wrapper(pcm, (void*)in->read_tmp_buf, size_in_bytes_tmp, in->dump);

        if (in->read_status != 0) {
            ALOGE("get_next_buffer() pcm_read_wrapper error %d", in->read_status);
            return in->read_status;
        }

        convert_record_data((void *)in->read_tmp_buf, (void *)data, frames_rq, bit_24b_2_16b, bit_32b_2_16b, mono2stereo, stereo2mono);
    }
    else {
        in->read_status = pcm_read_wrapper(pcm, (void*)data, count, in->dump);
    }

    if ((in->first_frame_read == false) && (in->read_status == 0)) {
        ALOGI("%s: first frame read, in stream %p", __func__, in);
        in->first_frame_read = true;
    }

    return in->read_status;
}

#define IN_SRC_FILE "/data/in_src.pcm"
#define IN_DST_FILE "/data/in_dst.pcm"
#define OUT_SRC_FILE "/data/out_src.pcm"
#define OUT_DST_FILE "/data/out_dst.pcm"

static void audio_dump(const void *buffer, size_t bytes, char *name)
{
    if ((buffer == NULL) || (bytes == 0) || (name == NULL))
        return;

    int fdDump = open(name, O_CREAT|O_APPEND|O_WRONLY, S_IRWXU|S_IRWXG);
    if (fdDump < 0) {
        ALOGW("%s: file open error, srcFile: %s, fd %d",
                __func__, name, fdDump);
        return;
    }

    write(fdDump, buffer, bytes);
    close(fdDump);

    return;
}

static int pcm_read_wrapper(struct pcm *pcm, const void * buffer, size_t bytes, bool dump)
{
    int ret = 0;
    ret = pcm_read(pcm, (void *)buffer, bytes);

    if(ret !=0) {
         ALOGV("ret %d, pcm read %zu error %s.", ret, bytes, pcm_get_error(pcm));

         switch(pcm_state(pcm)) {
              case PCM_STATE_SETUP:
              case PCM_STATE_XRUN:
                   ret = pcm_prepare(pcm);
                   if(ret != 0) return ret;
                   break;
              default:
                   return ret;
         }

         ret = pcm_read(pcm, (void *)buffer, bytes);
    }

    if (dump)
        audio_dump(buffer, bytes, IN_SRC_FILE);

    return ret;
}

static int pcm_write_wrapper(struct pcm *pcm, const void * buffer, size_t bytes, int flags, bool dump)
{
    int ret = 0;

    if(flags & PCM_MMAP)
         ret = pcm_mmap_write(pcm, (void *)buffer, bytes);
    else
         ret = pcm_write(pcm, (void *)buffer, bytes);

    if(ret !=0) {
         ALOGW("ret %d, pcm write %zu error %s", ret, bytes, pcm_get_error(pcm));
        if (lpa_enable == 1)
            return ret;

         switch(pcm_state(pcm)) {
              case PCM_STATE_SETUP:
              case PCM_STATE_XRUN:
                   ret = pcm_prepare(pcm);
                   if(ret != 0) return ret;
                   break;
              default:
                   return ret;
         }

         if(flags & PCM_MMAP)
            ret = pcm_mmap_write(pcm, (void *)buffer, bytes);
         else
            ret = pcm_write(pcm, (void *)buffer, bytes);
    }

    if (dump)
        audio_dump(buffer, bytes, OUT_DST_FILE);

    return ret;
}

static ssize_t out_write(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret = 0;
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    struct imx_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_out_frame_size(stream);
    size_t in_frames = bytes / frame_size;
    size_t out_frames = in_frames;
    bool force_input_standby = false;
    struct imx_stream_in *in;

    if (out->dump)
        audio_dump(buffer, bytes, OUT_SRC_FILE);

    // In HAL, AUDIO_FORMAT_DSD doesn't have proportional frames, audio_stream_out_frame_size will return 1
    // But in driver, frame_size is 8 byte (DSD_FRAMESIZE_BYTES: 2 channel && 32 bit)
    if (out->format == AUDIO_FORMAT_DSD)
        frame_size *= DSD_FRAMESIZE_BYTES;

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);

    if((adev->b_sco_rx_running) && (out == adev->primary_output))
        ALOGW("out_write, bt receive task is running");

    if (out->standby) {
        ret = start_output_stream(out);
        if (ret != 0) {
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
        out->standby = 0;
        /* a change in output device may change the microphone selection */
        if (adev->active_input &&
                adev->active_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION)
            force_input_standby = true;
    }
    pthread_mutex_unlock(&adev->lock);

    // bt-sco card only supports mono channel, but mixer can not support mono channel
    // so we set it as stereo channel in audio policy, then convert it to mono channel in HAL
    if ((out->device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET) ||
            (out->device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT) ||
            (out->device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO)) {
        convert_record_data((void *)buffer, (void *)(out->buffer), out_frames, false, false, false, true);
        frame_size = frame_size / 2;
    }

    if (out->echo_reference != NULL) {
        struct echo_reference_buffer b;
        b.raw = (void *)buffer;
        b.frame_count = in_frames;

        get_playback_delay(out, out_frames, &b);
        out->echo_reference->write(out->echo_reference, &b);
    }

    if (out->config.channels > 2)
        convert_output_for_esai(buffer, bytes, out->config.channels);

    if (passthrough_for_s24) {
        convert_passthrough_data_for_s24((void *)buffer, (void *)(out->buffer),
                out_frames);
    }

    if (out->pcm) {
        if (((out->flags & AUDIO_OUTPUT_FLAG_PRIMARY) || (out->flags == AUDIO_OUTPUT_FLAG_NONE)) &&
                (out->config.rate != DEFAULT_OUTPUT_SAMPLE_RATE)) {
            /* PCM resampled or channel converted.
               For bt-sai HSP case, stereo buffer converted to mono out->buffer */
            ret = pcm_write_wrapper(out->pcm, (void *)out->buffer, out_frames * frame_size, out->write_flags, out->dump);
        } else if (passthrough_for_s24) {
            /* For 8mp passthrough case, audio data is converted from
               PCM_FORMAT_S16_LE buffer to PCM_FORMAT_S24_LE out->buffer,
               so here double the bytes to write */
            ret = pcm_write_wrapper(out->pcm, (void *)out->buffer,
                    out_frames * frame_size * 2, out->write_flags, out->dump);
        } else {
            /* PCM uses native sample rate */
            ret = pcm_write_wrapper(out->pcm, (void *)buffer, bytes, out->write_flags, out->dump);
        }

        if (ret) {
            out->writeContiFailCount++;
        } else {
            out->writeContiFailCount = 0;

          if (out->first_frame_written == false) {
              ALOGI("%s: first frame write, out stream %p", __func__, out);
              out->first_frame_written = true;
          }
        }
    }

    //If continue fail, probably th fd is invalid.
    if ((out->flags & AUDIO_OUTPUT_FLAG_PRIMARY) && (out->writeContiFailCount > 100)) {
        ALOGW("pcm_write_wrapper continues failed for pcm, standby");
        do_output_standby(out, true);
    }

exit:
    size_t write_frames = bytes / frame_size;
    out->written += bytes / frame_size;
    pthread_mutex_unlock(&out->lock);

    out->frames_round += write_frames;
    if (out->frames_round >= out->sample_rate) {
        ALOGV("out frames_round %d", out->frames_round);
        out->frames_round -= out->sample_rate;
        out->dump = property_get_bool("vendor.audio.dump", false);
    }

    if (ret != 0) {
        ALOGV("write error, sleep few ms");
        usleep(bytes * 1000000 / frame_size / out_get_sample_rate(&stream->common));
    }

    if (force_input_standby) {
        pthread_mutex_lock(&adev->lock);
        if (adev->active_input) {
            in = adev->active_input;
            pthread_mutex_lock(&in->lock);
            do_input_standby(in);
            pthread_mutex_unlock(&in->lock);
        }
        pthread_mutex_unlock(&adev->lock);
    }
    return bytes;
}

/********************************************************************************************************
For esai, it will use the first channels/2 for Left channels, use second half channels for Right channels.
So we need to transform channel map in HAL for multichannel.
when input is "FL, FR, C, LFE, BL, BR, SL, SR", output is "FL,BL,C,FR,BR,LFE".
when input is "FL, FR, C, LFE, BL, BR, SL, SR", output is "FL,BL,C,SL,FR,BR,LFE,SR"
*********************************************************************************************************/
static void convert_output_for_esai(const void* buffer, size_t bytes, int channels)
{
    short *data_src = (short *)buffer;
    short *data_dst = (short *)buffer;
    short dataFL,dataFR,dataC,dataLFE,dataBL,dataBR,dataSL,dataSR;
    int i;

    if (channels == 6) {
        for (i = 0; i < (int)bytes/(2*channels); i++ ) {
           dataFL  = *data_src++;
           dataFR  = *data_src++;
           dataC   = *data_src++;
           dataLFE = *data_src++;
           dataBL  = *data_src++;
           dataBR  = *data_src++;
           *data_dst++ = dataFL;
           *data_dst++ = dataBL;
           *data_dst++ = dataC;
           *data_dst++ = dataFR;
           *data_dst++ = dataBR;
           *data_dst++ = dataLFE;
        }
    }
    else if (channels == 8) {
        for (i = 0; i < (int)bytes/(2*channels); i++ ) {
           dataFL  = *data_src++;
           dataFR  = *data_src++;
           dataC   = *data_src++;
           dataLFE = *data_src++;
           dataBL  = *data_src++;
           dataBR  = *data_src++;
           dataSL  = *data_src++;
           dataSR  = *data_src++;
           *data_dst++ = dataFL;
           *data_dst++ = dataBL;
           *data_dst++ = dataC;
           *data_dst++ = dataSL;
           *data_dst++ = dataFR;
           *data_dst++ = dataBR;
           *data_dst++ = dataLFE;
           *data_dst++ = dataSR;
        }
    }
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    struct timespec timestamp;
    int64_t signed_frames = 0;
    pthread_mutex_lock(&out->lock);

    if (out->pcm) {
        unsigned int avail;
        size_t kernel_buffer_size = out->config.period_size * out->config.period_count;
        // this is the number of frames which the dsp actually presented at least
        signed_frames = out->written - kernel_buffer_size;
        if (pcm_get_htimestamp(out->pcm, &avail, &timestamp) == 0) {
            // compensate for driver's frames consumed
            signed_frames += avail;
        }
    }
   if (signed_frames >= 0)
       *dsp_frames = (uint32_t)signed_frames;
   else
       *dsp_frames = 0;

    pthread_mutex_unlock(&out->lock);

    return 0;
}

static int out_add_audio_effect(const struct audio_stream *stream __unused, effect_handle_t effect __unused)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream __unused, effect_handle_t effect __unused)
{
    return 0;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
                                   uint64_t *frames, struct timespec *timestamp)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    int ret = -ENODATA;

    pthread_mutex_lock(&out->lock);

    if (out->pcm) {
        unsigned int avail;
        if (pcm_get_htimestamp(out->pcm, &avail, timestamp) == 0) {
            size_t kernel_buffer_size = out->config.period_size * out->config.period_count;
            int64_t signed_frames;
            if (out->format == AUDIO_FORMAT_DSD)
                // In AudioFlinger, frame_size is 1/4 byte(2 channel && 1 bit), here it is 8 byte(2 channel && 32 bit)
                signed_frames = (out->written - kernel_buffer_size + avail) * DSD_RATE_TO_PCM_RATE;
            else
                signed_frames = out->written - (kernel_buffer_size - avail);
            ALOGV("%s: avail: %u kernel_buffer_size: %zu written: %lld signed_frames: %ld", __func__, avail, kernel_buffer_size, (long long)out->written, (long)signed_frames);

            if (signed_frames >= 0) {
                *frames = signed_frames;
                ret = 0;
            }
        }
    }

    pthread_mutex_unlock(&out->lock);

    return ret;
}

/** audio_stream_in implementation **/
#define MID_RATE_0_8000  4000
#define MID_RATE_8000_11025  ((8000+11025)/2)
#define MID_RATE_11025_16000 ((11025+16000)/2)
#define MID_RATE_16000_22050  ((16000+22050)/2)
#define MID_RATE_22050_32000  ((22050+32000)/2)
#define MID_RATE_32000_44100  ((32000+44100)/2)
#define MID_RATE_44100_48000  ((44100+48000)/2)
#define MID_RATE_48000_64000  ((48000+64000)/2)
#define MID_RATE_64000_88200  ((64000+88200)/2)
#define MID_RATE_88200_96000  ((88200+96000)/2)
#define MID_RATE_96000_176400  ((96000+176400)/2)
#define MID_RATE_176400_196000  ((176400+196000)/2)

static int spdif_in_rate_check(struct imx_stream_in *in)
{
    struct imx_audio_device *adev = in->dev;
    int i = in->card_index;
    int ret = 0;

    if(!strcmp(adev->card_list[i]->driver_name, "imx-spdif")) {
        struct mixer_ctl *ctl;
        unsigned int rate = 0;
        struct mixer *mixer_spdif = adev->mixer[i];
        ctl = mixer_get_ctl_by_name(mixer_spdif, "RX Sample Rate");
        if (ctl) {
            rate = mixer_ctl_get_value(ctl, 0);
        }

	if (rate <= MID_RATE_0_8000)
		rate = 0;
	else if (MID_RATE_0_8000 < rate && rate <= MID_RATE_8000_11025)
		rate = 8000;
	else if (MID_RATE_8000_11025 < rate && rate <= MID_RATE_11025_16000)
		rate = 11025;
	else if (MID_RATE_11025_16000 < rate && rate <= MID_RATE_16000_22050)
		rate = 16000;
	else if (MID_RATE_16000_22050 < rate && rate <= MID_RATE_22050_32000)
		rate = 22050;
	else if (MID_RATE_22050_32000 < rate && rate <= MID_RATE_32000_44100)
		rate = 32000;
	else if (MID_RATE_32000_44100 < rate && rate <= MID_RATE_44100_48000)
		rate = 44100;
	else if (MID_RATE_44100_48000 < rate && rate <= MID_RATE_48000_64000)
		rate = 48000;
	else if (MID_RATE_48000_64000 < rate && rate <= MID_RATE_64000_88200)
		rate = 64000;
	else if (MID_RATE_64000_88200 < rate && rate <= MID_RATE_88200_96000)
		rate = 88200;
	else if (MID_RATE_88200_96000 < rate && rate <= MID_RATE_96000_176400)
		rate = 96000;
	else if (MID_RATE_96000_176400 < rate && rate <= MID_RATE_176400_196000)
		rate = 176400;
	else
		rate = 196000;

	if (rate > 0 && rate != in->config.rate) {
            in->config.rate = rate;
            ALOGW("spdif input rate changed to %d", rate);

            if (in->resampler) {
                release_resampler(in->resampler);
            }

            if (in->requested_rate != in->config.rate) {
                in->buf_provider.get_next_buffer = get_next_buffer;
                in->buf_provider.release_buffer = release_buffer;

                ret = create_resampler(in->config.rate,
                               in->requested_rate,
                               in->requested_channel,
                               RESAMPLER_QUALITY_DEFAULT,
                               &in->buf_provider,
                               &in->resampler);
            }

            /* if no supported sample rate is available, use the resampler */
            if (in->resampler) {
                in->resampler->reset(in->resampler);
            }
        }
    }

    return 0;
}

/* must be called with hw device and input stream mutexes locked */
static int start_input_stream(struct imx_stream_in *in)
{
    int ret = 0;
    int i;
    struct imx_audio_device *adev = in->dev;
    int card = -1;
    unsigned int port = 0;

    // If input device is opened by HFP thread, just return error here
    if (adev->b_sco_tx_running) {
        usleep(2000);
        return -EBUSY;
    }
    ALOGW("start_input_stream...., mode %d, in->device 0x%x", adev->mode, in->device);

    adev->active_input = in;
    if (adev->mode != AUDIO_MODE_IN_CALL) {
        adev->in_device = in->device;
        select_input_device(adev);
    } else {
        adev->in_device = in->device;
    }

    for(i = 0; i < adev->audio_card_num; i++) {
        if(adev->in_device & adev->card_list[i]->supported_in_devices) {
            card = adev->card_list[i]->card;
            in->card_index = i;
            port = 0;
            break;
        }
        if(i == adev->audio_card_num-1) {
            ALOGE("can not find supported device for %d",in->device);
            return -EINVAL;
        }
    }

    /*Error handler for usb mic plug in/plug out when recording. */
    memcpy(&in->config, &pcm_config_mm_in, sizeof(pcm_config_mm_in));

    if(in->device == (AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET & ~AUDIO_DEVICE_BIT_IN)) {
        in->config.rate = g_hsp_sample_rate;
        in->config.channels = g_hsp_chns;
    }

    in->config.stop_threshold = in->config.period_size * in->config.period_count;
    in->config.format = (enum pcm_format)adev_get_format_for_device(adev, in->device, PCM_IN);

    ALOGW("card %d, port %d device 0x%x", card, port, in->device);
    ALOGW("rate %d, channel %d format %d, period_size 0x%x", in->config.rate, in->config.channels, 
                                 in->config.format, in->config.period_size);

    if (in->need_echo_reference && in->echo_reference == NULL)
        in->echo_reference = get_echo_reference(adev,
                                        AUDIO_FORMAT_PCM_16_BIT,
                                        in->requested_channel,
                                        in->requested_rate);

    /* this assumes routing is done previously */
    in->pcm = pcm_open(card, port, PCM_IN | PCM_MONOTONIC, &in->config);
    if (!pcm_is_ready(in->pcm)) {
        ALOGE("cannot open pcm_in driver: %s", pcm_get_error(in->pcm));
        pcm_close(in->pcm);
        adev->active_input = NULL;
        return -ENOMEM;
    }

    in->read_buf_frames = 0;
    in->read_buf_size   = 0;
    in->proc_buf_frames = 0;
    in->proc_buf_size = 0;

    if (in->resampler) {
        release_resampler(in->resampler);
    }
    if (in->requested_rate != in->config.rate) {
        in->buf_provider.get_next_buffer = get_next_buffer;
        in->buf_provider.release_buffer = release_buffer;

        ret = create_resampler(in->config.rate,
                               in->requested_rate,
                               in->requested_channel,
                               RESAMPLER_QUALITY_DEFAULT,
                               &in->buf_provider,
                               &in->resampler);
    }

    /* if no supported sample rate is available, use the resampler */
    if (in->resampler) {
        in->resampler->reset(in->resampler);
    }
    return 0;
}

static uint32_t in_get_sample_rate(const struct audio_stream *stream)
{
    struct imx_stream_in *in = (struct imx_stream_in *)stream;

    return in->requested_rate;
}

static int in_set_sample_rate(struct audio_stream *stream __unused, uint32_t rate __unused)
{
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct imx_stream_in *in = (struct imx_stream_in *)stream;

    return get_input_buffer_size(in->requested_rate,
                                 in->requested_format,
                                 in->requested_channel_mask);
}

static audio_channel_mask_t in_get_channels(const struct audio_stream *stream)
{
    struct imx_stream_in *in = (struct imx_stream_in *)stream;

    if (in->requested_channel == 1) {
        return AUDIO_CHANNEL_IN_MONO;
    } else {
        return AUDIO_CHANNEL_IN_STEREO;
    }
}

static audio_format_t in_get_format(const struct audio_stream *stream)
{
    struct imx_stream_in *in = (struct imx_stream_in *)stream;
    return in->requested_format;
}

static int in_set_format(struct audio_stream *stream __unused, audio_format_t format __unused)
{
    return 0;
}

/* must be called with hw device and input stream mutexes locked */
static int do_input_standby(struct imx_stream_in *in)
{
    struct imx_audio_device *adev = in->dev;

    if (!in->standby) {
        ALOGW("do_in_standby..");
        pcm_close(in->pcm);
        in->pcm = NULL;

        adev->active_input = 0;
        if (adev->mode != AUDIO_MODE_IN_CALL) {
            adev->in_device = AUDIO_DEVICE_NONE;
            select_input_device(adev);
        }

        if (in->echo_reference != NULL) {
            /* stop reading from echo reference */
            in->echo_reference->read(in->echo_reference, NULL);
            put_echo_reference(adev, in->echo_reference);
            in->echo_reference = NULL;
        }

        in->standby = 1;
    }
    return 0;
}

static int in_standby(struct audio_stream *stream)
{
    struct imx_stream_in *in = (struct imx_stream_in *)stream;
    int status;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    status = do_input_standby(in);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_dump(const struct audio_stream *stream, int fd)
{
    if ((stream == NULL) || (fd < 0))
        return 0;

    struct imx_stream_in *in = (struct imx_stream_in *)stream;

    dprintf(fd, "audio read from HAL: rate %d, chns %d, audio format 0x%x\n", in->requested_rate, popcount(in->requested_channel), in->requested_format);
    dprintf(fd, "audio read from ALSA: rate %d, chns %d, alsa format 0x%x\n", in->config.rate, in->config.channels, in->config.format);
    dprintf(fd, "device 0x%x, card index %d, frames round %d, first_frame_read %d, read_status %d\n",
        in->device, in->card_index, in->frames_round, in->first_frame_read, in->read_status);

    if (in->pcm)
      dprintf(fd, "pcm fd %d\n", pcm_get_poll_fd(in->pcm));

    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct imx_stream_in *in = (struct imx_stream_in *)stream;
    struct imx_audio_device *adev = in->dev;
    struct str_parms *parms;
    char value[32];
    int ret, val = 0;
    bool do_standby = false;
    int status = 0;

    ALOGD("%s: enter: kvpairs=%s", __func__, kvpairs);
    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_INPUT_SOURCE, value, sizeof(value));

    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (ret >= 0) {
        val = atoi(value);
        /* no audio source uses val == 0 */
        if ((in->source != val) && (val != 0)) {
            in->source = val;
            do_standby = true;
        }
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        ALOGE("%s: Must not use set parameters API to set audio devices", __func__);
    }

    if (do_standby)
        do_input_standby(in);

    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&adev->lock);

    str_parms_destroy(parms);
    ALOGD("%s: exit: status(%d)", __func__, status);
    return status;
}

static char * in_get_parameters(const struct audio_stream *stream __unused,
                                const char *keys)
{
    struct str_parms *query = str_parms_create_str(keys);
    char *str = NULL;
    char value[256];
    struct str_parms *reply = str_parms_create();
    int ret;
    bool checked = false;

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS, value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        strcat(value, "AUDIO_FORMAT_PCM_16_BIT");
        str_parms_add_str(reply, AUDIO_PARAMETER_STREAM_SUP_FORMATS, value);
        str = strdup(str_parms_to_str(reply));
        checked = true;
    }

    if (!checked) {
        str = strdup("");
    }

    ALOGD("%s: query %s; reply %s", __func__, str_parms_to_str(query), str_parms_to_str(reply));
    str_parms_destroy(query);
    str_parms_destroy(reply);

    return str;
}

static int in_set_gain(struct audio_stream_in *stream __unused, float gain __unused)
{
    return 0;
}

static void get_capture_delay(struct imx_stream_in *in,
                       size_t frames,
                       struct echo_reference_buffer *buffer)
{

    /* read frames available in kernel driver buffer */
    unsigned int kernel_frames;
    struct timespec tstamp;
    long buf_delay;
    long rsmp_delay;
    long kernel_delay;
    long delay_ns;

    if (pcm_get_htimestamp(in->pcm, &kernel_frames, &tstamp) < 0) {
        buffer->time_stamp.tv_sec  = 0;
        buffer->time_stamp.tv_nsec = 0;
        buffer->delay_ns           = 0;
        ALOGW("read get_capture_delay(): pcm_htimestamp error");
        return;
    }

    /* read frames available in audio HAL input buffer
     * add number of frames being read as we want the capture time of first sample
     * in current buffer */
    buf_delay = (long)(((int64_t)(in->read_buf_frames) * 1000000000) / in->config.rate +
                       ((int64_t)(in->proc_buf_frames) * 1000000000) /
                           in->requested_rate);

    /* add delay introduced by resampler */
    rsmp_delay = 0;
    if (in->resampler) {
        rsmp_delay = in->resampler->delay_ns(in->resampler);
    }

    kernel_delay = (long)(((int64_t)kernel_frames * 1000000000) / in->config.rate);

    delay_ns = kernel_delay + buf_delay + rsmp_delay;

    buffer->time_stamp = tstamp;
    buffer->delay_ns   = delay_ns;
    ALOGV("get_capture_delay time_stamp = [%ld].[%ld], delay_ns: [%d],"
         " kernel_delay:[%ld], buf_delay:[%ld], rsmp_delay:[%ld], kernel_frames:[%u], "
         "in->read_buf_frames:[%zu], in->proc_buf_frames:[%zu], frames:[%zu]",
         buffer->time_stamp.tv_sec , buffer->time_stamp.tv_nsec, buffer->delay_ns,
         kernel_delay, buf_delay, rsmp_delay, kernel_frames,
         in->read_buf_frames, in->proc_buf_frames, frames);

}

static int32_t update_echo_reference(struct imx_stream_in *in, size_t frames)
{
    struct echo_reference_buffer b;
    b.delay_ns = 0;

    ALOGV("update_echo_reference, frames = [%zu], in->ref_frames_in = [%zu],  "
          "b.frame_count = [%zu]",
         frames, in->ref_frames_in, frames - in->ref_frames_in);
    if (in->ref_frames_in < frames) {
        if (in->ref_buf_size < frames) {
            in->ref_buf_size = frames;
            in->ref_buf = (int16_t *)realloc(in->ref_buf,
                                             in->ref_buf_size *
                                                 in->requested_channel * sizeof(int16_t));
        }

        b.frame_count = frames - in->ref_frames_in;
        b.raw = (void *)(in->ref_buf + in->ref_frames_in * in->requested_channel);

        get_capture_delay(in, frames, &b);

        if (in->echo_reference->read(in->echo_reference, &b) == 0)
        {
            in->ref_frames_in += b.frame_count;
            ALOGV("update_echo_reference: in->ref_frames_in:[%zu], "
                    "in->ref_buf_size:[%zu], frames:[%zu], b.frame_count:[%zu]",
                 in->ref_frames_in, in->ref_buf_size, frames, b.frame_count);
        }
    } else
        ALOGV("update_echo_reference: NOT enough frames to read ref buffer");
    return b.delay_ns;
}

static int set_preprocessor_param(effect_handle_t handle,
                           effect_param_t *param)
{
    uint32_t size = sizeof(int);
    uint32_t psize = ((param->psize - 1) / sizeof(int) + 1) * sizeof(int) +
                        param->vsize;

    int status = (*handle)->command(handle,
                                   EFFECT_CMD_SET_PARAM,
                                   sizeof (effect_param_t) + psize,
                                   param,
                                   &size,
                                   &param->status);
    if (status == 0)
        status = param->status;

    return status;
}

static int set_preprocessor_echo_delay(effect_handle_t handle,
                                     int32_t delay_us)
{
    uint32_t buf[sizeof(effect_param_t) / sizeof(uint32_t) + 2];
    effect_param_t *param = (effect_param_t *)buf;

    param->psize = sizeof(uint32_t);
    param->vsize = sizeof(uint32_t);
    *(uint32_t *)param->data = AEC_PARAM_ECHO_DELAY;
    *((int32_t *)param->data + 1) = delay_us;

    return set_preprocessor_param(handle, param);
}

static void push_echo_reference(struct imx_stream_in *in, size_t frames)
{
    /* read frames from echo reference buffer and update echo delay
     * in->ref_frames_in is updated with frames available in in->ref_buf */
    int32_t delay_us = update_echo_reference(in, frames)/1000;
    int i;
    audio_buffer_t buf;

    if (in->ref_frames_in < frames)
        frames = in->ref_frames_in;

    buf.frameCount = frames;
    buf.raw = in->ref_buf;

    for (i = 0; i < in->num_preprocessors; i++) {
        if ((*in->preprocessors[i].effect_itfe)->process_reverse == NULL)
            continue;

        (*in->preprocessors[i].effect_itfe)->process_reverse(in->preprocessors[i].effect_itfe,
                                               &buf,
                                               NULL);
        set_preprocessor_echo_delay(in->preprocessors[i].effect_itfe, delay_us);
    }

    in->ref_frames_in -= buf.frameCount;
    if (in->ref_frames_in) {
        memcpy(in->ref_buf,
               in->ref_buf + buf.frameCount * in->requested_channel,
               in->ref_frames_in * in->requested_channel * sizeof(int16_t));
    }
}

static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                                   struct resampler_buffer* buffer)
{
    struct imx_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return -EINVAL;

    in = (struct imx_stream_in *)((char *)buffer_provider -
                                   offsetof(struct imx_stream_in, buf_provider));

    if (in->pcm == NULL) {
        buffer->raw = NULL;
        buffer->frame_count = 0;
        in->read_status = -ENODEV;
        return -ENODEV;
    }

    if (in->read_buf_frames == 0) {
        size_t size_in_bytes = in->config.period_size * audio_stream_in_frame_size((const struct audio_stream_in *)&in->stream.common);
        if (in->read_buf_size < in->config.period_size) {
            in->read_buf_size = in->config.period_size;
            in->read_buf = (int16_t *) realloc(in->read_buf, size_in_bytes);
            ALOG_ASSERT((in->read_buf != NULL),
                        "get_next_buffer() failed to reallocate read_buf");
            ALOGV("get_next_buffer(): read_buf %p extended to %zu bytes",
                  in->read_buf, size_in_bytes);
        }

        in->read_status = pcm_read_convert(in, in->pcm, (void*)in->read_buf, size_in_bytes);

        if (in->read_status != 0) {
            ALOGE("get_next_buffer() pcm_read_convert error %d", in->read_status);
            buffer->raw = NULL;
            buffer->frame_count = 0;
            return in->read_status;
        }
        in->read_buf_frames = in->config.period_size;
    }

    buffer->frame_count = (buffer->frame_count > in->read_buf_frames) ?
                                in->read_buf_frames : buffer->frame_count;
    buffer->i16 = in->read_buf + (in->config.period_size - in->read_buf_frames) *
                                                in->requested_channel;

    return in->read_status;

}

static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                                  struct resampler_buffer* buffer)
{
    struct imx_stream_in *in;

    if (buffer_provider == NULL || buffer == NULL)
        return;

    in = (struct imx_stream_in *)((char *)buffer_provider -
                                   offsetof(struct imx_stream_in, buf_provider));

    in->read_buf_frames -= buffer->frame_count;
}

/* read_frames() reads frames from kernel driver, down samples to capture rate
 * if necessary and output the number of frames requested to the buffer specified */
static ssize_t read_frames(struct imx_stream_in *in, void *buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;

    while (frames_wr < frames) {
        size_t frames_rd = frames - frames_wr;
        if (in->resampler != NULL) {
            in->resampler->resample_from_provider(in->resampler,
                    (int16_t *)((char *)buffer +
                            frames_wr * audio_stream_in_frame_size((const struct audio_stream_in *)&in->stream.common)),
                    &frames_rd);
        } else {
            struct resampler_buffer buf = {
                    { .raw = NULL, },
                    .frame_count = frames_rd,
            };
            get_next_buffer(&in->buf_provider, &buf);
            if (buf.raw != NULL) {
                memcpy((char *)buffer +
                           frames_wr * audio_stream_in_frame_size((const struct audio_stream_in *)&in->stream.common),
                        buf.raw,
                        buf.frame_count * audio_stream_in_frame_size((const struct audio_stream_in *)&in->stream.common));
                frames_rd = buf.frame_count;
            }
            release_buffer(&in->buf_provider, &buf);
        }
        /* in->read_status is updated by getNextBuffer() also called by
         * in->resampler->resample_from_provider() */
        if (in->read_status != 0)
            return in->read_status;

        frames_wr += frames_rd;
    }
    return frames_wr;
}

/* process_frames() reads frames from kernel driver (via read_frames()),
 * calls the active audio pre processings and output the number of frames requested
 * to the buffer specified */
static ssize_t process_frames(struct imx_stream_in *in, void* buffer, ssize_t frames)
{
    ssize_t frames_wr = 0;
    audio_buffer_t in_buf;
    audio_buffer_t out_buf;
    int i;
    bool has_aux_channels = (~in->main_channels & in->aux_channels);
    void *proc_buf_out;

    if (has_aux_channels)
        proc_buf_out = in->proc_buf_out;
    else
        proc_buf_out = buffer;

    /* since all the processing below is done in frames and using the config.channels
     * as the number of channels, no changes is required in case aux_channels are present */
    while (frames_wr < frames) {
        /* first reload enough frames at the end of process input buffer */
        if (in->proc_buf_frames < (size_t)frames) {
            ssize_t frames_rd;

            if (in->proc_buf_size < (size_t)frames) {
                in->proc_buf_size = (size_t)frames;
                in->proc_buf_in = (int16_t *)realloc(in->proc_buf_in,
                                         in->proc_buf_size * in->requested_channel * sizeof(int16_t));

                ALOG_ASSERT((in->proc_buf_in != NULL),
                            "process_frames() failed to reallocate proc_buf_in");
                if (has_aux_channels) {
                    in->proc_buf_out = (int16_t *)realloc(in->proc_buf_out, in->proc_buf_size * in->requested_channel * sizeof(int16_t));
                    ALOG_ASSERT((in->proc_buf_out != NULL),
                                "process_frames() failed to reallocate proc_buf_out");
                    proc_buf_out = in->proc_buf_out;
                }
                ALOGV("process_frames(): proc_buf_in %p extended to %zu bytes",
                     in->proc_buf_in, in->proc_buf_size * in->requested_channel * sizeof(int16_t));
            }
            frames_rd = read_frames(in,
                                    in->proc_buf_in +
                                        in->proc_buf_frames * in->requested_channel,
                                    frames - in->proc_buf_frames);
            if (frames_rd < 0) {
                frames_wr = frames_rd;
                break;
            }
            in->proc_buf_frames += frames_rd;
        }

        if (in->echo_reference != NULL)
            push_echo_reference(in, in->proc_buf_frames);

         /* in_buf.frameCount and out_buf.frameCount indicate respectively
          * the maximum number of frames to be consumed and produced by process() */
        in_buf.frameCount = in->proc_buf_frames;
        in_buf.s16 = in->proc_buf_in;
        out_buf.frameCount = frames - frames_wr;
        out_buf.s16 = (int16_t *)proc_buf_out + frames_wr * in->requested_channel;

        /* FIXME: this works because of current pre processing library implementation that
         * does the actual process only when the last enabled effect process is called.
         * The generic solution is to have an output buffer for each effect and pass it as
         * input to the next.
         */
        for (i = 0; i < in->num_preprocessors; i++) {
            (*in->preprocessors[i].effect_itfe)->process(in->preprocessors[i].effect_itfe,
                                               &in_buf,
                                               &out_buf);
        }

        /* process() has updated the number of frames consumed and produced in
         * in_buf.frameCount and out_buf.frameCount respectively
         * move remaining frames to the beginning of in->proc_buf */
        in->proc_buf_frames -= in_buf.frameCount;
        if (in->proc_buf_frames) {
            memcpy(in->proc_buf_in,
                   in->proc_buf_in + in_buf.frameCount * in->requested_channel,
                   in->proc_buf_frames * in->requested_channel * sizeof(int16_t));
        }

        /* if not enough frames were passed to process(), read more and retry. */
        if (out_buf.frameCount == 0) {
            ALOGV("No frames produced by preproc");
            continue;
        }

        if ((frames_wr + (ssize_t)out_buf.frameCount) <= frames) {
            frames_wr += out_buf.frameCount;
        } else {
            /* The effect does not comply to the API. In theory, we should never end up here! */
            ALOGE("preprocessing produced too many frames: %d + %zu  > %d !",
                  (unsigned int)frames_wr, out_buf.frameCount, (unsigned int)frames);
            frames_wr = frames;
        }
    }
    /* Remove aux_channels that have been added on top of main_channels
     * Assumption is made that the channels are interleaved and that the main
     * channels are first. */
    if (has_aux_channels)
    {
        size_t src_channels = in->config.channels;
        size_t dst_channels = popcount(in->main_channels);
        int16_t* src_buffer = (int16_t *)proc_buf_out;
        int16_t* dst_buffer = (int16_t *)buffer;

        if (dst_channels == 1) {
            for (i = frames_wr; i > 0; i--)
            {
                *dst_buffer++ = *src_buffer;
                src_buffer += src_channels;
            }
        } else {
            for (i = frames_wr; i > 0; i--)
            {
                memcpy(dst_buffer, src_buffer, dst_channels*sizeof(int16_t));
                dst_buffer += dst_channels;
                src_buffer += src_channels;
            }
        }
    }

    return frames_wr;
}

static ssize_t in_read(struct audio_stream_in *stream, void* buffer,
                       size_t bytes)
{
    int ret = 0;
    struct imx_stream_in *in = (struct imx_stream_in *)stream;
    struct imx_audio_device *adev = in->dev;
    size_t frames_rq = bytes / audio_stream_in_frame_size((const struct audio_stream_in *)&stream->common);

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the input stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->standby) {
        ret = start_input_stream(in);
        if (ret == 0) {
            in->standby = 0;
            in->mute_500ms = in->requested_rate * audio_stream_in_frame_size((const struct audio_stream_in *)&stream->common)/2;
        }
    }
    pthread_mutex_unlock(&adev->lock);

    if (ret < 0)
        goto exit;

    spdif_in_rate_check(in);

    if (in->num_preprocessors != 0)
        ret = process_frames(in, buffer, frames_rq);
    else if (in->resampler != NULL)
        ret = read_frames(in, buffer, frames_rq);
    else
        ret = pcm_read_convert(in, in->pcm, buffer, bytes);

    if(ret < 0) ALOGW("ret %d, pcm read error %s.", ret, pcm_get_error(in->pcm));

    if (ret > 0)
        ret = 0;

    if (ret == 0 && adev->mic_mute)
        memset(buffer, 0, bytes);

    if (in->mute_500ms > 0) {
        if(bytes <= in->mute_500ms) {
                memset(buffer, 0, bytes);
                in->mute_500ms = in->mute_500ms - bytes;
        } else {
                memset(buffer, 0, in->mute_500ms);
                in->mute_500ms = 0;
        }
    }

exit:
    pthread_mutex_unlock(&in->lock);
    if (ret < 0) {
        memset(buffer, 0, bytes);
        usleep(bytes * 1000000 / audio_stream_in_frame_size((const struct audio_stream_in *)&stream->common) /
               in_get_sample_rate(&stream->common));
    }
    if (bytes > 0) {
        in->frames_read += frames_rq;
    }

    if (in->dump)
        audio_dump(buffer, bytes, IN_DST_FILE);

    in->frames_round += frames_rq;
    if (in->frames_round >= in->requested_rate) {
        ALOGV("in frames_round %d", in->frames_round);
        in->frames_round -= in->requested_rate;
        in->dump = property_get_bool("vendor.audio.dump", false);
    }

    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream __unused)
{
    return 0;
}

static int in_get_capture_position(const struct audio_stream_in *stream,
                                   int64_t *frames, int64_t *time)
{
    if (stream == NULL || frames == NULL || time == NULL) {
        return -EINVAL;
    }
    struct imx_stream_in *in = (struct imx_stream_in *)stream;
    int ret = -ENOSYS;

    pthread_mutex_lock(&in->lock);
    if (in->standby) {
        ALOGE_IF(in->pcm != NULL,
                 "%s stream in standby but pcm not NULL for non ST session", __func__);
        goto exit;
    }
    if (in->pcm) {
        struct timespec timestamp;
        unsigned int avail;
        if (pcm_get_htimestamp(in->pcm, &avail, &timestamp) == 0) {
            *frames = in->frames_read + avail;
            *time = timestamp.tv_sec * 1000000000LL + timestamp.tv_nsec;
            ret = 0;
        }
    }
exit:
    pthread_mutex_unlock(&in->lock);
    return ret;
}

#define GET_COMMAND_STATUS(status, fct_status, cmd_status) \
            do {                                           \
                if (fct_status != 0)                       \
                    status = fct_status;                   \
                else if (cmd_status != 0)                  \
                    status = cmd_status;                   \
            } while(0)

static int in_configure_effect(struct imx_stream_in* in, effect_handle_t effect)
{
    int32_t cmd_status;
    uint32_t size = sizeof(int);
    effect_config_t config;
    int32_t status = 0;
    int32_t fct_status = 0;

    config.inputCfg.channels = in->main_channels;
    config.outputCfg.channels = in->main_channels;
    config.inputCfg.format = AUDIO_FORMAT_PCM_16_BIT;
    config.outputCfg.format = AUDIO_FORMAT_PCM_16_BIT;
    config.inputCfg.samplingRate = in->requested_rate;
    config.outputCfg.samplingRate = in->requested_rate;
    config.inputCfg.mask = (EFFECT_CONFIG_SMP_RATE | EFFECT_CONFIG_CHANNELS |
                            EFFECT_CONFIG_FORMAT);
    config.outputCfg.mask = (EFFECT_CONFIG_SMP_RATE | EFFECT_CONFIG_CHANNELS |
                             EFFECT_CONFIG_FORMAT);

    fct_status = (*(effect))
                     ->command(effect,
                               EFFECT_CMD_SET_CONFIG,
                               sizeof(effect_config_t),
                               &config,
                               &size,
                               &cmd_status);
    GET_COMMAND_STATUS(status, fct_status, cmd_status);
    return status;
}

static int in_configure_reverse(struct imx_stream_in *in)
{
    int32_t cmd_status;
    uint32_t size = sizeof(int);
    effect_config_t config;
    int32_t status = 0;
    int32_t fct_status = 0;
    int i;

    if (in->num_preprocessors > 0) {
        config.inputCfg.channels = in->main_channels;
        config.outputCfg.channels = in->main_channels;
        config.inputCfg.format = AUDIO_FORMAT_PCM_16_BIT;
        config.outputCfg.format = AUDIO_FORMAT_PCM_16_BIT;
        config.inputCfg.samplingRate = in->requested_rate;
        config.outputCfg.samplingRate = in->requested_rate;
        config.inputCfg.mask =
                ( EFFECT_CONFIG_SMP_RATE | EFFECT_CONFIG_CHANNELS | EFFECT_CONFIG_FORMAT );
        config.outputCfg.mask =
                ( EFFECT_CONFIG_SMP_RATE | EFFECT_CONFIG_CHANNELS | EFFECT_CONFIG_FORMAT );

        for (i = 0; i < in->num_preprocessors; i++)
        {
            if ((*in->preprocessors[i].effect_itfe)->process_reverse == NULL)
                continue;
            fct_status = (*(in->preprocessors[i].effect_itfe))->command(
                                                        in->preprocessors[i].effect_itfe,
                                                        EFFECT_CMD_SET_CONFIG_REVERSE,
                                                        sizeof(effect_config_t),
                                                        &config,
                                                        &size,
                                                        &cmd_status);
            GET_COMMAND_STATUS(status, fct_status, cmd_status);
        }
    }
    return status;
}

#define MAX_NUM_CHANNEL_CONFIGS 10

static void in_read_audio_effect_channel_configs(struct imx_stream_in *in __unused,
                                                 struct effect_info_s *effect_info)
{
    /* size and format of the cmd are defined in hardware/audio_effect.h */
    effect_handle_t effect = effect_info->effect_itfe;
    uint32_t cmd_size = 2 * sizeof(uint32_t);
    uint32_t cmd[] = { EFFECT_FEATURE_AUX_CHANNELS, MAX_NUM_CHANNEL_CONFIGS };
    /* reply = status + number of configs (n) + n x channel_config_t */
    uint32_t reply_size =
            2 * sizeof(uint32_t) + (MAX_NUM_CHANNEL_CONFIGS * sizeof(channel_config_t));
    int32_t reply[reply_size];
    int32_t cmd_status;

    ALOG_ASSERT((effect_info->num_channel_configs == 0),
                "in_read_audio_effect_channel_configs() num_channel_configs not cleared");
    ALOG_ASSERT((effect_info->channel_configs == NULL),
                "in_read_audio_effect_channel_configs() channel_configs not cleared");

    /* if this command is not supported, then the effect is supposed to return -EINVAL.
     * This error will be interpreted as if the effect supports the main_channels but does not
     * support any aux_channels */
    cmd_status = (*effect)->command(effect,
                                EFFECT_CMD_GET_FEATURE_SUPPORTED_CONFIGS,
                                cmd_size,
                                (void*)&cmd,
                                &reply_size,
                                (void*)&reply);

    if (cmd_status != 0) {
        ALOGV("in_read_audio_effect_channel_configs(): "
                "fx->command returned %d", cmd_status);
        return;
    }

    if (reply[0] != 0) {
        ALOGW("in_read_audio_effect_channel_configs(): "
                "command EFFECT_CMD_GET_FEATURE_SUPPORTED_CONFIGS error %d num configs %d",
                reply[0], (reply[0] == -ENOMEM) ? reply[1] : MAX_NUM_CHANNEL_CONFIGS);
        return;
    }

    /* the feature is not supported */
    ALOGV("in_read_audio_effect_channel_configs()(): "
            "Feature supported and adding %d channel configs to the list", reply[1]);
    effect_info->num_channel_configs = reply[1];
    effect_info->channel_configs =
            (channel_config_t *) malloc(sizeof(channel_config_t) * reply[1]); /* n x configs */
    memcpy(effect_info->channel_configs, (reply + 2), sizeof(channel_config_t) * reply[1]);
}


static audio_channel_mask_t in_get_aux_channels(struct imx_stream_in *in)
{
    int i;
    channel_config_t new_chcfg = {AUDIO_CHANNEL_NONE, AUDIO_CHANNEL_NONE};

    if (in->num_preprocessors == 0)
        return AUDIO_CHANNEL_NONE;

    /* do not enable dual mic configurations when capturing from other microphones than
     * main or sub */
    if (!(in->device & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC)))
        return AUDIO_CHANNEL_NONE;

    /* retain most complex aux channels configuration compatible with requested main channels and
     * supported by audio driver and all pre processors */
    for (i = 0; i < NUM_IN_AUX_CNL_CONFIGS; i++) {
        channel_config_t *cur_chcfg = &in_aux_cnl_configs[i];
        if (cur_chcfg->main_channels == in->main_channels) {
            size_t match_cnt;
            size_t idx_preproc;
            for (idx_preproc = 0, match_cnt = 0;
                 /* no need to continue if at least one preprocessor doesn't match */
                 idx_preproc < (size_t)in->num_preprocessors && match_cnt == idx_preproc;
                 idx_preproc++) {
                struct effect_info_s *effect_info = &in->preprocessors[idx_preproc];
                size_t idx_chcfg;

                for (idx_chcfg = 0; idx_chcfg < effect_info->num_channel_configs; idx_chcfg++) {
                    if (memcmp(effect_info->channel_configs + idx_chcfg,
                               cur_chcfg,
                               sizeof(channel_config_t)) == 0) {
                        match_cnt++;
                        break;
                    }
                }
            }
            /* if all preprocessors match, we have a candidate */
            if (match_cnt == (size_t)in->num_preprocessors) {
                /* retain most complex aux channels configuration */
                if (popcount(cur_chcfg->aux_channels) > popcount(new_chcfg.aux_channels)) {
                    new_chcfg = *cur_chcfg;
                }
            }
        }
    }

    ALOGV("in_get_aux_channels(): return %04x", new_chcfg.aux_channels);

    return new_chcfg.aux_channels;
}

static int in_configure_effect_channels(effect_handle_t effect,
                                        channel_config_t *channel_config)
{
    int status = 0;
    int fct_status;
    int32_t cmd_status;
    uint32_t reply_size;
    effect_config_t config;
    uint32_t cmd[(sizeof(uint32_t) + sizeof(channel_config_t) - 1) / sizeof(uint32_t) + 1];

    ALOGV("in_configure_effect_channels(): configure effect with channels: [%04x][%04x]",
            channel_config->main_channels,
            channel_config->aux_channels);

    config.inputCfg.mask = EFFECT_CONFIG_CHANNELS;
    config.outputCfg.mask = EFFECT_CONFIG_CHANNELS;
    reply_size = sizeof(effect_config_t);
    fct_status = (*effect)->command(effect,
                                EFFECT_CMD_GET_CONFIG,
                                0,
                                NULL,
                                &reply_size,
                                &config);
    if (fct_status != 0) {
        ALOGE("in_configure_effect_channels(): EFFECT_CMD_GET_CONFIG failed");
        return fct_status;
    }

    config.inputCfg.channels = channel_config->main_channels | channel_config->aux_channels;
    config.outputCfg.channels = config.inputCfg.channels;
    reply_size = sizeof(uint32_t);
    fct_status = (*effect)->command(effect,
                                    EFFECT_CMD_SET_CONFIG,
                                    sizeof(effect_config_t),
                                    &config,
                                    &reply_size,
                                    &cmd_status);
    GET_COMMAND_STATUS(status, fct_status, cmd_status);

    cmd[0] = EFFECT_FEATURE_AUX_CHANNELS;
    memcpy(cmd + 1, channel_config, sizeof(channel_config_t));
    reply_size = sizeof(uint32_t);
    fct_status = (*effect)->command(effect,
                                EFFECT_CMD_SET_FEATURE_CONFIG,
                                sizeof(cmd), //sizeof(uint32_t) + sizeof(channel_config_t),
                                cmd,
                                &reply_size,
                                &cmd_status);
    GET_COMMAND_STATUS(status, fct_status, cmd_status);

    /* some implementations need to be re-enabled after a config change */
    reply_size = sizeof(uint32_t);
    fct_status = (*effect)->command(effect,
                                  EFFECT_CMD_ENABLE,
                                  0,
                                  NULL,
                                  &reply_size,
                                  &cmd_status);
    GET_COMMAND_STATUS(status, fct_status, cmd_status);

    return status;
}

static int in_reconfigure_channels(struct imx_stream_in *in,
                                   effect_handle_t effect,
                                   channel_config_t *channel_config,
                                   bool config_changed) {

    int status = 0;

    ALOGV("in_reconfigure_channels(): config_changed %d effect %p",
          config_changed, effect);

    /* if config changed, reconfigure all previously added effects */
    if (config_changed) {
        int i;
        for (i = 0; i < in->num_preprocessors; i++)
        {
            int cur_status = in_configure_effect_channels(in->preprocessors[i].effect_itfe,
                                                  channel_config);
            if (cur_status != 0) {
                ALOGV("in_reconfigure_channels(): error %d configuring effect "
                        "%d with channels: [%04x][%04x]",
                        cur_status,
                        i,
                        channel_config->main_channels,
                        channel_config->aux_channels);
                status = cur_status;
            }
        }
    } else if (effect != NULL && channel_config->aux_channels) {
        /* if aux channels config did not change but aux channels are present,
         * we still need to configure the effect being added */
        status = in_configure_effect_channels(effect, channel_config);
    }
    return status;
}

static void in_update_aux_channels(struct imx_stream_in *in,
                                   effect_handle_t effect)
{
    audio_channel_mask_t aux_channels;
    channel_config_t channel_config;
    int status;

    aux_channels = in_get_aux_channels(in);

    channel_config.main_channels = in->main_channels;
    channel_config.aux_channels = aux_channels;
    status = in_reconfigure_channels(in,
                                     effect,
                                     &channel_config,
                                     (aux_channels != in->aux_channels));

    if (status != 0) {
        ALOGV("in_update_aux_channels(): in_reconfigure_channels error %d", status);
        /* resetting aux channels configuration */
        aux_channels = AUDIO_CHANNEL_NONE;
        channel_config.aux_channels = AUDIO_CHANNEL_NONE;
        in_reconfigure_channels(in, effect, &channel_config, true);
    }
    if (in->aux_channels != aux_channels) {
        ALOGV("aux_channels_changed ");
        in->aux_channels_changed = true;
        in->aux_channels = aux_channels;
        do_input_standby(in);
    }
}

static int in_add_audio_effect(const struct audio_stream *stream,
                               effect_handle_t effect)
{
    struct imx_stream_in *in = (struct imx_stream_in *)stream;
    int status;
    effect_descriptor_t desc;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->num_preprocessors >= MAX_PREPROCESSORS) {
        status = -ENOSYS;
        goto exit;
    }

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;

    /* check compatibility between the effect and the stream */
    status = in_configure_effect(in, effect);
    if (status != 0)
        goto exit;

    in->preprocessors[in->num_preprocessors].effect_itfe = effect;
    /* add the supported channel of the effect in the channel_configs */
    in_read_audio_effect_channel_configs(in, &in->preprocessors[in->num_preprocessors]);

    in->num_preprocessors++;

    /* check compatibility between main channel supported and possible auxiliary channels */
    in_update_aux_channels(in, effect);

    ALOGV("in_add_audio_effect(), effect type: %08x", desc.type.timeLow);

    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = true;
        do_input_standby(in);
        in_configure_reverse(in);
    }

exit:

    ALOGW_IF(status != 0, "in_add_audio_effect() error %d", status);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_remove_audio_effect(const struct audio_stream *stream,
                                  effect_handle_t effect)
{
    struct imx_stream_in *in = (struct imx_stream_in *)stream;
    int i;
    int status = -EINVAL;
    effect_descriptor_t desc;

    pthread_mutex_lock(&in->dev->lock);
    pthread_mutex_lock(&in->lock);
    if (in->num_preprocessors <= 0) {
        status = -ENOSYS;
        goto exit;
    }

    for (i = 0; i < in->num_preprocessors; i++) {
        if (status == 0) { /* status == 0 means an effect was removed from a previous slot */
            in->preprocessors[i - 1].effect_itfe = in->preprocessors[i].effect_itfe;
            in->preprocessors[i - 1].channel_configs = in->preprocessors[i].channel_configs;
            in->preprocessors[i - 1].num_channel_configs = in->preprocessors[i].num_channel_configs;
            ALOGV("in_remove_audio_effect moving fx from %d to %d", i, i - 1);
            continue;
        }
        if (in->preprocessors[i].effect_itfe == effect) {
            ALOGV("in_remove_audio_effect found fx at index %d", i);
            free(in->preprocessors[i].channel_configs);
            status = 0;
        }
    }

    if (status != 0)
        goto exit;

    in->num_preprocessors--;
    /* if we remove one effect, at least the last preproc should be reset */
    in->preprocessors[in->num_preprocessors].num_channel_configs = 0;
    in->preprocessors[in->num_preprocessors].effect_itfe = NULL;
    in->preprocessors[in->num_preprocessors].channel_configs = NULL;


    /* check compatibility between main channel supported and possible auxiliary channels */
    in_update_aux_channels(in, NULL);

    status = (*effect)->get_descriptor(effect, &desc);
    if (status != 0)
        goto exit;

    ALOGV("in_remove_audio_effect(), effect type: %08x", desc.type.timeLow);

    if (memcmp(&desc.type, FX_IID_AEC, sizeof(effect_uuid_t)) == 0) {
        in->need_echo_reference = false;
        do_input_standby(in);
    }

exit:

    ALOGW_IF(status != 0, "in_remove_audio_effect() error %d", status);
    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&in->dev->lock);
    return status;
}

static int in_set_microphone_direction(const struct audio_stream_in *stream,
                                           audio_microphone_direction_t dir)
{
    (void)stream;
    (void)dir;
    return 0;
}

static int in_set_microphone_field_dimension(const struct audio_stream_in *stream, float zoom)
{
    (void)stream;
    (void)zoom;
    return 0;
}

static int out_read_hdmi_channel_masks(struct imx_audio_device *adev, struct imx_stream_out *out) {

    int card = -1;
    int i = 0;

    for (i = 0; i < adev->audio_card_num; i ++) {
        if(adev->card_list[i]->is_hdmi_card) {
             card = adev->card_list[i]->card;
             break;
         }
    }

    struct pcm_params *params = pcm_params_get(card, 0, PCM_OUT);
    if (params == NULL) {
        ALOGE("%s: failed to get pcm params for card %d", __func__, card);
        return -ENOSYS;
    }

    int channels = pcm_params_get_max(params, PCM_PARAM_CHANNELS);
    ALOGI("%s: card %d got %d max channels", __func__, card, channels);
    pcm_params_free(params);

    switch (channels) {
        case 6:
            out->sup_channel_masks[0] = AUDIO_CHANNEL_OUT_STEREO;
            out->sup_channel_masks[1] = AUDIO_CHANNEL_OUT_5POINT1;
            break;
        case 8:
            out->sup_channel_masks[0] = AUDIO_CHANNEL_OUT_STEREO;
            out->sup_channel_masks[1] = AUDIO_CHANNEL_OUT_5POINT1;
            out->sup_channel_masks[2] = AUDIO_CHANNEL_OUT_7POINT1;
            break;
        default:
            out->sup_channel_masks[0] = AUDIO_CHANNEL_OUT_STEREO;
            break;
    }

    return 0;
}

const unsigned int hdmi_supported_rates[] = {32000, 44100, 48000, 88200, 96000, 176400, 192000};

static int out_read_hdmi_rates(struct imx_audio_device *adev, struct imx_stream_out *out) {

    int count = 0;
    int card = -1;
    int i = 0;

    for (i = 0; i < adev->audio_card_num; i ++) {
         if(adev->card_list[i]->is_hdmi_card) {
             card = adev->card_list[i]->card;
             break;
         }
    }

    struct pcm_params *params = pcm_params_get(card, 0, PCM_OUT);
    if (params == NULL) {
        ALOGE("%s: failed to get pcm params for card %d", __func__, card);
        return -ENOSYS;
    }

    int min = pcm_params_get_min(params, PCM_PARAM_RATE);
    int max = pcm_params_get_max(params, PCM_PARAM_RATE);
    ALOGI("%s: card %d got sample rates: (%d-%d)", __func__, card, min, max);
    pcm_params_free(params);

    for (i = 0; i < sizeof(hdmi_supported_rates)/sizeof(unsigned int); i ++) {
        if (hdmi_supported_rates[i] >= min && hdmi_supported_rates[i] <= max)
            out->sup_rates[count++] = hdmi_supported_rates[i];
    }

    return 0;
}

#if ANDROID_SDK_VERSION >= 28
// This api is support from android 9.0
static int adev_get_microphones(const struct audio_hw_device *dev __unused,
                                struct audio_microphone_characteristic_t *mic_array,
                                size_t *mic_count) {
    ALOGD("%s", __func__);

    if (mic_count == NULL) {
        return -EINVAL;
    }
    if (mic_array == NULL) {
        return -EINVAL;
    }

    if (*mic_count == 0) {
        *mic_count = 1;
        return 0;
    }

    for (size_t ch = 0; ch < AUDIO_CHANNEL_COUNT_MAX; ch++) {
        mic_array->channel_mapping[ch] = AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED;
    }

    mic_array->device = AUDIO_DEVICE_IN_BUILTIN_MIC;
    strncpy(mic_array->address, AUDIO_BOTTOM_MICROPHONE_ADDRESS, AUDIO_DEVICE_MAX_ADDRESS_LEN);
    mic_array->address[AUDIO_DEVICE_MAX_ADDRESS_LEN-1] = 0;

    *mic_count = 1;

    return 0;
}

static int in_get_active_microphones(const struct audio_stream_in *stream,
                                     struct audio_microphone_characteristic_t *mic_array,
                                     size_t *mic_count)
{
    struct imx_stream_in *in = (struct imx_stream_in *)stream;
    struct imx_audio_device *adev = in->dev;

    return adev_get_microphones((struct audio_hw_device *)adev, mic_array, mic_count);
}
#endif

static int adev_set_audio_port_config(struct audio_hw_device *dev,
        const struct audio_port_config *config)
{
    struct imx_audio_device *adev = (struct imx_audio_device *)dev;
    char *bus_address = (char *)config->ext.device.address;
    struct imx_stream_out *out = (struct imx_stream_out *)hashmapGet(adev->out_bus_stream_map, bus_address);
    int card_index = -1;
    struct route_setting *route;
    struct mixer *mixer;
    int ret = 0;

    if (out) {
        if (!out->gain_stage.step_value) {
            ALOGE("%s: gain stage step value cannot be %d", __func__, out->gain_stage.step_value);
            ret = -EINVAL;
            return ret;
        }
        if (-1 == get_card_for_bus(adev, bus_address, &card_index, NULL)) {
            ALOGE("%s: No card available for config setting!", __func__);
            ret = -EINVAL;
            return ret;
        }
        route = adev->card_list[card_index]->out_volume_ctl;
        mixer = adev->mixer[card_index];

        pthread_mutex_lock(&out->lock);
        int gainIndex = (config->gain.values[0] - out->gain_stage.min_value) /
            out->gain_stage.step_value;
        int totalSteps = (out->gain_stage.max_value - out->gain_stage.min_value) /
            out->gain_stage.step_value;
        int minDb = out->gain_stage.min_value / 100;
        int maxDb = out->gain_stage.max_value / 100;
        int max_volume = 255;
        int volume;
        // curve: 10^((minDb + (maxDb - minDb) * gainIndex / totalSteps) / 20)
        float amplitude_ratio = pow(10,
                (minDb + (maxDb - minDb) * (gainIndex / (float)totalSteps)) / 20);
        if (gainIndex == 0) amplitude_ratio = 0;
        volume = max_volume * amplitude_ratio;
        out_set_control_volume(mixer, route, volume, volume);
        pthread_mutex_unlock(&out->lock);
        ALOGD("%s: set audio gain: card: %d, address: %s, amplitude_ratio: %f, volume: %d",
                __func__, adev->card_list[card_index]->card, bus_address, amplitude_ratio, volume);
    } else {
        ALOGE("%s: can not find output stream by bus_address:%s", __func__, bus_address);
        ret = -EINVAL;
    }

    return ret;
}

// This must be called with adev->lock held.
struct imx_stream_out *get_stream_out_by_io_handle_l(
        struct imx_audio_device *adev, audio_io_handle_t handle) {
    struct listnode *node;

    list_for_each(node, &adev->out_streams) {
        struct imx_stream_out *out = node_to_item(
                node, struct imx_stream_out, stream_node);
        if (out->handle == handle) {
            return out;
        }
    }
    return NULL;
}

// This must be called with adev->lock held.
struct imx_stream_in *get_stream_in_by_io_handle_l(
        struct imx_audio_device *adev, audio_io_handle_t handle) {
    struct listnode *node;

    list_for_each(node, &adev->in_streams) {
        struct imx_stream_in *in = node_to_item(
                node, struct imx_stream_in, stream_node);
        if (in->handle == handle) {
            return in;
        }
    }
    return NULL;
}

// This must be called with adev->lock held.
struct imx_stream_out *get_stream_out_by_patch_handle_l(
        struct imx_audio_device *adev, audio_patch_handle_t patch_handle) {
    struct listnode *node;

    list_for_each(node, &adev->out_streams) {
        struct imx_stream_out *out = node_to_item(
                node, struct imx_stream_out, stream_node);
        if (out->patch_handle == patch_handle) {
            return out;
        }
    }
    return NULL;
}

// This must be called with adev->lock held.
struct imx_stream_in *get_stream_in_by_patch_handle_l(
        struct imx_audio_device *adev, audio_patch_handle_t patch_handle) {
    struct listnode *node;

    list_for_each(node, &adev->in_streams) {
        struct imx_stream_in *in = node_to_item(
                node, struct imx_stream_in, stream_node);
        if (in->patch_handle == patch_handle) {
            return in;
        }
    }
    return NULL;
}

static int adev_create_audio_patch(struct audio_hw_device *dev,
        unsigned int num_sources,
        const struct audio_port_config *sources,
        unsigned int num_sinks,
        const struct audio_port_config *sinks,
        audio_patch_handle_t *handle)
{
    if (num_sources != 1 || num_sinks == 0 || num_sinks > AUDIO_PATCH_PORTS_MAX) {
        ALOGE("%s() invalid source/sink number", __func__);
        return -EINVAL;
    }

    if (sources[0].type == AUDIO_PORT_TYPE_DEVICE) {
        ALOGD("%s() device->mix: %d -> %d", __func__, sources[0].ext.device.type &~ AUDIO_DEVICE_BIT_IN, sinks[0].ext.mix.handle);
        // If source is a device, the number of sinks should be 1.
        if (num_sinks != 1 || sinks[0].type != AUDIO_PORT_TYPE_MIX) {
            return -EINVAL;
        }
    } else if (sources[0].type == AUDIO_PORT_TYPE_MIX) {
        ALOGD("%s() mix->device: %d -> %d", __func__, sources[0].ext.mix.handle, sinks[0].ext.device.type);
        // If source is a mix, all sinks should be device.
        for (unsigned int i = 0; i < num_sinks; i++) {
            if (sinks[i].type != AUDIO_PORT_TYPE_DEVICE) {
                ALOGE("%s() invalid sink type %#x for mix source", __func__, sinks[i].type);
                return -EINVAL;
            }
        }
    } else {
        // All other cases are invalid.
        ALOGE("%s() invalid source/sink configures", __func__);
        return -EINVAL;
    }

    struct imx_audio_device* adev = (struct imx_audio_device*) dev;
    int ret = 0;
    bool generatedPatchHandle = false;
    pthread_mutex_lock(&adev->lock);
    if (*handle == AUDIO_PATCH_HANDLE_NONE) {
        *handle = ++adev->next_patch_handle;
        generatedPatchHandle = true;
    }

    // Only handle patches for mix->devices and device->mix case.
    if (sources[0].type == AUDIO_PORT_TYPE_DEVICE) {
        struct imx_stream_in *in =
                get_stream_in_by_io_handle_l(adev, sinks[0].ext.mix.handle);
        if (in == NULL) {
            ALOGE("%s()can not find stream with handle(%d)", __func__, sources[0].ext.mix.handle);
            ret = -EINVAL;
            goto error;
        }

        // Check if the patch handle match the recorded one if a valid patch handle is passed.
        if (!generatedPatchHandle && in->patch_handle != *handle) {
            ALOGE("%s() the patch handle(%d) does not match recorded one(%d) for stream "
                  "with handle(%d) when creating audio patch for device->mix",
                  __func__, *handle, in->patch_handle, in->handle);
            ret = -EINVAL;
            goto error;
        }
        pthread_mutex_lock(&in->lock);
        if (in->device != sources[0].ext.device.type) {
            in->device = sources[0].ext.device.type &~ AUDIO_DEVICE_BIT_IN;
            do_input_standby(in);
            in_update_aux_channels(in, NULL);
            ALOGD("%s() set input(%d) device to %x, patch_handle %d", __func__, in->handle, in->device, *handle);
        }
        pthread_mutex_unlock(&in->lock);
        in->patch_handle = *handle;
    } else {
        struct imx_stream_out *out =
                get_stream_out_by_io_handle_l(adev, sources[0].ext.mix.handle);
        if (out == NULL) {
            ALOGE("%s()can not find stream with handle(%d)", __func__, sources[0].ext.mix.handle);
            ret = -EINVAL;
            goto error;
        }

        // Check if the patch handle match the recorded one if a valid patch handle is passed.
        if (!generatedPatchHandle && out->patch_handle != *handle) {
            ALOGE("%s() the patch handle(%d) does not match recorded one(%d) for stream "
                  "with handle(%d) when creating audio patch for mix->device",
                  __func__, *handle, out->patch_handle, out->handle);
            ret = -EINVAL;
            goto error;
        }
        pthread_mutex_lock(&out->lock);
        for (out->num_devices = 0; out->num_devices < num_sinks; out->num_devices++) {
            out->devices[out->num_devices] = sinks[out->num_devices].ext.device.type;
        }
        if (out->device != sinks[0].ext.device.type) {
            out->device = sinks[0].ext.device.type;
            do_output_standby(out, true);
            ALOGD("%s() set output(%d) device to %x, patch_handle %d", __func__, out->handle, out->device, *handle);
        }
        pthread_mutex_unlock(&out->lock);
        out->patch_handle = *handle;
    }

error:
    if (ret != 0 && generatedPatchHandle) {
        *handle = AUDIO_PATCH_HANDLE_NONE;
    }
    pthread_mutex_unlock(&adev->lock);
    return 0;
}

static int adev_release_audio_patch(struct audio_hw_device *dev,
        audio_patch_handle_t patch_handle)
{
    struct imx_audio_device *adev = (struct imx_audio_device *) dev;

    ALOGE("%s() patch_handle: %d", __func__, patch_handle);
    pthread_mutex_lock(&adev->lock);
    struct imx_stream_out *out = get_stream_out_by_patch_handle_l(adev, patch_handle);
    if (out != NULL) {
        pthread_mutex_lock(&out->lock);
        out->num_devices = 0;
        memset(out->devices, 0, sizeof(out->devices));
        out->device = 0;
        do_output_standby(out, true);
        pthread_mutex_unlock(&out->lock);
        out->patch_handle = AUDIO_PATCH_HANDLE_NONE;
        pthread_mutex_unlock(&adev->lock);
        return 0;
    }
    struct imx_stream_in *in = get_stream_in_by_patch_handle_l(adev, patch_handle);
    if (in != NULL) {
        pthread_mutex_lock(&in->lock);
        in->device = AUDIO_DEVICE_NONE;
        do_input_standby(in);
        pthread_mutex_unlock(&in->lock);
        in->patch_handle = AUDIO_PATCH_HANDLE_NONE;
        pthread_mutex_unlock(&adev->lock);
        return 0;
    }

    pthread_mutex_unlock(&adev->lock);
    ALOGW("%s() cannot find stream for patch handle: %d", __func__, patch_handle);
    return -EINVAL;
}

static int adev_get_audio_port(struct audio_hw_device *dev, struct audio_port *port)
{
    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle __unused,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out,
                                   const char* address)
{
    struct imx_audio_device *ladev = (struct imx_audio_device *)dev;
    struct imx_stream_out *out;
    int ret;

    ALOGI("%s: enter: sample_rate(%d) channel_mask(%#x) format(%#x) devices(%#x) flags(%#x), address(%s)",
              __func__, config->sample_rate, config->channel_mask, config->format, devices, flags, address);
    out = (struct imx_stream_out *)calloc(1, sizeof(struct imx_stream_out));
    if (!out)
        return -ENOMEM;

    out->flags = flags;
    out->device = devices;
    out->address = strdup(address);
    out->sup_rates[0] = config->sample_rate;
    out->sup_channel_masks[0] = AUDIO_CHANNEL_OUT_STEREO;
    out->sample_rate = config->sample_rate;
    out->channel_mask = config->channel_mask;
    out->format = config->format;

    if (flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        ALOGW("%s: compress offload stream", __func__);
        if (out->format != AUDIO_FORMAT_DSD) {
            ALOGE("%s: Unsupported audio format", __func__);
            ret = -EINVAL;
            goto err_open;
        }
        if (config->sample_rate == 0)
            config->sample_rate = pcm_config_dsd.rate;
        if (config->channel_mask == 0)
            config->channel_mask = AUDIO_CHANNEL_OUT_STEREO;
        out->channel_mask = config->channel_mask;
        pcm_config_dsd.rate = config->sample_rate / DSD_RATE_TO_PCM_RATE;
        out->config = pcm_config_dsd;
        out->stream.flush = out_flush;
    } else if (flags & AUDIO_OUTPUT_FLAG_DIRECT &&
               devices == AUDIO_DEVICE_OUT_AUX_DIGITAL) {
        ALOGW("adev_open_output_stream() HDMI multichannel");
        ret = out_read_hdmi_channel_masks(ladev, out);
        if (ret != 0)
            goto err_open;

        ret = out_read_hdmi_rates(ladev, out);
        if (ret != 0)
            goto err_open;

        if (config->sample_rate == 0) {
            config->sample_rate = DEFAULT_OUTPUT_SAMPLE_RATE;
            out->sample_rate = config->sample_rate;
        }
        if (config->channel_mask == 0)
            config->channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
        if (config->format == AUDIO_FORMAT_DEFAULT) {
            config->format = AUDIO_FORMAT_PCM_16_BIT;
            out->format = config->format;
        }
        out->channel_mask = config->channel_mask;
        out->config = pcm_config_hdmi_multi;
        out->config.rate = config->sample_rate;
        out->config.channels = popcount(config->channel_mask);

        update_passthrough_status();
        if (strcmp(ladev->device_name, "evk_8mp") == 0) {
            out->config.format = PCM_FORMAT_S24_LE;
            if (passthrough_enabled) {
                passthrough_for_s24 = true;
                ALOGI("%s, passthrough is enabled on evk_8mp", __func__);
            }
        }
    } else if (flags & AUDIO_OUTPUT_FLAG_DIRECT &&
              ((devices == AUDIO_DEVICE_OUT_SPEAKER) ||
               (devices == AUDIO_DEVICE_OUT_LINE) ||
               (devices == AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) &&
               (ladev->support_multichannel || (ladev->support_lpa && lpa_enable))) {
        ALOGW("adev_open_output_stream() ESAI multichannel");
        int lpa_hold_second = 0;
        int lpa_period_ms = 0;

        if (lpa_enable == 0) {
            lpa_hold_second = property_get_int32("vendor.audio.lpa.hold_second", 0);
            lpa_period_ms = property_get_int32("vendor.audio.lpa.period_ms", 0);
        } else if (lpa_enable == 1) {
            lpa_hold_second = property_get_int32("vendor.audio.lpa.hold_second", 60);
            lpa_period_ms = property_get_int32("vendor.audio.lpa.period_ms", 1000);
        }

        if(lpa_hold_second && lpa_period_ms) {
            pcm_config_esai_multi.period_size = config->sample_rate * lpa_period_ms / 1000;
            pcm_config_esai_multi.period_count =  lpa_hold_second * 1000 / lpa_period_ms;
        }

        ALOGD("%s: LPA esai direct output stream, hold_second: %d, period_ms: %d", __func__, lpa_hold_second, lpa_period_ms);
        if (config->sample_rate == 0) {
            config->sample_rate = DEFAULT_OUTPUT_SAMPLE_RATE;
            out->sample_rate = config->sample_rate;
        }
        if (config->channel_mask == 0)
            config->channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
        if (config->format != AUDIO_FORMAT_PCM_FLOAT)
            pcm_config_esai_multi.format = PCM_FORMAT_S16_LE;
        out->channel_mask = config->channel_mask;

        out->config = pcm_config_esai_multi;
        out->config.rate = config->sample_rate;
        out->config.channels = popcount(config->channel_mask);
        out->stream.pause = out_pause;
        out->stream.resume = out_resume;
        out->stream.flush = out_flush;
    } else if ((flags == AUDIO_OUTPUT_FLAG_NONE) &&
            ((devices == AUDIO_DEVICE_OUT_AUX_DIGITAL) ||
             (devices == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET) ||
             (devices == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT) ||
             (devices == AUDIO_DEVICE_OUT_BLUETOOTH_SCO))) {
        ALOGD("%s: non-primary mixer output stream", __func__);

        out->config = pcm_config_mm_out;
        out->config.rate = config->sample_rate;
        out->config.channels = audio_channel_count_from_out_mask(config->channel_mask);
        out->config.format = pcm_format_from_audio_format(config->format);

        if ((out->device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET) ||
                (out->device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT) ||
                (out->device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO)) {
            out->config.channels = g_hsp_chns;
        }
    } else {
        ALOGD("%s: primary output stream", __func__);
        if (flags & AUDIO_OUTPUT_FLAG_PRIMARY) {
            if (ladev->primary_output == NULL)
                ladev->primary_output = out;
            else {
                if (out->device == AUDIO_DEVICE_OUT_BUS)
                    ALOGD("%s: car device already has primary output: %p", __func__, ladev->primary_output);
                else {
                    ALOGE("%s: Primary output is already opened", __func__);
                    ret = -EEXIST;
                    goto err_open;
                }
            }
        }
        if (flags & AUDIO_OUTPUT_FLAG_DIRECT) {
            ALOGE("%s: direct output stream should not come here", __func__);
            // VtsHalAudioV6_0TargetTest asserts each stream declared in policy xml can be opened.
            // In LPA image, if not set lpa.enable property, direct output stream will come here.
            // Assign this stream's pause and resume interface to avoid below fatal error:
            //    "HW_AV_SYNC requested but HAL does not implement pause and resume"
            out->stream.pause = out_pause_dummy;
            out->stream.resume = out_resume_dummy;
        }
        out->config = pcm_config_mm_out;
        out->sample_rate = DEFAULT_OUTPUT_SAMPLE_RATE;
        out->channel_mask = DEFAULT_OUTPUT_CHANNEL_MASK;
        out->format = DEFAULT_OUTPUT_FORMAT;
    }

    if (address) {
        hashmapPut(ladev->out_bus_stream_map, out->address, out);
        if (strcmp(out->address, "bus0_media_out") == 0) {
            // gain_stage should align with audio_policy_configuration.xml
            out->gain_stage = (struct audio_gain) {
                .min_value = -500,
                .max_value = 0,
                .step_value = 100,
            };
        } else if (strcmp(out->address, "bus1_system_sound_out") == 0) {
            out->gain_stage = (struct audio_gain) {
                .min_value = -500,
                .max_value = 0,
                .step_value = 100,
            };
        }
    }

    out->stream.common.get_buffer_size  = out_get_buffer_size;
    out->stream.common.get_sample_rate  = out_get_sample_rate;
    out->stream.common.set_sample_rate  = out_set_sample_rate;
    out->stream.common.get_channels     = out_get_channels;
    out->stream.common.get_format       = out_get_format;
    out->stream.common.set_format       = out_set_format;
    out->stream.common.standby          = out_standby;
    out->stream.common.dump             = out_dump;
    out->stream.common.set_parameters   = out_set_parameters;
    out->stream.common.get_parameters   = out_get_parameters;
    out->stream.common.add_audio_effect = out_add_audio_effect;
    out->stream.common.remove_audio_effect  = out_remove_audio_effect;
    out->stream.set_volume                  = out_set_volume;
    out->stream.get_render_position         = out_get_render_position;
    out->stream.get_presentation_position   = out_get_presentation_position;
    out->stream.get_latency                 = out_get_latency;
    out->stream.write                       = out_write;

    out->handle = handle;

    out->dev = ladev;
    out->standby = 1;
    out->paused = false;
    out->card_index = -1;

    /* FIXME: when we support multiple output devices, we will want to
     * do the following:
     * adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
     * adev->devices |= out->device;
     * select_output_device(adev);
     * This is because out_set_parameters() with a route is not
     * guaranteed to be called after an output stream is opened. */

    if ((config->format != AUDIO_FORMAT_DEFAULT && config->format != out->stream.common.get_format(&out->stream.common)) ||
        (config->channel_mask != 0 && config->channel_mask != out->stream.common.get_channels(&out->stream.common)) ||
        (config->sample_rate != 0 && config->sample_rate != out->stream.common.get_sample_rate(&out->stream.common))) {
        ALOGE("%s: Unsupported output config. sample_rate:%d:%d; format:%#x:%#x; channel_mask:%#x:%#x",
                      __func__, config->sample_rate, out->stream.common.get_sample_rate(&out->stream.common),
                      config->format, out->stream.common.get_format(&out->stream.common),
                      config->channel_mask, out->stream.common.get_channels(&out->stream.common));
        config->format = out->stream.common.get_format(&out->stream.common);
        config->channel_mask = out->stream.common.get_channels(&out->stream.common);
        config->sample_rate = out->stream.common.get_sample_rate(&out->stream.common);
        ret = -EINVAL;
        goto err_open;
    }

    config->format = out->stream.common.get_format(&out->stream.common);
    config->channel_mask = out->stream.common.get_channels(&out->stream.common);
    config->sample_rate = out->stream.common.get_sample_rate(&out->stream.common);

    pthread_mutex_lock(&ladev->lock);
    list_add_tail(&ladev->out_streams, &out->stream_node);
    pthread_mutex_unlock(&ladev->lock);

    *stream_out = &out->stream;

    ALOGI("%s: exit: output %p", __func__, *stream_out);
    return 0;

err_open:
    free(out);
    *stream_out = NULL;
    ALOGW("%s: exit: ret %d", __func__, ret);
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    struct imx_audio_device *adev = (struct imx_audio_device *)dev;
    ALOGW("adev_close_output_stream...%p", out);

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    do_output_standby(out, true);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);

    if (out->buffer)
        free(out->buffer);

    if (out->address) {
        hashmapRemove(adev->out_bus_stream_map, out->address);
        free(out->address);
    }

    if (out->resampler) {
        release_resampler(out->resampler);
        out->resampler = NULL;
    }

    pthread_mutex_lock(&adev->lock);
    list_remove(&out->stream_node);
    pthread_mutex_unlock(&adev->lock);
    free(stream);
}

static void* sco_rx_task(void *arg)
{
    int ret = 0;
    uint32_t size = 0;
    uint32_t frames = 0;
    uint32_t out_frames = 0;
    uint32_t out_size = 0;
    uint8_t *buffer = NULL;
    struct pcm *out_pcm = NULL;
    struct imx_stream_out *stream_out = NULL;
    struct imx_audio_device *adev = (struct imx_audio_device *)arg;
    int flag = 0;
    uint32_t sco_rx_out_buffer_size = 0;
    char *sco_rx_out_buffer = NULL;

    if(adev == NULL)
        return NULL;

    frames = pcm_config_sco_in.period_size;
    size = pcm_frames_to_bytes(adev->pcm_sco_rx, frames);
    buffer = (uint8_t *)malloc(size);
    if(buffer == NULL) {
        ALOGE("sco_rx_task, malloc %d bytes failed", size);
        return NULL;
    }

    ALOGI("enter sco_rx_task, pcm_sco_rx frames %d, szie %d", frames, size);

    stream_out = adev->primary_output;
    if(NULL == stream_out) {
        ALOGE("sco_rx_task, primary output stream is null");
        goto exit;
    }

    if (stream_out->standby) {
        ALOGI("%s: primary output is standy, open it", __func__);
        int ret = start_output_stream(stream_out);
        if (ret == 0)
            stream_out->standby = 0;
        else
            ALOGE("%s: start_output_stream failed, ret %d", __func__, ret);
    }

    out_pcm = stream_out->pcm;
    flag = stream_out->write_flags;
    if(NULL == out_pcm) {
        ALOGE("sco_rx_task, out_pcm is null");
        goto exit;
    }

    sco_rx_out_buffer_size = stream_out->buffer_frames * audio_stream_out_frame_size((const struct audio_stream_out *)&stream_out->stream.common);
    sco_rx_out_buffer = (char *)malloc(sco_rx_out_buffer_size);
    if(sco_rx_out_buffer == NULL) {
        ALOGE("sco_rx_task, malloc sco_rx_out_buffer %d bytes failed", sco_rx_out_buffer_size);
        goto exit;
    }

    while(adev->b_sco_rx_running) {
        ret = pcm_read(adev->pcm_sco_rx, buffer, size);
        if(ret) {
            ALOGE("sco_rx_task, pcm_read ret %d, size %d, %s",
                ret, size, pcm_get_error(adev->pcm_sco_rx));
            usleep(2000);
            continue;
        }

        // 16k to 48k convert, 1chn
        frames = pcm_config_sco_in.period_size;
        out_frames = stream_out->buffer_frames;
        adev->rsmpl_sco_rx->resample_from_input(adev->rsmpl_sco_rx,
                                                (int16_t *)buffer,
                                                (size_t *)&frames,
                                                (int16_t *)stream_out->buffer,
                                                (size_t *)&out_frames);

        ALOGV("sco_rx_task, resample_from_input, in frames %d, %d, out_frames %zu, %d",
            pcm_config_sco_in.period_size, frames,
            stream_out->buffer_frames, out_frames);

        // mono to stereo
        convert_record_data(stream_out->buffer, sco_rx_out_buffer, out_frames, false, false, true, false);
        out_size = pcm_frames_to_bytes(out_pcm, out_frames);

        pthread_mutex_lock(&stream_out->lock);
        ret = pcm_write_wrapper(out_pcm, sco_rx_out_buffer, out_size, flag, stream_out->dump);
        pthread_mutex_unlock(&stream_out->lock);
        if(ret) {
            ALOGE("sco_rx_task, pcm_write ret %d, size %d, %s",
                ret, out_size, pcm_get_error(out_pcm));
            usleep(2000);
        }
    }

    free(sco_rx_out_buffer);
exit:
    free(buffer);
    ALOGI("leave sco_rx_task");

    return NULL;
}

static void* sco_tx_task(void *arg)
{
    int ret = 0;
    uint32_t size = 0;
    uint32_t frames = 0;
    uint8_t *buffer = NULL;
    uint32_t out_frames = 0;
    uint32_t out_size = 0;
    uint8_t *out_buffer = NULL;

    struct imx_audio_device *adev = (struct imx_audio_device *)arg;
    if(adev == NULL)
        return NULL;

    frames = adev->cap_config.period_size;
    size = pcm_frames_to_bytes(adev->pcm_cap , frames);
    buffer = (uint8_t *)malloc(size);
    if(buffer == NULL) {
        ALOGE("sco_tx_task, malloc %d bytes failed", size);
        return NULL;
    }

    ALOGI("enter sco_tx_task, pcm_cap frames %d, szie %d", frames, size);

    out_frames = pcm_config_sco_out.period_size;
    out_size = pcm_frames_to_bytes(adev->pcm_sco_tx, out_frames);
    out_buffer = (uint8_t *)malloc(out_size);
    if(out_buffer == NULL) {
        free(buffer);
        ALOGE("sco_tx_task, malloc out_buffer %d bytes failed", out_size);
        return NULL;
    }

    while(adev->b_sco_tx_running) {
        ret = pcm_read(adev->pcm_cap, buffer, size);
        if(ret) {
            ALOGI("sco_tx_task, pcm_read ret %d, size %d, %s",
                ret, size, pcm_get_error(adev->pcm_cap));
            continue;
        }

        frames = adev->cap_config.period_size;
        out_frames = pcm_config_sco_out.period_size;
        adev->rsmpl_sco_tx->resample_from_input(adev->rsmpl_sco_tx,
                                                (int16_t *)buffer,
                                                (size_t *)&frames,
                                                (int16_t *)out_buffer,
                                                (size_t *)&out_frames);

        ALOGV("sco_tx_task, resample_from_input, in frames %d, %d, out_frames %d, %d",
            adev->cap_config.period_size, frames,
            pcm_config_sco_out.period_size, out_frames);

        out_size = pcm_frames_to_bytes(adev->pcm_sco_tx, out_frames);
        ret = pcm_write(adev->pcm_sco_tx, out_buffer, out_size);
        if(ret) {
            ALOGE("sco_tx_task, pcm_write ret %d, size %d, %s",
                ret, out_size, pcm_get_error(adev->pcm_sco_tx));
        }
    }

    free(buffer);
    free(out_buffer);
    ALOGI("leave sco_tx_task");

    return NULL;
}

static int sco_release_resource(struct imx_audio_device *adev)
{
    if(NULL == adev)
        return -1;

    // release rx resource
    if(adev->tid_sco_rx) {
        adev->b_sco_rx_running = false;
        pthread_join(adev->tid_sco_rx, NULL);
    }

    if(adev->pcm_sco_rx) {
        pcm_close(adev->pcm_sco_rx);
        adev->pcm_sco_rx = NULL;
    }

    if(adev->rsmpl_sco_rx) {
        release_resampler(adev->rsmpl_sco_rx);
        adev->rsmpl_sco_rx = NULL;
    }

    // release tx resource
    if(adev->tid_sco_tx) {
        adev->b_sco_tx_running = false;
        pthread_join(adev->tid_sco_tx, NULL);
    }

    if(adev->pcm_sco_tx) {
        pcm_close(adev->pcm_sco_tx);
        adev->pcm_sco_tx = NULL;
    }

    if(adev->rsmpl_sco_tx) {
        release_resampler(adev->rsmpl_sco_tx);
        adev->rsmpl_sco_tx = NULL;
    }

    if(adev->pcm_cap){
        pcm_close(adev->pcm_cap);
        adev->pcm_cap = NULL;
    }

    return 0;
}

static int sco_task_create(struct imx_audio_device *adev)
{
    int ret = 0;
    unsigned int port = 0;
    unsigned int card = 0;
    pthread_t tid_sco_rx = 0;
    pthread_t tid_sco_tx = 0;
    pthread_attr_t attr;
    struct sched_param schParam;

    if(NULL == adev)
        return -1;

    /*=============== create rx task ===============*/
    ALOGI("prepare bt rx task");
    //open sco card for read
    card = get_card_for_hfp(adev, NULL);
    pcm_config_sco_in.period_size =  pcm_config_mm_out.period_size * pcm_config_sco_in.rate / pcm_config_mm_out.rate;
    pcm_config_sco_in.period_count = pcm_config_mm_out.period_count;

    ALOGI("set pcm_config_sco_in.period_size to %d", pcm_config_sco_in.period_size);
    ALOGI("open sco for read, card %d, port %d", card, port);
    ALOGI("rate %d, channel %d, period_size 0x%x, period_count %d",
        pcm_config_sco_in.rate, pcm_config_sco_in.channels,
        pcm_config_sco_in.period_size, pcm_config_sco_in.period_count);

    adev->pcm_sco_rx = pcm_open(card, port, PCM_IN, &pcm_config_sco_in);
    ALOGI("after pcm open, rate %d, channel %d, period_size 0x%x, period_count %d",
        pcm_config_sco_in.rate, pcm_config_sco_in.channels,
        pcm_config_sco_in.period_size, pcm_config_sco_in.period_count);

    if (adev->pcm_sco_rx && !pcm_is_ready(adev->pcm_sco_rx)) {
        ret = -1;
        ALOGE("cannot open pcm_sco_rx: %s", pcm_get_error(adev->pcm_sco_rx));
        goto error;
    }

    //create resampler
    ret = create_resampler(pcm_config_sco_in.rate,
                           pcm_config_mm_out.rate,
                           pcm_config_sco_in.channels,
                           RESAMPLER_QUALITY_DEFAULT,
                           NULL,
                           &adev->rsmpl_sco_rx);
    if(ret) {
        ALOGI("create_resampler rsmpl_sco_rx failed, ret %d", ret);
        goto error;
    }

    ALOGI("create_resampler rsmpl_sco_rx, in rate %d, out rate %d",
        pcm_config_sco_in.rate, pcm_config_mm_out.rate);

    //create rx task, use real time thread.
    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    schParam.sched_priority = 3;
    pthread_attr_setschedparam(&attr, &schParam);

    adev->b_sco_rx_running = true;
    ret = pthread_create(&tid_sco_rx, &attr, sco_rx_task, (void *)adev);
    if(ret) {
        goto error;
    }
    adev->tid_sco_rx = tid_sco_rx;
    ALOGI("sco_rx_task create ret %d, tid_sco_rx %ld", ret, tid_sco_rx);

    /*=============== create tx task ===============*/
    ALOGI("prepare bt tx task");
    //open sco card for write
    card = get_card_for_hfp(adev, NULL);
    ALOGI("open sco for write, card %d, port %d", card, port);
    ALOGI("rate %d, channel %d, period_size 0x%x",
        pcm_config_sco_out.rate, pcm_config_sco_out.channels, pcm_config_sco_out.period_size);

    adev->pcm_sco_tx = pcm_open(card, port, PCM_OUT, &pcm_config_sco_out);
    if (adev->pcm_sco_tx && !pcm_is_ready(adev->pcm_sco_tx)) {
        ret = -1;
        ALOGE("cannot open pcm_sco_tx: %s", pcm_get_error(adev->pcm_sco_tx));
        goto error;
    }

    card = get_card_for_device(adev, SCO_IN_DEVICE, PCM_IN, NULL);
    adev->cap_config = pcm_config_sco_out;
    adev->cap_config.rate = 48000;
    adev->cap_config.period_size = pcm_config_sco_out.period_size * adev->cap_config.rate / pcm_config_sco_out.rate;
    ALOGW(" open mic, card %d, port %d", card, port);
    ALOGW("rate %d, channel %d, period_size 0x%x",
        adev->cap_config.rate, adev->cap_config.channels, adev->cap_config.period_size);

    // Standby active input stream in HFP case
    if (adev->active_input) {
        do_input_standby(adev->active_input);
        usleep(2000);
    }

    adev->pcm_cap = pcm_open(card, port, PCM_IN, &adev->cap_config);
    if (adev->pcm_cap && !pcm_is_ready(adev->pcm_cap)) {
        ret = -1;
        ALOGE("cannot open pcm_cap: %s", pcm_get_error(adev->pcm_cap));
        goto error;
    }

    //create resampler
    ret = create_resampler(adev->cap_config.rate,
                         pcm_config_sco_out.rate,
                         pcm_config_sco_out.channels,
                         RESAMPLER_QUALITY_DEFAULT,
                         NULL,
                         &adev->rsmpl_sco_tx);
    if(ret) {
      ALOGI("create_resampler rsmpl_sco_tx failed, ret %d", ret);
      goto error;
    }

    ALOGI("create_resampler rsmpl_sco_tx, in rate %d, out rate %d",
        adev->cap_config.rate, pcm_config_sco_out.rate);

    //create tx task, use real time thread.
    pthread_attr_init(&attr);
    pthread_attr_setschedpolicy(&attr, SCHED_FIFO);
    schParam.sched_priority = 3;
    pthread_attr_setschedparam(&attr, &schParam);

    adev->b_sco_tx_running = true;
    ret = pthread_create(&tid_sco_tx, &attr, sco_tx_task, (void *)adev);
    if(ret) {
        goto error;
    }
    adev->tid_sco_tx = tid_sco_tx;
    ALOGI("sco_tx_task create ret %d, tid_sco_tx %ld", ret, tid_sco_tx);

    return 0;

error:
    sco_release_resource(adev);
    return ret;
}

static int sco_task_destroy(struct imx_audio_device *adev)
{
    int ret;

    ALOGI("enter sco_task_destroy");
    ret = sco_release_resource(adev);
    ALOGI("leave sco_task_destroy");

    return ret;
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct imx_audio_device *adev = (struct imx_audio_device *)dev;
    struct str_parms *parms;
    char value[32];
    int ret;
    int status = 0;

    ALOGD("%s: enter: %s", __func__, kvpairs);

    parms = str_parms_create_str(kvpairs);
    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_TTY_MODE, value, sizeof(value));
    if (ret >= 0) {
        int tty_mode;

        if (strcmp(value, AUDIO_PARAMETER_VALUE_TTY_OFF) == 0)
            tty_mode = TTY_MODE_OFF;
        else if (strcmp(value, AUDIO_PARAMETER_VALUE_TTY_VCO) == 0)
            tty_mode = TTY_MODE_VCO;
        else if (strcmp(value, AUDIO_PARAMETER_VALUE_TTY_HCO) == 0)
            tty_mode = TTY_MODE_HCO;
        else if (strcmp(value, AUDIO_PARAMETER_VALUE_TTY_FULL) == 0)
            tty_mode = TTY_MODE_FULL;
        else {
            status = -EINVAL;
            goto done;
        }

        pthread_mutex_lock(&adev->lock);
        if (tty_mode != adev->tty_mode) {
            adev->tty_mode = tty_mode;
            if (adev->mode == AUDIO_MODE_IN_CALL)
                select_output_device(adev);
        }
        pthread_mutex_unlock(&adev->lock);
    }

    ret = str_parms_get_str(parms, "hfp_set_sampling_rate", value, sizeof(value));
    if (ret >= 0) {
        int rate = atoi(value);
        ALOGI("hfp_set_sampling_rate, %d", rate);
        pcm_config_sco_in.rate = rate;
        pcm_config_sco_out.rate = rate;
    }

    ret = str_parms_get_str(parms, "hfp_enable", value, sizeof(value));
    if (ret >= 0) {
        if(0 == strcmp(value, "true")) {
            pthread_mutex_lock(&adev->lock);
            ret = sco_task_create(adev);
            pthread_mutex_unlock(&adev->lock);
            ALOGI("sco_task_create, ret %d", ret);
        } else {
            pthread_mutex_lock(&adev->lock);
            ret = sco_task_destroy(adev);
            pthread_mutex_unlock(&adev->lock);
            ALOGI("sco_task_destroy, ret %d", ret);
        }
    }

    ret = str_parms_get_str(parms, "pcm_bit", value, sizeof(value));
    if (ret >= 0) {
        int bits = atoi(value);
        if (bits == 16)
            pcm_config_esai_multi.format = PCM_FORMAT_S16_LE;
        else if (bits == 24)
            pcm_config_esai_multi.format = PCM_FORMAT_S24_LE;
        else if (bits == 32)
            pcm_config_esai_multi.format = PCM_FORMAT_S32_LE;
    }

done:
    str_parms_destroy(parms);
    ALOGD("%s: exit with code(%d)", __func__, status);
    return status;
}

static char * adev_get_parameters(const struct audio_hw_device *dev __unused,
                                  const char *keys __unused)
{
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev __unused)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct imx_audio_device *adev = (struct imx_audio_device *)dev;

    adev->voice_volume = volume;

    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev __unused, float volume __unused)
{
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, audio_mode_t mode)
{
    struct imx_audio_device *adev = (struct imx_audio_device *)dev;

    ALOGI("adev_set_mode mode %d\n", mode);
    pthread_mutex_lock(&adev->lock);
    if (adev->mode != mode) {
        adev->mode = mode;
        select_mode(adev);
    }
    pthread_mutex_unlock(&adev->lock);

    return 0;
}

static int adev_set_mic_mute(struct audio_hw_device *dev, bool state)
{
    struct imx_audio_device *adev = (struct imx_audio_device *)dev;

    adev->mic_mute = state;

    return 0;
}

static int adev_get_mic_mute(const struct audio_hw_device *dev, bool *state)
{
    struct imx_audio_device *adev = (struct imx_audio_device *)dev;

    *state = adev->mic_mute;

    return 0;
}

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev __unused,
                                         const struct audio_config *config)
{
    return get_input_buffer_size(config->sample_rate, config->format, config->channel_mask);
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle __unused,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in,
                                  audio_input_flags_t flags __unused,
                                  const char* address,
                                  audio_source_t source __unused)
{
    struct imx_audio_device *adev = (struct imx_audio_device *)dev;
    struct imx_stream_in *in;
    int channel_count = popcount(config->channel_mask);

    if (refine_input_parameters(&config->sample_rate, &config->format, &config->channel_mask)) {
        ALOGE("Error opening input stream. Refine parameters to format %d, channel_mask %#x, \
                sample_rate %u", config->format, config->channel_mask, config->sample_rate);
        return -EINVAL;
    }

    ALOGI("%s: enter: devices(%#x) flags(%#x), address(%s)", __func__, devices, flags, address);

    in = (struct imx_stream_in *)calloc(1, sizeof(struct imx_stream_in));
    if (!in)
        return -ENOMEM;

    in->stream.common.get_sample_rate   = in_get_sample_rate;
    in->stream.common.set_sample_rate   = in_set_sample_rate;
    in->stream.common.get_buffer_size   = in_get_buffer_size;
    in->stream.common.get_channels      = in_get_channels;
    in->stream.common.get_format        = in_get_format;
    in->stream.common.set_format        = in_set_format;
    in->stream.common.standby           = in_standby;
    in->stream.common.dump              = in_dump;
    in->stream.common.set_parameters    = in_set_parameters;
    in->stream.common.get_parameters    = in_get_parameters;
    in->stream.common.add_audio_effect  = in_add_audio_effect;
    in->stream.common.remove_audio_effect = in_remove_audio_effect;
    in->stream.set_gain = in_set_gain;
    in->stream.read = in_read;
    in->stream.get_input_frames_lost = in_get_input_frames_lost;
    in->stream.get_capture_position = in_get_capture_position;
#if ANDROID_SDK_VERSION >= 28
    in->stream.get_active_microphones = in_get_active_microphones;
    in->stream.set_microphone_direction = in_set_microphone_direction;
    in->stream.set_microphone_field_dimension = in_set_microphone_field_dimension;
#endif

    in->requested_rate    = config->sample_rate;
    in->requested_format  = config->format;
    in->requested_channel = channel_count;
    in->requested_channel_mask = config->channel_mask;
    in->device  = devices & ~AUDIO_DEVICE_BIT_IN;

    ALOGW("In channels %d, rate %d, devices 0x%x", channel_count, config->sample_rate, devices);
    memcpy(&in->config, &pcm_config_mm_in, sizeof(pcm_config_mm_in));
    //in->config.channels = channel_count;
    //in->config.rate     = *sample_rate;
    /*fix to 2 channel,  caused by the wm8958 driver*/

    in->main_channels = config->channel_mask;

    in->address = strdup(address);
    in->dev = adev;
    in->standby = 1;

    in->handle = handle;

    pthread_mutex_lock(&adev->lock);
    list_add_tail(&adev->in_streams, &in->stream_node);
    pthread_mutex_unlock(&adev->lock);

    *stream_in = &in->stream;

    return 0;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                   struct audio_stream_in *stream)
{
    struct imx_stream_in *in = (struct imx_stream_in *)stream;
    struct imx_audio_device *adev = (struct imx_audio_device *) dev;

    in_standby(&stream->common);

    if (in->read_buf)
        free(in->read_buf);

    if (in->resampler) {
        release_resampler(in->resampler);
    }
    if (in->proc_buf_in)
        free(in->proc_buf_in);
    if (in->proc_buf_out)
        free(in->proc_buf_out);
    if (in->ref_buf)
        free(in->ref_buf);
    if (in->address)
        free(in->address);

    pthread_mutex_lock(&adev->lock);
    list_remove(&in->stream_node);
    pthread_mutex_unlock(&adev->lock);
    free(stream);
    return;
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    if ((device == NULL) || (fd < 0))
        return 0;

    struct imx_audio_device *adev = (struct imx_audio_device *)device;

    dprintf(fd, "Hal dev dump\n");
    dprintf(fd, "audio card num %d\n", adev->audio_card_num);
    for (int i = 0; i < adev->audio_card_num && i < MAX_AUDIO_CARD_NUM; i++) {
        struct audio_card *acard = adev->card_list[i];
        if (acard)
            dprintf(fd, "card index %d, name %s, id %d\n", i, acard->driver_name,acard->card);
    }

    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct imx_audio_device *adev = (struct imx_audio_device *)device;
    int i;
    for(i = 0; i < adev->audio_card_num; i++)
        if(adev->mixer[i])
            mixer_close(adev->mixer[i]);

    if (adev->out_bus_stream_map) {
        hashmapFree(adev->out_bus_stream_map);
    }

    free(device);

    release_all_cards();

    return 0;
}

static int adev_get_format_for_device(struct imx_audio_device *adev, uint32_t devices, unsigned int flag)
{
     int i;
     if (flag == PCM_OUT) {
         for (i = 0; i < adev->audio_card_num; i ++) {
                   if (adev->card_list[i]->supported_out_devices & devices)
		       return adev->card_list[i]->out_format;
         }
     } else {
         for (i = 0; i < adev->audio_card_num; i ++) {
                   if (adev->card_list[i]->supported_in_devices & devices)
		       return adev->card_list[i]->in_format;
         }
     }
     return 0;
}

/* On imx8q board, we prefer use audio board(cs42888) as speaker and mic */
static void adjust_card_sequence(struct imx_audio_device *adev)
{
    int cardIdx;
    struct audio_card *pcard_wm8960 = NULL;
    struct audio_card *pcard_cs42888 = NULL;
    struct mixer *pmixer_wm8960 = NULL;
    struct mixer *pmixer_cs42888 = NULL;
    int idx_wm8960 = -1;
    int idx_cs42888 = -1;

    if((adev == NULL) || (adev->audio_card_num == 0))
        return;

    if(!strstr(adev->device_name, "mek_8q"))
        return;

    for(cardIdx = 0; cardIdx < adev->audio_card_num; cardIdx++) {
        if(strstr(adev->card_list[cardIdx]->driver_name, "wm8960")) {
            pcard_wm8960 = adev->card_list[cardIdx];
            pmixer_wm8960 = adev->mixer[cardIdx];
            idx_wm8960 = cardIdx;
        }
        else if(strstr(adev->card_list[cardIdx]->driver_name, "cs42888")) {
            pcard_cs42888 = adev->card_list[cardIdx];
            pmixer_cs42888 = adev->mixer[cardIdx];
            idx_cs42888 = cardIdx;
        }
    }

    if((pcard_wm8960 == NULL) || (pcard_cs42888 == NULL))
        return;

    if(idx_cs42888 < idx_wm8960)
        return;

    adev->card_list[idx_wm8960] = pcard_cs42888;
    adev->card_list[idx_cs42888] = pcard_wm8960;
    adev->mixer[idx_wm8960] = pmixer_cs42888;
    adev->mixer[idx_cs42888] = pmixer_wm8960;

    return;
}

static void audio_card_refine_config(struct audio_card *audio_card, struct imx_audio_device *adev)
{
    if (!audio_card || !adev)
        return;

    int card = audio_card->card;

    if(strstr(audio_card->driver_name, "sco-audio"))
        g_hsp_chns = 1;

    if(audio_card->support_multi_chn) {
        ALOGI("%s support multichannel", audio_card->driver_name);
        adev->support_multichannel = true;
    }

    if(audio_card->support_lpa) {
        ALOGI("%s support lpa", audio_card->driver_name);
        adev->support_lpa = true;
    }

    // The default period size:count is 192:8. On some cards/platforms(rpmsg/imx7ulp), it's too small.
    // Will Underrun due to the producer (MonoPipe::write) is schduled at lower frequency.
    // Fix me, pcm_config_mm_out will impact all the cards on one board.
    if(audio_card->out_period_size > 0) {
        ALOGI("%s, set out period_size to %d", audio_card->driver_name, audio_card->out_period_size);
        pcm_config_mm_out.period_size = audio_card->out_period_size;
    }

    if(audio_card->out_period_count > 0) {
        ALOGI("%s, set out period_count to %d", audio_card->driver_name, audio_card->out_period_count);
        pcm_config_mm_out.period_count = audio_card->out_period_count;
    }

    if (audio_card->in_period_size > 0) {
        ALOGI("%s, set in period_size to %d", audio_card->driver_name, audio_card->in_period_size);
        pcm_config_mm_in.period_size = audio_card->in_period_size;
    }

    if (audio_card->in_period_count > 0) {
        ALOGI("%s, set in period_count to %d", audio_card->driver_name, audio_card->in_period_count);
        pcm_config_mm_in.period_count = audio_card->in_period_count;
    }

    int format = PCM_FORMAT_S16_LE;
    if (pcm_check_param_mask(card, 0, PCM_IN, PCM_HW_PARAM_FORMAT, format)) {
        audio_card->in_format = format;
    } else {
        format = PCM_FORMAT_S24_LE;
        if (pcm_check_param_mask(card, 0, PCM_IN, PCM_HW_PARAM_FORMAT, format)) {
            audio_card->in_format = format;
        } else {
            format = PCM_FORMAT_S32_LE;
            if (pcm_check_param_mask(card, 0, PCM_IN, PCM_HW_PARAM_FORMAT, format)) {
                audio_card->in_format = format;
            } else {
                audio_card->in_format = PCM_FORMAT_S16_LE;
                ALOGE("No supported formats: S16_LE, S24_LE and S32_LE, use default one: S16_LE");
            }
        }
    }
    ALOGW("Supported input format %d", audio_card->in_format);

    return;
}

// Scan all cards listed in /proc/asound/cards
static int scan_available_device(struct imx_audio_device *adev)
{
    struct mixer *mixer;
    const char *card_name;
    int card = 0;
    int card_num = 0;

    for (card = 0; card < MAX_AUDIO_CARD_NUM; card++) {
        mixer = mixer_open(card);
        if (!mixer) {
            ALOGV("%s: Failed to open mixer for card%d", __func__, card);
            continue;
        }
        card_name = mixer_get_name(mixer);
        ALOGI("card%d: %s", card, card_name);

        struct audio_card *audio_card = audio_card_get_by_name(card_name);
        if (!audio_card) {
            ALOGV("%s: card %s is not supported", __func__, card_name);
            continue;
        }
        audio_card->card = card;
        adev->card_list[card_num] = audio_card;
        adev->mixer[card_num] = mixer;
        card_num++;

        audio_card_refine_config(audio_card, adev);
    }
    adev->audio_card_num = card_num;

    adjust_card_sequence(adev);

    ALOGI("Total %d cards match", card_num);
    for(int cardIdx = 0; cardIdx < card_num; cardIdx++) {
        ALOGI("card idx %d, name %s", cardIdx, adev->card_list[cardIdx]->driver_name);
    }

    /*must have one card*/
    if(!adev->card_list[0]) {
        ALOGE("no supported sound card found, aborting.");
        return  -EINVAL;
    }

    return 0;
}

/* copied from libcutils/str_parms.c */
static bool str_eq(void *key_a, void *key_b) {
    return !strcmp((const char *)key_a, (const char *)key_b);
}

/**
 * use djb hash unless we find it inadequate.
 * copied from libcutils/str_parms.c
 */
#ifdef __clang__
__attribute__((no_sanitize("integer")))
#endif
static int str_hash_fn(void *str) {
    uint32_t hash = 5381;
    char *p;
    for (p = (char *)str; p && *p; p++) {
        hash = ((hash << 5) + hash) + *p;
    }
    return (int)hash;
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct imx_audio_device *adev;
    int ret = 0;
    int i;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = (struct imx_audio_device *)calloc(1, sizeof(struct imx_audio_device));
    if (!adev)
        return -ENOMEM;

    adev->hw_device.common.tag      = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version  = AUDIO_DEVICE_API_VERSION_3_0;
    adev->hw_device.common.module   = (struct hw_module_t *) module;
    adev->hw_device.common.close    = adev_close;

    adev->hw_device.init_check              = adev_init_check;
    adev->hw_device.set_voice_volume        = adev_set_voice_volume;
    adev->hw_device.set_master_volume       = adev_set_master_volume;
    adev->hw_device.set_mode                = adev_set_mode;
    adev->hw_device.set_mic_mute            = adev_set_mic_mute;
    adev->hw_device.get_mic_mute            = adev_get_mic_mute;
    adev->hw_device.set_parameters          = adev_set_parameters;
    adev->hw_device.get_parameters          = adev_get_parameters;
    adev->hw_device.get_input_buffer_size   = adev_get_input_buffer_size;
    adev->hw_device.open_output_stream      = adev_open_output_stream;
    adev->hw_device.close_output_stream     = adev_close_output_stream;
    adev->hw_device.open_input_stream       = adev_open_input_stream;
    adev->hw_device.close_input_stream      = adev_close_input_stream;
#if ANDROID_SDK_VERSION >= 28
    adev->hw_device.get_microphones         = adev_get_microphones;
#endif
    adev->hw_device.set_audio_port_config   = adev_set_audio_port_config;
    adev->hw_device.create_audio_patch      = adev_create_audio_patch;
    adev->hw_device.release_audio_patch     = adev_release_audio_patch;
    adev->hw_device.get_audio_port          = adev_get_audio_port;
    adev->hw_device.dump                    = adev_dump;
    adev->support_multichannel              = false;
    adev->support_lpa                       = false;
    adev->primary_output                    = NULL;

    memset(adev->device_name, 0, sizeof(adev->device_name));
    property_get(PRODUCT_DEVICE_PROPERTY, adev->device_name,
            DEFAULT_ERROR_NAME_str);

    parse_all_cards();

    ret = scan_available_device(adev);
    if (ret != 0) {
        free(adev);
        return ret;
    }

    /* Set the default route before the PCM stream is opened */
    pthread_mutex_lock(&adev->lock);
    for(i = 0; i < adev->audio_card_num; i++)
        set_route_by_array(adev->mixer[i], adev->card_list[i]->init_ctl, 1);
    adev->mode    = AUDIO_MODE_NORMAL;
    adev->out_device = AUDIO_DEVICE_OUT_SPEAKER;
    adev->in_device  = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;
    select_output_device(adev);

    adev->next_patch_handle = AUDIO_PATCH_HANDLE_NONE;
    list_init(&adev->out_streams);
    list_init(&adev->in_streams);

    adev->voice_volume  = 1.0f;
    adev->tty_mode      = TTY_MODE_OFF;

    pthread_mutex_unlock(&adev->lock);

    *device = &adev->hw_device.common;

    lpa_enable = property_get_int32("vendor.audio.lpa.enable", 0);

    // Initialize the bus address to output stream map
    adev->out_bus_stream_map = hashmapCreate(5, str_hash_fn, str_eq);

    return 0;
}

static struct hw_module_methods_t hal_module_methods = {
    .open = adev_open,
};

struct audio_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = AUDIO_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = AUDIO_HARDWARE_MODULE_ID,
        .name = "NXP i.MX Audio HW HAL",
        .author = "The Android Open Source Project",
        .methods = &hal_module_methods,
    },
};


