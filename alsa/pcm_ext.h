/*
 * Copyright (C) 2017 The Android Open Source Project
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
/* Copyright 2017 NXP */

#ifndef _PCM_EXT_H
#define _PCM_EXT_H

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

int pcm_get_near_param(unsigned int card, unsigned int device,
                     unsigned int flags, int type, int *data);

int pcm_check_param_mask(unsigned int card, unsigned int device,
                     unsigned int flags, int type, int data);

int __attribute__((weak)) pcm_get_time_of_xrun(struct pcm *pcm);

#endif
