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
 *  Filename:      hardware.c
 *
 *  Description:   Contains ath3k controller-specific functions, like
 *                      firmware patch download
 *                      low power mode operations
 *
 ******************************************************************************/

#define LOG_TAG "bt_hwcfg"

#include <utils/Log.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <cutils/properties.h>
#include <stdlib.h>

#include "bt_hci_bdroid.h"
#include "bt_vendor_ath3k.h"
#include "userial_vendor.h"

/******************************************************************************
**  Constants & Macros
******************************************************************************/

#ifndef BTHW_DBG
#define BTHW_DBG FALSE
#endif

#if (BTHW_DBG == TRUE)
#define BTHWDBG(param, ...) {ALOGD(param, ## __VA_ARGS__);}
#else
#define BTHWDBG(param, ...) {}
#endif

#define HCI_SLEEP_CMD_OCF       0x04
#define GET_DEV_TYPE_OCF        0x05
#define HCI_PS_CMD_OCF          0x0B
#define HCI_CHG_BAUD_CMD_OCF    0x0C
#define GET_VERSION_OCF         0x1E

/* AR3k Vendor Specific commands */
#define HCI_GRP_VENDOR_SPECIFIC (0x3F << 10)            /* 0xFC00 */
#define HCI_VSC_SLEEP_CMD       (HCI_GRP_VENDOR_SPECIFIC | HCI_SLEEP_CMD_OCF)
#define HCI_VSC_GET_DEV_TYPE    (HCI_GRP_VENDOR_SPECIFIC | GET_DEV_TYPE_OCF)
#define HCI_VSC_PS_CMD          (HCI_GRP_VENDOR_SPECIFIC | HCI_PS_CMD_OCF)
#define HCI_VSC_CHG_BAUD_CMD    (HCI_GRP_VENDOR_SPECIFIC | HCI_CHG_BAUD_CMD_OCF)
#define HCI_VSC_GET_VERSION     (HCI_GRP_VENDOR_SPECIFIC | GET_VERSION_OCF)
#define HCI_RESET               0x0C03

#define HCI_CMD_MAX_LEN                         258
#define HCI_EVT_CMD_CMPL_OPCODE                 3
#define HCI_EVT_CMD_CMPL_STATUS_RET_BYTE        5
#define HCI_EVT_CMD_CMPL_RESPONSE               6
#define HCI_CMD_PREAMBLE_SIZE                   3
#define GET_DEV_TYPE_CMD_PARAM_SIZE             5
#define PS_CMD_GET_CRC_PARAM_SIZE               4
#define PS_CMD_WRITE_BDADDR_PARAM_SIZE          10
#define PS_CMD_WRITE_PATCH_PARAM_SIZE           4
#define CHG_BAUD_CMD_PARAM_SIZE                 2
#define PS_RESET_PARAM_LEN                      6

#define DEV_REGISTER            0x4FFC
#define PS_ASIC_FILE            "PS_ASIC.pst"
#define PS_FPGA_FILE            "PS_FPGA.pst"
#define MAXPATHLEN              4096
#define PATCH_FILE              "RamPatch.txt"
#define BDADDR_FILE             "ar3kbdaddr.pst"
#define FPGA_ROM_VERSION        0x99999999
#define ROM_DEV_TYPE            0xdeadc0de
#define PS_HEX                  0
#define PS_DEC                  1
#define UNDEFINED               0xFFFF
#define ARRAY                   'A'
#define STRING                  'S'
#define DECIMAL                 'D'
#define BINARY                  'B'
#define PS_UNDEF                0
#define PS_ID                   1
#define PS_LEN                  2
#define PS_DATA                 3
#define PS_MAX_LEN              500
#define LINE_SIZE_MAX           (PS_MAX_LEN * 2)
#define ENTRY_PER_LINE          16
#define MAX_PREAMBLE_LEN        4
#define PATCH_LOC_KEY           "DA:"
#define MAX_BD_ADDR_LEN         20
#define SET_PATCH_RAM_ID        0x0D
#define SET_PATCH_RAM_CMD_SIZE  11
#define ADDRESS_LEN             4
#define MAX_TAGS                50
#define PS_HDR_LEN              4
#define VERIFY_CRC              9
#define PS_REGION               1
#define PATCH_REGION            2
#define MAX_PATCH_CMD           244
#define PS_WRITE                1
#define PS_RESET                2
#define WRITE_PATCH             8
#define ENABLE_PATCH            11
#define PS_ID_MASK              0xFF

#define __check_comment(buf) (((buf)[0] == '/') && ((buf)[1] == '/'))
#define __skip_space(str)      while (*(str) == ' ') ((str)++)
#define __is_delim(ch) ((ch) == ':')

#define STREAM_TO_UINT16(u16, p) {u16 = ((uint16_t)(*(p)) + (((uint16_t)(*((p) + 1))) << 8)); (p) += 2;}
#define UINT16_TO_STREAM(p, u16) {*(p)++ = (uint8_t)(u16); *(p)++ = (uint8_t)((u16) >> 8);}

struct patch_entry {
    int16_t len;
    uint8_t data[MAX_PATCH_CMD];
};

#define HCI_PS_CMD_HDR_LEN HCI_CMD_PREAMBLE_SIZE + PS_HDR_LEN
#define PS_RESET_CMD_LEN   (HCI_PS_CMD_HDR_LEN + PS_RESET_PARAM_LEN)

#define LOCAL_NAME_BUFFER_LEN                   32

