/*
 * Copyright (C) 2016 The Android Open Source Project
 *
 * Copyright 2023 NXP
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef AVB_TRUSTY_H_
#define AVB_TRUSTY_H_

#define AVB_PORT "com.android.trusty.avb"
#define AVB_MAX_BUFFER_LENGTH 2048

enum avbCommand {
    AVB_REQ_SHIFT = 1,
    AVB_RESP_BIT = 1,

    READ_ROLLBACK_INDEX = (0 << AVB_REQ_SHIFT),
    WRITE_ROLLBACK_INDEX = (1 << AVB_REQ_SHIFT),
    AVB_GET_VERSION = (2 << AVB_REQ_SHIFT),
    READ_PERMANENT_ATTRIBUTES = (3 << AVB_REQ_SHIFT),
    WRITE_PERMANENT_ATTRIBUTES = (4 << AVB_REQ_SHIFT),
    READ_LOCK_STATE = (5 << AVB_REQ_SHIFT),
    WRITE_LOCK_STATE = (6 << AVB_REQ_SHIFT),
    LOCK_BOOT_STATE = (7 << AVB_REQ_SHIFT),
    READ_VBMETA_PUBLIC_KEY = (8 << AVB_REQ_SHIFT),
    WRITE_VBMETA_PUBLIC_KEY = (9 << AVB_REQ_SHIFT),
    WRITE_OEM_UNLOCK_DEVICE_PERMISSION = (10 << AVB_REQ_SHIFT),
    READ_OEM_UNLOCK_DEVICE_PERMISSION = (11 << AVB_REQ_SHIFT),
};

/**
 * enum avbError - error codes for AVB protocol
 * @AVB_ERROR_NONE:         All OK
 * @AVB_ERROR_INVALID:      Invalid input
 * @AVB_ERROR_INTERNAL:     Error occurred during an operation in Trusty
 */
enum avbError {
    AVB_ERROR_NONE = 0,
    AVB_ERROR_INVALID = -1,
    AVB_ERROR_INTERNAL = -2,
};

/**
 * avbMessage - Serial header for communicating with AVB server
 * @cmd:     the command. Payload must be a serialized buffer of the
 *           corresponding request object.
 * @result:  resulting error code for message, one of avb_error.
 * @payload: start of the serialized command specific payload
 */
struct avbMessage {
    uint32_t cmd;
    uint32_t result;
    uint8_t payload[0];
};

class avbOemUnlockIpc {
public:
    avbError avbCall(int handler, avbCommand cmd, void* in, uint32_t in_size, void* out,
                     uint32_t* out_size);
    avbError readDeviceUnlockPermission(uint8_t* status);
    avbError writeDeviceUnlockPermission(uint8_t status);
};

#endif /* AVB_TRUSTY_H_ */
