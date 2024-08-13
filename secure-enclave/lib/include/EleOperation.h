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
#ifndef __ELE_OPERATION_H__
#define __ELE_OPERATION_H__

#include <EleCommand.h>
#include <EleMessage.h>
#include <log/log.h>
#include <string.h>

#include <cinttypes>
#include <cstdint>
#include <mutex>

class EleOperation {
public:
    EleOperation(enum MuType type) : mu_type(type){};
    ~EleOperation() { eleCloseDeviceNode(); };

    ErrorType eleOpenDeviceNode(void);
    ErrorType eleOpenSession();
    ErrorType eleCloseSession();
    ErrorType eleOpenKeyStore(uint32_t keyStoreId, uint32_t nonce, uint8_t op,
                              uint32_t *keyStoreHandler);
    ErrorType eleCloseKeyStore(uint32_t keyStoreHandler);
    ErrorType eleOpenKeyManagement(uint32_t keyStoreHandler, uint32_t *keyMgtHandle);
    ErrorType eleCloseKeyManagement(uint32_t keyMgtHandle);
    ErrorType eleGenerateKey(uint32_t keyMgtHandle, uint32_t *keyId,
                             gen_key_attribute *keyAttribute);
    ErrorType eleDeleteKey(uint32_t keyMgtHandle, uint32_t keyId, uint8_t flags);
    ErrorType eleGetKeyAttr(uint32_t keyMgtHandle, uint32_t keyId, key_attribute *keyAttribute);
    ErrorType eleOpenCipher(uint32_t keyStoreHandler, uint32_t *cipherHandle);
    ErrorType eleCloseCipher(uint32_t cipherHandle);
    ErrorType eleCipherOperation(uint32_t cipherHandle, cipher_operation_attr *cipherAttr);
    ErrorType eleCipherAEOperation(uint32_t cipherHandle, cipher_ae_operation_attr *cipherAEAttr);
    ErrorType eleSignGenerateOpen(uint32_t keyStoreHandler, uint32_t *signGenHandle);
    ErrorType eleSignGenerateClose(uint32_t signGenHandle);
    ErrorType eleSignGenerate(uint32_t signGenHandle, gen_sign_attr *genSignAttr);
    ErrorType eleSignVerifyOpen(uint32_t *signVerifyHandle);
    ErrorType eleSignVerifyClose(uint32_t signVerifyHandle);
    ErrorType eleSignVerify(uint32_t signVerifyHandle, verify_sign_attr *verifySignAttr);

    /* NVM operations*/
    ErrorType eleOpenStorage(uint32_t *nvmStorageHandle);
    ErrorType eleCloseStorage(uint32_t nvmStorageHandle);
    ErrorType eleNvmMasterImport(struct nvm_context *nvmCtx);
    ErrorType eleHandleNVMRequest(struct nvm_context *nvmCtx);

private:
    ErrorType sendMuMsg(void *msg, uint32_t reqLen);
    uint32_t receiveMuMsg(void *msg, uint32_t respLen);
    uint32_t retrivePhyAddress(uint8_t *src, uint32_t size, uint32_t flag);
    ErrorType eleSendAndReciveMsg(struct mu_msg *msg, uint32_t len);
    ErrorType eleSendAndReciveMsg(struct mu_msg *msg, uint32_t reqLen, uint32_t *respLen);
    ErrorType eleCloseDeviceNode(void);

    /* NVM operations */
    ErrorType eleHandleChunkGetDone(struct mu_msg *cmd, uint32_t cmdLen, struct mu_msg *resp,
                                    uint32_t *respLen, uint32_t rspMsgInfo,
                                    struct nvm_context *nvmCtx);
    ErrorType eleHandleChunkGetReq(struct mu_msg *cmd, uint32_t cmdLen, struct mu_msg *resp,
                                   uint32_t *respLen, uint32_t rspMsgInfo,
                                   struct nvm_context *nvmCtx);
    ErrorType eleHandleChunkExportReq(struct mu_msg *cmd, uint32_t cmdLen, struct mu_msg *resp,
                                      uint32_t *respLen, uint32_t rspMsgInfo,
                                      struct nvm_context *nvmCtx);
    ErrorType eleHandleExportFinish(struct mu_msg *cmd, uint32_t cmdLen, struct mu_msg *resp,
                                    uint32_t *respLen, uint32_t rspMsgInfo,
                                    struct nvm_context *nvmCtx);
    ErrorType eleHandleMasterExportReq(struct mu_msg *cmd, uint32_t cmdLen, struct mu_msg *resp,
                                       uint32_t *respLen, uint32_t rspMsgInfo,
                                       struct nvm_context *nvmCtx);
    ErrorType receiveNVMRequest(struct mu_msg *cmd, uint32_t *cmdLen, uint32_t *cmdID);

    int fd = -1;
    uint32_t session_handle = 0;
    enum MuType mu_type = MU_CHANNEL_INVALID;
    ele_mu_info mu_info;
};

#endif //__ELE_OPERATION_H__
