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

#ifndef ANDROID_INCLUDE_IMX_AUDIO_HARDWARE_H
#define ANDROID_INCLUDE_IMX_AUDIO_HARDWARE_H

#include <stdlib.h>
#include <tinyalsa/asoundlib.h>

#include <hardware/hardware.h>


#define MIN(x, y) ((x) > (y) ? (y) : (x))

enum tty_modes {
    TTY_MODE_OFF,
    TTY_MODE_VCO,
    TTY_MODE_HCO,
    TTY_MODE_FULL
};

struct route_setting
{
    char *ctl_name;
    int intval;
    char *strval;
};


struct audio_card{
    char * name;
    char * driver_name;
    int  supported_devices;
    struct route_setting *defaults;
    struct route_setting *bt_output;
    struct route_setting *speaker_output;
    struct route_setting *hs_output;
    struct route_setting *earpiece_output;
    struct route_setting *vx_hs_mic_input;
    struct route_setting *mm_main_mic_input;
    struct route_setting *vx_main_mic_input;
    struct route_setting *mm_hs_mic_input;
    struct route_setting *vx_bt_mic_input;
    struct route_setting *mm_bt_mic_input;
    int  card;
    int  out_rate;
    int  in_rate;
};

#define MAX_AUDIO_CARD_NUM  3
#define MAX_AUDIO_CARD_SCAN 3

struct imx_audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    int mode;
    int devices;
    struct pcm *pcm_modem_dl;
    struct pcm *pcm_modem_ul;
    int in_call;
    float voice_volume;
    struct imx_stream_in  *active_input;                /*1 input stream, 2 outout stream*/
    struct imx_stream_out *active_output[MAX_AUDIO_CARD_NUM];
    bool mic_mute;
    int tty_mode;
    struct echo_reference_itfe *echo_reference;
    bool bluetooth_nrec;
    bool device_is_imx;
    int  wb_amr;
    bool low_power;
    struct audio_card *card_list[MAX_AUDIO_CARD_NUM];
    struct mixer *mixer[MAX_AUDIO_CARD_NUM];
    int out_stream_num;
    int audio_card_num;
};

struct imx_stream_out {
    struct audio_stream_out stream;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    struct resampler_itfe *resampler;
    char *buffer;
    int standby;
    struct echo_reference_itfe *echo_reference;
    struct imx_audio_device *dev;
    int write_threshold;
    bool low_power;
    int frame_count;
    int write_flags;
    int device;
    int out_id;
};

#define MAX_PREPROCESSORS 3 /* maximum one AGC + one NS + one AEC per input stream */

struct imx_stream_in {
    struct audio_stream_in stream;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    int device;
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    int16_t *buffer;
    size_t frames_in;
    unsigned int requested_rate;
    int standby;
    int source;
    struct echo_reference_itfe *echo_reference;
    bool need_echo_reference;
    effect_handle_t preprocessors[MAX_PREPROCESSORS];
    int num_preprocessors;
    int16_t *proc_buf;
    size_t proc_buf_size;
    size_t proc_frames_in;
    int16_t *ref_buf;
    size_t ref_buf_size;
    size_t ref_frames_in;
    int read_status;
    size_t mute_500ms;
    struct imx_audio_device *dev;
    int last_time_of_xrun;
};

#define SUPPORTED_DEVICE_IN_MODULE               \
          ( AUDIO_DEVICE_OUT_EARPIECE |          \
            AUDIO_DEVICE_OUT_SPEAKER |           \
            AUDIO_DEVICE_OUT_WIRED_HEADSET |     \
            AUDIO_DEVICE_OUT_WIRED_HEADPHONE |   \
            AUDIO_DEVICE_OUT_AUX_DIGITAL |       \
            AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET | \
            AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET | \
            AUDIO_DEVICE_OUT_ALL_SCO |           \
            AUDIO_DEVICE_OUT_DEFAULT |           \
            AUDIO_DEVICE_IN_COMMUNICATION |      \
            AUDIO_DEVICE_IN_AMBIENT |            \
            AUDIO_DEVICE_IN_BUILTIN_MIC |        \
            AUDIO_DEVICE_IN_WIRED_HEADSET |      \
            AUDIO_DEVICE_IN_AUX_DIGITAL |        \
            AUDIO_DEVICE_IN_BACK_MIC |           \
            AUDIO_DEVICE_IN_ALL_SCO |            \
            AUDIO_DEVICE_IN_DEFAULT )

#endif  /* ANDROID_INCLUDE_IMX_AUDIO_HARDWARE_H */
