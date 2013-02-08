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

/******************************************************************************
 *
 *  Filename:      bt_vendor_ar3k.c
 *
 *  Description:   QCOM/Atheros vendor specific library implementation
 *
 ******************************************************************************/

#define LOG_TAG "bt_vendor"

#include <utils/Log.h>
#include "bt_vendor_ath3k.h"
#include "upio.h"
#include "userial_vendor.h"

#ifndef BTVND_DBG
#define BTVND_DBG FALSE
#endif

#if (BTVND_DBG == TRUE)
#define BTVNDDBG(param, ...) {ALOGD(param, ## __VA_ARGS__);}
#else
#define BTVNDDBG(param, ...) {}
#endif

/******************************************************************************
**  Static Variables
******************************************************************************/

static const tUSERIAL_CFG userial_init_cfg =
{
    (USERIAL_DATABITS_8 | USERIAL_PARITY_NONE | USERIAL_STOPBITS_1),
    USERIAL_BAUD_115200
};

/******************************************************************************
**  Externs
******************************************************************************/
extern void hw_config_start(void);
extern void vnd_load_conf(const char *p_path);

/******************************************************************************
**  Variables
******************************************************************************/
bt_vendor_callbacks_t *bt_vendor_cbacks = NULL;
uint8_t vnd_local_bd_addr[6]={0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

/******************************************************************************
**  Local type definitions
******************************************************************************/


/******************************************************************************
**  Functions
******************************************************************************/

/*****************************************************************************
**
**   BLUETOOTH VENDOR INTERFACE LIBRARY FUNCTIONS
**
*****************************************************************************/

static int init(const bt_vendor_callbacks_t* p_cb, unsigned char *local_bdaddr)
{
    BTVNDDBG("init");

    if (p_cb == NULL)
    {
        ALOGE("init failed with no user callbacks!");
        return -1;
    }

    userial_vendor_init();

    vnd_load_conf(VENDOR_LIB_CONF_FILE);

    /* store reference to user callbacks */
    bt_vendor_cbacks = (bt_vendor_callbacks_t *) p_cb;

    /* This is handed over from the stack, when to use it? */
    memcpy(vnd_local_bd_addr, local_bdaddr, 6);

    return 0;
}

/** Requested operations */
static int op(bt_vendor_opcode_t opcode, void *param)
{
    int retval = 0;

    BTVNDDBG("op for %d", opcode);

    switch(opcode)
    {
        case BT_VND_OP_POWER_CTRL:
            {
                int *state = (int *) param;

                if (*state == BT_VND_PWR_OFF)
                    upio_set_bluetooth_power(UPIO_BT_POWER_OFF);
                else if (*state == BT_VND_PWR_ON)
                    upio_set_bluetooth_power(UPIO_BT_POWER_ON);

                retval = 0;

            }
            break;

        case BT_VND_OP_FW_CFG:
            {
                if (bt_vendor_cbacks) {
                   BTVNDDBG("Bluetooth hw config start");

                   hw_config_start();

                } else {
                   ALOGE("Bluetooth bt_vendor_cbacks is NULL ");
                }
            }
            break;

        case BT_VND_OP_SCO_CFG:
            {
                bt_vendor_cbacks->scocfg_cb(BT_VND_OP_RESULT_SUCCESS); //dummy
                retval = -1;
            }
            break;

        case BT_VND_OP_USERIAL_OPEN:
            {
                int (*fd_array)[] = (int (*)[]) param;
                int fd, idx;
                fd = userial_vendor_open((tUSERIAL_CFG *) &userial_init_cfg);
                if (fd != -1)
                {
                    for (idx=0; idx < CH_MAX; idx++)
                        (*fd_array)[idx] = fd;

                    retval = 1;
                }

            }
            break;

        case BT_VND_OP_USERIAL_CLOSE:
            {
                 userial_vendor_close();
            }
            break;

        case BT_VND_OP_GET_LPM_IDLE_TIMEOUT:
            {
                uint32_t *timeout_ms = (uint32_t *) param;
                *timeout_ms = 50;
            }
            break;

        case BT_VND_OP_LPM_SET_MODE:
            {
                bt_vendor_cbacks->lpm_cb(BT_VND_OP_RESULT_SUCCESS); //dummy
            }
            break;

        case BT_VND_OP_LPM_WAKE_SET_STATE:
            /*{
                uint8_t *state = (uint8_t *) param;
                uint8_t wake_assert = (*state == BT_VND_LPM_WAKE_ASSERT) ? \
                                        TRUE : FALSE;
            }*/
            break;
    }

    return retval;
}

/** Closes the interface */
static void cleanup( void )
{
    BTVNDDBG("cleanup");

    bt_vendor_cbacks = NULL;
}

// Entry point of DLib
const bt_vendor_interface_t BLUETOOTH_VENDOR_LIB_INTERFACE = {
    sizeof(bt_vendor_interface_t),
    init,
    op,
    cleanup
};
