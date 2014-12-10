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
/* Copyright (C) 2012-2014 Freescale Semiconductor, Inc. */

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

enum pcm_type {
    PCM_NORMAL = 0,
    PCM_HDMI,
    PCM_ESAI,
    PCM_TOTAL,
};

enum output_type {
    OUTPUT_DEEP_BUF,      // deep PCM buffers output stream
    OUTPUT_PRIMARY,   // low latency output stream
    OUTPUT_HDMI,
    OUTPUT_ESAI,
    OUTPUT_TOTAL
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
    int  supported_out_devices;
    int  supported_in_devices;
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
    unsigned int  out_rate;
    int  out_channels;
    int  out_format;
    unsigned int  in_rate;
    int  in_channels;
    int  in_format;
};

#define MAX_AUDIO_CARD_NUM  4
#define MAX_AUDIO_CARD_SCAN 4

#define MAX_SUP_CHANNEL_NUM  20
#define MAX_SUP_RATE_NUM     20

struct imx_audio_device {
    struct audio_hw_device hw_device;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    int mode;
    int in_device;
    int in_card_idx; /* the index for array card_list and mixer */
    int out_device;
    struct pcm *pcm_modem_dl;
    struct pcm *pcm_modem_ul;
    int in_call;
    float voice_volume;
    struct imx_stream_in  *active_input;                /*1 input stream, 2 outout stream*/
    struct imx_stream_out *active_output[OUTPUT_TOTAL];
    bool mic_mute;
    int tty_mode;
    struct echo_reference_itfe *echo_reference;
    bool bluetooth_nrec;
    bool device_is_auto;
    int  wb_amr;
    bool low_power;
    struct audio_card *card_list[MAX_AUDIO_CARD_NUM];
    struct mixer *mixer[MAX_AUDIO_CARD_NUM];
    int audio_card_num;
    unsigned int default_rate;               /*HAL input samplerate*/
    unsigned int mm_rate;                    /*HAL hardware output samplerate*/
    char usb_card_name[128];
};

struct imx_stream_out {
    struct audio_stream_out stream;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config[PCM_TOTAL];
    struct pcm *pcm[PCM_TOTAL];
    struct resampler_itfe *resampler[PCM_TOTAL];
    char *buffer;
    int standby;
    struct echo_reference_itfe *echo_reference;
    struct imx_audio_device *dev;
    int write_threshold[PCM_TOTAL];
    bool low_power;
    int write_flags[PCM_TOTAL];
    int device;
    size_t buffer_frames;
    uint64_t written;
    audio_channel_mask_t channel_mask;
    audio_channel_mask_t sup_channel_masks[3];
    int sup_rates[MAX_SUP_RATE_NUM];
};

#define MAX_PREPROCESSORS 3 /* maximum one AGC + one NS + one AEC per input stream */
struct effect_info_s {
    effect_handle_t effect_itfe;
    size_t num_channel_configs;
    channel_config_t* channel_configs;
};

#define NUM_IN_AUX_CNL_CONFIGS 2
channel_config_t in_aux_cnl_configs[NUM_IN_AUX_CNL_CONFIGS] = {
    { AUDIO_CHANNEL_IN_FRONT , AUDIO_CHANNEL_IN_BACK},
    { AUDIO_CHANNEL_IN_STEREO , AUDIO_CHANNEL_IN_RIGHT}
};

#define MAX_NUM_CHANNEL_CONFIGS 10

struct imx_stream_in {
    struct audio_stream_in stream;

    pthread_mutex_t lock;       /* see note below on mutex acquisition order */
    struct pcm_config config;
    struct pcm *pcm;
    int device;
    struct resampler_itfe *resampler;
    struct resampler_buffer_provider buf_provider;
    int16_t *read_buf;
    size_t read_buf_size;
    size_t read_buf_frames;
    int32_t *read_tmp_buf;
    size_t read_tmp_buf_size;
    size_t read_tmp_buf_frames;

    unsigned int requested_rate;
    unsigned int requested_format;
    unsigned int requested_channel;
    int standby;
    int source;
    struct echo_reference_itfe *echo_reference;
    bool need_echo_reference;
    struct effect_info_s preprocessors[MAX_PREPROCESSORS];
    int num_preprocessors;

    int16_t *proc_buf_in;
    int16_t *proc_buf_out;
    size_t proc_buf_size;
    size_t proc_buf_frames;

    int16_t *ref_buf;
    size_t ref_buf_size;
    size_t ref_frames_in;
    int read_status;
    size_t mute_500ms;
    struct imx_audio_device *dev;
    int last_time_of_xrun;
    bool aux_channels_changed;
    uint32_t main_channels;
    uint32_t aux_channels;
};
#define STRING_TO_ENUM(string) { #string, string }
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct string_to_enum {
    const char *name;
    uint32_t value;
};

#define SUPPORTED_DEVICE_OUT_MODULE               \
          ( AUDIO_DEVICE_OUT_EARPIECE |          \
            AUDIO_DEVICE_OUT_SPEAKER |           \
            AUDIO_DEVICE_OUT_WIRED_HEADSET |     \
            AUDIO_DEVICE_OUT_WIRED_HEADPHONE |   \
            AUDIO_DEVICE_OUT_ALL_SCO |           \
            AUDIO_DEVICE_OUT_AUX_DIGITAL |       \
            AUDIO_DEVICE_OUT_ANLG_DOCK_HEADSET | \
            AUDIO_DEVICE_OUT_DGTL_DOCK_HEADSET | \
            AUDIO_DEVICE_OUT_REMOTE_SUBMIX |     \
            AUDIO_DEVICE_OUT_DEFAULT)

#define SUPPORTED_DEVICE_IN_MODULE               \
          ( AUDIO_DEVICE_IN_COMMUNICATION |      \
            AUDIO_DEVICE_IN_AMBIENT |            \
            AUDIO_DEVICE_IN_BUILTIN_MIC |        \
            AUDIO_DEVICE_IN_ALL_SCO |            \
            AUDIO_DEVICE_IN_WIRED_HEADSET |      \
            AUDIO_DEVICE_IN_AUX_DIGITAL |        \
            AUDIO_DEVICE_IN_VOICE_CALL   |       \
            AUDIO_DEVICE_IN_BACK_MIC   |         \
            AUDIO_DEVICE_IN_REMOTE_SUBMIX |      \
            AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET |  \
            AUDIO_DEVICE_IN_DGTL_DOCK_HEADSET |  \
            AUDIO_DEVICE_IN_USB_ACCESSORY |      \
            AUDIO_DEVICE_IN_USB_DEVICE  |        \
            AUDIO_DEVICE_IN_DEFAULT )

#endif  /* ANDROID_INCLUDE_IMX_AUDIO_HARDWARE_H */
