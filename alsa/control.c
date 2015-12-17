/*
** Copyright 2011, The Android Open Source Project
** Copyright (C) 2012-2015 Freescale Semiconductor, Inc.
**
** Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are met:
**     * Redistributions of source code must retain the above copyright
**       notice, this list of conditions and the following disclaimer.
**     * Redistributions in binary form must reproduce the above copyright
**       notice, this list of conditions and the following disclaimer in the
**       documentation and/or other materials provided with the distribution.
**     * Neither the name of The Android Open Source Project nor the names of
**       its contributors may be used to endorse or promote products derived
**       from this software without specific prior written permission.
**
** THIS SOFTWARE IS PROVIDED BY The Android Open Source Project ``AS IS'' AND
** ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
** IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
** ARE DISCLAIMED. IN NO EVENT SHALL The Android Open Source Project BE LIABLE
** FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
** DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
** SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
** CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
** LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
** OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
** DAMAGE.
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

    current = control->pcm_info_p;
    while(!current)
    {
        if(!current->info)
            free(current->info);
        p = current;
        current = current->next;
        free(p);
    }

    current = control->pcm_info_c;
    while(!current)
    {
        if(!current->info)
            free(current->info);
        p = current;
        current = current->next;
        free(p);
    }
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


