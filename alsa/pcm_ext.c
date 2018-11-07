/*
 * Copyright 2011, The Android Open Source Project
 * Copyright 2017 NXP
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

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sound/asound.h>
#include <tinyalsa/asoundlib.h>
#include "pcm_ext.h"

#define PCM_ERROR_MAX 128

struct pcm {
    int fd;
    unsigned int flags;
    int running:1;
    int prepared:1;
    int underruns;
    unsigned int buffer_size;
    unsigned int boundary;
    char error[PCM_ERROR_MAX];
    struct pcm_config config;
    struct snd_pcm_mmap_status *mmap_status;
    struct snd_pcm_mmap_control *mmap_control;
    struct snd_pcm_sync_ptr *sync_ptr;
    void *mmap_buffer;
    unsigned int noirq_frames_per_msec;
    int wait_for_avail_min;
};


static inline int param_is_mask(int p)
{
    return (p >= SNDRV_PCM_HW_PARAM_FIRST_MASK) &&
        (p <= SNDRV_PCM_HW_PARAM_LAST_MASK);
}

static inline int param_is_interval(int p)
{
    return (p >= SNDRV_PCM_HW_PARAM_FIRST_INTERVAL) &&
        (p <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL);
}

static inline struct snd_interval *param_to_interval(struct snd_pcm_hw_params *p, int n)
{
    return &(p->intervals[n - SNDRV_PCM_HW_PARAM_FIRST_INTERVAL]);
}

static inline struct snd_mask *param_to_mask(struct snd_pcm_hw_params *p, int n)
{
    return &(p->masks[n - SNDRV_PCM_HW_PARAM_FIRST_MASK]);
}

static void param_set_mask(struct snd_pcm_hw_params *p, int n, unsigned int bit)
{
    if (bit >= SNDRV_MASK_MAX)
        return;
    if (param_is_mask(n)) {
        struct snd_mask *m = param_to_mask(p, n);
        m->bits[0] = 0;
        m->bits[1] = 0;
        m->bits[bit >> 5] |= (1 << (bit & 31));
    }
}

static void param_init(struct snd_pcm_hw_params *p)
{
    int n;

    memset(p, 0, sizeof(*p));
    for (n = SNDRV_PCM_HW_PARAM_FIRST_MASK;
         n <= SNDRV_PCM_HW_PARAM_LAST_MASK; n++) {
            struct snd_mask *m = param_to_mask(p, n);
            m->bits[0] = ~0;
            m->bits[1] = ~0;
    }
    for (n = SNDRV_PCM_HW_PARAM_FIRST_INTERVAL;
         n <= SNDRV_PCM_HW_PARAM_LAST_INTERVAL; n++) {
            struct snd_interval *i = param_to_interval(p, n);
            i->min = 0;
            i->max = ~0;
    }
    p->rmask = ~0U;
    p->cmask = 0;
    p->info = ~0U;
}

static void param_set_min(struct snd_pcm_hw_params *p, int n, unsigned int val)
{
    if (param_is_interval(n)) {
        struct snd_interval *i = param_to_interval(p, n);
        i->min = val;
    }
}

static unsigned int param_get_min(struct snd_pcm_hw_params *p, int n)
{
    if (param_is_interval(n)) {
        struct snd_interval *i = param_to_interval(p, n);
        return i->min;
    }
    return 0;
}

static void param_set_max(struct snd_pcm_hw_params *p, int n, unsigned int val)
{
    if (param_is_interval(n)) {
        struct snd_interval *i = param_to_interval(p, n);
        i->max = val;
    }
}

static unsigned int param_get_max(struct snd_pcm_hw_params *p, int n)
{
    if (param_is_interval(n)) {
        struct snd_interval *i = param_to_interval(p, n);
        return i->max;
    }
    return 0;
}

static int oops(struct pcm *pcm, int e, const char *fmt, ...)
{
    va_list ap;
    int sz;

    va_start(ap, fmt);
    vsnprintf(pcm->error, PCM_ERROR_MAX, fmt, ap);
    va_end(ap);
    sz = strlen(pcm->error);

    if (errno)
        snprintf(pcm->error + sz, PCM_ERROR_MAX - sz,
                 ": %s", strerror(e));
    return -1;
}


static void param_set_rmask(struct snd_pcm_hw_params *p, int n)
{
    p->rmask |= 1 << n;
}

static unsigned int pcm_format_to_alsa(enum pcm_format format)
{
    switch (format) {
    case PCM_FORMAT_S32_LE:
        return SNDRV_PCM_FORMAT_S32_LE;
    case PCM_FORMAT_S8:
        return SNDRV_PCM_FORMAT_S8;
    case PCM_FORMAT_S24_3LE:
        return SNDRV_PCM_FORMAT_S24_3LE;
    case PCM_FORMAT_S24_LE:
        return SNDRV_PCM_FORMAT_S24_LE;
    default:
    case PCM_FORMAT_S16_LE:
        return SNDRV_PCM_FORMAT_S16_LE;
    };
}

int pcm_get_near_param(unsigned int card, unsigned int device,
                     unsigned int flags, int type, int *data)
{
    struct pcm *pcm;
    struct snd_pcm_hw_params params;
    char fn[256];
    int ret = 0;
    int min = 0, max = 0;
    int mask = 0;
    int request_data = *data;
    *data = 0;

    if(param_is_mask(type)) return -1;

    pcm = calloc(1, sizeof(struct pcm));
    if (!pcm)
        return -1;

    snprintf(fn, sizeof(fn), "/dev/snd/pcmC%uD%u%c", card, device,
             flags & PCM_IN ? 'c' : 'p');

    pcm->flags = flags;
    pcm->fd = open(fn, O_RDWR);
    if (pcm->fd < 0) {
        oops(pcm, errno, "cannot open device '%s'", fn);
        ret = -1;
        goto fail;
    }

    param_init(&params);
    param_set_min(&params, type, request_data);
    param_set_rmask(&params, type);

    if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_HW_REFINE, &params)) {
        oops(pcm, errno, "cannot set hw params rate min");
    } else
        min = param_get_min(&params, type);

    param_init(&params);
    param_set_max(&params, type, request_data);
    param_set_rmask(&params, type);

    if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_HW_REFINE, &params)) {
        oops(pcm, errno, "cannot set hw params rate max");
    }
    else
        max = param_get_max(&params, type);

    if(min > 0)       *data = min;
    else if(max > 0)  *data = max;
    else              *data = 0;

fail_close:
    close(pcm->fd);
    pcm->fd = -1;
fail:
    free(pcm);
    return ret;
}


int pcm_check_param_mask(unsigned int card, unsigned int device,
                     unsigned int flags, int type, int data)
{
    struct pcm *pcm;
    struct snd_pcm_hw_params params;
    char fn[256];
    int ret = 0;
    int min = 0, max = 0;
    int mask = 0;
    int request_data = data;

    if (param_is_interval(type)) return 0;

    pcm = calloc(1, sizeof(struct pcm));
    if (!pcm)
        return 0;

    snprintf(fn, sizeof(fn), "/dev/snd/pcmC%uD%u%c", card, device,
             flags & PCM_IN ? 'c' : 'p');

    pcm->flags = flags;
    pcm->fd = open(fn, O_RDWR);
    if (pcm->fd < 0) {
        oops(pcm, errno, "cannot open device '%s'", fn);
        ret = 0;
        goto fail;
    }

    param_init(&params);
    if (type == PCM_HW_PARAM_FORMAT)
        param_set_mask(&params, type, pcm_format_to_alsa(request_data));
    else
        param_set_mask(&params, type, request_data);
    param_set_rmask(&params, type);

    if (ioctl(pcm->fd, SNDRV_PCM_IOCTL_HW_REFINE, &params) == 0) {
        oops(pcm, errno, "cannot set hw params rate min");
        ret = 1;
    } else
        ret = 0;

fail_close:
    close(pcm->fd);
    pcm->fd = -1;
fail:
    free(pcm);
    return ret;
}


