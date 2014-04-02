/*
 * Copyright 2012 The Android Open Source Project
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
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

/******************************************************************************
 *
 *  Filename:      bt_vendor_qcom_AR3002.c
 *
 *  Description:   QCOM vendor specific library implementation
 *
 ******************************************************************************/

#define LOG_TAG "bt_vendor"

#include <utils/Log.h>
#include <fcntl.h>
#include <termios.h>
#include "bt_vendor_QCA3002.h"
#include "userial_vendor_QCA3002.h"
#include "upio.h"

#define UPIO_BT_POWER_OFF 0
#define UPIO_BT_POWER_ON 1
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


/******************************************************************************
**  Variables
******************************************************************************/
int pFd[2] = {0,};
//bt_hci_transport_device_type bt_hci_transport_device;

bt_vendor_callbacks_t *bt_vendor_cbacks = NULL;
uint8_t vnd_local_bd_addr[6]={0x11, 0x22, 0x33, 0x44, 0x55, 0xFF};
#if (HW_NEED_END_WITH_HCI_RESET == TRUE)
void hw_epilog_process(void);
#endif


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
    ALOGI("bt-vendor : init");

    if (p_cb == NULL)
    {
        ALOGE("init failed with no user callbacks!");
        return -1;
    }

    userial_vendor_init();
    //upio_init();

    //vnd_load_conf(VENDOR_LIB_CONF_FILE);

    /* store reference to user callbacks */
    bt_vendor_cbacks = (bt_vendor_callbacks_t *) p_cb;

    /* This is handed over from the stack */
    memcpy(vnd_local_bd_addr, local_bdaddr, 6);

    return 0;
}


/** Requested operations */
static int op(bt_vendor_opcode_t opcode, void *param)
{
    int retval = 0;
    int nCnt = 0;
    int nState = -1;

    ALOGV("%s : bt-vendor : op for %d", __FUNCTION__, opcode);

    switch(opcode)
    {
        case BT_VND_OP_POWER_CTRL:
            {
                ALOGV("AR3002 ::BT_VND_OP_POWER_CTRL");
                int *state = (int *) param;
				if (*state == BT_VND_PWR_OFF){
                    ALOGI("[//]AR3002 UPIO_BT_POWER_OFF");
					upio_set_bluetooth_power(UPIO_BT_POWER_OFF);
				}
                else if (*state == BT_VND_PWR_ON){
                    ALOGI("[//]AR3002 UPIO_BT_POWER_ON");
					upio_set_bluetooth_power(UPIO_BT_POWER_ON);
                }
                retval = 0;
            }
            break;

        case BT_VND_OP_FW_CFG:
            {
				ALOGI("AR3002 ::BT_VND_OP_FW_CFG");

                if(bt_vendor_cbacks){
                   ALOGI("AR3002 ::Bluetooth Firmware download");
                   bt_vendor_cbacks->fwcfg_cb(BT_VND_OP_RESULT_SUCCESS);
                }
                else{
                   ALOGE("AR3002 ::Error : AR3002 Bluetooth Firmware download");
                   bt_vendor_cbacks->fwcfg_cb(BT_VND_OP_RESULT_FAIL);
                }
            }
            break;

        case BT_VND_OP_SCO_CFG:
            {
                bt_vendor_cbacks->scocfg_cb(BT_VND_OP_RESULT_SUCCESS); //dummy
            }
            break;

        case BT_VND_OP_USERIAL_OPEN:
            {
                ALOGI("AR3002 ::BT_VND_OP_USERIAL_OPEN ");
                int (*fd_array)[] = (int (*)[]) param;
                int fd,idx;
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
                ALOGI("AR3002 ::BT_VND_OP_USERIAL_CLOSE ");
                userial_vendor_close();
            }
            break;

        case BT_VND_OP_GET_LPM_IDLE_TIMEOUT:
            ALOGI("AR3002 ::BT_VND_OP_GET_LPM_IDLE_TIMEOUT (timeout_ms = 3000;)");
            uint32_t *timeout_ms = (uint32_t *) param;
            *timeout_ms = 3000;
            break;

        case BT_VND_OP_LPM_SET_MODE:
            {
                ALOGI("AR3002 ::BT_VND_OP_LPM_SET_MODE ()");
                bt_vendor_cbacks->lpm_cb(BT_VND_OP_RESULT_SUCCESS); //dummy
            }
            break;

        case BT_VND_OP_LPM_WAKE_SET_STATE:
			{
				ALOGI("AR3002 ::BT_VND_OP_LPM_WAKE_SET_STATE ()");
				bt_vendor_cbacks->lpm_cb(BT_VND_OP_RESULT_SUCCESS); //dummy
        	}			
            break;
        case BT_VND_OP_EPILOG:
            {
#if (HW_NEED_END_WITH_HCI_RESET == FALSE)
                if (bt_vendor_cbacks)
                {
                    bt_vendor_cbacks->epilog_cb(BT_VND_OP_RESULT_SUCCESS);
                }
#else
                hw_epilog_process();
#endif
            }
    }

    return retval;
}

/** Closes the interface */
static void cleanup( void )
{
    ALOGI("cleanup");
    //upio_cleanup();
    bt_vendor_cbacks = NULL;
}

// Entry point of DLib
const bt_vendor_interface_t BLUETOOTH_VENDOR_LIB_INTERFACE = {
    sizeof(bt_vendor_interface_t),
    init,
    op,
    cleanup
};
