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

#define LOG_TAG "audio_hw_primary"
//#define LOG_NDEBUG 0

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <stdlib.h>

#include <cutils/log.h>
#include <cutils/str_parms.h>
#include <cutils/properties.h>

#include <hardware/hardware.h>
#include <system/audio.h>
#include <hardware/audio.h>

#include <tinyalsa/asoundlib.h>
#include <audio_utils/resampler.h>
#include <audio_utils/echo_reference.h>
#include <hardware/audio_effect.h>
#include <audio_effects/effect_aec.h>

#include "audio_hardware.h"
#include "config_wm8962.h"
#include "config_wm8958.h"
#include "config_hdmi.h"
#include "config_nullcard.h"
#include "config_spdif.h"
#include "config_cs42888.h"
#include "config_wm8960.h"
#include "config_sii902x.h"
#include "config_sgtl5000.h"
#include "config_tc358743.h"

#ifdef BRILLO
#define PCM_HW_PARAM_ACCESS 0
#define PCM_HW_PARAM_FORMAT 1
#define PCM_HW_PARAM_SUBFORMAT 2
#define PCM_HW_PARAM_FIRST_MASK PCM_HW_PARAM_ACCESS
#define PCM_HW_PARAM_LAST_MASK PCM_HW_PARAM_SUBFORMAT
#define PCM_HW_PARAM_SAMPLE_BITS 8
#define PCM_HW_PARAM_FRAME_BITS 9
#define PCM_HW_PARAM_CHANNELS 10
#define PCM_HW_PARAM_RATE 11
#define PCM_HW_PARAM_PERIOD_TIME 12
#define PCM_HW_PARAM_PERIOD_SIZE 13
#define PCM_HW_PARAM_PERIOD_BYTES 14
#define PCM_HW_PARAM_PERIODS 15
#define PCM_HW_PARAM_BUFFER_TIME 16
#define PCM_HW_PARAM_BUFFER_SIZE 17
#define PCM_HW_PARAM_BUFFER_BYTES 18
#define PCM_HW_PARAM_TICK_TIME 19
#define PCM_HW_PARAM_FIRST_INTERVAL PCM_HW_PARAM_SAMPLE_BITS
#define PCM_HW_PARAM_LAST_INTERVAL PCM_HW_PARAM_TICK_TIME
#define PCM_HW_PARAMS_NORESAMPLE (1<<0)
#endif


/* ALSA ports for IMX */
#define PORT_MM     0
#define PORT_MM2_UL 0
#define PORT_SPDIF  6 /*not used*/
#define PORT_HDMI   0

/*align the definition in kernel for hdmi audio*/
#define HDMI_PERIOD_SIZE       192
#define PLAYBACK_HDMI_PERIOD_COUNT      8

#define ESAI_PERIOD_SIZE       192
#define PLAYBACK_ESAI_PERIOD_COUNT      8

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
#define CAPTURE_PERIOD_SIZE  192
/* number of periods for capture */
#define CAPTURE_PERIOD_COUNT 16
/* minimum sleep time in out_write() when write threshold is not reached */
#define MIN_WRITE_SLEEP_US 5000

#define DEFAULT_OUT_SAMPLING_RATE 44100

/* sampling rate when using MM low power port */
#define MM_LOW_POWER_SAMPLING_RATE  44100
/* sampling rate when using MM full power port */
#define MM_FULL_POWER_SAMPLING_RATE 44100

#define MM_USB_AUDIO_IN_RATE   16000

/* product-specific defines */
#define PRODUCT_DEVICE_PROPERTY "ro.product.device"
#define PRODUCT_NAME_PROPERTY   "ro.product.name"
#define PRODUCT_DEVICE_IMX      "imx"
#define PRODUCT_DEVICE_AUTO     "sabreauto"
#define PROPERTY_HDMI_IN	"persist.audio.hdmi_in"
#define PROPERTY_HDMI_OUT	"persist.audio.hdmi_out"

/*"null_card" must be in the end of this array*/
struct audio_card *audio_card_list[] = {
    &wm8958_card,
    &wm8962_card,
    &hdmi_card,
    /* &usbaudio_card, */
    &spdif_card,
    &cs42888_card,
    &wm8960_card,
    &sii902x_card,
    &sgtl5000_card,
    &tc358743_card,
    &null_card,
};

#define SUPPORT_CARD_NUM        ARRAY_SIZE(audio_card_list)

struct pcm_config pcm_config_mm_out = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = LONG_PERIOD_SIZE,
    .period_count = PLAYBACK_LONG_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .avail_min = 0,
};

struct pcm_config pcm_config_hdmi_multi = {
    .channels = 8, /* changed when the stream is opened */
    .rate = MM_FULL_POWER_SAMPLING_RATE, /* changed when the stream is opened */
    .period_size = HDMI_PERIOD_SIZE,
    .period_count = PLAYBACK_HDMI_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .avail_min = 0,
};
struct pcm_config pcm_config_esai_multi = {
    .channels = 8, /* changed when the stream is opened */
    .rate = MM_FULL_POWER_SAMPLING_RATE, /* changed when the stream is opened */
    .period_size = ESAI_PERIOD_SIZE,
    .period_count = PLAYBACK_ESAI_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .avail_min = 0,
};

