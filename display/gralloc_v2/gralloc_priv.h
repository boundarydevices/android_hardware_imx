/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2009-2016 Freescale Semiconductor, Inc.
 * Copyright 2017 NXP.
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

#ifndef GRALLOC_PRIV_H_
#define GRALLOC_PRIV_H_

#include <stdint.h>
#include <limits.h>
#include <sys/cdefs.h>
#include <hardware/gralloc.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#include <cutils/native_handle.h>
#include <linux/fb.h>

struct private_handle_t : public native_handle
{
    int  fd;
    int  magic;
    int  flags;
    int  size;
    int  offset;
    uint64_t base __attribute__((aligned(8)));
    uint64_t phys __attribute__((aligned(8)));
    int  format;
    int  width;
    int  height;
    int  pid;

    int  usage;
    int  stride;
};

#endif /* GRALLOC_PRIV_H_ */