void hw_config_cback(void *p_mem);
#define PATCH_LOC_STRING_LEN   8

/******************************************************************************
**  Static variables
******************************************************************************/

/* h/w config control block */
typedef struct
{
    uint8_t state;                          /* Hardware configuration state */
    int     fw_fd;                          /* FW patch file fd */
    uint8_t f_set_baud_2;                   /* Baud rate switch state */
    char    local_chip_name[LOCAL_NAME_BUFFER_LEN];
} bt_hw_cfg_cb_t;

static bt_hw_cfg_cb_t hw_cfg_cb;

static char patch_file[PATH_MAX];
static char ps_file[PATH_MAX];
static FILE *rampatch_fd;
static int tag_count=0;
static int current_tag_idx=0;
static uint32_t dev_type = 0xdeadc0de;
static uint32_t rom_version = 0x30101;
static uint32_t bld_version = 0x9d6a2;
char ARbyte[3];
char ARptr[MAX_PATCH_CMD + 1];
int byte_cnt;
int segment_idx = 0;
char patch_loc[PATCH_LOC_STRING_LEN + 1];
int ps_counter=0;
static char fw_patchfile_path[MAXPATHLEN] = FW_PATCHFILE_LOCATION;

struct tag_info {
    unsigned section;
    unsigned line_count;
    unsigned char_cnt;
    unsigned byte_count;
};

struct ps_cfg_entry {
    uint32_t id;
    uint32_t len;
    uint8_t *data;
};

struct ps_entry_type {
    unsigned char type;
    unsigned char array;
};

struct ps_cfg_entry ps_list[MAX_TAGS];

struct hci_command_hdr {
    uint16_t    opcode;     /* OCF & OGF */
    uint8_t     plen;
};

/* Hardware Configuration State */
enum {
    HW_CFG_START = 1,
    HW_CFG_SET_CNTRLR_BAUD,
    HW_CFG_GET_DEVICE_TYPE,
    HW_CFG_GET_ATH3K_VERSION,
    HW_CFG_GET_ATH3K_CRC,
    HW_CFG_PSPATCH_START_DOWNLOAD,
    HW_CFG_PSPATCH_DOWNLOAD,
    HW_CFG_PSPATCH_DOWNLOADING,
    HW_CFG_PSPATCH_DOWNLOADED,
    HW_CFG_CONFIG_RESET,
    HW_CFG_CONFIG_DOWNLOADING,
    HW_CFG_WRITE_BDADDRESS,
    HW_CFG_WRITE_BDADDRESS_DONE,
    HW_CFG_HCI_RESET,
    HW_CFG_CNTRLR_BAUD,
    HW_CFG_CNTRLR_BAUD_UPDATE,
};

static int send_cmd(HC_BT_HDR *p_buf, uint8_t *buffer, int len)
{
    uint8_t     *p;

    BTHWDBG("%s", __func__);

    if (p_buf)
    {
        struct hci_command_hdr *ch = (void *)buffer;

        p_buf->event = MSG_STACK_TO_HC_HCI_CMD;
        p_buf->offset = 0;
        p_buf->len = len;
        p_buf->layer_specific = 0;

        p = (uint8_t *) (p_buf + 1);
        memcpy(p, buffer, len);

        bt_vendor_cbacks->xmit_cb(ch->opcode, p_buf, hw_config_cback);
    }
    return 0;
}

static void load_hci_ps_hdr(uint8_t *cmd, uint8_t ps_op, int len, int index)
{
    BTHWDBG("%s ps_op = 0x%x", __func__, ps_op);

    UINT16_TO_STREAM(cmd, HCI_VSC_PS_CMD);
    *cmd++ = len + PS_HDR_LEN;
    *cmd++ = ps_op;
    *cmd++ = index;
    *cmd++ = index >> 8;
    *cmd = len;
}

/* Sends PS commands using vendor specficic HCI commands */
static int write_ps_cmd(HC_BT_HDR *p_buf, uint8_t opcode, uint32_t ps_param)
{
    uint8_t cmd[HCI_CMD_MAX_LEN];

    BTHWDBG("%s opcode %d", __func__, opcode);

    switch (opcode) {
        case PS_WRITE:
            {
                load_hci_ps_hdr(cmd, opcode, ps_list[ps_param].len,
                                ps_list[ps_param].id);

                memcpy(&cmd[HCI_PS_CMD_HDR_LEN], ps_list[ps_param].data,
                                        ps_list[ps_param].len);

                if (send_cmd(p_buf, cmd, ps_list[ps_param].len +
                                            HCI_PS_CMD_HDR_LEN) < 0)
                    return -EILSEQ;
            }
            break;
        case ENABLE_PATCH:
            {
                load_hci_ps_hdr(cmd, opcode, 0, 0x00);

                if (send_cmd(p_buf, cmd, HCI_PS_CMD_HDR_LEN) < 0)
                    return -EILSEQ;
            }
            break;

        case PS_RESET:
            {
                load_hci_ps_hdr(cmd, opcode, PS_RESET_PARAM_LEN, 0x00);

                cmd[7] = 0x00;
                cmd[8] = 0x00;
                cmd[9] = 0x00;
                cmd[10] = 0x00;

                cmd[PS_RESET_CMD_LEN - 2] = ps_param & PS_ID_MASK;
                cmd[PS_RESET_CMD_LEN - 1] = (ps_param >> 8) & PS_ID_MASK;

                if (send_cmd(p_buf, cmd, PS_RESET_CMD_LEN) < 0)
                    return -EILSEQ;
            }
            break;
    }
    return 0;
}

