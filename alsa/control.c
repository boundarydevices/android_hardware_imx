/*
** Copyright 2011, The Android Open Source Project
** Copyright (C) 2012-2015 Freescale Semiconductor, Inc.
**
** Licensed under the Apache License, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**      http://www.apache.org/licenses/LICENSE-2.0
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <linux/ioctl.h>
#include <sound/asound.h>
#include <tinyalsa/asoundlib.h>
#include "control.h"

struct control *control_open(unsigned int card)
{
    struct snd_ctl_card_info tmp;
    struct control *control = NULL;
    unsigned int n, m;
    int fd;
    char fn[256];
    int device = -1;
    struct ctl_pcm_info      *current = NULL;

    snprintf(fn, sizeof(fn), "/dev/snd/controlC%u", card);
    fd = open(fn, O_RDWR);
    if (fd < 0)
        return 0;

    control = calloc(1, sizeof(*control));
    if (!control)
        goto fail;

    control->count_p   = 0;
    control->count_c   = 0;
    control->fd        = fd;
    control->card_info = calloc(1, sizeof(struct snd_ctl_card_info));
    if (!control->card_info)
        goto fail;

    if (ioctl(fd, SNDRV_CTL_IOCTL_CARD_INFO, control->card_info) < 0)
        goto fail;

    control->pcm_info_p = calloc(1, sizeof(struct ctl_pcm_info));
    if (!control->pcm_info_p)
        goto fail;

    current = control->pcm_info_p;
    device = -1;
    while(1)
    {
        if (ioctl(fd, SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE, &device) < 0)
            break;
        if(device < 0)
            break;

        control->count_p           += 1;
        current->info = calloc(1, sizeof(struct snd_pcm_info));
        if (!current->info)
            goto fail;

        current->info->device       = device;
        current->info->subdevice    = 0;
        current->info->stream       = SNDRV_PCM_STREAM_PLAYBACK;

        if (ioctl(fd, SNDRV_CTL_IOCTL_PCM_INFO, current->info) < 0)
            break;

        current->next = calloc(1, sizeof(struct ctl_pcm_info));
        if (!current->next)
            goto fail;
        current = current->next;
    }

    control->pcm_info_c = calloc(1, sizeof(struct ctl_pcm_info));
    if (!control->pcm_info_c)
        goto fail;

    current = control->pcm_info_c;
    device = -1;
    while(1)
    {
        if (ioctl(fd, SNDRV_CTL_IOCTL_PCM_NEXT_DEVICE, &device) < 0)
            break;
        if(device < 0)
            break;

        control->count_c           += 1;
        current->info = calloc(1, sizeof(struct snd_pcm_info));
        if (!current->info)
            goto fail;

        current->info->device       = device;
        current->info->subdevice    = 0;
        current->info->stream       = SNDRV_PCM_STREAM_CAPTURE;

        if (ioctl(fd, SNDRV_CTL_IOCTL_PCM_INFO, current->info) < 0)
            break;

        current->next = calloc(1, sizeof(struct ctl_pcm_info));
        if (!current->next)
            goto fail;
        current = current->next;
    }
    return control;

fail:
    if (control)
        control_close(control);
    else if (fd >= 0)
        close(fd);
    return 0;
}

static void free_ctl_pcm_info(struct ctl_pcm_info* current) {
    struct ctl_pcm_info* p = NULL;
    while (current) {
        free(current->info);
        p = current;
        current = current->next;
        free(p);
    }
}

void control_close(struct control *control)
{
    unsigned int n,m;
    struct ctl_pcm_info      *current = NULL;
    struct ctl_pcm_info      *p = NULL;

    if (!control)
        return;

    if (control->fd >= 0)
        close(control->fd);

    if (control->card_info)
        free(control->card_info);

    free_ctl_pcm_info(control->pcm_info_p);
    free_ctl_pcm_info(control->pcm_info_c);

    free(control);
}

const char *control_card_info_get_id(struct control *control)
{
    if (!control)
        return "";

    return (const char *)control->card_info->id;
}

const char *control_card_info_get_driver(struct control *control)
{
    if (!control)
        return "";

    return (const char *)control->card_info->driver;
}

const char *control_card_info_get_name(struct control *control)
{
    if (!control)
        return "";

    return (const char *)control->card_info->name;
}


int control_pcm_next_device(struct control *control, int *device, int stream)
{
    struct ctl_pcm_info      *current = NULL;
    if (!control)
        return -EINVAL;

    if(stream == SNDRV_PCM_STREAM_PLAYBACK)   current = control->pcm_info_p;
    else                                      current = control->pcm_info_c;

    while(!current->info)
        if((int)current->info->device > *device)
        {
            *device = current->info->device;
            return 0;
        }
    return -1;
}


const char *control_pcm_info_get_id(struct control *control, unsigned int device, int stream)
{
    struct ctl_pcm_info      *current = NULL;
    if (!control)
        return "";

    if(stream == SNDRV_PCM_STREAM_PLAYBACK)   current = control->pcm_info_p;
    else                                    current = control->pcm_info_c;

    while(!current->info)
        if(current->info->device == device)
            return (const char *)current->info->id;

    return "";
}

const char *control_pcm_info_get_name(struct control *control, unsigned int device, int stream)
{
    struct ctl_pcm_info      *current = NULL;
    if (!control)
        return "";

    if(stream == SNDRV_PCM_STREAM_PLAYBACK)   current = control->pcm_info_p;
    else                                    current = control->pcm_info_c;

    while(!current->info)
        if(current->info->device == device)
            return (const char *)current->info->name;

    return "";
}


