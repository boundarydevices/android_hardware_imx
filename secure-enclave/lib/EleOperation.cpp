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

#define LOG_TAG "ImxSecureEnclaveOperation"

#include <EleOperation.h>
#include <fcntl.h>

static const char hsm_mu_path[] = "/dev/hsm1_ch0";
static const char hsm_mu_nvm_path[] = "/dev/hsm1_ch1";
static const char hsm_mu_secondary_path[] = "/dev/hsm1_ch2";

#define SIZE_MSG(msg) sizeMsg(sizeof(msg))
#define ROUND_UP(x, alignment) ((x + alignment - 1) & ~(alignment - 1))

static uint32_t sizeMsg(size_t len) {
    uint32_t len_words = ROUND_UP(len, sizeof(uint32_t));

    /* Include the header size */
    return len_words + 4;
}

static void dumpResponseCode(struct mu_rsp *rsp) {
    ALOGE("ELE Response code: status: 0x%" PRIx8 " rating: 0x%" PRIx8 " ext: 0x%" PRIx16 "",
          rsp->status, rsp->rating, rsp->rating_extension);
}

static void dumpMessage(struct mu_msg *msg) {
    uint32_t *data = (uint32_t *)msg;
    int i = 0;

    ALOGE("==== ELE message dump ==== ");
    for (i = 0; i < msg->header.size; i++) {
        ALOGE("word[%d]: 0x%08x", i, data[i]);
    }
    ALOGE("========================== ");
}

/* Calculate the CRC, return the CRC on success.
 * buf - pointer to the source buffer
 * len - length of the source buffer in words(4 bytes)
 * */
static uint32_t calCRC(uint32_t *buf, uint32_t len) {
    uint32_t crc = 0, i;

    for (i = 0; i < len; i++) crc ^= buf[i];

    return crc;
}

static void addCRC(struct mu_msg *msg) {
    uint32_t crc = 0, *buf;

    buf = (uint32_t *)(msg);
    /* need to exclude the crc itself */
    crc = calCRC(buf, msg->header.size - 1);

    /* the crc is in the last word of data */
    msg->data.u32[msg->header.size - 2] = crc;
}

static void buildMsgHeader(struct mu_msg *msg, uint32_t cmd, uint32_t size, uint8_t tag) {
    msg->header.ver = ELE_VERSION_HSM;
    msg->header.size = (size >> 2);
    msg->header.cmd = cmd;
    msg->header.tag = tag;
}

ErrorType EleOperation::sendMuMsg(void *msg, uint32_t reqLen) {
    std::mutex lock;
    std::unique_lock<std::mutex> stateLock(lock);

#ifdef ELE_DEBUG
    dumpMessage((struct mu_msg *)msg);
#endif

    if (TEMP_FAILURE_RETRY(write(this->fd, msg, reqLen)) != reqLen) {
        ALOGE("Failed to send MU data!");
        dumpMessage((struct mu_msg *)msg);
        return ELE_COMMUNICATION_ERROR;
    }
    return ELE_NO_ERROR;
}

uint32_t EleOperation::receiveMuMsg(void *msg, uint32_t respLen) {
    std::mutex lock;
    std::unique_lock<std::mutex> stateLock(lock);
    int ret = 0;

    ret = read(this->fd, msg, respLen);
    if (ret <= 0) {
        ALOGE("Failed to read MU data!");
        ret = 0;
    } else {
#ifdef ELE_DEBUG
        dumpMessage((struct mu_msg *)msg);
#endif
    }

    return ret;
}

ErrorType EleOperation::receiveNVMRequest(struct mu_msg *cmd, uint32_t *cmdLen, uint32_t *cmdID) {
    uint32_t ret;

    if (!cmd || !cmdID || !cmdLen) {
        ALOGE("Invalid input parameters!");
        return ELE_INVALID_ARGS;
    }

    ret = receiveMuMsg(cmd, *cmdLen);
    if (ret == 0 || ret != (cmd->header.size << 2)) {
        ALOGE("Failed to receive ELE request or message is corrupted!");
        return ELE_INVALID_MESSAGE;
    }

    *cmdLen = cmd->header.size << 2;
    *cmdID = cmd->header.cmd;

    return ELE_NO_ERROR;
}

// TODO support both NS file and secure storage access
/*
 * Read the file "fileName" to buffer "dst" with length "size".
 *
 * fileName - the path of the file.
 * dst - the destination buffer,
 * size - the expected size to read.
 *
 * This function returns the actual length of data read to the buffer, 0 for failure.
 */
uint32_t readStorageFile(char *fileName, uint8_t *dst, uint32_t size) {
    int fd;
    int len = 0;

    if (!fileName || !dst || !size) {
        return 0;
    }

    /* open the file as read only */
    fd = TEMP_FAILURE_RETRY(open(fileName, O_RDONLY));
    if (fd >= 0) {
        len = TEMP_FAILURE_RETRY(read(fd, dst, size));
        close(fd);
        if (len < 0) {
            ALOGE("Failed to read file:%s !", fileName);
        } else
            return len;
    }

    return 0;
}

// TODO support both NS file and secure storage access
/*
 * Write the buffer "src" with length "size" to the file "fileName".
 *
 * fileName - the path of the file.
 * src - the source buffer,
 * size - the expected size to write.
 *
 * This function returns the actual length of data write to the file, 0 for failure.
 */
uint32_t writeStorageFile(char *fileName, uint8_t *src, uint32_t size) {
    int fd;
    int len = 0;

    if (!fileName || !src || !size) {
        return 0;
    }

    fd = TEMP_FAILURE_RETRY(open(fileName, O_CREAT | O_WRONLY | O_SYNC, S_IRUSR | S_IWUSR));
    if (fd >= 0) {
        len = TEMP_FAILURE_RETRY(write(fd, src, size));
        close(fd);
        if (len < 0) {
            ALOGE("Failed to write file:%s !", fileName);
        } else
            return len;
    }

    return 0;
}

uint32_t EleOperation::retrivePhyAddress(uint8_t *src, uint32_t size, uint32_t flag) {
    std::mutex lock;
    std::unique_lock<std::mutex> stateLock(lock);
    struct ele_mu_ioctl_iobuf io;
    int error;

    io.user_buf = src;
    io.length = size;
    io.flags = flag;

    error = TEMP_FAILURE_RETRY(ioctl(this->fd, ELE_MU_IOCTL_SETUP_IOBUF, &io));
    if (error != 0) {
        io.ele_addr = 0;
    }

    /* only takes the low 32 bytes address */
    return (io.ele_addr) & (0xffffffff);
}