/* Parse PS entry preamble of format [X:X] for main type and subtype */
static int get_ps_type(char *ptr, int index, char *type, char *sub_type)
{
    int i;
    int delim = FALSE;

    BTHWDBG("%s ", __func__);

    if (index > MAX_PREAMBLE_LEN)
        return -EILSEQ;

    for (i = 1; i < index; i++) {
        if (__is_delim(ptr[i])) {
            delim = TRUE;
            continue;
        }

        if (isalpha(ptr[i])) {
            if (delim == FALSE)
                (*type) = toupper(ptr[i]);
            else
                (*sub_type) = toupper(ptr[i]);
        }
    }
    return 0;
}

static int get_input_format(char *buf, struct ps_entry_type *format)
{
    char *ptr = NULL;
    char type = '\0';
    char sub_type = '\0';

    format->type = PS_HEX;
    format->array = TRUE;

    if (strstr(buf, "[") != buf)
        return 0;

    ptr = strstr(buf, "]");
    if (!ptr)
        return -EILSEQ;

    if (get_ps_type(buf, ptr - buf, &type, &sub_type) < 0)
        return -EILSEQ;

    /* Check is data type is of array */
    if (type == ARRAY || sub_type == ARRAY)
        format->array = TRUE;

    if (type == STRING || sub_type == STRING)
        format->array = FALSE;

    if (type == DECIMAL || type == BINARY)
        format->type = PS_DEC;
    else
        format->type = PS_HEX;

    return 0;
}

static unsigned int read_data_in_section(char *buf, struct ps_entry_type type)
{
    char *ptr = buf;

    if (!buf)
        return UNDEFINED;

    if (buf == strstr(buf, "[")) {
        ptr = strstr(buf, "]");
        if (!ptr)
            return UNDEFINED;

        ptr++;
    }

    if (type.type == PS_HEX && type.array != TRUE)
        return strtol(ptr, NULL, 16);

    return UNDEFINED;
}

/* Read PS entries as string, convert and add to Hex array */
static void update_tag_data(struct ps_cfg_entry *tag,
                    struct tag_info *info, const char *ptr)
{
    char buf[3];

    buf[2] = '\0';

    strncpy(buf, &ptr[info->char_cnt], 2);
    tag->data[info->byte_count] = strtol(buf, NULL, 16);
    info->char_cnt += 3;
    info->byte_count++;

    strncpy(buf, &ptr[info->char_cnt], 2);
    tag->data[info->byte_count] = strtol(buf, NULL, 16);
    info->char_cnt += 3;
    info->byte_count++;
}

static inline int update_char_count(const char *buf)
{
    char *end_ptr;

    if (strstr(buf, "[") == buf) {
        end_ptr = strstr(buf, "]");
        if (!end_ptr)
            return 0;
        else
            return (end_ptr - buf) + 1;
    }

    return 0;
}

static int ath_parse_ps(FILE *ps_stream)
{
    char buf[LINE_SIZE_MAX + 1];
    char *ptr;
    uint8_t tag_cnt = 0;
    int16_t byte_count = 0;
    struct ps_entry_type format;
    struct tag_info status = { 0, 0, 0, 0 };

    do{
        int read_count;
        struct ps_cfg_entry *tag;

        ptr = fgets(buf, LINE_SIZE_MAX, ps_stream);
        if (!ptr)
            break;

        __skip_space(ptr);
        if (__check_comment(ptr))
            continue;

        /* Lines with a '#' will be followed by new PS entry */
        if (ptr == strstr(ptr, "#")) {
            if (status.section != PS_UNDEF) {
                return -EILSEQ;
            } else {
                status.section = PS_ID;
                continue;
            }
        }

        tag = &ps_list[tag_cnt];

        switch (status.section) {
        case PS_ID:
            if (get_input_format(ptr, &format) < 0)
                return -EILSEQ;

            tag->id = read_data_in_section(ptr, format);
            status.section = PS_LEN;
            break;

        case PS_LEN:
            if (get_input_format(ptr, &format) < 0)
                return -EILSEQ;

            byte_count = read_data_in_section(ptr, format);
            if (byte_count > PS_MAX_LEN)
                return -EILSEQ;

            tag->len = byte_count;
            tag->data = (uint8_t *)malloc(byte_count);

            status.section = PS_DATA;
            status.line_count = 0;
            break;

        case PS_DATA:
            if (status.line_count == 0)
                if (get_input_format(ptr, &format) < 0)
                    return -EILSEQ;

            __skip_space(ptr);

            status.char_cnt = update_char_count(ptr);

            read_count = (byte_count > ENTRY_PER_LINE) ?
                    ENTRY_PER_LINE : byte_count;

            if (format.type == PS_HEX && format.array == TRUE) {
                while (read_count > 0) {
                    update_tag_data(tag, &status, ptr);
                    read_count -= 2;
                }

                if (byte_count > ENTRY_PER_LINE)
                    byte_count -= ENTRY_PER_LINE;
                else
                    byte_count = 0;
            }

            status.line_count++;

            if (byte_count == 0)
                memset(&status, 0x00, sizeof(struct tag_info));

            if (status.section == PS_UNDEF)
                tag_cnt++;

            if (tag_cnt == MAX_TAGS)
                return -EILSEQ;
            break;
        }
    } while (ptr);

    return tag_cnt;
}


