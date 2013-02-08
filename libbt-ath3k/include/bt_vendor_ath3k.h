/*
 * Copyright 2012 The Android Open Source Project
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

/* Copyright (C) 2013 Freescale Semiconductor, Inc. */

#ifndef BT_VENDOR_ATH3K_H
#define BT_VENDOR_ATH3K_H

#include "bt_vendor_lib.h"
#include "vnd_buildcfg.h"

#ifndef FALSE
#define FALSE  0
#endif

#ifndef TRUE
#define TRUE   (!FALSE)
#endif

/* Run-time configuration file */
#ifndef VENDOR_LIB_CONF_FILE
#define VENDOR_LIB_CONF_FILE "/etc/bluetooth/bt_vendor.conf"
#endif

#ifndef UART_TARGET_BAUD_RATE
#define UART_TARGET_BAUD_RATE           3000000
#endif

extern bt_vendor_callbacks_t *bt_vendor_cbacks;

#endif /* BT_VENDOR_ATH3K_H */

