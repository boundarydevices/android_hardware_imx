/*
** Copyright 2015, The Android Open Source Project
** Copyright (C) 2015 Freescale Semiconductor, Inc.
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