static int ps_reset_config(HC_BT_HDR *p_buf)
{
#define PS_RAM_SIZE 2048

    if (write_ps_cmd(p_buf, PS_RESET, PS_RAM_SIZE) < 0)
        return -1;
    else
        return 0;
}

static int ps_config_download(HC_BT_HDR *p_buf, int tag_count)
{
    if (write_ps_cmd(p_buf, PS_WRITE, tag_count) < 0)
        return -1;
    else
        return 0;
}

static int set_patch_ram(HC_BT_HDR *p_buf, char *patch_loc)
{
    int err;
    int i;
    char loc_byte[3];
    uint8_t *p;

    if (!patch_loc)
        return -1;

    loc_byte[2] = '\0';

    if (p_buf)
    {
        p_buf->event = MSG_STACK_TO_HC_HCI_CMD;
        p_buf->offset = 0;
        p_buf->len = HCI_CMD_PREAMBLE_SIZE + PS_HDR_LEN + ADDRESS_LEN;
        p_buf->layer_specific = 0;

        p = (uint8_t *) (p_buf + 1);

        UINT16_TO_STREAM(p, HCI_VSC_PS_CMD);
        *p++ = PS_HDR_LEN + ADDRESS_LEN;
        *p++ = SET_PATCH_RAM_ID;
        *p++ = 0;
        *p++ = 0 >> 8;
        *p++ = ADDRESS_LEN;

        for (i = 3; i >= 0; i--) {
            loc_byte[0] = patch_loc[0];
            loc_byte[1] = patch_loc[1];
            *p++ = strtol(loc_byte, NULL, 16);
            patch_loc += 2;
        }

        hw_cfg_cb.state = HW_CFG_PSPATCH_START_DOWNLOAD;
        bt_vendor_cbacks->xmit_cb(HCI_VSC_PS_CMD, p_buf, hw_config_cback);
    }
    return err;
}


static int enable_patch(HC_BT_HDR *p_buf)
{
    BTHWDBG("%s ", __func__);

    if (write_ps_cmd(p_buf, ENABLE_PATCH, 0) < 0)
        return -1;

    return 0;
}

static int write_patch(HC_BT_HDR *p_buf)
{
    uint8_t ret = -1;
    uint8_t *p;
    int i;
    BTHWDBG("%s ", __func__);

    if (ps_counter==1)
    {
        ps_counter=2;
        byte_cnt = strtol(ARptr, NULL, 16);
    }

    if (byte_cnt > 0) {
        struct patch_entry patch;

        if (byte_cnt > MAX_PATCH_CMD)
            patch.len = MAX_PATCH_CMD;
        else
            patch.len = byte_cnt;

        /* Read 2 bytes from the stream */
        for (i = 0; i < patch.len; i++) {
            if (!fgets(ARbyte, 3, rampatch_fd)) {
                return -1;
            }
            /* Convert to integer */
            patch.data[i] = strtoul(ARbyte, NULL, 16);
        }

        p_buf->event = MSG_STACK_TO_HC_HCI_CMD;
        p_buf->offset = 0;
        p_buf->len = patch.len + HCI_CMD_PREAMBLE_SIZE + PS_CMD_WRITE_PATCH_PARAM_SIZE;
        p_buf->layer_specific = 0;

        p = (uint8_t *) (p_buf + 1);

        /* Set opcode */
        UINT16_TO_STREAM(p, HCI_VSC_PS_CMD);

        /* Set length */
        *p++ = patch.len + PS_CMD_WRITE_PATCH_PARAM_SIZE;
        *p++ = WRITE_PATCH;
        /* LSB, MSB of segment index */
        *p++ = segment_idx;
        *p++ = segment_idx >> 8;
        /* Segment length */
        *p++ = patch.len;

        memcpy(p, patch.data, patch.len);

        hw_cfg_cb.state = HW_CFG_PSPATCH_DOWNLOADING;
        ret = bt_vendor_cbacks->xmit_cb(HCI_VSC_PS_CMD, p_buf, hw_config_cback);

        segment_idx++;
        byte_cnt = byte_cnt - patch.len;

    }
    else {
        /* Rampatch download complete, enable patch */
        hw_cfg_cb.state = HW_CFG_PSPATCH_DOWNLOADED;
        ret = enable_patch(p_buf);
    }

    return ret;
}


static int set_patch_ram_id(HC_BT_HDR *p_buf)
{
    int ret = 0;
    BTHWDBG("%s ", __func__);

    if (ps_counter == 0)
    {
        ps_counter = 1;
        ARbyte[2] = '\0';
    }

    /* Get first token before newline */
    while (fgets(ARptr, MAX_PATCH_CMD, rampatch_fd) != NULL) {
        if (strlen(ARptr) <= 1) {
            continue;
        }
        else if (strstr(ARptr, PATCH_LOC_KEY) == ARptr) {
            strncpy(patch_loc, &ARptr[sizeof(PATCH_LOC_KEY) - 1],PATCH_LOC_STRING_LEN);
            set_patch_ram(p_buf, patch_loc);
            break; //wait for next time be called from cal back function.
        } else if (isxdigit(ARptr[0])) {
            if (write_patch(p_buf) < 0)
                ret = -1;
            break;
        }
        else
            break;
    }
    return ret;
}

