/*
** Copyright 2015, The Android Open Source Project
** Copyright (C) 2015 Freescale Semiconductor, Inc.
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

#ifndef _CONTROL_H
#define _CONTROL_H

struct ctl_pcm_info {
    struct snd_pcm_info      *info;
    struct ctl_pcm_info      *next;
};

struct control {
    int fd;
    struct snd_ctl_card_info *card_info;
    struct ctl_pcm_info      *pcm_info_p;
    unsigned int count_p;
    struct ctl_pcm_info      *pcm_info_c;
    unsigned int count_c;
};

struct control *control_open(unsigned int card);
void control_close(struct control *control);
const char *control_card_info_get_id(struct control *control);
const char *control_card_info_get_driver(struct control *control);
const char *control_card_info_get_name(struct control *control);

#endif //_CONTROL_H
