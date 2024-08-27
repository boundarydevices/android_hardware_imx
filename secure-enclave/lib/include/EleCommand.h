/*
 * Copyright 2024 NXP
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
#ifndef __ELE_COMMAND_H__
#define __ELE_COMMAND_H__

#include <EleMessage.h>
#include <sys/ioctl.h>

/* definations for message communication */
#define ELE_VERSION_BASELINE (0x06u)
#define ELE_VERSION_HSM (0x07u)
#define ELE_COMMAND_SUCCEED (0xd6u)
#define ELE_COMMAND_FAILED (0x29u)
#define ELE_REQUEST_TAG (0x17u)
#define ELE_RESPONSE_TAG (0xe1u)

/* operations for ELE keystore */
#define KEY_STORE_OPERATION_LOAD (0u)
#define KEY_STORE_OPERATION_CREATE (0x1u << 0)

/* definations for ELE commands */
#define SESSION_OPEN_REQ (0x10u)
#define SESSION_CLOSE_REQ (0x11u)
#define KEY_STORE_OPEN_REQ (0x30u)
#define KEY_STORE_CLOSE_REQ (0x31u)
#define KEY_MANAGEMENT_OPEN_REQ (0x40u)
#define KEY_MANAGEMENT_CLOSE_REQ (0x41u)
#define KEY_GENERATE_KEY_REQ (0x42u)
#define KEY_GET_ATTRIBUTE_REQ (0x4Cu)
#define KEY_DELETE_KEY_REQ (0x4Eu)
#define KEY_CIPHER_OPEN_REQ (0x60u)
#define KEY_CIPHER_CLOSE_REQ (0x61u)
#define KEY_CIPHER_OPERATION_REQ (0x62u)
#define KEY_CIPHER_AE_OPERATION_REQ (0x64u)
#define KEY_SIGN_GENERATE_OPEN_REQ (0x70u)
#define KEY_SIGN_GENERATE_CLOSE_REQ (0x71u)
#define KEY_SIGN_GENERATE_REQ (0x72u)
#define KEY_SIGN_VERIFY_OPEN_REQ (0x80u)
#define KEY_SIGN_VERIFY_CLOSE_REQ (0x81u)
#define KEY_SIGN_VERIFY_REQ (0x82u)
#define STORAGE_OPEN_REQ (0xE0u)
#define STORAGE_CLOSE_REQ (0xE1u)
#define STORAGE_MASTER_IMPORT (0xE2u)
#define STORAGE_MASTER_EXPORT_REQ (0xE3u)
#define STORAGE_EXPORT_FINISH_REQ (0xE4u)
#define STORAGE_CHUNK_EXPORT_REQ (0xE5u)
#define STORAGE_CHUNK_GET_REQ (0xE6u)
#define STORAGE_CHUNK_GET_DONE_REQ (0xE7u)
#define STORAGE_KEY_DB_REQ (0xE8u)
#define STORAGE_NVM_LAST_CMD (STORAGE_KEY_DB_REQ + 1)

/* ioctl */
#define ELE_MU_IO_FLAGS_IS_OUTPUT (0x0u)
#define ELE_MU_IO_FLAGS_IS_INPUT (0x01u)
#define ELE_MU_IO_FLAGS_USE_SEC_MEM (0x02u)
#define ELE_MU_IO_FLAGS_USE_SHORT_ADDR (0x04u)

#define ELE_MU_IOCTL (0x0A)
#define ELE_MU_IOCTL_ENABLE_CMD_RCV _IO(ELE_MU_IOCTL, 0x01)
#define ELE_MU_IOCTL_SHARED_BUF_CFG _IOW(ELE_MU_IOCTL, 0x02, struct ele_mu_ioctl_shared_mem_cfg)
#define ELE_MU_IOCTL_SETUP_IOBUF _IOWR(ELE_MU_IOCTL, 0x03, struct ele_mu_ioctl_iobuf)
#define ELE_MU_IOCTL_GET_MU_INFO _IOR(ELE_MU_IOCTL, 0x04, struct ele_mu_info)
#define ELE_MU_IOCTL_SIGNED_MESSAGE     _IOWR(ELE_MU_IOCTL, 0x05,

#define NVM_EXPORT_STATUS_SUCCESS (0xBA2CC2AB)
#define NVM_CHUNK_GET_CHUNK_SUCCESS (0xCA3BB3AC)
#define ELE_SIGNATURE_VERIFY_SUCCESS (0x5A3CC3A5)
#define ELE_SIGNATURE_VERIFY_FAILURE (0x2B4DD4B2)

#endif //__ELE_COMMAND_H__