static void convert_bdaddr(char *bdaddr, char *p)
{
    char bdbyte[3];
    char *str_byte = bdaddr;
    int i;
    int colon_present = 0;

    if (strstr(bdaddr, ":"))
        colon_present = 1;

    bdbyte[2] = '\0';

    /* Reverse the BDADDR to LSB first */
    for (i = 5; i >= 0; i--) {
        bdbyte[0] = str_byte[0];
        bdbyte[1] = str_byte[1];
        p[i] = strtol(bdbyte, NULL, 16);

        if (colon_present == 1)
            str_byte += 3;
        else
            str_byte += 2;
    }
}


static int write_bdaddr(HC_BT_HDR *p_buf, char *bdaddr)
{
    uint8_t  *p = NULL;

    p_buf->event = MSG_STACK_TO_HC_HCI_CMD;
    p_buf->offset = 0;
    p_buf->layer_specific = 0;
    p_buf->len = HCI_CMD_PREAMBLE_SIZE + PS_CMD_WRITE_BDADDR_PARAM_SIZE;
    p = (uint8_t *) (p_buf + 1);

    /* Set PS cmd opcode */
    UINT16_TO_STREAM(p, HCI_VSC_PS_CMD);

    *p++ = PS_CMD_WRITE_BDADDR_PARAM_SIZE;
    /* PS_WRITE */
    *p++ = 0x01;
    /* LSB, MSB of PS tag ID */
    *p++ = 0x01;
    *p++ = 0x00;
    /* Length of BDAddress */
    *p++ = 0x06;
    /* Convert and copy the bdaddr */
    convert_bdaddr(bdaddr, (char *)p);

    //Next one is HW_CFG_WRITE_BDADDRESS_DONE
    bt_vendor_cbacks->xmit_cb(HCI_VSC_PS_CMD, p_buf, hw_config_cback);

    return 0;
}

static void write_bdaddr_from_file(HC_BT_HDR *p_buf, int rom_version)
{
    FILE *fd;
    char bdaddr[MAX_BD_ADDR_LEN];
    char bdaddr_file[PATH_MAX];

    snprintf(bdaddr_file, MAXPATHLEN, "%s%x/%s",
            fw_patchfile_path, rom_version, BDADDR_FILE);

    fd = fopen(bdaddr_file, "r");
    if (!fd) {
        ALOGE("Cannot open bdaddr file %s", bdaddr_file);
        /* Set BDAddr to 0 */
        memset(bdaddr, 0, sizeof(bdaddr));
        write_bdaddr(p_buf, bdaddr);
        return;
    }

    /* Get the first token before newline in BD Address file */
    if (fgets(bdaddr, MAX_BD_ADDR_LEN, fd))
        write_bdaddr(p_buf, bdaddr);

    fclose(fd);
}

static void get_ps_file_name(uint32_t devtype, uint32_t rom_version,
                                char *path)
{
    char *filename;

    if (devtype == 0xdeadc0de)
        filename = PS_ASIC_FILE;
    else
        filename = PS_FPGA_FILE;

    snprintf(path, MAXPATHLEN, "%s%x/%s",
                fw_patchfile_path, rom_version, filename);
}

static void get_patch_file_name(uint32_t dev_type, uint32_t rom_version,
                                uint32_t bld_version, char *path)
{
    if ((rom_version == FPGA_ROM_VERSION) && (dev_type != ROM_DEV_TYPE) &&
                (dev_type != 0) && (bld_version == 1))
        path[0] = '\0';
    else
        snprintf(path, MAXPATHLEN, "%s%x/%s",
                    fw_patchfile_path, rom_version, PATCH_FILE);
}

/*******************************************************************************
**
** Function        line_speed_to_userial_baud
**
** Description     helper function converts line speed number into USERIAL baud
**                 rate symbol
**
** Returns         unit8_t (USERIAL baud symbol)
**
*******************************************************************************/
uint8_t line_speed_to_userial_baud(uint32_t line_speed)
{
    uint8_t baud;

    if (line_speed == 4000000)
        baud = USERIAL_BAUD_4M;
    else if (line_speed == 3000000)
        baud = USERIAL_BAUD_3M;
    else if (line_speed == 2000000)
        baud = USERIAL_BAUD_2M;
    else if (line_speed == 1000000)
        baud = USERIAL_BAUD_1M;
    else if (line_speed == 921600)
        baud = USERIAL_BAUD_921600;
    else if (line_speed == 460800)
        baud = USERIAL_BAUD_460800;
    else if (line_speed == 230400)
        baud = USERIAL_BAUD_230400;
    else if (line_speed == 115200)
        baud = USERIAL_BAUD_115200;
    else if (line_speed == 57600)
        baud = USERIAL_BAUD_57600;
    else if (line_speed == 19200)
        baud = USERIAL_BAUD_19200;
    else if (line_speed == 9600)
        baud = USERIAL_BAUD_9600;
    else if (line_speed == 1200)
        baud = USERIAL_BAUD_1200;
    else if (line_speed == 600)
        baud = USERIAL_BAUD_600;
    else
    {
        ALOGE( "userial vendor: unsupported baud speed %d", line_speed);
        baud = USERIAL_BAUD_115200;
    }

    return baud;
}