ErrorType EleOperation::eleOpenDeviceNode() {
    int error;
    const char *path = nullptr;

    /* Make sure we are not opening without close. */
    if (fd >= 0) {
        ALOGE("Open another ele device without closing the previous one!");
        return ELE_GENERAL_ERROR;
    }

    /* Select the correct device node according to the MU type */
    if (mu_type == MU_CHANNEL_PLAT_HSM)
        path = hsm_mu_path;
    else if (mu_type == MU_CHANNEL_PLAT_HSM_NVM)
        path = hsm_mu_nvm_path;
    else if (mu_type == MU_CHANNEL_PLAT_HSM_SECONDARY) {
        path = hsm_mu_secondary_path;
    } else {
        ALOGE("Invalid MU device type!");
        return ELE_INVALID_MU_TYPE;
    }

    ALOGI("Opening ELE MU path: %s.", path);
    fd = TEMP_FAILURE_RETRY(open(path, O_RDWR));
    if (fd < 0) {
        ALOGE("Failed to open ELE device, err = %d.", fd);
        return ELE_COMMUNICATION_ERROR;
    }

    /* Get MU info */
    error = TEMP_FAILURE_RETRY(ioctl(fd, ELE_MU_IOCTL_GET_MU_INFO, &mu_info));
    if (error != 0) {
        ALOGE("Failed to get mu info, err = %d.", error);
        close(fd);
        fd = -1;
        return ELE_COMMUNICATION_ERROR;
    }

    /* NVM: Configure the device to accept incoming commands. */
    if ((mu_type == MU_CHANNEL_PLAT_HSM_NVM) &&
        TEMP_FAILURE_RETRY(ioctl(fd, ELE_MU_IOCTL_ENABLE_CMD_RCV))) {
        ALOGE("Failed to configure for NVM, err = %d.", error);
        close(fd);
        fd = -1;
        return ELE_COMMUNICATION_ERROR;
    }

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleCloseDeviceNode() {
    if (fd != -1) {
        close(fd);
        fd = -1;
    }
    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleSendAndReciveMsg(struct mu_msg *msg, uint32_t req_len,
                                            uint32_t *respLen) {
    struct mu_rsp *rsp;
    uint32_t crc = 0;

    /* valid "fd" means ele mu device node ready */
    if (fd < 0) {
        ALOGE("ELE MU device is not ready!");
        return ELE_NOT_INITED;
    }

    if (msg->header.tag != ELE_REQUEST_TAG) {
        ALOGE("Wrong request tag: 0x%x!", msg->header.tag);
        return ELE_INVALID_MESSAGE;
    }

    /* Send the request */
    if (sendMuMsg((void *)msg, req_len) != ELE_NO_ERROR)
        return ELE_COMMUNICATION_ERROR;

    /* Read the response */
    if (receiveMuMsg((void *)msg, *respLen) != *respLen)
        return ELE_COMMUNICATION_ERROR;

    /* Check response crc */
    if (msg->header.size > STORAGE_NB_WORDS_MAX_NO_CRC) {
        crc = calCRC((uint32_t *)msg, (msg->header.size - 1));
        if (crc != msg->data.u32[msg->header.size - 2]) {
            ALOGE("Wrong response crc detected!");
            return ELE_INVALID_ARGS;
        }
    }

    /* Check the result */
    rsp = (struct mu_rsp *)(msg->data.u8);
    if (msg->header.tag != ELE_RESPONSE_TAG) {
        ALOGE("Wrong response tag: 0x%x!", msg->header.tag);
        return ELE_INVALID_MESSAGE;
    }
    if (rsp->status != ELE_COMMAND_SUCCEED) {
        ALOGE("ELE returns wrong response code.");
        dumpResponseCode(rsp);
        return (ErrorType)(msg->data.u16[0]);
    } else if (rsp->rating != 0) {
        ALOGE("ELE command succeed with warning, rating: %02x.", rsp->rating);
    }

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleOpenSession() {
    struct open_session_msg_cmd *open_session_args;
    struct open_session_msg_rsp *open_session_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* We only allow one ELE session */
    if (session_handle != 0) {
        ALOGE("ELE session is already opened!");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct open_session_msg_cmd);
    open_session_args = (struct open_session_msg_cmd *)(msg.data.u8);
    open_session_args->interrupt_num = mu_info.interrupt_idx;
    buildMsgHeader(&msg, SESSION_OPEN_REQ, req_len, mu_info.cmd_tag);

    resp_len = SIZE_MSG(struct open_session_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to open ELE session!");
        return error;
    }

    open_session_resp = (struct open_session_msg_rsp *)(msg.data.u8);
    if (open_session_resp->session_handle == 0) {
        ALOGE("Invalid session handler!");
        return ELE_INVALID_MESSAGE;
    }
    session_handle = open_session_resp->session_handle;
    ALOGI("ELE session opened, handle: 0x%x", session_handle);

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleCloseSession() {
    struct close_session_msg_cmd *close_session_args;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the session handle before close */
    if (session_handle == 0) {
        ALOGE("Can't close invalid session!");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct close_session_msg_cmd);
    close_session_args = (struct close_session_msg_cmd *)(msg.data.u8);
    close_session_args->session_handle = session_handle;
    buildMsgHeader(&msg, SESSION_CLOSE_REQ, req_len, mu_info.cmd_tag);

    resp_len = SIZE_MSG(struct close_session_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to close ELE session!");
        return error;
    }

    session_handle = 0;
    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleOpenKeyStore(uint32_t keyStoreId, uint32_t nonce, uint8_t op,
                                        uint32_t *keyStoreHandler) {
    struct key_store_open_msg_cmd *open_keystore_args;
    struct key_store_open_msg_rsp *open_keystore_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the session handle before opening keystore */
    if (session_handle == 0) {
        ALOGE("ELE session is not yet opened!");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct key_store_open_msg_cmd);
    open_keystore_args = (struct key_store_open_msg_cmd *)(msg.data.u8);
    open_keystore_args->session_handle = session_handle;
    open_keystore_args->id = keyStoreId;
    open_keystore_args->nonce = nonce;
    open_keystore_args->flags = op;

    buildMsgHeader(&msg, KEY_STORE_OPEN_REQ, req_len, mu_info.cmd_tag);

    /* add the CRC */
    addCRC(&msg);

    resp_len = SIZE_MSG(struct key_store_open_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to open ELE keystore with ID: 0x%08x. Already exist?", keyStoreId);
        return error;
    }

    open_keystore_resp = (struct key_store_open_msg_rsp *)(msg.data.u8);
    if (open_keystore_resp->key_store_handle == 0) {
        ALOGE("Invalid key store handler!");
        return ELE_INVALID_MESSAGE;
    }
    *keyStoreHandler = open_keystore_resp->key_store_handle;
    ALOGI("ELE keystore opened, handle: 0x%x", *keyStoreHandler);

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleCloseKeyStore(uint32_t keyStoreHandler) {
    struct key_store_close_msg_cmd *close_keystore_args;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the keystore handle before closing */
    if (keyStoreHandler == 0) {
        ALOGE("Invalid keystore handler!");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct key_store_close_msg_cmd);
    close_keystore_args = (struct key_store_close_msg_cmd *)(msg.data.u8);
    close_keystore_args->key_store_handle = keyStoreHandler;

    buildMsgHeader(&msg, KEY_STORE_CLOSE_REQ, req_len, mu_info.cmd_tag);

    resp_len = SIZE_MSG(struct key_store_close_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to close ELE keystore!");
        return error;
    }

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleOpenStorage(uint32_t *nvmStorageHandle) {
    struct storage_open_msg_cmd *open_storage_args;
    struct storage_open_msg_rsp *open_storage_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the session handle before opening nvm storage */
    if (session_handle == 0) {
        ALOGE("ELE session is not yet opened!");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct storage_open_msg_cmd);
    open_storage_args = (struct storage_open_msg_cmd *)(msg.data.u8);
    open_storage_args->session_handle = session_handle;
    buildMsgHeader(&msg, STORAGE_OPEN_REQ, req_len, mu_info.cmd_tag);

    /* add the CRC */
    addCRC(&msg);

    resp_len = SIZE_MSG(struct storage_open_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to open nvm storage!");
        return error;
    }

    open_storage_resp = (struct storage_open_msg_rsp *)(msg.data.u8);
    if (open_storage_resp->nvm_storage_hdl == 0) {
        ALOGE("Invalid nvm storage handler!");
        return ELE_INVALID_MESSAGE;
    }

    *nvmStorageHandle = open_storage_resp->nvm_storage_hdl;
    ALOGI("ELE nvm storage opened, handle: 0x%x", *nvmStorageHandle);

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleCloseStorage(uint32_t nvmStorageHandle) {
    struct storage_close_msg_cmd *close_storage_args;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the storage handle before closing */
    if (nvmStorageHandle == 0) {
        ALOGE("Invalid nvmStorageHandle handler!");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct storage_close_msg_cmd);
    close_storage_args = (struct storage_close_msg_cmd *)(msg.data.u8);
    close_storage_args->nvm_storage_handle = nvmStorageHandle;
    buildMsgHeader(&msg, STORAGE_CLOSE_REQ, req_len, mu_info.cmd_tag);

    resp_len = SIZE_MSG(struct storage_close_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to close nvm storage!");
        return error;
    }

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleOpenKeyManagement(uint32_t keyStoreHandler, uint32_t *keyMgtHandle) {
    struct key_mgt_open_msg_cmd *open_key_mgt_args;
    struct key_mgt_open_msg_rsp *open_key_mgt_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the key store handle before opening key management */
    if (keyStoreHandler == 0) {
        ALOGE("Invalid keystore handle");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct key_mgt_open_msg_cmd);
    open_key_mgt_args = (struct key_mgt_open_msg_cmd *)(msg.data.u8);
    open_key_mgt_args->key_store_handle = keyStoreHandler;

    buildMsgHeader(&msg, KEY_MANAGEMENT_OPEN_REQ, req_len, mu_info.cmd_tag);

    /* add the CRC */
    addCRC(&msg);

    resp_len = SIZE_MSG(struct key_mgt_open_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to open key management!");
        return error;
    }

    open_key_mgt_resp = (struct key_mgt_open_msg_rsp *)(msg.data.u8);
    if (open_key_mgt_resp->key_mgt_handle == 0) {
        ALOGE("Invalid key management handle!");
        return ELE_INVALID_MESSAGE;
    }
    *keyMgtHandle = open_key_mgt_resp->key_mgt_handle;
    ALOGI("ELE key management opened, handle: 0x%x", *keyMgtHandle);

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleCloseKeyManagement(uint32_t keyMgtHandle) {
    struct key_mgt_close_msg_cmd *close_key_mgt_args;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the key management handle before closing */
    if (keyMgtHandle == 0) {
        ALOGE("Invalid key management handler!");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct key_mgt_close_msg_cmd);
    close_key_mgt_args = (struct key_mgt_close_msg_cmd *)(msg.data.u8);
    close_key_mgt_args->key_mgt_handle = keyMgtHandle;

    buildMsgHeader(&msg, KEY_MANAGEMENT_CLOSE_REQ, req_len, mu_info.cmd_tag);

    resp_len = SIZE_MSG(struct key_mgt_close_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to close ELE key management!");
        return error;
    }

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleGenerateKey(uint32_t keyMgtHandle, uint32_t *keyId,
                                       gen_key_attribute *keyAttribute) {
    struct gen_key_msg_cmd *gen_key_args;
    struct gen_key_msg_rsp *gen_key_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    if (!keyId || !keyAttribute) {
        ALOGE("Invalid input parameters!");
        return ELE_INVALID_MESSAGE;
    }

    /* check the key management handle before generating key */
    if (keyMgtHandle == 0) {
        ALOGE("Invalid key management handler!");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct gen_key_msg_cmd);
    gen_key_args = (struct gen_key_msg_cmd *)(msg.data.u8);
    gen_key_args->key_mgt_handle = keyMgtHandle;
    gen_key_args->key_id = *keyId;
    gen_key_args->pub_key_size = keyAttribute->pub_key_size;
    gen_key_args->key_group = keyAttribute->key_group;
    gen_key_args->type = keyAttribute->type;
    gen_key_args->size_bits = keyAttribute->size_bits;
    gen_key_args->lifetime = keyAttribute->lifetime;
    gen_key_args->usage = keyAttribute->usage;
    gen_key_args->permit_algo = keyAttribute->permit_algo;
    gen_key_args->lifecycle = keyAttribute->lifecycle;
    gen_key_args->flags = keyAttribute->flags;
    /* Input address should be virtual address and needs to be
     * converted to physical address.
     */
    if (keyAttribute->pub_key_lsb_addr != nullptr && keyAttribute->pub_key_size != 0) {
        gen_key_args->pub_key_lsb_addr =
                retrivePhyAddress(keyAttribute->pub_key_lsb_addr, keyAttribute->pub_key_size,
                                  ELE_MU_IO_FLAGS_IS_OUTPUT);
    }

    buildMsgHeader(&msg, KEY_GENERATE_KEY_REQ, req_len, mu_info.cmd_tag);

    /* add the CRC */
    addCRC(&msg);

    resp_len = SIZE_MSG(struct gen_key_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to generate key!");
        return error;
    }

    gen_key_resp = (struct gen_key_msg_rsp *)(msg.data.u8);
    if (gen_key_resp->pub_key_size != keyAttribute->pub_key_size) {
        ALOGE("Error happened when exporting the public key!");
        return ELE_GENERAL_ERROR;
    }

    /* check the key id */
    if ((*keyId != 0) && (*keyId != gen_key_resp->key_id)) {
        ALOGE("Invalid key ID! Requested: 0x%x but get: 0x%x", *keyId, gen_key_resp->key_id);
        return ELE_GENERAL_ERROR;
    }
    if ((gen_key_resp->key_id == 0) || (gen_key_resp->key_id > (0x7fffffff))) {
        ALOGE("The key ID is out of range: 0x%x", gen_key_resp->key_id);
        return ELE_GENERAL_ERROR;
    }

    *keyId = gen_key_resp->key_id;
    ALOGI("ELE new key generated with ID: 0x%x", *keyId);

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleDeleteKey(uint32_t keyMgtHandle, uint32_t keyId, uint8_t flags) {
    struct del_key_msg_cmd *del_key_args;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the input parameters */
    if ((keyMgtHandle == 0) || (keyId == 0) || (keyId > (0x7fffffff))) {
        ALOGE("Invalid input parameters!");
        return ELE_INVALID_MESSAGE;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct del_key_msg_cmd);
    del_key_args = (struct del_key_msg_cmd *)(msg.data.u8);
    del_key_args->key_mgt_handle = keyMgtHandle;
    del_key_args->key_id = keyId;
    del_key_args->flags = flags;

    buildMsgHeader(&msg, KEY_DELETE_KEY_REQ, req_len, mu_info.cmd_tag);

    resp_len = SIZE_MSG(struct del_key_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to delete key!");
        return error;
    }

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleGetKeyAttr(uint32_t keyMgtHandle, uint32_t keyId,
                                      key_attribute *keyAttribute) {
    struct get_key_attr_msg_cmd *get_key_attr_args;
    struct get_key_attr_rsp *get_key_attr_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the input parameters */
    if ((keyMgtHandle == 0) || (keyId == 0) || (keyId > (0x7fffffff))) {
        ALOGE("Invalid input parameters!");
        return ELE_INVALID_MESSAGE;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct get_key_attr_msg_cmd);
    get_key_attr_args = (struct get_key_attr_msg_cmd *)(msg.data.u8);
    get_key_attr_args->key_mgt_handle = keyMgtHandle;
    get_key_attr_args->key_id = keyId;

    buildMsgHeader(&msg, KEY_GET_ATTRIBUTE_REQ, req_len, mu_info.cmd_tag);

    resp_len = SIZE_MSG(struct get_key_attr_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to get the key attribute!");
        return error;
    }

    get_key_attr_resp = (struct get_key_attr_rsp *)(msg.data.u8);
    keyAttribute->type = get_key_attr_resp->type;
    keyAttribute->size_bits = get_key_attr_resp->size_bits;
    keyAttribute->lifetime = get_key_attr_resp->lifetime;
    keyAttribute->usage = get_key_attr_resp->usage;
    keyAttribute->permit_algo = get_key_attr_resp->permit_algo;
    keyAttribute->lifecycle = get_key_attr_resp->lifecycle;

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleOpenCipher(uint32_t keyStoreHandler, uint32_t *cipherHandle) {
    struct cipher_open_msg_cmd *cipher_open_args;
    struct cipher_open_msg_rsp *cipher_open_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the keystore handle */
    if (keyStoreHandler == 0) {
        ALOGE("Invalid keystore handle");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct cipher_open_msg_cmd);
    cipher_open_args = (struct cipher_open_msg_cmd *)(msg.data.u8);
    cipher_open_args->key_store_handle = keyStoreHandler;

    buildMsgHeader(&msg, KEY_CIPHER_OPEN_REQ, req_len, mu_info.cmd_tag);

    /* add the CRC */
    addCRC(&msg);

    resp_len = SIZE_MSG(struct cipher_open_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to open cipher session!");
        return error;
    }

    cipher_open_resp = (struct cipher_open_msg_rsp *)(msg.data.u8);
    if (cipher_open_resp->cipher_hdl == 0) {
        ALOGE("Invalid key cipher handle!");
        return ELE_INVALID_MESSAGE;
    }

    *cipherHandle = cipher_open_resp->cipher_hdl;
    ALOGI("ELE key cipher opened, handle: 0x%x", *cipherHandle);

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleCloseCipher(uint32_t cipherHandle) {
    struct cipher_close_msg_cmd *cipher_close_args;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the cipher handle before close */
    if (cipherHandle == 0) {
        ALOGE("Invalid key cipher handler!");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct cipher_close_msg_cmd);
    cipher_close_args = (struct cipher_close_msg_cmd *)(msg.data.u8);
    cipher_close_args->cipher_hdl = cipherHandle;

    buildMsgHeader(&msg, KEY_CIPHER_CLOSE_REQ, req_len, mu_info.cmd_tag);

    resp_len = SIZE_MSG(struct cipher_close_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to close ELE key cipher session!");
        return error;
    }

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleCipherOperation(uint32_t cipherHandle,
                                           cipher_operation_attr *cipherAttr) {
    struct cipher_msg_cmd *cipher_op_args;
    struct cipher_msg_rsp *cipher_op_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the input parameters */
    if (!cipherHandle || !cipherAttr || !(cipherAttr->key_id)) {
        ALOGE("Invalid cipher handler or attributes!");
        return ELE_INVALID_MESSAGE;
    }
    if (((cipherAttr->iv_addr == nullptr) && (cipherAttr->iv_size != 0)) ||
        ((cipherAttr->iv_addr) && (cipherAttr->iv_size == 0))) {
        ALOGE("Invalid cipher iv parameters!");
        return ELE_INVALID_MESSAGE;
    }
    if (!cipherAttr->input_addr || !cipherAttr->input_size || !cipherAttr->output_addr ||
        !cipherAttr->output_size) {
        ALOGE("Invalid cipher input/output parameters!");
        return ELE_INVALID_MESSAGE;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct cipher_msg_cmd);
    cipher_op_args = (struct cipher_msg_cmd *)(msg.data.u8);
    cipher_op_args->cipher_hdl = cipherHandle;
    cipher_op_args->key_id = cipherAttr->key_id;
    if (cipherAttr->iv_size != 0) {
        cipher_op_args->iv_addr = retrivePhyAddress(cipherAttr->iv_addr, cipherAttr->iv_size,
                                                    ELE_MU_IO_FLAGS_IS_INPUT);
        cipher_op_args->iv_size = cipherAttr->iv_size;
    }
    cipher_op_args->flags = cipherAttr->flags;
    cipher_op_args->algo = cipherAttr->algo;
    cipher_op_args->input_addr = retrivePhyAddress(cipherAttr->input_addr, cipherAttr->input_size,
                                                   ELE_MU_IO_FLAGS_IS_INPUT);
    cipher_op_args->input_size = cipherAttr->input_size;
    cipher_op_args->output_addr =
            retrivePhyAddress(cipherAttr->output_addr, cipherAttr->output_size,
                              ELE_MU_IO_FLAGS_IS_OUTPUT);
    cipher_op_args->output_size = cipherAttr->output_size;

    buildMsgHeader(&msg, KEY_CIPHER_OPERATION_REQ, req_len, mu_info.cmd_tag);

    /* add the CRC */
    addCRC(&msg);

    resp_len = SIZE_MSG(struct cipher_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to do cipher operation!");
        return error;
    }

    cipher_op_resp = (struct cipher_msg_rsp *)(msg.data.u8);
    /* return the actual output size */
    cipherAttr->output_size = cipher_op_resp->output_size;

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleCipherAEOperation(uint32_t cipherHandle,
                                             cipher_ae_operation_attr *cipherAEAttr) {
    struct cipher_ae_msg_cmd *cipher_ae_op_args;
    struct cipher_ae_msg_rsp *cipher_ae_op_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the input parameters */
    if (!cipherHandle || !cipherAEAttr || !(cipherAEAttr->key_id)) {
        ALOGE("Invalid cipher ae handler or attributes!");
        return ELE_INVALID_MESSAGE;
    }

    if (cipherAEAttr->flags & CIPHER_ONE_GO_FLAGS_FULL_IV) {
        if (cipherAEAttr->iv_size != 0) {
            ALOGE("The iv size should be 0 when CIPHER_ONE_GO_FLAGS_FULL_IV is set!");
            return ELE_INVALID_MESSAGE;
        }
    } else if (cipherAEAttr->flags & CIPHER_ONE_GO_FLAGS_COUNTER_IV) {
        if (cipherAEAttr->iv_size != 4) {
            ALOGE("The iv size should be 4 when CIPHER_ONE_GO_FLAGS_COUNTER_IV is set!");
            return ELE_INVALID_MESSAGE;
        }
    } else {
        if (cipherAEAttr->iv_size != 12) {
            ALOGE("The iv size should be 12 when supplied by user!");
            return ELE_INVALID_MESSAGE;
        }
    }

    if (!cipherAEAttr->input_addr || !cipherAEAttr->input_size || !cipherAEAttr->output_addr ||
        !cipherAEAttr->output_size || !cipherAEAttr->aad_addr || !cipherAEAttr->aad_size) {
        ALOGE("Invalid cipher input/output parameters!");
        return ELE_INVALID_MESSAGE;
    }

    if ((cipherAEAttr->flags & CIPHER_ONE_GO_FLAGS_ENCRYPT) &&
        (cipherAEAttr->output_size < cipherAEAttr->input_size + AEAD_TAG_LENGTH)) {
        ALOGE("ELE AEAD Output buffer is too small!");
        return ELE_INVALID_MESSAGE;
    }

    if ((cipherAEAttr->flags & CIPHER_ONE_GO_FLAGS_DECRYPT) &&
        (cipherAEAttr->output_size < cipherAEAttr->input_size - AEAD_TAG_LENGTH)) {
        ALOGE("ELE AEAD Output buffer is too small!");
        return ELE_INVALID_MESSAGE;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct cipher_ae_msg_cmd);
    cipher_ae_op_args = (struct cipher_ae_msg_cmd *)(msg.data.u8);
    cipher_ae_op_args->cipher_hdl = cipherHandle;
    cipher_ae_op_args->key_id = cipherAEAttr->key_id;
    if (cipherAEAttr->iv_size != 0) {
        cipher_ae_op_args->iv_addr = retrivePhyAddress(cipherAEAttr->iv_addr, cipherAEAttr->iv_size,
                                                       ELE_MU_IO_FLAGS_IS_INPUT);
        cipher_ae_op_args->iv_size = cipherAEAttr->iv_size;
    }
    cipher_ae_op_args->flags = cipherAEAttr->flags;
    cipher_ae_op_args->algo = cipherAEAttr->algo;
    cipher_ae_op_args->aad_addr = retrivePhyAddress(cipherAEAttr->aad_addr, cipherAEAttr->aad_size,
                                                    ELE_MU_IO_FLAGS_IS_INPUT);
    cipher_ae_op_args->aad_size = cipherAEAttr->aad_size;
    cipher_ae_op_args->input_addr =
            retrivePhyAddress(cipherAEAttr->input_addr, cipherAEAttr->input_size,
                              ELE_MU_IO_FLAGS_IS_INPUT);
    cipher_ae_op_args->input_size = cipherAEAttr->input_size;
    cipher_ae_op_args->output_addr =
            retrivePhyAddress(cipherAEAttr->output_addr, cipherAEAttr->output_size,
                              ELE_MU_IO_FLAGS_IS_OUTPUT);
    cipher_ae_op_args->output_size = cipherAEAttr->output_size;

    buildMsgHeader(&msg, KEY_CIPHER_AE_OPERATION_REQ, req_len, mu_info.cmd_tag);

    /* add the CRC */
    addCRC(&msg);

    resp_len = SIZE_MSG(struct cipher_ae_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to do cipher ae operation!");
        return error;
    }

    cipher_ae_op_resp = (struct cipher_ae_msg_rsp *)(msg.data.u8);
    /* return the actual output size */
    cipherAEAttr->output_size = cipher_ae_op_resp->output_size;

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleSignGenerateOpen(uint32_t keyStoreHandler, uint32_t *signGenHandle) {
    struct sign_gen_open_msg_cmd *sign_gen_open_args;
    struct sign_gen_open_msg_rsp *sign_gen_open_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the keystore handle */
    if (keyStoreHandler == 0) {
        ALOGE("Invalid keystore handle");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct sign_gen_open_msg_cmd);
    sign_gen_open_args = (struct sign_gen_open_msg_cmd *)(msg.data.u8);
    sign_gen_open_args->key_store_handle = keyStoreHandler;

    buildMsgHeader(&msg, KEY_SIGN_GENERATE_OPEN_REQ, req_len, mu_info.cmd_tag);

    /* add the CRC */
    addCRC(&msg);

    resp_len = SIZE_MSG(struct sign_gen_open_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to open signature generate session!");
        return error;
    }

    sign_gen_open_resp = (struct sign_gen_open_msg_rsp *)(msg.data.u8);
    if (sign_gen_open_resp->sign_gen_hdl == 0) {
        ALOGE("Invalid signature generate handle!");
        return ELE_INVALID_MESSAGE;
    }

    *signGenHandle = sign_gen_open_resp->sign_gen_hdl;
    ALOGI("ELE signature generation opened, handle: 0x%x", *signGenHandle);

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleSignGenerateClose(uint32_t signGenHandle) {
    struct sign_gen_close_msg_cmd *sign_gen_close_args;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the handle before close */
    if (signGenHandle == 0) {
        ALOGE("Invalid signature generate handler!");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct sign_gen_close_msg_cmd);
    sign_gen_close_args = (struct sign_gen_close_msg_cmd *)(msg.data.u8);
    sign_gen_close_args->sign_gen_hdl = signGenHandle;

    buildMsgHeader(&msg, KEY_SIGN_GENERATE_CLOSE_REQ, req_len, mu_info.cmd_tag);

    resp_len = SIZE_MSG(struct sign_gen_close_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to close ELE signature generate session!");
        return error;
    }

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleSignGenerate(uint32_t signGenHandle, gen_sign_attr *genSignAttr) {
    struct gen_sign_msg_cmd *gen_sign_args;
    struct gen_sign_msg_rsp *gen_sign_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the input parameters */
    if (!signGenHandle || !genSignAttr || !genSignAttr->key_id) {
        ALOGE("Invalid signature generation handler or attributes!");
        return ELE_INVALID_MESSAGE;
    }
    if (!genSignAttr->msg_lsb_addr || !genSignAttr->msg_size || !genSignAttr->sign_lsb_addr ||
        !genSignAttr->sign_size) {
        ALOGE("Invalid signature generation input/output parameters!");
        return ELE_INVALID_MESSAGE;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct gen_sign_msg_cmd);
    gen_sign_args = (struct gen_sign_msg_cmd *)(msg.data.u8);
    gen_sign_args->sign_gen_hdl = signGenHandle;
    gen_sign_args->key_id = genSignAttr->key_id;
    gen_sign_args->msg_lsb_addr =
            retrivePhyAddress(genSignAttr->msg_lsb_addr, genSignAttr->msg_size,
                              ELE_MU_IO_FLAGS_IS_INPUT);
    gen_sign_args->sign_lsb_addr =
            retrivePhyAddress(genSignAttr->sign_lsb_addr, genSignAttr->sign_size,
                              ELE_MU_IO_FLAGS_IS_OUTPUT);
    gen_sign_args->msg_size = genSignAttr->msg_size;
    gen_sign_args->sign_size = genSignAttr->sign_size;
    gen_sign_args->flags = genSignAttr->flags;
    gen_sign_args->sign_scheme = genSignAttr->sign_scheme;
    gen_sign_args->salt_len = genSignAttr->salt_len;

    buildMsgHeader(&msg, KEY_SIGN_GENERATE_REQ, req_len, mu_info.cmd_tag);

    /* add the CRC */
    addCRC(&msg);

    resp_len = SIZE_MSG(struct gen_sign_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    gen_sign_resp = (struct gen_sign_msg_rsp *)(msg.data.u8);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to generate signature! error: %d", error);
        if (error == ELE_COMMAND_OUTPUT_TOO_SMALL) {
            ALOGE("Signature buffer is too small!");
            genSignAttr->sign_size = gen_sign_resp->signature_size;
        }

        return error;
    }

    /* generated signature size */
    genSignAttr->sign_size = gen_sign_resp->signature_size;

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleSignVerifyOpen(uint32_t *signVerifyHandle) {
    struct sign_verify_open_msg_cmd *sign_verify_open_args;
    struct sign_verify_open_msg_rsp *sign_verify_open_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the session handle before opening sign verify session */
    if (session_handle == 0) {
        ALOGE("ELE session is not yet opened!");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct sign_verify_open_msg_cmd);
    sign_verify_open_args = (struct sign_verify_open_msg_cmd *)(msg.data.u8);
    sign_verify_open_args->session_hdl = session_handle;

    buildMsgHeader(&msg, KEY_SIGN_VERIFY_OPEN_REQ, req_len, mu_info.cmd_tag);

    /* add the CRC */
    addCRC(&msg);

    resp_len = SIZE_MSG(struct sign_verify_open_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to open signature verify session!");
        return error;
    }

    sign_verify_open_resp = (struct sign_verify_open_msg_rsp *)(msg.data.u8);
    if (sign_verify_open_resp->sign_verify_hdl == 0) {
        ALOGE("Invalid signature verify handle!");
        return ELE_INVALID_MESSAGE;
    }

    *signVerifyHandle = sign_verify_open_resp->sign_verify_hdl;
    ALOGI("ELE signature verify session opened, handle: 0x%x", *signVerifyHandle);

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleSignVerifyClose(uint32_t signVerifyHandle) {
    struct sign_verify_close_msg_cmd *sign_verify_close_args;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the handle before close */
    if (signVerifyHandle == 0) {
        ALOGE("Invalid signature verify handler!");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct sign_verify_close_msg_cmd);
    sign_verify_close_args = (struct sign_verify_close_msg_cmd *)(msg.data.u8);
    sign_verify_close_args->sign_verify_hdl = signVerifyHandle;

    buildMsgHeader(&msg, KEY_SIGN_VERIFY_CLOSE_REQ, req_len, mu_info.cmd_tag);

    resp_len = SIZE_MSG(struct sign_verify_close_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to close ELE signature verify session!");
        return error;
    }

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleSignVerify(uint32_t signVerifyHandle, verify_sign_attr *verifySignAttr) {
    struct verify_sign_msg_cmd *verify_sign_msg_args;
    struct verify_sign_msg_rsp *verify_sign_msg_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the input parameters */
    if (!signVerifyHandle || !verifySignAttr) {
        ALOGE("Invalid signature verify handler or attributes!");
        return ELE_INVALID_MESSAGE;
    }
    if (!verifySignAttr->key_lsb_addr || !verifySignAttr->key_size ||
        !verifySignAttr->msg_lsb_addr || !verifySignAttr->msg_size ||
        !verifySignAttr->sign_lsb_addr || !verifySignAttr->sign_size) {
        ALOGE("Invalid signature verify input/output parameters!");
        return ELE_INVALID_MESSAGE;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct verify_sign_msg_cmd);
    verify_sign_msg_args = (struct verify_sign_msg_cmd *)(msg.data.u8);
    verify_sign_msg_args->sign_verify_hdl = signVerifyHandle;
    verify_sign_msg_args->key_lsb_addr =
            retrivePhyAddress(verifySignAttr->key_lsb_addr, verifySignAttr->key_size,
                              ELE_MU_IO_FLAGS_IS_INPUT);
    verify_sign_msg_args->key_size = verifySignAttr->key_size;
    verify_sign_msg_args->msg_lsb_addr =
            retrivePhyAddress(verifySignAttr->msg_lsb_addr, verifySignAttr->msg_size,
                              ELE_MU_IO_FLAGS_IS_INPUT);
    verify_sign_msg_args->msg_size = verifySignAttr->msg_size;
    verify_sign_msg_args->sign_lsb_addr =
            retrivePhyAddress(verifySignAttr->sign_lsb_addr, verifySignAttr->sign_size,
                              ELE_MU_IO_FLAGS_IS_INPUT);
    verify_sign_msg_args->sign_size = verifySignAttr->sign_size;
    verify_sign_msg_args->key_security_size = verifySignAttr->key_security_size;
    verify_sign_msg_args->key_type = verifySignAttr->key_type;
    verify_sign_msg_args->flags = verifySignAttr->flags;
    verify_sign_msg_args->sign_scheme = verifySignAttr->sign_scheme;
    verify_sign_msg_args->salt_len = verifySignAttr->salt_len;

    buildMsgHeader(&msg, KEY_SIGN_VERIFY_REQ, req_len, mu_info.cmd_tag);

    /* add the CRC */
    addCRC(&msg);

    resp_len = SIZE_MSG(struct verify_sign_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to verify signature!");
        return error;
    }

    verify_sign_msg_resp = (struct verify_sign_msg_rsp *)(msg.data.u8);
    if (verify_sign_msg_resp->verify_status != ELE_SIGNATURE_VERIFY_SUCCESS) {
        ALOGE("Invalid signature verification status!");
        return ELE_VERIFICATION_FAILURE;
    }

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleMacOpen(uint32_t keyStoreHandler, uint32_t *macHandle) {
    struct mac_open_msg_cmd *open_mac_args;
    struct mac_open_msg_rsp *open_mac_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the key store handle before opening mac session */
    if (keyStoreHandler == 0) {
        ALOGE("Invalid keystore handle");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct mac_open_msg_cmd);
    open_mac_args = (struct mac_open_msg_cmd *)(msg.data.u8);
    open_mac_args->key_store_handle = keyStoreHandler;

    buildMsgHeader(&msg, KEY_MAC_OPEN_REQ, req_len, mu_info.cmd_tag);

    /* add the CRC */
    addCRC(&msg);

    resp_len = SIZE_MSG(struct mac_open_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to open mac session!");
        return error;
    }

    open_mac_resp = (struct mac_open_msg_rsp *)(msg.data.u8);
    if (open_mac_resp->mac_hdl == 0) {
        ALOGE("Invalid mac session handle!");
        return ELE_INVALID_MESSAGE;
    }
    *macHandle = open_mac_resp->mac_hdl;
    ALOGI("ELE mac session opened, handle: 0x%x", *macHandle);

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleMacClose(uint32_t macHandle) {
    struct mac_close_msg_cmd *close_mac_args;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the mac handle before closing */
    if (macHandle == 0) {
        ALOGE("Invalid mac session handler!");
        return ELE_GENERAL_ERROR;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct mac_close_msg_cmd);
    close_mac_args = (struct mac_close_msg_cmd *)(msg.data.u8);
    close_mac_args->mac_hdl = macHandle;

    buildMsgHeader(&msg, KEY_MAC_CLOSE_REQ, req_len, mu_info.cmd_tag);

    resp_len = SIZE_MSG(struct mac_close_msg_cmd);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to close ELE mac session!");
        return error;
    }

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleMacOperation(uint32_t macHandle, mac_operation_attr *macOperationAttr) {
    struct mac_operation_msg_cmd *mac_operation_args;
    struct mac_operation_msg_rsp *mac_operation_resp;
    struct mu_msg msg;
    ErrorType error;
    uint32_t req_len, resp_len;

    /* check the input parameters */
    if (!macHandle || !macOperationAttr) {
        ALOGE("Invalid mac operation handler or attributes!");
        return ELE_INVALID_MESSAGE;
    }
    if (!macOperationAttr->payload_addr || !macOperationAttr->payload_size ||
        !macOperationAttr->mac_addr || !macOperationAttr->mac_size) {
        ALOGE("Invalid mac operation input/output parameters!");
        return ELE_INVALID_MESSAGE;
    }

    /* construct the message command */
    memset(&msg, 0, sizeof(msg));
    req_len = SIZE_MSG(struct mac_operation_msg_cmd);
    mac_operation_args = (struct mac_operation_msg_cmd *)(msg.data.u8);
    mac_operation_args->mac_hdl = macHandle;
    mac_operation_args->key_id = macOperationAttr->key_id;
    mac_operation_args->payload_addr =
            retrivePhyAddress(macOperationAttr->payload_addr, macOperationAttr->payload_size,
                              ELE_MU_IO_FLAGS_IS_INPUT);
    mac_operation_args->mac_addr =
            retrivePhyAddress(macOperationAttr->mac_addr, macOperationAttr->mac_size,
                              ELE_MU_IO_FLAGS_IS_IN_OUT);
    mac_operation_args->payload_size = macOperationAttr->payload_size;
    mac_operation_args->mac_size = macOperationAttr->mac_size;
    mac_operation_args->flags = macOperationAttr->flags;
    mac_operation_args->algo = macOperationAttr->algo;

    buildMsgHeader(&msg, KEY_MAC_OPERATION_REQ, req_len, mu_info.cmd_tag);
    addCRC(&msg);

    resp_len = SIZE_MSG(struct mac_operation_msg_rsp);
    error = eleSendAndReciveMsg(&msg, req_len, &resp_len);
    if (error != ELE_NO_ERROR) {
        ALOGE("Failed to do mac operation!");
        return error;
    }

    mac_operation_resp = (struct mac_operation_msg_rsp *)(msg.data.u8);
    if (macOperationAttr->flags & MAC_ONE_GO_GENERATION) {
        /* return the actual mac size for mac generation */
        macOperationAttr->mac_size = mac_operation_resp->out_mac_size;
    } else {
        /* check the verification status for mac verification */
        if (mac_operation_resp->verify_status != ELE_MAC_VERIFY_SUCCESS) {
            ALOGE("Invalid mac verification status!");
            return ELE_VERIFICATION_FAILURE;
        }
    }

    return ELE_NO_ERROR;
}

ErrorType EleOperation::eleNvmMasterImport(struct nvm_context *nvmCtx) {
    struct storage_master_import_msg_cmd *master_import_args;
    struct nvm_header blob_hdr;
    struct mu_msg msg;
    ErrorType error = ELE_GENERAL_ERROR;
    uint32_t req_len, resp_len;
    uint32_t data_phy = 0;
    uint8_t *data = nullptr;
    uint32_t data_len, crc;

    do {
        if (readStorageFile((char *)nvmCtx->nvm_master_name, (uint8_t *)&blob_hdr,
                            NVM_HEADER_SIZE) != NVM_HEADER_SIZE) {
            ALOGE("Can not find storage master file or load failed!");
            error = ELE_NO_ERROR;
            break;
        }

        /* file already exist, load and import the data */
        ALOGI("ELE storage master already exist, len: %d", blob_hdr.size);
        data_len = blob_hdr.size + NVM_HEADER_SIZE;
        data = (uint8_t *)malloc(data_len);
        if (!data) {
            ALOGE("Failed to allocate memory!");
            error = ELE_MEMORY_FAILURE;
            break;
        }
        if (readStorageFile((char *)nvmCtx->nvm_master_name, data, data_len) != data_len) {
            ALOGE("Failed to load storage master file!");
            break;
        }
        /* check the crc */
        crc = calCRC((uint32_t *)(data + NVM_HEADER_SIZE), blob_hdr.size >> 2);
        if (crc != blob_hdr.crc) {
            ALOGE("Wrong crc (expected: 0x%08x but get 0x%08x),"
                  "master data could be corrupted!",
                  blob_hdr.crc, crc);
            break;
        }
        ALOGI("ELE storage master loaded, start importing to ELE...");

        /* Check the nvm storage handler */
        if (nvmCtx->nvm_handle == 0) {
            ALOGE("Invalid nvm sorage handler!");
            break;
        }

        /* construct the message command */
        memset(&msg, 0, sizeof(msg));
        req_len = SIZE_MSG(struct storage_master_import_msg_cmd);
        master_import_args = (struct storage_master_import_msg_cmd *)(msg.data.u8);
        /* Retrive the physical address */
        data_phy =
                retrivePhyAddress(data + NVM_HEADER_SIZE, blob_hdr.size, ELE_MU_IO_FLAGS_IS_INPUT);
        if (!data_phy) {
            ALOGE("Failed to retrive physical address!");
            break;
        }

        master_import_args->nvm_storage_handle = nvmCtx->nvm_handle;
        master_import_args->master_data_lsb_addr = data_phy;
        master_import_args->master_data_size = blob_hdr.size;
        buildMsgHeader(&msg, STORAGE_MASTER_IMPORT_REQ, req_len, mu_info.cmd_tag);

        resp_len = SIZE_MSG(struct storage_master_import_msg_rsp);
        if (eleSendAndReciveMsg(&msg, req_len, &resp_len) != ELE_NO_ERROR) {
            ALOGE("Failed to import nvm master storage!");
            break;
        }

        error = ELE_NO_ERROR;
    } while (false);

    if (data)
        free(data);

    return error;
}

ErrorType EleOperation::eleHandleMasterExportReq(struct mu_msg *cmd, uint32_t cmdLen,
                                                 struct mu_msg *resp, uint32_t *respLen,
                                                 uint32_t rspMsgInfo, struct nvm_context *nvmCtx) {
    storage_master_exp_msg_cmd *req;
    storage_master_exp_msg_rsp *rsp;
    ErrorType error = ELE_NO_ERROR;
    struct nvm_header *blob_hdr;
    uint8_t *data_buf;
    uint32_t data_len;

    req = (storage_master_exp_msg_cmd *)(cmd->data.u8);
    rsp = (storage_master_exp_msg_rsp *)(resp->data.u8);
    nvmCtx->prev_command = cmd->header.cmd;
    nvmCtx->next_command = STORAGE_NVM_LAST_CMD;
    rsp->rsp_code = ELE_COMMAND_GENERAL_ERROR;

    do {
        if (rspMsgInfo != ELE_COMMAND_SUCCEED) {
            rsp->rsp_code = rspMsgInfo;
            error = ELE_INVALID_MESSAGE;
            break;
        }

        if (cmdLen != SIZE_MSG(storage_master_exp_msg_cmd)) {
            ALOGE("The cmd length doesn't match expected!");
            error = ELE_INVALID_MESSAGE;
            break;
        }

        if (req->nvm_storage_handle != nvmCtx->nvm_handle) {
            ALOGE("The nvm handle doesn't match expected!");
            error = ELE_INVALID_MESSAGE;
            break;
        }

        /* Get the master data size */
        data_len = req->master_data_size + NVM_HEADER_SIZE;
        if (req->master_data_size == 0 || data_len > STORAGE_MAX_DATA_SIZE) {
            ALOGE("The master data size is unexpected: 0x%x!", data_len);
            error = ELE_INVALID_MESSAGE;
            break;
        }

        /* Allocate the buffer to store the exported data */
        data_buf = (uint8_t *)malloc(data_len);
        if (data_buf != nullptr) {
            blob_hdr = (struct nvm_header *)data_buf;
            blob_hdr->size = req->master_data_size;
            /* reset the blob id which is not used for master */
            memset(&(blob_hdr->blob_id), 0, sizeof(struct nvm_blob_id));
            /* send the physical address to ELE */
            rsp->master_data_addr =
                    retrivePhyAddress(data_buf + NVM_HEADER_SIZE, req->master_data_size,
                                      ELE_MU_IO_FLAGS_IS_OUTPUT);
        } else {
            /* Return null address for memory failure. */
            ALOGE("Failed to allocate memory!");
            rsp->master_data_addr = 0;
            break;
        }

        rsp->rsp_code = ELE_COMMAND_SUCCEED;

        nvmCtx->next_command = STORAGE_EXPORT_FINISH_REQ;
        nvmCtx->last_data = data_buf;
    } while (false);

    rsp->nvm_storage_handle = nvmCtx->nvm_handle;
    *respLen = SIZE_MSG(storage_master_exp_msg_rsp);

    return error;
}

ErrorType EleOperation::eleHandleExportFinish(struct mu_msg *cmd, uint32_t cmdLen,
                                              struct mu_msg *resp, uint32_t *respLen,
                                              uint32_t rspMsgInfo, struct nvm_context *nvmCtx) {
    storage_exp_finish_msg_cmd *req;
    storage_exp_finish_msg_rsp *rsp;
    ErrorType error = ELE_NO_ERROR;
    struct nvm_header *blob_hdr;
    struct nvm_blob_id *blob_id;
    uint32_t data_len;
    char *file_name = nullptr;

    req = (storage_exp_finish_msg_cmd *)(cmd->data.u8);
    rsp = (storage_exp_finish_msg_rsp *)(resp->data.u8);
    nvmCtx->next_command = STORAGE_NVM_LAST_CMD;
    rsp->rsp_code = ELE_COMMAND_GENERAL_ERROR;

    do {
        if (rspMsgInfo != ELE_COMMAND_SUCCEED) {
            rsp->rsp_code = rspMsgInfo;
            error = ELE_INVALID_MESSAGE;
            break;
        }
        if (cmdLen != SIZE_MSG(storage_exp_finish_msg_cmd)) {
            ALOGE("The cmd length doesn't match expected!");
            error = ELE_INVALID_MESSAGE;
            break;
        }

        if (req->nvm_storage_handle != nvmCtx->nvm_handle) {
            ALOGE("The nvm handle doesn't match expected!");
            error = ELE_INVALID_MESSAGE;
            break;
        }

        if (req->export_status != NVM_EXPORT_STATUS_SUCCESS) {
            ALOGE("NVM export failed!");
            error = ELE_INVALID_MESSAGE;
            /* Acknowledge the failure, don't return failure status */
            rsp->rsp_code = ELE_COMMAND_SUCCEED;
            break;
        }

        blob_hdr = (struct nvm_header *)(nvmCtx->last_data);
        data_len = blob_hdr->size + NVM_HEADER_SIZE;
        blob_hdr->crc =
                calCRC((uint32_t *)(nvmCtx->last_data + NVM_HEADER_SIZE), blob_hdr->size >> 2);

        file_name = (char *)malloc(NVM_MAX_FILE_NAME_LEN);
        if (!file_name) {
            ALOGE("Failed to allocate memory!");
            error = ELE_MEMORY_FAILURE;
            break;
        }
        memset(file_name, '\0', NVM_MAX_FILE_NAME_LEN);
        /* Check the operation is storage master export or chunk export */
        if (nvmCtx->prev_command == STORAGE_MASTER_EXPORT_REQ) {
            /* storage master case */
            if (nvmCtx->nvm_master_name != nullptr)
                strncpy(file_name, nvmCtx->nvm_master_name, NVM_MAX_FILE_NAME_LEN);
            // TODO handle secure storage case
        } else if (nvmCtx->prev_command == STORAGE_CHUNK_EXPORT_REQ) {
            /* storage chunk case */
            /* The chunk file name is constructed by:
             * PATH + blob_id->ext + blob_id->id + blob_id->metadata.
             */
            blob_id = &(blob_hdr->blob_id);
            if (snprintf(file_name, NVM_MAX_FILE_NAME_LEN, "%s%0*x%0*x%0*x", nvmCtx->nvm_chunk_path,
                         (int)(sizeof(blob_id->ext) * 2), blob_id->ext,
                         (int)(sizeof(blob_id->id) * 2), blob_id->id,
                         (int)(sizeof(blob_id->metadata) * 2), blob_id->metadata) == -1) {
                ALOGE("Failed to construct the file name!");
                error = ELE_GENERAL_ERROR;
                break;
            }
            ALOGI("NVM data file path:%s.", file_name);
        } else {
            /* Wrong command */
            ALOGE("Error, only STORAGE_MASTER_EXPORT_REQ or STORAGE_CHUNK_EXPORT_REQ is expected!");
            error = ELE_INVALID_MESSAGE;
            break;
        }
        /* Write to NVM */
        if (writeStorageFile(file_name, nvmCtx->last_data, data_len) == data_len) {
            ALOGI("NVM data stored to path:%s!", file_name);
            rsp->rsp_code = ELE_COMMAND_SUCCEED;
        } else {
            ALOGE("Failed to write storage master file to NVM!");
            error = ELE_GENERAL_ERROR;
            break;
        }
    } while (false);

    rsp->nvm_storage_handle = nvmCtx->nvm_handle;
    *respLen = SIZE_MSG(storage_exp_finish_msg_rsp);
    if (file_name)
        free(file_name);

    return error;
}

ErrorType EleOperation::eleHandleChunkExportReq(struct mu_msg *cmd, uint32_t cmdLen,
                                                struct mu_msg *resp, uint32_t *respLen,
                                                uint32_t rspMsgInfo, struct nvm_context *nvmCtx) {
    storage_chunk_exp_msg_cmd *req;
    storage_chunk_exp_msg_rsp *rsp;
    ErrorType error = ELE_NO_ERROR;
    struct nvm_header *blob_hdr;
    uint32_t data_len;
    uint8_t *data_buf;

    req = (storage_chunk_exp_msg_cmd *)(cmd->data.u8);
    rsp = (storage_chunk_exp_msg_rsp *)(resp->data.u8);
    nvmCtx->prev_command = cmd->header.cmd;
    nvmCtx->next_command = STORAGE_NVM_LAST_CMD;
    rsp->rsp_code = ELE_COMMAND_GENERAL_ERROR;

    do {
        if (rspMsgInfo != ELE_COMMAND_SUCCEED) {
            rsp->rsp_code = rspMsgInfo;
            error = ELE_INVALID_MESSAGE;
            break;
        }

        if (cmdLen != SIZE_MSG(storage_chunk_exp_msg_cmd)) {
            ALOGE("The cmd length doesn't match expected!");
            error = ELE_INVALID_MESSAGE;
            break;
        }

        if (req->nvm_storage_handle != nvmCtx->nvm_handle) {
            ALOGE("The nvm handle doesn't match expected!");
            error = ELE_INVALID_MESSAGE;
            break;
        }

        /* Get the chunk data size */
        data_len = req->chunk_size + NVM_HEADER_SIZE;
        if (req->chunk_size == 0 || data_len > STORAGE_MAX_DATA_SIZE) {
            ALOGE("The chunk data size is unexpected: 0x%x!", data_len);
            error = ELE_INVALID_MESSAGE;
            break;
        }

        /* Allocate the buffer to store the exported data */
        data_buf = (uint8_t *)malloc(data_len);
        if (data_buf != nullptr) {
            blob_hdr = (struct nvm_header *)data_buf;
            blob_hdr->size = req->chunk_size;
            memcpy(&(blob_hdr->blob_id), &(req->blob_id), sizeof(struct nvm_blob_id));
            /* send the physical address to ELE */
            rsp->chunk_blob_addr = retrivePhyAddress(data_buf + NVM_HEADER_SIZE, req->chunk_size,
                                                     ELE_MU_IO_FLAGS_IS_OUTPUT);
        } else {
            /* Return null address for memory failure. */
            ALOGE("Failed to allocate memory!");
            rsp->chunk_blob_addr = 0;
            break;
        }

        rsp->rsp_code = ELE_COMMAND_SUCCEED;

        nvmCtx->next_command = STORAGE_EXPORT_FINISH_REQ;
        nvmCtx->last_data = data_buf;
    } while (false);

    *respLen = SIZE_MSG(storage_chunk_exp_msg_rsp);

    return error;
}

ErrorType EleOperation::eleHandleChunkGetReq(struct mu_msg *cmd, uint32_t cmdLen,
                                             struct mu_msg *resp, uint32_t *respLen,
                                             uint32_t rspMsgInfo, struct nvm_context *nvmCtx) {
    storage_get_chunk_msg_cmd *req;
    storage_get_chunk_msg_rsp *rsp;
    ErrorType error = ELE_NO_ERROR;
    struct nvm_header blob_hdr;
    struct nvm_blob_id *blob_id;
    char *file_name = nullptr;
    uint32_t data_len, crc;
    uint8_t *data_buf = nullptr;

    req = (storage_get_chunk_msg_cmd *)(cmd->data.u8);
    rsp = (storage_get_chunk_msg_rsp *)(resp->data.u8);
    nvmCtx->prev_command = cmd->header.cmd;
    nvmCtx->next_command = STORAGE_NVM_LAST_CMD;
    rsp->rsp_code = ELE_COMMAND_GENERAL_ERROR;

    do {
        if (rspMsgInfo != ELE_COMMAND_SUCCEED) {
            rsp->rsp_code = rspMsgInfo;
            error = ELE_INVALID_MESSAGE;
            break;
        }

        if (cmdLen != SIZE_MSG(storage_get_chunk_msg_cmd)) {
            ALOGE("The cmd length doesn't match expected!");
            error = ELE_INVALID_MESSAGE;
            break;
        }

        if (req->nvm_storage_handle != nvmCtx->nvm_handle) {
            ALOGE("The nvm handle doesn't match expected!");
            error = ELE_INVALID_MESSAGE;
            break;
        }
        /* Construct the chunk file name */
        file_name = (char *)malloc(NVM_MAX_FILE_NAME_LEN);
        if (!file_name) {
            ALOGE("Failed to allocate memory!");
            error = ELE_MEMORY_FAILURE;
            break;
        }
        memset(file_name, '\0', NVM_MAX_FILE_NAME_LEN);
        blob_id = &(req->blob_id);
        if (snprintf(file_name, NVM_MAX_FILE_NAME_LEN, "%s%0*x%0*x%0*x", nvmCtx->nvm_chunk_path,
                     (int)(sizeof(blob_id->ext) * 2), blob_id->ext, (int)(sizeof(blob_id->id) * 2),
                     blob_id->id, (int)(sizeof(blob_id->metadata) * 2), blob_id->metadata) == -1) {
            ALOGE("Failed to construct the file name!");
            error = ELE_GENERAL_ERROR;
            break;
        }
        ALOGI("NVM data file path:%s.", file_name);

        /* Read the header first */
        if (readStorageFile(file_name, (uint8_t *)&blob_hdr, NVM_HEADER_SIZE) != NVM_HEADER_SIZE) {
            ALOGE("Failed to load chunk file header: %s!", file_name);
            error = ELE_GENERAL_ERROR;
            break;
        }
        /* Parse the file size */
        data_len = blob_hdr.size + NVM_HEADER_SIZE;
        data_buf = (uint8_t *)malloc(data_len);
        if (!data_buf) {
            ALOGE("Failed to allocate memory!");
            error = ELE_MEMORY_FAILURE;
            break;
        }
        /* Load the whole file */
        if (readStorageFile(file_name, data_buf, data_len) != data_len) {
            ALOGE("Failed to load full chunk file: %s!", file_name);
            error = ELE_GENERAL_ERROR;
            break;
        }
        /* check the crc */
        crc = calCRC((uint32_t *)(data_buf + NVM_HEADER_SIZE), blob_hdr.size >> 2);
        if (crc != blob_hdr.crc) {
            ALOGE("Wrong crc (expected: 0x%08x but get 0x%08x),"
                  "chunk data could be corrupted!",
                  blob_hdr.crc, crc);
            error = ELE_GENERAL_ERROR;
            break;
        }

        rsp->rsp_code = ELE_COMMAND_SUCCEED;
        rsp->chunk_size = blob_hdr.size;
        rsp->chunk_addr = retrivePhyAddress(data_buf + NVM_HEADER_SIZE, blob_hdr.size,
                                            ELE_MU_IO_FLAGS_IS_INPUT);

        nvmCtx->next_command = STORAGE_CHUNK_GET_DONE_REQ;
        nvmCtx->last_data = data_buf;
    } while (false);

    *respLen = SIZE_MSG(storage_get_chunk_msg_rsp);
    if (file_name)
        free(file_name);

    return error;
}

ErrorType EleOperation::eleHandleChunkGetDone(struct mu_msg *cmd, uint32_t cmdLen,
                                              struct mu_msg *resp, uint32_t *respLen,
                                              uint32_t rspMsgInfo, struct nvm_context *nvmCtx) {
    storage_get_chunk_done_msg_cmd *req;
    storage_get_chunk_done_msg_rsp *rsp;
    ErrorType error = ELE_NO_ERROR;

    req = (storage_get_chunk_done_msg_cmd *)(cmd->data.u8);
    rsp = (storage_get_chunk_done_msg_rsp *)(resp->data.u8);
    nvmCtx->next_command = STORAGE_NVM_LAST_CMD;
    rsp->rsp_code = ELE_COMMAND_GENERAL_ERROR;

    do {
        if (rspMsgInfo != ELE_COMMAND_SUCCEED) {
            rsp->rsp_code = rspMsgInfo;
            error = ELE_INVALID_MESSAGE;
            break;
        }
        if (cmdLen != SIZE_MSG(storage_get_chunk_done_msg_cmd)) {
            ALOGE("The cmd length doesn't match expected!");
            error = ELE_INVALID_MESSAGE;
            break;
        }

        if (req->nvm_storage_handle != nvmCtx->nvm_handle) {
            ALOGE("The nvm handle doesn't match expected!");
            error = ELE_INVALID_MESSAGE;
            break;
        }

        if (req->status != NVM_CHUNK_GET_CHUNK_SUCCESS) {
            ALOGE("Chunk get failed!");
            error = ELE_INVALID_MESSAGE;
            break;
        }

        rsp->rsp_code = ELE_COMMAND_SUCCEED;
    } while (false);

    *respLen = SIZE_MSG(storage_get_chunk_done_msg_rsp);

    return error;
}

ErrorType EleOperation::eleHandleNVMRequest(struct nvm_context *nvmCtx) {
    struct mu_msg cmd, resp;
    uint32_t cmd_len = sizeof(cmd);
    uint32_t resp_len = 0;
    uint32_t cmd_id = STORAGE_NVM_LAST_CMD;
    uint32_t rsp_msg_info;
    uint32_t *cmd_buf = (uint32_t *)(&cmd);
    ErrorType error;

    memset(&cmd, 0, sizeof(struct mu_msg));
    memset(&resp, 0, sizeof(struct mu_msg));

    do {
        /* Recive msg from ELE */
        error = receiveNVMRequest(&cmd, &cmd_len, &cmd_id);
        if (error != ELE_NO_ERROR) {
            // TODO handle total retry for ELE_COMMUNICATION_ERROR
            error = ELE_ERROR_RETRY;
            break;
        }

        /* Check the command, return for invalid command */
        if (cmd.header.cmd < STORAGE_OPEN_REQ || cmd.header.cmd > STORAGE_CHUNK_GET_DONE_REQ) {
            ALOGE("The nvm request command received from ELE is out of range!");
            error = ELE_NO_ERROR;
            break;
        }

        /* Check the command and the CRC */
        if (nvmCtx->next_command != STORAGE_NVM_LAST_CMD &&
            cmd.header.cmd != nvmCtx->next_command) {
            /* Command is unexpected, send error response */
            ALOGE("command is unexpected (expected: 0x%0x but get: 0x%x)!", nvmCtx->next_command,
                  cmd.header.cmd);
            nvmCtx->next_command = STORAGE_NVM_LAST_CMD;
            rsp_msg_info = ELE_COMMAND_SERVICE_DISABLED;
        } else if ((cmd_len >> 2) > STORAGE_NB_WORDS_MAX_NO_CRC &&
                   cmd_buf[(cmd_len >> 2) - 1] != calCRC(cmd_buf, (cmd_len >> 2) - 1)) {
            ALOGE("CRC check failed, message may be corrupted!");
            rsp_msg_info = ELE_COMMAND_WRONG_CRC;
        } else {
            rsp_msg_info = ELE_COMMAND_SUCCEED;
        }

        /* Prepare the message response */
        switch (cmd_id) {
            case STORAGE_MASTER_EXPORT_REQ:
                error = eleHandleMasterExportReq(&cmd, cmd_len, &resp, &resp_len, rsp_msg_info,
                                                 nvmCtx);
                break;
            case STORAGE_EXPORT_FINISH_REQ:
                error = eleHandleExportFinish(&cmd, cmd_len, &resp, &resp_len, rsp_msg_info,
                                              nvmCtx);
                break;
            case STORAGE_CHUNK_EXPORT_REQ:
                error = eleHandleChunkExportReq(&cmd, cmd_len, &resp, &resp_len, rsp_msg_info,
                                                nvmCtx);
                break;
            case STORAGE_CHUNK_GET_REQ:
                error = eleHandleChunkGetReq(&cmd, cmd_len, &resp, &resp_len, rsp_msg_info, nvmCtx);
                break;
            case STORAGE_CHUNK_GET_DONE_REQ:
                error = eleHandleChunkGetDone(&cmd, cmd_len, &resp, &resp_len, rsp_msg_info,
                                              nvmCtx);
                break;
            default:
                ALOGE(" Unsupported command (%04x)!", cmd_id);
                error = ELE_INVALID_ARGS;
        };
        if (error != ELE_NO_ERROR) {
            /*
             * Something is wrong with the cmd or response, we don't break
             * because we need to send the failure response back to ELE.
             */
            ALOGE("Warning: command (%04x) failed!", cmd_id);
        }

        /* Check the response length */
        if (resp_len > sizeof(struct mu_msg)) {
            ALOGE("Response messgae is too long!");
            error = ELE_GENERAL_ERROR;
            break;
        }
        /* Build the response header */
        buildMsgHeader(&resp, cmd_id, resp_len, mu_info.rsp_tag);
        /* Add CRC */
        if ((resp_len >> 2) > STORAGE_NB_WORDS_MAX_NO_CRC)
            addCRC(&resp);
        /* Send the response */
        error = sendMuMsg((void *)&resp, resp_len);
    } while (false);

    /* Free memory for STORAGE_NVM_LAST_CMD */
    if (nvmCtx->next_command == STORAGE_NVM_LAST_CMD) {
        if (nvmCtx->last_data) {
            free(nvmCtx->last_data);
            nvmCtx->last_data = nullptr;
        }
    }

    return error;
}