struct pcm_config pcm_config_mm_in = {
    .channels = 2,
    .rate = MM_FULL_POWER_SAMPLING_RATE,
    .period_size = CAPTURE_PERIOD_SIZE,
    .period_count = CAPTURE_PERIOD_COUNT,
    .format = PCM_FORMAT_S16_LE,
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
static int scan_available_device(struct imx_audio_device *adev, bool queryInput, bool queryOutput);
static int get_next_buffer(struct resampler_buffer_provider *buffer_provider,
                                   struct resampler_buffer* buffer);
static void release_buffer(struct resampler_buffer_provider *buffer_provider,
                                  struct resampler_buffer* buffer);
static int adev_get_rate_for_device(struct imx_audio_device *adev, uint32_t devices, unsigned int flag);
static int adev_get_channels_for_device(struct imx_audio_device *adev, uint32_t devices, unsigned int flag);
static int adev_get_format_for_device(struct imx_audio_device *adev, uint32_t devices, unsigned int flag);
static void in_update_aux_channels(struct imx_stream_in *in, effect_handle_t effect);
static int pcm_read_wrapper(struct pcm *pcm, const void * buffer, size_t bytes);

/* Returns true on devices that are sabreauto, false otherwise */
static int is_device_auto(void)
{
    char property[PROPERTY_VALUE_MAX];

    property_get(PRODUCT_DEVICE_PROPERTY, property, "");

    /* return true if the property matches the given value */
    return strstr(property, PRODUCT_DEVICE_AUTO) != NULL;
}

static int convert_record_data(void *src, void *dst, unsigned int frames, bool bit_24b_2_16b, bool mono2stereo, bool stereo2mono)
{
     unsigned int i;
     short *dst_t = (short *)dst;
     if (bit_24b_2_16b && mono2stereo && !stereo2mono) {
        int data;
        int *src_t = (int *)src;
        for(i = 0; i < frames; i++)
        {
            data   = *src_t++;
            *dst_t++ = (short)(data >> 8);
            *dst_t++ = (short)(data >> 8);
        }
     }

     if (bit_24b_2_16b && !mono2stereo && stereo2mono) {
        int data1=0, data2=0;
        int *src_t = (int *)src;
        for(i = 0; i < frames; i++)
        {
            data1   = *src_t++;
            data2   = *src_t++;
            *dst_t++ = (short)(((data1 << 8) >> 17) + ((data2 << 8) >> 17));
        }
     }

     if (bit_24b_2_16b && !mono2stereo && !stereo2mono) {
        int data1, data2;
        int *src_t = (int *)src;
        for(i = 0; i < frames; i++)
        {
            data1   = *src_t++;
            data2   = *src_t++;
            *dst_t++ = (short)(data1 >> 8);
            *dst_t++ = (short)(data2 >> 8);
        }
     }

     if (!bit_24b_2_16b && mono2stereo && !stereo2mono ) {
        short data;
        short *src_t = (short *)src;
        for(i = 0; i < frames; i++)
        {
            data   = *src_t++;
            *dst_t++ = data;
            *dst_t++ = data;
        }
     }

     if (!bit_24b_2_16b && !mono2stereo && stereo2mono) {
        short data1, data2;
        short *src_t = (short *)src;
        for(i = 0; i < frames; i++)
        {
            data1   = *src_t++;
            data2   = *src_t++;
            *dst_t++ = (data1 >> 1) + (data2 >> 1);
        }
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
            ALOGW("Can't set mixer ctl value for missing control %s", route[i].ctl_name);
            i++;
            continue;
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



static void force_all_standby(struct imx_audio_device *adev)
{
    struct imx_stream_in *in;
    struct imx_stream_out *out;
    int i;

    for(i = 0; i < OUTPUT_TOTAL; i++)
        if (adev->active_output[i]) {
            out = adev->active_output[i];
            pthread_mutex_lock(&out->lock);
            do_output_standby(out, true);
            pthread_mutex_unlock(&out->lock);
        }

    if (adev->active_input) {
        in = adev->active_input;
        pthread_mutex_lock(&in->lock);
        do_input_standby(in);
        pthread_mutex_unlock(&in->lock);
    }
}

static void select_mode(struct imx_audio_device *adev)
{
    if (adev->mode == AUDIO_MODE_IN_CALL) {
        ALOGW("Entering IN_CALL state, in_call=%d", adev->in_call);
        if (!adev->in_call) {
            force_all_standby(adev);
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
            force_all_standby(adev);
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
    int earpiece_on;
    int bt_on;
    bool tty_volume = false;
    unsigned int channel;
    int i;

    headset_on      = adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADSET;
    headphone_on    = adev->out_device & AUDIO_DEVICE_OUT_WIRED_HEADPHONE;
    speaker_on      = adev->out_device & AUDIO_DEVICE_OUT_SPEAKER;
    earpiece_on     = adev->out_device & AUDIO_DEVICE_OUT_EARPIECE;
    bt_on           = adev->out_device & AUDIO_DEVICE_OUT_ALL_SCO;

    /* force rx path according to TTY mode when in call */
    if (adev->mode == AUDIO_MODE_IN_CALL && !bt_on) {
        switch(adev->tty_mode) {
            case TTY_MODE_FULL:
            case TTY_MODE_VCO:
                /* rx path to headphones */
                headphone_on = 1;
                headset_on = 0;
                speaker_on = 0;
                earpiece_on = 0;
                tty_volume = true;
                break;
            case TTY_MODE_HCO:
                /* rx path to device speaker */
                headphone_on = 0;
                headset_on = 0;
                speaker_on = 1;
                earpiece_on = 0;
                break;
            case TTY_MODE_OFF:
            default:
                /* force speaker on when in call and HDMI is selected as voice DL audio
                 * cannot be routed to HDMI by ABE */
                if (adev->out_device & AUDIO_DEVICE_OUT_AUX_DIGITAL)
                    speaker_on = 1;
                break;
        }
    }
    /*if mode = AUDIO_MODE_IN_CALL*/
    ALOGV("headphone %d ,headset %d ,speaker %d, earpiece %d, \n", headphone_on, headset_on, speaker_on, earpiece_on);
    /* select output stage */
    for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
        set_route_by_array(adev->mixer[i], adev->card_list[i]->bt_output, bt_on);
    for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
        set_route_by_array(adev->mixer[i], adev->card_list[i]->hs_output, headset_on | headphone_on);
    for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
        set_route_by_array(adev->mixer[i], adev->card_list[i]->speaker_output, speaker_on);
    for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
        set_route_by_array(adev->mixer[i], adev->card_list[i]->earpiece_output, earpiece_on);

    /* Special case: select input path if in a call, otherwise
       in_set_parameters is used to update the input route
       todo: use sub mic for handsfree case */
    if (adev->mode == AUDIO_MODE_IN_CALL) {
        if (bt_on)
            for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
                set_route_by_array(adev->mixer[i], adev->card_list[i]->vx_bt_mic_input, bt_on);
        else {
            /* force tx path according to TTY mode when in call */
            switch(adev->tty_mode) {
                case TTY_MODE_FULL:
                case TTY_MODE_HCO:
                    /* tx path from headset mic */
                    headphone_on = 0;
                    headset_on = 1;
                    speaker_on = 0;
                    earpiece_on = 0;
                    break;
                case TTY_MODE_VCO:
                    /* tx path from device sub mic */
                    headphone_on = 0;
                    headset_on = 0;
                    speaker_on = 1;
                    earpiece_on = 0;
                    break;
                case TTY_MODE_OFF:
                default:
                    break;
            }

            if (headset_on)
                for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
                    set_route_by_array(adev->mixer[i], adev->card_list[i]->vx_hs_mic_input, 1);
            else if (headphone_on || earpiece_on || speaker_on)
                for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
                    set_route_by_array(adev->mixer[i], adev->card_list[i]->vx_main_mic_input, 1);
            else
                for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
                    set_route_by_array(adev->mixer[i], adev->card_list[i]->vx_main_mic_input, 0);
        }
    }
}

static void select_input_device(struct imx_audio_device *adev)
{
    int i;
    int headset_on = 0;
    int main_mic_on = 0;
    int sub_mic_on = 0;
    int bt_on = adev->in_device & AUDIO_DEVICE_IN_ALL_SCO;

    if (!bt_on) {
        if ((adev->mode != AUDIO_MODE_IN_CALL) && (adev->active_input != 0)) {
            /* sub mic is used for camcorder or VoIP on speaker phone */
            sub_mic_on = (adev->active_input->source == AUDIO_SOURCE_CAMCORDER) ||
                         ((adev->out_device & AUDIO_DEVICE_OUT_SPEAKER) &&
                          (adev->active_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION));
        }

        headset_on = adev->in_device & AUDIO_DEVICE_IN_WIRED_HEADSET;
        main_mic_on = adev->in_device & AUDIO_DEVICE_IN_BUILTIN_MIC;
    }

   /* TODO: check how capture is possible during voice calls or if
    * both use cases are mutually exclusive.
    */
    if (bt_on)
        for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
            set_route_by_array(adev->mixer[i], adev->card_list[i]->mm_bt_mic_input, 1);
    else {
        /* Select front end */
        if (headset_on)
            for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
                set_route_by_array(adev->mixer[i], adev->card_list[i]->mm_hs_mic_input, 1);
        else if (main_mic_on || sub_mic_on)
            for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
                set_route_by_array(adev->mixer[i], adev->card_list[i]->mm_main_mic_input, 1);
        else
            for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
                set_route_by_array(adev->mixer[i], adev->card_list[i]->mm_main_mic_input, 0);
    }
}

static int get_card_for_device(struct imx_audio_device *adev, int device, unsigned int flag, int *card_index)
{
    int i;
    int card = -1;
    char property[PROPERTY_VALUE_MAX];
    property_get(PROPERTY_HDMI_OUT, property, "");

    /* use low bit of persist.audio.hdmi_out property (will be zero if not set) */
    #define FORCE_HDMI_OUT() (property[0]&1)
    if (flag == PCM_OUT ) {
        for(i = 0; i < MAX_AUDIO_CARD_NUM; i++) {
            struct audio_card *thiscard = adev->card_list[i];
            if((thiscard->supported_out_devices & device)
	       &&
	       (!FORCE_HDMI_OUT() || strstr(thiscard->name, "hdmi"))) {
                  card = thiscard->card;
                  break;
            }
        }
    } else {
        for(i = 0; i < MAX_AUDIO_CARD_NUM; i++) {
            if(adev->card_list[i]->supported_in_devices & device) {
                  card = adev->card_list[i]->card;
                  break;
            }
        }
    }
    if (card_index != NULL)
        *card_index = i;
    return card;
}
/* must be called with hw device and output stream mutexes locked */
static int start_output_stream_primary(struct imx_stream_out *out)
{
    struct imx_audio_device *adev = out->dev;
    unsigned int card = -1;
    unsigned int port = 0;
    int i;
    int pcm_device;
    bool success = false;

    ALOGI("start_output_stream_primary... %d, device %d",(uintptr_t)out, out->device);

    if (adev->mode != AUDIO_MODE_IN_CALL) {
        /* FIXME: only works if only one output can be active at a time */
        select_output_device(adev);
    }

    pcm_device = out->device & (AUDIO_DEVICE_OUT_ALL & ~AUDIO_DEVICE_OUT_AUX_DIGITAL);
    if (pcm_device && (adev->active_output[OUTPUT_ESAI] == NULL || adev->active_output[OUTPUT_ESAI]->standby)) {
        out->write_flags[PCM_NORMAL]            = PCM_OUT | PCM_MMAP | PCM_MONOTONIC;
        out->write_threshold[PCM_NORMAL]        = PLAYBACK_LONG_PERIOD_COUNT * LONG_PERIOD_SIZE;
        out->config[PCM_NORMAL] = pcm_config_mm_out;


        card = get_card_for_device(adev, pcm_device, PCM_OUT, &out->card_index);
        out->pcm[PCM_NORMAL] = pcm_open(card, port,out->write_flags[PCM_NORMAL], &out->config[PCM_NORMAL]);
        ALOGW("card %d, port %d device 0x%x", card, port, out->device);
        ALOGW("rate %d, channel %d period_size 0x%x", out->config[PCM_NORMAL].rate, out->config[PCM_NORMAL].channels, out->config[PCM_NORMAL].period_size);
        success = true;
    }

    pcm_device = out->device & AUDIO_DEVICE_OUT_AUX_DIGITAL;
    if(pcm_device && (adev->active_output[OUTPUT_HDMI] == NULL || adev->active_output[OUTPUT_HDMI]->standby)) {
        out->write_flags[PCM_HDMI]            = PCM_OUT | PCM_MONOTONIC;
        out->write_threshold[PCM_HDMI]        = HDMI_PERIOD_SIZE * PLAYBACK_HDMI_PERIOD_COUNT;
        out->config[PCM_HDMI] = pcm_config_mm_out;
        card = get_card_for_device(adev, pcm_device, PCM_OUT, &out->card_index);
        out->pcm[PCM_HDMI] = pcm_open(card, port,out->write_flags[PCM_HDMI], &out->config[PCM_HDMI]);
        ALOGW("card %d, port %d device 0x%x", card, port, out->device);
        ALOGW("rate %d, channel %d period_size 0x%x", out->config[PCM_HDMI].rate, out->config[PCM_HDMI].channels, out->config[PCM_HDMI].period_size);
        success = true;
    }
    /* default to low power: will be corrected in out_write if necessary before first write to
     * tinyalsa.
     */
    out->low_power   = 0;
    /* Close any PCMs that could not be opened properly and return an error */
    for (i = 0; i < PCM_TOTAL; i++) {
        if (out->pcm[i] && !pcm_is_ready(out->pcm[i])) {
            ALOGE("cannot open pcm_out driver %d: %s", i, pcm_get_error(out->pcm[i]));
            pcm_close(out->pcm[i]);
            out->pcm[i] = NULL;
            success = false;
        }
    }

    if (success) {
        out->buffer_frames = pcm_config_mm_out.period_size * 2;
        if (out->buffer == NULL)
            out->buffer = malloc(out->buffer_frames * audio_stream_frame_size(&out->stream.common));

        if (adev->echo_reference != NULL)
            out->echo_reference = adev->echo_reference;

        for(i = 0; i < PCM_TOTAL; i++) {
            if (out->resampler[i])
                out->resampler[i]->reset(out->resampler[i]);
        }

        return 0;
    }

    return -ENOMEM;
}

static int start_output_stream_hdmi(struct imx_stream_out *out)
{
    struct imx_audio_device *adev = out->dev;
    unsigned int card = -1;
    unsigned int port = 0;
    int i = 0;

    ALOGI("start_output_stream_hdmi, out %d, device 0x%x", (uintptr_t)out, out->device);
    /* force standby on low latency output stream to close HDMI driver in case it was in use */
    if (adev->active_output[OUTPUT_PRIMARY] != NULL &&
            !adev->active_output[OUTPUT_PRIMARY]->standby) {
        struct imx_stream_out *p_out = adev->active_output[OUTPUT_PRIMARY];
        pthread_mutex_lock(&p_out->lock);
        do_output_standby(p_out, true);
        pthread_mutex_unlock(&p_out->lock);
    }

    card = get_card_for_device(adev, out->device & AUDIO_DEVICE_OUT_AUX_DIGITAL, PCM_OUT, &out->card_index);
    ALOGW("card %d, port %d device 0x%x", card, port, out->device);
    ALOGW("rate %d, channel %d period_size 0x%x", out->config[PCM_HDMI].rate, out->config[PCM_HDMI].channels, out->config[PCM_HDMI].period_size);

    out->pcm[PCM_HDMI] = pcm_open(card, port, PCM_OUT | PCM_MONOTONIC, &out->config[PCM_HDMI]);

    if (out->pcm[PCM_HDMI] && !pcm_is_ready(out->pcm[PCM_HDMI])) {
        ALOGE("cannot open pcm_out driver: %s", pcm_get_error(out->pcm[PCM_HDMI]));
        pcm_close(out->pcm[PCM_HDMI]);
        out->pcm[PCM_HDMI] = NULL;
        return -ENOMEM;
    }
    return 0;
}

static int start_output_stream_esai(struct imx_stream_out *out)
{
    struct imx_audio_device *adev = out->dev;
    unsigned int card = -1;
    unsigned int port = 0;
    int i = 0;

    ALOGI("start_output_stream_esai, out %d, device 0x%x", (uintptr_t)out, out->device);
    /* force standby on low latency output stream to close HDMI driver in case it was in use */
    if (adev->active_output[OUTPUT_PRIMARY] != NULL &&
            !adev->active_output[OUTPUT_PRIMARY]->standby) {
        struct imx_stream_out *p_out = adev->active_output[OUTPUT_PRIMARY];
        pthread_mutex_lock(&p_out->lock);
        do_output_standby(p_out, true);
        pthread_mutex_unlock(&p_out->lock);
    }

    card = get_card_for_device(adev, out->device & AUDIO_DEVICE_OUT_SPEAKER, PCM_OUT, &out->card_index);
    ALOGW("card %d, port %d device 0x%x", card, port, out->device);
    ALOGW("rate %d, channel %d period_size 0x%x", out->config[PCM_ESAI].rate, out->config[PCM_ESAI].channels, out->config[PCM_ESAI].period_size);

    out->pcm[PCM_ESAI] = pcm_open(card, port, PCM_OUT | PCM_MONOTONIC, &out->config[PCM_ESAI]);

    if (out->pcm[PCM_ESAI] && !pcm_is_ready(out->pcm[PCM_ESAI])) {
        ALOGE("cannot open pcm_out driver: %s", pcm_get_error(out->pcm[PCM_ESAI]));
        pcm_close(out->pcm[PCM_ESAI]);
        out->pcm[PCM_ESAI] = NULL;
        return -ENOMEM;
    }
    out->written = 0;
    return 0;
}

static int check_input_parameters(uint32_t sample_rate, int format, int channel_count)
{
    if (format != AUDIO_FORMAT_PCM_16_BIT)
        return -EINVAL;

    if ((channel_count < 1) || (channel_count > 2))
        return -EINVAL;

    switch(sample_rate) {
    case 8000:
    case 11025:
    case 16000:
    case 22050:
    case 24000:
    case 32000:
    case 44100:
    case 48000:
        break;
    default:
        return -EINVAL;
    }

    return 0;
}

static size_t get_input_buffer_size(uint32_t sample_rate, int format, int channel_count)
{
    size_t size;
    size_t device_rate;

    if (check_input_parameters(sample_rate, format, channel_count) != 0)
        return 0;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size = (pcm_config_mm_in.period_size * sample_rate) / pcm_config_mm_in.rate;
    size = ((size + 15) / 16) * 16;

    ALOGW("get_input_buffer_size size = %d, channel_count = %d",size,channel_count);
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

        if (adev->active_output[OUTPUT_PRIMARY] != NULL &&
             !adev->active_output[OUTPUT_PRIMARY]->standby )
                remove_echo_reference(adev->active_output[OUTPUT_PRIMARY], reference);
        release_echo_reference(reference);
        adev->echo_reference = NULL;
    }
}

static struct echo_reference_itfe *get_echo_reference(struct imx_audio_device *adev,
                                               audio_format_t format,
                                               uint32_t channel_count,
                                               uint32_t sampling_rate)
{
    put_echo_reference(adev, adev->echo_reference);
    /*only for mixer output, only one output*/
    if (adev->active_output[OUTPUT_PRIMARY] != NULL &&
             !adev->active_output[OUTPUT_PRIMARY]->standby){
        struct audio_stream *stream = &adev->active_output[OUTPUT_PRIMARY]->stream.common;
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
            add_echo_reference(adev->active_output[OUTPUT_PRIMARY], adev->echo_reference);
    }

    return adev->echo_reference;
}

static int get_playback_delay(struct imx_stream_out *out,
                       size_t frames,
                       struct echo_reference_buffer *buffer)
{
    size_t kernel_frames;
    int status;
    int primary_pcm = 0;
    struct imx_audio_device *adev = out->dev;

    /* Find the first active PCM to act as primary */
    for (primary_pcm = 0; primary_pcm < PCM_TOTAL; primary_pcm++) {
        if (out->pcm[primary_pcm]) {
            status = pcm_get_htimestamp(out->pcm[primary_pcm], &kernel_frames, &buffer->time_stamp);
            if (status < 0) {
                buffer->time_stamp.tv_sec  = 0;
                buffer->time_stamp.tv_nsec = 0;
                buffer->delay_ns           = 0;
                ALOGV("get_playback_delay(): pcm_get_htimestamp error,"
                        "setting playbackTimestamp to 0");
                return status;
            }

            kernel_frames = pcm_get_buffer_size(out->pcm[primary_pcm]) - kernel_frames;

            /* adjust render time stamp with delay added by current driver buffer.
             * Add the duration of current frame as we want the render time of the last
             * sample being written. */
            buffer->delay_ns = (long)(((int64_t)(kernel_frames + frames)* 1000000000)/
                                    adev->mm_rate);

            return 0;
        }
    }
    return -1;
}

static uint32_t out_get_sample_rate(const struct audio_stream *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    return pcm_config_mm_out.rate;
}

static uint32_t out_get_sample_rate_hdmi(const struct audio_stream *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    return out->config[PCM_HDMI].rate;
}

static uint32_t out_get_sample_rate_esai(const struct audio_stream *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    return out->config[PCM_ESAI].rate;
}

static int out_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    ALOGW("out_set_sample_rate %d", rate);
    return 0;
}

static size_t out_get_buffer_size_primary(const struct audio_stream *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    struct imx_audio_device *adev = out->dev;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size_t size = (pcm_config_mm_out.period_size * adev->default_rate) / pcm_config_mm_out.rate;
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_frame_size((struct audio_stream *)stream);
}

static size_t out_get_buffer_size_hdmi(const struct audio_stream *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size_t size = pcm_config_hdmi_multi.period_size;
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_frame_size((struct audio_stream *)stream);
}

static size_t out_get_buffer_size_esai(const struct audio_stream *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;

    /* take resampling into account and return the closest majoring
    multiple of 16 frames, as audioflinger expects audio buffers to
    be a multiple of 16 frames */
    size_t size = pcm_config_esai_multi.period_size;
    size = ((size + 15) / 16) * 16;
    return size * audio_stream_frame_size((struct audio_stream *)stream);
}

static uint32_t out_get_channels(const struct audio_stream *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    return out->channel_mask;
}

static audio_format_t out_get_format(const struct audio_stream *stream)
{
    return AUDIO_FORMAT_PCM_16_BIT;
}

static int out_set_format(struct audio_stream *stream, audio_format_t format)
{
    ALOGW("out_set_format %d", format);
    return 0;
}

/* must be called with hw device and output stream mutexes locked */
static int do_output_standby(struct imx_stream_out *out, int force_standby)
{
    struct imx_audio_device *adev = out->dev;
    int i;

    if (!force_standby && !strcmp(adev->card_list[out->card_index]->driver_name, "wm8962-audio")) {
        ALOGW("no standby");
        return 0;
    }
    if (!out->standby) {

        for (i = 0; i < PCM_TOTAL; i++) {
            if (out->pcm[i]) {
                pcm_close(out->pcm[i]);
                out->pcm[i] = NULL;
            }

            out->writeContiFailCount[i] = 0;
        }

        ALOGW("do_out_standby... %d",(uintptr_t)out);

        /* if in call, don't turn off the output stage. This will
        be done when the call is ended */
        if (adev->mode != AUDIO_MODE_IN_CALL) {
            /* FIXME: only works if only one output can be active at a time */
        }

        /* stop writing to echo reference */
        if (out->echo_reference != NULL) {
            out->echo_reference->write(out->echo_reference, NULL);
            out->echo_reference = NULL;
        }

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
    return 0;
}

static int out_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    struct imx_audio_device *adev = out->dev;
    struct imx_stream_in *in;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret, val = 0;
    bool force_input_standby = false;
    bool out_is_active = false;
    int  i;

    parms = str_parms_create_str(kvpairs);

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_STREAM_ROUTING, value, sizeof(value));
    if (ret >= 0) {
        val = atoi(value);
        pthread_mutex_lock(&adev->lock);
        pthread_mutex_lock(&out->lock);

        if (adev->out_device != val) {
            if (out == adev->active_output[OUTPUT_PRIMARY] && !out->standby) {
                /* a change in output device may change the microphone selection */
                if (adev->active_input &&
                        adev->active_input->source == AUDIO_SOURCE_VOICE_COMMUNICATION) {
                    force_input_standby = true;
                }
                /* force standby if moving to/from HDMI */
                if (((val & AUDIO_DEVICE_OUT_AUX_DIGITAL) ^
                        (adev->out_device & AUDIO_DEVICE_OUT_AUX_DIGITAL)) ||
                        ((val & AUDIO_DEVICE_OUT_SPEAKER) ^
                        (adev->out_device & AUDIO_DEVICE_OUT_SPEAKER)) ||
                        (adev->mode == AUDIO_MODE_IN_CALL)) {
                        ALOGI("out_set_parameters, old 0x%x, new 0x%x do_output_standby", adev->out_device, val);
                    do_output_standby(out, true);
                }
            }
            if ((out != adev->active_output[OUTPUT_HDMI]) && val) {
                adev->out_device = val;
                out->device    = val;

                select_output_device(adev);
            }
        }
        pthread_mutex_unlock(&out->lock);
        if (force_input_standby) {
            in = adev->active_input;
            pthread_mutex_lock(&in->lock);
            do_input_standby(in);
            pthread_mutex_unlock(&in->lock);
        }
        pthread_mutex_unlock(&adev->lock);

        ret = 0;
    }

    ALOGW("out_set_parameters %s, ret %d, out %d",kvpairs, ret, (uintptr_t) out);
    str_parms_destroy(parms);

    return ret;
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
        str = strdup(str_parms_to_str(reply));
        checked = true;
    }

    ret = str_parms_get_str(query, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, value, sizeof(value));
    if (ret >= 0) {
        value[0] = '\0';
        i = 0;
        if (str != NULL)
            free(str);
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
        str = strdup(str_parms_to_str(reply));
        checked = true;
    }

    if (!checked) {
        str = strdup(keys);
    }

    ALOGW("out get parameters query %s, reply %s",str_parms_to_str(query), str_parms_to_str(reply));
    str_parms_destroy(query);
    str_parms_destroy(reply);
    return str;
}

static uint32_t out_get_latency_primary(const struct audio_stream_out *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;

    return (pcm_config_mm_out.period_size * pcm_config_mm_out.period_count * 1000) / pcm_config_mm_out.rate;
}

static uint32_t out_get_latency_hdmi(const struct audio_stream_out *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;

    return (pcm_config_hdmi_multi.period_size * pcm_config_hdmi_multi.period_count * 1000) / pcm_config_hdmi_multi.rate;
}

static uint32_t out_get_latency_esai(const struct audio_stream_out *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;

    return (pcm_config_esai_multi.period_size * pcm_config_esai_multi.period_count * 1000) / pcm_config_esai_multi.rate;
}

static int out_set_volume(struct audio_stream_out *stream, float left,
                          float right)
{
    return -ENOSYS;
}

static int pcm_read_convert(struct imx_stream_in *in, struct pcm *pcm, void *data, unsigned int count)
{
    bool bit_24b_2_16b = false;
    bool mono2stereo = false;
    bool stereo2mono = false;
    size_t frames_rq = count / audio_stream_frame_size(&in->stream.common);

    if (in->config.format == PCM_FORMAT_S24_LE && in->requested_format == PCM_FORMAT_S16_LE) bit_24b_2_16b = true;
    if (in->config.channels == 2 && in->requested_channel == 1) stereo2mono = true;
    if (in->config.channels == 1 && in->requested_channel == 2) mono2stereo = true;

    if (bit_24b_2_16b || mono2stereo || stereo2mono) {
        size_t size_in_bytes_tmp = pcm_frames_to_bytes(in->pcm, frames_rq);
        if (in->read_tmp_buf_size < in->config.period_size) {
            in->read_tmp_buf_size = in->config.period_size;
            in->read_tmp_buf = (int32_t *) realloc(in->read_tmp_buf, size_in_bytes_tmp);
            ALOG_ASSERT((in->read_tmp_buf != NULL),
                        "get_next_buffer() failed to reallocate read_tmp_buf");
            ALOGV("get_next_buffer(): read_tmp_buf %p extended to %d bytes",
                     in->read_tmp_buf, size_in_bytes_tmp);
        }

        in->read_status = pcm_read_wrapper(pcm, (void*)in->read_tmp_buf, size_in_bytes_tmp);

        if (in->read_status != 0) {
            ALOGE("get_next_buffer() pcm_read_wrapper error %d", in->read_status);
            return in->read_status;
        }
        convert_record_data((void *)in->read_tmp_buf, (void *)data, frames_rq, bit_24b_2_16b, mono2stereo, stereo2mono);
    }
    else {
        in->read_status = pcm_read_wrapper(pcm, (void*)data, count);
    }

    return in->read_status;
}

static int pcm_read_wrapper(struct pcm *pcm, const void * buffer, size_t bytes)
{
    int ret = 0;
    ret = pcm_read(pcm, (void *)buffer, bytes);

    if(ret !=0) {
         ALOGV("ret %d, pcm read %d error %s.", ret, bytes, pcm_get_error(pcm));

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

    return ret;
}

static int pcm_write_wrapper(struct pcm *pcm, const void * buffer, size_t bytes, int flags)
{
    int ret = 0;
    if(flags & PCM_MMAP)
         ret = pcm_mmap_write(pcm, (void *)buffer, bytes);
    else
         ret = pcm_write(pcm, (void *)buffer, bytes);

    if(ret !=0) {
         ALOGW("ret %d, pcm write %d error %s", ret, bytes, pcm_get_error(pcm));

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

    return ret;
}

static ssize_t out_write_primary(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret;
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    struct imx_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_frame_size(&out->stream.common);
    size_t in_frames = bytes / frame_size;
    size_t out_frames = in_frames;
    bool force_input_standby = false;
    struct imx_stream_in *in;
    int i;
    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        ret = start_output_stream_primary(out);
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

    /* only use resampler if required */
    for (i = 0; i < PCM_TOTAL; i++) {
        /* only use resampler if required */
        if (out->pcm[i] && out->resampler[i] && (out->config[i].rate != adev->default_rate)) {
            out_frames = out->buffer_frames;
            out->resampler[i]->resample_from_input(out->resampler[i],
                                                (int16_t *)buffer,
                                                &in_frames,
                                                (int16_t *)out->buffer,
                                                &out_frames);
            break;
        }
    }

    if (out->echo_reference != NULL) {
        struct echo_reference_buffer b;
        b.raw = (void *)buffer;
        b.frame_count = in_frames;

        get_playback_delay(out, out_frames, &b);
        out->echo_reference->write(out->echo_reference, &b);
    }
    /* do not allow more than out->write_threshold frames in kernel pcm driver buffer */
    /* Write to all active PCMs */
    for (i = 0; i < PCM_TOTAL; i++) {
        if (out->pcm[i]) {
            if (out->config[i].rate == adev->default_rate) {
                /* PCM uses native sample rate */
                ret = pcm_write_wrapper(out->pcm[i], (void *)buffer, bytes, out->write_flags[i]);
            } else {
                /* PCM needs resampler */
                ret = pcm_write_wrapper(out->pcm[i], (void *)out->buffer, out_frames * frame_size, out->write_flags[i]);
            }

            if (ret) {
                out->writeContiFailCount[i]++;
                break;
            } else {
                out->writeContiFailCount[i] = 0;
            }
        }
   }

    //If continue fail, probably th fd is invalid.
    for (i = 0; i < PCM_TOTAL; i++) {
        if(out->writeContiFailCount[i] > 100) {
            ALOGW("pcm_write_wrapper continues failed for pcm %d, standby", i);
            do_output_standby(out, true);
            break;
        }
    }

exit:
    out->written += bytes / frame_size;
    pthread_mutex_unlock(&out->lock);

    if (ret != 0) {
        ALOGV("write error, sleep few ms");
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
               out_get_sample_rate(&stream->common));
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

static ssize_t out_write_hdmi(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret;
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    struct imx_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_frame_size(&out->stream.common);
    size_t in_frames = bytes / frame_size;

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        ret = start_output_stream_hdmi(out);
        if (ret != 0) {
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
        out->standby = 0;
    }
    pthread_mutex_unlock(&adev->lock);

    /* do not allow more than out->write_threshold frames in kernel pcm driver buffer */

    ret = pcm_write_wrapper(out->pcm[PCM_HDMI], (void *)buffer, bytes, out->write_flags[PCM_HDMI]);

exit:
    out->written += bytes / frame_size;
    pthread_mutex_unlock(&out->lock);

    if (ret != 0) {
        ALOGV("write error, sleep few ms");
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
               out_get_sample_rate(&stream->common));
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

static ssize_t out_write_esai(struct audio_stream_out *stream, const void* buffer,
                         size_t bytes)
{
    int ret;
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    struct imx_audio_device *adev = out->dev;
    size_t frame_size = audio_stream_frame_size(&out->stream.common);
    size_t in_frames = bytes / frame_size;

    /* acquiring hw device mutex systematically is useful if a low priority thread is waiting
     * on the output stream mutex - e.g. executing select_mode() while holding the hw device
     * mutex
     */
    pthread_mutex_lock(&adev->lock);
    pthread_mutex_lock(&out->lock);
    if (out->standby) {
        ret = start_output_stream_esai(out);
        if (ret != 0) {
            pthread_mutex_unlock(&adev->lock);
            goto exit;
        }
        out->standby = 0;
    }
    pthread_mutex_unlock(&adev->lock);

    /* do not allow more than out->write_threshold frames in kernel pcm driver buffer */

    convert_output_for_esai(buffer, bytes, out->config[PCM_ESAI].channels);
    ret = pcm_write_wrapper(out->pcm[PCM_ESAI], (void *)buffer, bytes, out->write_flags[PCM_ESAI]);

exit:
    out->written += bytes / frame_size;
    pthread_mutex_unlock(&out->lock);

    if (ret != 0) {
        ALOGV("write error, sleep few ms");
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
               out_get_sample_rate(&stream->common));
    }

    return bytes;
}

static int out_get_render_position(const struct audio_stream_out *stream,
                                   uint32_t *dsp_frames)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    struct imx_audio_device *adev = out->dev;
    struct timespec timestamp;
    int64_t signed_frames = 0;
    int i;
    pthread_mutex_lock(&out->lock);

    for (i = 0; i < PCM_TOTAL; i++)
        if (out->pcm[i]) {
            size_t avail;
            size_t kernel_buffer_size = out->config[i].period_size * out->config[i].period_count;
            // this is the number of frames which the dsp actually presented at least
            signed_frames = out->written - kernel_buffer_size * adev->default_rate / out->config[i].rate;
            if (pcm_get_htimestamp(out->pcm[i], &avail, &timestamp) == 0) {
                // compensate for driver's frames consumed
                signed_frames += avail * adev->default_rate / out->config[i].rate;
                break;
            }
        }
   if (signed_frames >= 0)
       *dsp_frames = (uint32_t)signed_frames;
   else
       *dsp_frames = 0;

    pthread_mutex_unlock(&out->lock);

    return 0;
}

static int out_add_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_remove_audio_effect(const struct audio_stream *stream, effect_handle_t effect)
{
    return 0;
}

static int out_get_presentation_position(const struct audio_stream_out *stream,
                                   uint64_t *frames, struct timespec *timestamp)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    struct imx_audio_device *adev = out->dev;
    int ret = -1;
    int i;

    pthread_mutex_lock(&out->lock);

    for (i = 0; i < PCM_TOTAL; i++)
        if (out->pcm[i]) {
            size_t avail;
            if (pcm_get_htimestamp(out->pcm[i], &avail, timestamp) == 0) {
                size_t kernel_buffer_size = out->config[i].period_size * out->config[i].period_count;
		/*Actually we have no case for adev->default_rate != out->config[i].rate */
                int64_t signed_frames = out->written - (kernel_buffer_size - avail) * adev->default_rate / out->config[i].rate;

                if (signed_frames >= 0) {
                    *frames = signed_frames;
                    ret = 0;
                }
                break;
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
    int i = adev->in_card_idx;
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
    unsigned int card = -1;
    unsigned int port = 0;
    struct mixer *mixer;
    int rate = 0, channels = 0, format = 0;
    char property[PROPERTY_VALUE_MAX];
    property_get(PROPERTY_HDMI_IN, property, "");

    /* use low bit of persist.audio.hdmi_in property */
    #define FORCE_HDMI_IN() (property[0]&1)

    ALOGW("start_input_stream....");

    adev->active_input = in;
    if (adev->mode != AUDIO_MODE_IN_CALL) {
        adev->in_device = in->device;
        select_input_device(adev);
    }

    for(i = 0; i < MAX_AUDIO_CARD_NUM; i++) {
        if ((adev->in_device & adev->card_list[i]->supported_in_devices) &&
	   (!FORCE_HDMI_IN() || strstr(adev->card_list[i]->name, "tc358743"))) {
            card = adev->card_list[i]->card;
            adev->in_card_idx = i;
            port = 0;
            break;
        }
        if(i == MAX_AUDIO_CARD_NUM-1) {
            ALOGE("can not find supported device for %d",in->device);
            return -EINVAL;
        }
    }

    /*Error handler for usb mic plug in/plug out when recording. */
    memcpy(&in->config, &pcm_config_mm_in, sizeof(pcm_config_mm_in));

    in->config.stop_threshold = in->config.period_size * in->config.period_count;

    if (in->device & AUDIO_DEVICE_IN_AUX_DIGITAL) {
        format     = adev_get_format_for_device(adev, in->device, PCM_IN);
        in->config.format  = format;
    }

    ALOGW("card %d, port %d device 0x%x", card, port, in->device);
    ALOGW("rate %d, channel %d format %d, period_size 0x%x", in->config.rate, in->config.channels, 
                                 in->config.format, in->config.period_size);

    if (in->need_echo_reference && in->echo_reference == NULL)
        in->echo_reference = get_echo_reference(adev,
                                        AUDIO_FORMAT_PCM_16_BIT,
                                        in->requested_channel,
                                        in->requested_rate);

    /* this assumes routing is done previously */
    in->pcm = pcm_open(card, port, PCM_IN, &in->config);
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

static int in_set_sample_rate(struct audio_stream *stream, uint32_t rate)
{
    return 0;
}

static size_t in_get_buffer_size(const struct audio_stream *stream)
{
    struct imx_stream_in *in = (struct imx_stream_in *)stream;

    return get_input_buffer_size(in->requested_rate,
                                 AUDIO_FORMAT_PCM_16_BIT,
                                 in->requested_channel);
}

static uint32_t in_get_channels(const struct audio_stream *stream)
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
    switch(in->requested_format) {
    case PCM_FORMAT_S16_LE:
         return AUDIO_FORMAT_PCM_16_BIT;
    case PCM_FORMAT_S32_LE:
         return AUDIO_FORMAT_PCM_32_BIT;
    case PCM_FORMAT_S24_LE:
         return AUDIO_FORMAT_PCM_8_24_BIT;
    default:
         return AUDIO_FORMAT_PCM_16_BIT;
    }

}

static int in_set_format(struct audio_stream *stream, audio_format_t format)
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
        in->last_time_of_xrun = 0;

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
    return 0;
}

static int in_set_parameters(struct audio_stream *stream, const char *kvpairs)
{
    struct imx_stream_in *in = (struct imx_stream_in *)stream;
    struct imx_audio_device *adev = in->dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret, val = 0;
    bool do_standby = false;

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
        val = atoi(value) & ~AUDIO_DEVICE_BIT_IN;
        if ((in->device != val) && (val != 0)) {
            in->device = val;
            do_standby = true;
            in_update_aux_channels(in, NULL);
        }
    }

    if (do_standby)
        do_input_standby(in);

    pthread_mutex_unlock(&in->lock);
    pthread_mutex_unlock(&adev->lock);

    ALOGW("in_set_parameters %s", kvpairs);
    str_parms_destroy(parms);

    return 0;
}

static char * in_get_parameters(const struct audio_stream *stream,
                                const char *keys)
{
    return strdup("");
}

static int in_set_gain(struct audio_stream_in *stream, float gain)
{
    return 0;
}

static void get_capture_delay(struct imx_stream_in *in,
                       size_t frames,
                       struct echo_reference_buffer *buffer)
{

    /* read frames available in kernel driver buffer */
    size_t kernel_frames;
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
         " kernel_delay:[%ld], buf_delay:[%ld], rsmp_delay:[%ld], kernel_frames:[%d], "
         "in->read_buf_frames:[%d], in->proc_buf_frames:[%d], frames:[%d]",
         buffer->time_stamp.tv_sec , buffer->time_stamp.tv_nsec, buffer->delay_ns,
         kernel_delay, buf_delay, rsmp_delay, kernel_frames,
         in->read_buf_frames, in->proc_buf_frames, frames);

}

static int32_t update_echo_reference(struct imx_stream_in *in, size_t frames)
{
    struct echo_reference_buffer b;
    b.delay_ns = 0;

    ALOGV("update_echo_reference, frames = [%d], in->ref_frames_in = [%d],  "
          "b.frame_count = [%d]",
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
            ALOGV("update_echo_reference: in->ref_frames_in:[%d], "
                    "in->ref_buf_size:[%d], frames:[%d], b.frame_count:[%d]",
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
        size_t size_in_bytes = in->config.period_size * audio_stream_frame_size(&in->stream.common);
        if (in->read_buf_size < in->config.period_size) {
            in->read_buf_size = in->config.period_size;
            in->read_buf = (int16_t *) realloc(in->read_buf, size_in_bytes);
            ALOG_ASSERT((in->read_buf != NULL),
                        "get_next_buffer() failed to reallocate read_buf");
            ALOGV("get_next_buffer(): read_buf %p extended to %d bytes",
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
                            frames_wr * audio_stream_frame_size(&in->stream.common)),
                    &frames_rd);
        } else {
            struct resampler_buffer buf = {
                    { raw : NULL, },
                    frame_count : frames_rd,
            };
            get_next_buffer(&in->buf_provider, &buf);
            if (buf.raw != NULL) {
                memcpy((char *)buffer +
                           frames_wr * audio_stream_frame_size(&in->stream.common),
                        buf.raw,
                        buf.frame_count * audio_stream_frame_size(&in->stream.common));
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
                ALOGV("process_frames(): proc_buf_in %p extended to %d bytes",
                     in->proc_buf_in,in->proc_buf_size * in->requested_channel * sizeof(int16_t));
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
            ALOGE("preprocessing produced too many frames: %d + %d  > %d !",
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
    size_t frames_rq = bytes / audio_stream_frame_size(&stream->common);

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
            in->mute_500ms = in->requested_rate * audio_stream_frame_size(&stream->common)/2;
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
    if (ret < 0) {
        memset(buffer, 0, bytes);
        usleep(bytes * 1000000 / audio_stream_frame_size(&stream->common) /
               in_get_sample_rate(&stream->common));
    }
    pthread_mutex_unlock(&in->lock);

    return bytes;
}

static uint32_t in_get_input_frames_lost(struct audio_stream_in *stream)
{
    int times, diff;
    struct imx_stream_in *in = (struct imx_stream_in *)stream;
    if (in->pcm == NULL)  return 0;
#ifdef BRILLO
    times = 0;
#else
    times = pcm_get_time_of_xrun(in->pcm);
#endif
    diff = times - in->last_time_of_xrun;
    ALOGW_IF((diff != 0), "in_get_input_frames_lost %d ms total %d ms\n",diff, times);
    in->last_time_of_xrun = times;
    return diff * in->requested_rate / 1000;
}

#define GET_COMMAND_STATUS(status, fct_status, cmd_status) \
            do {                                           \
                if (fct_status != 0)                       \
                    status = fct_status;                   \
                else if (cmd_status != 0)                  \
                    status = cmd_status;                   \
            } while(0)

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

static void in_read_audio_effect_channel_configs(struct imx_stream_in *in,
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


static uint32_t in_get_aux_channels(struct imx_stream_in *in)
{
    int i;
    channel_config_t new_chcfg = {0, 0};

    if (in->num_preprocessors == 0)
        return 0;

    /* do not enable dual mic configurations when capturing from other microphones than
     * main or sub */
    if (!(in->device & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC)))
        return 0;

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
    uint32_t aux_channels;
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
        aux_channels = 0;
        channel_config.aux_channels = 0;
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

static int out_read_hdmi_channel_masks(struct imx_audio_device *adev, struct imx_stream_out *out) {

    int count = 0;
    int sup_channels[MAX_SUP_CHANNEL_NUM]; //temp buffer for supported channels
    int card = -1;
    int i = 0;
    int j = 0;
    struct mixer *mixer_hdmi = NULL;

    for (i = 0; i < MAX_AUDIO_CARD_NUM; i ++) {
         if(!strcmp(adev->card_list[i]->driver_name, hdmi_card.driver_name)) {
             mixer_hdmi = adev->mixer[i];
             card = adev->card_list[i]->card;
             break;
         }
    }

    if (mixer_hdmi) {
        struct mixer_ctl *ctl;
        ctl = mixer_get_ctl_by_name(mixer_hdmi, "HDMI Support Channels");
        if (ctl) {
            count = mixer_ctl_get_num_values(ctl);
            for(i = 0; i < count; i ++) {
                sup_channels[i] = mixer_ctl_get_value(ctl, i);
                ALOGW("out_read_hdmi_channel_masks() card %d got %d sup channels", card, sup_channels[i]);
            }
        }
    }

    /*when channel is 6, the mask is 5.1,when channel is 8, the mask is 7.1*/
    for(i = 0; i < count; i++ ) {
       if(sup_channels[i] == 2) {
          out->sup_channel_masks[j]   = AUDIO_CHANNEL_OUT_STEREO;
          j++;
       }
       if(sup_channels[i] == 6) {
          out->sup_channel_masks[j]   = AUDIO_CHANNEL_OUT_5POINT1;
          j++;
       }
       if(sup_channels[i] == 8) {
          out->sup_channel_masks[j]   = AUDIO_CHANNEL_OUT_7POINT1;
          j++;
       }
    }
    /*if HDMI device does not support 2,6,8 channels, then return error*/
    if (j == 0) return -ENOSYS;

    return 0;
}

static int out_read_hdmi_rates(struct imx_audio_device *adev, struct imx_stream_out *out) {

    int count = 0;
    int card = -1;
    int i = 0;
    struct mixer *mixer_hdmi = NULL;

    for (i = 0; i < MAX_AUDIO_CARD_NUM; i ++) {
         if(!strcmp(adev->card_list[i]->driver_name, hdmi_card.driver_name)) {
             mixer_hdmi = adev->mixer[i];
             card = adev->card_list[i]->card;
             break;
         }
    }

    if (mixer_hdmi) {
        struct mixer_ctl *ctl;
        ctl = mixer_get_ctl_by_name(mixer_hdmi, "HDMI Support Rates");
        if (ctl) {
            count = mixer_ctl_get_num_values(ctl);
            for(i = 0; i < count; i ++) {
                out->sup_rates[i] = mixer_ctl_get_value(ctl, i);
                ALOGW("out_read_hdmi_rates() card %d got %d sup rates", card, out->sup_rates[i]);
            }
        }
    }

    return 0;
}

static int adev_open_output_stream(struct audio_hw_device *dev,
                                   audio_io_handle_t handle,
                                   audio_devices_t devices,
                                   audio_output_flags_t flags,
                                   struct audio_config *config,
                                   struct audio_stream_out **stream_out)
{
    struct imx_audio_device *ladev = (struct imx_audio_device *)dev;
    struct imx_stream_out *out;
    int ret;
    int output_type;
    int i;

    ALOGW("open output stream devices %d, format %d, channels %d, sample_rate %d, flag %d",
                        devices, config->format, config->channel_mask, config->sample_rate, flags);

    out = (struct imx_stream_out *)calloc(1, sizeof(struct imx_stream_out));
    if (!out)
        return -ENOMEM;

    out->sup_rates[0] = ladev->mm_rate;
    out->sup_channel_masks[0] = AUDIO_CHANNEL_OUT_STEREO;
    out->channel_mask = AUDIO_CHANNEL_OUT_STEREO;

    if (flags & AUDIO_OUTPUT_FLAG_DIRECT &&
                   devices == AUDIO_DEVICE_OUT_AUX_DIGITAL) {
        ALOGW("adev_open_output_stream() HDMI multichannel");
        if (ladev->active_output[OUTPUT_HDMI] != NULL) {
            ret = -ENOSYS;
            goto err_open;
        }
        ret = out_read_hdmi_channel_masks(ladev, out);
        if (ret != 0)
            goto err_open;

        ret = out_read_hdmi_rates(ladev, out);

        output_type = OUTPUT_HDMI;
        if (config->sample_rate == 0)
            config->sample_rate = ladev->mm_rate;
        if (config->channel_mask == 0)
            config->channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
        out->channel_mask = config->channel_mask;
        out->stream.common.get_buffer_size = out_get_buffer_size_hdmi;
        out->stream.common.get_sample_rate = out_get_sample_rate_hdmi;
        out->stream.get_latency = out_get_latency_hdmi;
        out->stream.write = out_write_hdmi;
        out->config[PCM_HDMI] = pcm_config_hdmi_multi;
        out->config[PCM_HDMI].rate = config->sample_rate;
        out->config[PCM_HDMI].channels = popcount(config->channel_mask);
    } else if (flags & AUDIO_OUTPUT_FLAG_DIRECT &&
                   devices == AUDIO_DEVICE_OUT_SPEAKER && ladev->device_is_auto) {
        ALOGW("adev_open_output_stream() ESAI multichannel");
        if (ladev->active_output[OUTPUT_ESAI] != NULL) {
            ret = -ENOSYS;
            goto err_open;
        }

        output_type = OUTPUT_ESAI;
        if (config->sample_rate == 0)
            config->sample_rate = ladev->mm_rate;
        if (config->channel_mask == 0)
            config->channel_mask = AUDIO_CHANNEL_OUT_5POINT1;
        out->channel_mask = config->channel_mask;
        out->stream.common.get_buffer_size = out_get_buffer_size_esai;
        out->stream.common.get_sample_rate = out_get_sample_rate_esai;
        out->stream.get_latency = out_get_latency_esai;
        out->stream.write = out_write_esai;
        out->config[PCM_ESAI] = pcm_config_esai_multi;
        out->config[PCM_ESAI].rate = config->sample_rate;
        out->config[PCM_ESAI].channels = popcount(config->channel_mask);
    } else {
        ALOGV("adev_open_output_stream() normal buffer");
        if (ladev->active_output[OUTPUT_PRIMARY] != NULL) {
            ret = -ENOSYS;
            goto err_open;
        }
        output_type = OUTPUT_PRIMARY;
        out->stream.common.get_buffer_size = out_get_buffer_size_primary;
        out->stream.common.get_sample_rate = out_get_sample_rate;
        out->stream.get_latency = out_get_latency_primary;
        out->stream.write = out_write_primary;
    }


    for(i = 0; i < PCM_TOTAL; i++) {
         ret = create_resampler(ladev->default_rate,
                               ladev->mm_rate,
                               2,
                               RESAMPLER_QUALITY_DEFAULT,
                               NULL,
                               &out->resampler[i]);
        if (ret != 0)
            goto err_open;
    }

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

    out->dev = ladev;
    out->standby = 1;
    out->device      = devices;

    /* FIXME: when we support multiple output devices, we will want to
     * do the following:
     * adev->devices &= ~AUDIO_DEVICE_OUT_ALL;
     * adev->devices |= out->device;
     * select_output_device(adev);
     * This is because out_set_parameters() with a route is not
     * guaranteed to be called after an output stream is opened. */

    config->format = out->stream.common.get_format(&out->stream.common);
    config->channel_mask = out->stream.common.get_channels(&out->stream.common);
    config->sample_rate = out->stream.common.get_sample_rate(&out->stream.common);

    *stream_out = &out->stream;
    ladev->active_output[output_type] = out;
    ALOGW("opened out stream...%d, type %d",(uintptr_t)out, output_type);
    return 0;

err_open:
    free(out);
    *stream_out = NULL;
    return ret;
}

static void adev_close_output_stream(struct audio_hw_device *dev,
                                     struct audio_stream_out *stream)
{
    struct imx_stream_out *out = (struct imx_stream_out *)stream;
    struct imx_audio_device *ladev = (struct imx_audio_device *)dev;
    int i;
    ALOGW("adev_close_output_stream...%d",(uintptr_t)out);

    pthread_mutex_lock(&out->dev->lock);
    pthread_mutex_lock(&out->lock);
    do_output_standby(out, true);
    pthread_mutex_unlock(&out->lock);
    pthread_mutex_unlock(&out->dev->lock);

    for (i = 0; i < OUTPUT_TOTAL; i++) {
        if (ladev->active_output[i] == out) {
            ladev->active_output[i] = NULL;
            break;
        }
    }

    if (out->buffer)
        free(out->buffer);

    for (i = 0; i < PCM_TOTAL; i++) {
        if (out->resampler[i]) {
            release_resampler(out->resampler[i]);
            out->resampler[i] = NULL;
        }
    }

    free(stream);
}

static int adev_set_parameters(struct audio_hw_device *dev, const char *kvpairs)
{
    struct imx_audio_device *adev = (struct imx_audio_device *)dev;
    struct str_parms *parms;
    char *str;
    char value[32];
    int ret;
    ALOGW("set parameters %s",kvpairs);
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
        else
            return -EINVAL;

        pthread_mutex_lock(&adev->lock);
        if (tty_mode != adev->tty_mode) {
            adev->tty_mode = tty_mode;
            if (adev->mode == AUDIO_MODE_IN_CALL)
                select_output_device(adev);
        }
        pthread_mutex_unlock(&adev->lock);
    }

    ret = str_parms_get_str(parms, AUDIO_PARAMETER_KEY_BT_NREC, value, sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->bluetooth_nrec = true;
        else
            adev->bluetooth_nrec = false;
    }

    ret = str_parms_get_str(parms, "screen_state", value, sizeof(value));
    if (ret >= 0) {
        if (strcmp(value, AUDIO_PARAMETER_VALUE_ON) == 0)
            adev->low_power = false;
        else
            adev->low_power = true;
    }

    str_parms_destroy(parms);

    return 0;
}

static char * adev_get_parameters(const struct audio_hw_device *dev,
                                  const char *keys)
{
    return strdup("");
}

static int adev_init_check(const struct audio_hw_device *dev)
{
    return 0;
}

static int adev_set_voice_volume(struct audio_hw_device *dev, float volume)
{
    struct imx_audio_device *adev = (struct imx_audio_device *)dev;

    adev->voice_volume = volume;

    return 0;
}

static int adev_set_master_volume(struct audio_hw_device *dev, float volume)
{
    return -ENOSYS;
}

static int adev_set_mode(struct audio_hw_device *dev, int mode)
{
    struct imx_audio_device *adev = (struct imx_audio_device *)dev;
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

static size_t adev_get_input_buffer_size(const struct audio_hw_device *dev,
                                         const struct audio_config *config)
{
    size_t size;
    int channel_count = popcount(config->channel_mask);
    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return 0;

    return get_input_buffer_size(config->sample_rate, config->format, channel_count);
}

static int adev_open_input_stream(struct audio_hw_device *dev,
                                  audio_io_handle_t handle,
                                  audio_devices_t devices,
                                  struct audio_config *config,
                                  struct audio_stream_in **stream_in)
{
    struct imx_audio_device *ladev = (struct imx_audio_device *)dev;
    struct imx_stream_in *in;
    int ret;
    int rate, channels;
    int channel_count = popcount(config->channel_mask);

    if (check_input_parameters(config->sample_rate, config->format, channel_count) != 0)
        return -EINVAL;

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

    in->requested_rate    = config->sample_rate;
    in->requested_format  = PCM_FORMAT_S16_LE;
    in->requested_channel = channel_count;
    in->device  = devices & ~AUDIO_DEVICE_BIT_IN;

    ALOGW("In channels %d, rate %d, devices 0x%x", channel_count, config->sample_rate, devices);
    memcpy(&in->config, &pcm_config_mm_in, sizeof(pcm_config_mm_in));
    //in->config.channels = channel_count;
    //in->config.rate     = *sample_rate;
    /*fix to 2 channel,  caused by the wm8958 driver*/

    in->main_channels = config->channel_mask;

    in->dev = ladev;
    in->standby = 1;

    *stream_in = &in->stream;

    return 0;
}

static void adev_close_input_stream(struct audio_hw_device *dev,
                                   struct audio_stream_in *stream)
{
    struct imx_stream_in *in = (struct imx_stream_in *)stream;

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

    free(stream);
    return;
}

static int adev_dump(const audio_hw_device_t *device, int fd)
{
    return 0;
}

static int adev_close(hw_device_t *device)
{
    struct imx_audio_device *adev = (struct imx_audio_device *)device;
    int i;
    for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
        if(adev->mixer[i])
            mixer_close(adev->mixer[i]);

    free(device);
    return 0;
}

static int adev_get_rate_for_device(struct imx_audio_device *adev, uint32_t devices, unsigned int flag)
{
     int i;
     if (flag == PCM_OUT) {
         for (i = 0; i < MAX_AUDIO_CARD_NUM; i ++) {
                   if (adev->card_list[i]->supported_out_devices & devices)
		       return adev->card_list[i]->out_rate;
         }
     } else {
         for (i = 0; i < MAX_AUDIO_CARD_NUM; i ++) {
                   if (adev->card_list[i]->supported_in_devices & devices)
		       return adev->card_list[i]->in_rate;
         }
     }
     return 0;
}

static int adev_get_channels_for_device(struct imx_audio_device *adev, uint32_t devices, unsigned int flag)
{
     int i;
     if (flag == PCM_OUT) {
         for (i = 0; i < MAX_AUDIO_CARD_NUM; i ++) {
                   if (adev->card_list[i]->supported_out_devices & devices)
		       return adev->card_list[i]->out_channels;
         }
     } else {
         for (i = 0; i < MAX_AUDIO_CARD_NUM; i ++) {
                   if (adev->card_list[i]->supported_in_devices & devices)
		       return adev->card_list[i]->in_channels;
         }
     }
     return 0;
}

static int adev_get_format_for_device(struct imx_audio_device *adev, uint32_t devices, unsigned int flag)
{
     int i;
     if (flag == PCM_OUT) {
         for (i = 0; i < MAX_AUDIO_CARD_NUM; i ++) {
                   if (adev->card_list[i]->supported_out_devices & devices)
		       return adev->card_list[i]->out_format;
         }
     } else {
         for (i = 0; i < MAX_AUDIO_CARD_NUM; i ++) {
                   if (adev->card_list[i]->supported_in_devices & devices)
		       return adev->card_list[i]->in_format;
         }
     }
     return 0;
}

static int pcm_get_near_param_wrap(unsigned int card, unsigned int device,
                     unsigned int flags, int type, int *data)
{
#ifdef BRILLO
    return 0;
#else
    return pcm_get_near_param(card, device, flags, type, data);
#endif
}

static int scan_available_device(struct imx_audio_device *adev, bool queryInput, bool queryOutput)
{
    int i,j,k;
    int m,n;
    bool found;
    bool scanned;
    struct control *imx_control;
    int left_out_devices = SUPPORTED_DEVICE_OUT_MODULE;
    int left_in_devices = SUPPORTED_DEVICE_IN_MODULE;
    int rate, channels, format;
    /* open the mixer for main sound card, main sound cara is like sgtl5000, wm8958, cs428888*/
    /* note: some platform do not have main sound card, only have auxiliary card.*/
    /* max num of supported card is 2 */
    k = adev->audio_card_num;
    for(i = 0; i < k; i++) {
        left_out_devices &= ~adev->card_list[i]->supported_out_devices;
        left_in_devices &= ~adev->card_list[i]->supported_in_devices;
    }

    for (i = 0; i < MAX_AUDIO_CARD_SCAN ; i ++) {
        found = false;
        imx_control = control_open(i);
        if(!imx_control)
            break;
        ALOGW("card %d, id %s ,driver %s, name %s", i, control_card_info_get_id(imx_control),
                                                      control_card_info_get_driver(imx_control),
                                                      control_card_info_get_name(imx_control));
        for(j = 0; j < (int)SUPPORT_CARD_NUM; j++) {
            if(strstr(control_card_info_get_driver(imx_control), audio_card_list[j]->driver_name) != NULL){

                //On 8dv, if period_size too small(176), when underrun,
                //the out_write_primary comsume 176 smaples quickly as only several us,
                //the producer (MonoPipe::write) is schduled at lower frequency,
                //and can't jump the loop. So the sii902x is always xrun.
                //One resolution is to calulate pcm_write interval, if too short for 10 times,
                //usleep 20ms, can fix the issue.
                //Here we enlarge period_size to avoid the issue.
                if(strcmp(audio_card_list[j]->driver_name, "sii902x-audio") == 0) {
                    ALOGI("sii902x audio, set period_size to 768");
                    pcm_config_mm_out.period_size = 768;
                }

                // check if the device have been scaned before
                scanned = false;
                n = k;
                for (m = 0; m < k; m++) {
                    if (!strcmp(audio_card_list[j]->driver_name, adev->card_list[m]->driver_name)) {
                         scanned = true;
                         found = true;
                    }
                }
                if (scanned) break;
                if(n >= MAX_AUDIO_CARD_NUM) {
                    break;
                }
                adev->card_list[n]  = audio_card_list[j];
                adev->card_list[n]->card = i;
                adev->mixer[n] = mixer_open(i);
                if (!adev->mixer[n]) {
                     ALOGE("Unable to open the mixer, aborting.");
                     control_close(imx_control);
                     return -EINVAL;
                }

                if(queryOutput) {
                    rate = 44100;
                    if( pcm_get_near_param_wrap(i, 0, PCM_OUT, PCM_HW_PARAM_RATE, &rate) == 0)
                            adev->card_list[n]->out_rate = rate;
                    ALOGW("out rate %d",adev->card_list[n]->out_rate);

                    if(adev->card_list[n]->out_rate > adev->mm_rate)
                        adev->mm_rate = adev->card_list[n]->out_rate;

                    channels = 2;
                    if( pcm_get_near_param_wrap(i, 0, PCM_OUT, PCM_HW_PARAM_CHANNELS, &channels) == 0)
                            adev->card_list[n]->out_channels = channels;
                }

                if(queryInput) {
                    rate = 44100;
                    if( pcm_get_near_param_wrap(i, 0, PCM_IN, PCM_HW_PARAM_RATE, &rate) == 0)
                            adev->card_list[n]->in_rate = rate;

                    channels = 1;
                    if( pcm_get_near_param_wrap(i, 0, PCM_IN, PCM_HW_PARAM_CHANNELS, &channels) == 0)
                            adev->card_list[n]->in_channels = channels;

                    format = PCM_FORMAT_S16_LE;

#ifdef BRILLO
                    adev->card_list[n]->in_format = format;
#else
                    if( pcm_check_param_mask(i, 0, PCM_IN, PCM_HW_PARAM_FORMAT, format))
                            adev->card_list[n]->in_format = format;
                    else {
                        format = PCM_FORMAT_S24_LE;
                        if( pcm_check_param_mask(i, 0, PCM_IN, PCM_HW_PARAM_FORMAT, format))
                            adev->card_list[n]->in_format = format;
                    }
#endif

                    ALOGW("in rate %d, channels %d format %d",adev->card_list[n]->in_rate, adev->card_list[n]->in_channels, adev->card_list[n]->in_format);
                }

                left_out_devices &= ~audio_card_list[j]->supported_out_devices;
                left_in_devices &= ~audio_card_list[j]->supported_in_devices;
                k ++;
                found = true;
                break;
            }
        }

        control_close(imx_control);
        if(!found){
            ALOGW("unrecognized card found.");
        }
    }
    adev->audio_card_num = k;
    /*must have one card*/
    if(!adev->card_list[0]) {
        ALOGE("no supported sound card found, aborting.");
        return  -EINVAL;
    }
    /*second card maybe null*/
    while (k < MAX_AUDIO_CARD_NUM) {
        adev->card_list[k]  = audio_card_list[SUPPORT_CARD_NUM-1];
        /*FIXME:This is workaround for some board which only have one card, whose supported device only is not full*/
        adev->card_list[k]->supported_out_devices  = left_out_devices;
        adev->card_list[k]->supported_in_devices  = left_in_devices;
        k++;
    }

    return 0;
}

static int adev_open(const hw_module_t* module, const char* name,
                     hw_device_t** device)
{
    struct imx_audio_device *adev;
    int ret = 0;
    int i,j,k;
    bool found;

    if (strcmp(name, AUDIO_HARDWARE_INTERFACE) != 0)
        return -EINVAL;

    adev = calloc(1, sizeof(struct imx_audio_device));
    if (!adev)
        return -ENOMEM;

    adev->hw_device.common.tag      = HARDWARE_DEVICE_TAG;
    adev->hw_device.common.version  = AUDIO_DEVICE_API_VERSION_2_0;
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
    adev->hw_device.dump                    = adev_dump;
    adev->mm_rate                           = 44100;

    ret = scan_available_device(adev, true, true);
    if (ret != 0) {
        free(adev);
        return ret;
    }

    adev->default_rate                      = adev->mm_rate;
    pcm_config_mm_out.rate                  = adev->mm_rate;
    pcm_config_mm_in.rate                   = adev->mm_rate;
    pcm_config_hdmi_multi.rate              = adev->mm_rate;
    pcm_config_esai_multi.rate              = adev->mm_rate;

    /* Set the default route before the PCM stream is opened */
    pthread_mutex_lock(&adev->lock);
    for(i = 0; i < MAX_AUDIO_CARD_NUM; i++)
        set_route_by_array(adev->mixer[i], adev->card_list[i]->defaults, 1);
    adev->mode    = AUDIO_MODE_NORMAL;
    adev->out_device = AUDIO_DEVICE_OUT_SPEAKER;
    adev->in_device  = AUDIO_DEVICE_IN_BUILTIN_MIC & ~AUDIO_DEVICE_BIT_IN;
    select_output_device(adev);

    adev->pcm_modem_dl  = NULL;
    adev->pcm_modem_ul  = NULL;
    adev->voice_volume  = 1.0f;
    adev->tty_mode      = TTY_MODE_OFF;
    adev->device_is_auto = is_device_auto();
    adev->bluetooth_nrec = true;
    adev->wb_amr = 0;
    pthread_mutex_unlock(&adev->lock);

    *device = &adev->hw_device.common;

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
        .name = "Freescale i.MX Audio HW HAL",
        .author = "The Android Open Source Project",
        .methods = &hal_module_methods,
    },
};