static void set_cntrlr_baud(HC_BT_HDR *p_buf, int speed)
{
    BTHWDBG(" %s ", __FUNCTION__);

    int baud;
    uint8_t *p;

    /* set controller baud rate to user specified value */

    p_buf->event = MSG_STACK_TO_HC_HCI_CMD;
    p_buf->offset = 0;
    p_buf->layer_specific = 0;
    p_buf->len = HCI_CMD_PREAMBLE_SIZE + CHG_BAUD_CMD_PARAM_SIZE;

    p = (uint8_t *)(p_buf + 1);

    /* Set opcode */
    UINT16_TO_STREAM(p, HCI_VSC_CHG_BAUD_CMD);
    /* Set length */
    *p++ = CHG_BAUD_CMD_PARAM_SIZE;

    /* baud should be set as real baud/100 */
    baud = speed/100;
    /* Set baud LSB */
    *p++ = baud & 0xff;
    /* Set baud MSB */
    *p = (baud >> 8) & 0xff;

    bt_vendor_cbacks->xmit_cb(HCI_VSC_CHG_BAUD_CMD, p_buf, hw_config_cback);
}

void hw_config_start(void)
{
    HC_BT_HDR  *p_buf = NULL;
    uint8_t     *p;

    hw_cfg_cb.state = 0;
    hw_cfg_cb.fw_fd = -1;
    hw_cfg_cb.f_set_baud_2 = FALSE;
    ps_counter=0;
    current_tag_idx=0;
    segment_idx=0;

    BTHWDBG("%s ", __func__);

    /* Start from sending HCI_RESET */

    if (bt_vendor_cbacks)
    {
        p_buf = (HC_BT_HDR *) bt_vendor_cbacks->alloc(BT_HC_HDR_SIZE + \
                                                       HCI_CMD_PREAMBLE_SIZE);
    }

    if (p_buf)
    {
        p_buf->event = MSG_STACK_TO_HC_HCI_CMD;
        p_buf->offset = 0;
        p_buf->layer_specific = 0;
        p_buf->len = HCI_CMD_PREAMBLE_SIZE;

        p = (uint8_t *) (p_buf + 1);
        UINT16_TO_STREAM(p, HCI_RESET);
        *p = 0; /* parameter length */

        hw_cfg_cb.state = HW_CFG_START;

        bt_vendor_cbacks->xmit_cb(HCI_RESET, p_buf, hw_config_cback);
    }
    else
    {
        if (bt_vendor_cbacks)
        {
            ALOGE("vendor lib fw conf aborted [no buffer]");
            bt_vendor_cbacks->fwcfg_cb(BT_VND_OP_RESULT_FAIL);
        }
    }
}

/*******************************************************************************
**
** Function         hw_config_cback
**
** Description      Callback function for controller configuration
**
** Returns          None
**
*******************************************************************************/
void hw_config_cback(void *p_mem)
{
    HC_BT_HDR *p_evt_buf = (HC_BT_HDR *) p_mem;
    char *p_tmp;
    uint8_t *p, status;
    uint16_t opcode;
    HC_BT_HDR *p_buf=NULL;

    status = *((uint8_t *)(p_evt_buf + 1) + HCI_EVT_CMD_CMPL_STATUS_RET_BYTE);
    p = (uint8_t *)(p_evt_buf + 1) + HCI_EVT_CMD_CMPL_OPCODE;
    STREAM_TO_UINT16(opcode,p);

    BTHWDBG("Call back state %d status 0x%x, with opcode 0x%x", hw_cfg_cb.state, status, opcode);

    /* Ask a new buffer big enough to hold any HCI commands sent in here */
    /* HW_CFG_GET_ATH3K_CRC return non-zero if controller has no ram patch or PS config  */
    if (((status == 0)&& bt_vendor_cbacks) ||
        ((status != 0)&& bt_vendor_cbacks && (opcode == 0xfc0b) && (hw_cfg_cb.state == HW_CFG_GET_ATH3K_CRC)))
        p_buf = (HC_BT_HDR *) bt_vendor_cbacks->alloc(BT_HC_HDR_SIZE + HCI_CMD_MAX_LEN);

    if (p_buf != NULL)
    {
        p_buf->event = MSG_STACK_TO_HC_HCI_CMD;
        p_buf->offset = 0;
        p_buf->len = 0;
        p_buf->layer_specific = 0;
        p = (uint8_t *) (p_buf + 1);

        switch (hw_cfg_cb.state)
        {
            case HW_CFG_START:
                {
                    hw_cfg_cb.state = HW_CFG_SET_CNTRLR_BAUD;
                    set_cntrlr_baud(p_buf, UART_TARGET_BAUD_RATE);
                }
                break;

            case HW_CFG_SET_CNTRLR_BAUD:
                {
                    /* Set UART Baud after Ctrl Baud is set */
                    userial_vendor_set_baud(line_speed_to_userial_baud(UART_TARGET_BAUD_RATE));


                    //Next one is HW_CFG_GET_DEVICE_TYPE

                    //*  --------------------------------------------
                    //*  |  HC_BT_HDR  |  HCI command         |
                    //*  --------------------------------------------
                    //*
                    //*     AR3002 HCI command
                    //*  --------------------------------------------------------
                    //*  |  packet type(1) | opcode (2)  | length(1) | command parameter |
                    //*  --------------------------------------------------------
                    //*
                    //* So, the packet format will be:
                    //*  ------------------------------------------------------------------------
                    //*  |  HC_BT_HDR  | packet type(1) | opcode (2)  | length(1) | command parameter |
                    //*  ------------------------------------------------------------------------

                    p_buf->len = HCI_CMD_PREAMBLE_SIZE + GET_DEV_TYPE_CMD_PARAM_SIZE;

                    /* Set opcode */
                    UINT16_TO_STREAM(p, HCI_VSC_GET_DEV_TYPE);
                    /* Set length */
                    *p++ = GET_DEV_TYPE_CMD_PARAM_SIZE;

                    /* Set Register Address */
                    *p++ = (uint8_t)DEV_REGISTER;
                    *p++ = (uint8_t)(DEV_REGISTER >> 8);
                    *p++ = (uint8_t)(DEV_REGISTER >> 16);
                    *p++ = (uint8_t)(DEV_REGISTER >> 24);
                    /* Set length to read */
                    *p = 0x04;

                    hw_cfg_cb.state = HW_CFG_GET_DEVICE_TYPE;
                    bt_vendor_cbacks->xmit_cb(HCI_VSC_GET_DEV_TYPE, p_buf, hw_config_cback);
                }
                break;
            case HW_CFG_GET_DEVICE_TYPE:
                {
                    p_tmp = (char *) (p_evt_buf + 1 ) + HCI_EVT_CMD_CMPL_RESPONSE;

                    /* MSB of device type */
                    dev_type = *(p_tmp + 3);
                    dev_type = (dev_type << 8) | *(p_tmp + 2);
                    /* LSB of device type */
                    dev_type = (dev_type << 8) | *(p_tmp + 1);
                    dev_type = (dev_type << 8) | *(p_tmp );

                    ALOGI("AR3002 dev_type %x", dev_type);

                    //Next one is  HW_CFG_GET_ATH3K_VERSION

                    p_buf->len = HCI_CMD_PREAMBLE_SIZE;

                    /* Set opcode */
                    UINT16_TO_STREAM(p, HCI_VSC_GET_VERSION);
                    *p = 0;

                    hw_cfg_cb.state = HW_CFG_GET_ATH3K_VERSION;
                    bt_vendor_cbacks->xmit_cb(HCI_VSC_GET_VERSION, p_buf, hw_config_cback);

                }
                break;
            case HW_CFG_GET_ATH3K_VERSION:
                {
                    p_tmp = (char *) (p_evt_buf +1 ) + HCI_EVT_CMD_CMPL_RESPONSE;

                    /* Assemble ROM version from response */
                    rom_version = *(p_tmp + 3);
                    rom_version = (rom_version << 8) | *(p_tmp + 2);
                    rom_version = (rom_version << 8) | *(p_tmp + 1);
                    rom_version = (rom_version << 8) | *(p_tmp);

                    /* Assemble Build version from response */
                    bld_version = *(p_tmp + 7);
                    bld_version = (bld_version << 8) | *(p_tmp + 6);
                    bld_version = (bld_version << 8) | *(p_tmp + 5);
                    bld_version = (bld_version << 8) | *(p_tmp + 4);

                    ALOGI("AR3002 rom_version %x ,bld_version %x ", rom_version, bld_version);

                    //Next one is HW_CFG_GET_ATH3K_CRC
                    p_buf->len = HCI_CMD_PREAMBLE_SIZE + PS_CMD_GET_CRC_PARAM_SIZE;

                    /* Set opcode */
                    UINT16_TO_STREAM(p, HCI_VSC_PS_CMD);
                    /* Set length */
                    *p++ = PS_CMD_GET_CRC_PARAM_SIZE;
                    *p++ = VERIFY_CRC;
                    /* Memory region LSB, MSB */
                    *p++ = PS_REGION|PATCH_REGION;
                    *p++ = (PS_REGION|PATCH_REGION) >> 8;
                    *p   = 0; /* Length */

                    hw_cfg_cb.state = HW_CFG_GET_ATH3K_CRC;

                    bt_vendor_cbacks->xmit_cb(HCI_VSC_PS_CMD, p_buf, hw_config_cback);
                }
                break;
            case HW_CFG_GET_ATH3K_CRC:
                {
                    FILE *fd = 0;

                    if  ( status == 0 )
                    {
                        ALOGI("bt vendor lib: AR3K already has ram patch and PS configuration;\
                                                        there is no need to download them. ");
                        /* Set bdaddr, Next one is HW_CFG_WRITE_BDADDRESS_DONE */
                        hw_cfg_cb.state = HW_CFG_WRITE_BDADDRESS_DONE;
                        write_bdaddr_from_file(p_buf, rom_version);
                        break;
                    }

                    /* Get PS config file name */
                    get_ps_file_name(dev_type, rom_version, ps_file);
                    /* Get Ram Patch file name */
                    get_patch_file_name(dev_type, rom_version, bld_version, patch_file);

                    /* Open PS config file */
                    fd = fopen(ps_file, "r");
                    if (!fd) {
                        ALOGE("PS config file open error");
                        /* No PS config file, Set baud, Next one is HW_CFG_CNTRLR_BAUD_UPDATE */
                        hw_cfg_cb.state = HW_CFG_CNTRLR_BAUD_UPDATE;
                        set_cntrlr_baud(p_buf, UART_TARGET_BAUD_RATE);
                        break;
                    }

                    /* Parse the PS config file and get number of tags */
                    tag_count = ath_parse_ps(fd);

                    /* Close PS config file */
                    fclose(fd);

                    if (tag_count < 0) {
                        ALOGE("tag_count < 0\n");
                        /* Set bdaddr, Next one is HW_CFG_CNTRLR_BAUD_UPDATE */
                        hw_cfg_cb.state = HW_CFG_CNTRLR_BAUD_UPDATE;
                        set_cntrlr_baud(p_buf, UART_TARGET_BAUD_RATE);
                        break;
                    }

                    rampatch_fd = fopen(patch_file, "r");
                    if (!rampatch_fd) {
                        ALOGE("Rampatch file open error");
                        /* No RamPatchfile, Set bdaddr, Next one is HW_CFG_WRITE_BDADDRESS_DONE */
                        hw_cfg_cb.state = HW_CFG_WRITE_BDADDRESS_DONE;
                        write_bdaddr_from_file(p_buf, rom_version);
                        break;
                    }
                    else {
                        if (set_patch_ram_id(p_buf) < 0) {
                            ALOGE("PS patch write error");
                            /* Set baud, Next one is HW_CFG_CNTRLR_BAUD_UPDATE */
                            hw_cfg_cb.state = HW_CFG_CNTRLR_BAUD_UPDATE;
                            set_cntrlr_baud(p_buf, UART_TARGET_BAUD_RATE);
                            break;
                        }
                    }
                }
                break;
            case HW_CFG_PSPATCH_START_DOWNLOAD:
            case HW_CFG_PSPATCH_DOWNLOADING:
                {
                    if (write_patch(p_buf) < 0) {
                        ALOGE("PS patch write error");
                        /* Set baud, Next one is HW_CFG_CNTRLR_BAUD_UPDATE */
                        hw_cfg_cb.state = HW_CFG_CNTRLR_BAUD_UPDATE;
                        set_cntrlr_baud(p_buf, UART_TARGET_BAUD_RATE);
                        break;
                    }
                }
                break;
            case HW_CFG_PSPATCH_DOWNLOADED:
                {
                    /* RamPatch download finished, close the file */
                    fclose(rampatch_fd);

                    if (segment_idx < 0) {
                        ALOGE("segment_idx < 0\n");
                    }
                    hw_cfg_cb.state = HW_CFG_CONFIG_RESET;
                    if (ps_reset_config(p_buf) < 0) {
                        ALOGE("PS patch write error");
                        /* Set baud, Next one is HW_CFG_CNTRLR_BAUD_UPDATE */
                        hw_cfg_cb.state = HW_CFG_CNTRLR_BAUD_UPDATE;
                        set_cntrlr_baud(p_buf, UART_TARGET_BAUD_RATE);
                        break;
                    }
                }
                break;
            case HW_CFG_CONFIG_RESET:
            case HW_CFG_CONFIG_DOWNLOADING:
                {
                    if (ps_config_download(p_buf, current_tag_idx) < 0) {
                        ALOGE("PS config write error");
                        /* Set baud, Next one is HW_CFG_CNTRLR_BAUD_UPDATE */
                        hw_cfg_cb.state = HW_CFG_CNTRLR_BAUD_UPDATE;
                        set_cntrlr_baud(p_buf, UART_TARGET_BAUD_RATE);
                        break;
                    }
                    current_tag_idx++;
                    if(tag_count == current_tag_idx) {
                        hw_cfg_cb.state = HW_CFG_WRITE_BDADDRESS;
                    }
                    else
                        hw_cfg_cb.state = HW_CFG_CONFIG_DOWNLOADING;
                }
                break;
            case HW_CFG_WRITE_BDADDRESS:
                {
                    /* Write BDADDR */
                    hw_cfg_cb.state = HW_CFG_WRITE_BDADDRESS_DONE;
                    write_bdaddr_from_file(p_buf, rom_version);
                }
                break;
            case HW_CFG_WRITE_BDADDRESS_DONE:
                {
                    /* Send HCI_RESET to enable patch */
                    p_buf->event = MSG_STACK_TO_HC_HCI_CMD;
                    p_buf->offset = 0;
                    p_buf->layer_specific = 0;
                    p_buf->len = HCI_CMD_PREAMBLE_SIZE;

                    p = (uint8_t *) (p_buf + 1);
                    UINT16_TO_STREAM(p, HCI_RESET);
                    *p = 0; /* parameter length */

                    hw_cfg_cb.state = HW_CFG_HCI_RESET;

                    bt_vendor_cbacks->xmit_cb(HCI_RESET, p_buf, hw_config_cback);
                }
                break;
            case HW_CFG_HCI_RESET:
                {
                    /* Set BT controller baud */
                    hw_cfg_cb.state = HW_CFG_CNTRLR_BAUD_UPDATE;

                    set_cntrlr_baud(p_buf, UART_TARGET_BAUD_RATE);
                }
                break;
            case HW_CFG_CNTRLR_BAUD_UPDATE:
                {
                    /* Set the UART port Baud */
                    userial_vendor_set_baud(line_speed_to_userial_baud(UART_TARGET_BAUD_RATE));

                    hw_cfg_cb.state = 0;
                    bt_vendor_cbacks->dealloc(p_buf);
                    bt_vendor_cbacks->fwcfg_cb(BT_VND_OP_RESULT_SUCCESS);
                }
                break;
            default:
                break;
        } // switch(hw_cfg_cb.state)
    } // if (p_buf != NULL)

    /* Free the RX event buffer */
    if (bt_vendor_cbacks)
        bt_vendor_cbacks->dealloc(p_evt_buf);
}

/*******************************************************************************
**
** Function        hw_set_patch_file_path
**
** Description     Set the location of firmware patch file
**
** Returns         0 : Success
**                 Otherwise : Fail
**
*******************************************************************************/
int hw_set_patch_file_path(char *p_conf_name, char *p_conf_value, int param)
{
    strcpy(fw_patchfile_path, p_conf_value);

    return 0;
}

