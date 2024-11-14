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

#define LOG_TAG "secure-enclave"

#include "SecureEnclave.h"

#include <binder/IPCThreadState.h>

namespace aidl {
namespace nxp {
namespace hardware {
namespace ele {

SecureEnclave::SecureEnclave() {
    ErrorType error;

    eleOps = std::make_unique<EleOperation>(MU_CHANNEL_PLAT_HSM);
    if (!eleOps) {
        ALOGE("Failed to allocate ele operation handle!");
        return;
    }

    do {
        if (eleOps->eleOpenDeviceNode() != ELE_NO_ERROR) {
            ALOGE("Failed to open ele device nodes!");
            break;
        }

        eleDeviceReady = true;
        ALOGI("ELE device ready.");
    } while (false);

    /* Clean up if some error happens
     */
    if (!eleDeviceReady) {
        shutDownDevice();
    }
}

SecureEnclave::~SecureEnclave() {
    shutDownDevice();
}

void SecureEnclave::shutDownDevice() {
    eleDeviceReady = false;
}

ErrorType SecureEnclave::checkDevice() {
    if (!eleDeviceReady) {
        return ELE_NOT_INITED;
    }

    auto callingId = android::IPCThreadState::self()->getCallingUid();
    if (callingId != KEY_STORE_NONCE) {
        ALOGE("unauthorized user: %d, rejected!", callingId);
        return ELE_GENERAL_ERROR;
    }

    return ELE_NO_ERROR;
}

ErrorType SecureEnclave::eleOpenSessionKeystore(uint32_t* keyStoreHandler) {
    ErrorType error;
    uint32_t handle = 0;

    if (!eleDeviceReady) {
        return ELE_NOT_INITED;
    }

    if (eleOps->eleOpenSession() != ELE_NO_ERROR) {
        ALOGE("Failed to open ele session!");
        return ELE_GENERAL_ERROR;
    }

    /* open/load keystore */
    error = eleOps->eleOpenKeyStore(KEY_STORE_ID, KEY_STORE_NONCE,
                                    KEY_STORE_OPERATION_CREATE | OPERATION_SYNC, &handle);
    if (error == ELE_COMMAND_KEYSTORE_CONFLICT) {
        ALOGE("Keystore already existed, loading it...!");
        error = eleOps->eleOpenKeyStore(KEY_STORE_ID, KEY_STORE_NONCE, KEY_STORE_OPERATION_LOAD,
                                        &handle);
        if (error != ELE_NO_ERROR) {
            ALOGE("Keystore load failed!");
            eleOps->eleCloseSession();
            return ELE_GENERAL_ERROR;
        }
    } else if (error != ELE_NO_ERROR) {
        ALOGE("Keystore open failed!");
        eleOps->eleCloseSession();
        return ELE_GENERAL_ERROR;
    }

    *keyStoreHandler = handle;
    return ELE_NO_ERROR;
}

ErrorType SecureEnclave::eleCloseSessionKeystore(uint32_t keyStoreHandler) {
    if (eleOps != nullptr) {
        if (keyStoreHandler != 0) {
            eleOps->eleCloseKeyStore(keyStoreHandler);
        }
        eleOps->eleCloseSession();
    }

    return ELE_NO_ERROR;
}

::ndk::ScopedAStatus SecureEnclave::eleGenerateKey(int32_t in_keyId, int32_t in_pubKeySize,
                                                   int32_t in_keyGroup, int32_t in_keyType,
                                                   int32_t in_sizeBit, int32_t in_lifeTime,
                                                   int32_t in_usage, int32_t in_permitAlgo,
                                                   int32_t in_lifeCycle, int32_t in_flags,
                                                   std::vector<uint8_t>* out_pubKey,
                                                   int32_t* _aidl_return) {
    ErrorType error = ELE_NO_ERROR;
    uint32_t keyId = (uint32_t)in_keyId;
    uint32_t keyStoreHandler = 0;
    uint32_t keyMgtHandle = 0;
    gen_key_attribute keyAttribute;
    std::vector<uint8_t> pubKey(in_pubKeySize, 0);

    error = checkDevice();
    if (error != ELE_NO_ERROR) {
        *_aidl_return = 0;
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    }

    do {
        /* Open session and keystore */
        error = eleOpenSessionKeystore(&keyStoreHandler);
        if (error != ELE_NO_ERROR) {
            ALOGE("ELE session/keystore open failed!");
            break;
        }

        /* Open keymangement session */
        error = eleOps->eleOpenKeyManagement(keyStoreHandler, &keyMgtHandle);
        if (error != ELE_NO_ERROR) {
            ALOGE("Key management open failed!");
            break;
        }

        /* Generate key */
        memset(&keyAttribute, 0, sizeof(gen_key_attribute));
        keyAttribute.pub_key_size = in_pubKeySize;
        keyAttribute.key_group = in_keyGroup;
        keyAttribute.type = in_keyType;
        keyAttribute.size_bits = in_sizeBit;
        keyAttribute.lifetime = in_lifeTime;
        keyAttribute.usage = in_usage;
        keyAttribute.permit_algo = in_permitAlgo;
        keyAttribute.lifecycle = in_lifeCycle;
        keyAttribute.flags = in_flags;
        keyAttribute.pub_key_lsb_addr = pubKey.data();
        error = eleOps->eleGenerateKey(keyMgtHandle, &keyId, &keyAttribute);
        if (error != ELE_NO_ERROR) {
            ALOGE("Failed to generate key!");
            break;
        }

    } while (false);

    /* Close key management handle */
    if (keyMgtHandle != 0)
        eleOps->eleCloseKeyManagement(keyMgtHandle);
    /* Close session and keystore */
    eleCloseSessionKeystore(keyStoreHandler);

    if (error != ELE_NO_ERROR) {
        *_aidl_return = 0;
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    } else {
        /* Succeed to generate the key */
        *_aidl_return = keyId;
        if (in_pubKeySize > 0) {
            *out_pubKey = pubKey;
        }

        ALOGI("Succeed generating key with ID: %d!", keyId);
        return ndk::ScopedAStatus::ok();
    }
}

::ndk::ScopedAStatus SecureEnclave::eleDeleteKey(int32_t in_keyId, int32_t in_flags) {
    ErrorType error = ELE_NO_ERROR;
    uint32_t keyStoreHandler = 0;
    uint32_t keyMgtHandle = 0;

    error = checkDevice();
    if (error != ELE_NO_ERROR) {
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    }

    do {
        /* Open session and keystore */
        error = eleOpenSessionKeystore(&keyStoreHandler);
        if (error != ELE_NO_ERROR) {
            ALOGE("ELE session/keystore open failed!");
            break;
        }

        /* Open keymangement */
        error = eleOps->eleOpenKeyManagement(keyStoreHandler, &keyMgtHandle);
        if (error != ELE_NO_ERROR) {
            ALOGE("Key management open failed!");
            break;
        }

        /* Delete the key */
        error = eleOps->eleDeleteKey(keyMgtHandle, in_keyId, in_flags);
        if (error != ELE_NO_ERROR) {
            ALOGE("Failed to delete key with id: %d!", in_keyId);
            break;
        }
    } while (false);

    /* Close key management handle */
    if (keyMgtHandle != 0)
        eleOps->eleCloseKeyManagement(keyMgtHandle);
    /* Close session and keystore */
    eleCloseSessionKeystore(keyStoreHandler);

    if (error != ELE_NO_ERROR) {
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    } else {
        ALOGI("Key with id: %d deleted!", in_keyId);
        return ndk::ScopedAStatus::ok();
    }
}

::ndk::ScopedAStatus SecureEnclave::eleGetKeyAttr(int32_t in_keyId,
                                                  ISecureEnclave::KeyAttribute* out_keyAttr) {
    ErrorType error = ELE_NO_ERROR;
    uint32_t keyStoreHandler = 0;
    uint32_t keyMgtHandle = 0;
    key_attribute key_attr;

    error = checkDevice();
    if (error != ELE_NO_ERROR) {
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    }

    do {
        /* Open session and keystore */
        error = eleOpenSessionKeystore(&keyStoreHandler);
        if (error != ELE_NO_ERROR) {
            ALOGE("ELE session/keystore open failed!");
            break;
        }

        /* Open keymangement */
        error = eleOps->eleOpenKeyManagement(keyStoreHandler, &keyMgtHandle);
        if (error != ELE_NO_ERROR) {
            ALOGE("Key management open failed!");
            break;
        }

        /* Get key attribute */
        memset(&key_attr, 0, sizeof(key_attribute));
        error = eleOps->eleGetKeyAttr(keyMgtHandle, in_keyId, &key_attr);
        if (error != ELE_NO_ERROR) {
            ALOGE("Failed to get key(%d) attribute!", in_keyId);
            break;
        }
    } while (false);

    /* Close key management handle */
    if (keyMgtHandle != 0)
        eleOps->eleCloseKeyManagement(keyMgtHandle);
    /* Close session and keystore */
    eleCloseSessionKeystore(keyStoreHandler);

    if (error != ELE_NO_ERROR) {
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    } else {
        ALOGI("Key(%d) attribute retrieved!", in_keyId);
        out_keyAttr->type = key_attr.type;
        out_keyAttr->sizeBit = key_attr.size_bits;
        out_keyAttr->lifeTime = key_attr.lifetime;
        out_keyAttr->usage = key_attr.usage;
        out_keyAttr->permitAlgo = key_attr.permit_algo;
        out_keyAttr->lifeCycle = key_attr.lifecycle;

        return ndk::ScopedAStatus::ok();
    }
}

::ndk::ScopedAStatus SecureEnclave::eleCipherOperation(int32_t in_keyId,
                                                       const std::vector<uint8_t>& in_iv,
                                                       int32_t in_flags, int32_t in_algo,
                                                       const std::vector<uint8_t>& in_input,
                                                       std::vector<uint8_t>* out_output,
                                                       int32_t* _aidl_return) {
    ErrorType error = ELE_NO_ERROR;
    uint32_t keyStoreHandler = 0;
    uint32_t cipherHandle = 0;
    cipher_operation_attr cipherOperationAttr;

    error = checkDevice();
    if (error != ELE_NO_ERROR) {
        *_aidl_return = 0;
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    }

    do {
        /* Open session and keystore */
        error = eleOpenSessionKeystore(&keyStoreHandler);
        if (error != ELE_NO_ERROR) {
            ALOGE("ELE session/keystore open failed!");
            break;
        }

        /* Open cipher session */
        error = eleOps->eleOpenCipher(keyStoreHandler, &cipherHandle);
        if (error != ELE_NO_ERROR) {
            ALOGE("Key cipher open failed!");
            break;
        }

        /* Perform cipher operation */
        memset(&cipherOperationAttr, 0, sizeof(cipher_operation_attr));
        cipherOperationAttr.key_id = in_keyId;
        cipherOperationAttr.iv_addr = (uint8_t*)(in_iv.data());
        cipherOperationAttr.iv_size = in_iv.size();
        cipherOperationAttr.flags = in_flags;
        cipherOperationAttr.algo = in_algo;
        cipherOperationAttr.input_addr = (uint8_t*)(in_input.data());
        cipherOperationAttr.input_size = in_input.size();
        cipherOperationAttr.output_addr = out_output->data();
        cipherOperationAttr.output_size = out_output->size();
        error = eleOps->eleCipherOperation(cipherHandle, &cipherOperationAttr);
        if (error != ELE_NO_ERROR) {
            ALOGE("Cipher operation failed!");
            break;
        }
    } while (false);

    /* Close cipher session */
    if (cipherHandle != 0)
        eleOps->eleCloseCipher(cipherHandle);
    /* Close session and keystore */
    eleCloseSessionKeystore(keyStoreHandler);

    /* return the actual output size */
    *_aidl_return = cipherOperationAttr.output_size;
    if (error != ELE_NO_ERROR) {
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    } else {
        ALOGI("Cipher operation succeed!");
        return ndk::ScopedAStatus::ok();
    }
}

::ndk::ScopedAStatus SecureEnclave::eleCipherAEOperation(
        int32_t in_keyId, const std::vector<uint8_t>& in_iv, int32_t in_flags, int32_t in_algo,
        const std::vector<uint8_t>& in_aad, const std::vector<uint8_t>& in_input,
        std::vector<uint8_t>* out_output, int32_t* _aidl_return) {
    ErrorType error = ELE_NO_ERROR;
    uint32_t keyStoreHandler = 0;
    uint32_t cipherHandle = 0;
    cipher_ae_operation_attr cipherAEOperationAttr;

    error = checkDevice();
    if (error != ELE_NO_ERROR) {
        *_aidl_return = 0;
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    }

    do {
        /* Open session and keystore */
        error = eleOpenSessionKeystore(&keyStoreHandler);
        if (error != ELE_NO_ERROR) {
            ALOGE("ELE session/keystore open failed!");
            break;
        }

        /* Open cipher session */
        error = eleOps->eleOpenCipher(keyStoreHandler, &cipherHandle);
        if (error != ELE_NO_ERROR) {
            ALOGE("Key cipher open failed!");
            break;
        }

        /* Perform authenticated cipher operation */
        memset(&cipherAEOperationAttr, 0, sizeof(cipher_ae_operation_attr));
        cipherAEOperationAttr.key_id = in_keyId;
        cipherAEOperationAttr.iv_addr = (uint8_t*)(in_iv.data());
        cipherAEOperationAttr.iv_size = in_iv.size();
        cipherAEOperationAttr.flags = in_flags;
        cipherAEOperationAttr.algo = in_algo;
        cipherAEOperationAttr.aad_addr = (uint8_t*)(in_aad.data());
        cipherAEOperationAttr.aad_size = in_aad.size();
        cipherAEOperationAttr.input_addr = (uint8_t*)(in_input.data());
        cipherAEOperationAttr.input_size = in_input.size();
        cipherAEOperationAttr.output_addr = out_output->data();
        cipherAEOperationAttr.output_size = out_output->size();
        error = eleOps->eleCipherAEOperation(cipherHandle, &cipherAEOperationAttr);
        if (error != ELE_NO_ERROR) {
            ALOGE("Authenticated cipher operation failed!");
            break;
        }
    } while (false);

    /* Close cipher session */
    if (cipherHandle != 0)
        eleOps->eleCloseCipher(cipherHandle);
    /* Close session and keystore */
    eleCloseSessionKeystore(keyStoreHandler);

    /* return the actual output size */
    *_aidl_return = cipherAEOperationAttr.output_size;
    if (error != ELE_NO_ERROR) {
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    } else {
        ALOGI("Authenticated cipher operation succeed!");
        return ndk::ScopedAStatus::ok();
    }
}

::ndk::ScopedAStatus SecureEnclave::eleSignGenerate(int32_t in_keyId,
                                                    const std::vector<uint8_t>& in_message,
                                                    std::vector<uint8_t>* out_signature,
                                                    int32_t in_flags, int32_t in_signScheme,
                                                    int32_t in_saltLen, int32_t* _aidl_return) {
    ErrorType error = ELE_NO_ERROR;
    gen_sign_attr signGenerateAttr;
    uint32_t keyStoreHandler = 0;
    uint32_t signGenHandle = 0;

    error = checkDevice();
    if (error != ELE_NO_ERROR) {
        *_aidl_return = 0;
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    }

    do {
        /* Open session and keystore */
        error = eleOpenSessionKeystore(&keyStoreHandler);
        if (error != ELE_NO_ERROR) {
            ALOGE("ELE session/keystore open failed!");
            break;
        }

        /* Open signature generation session */
        error = eleOps->eleSignGenerateOpen(keyStoreHandler, &signGenHandle);
        if (error != ELE_NO_ERROR) {
            ALOGE("Signature generation session open failed!");
            break;
        }

        /* Generate signature */
        memset(&signGenerateAttr, 0, sizeof(gen_sign_attr));
        signGenerateAttr.key_id = in_keyId;
        signGenerateAttr.msg_lsb_addr = (uint8_t*)(in_message.data());
        signGenerateAttr.msg_size = in_message.size();
        signGenerateAttr.sign_lsb_addr = out_signature->data();
        signGenerateAttr.sign_size = out_signature->size();
        signGenerateAttr.flags = in_flags;
        signGenerateAttr.sign_scheme = in_signScheme;
        signGenerateAttr.salt_len = in_saltLen;
        error = eleOps->eleSignGenerate(signGenHandle, &signGenerateAttr);
        if (error != ELE_NO_ERROR) {
            ALOGE("Signature generation failed!");
            break;
        };
    } while (false);

    /* Close signature generation session */
    if (signGenHandle != 0)
        eleOps->eleSignGenerateClose(signGenHandle);
    /* Close session and keystore */
    eleCloseSessionKeystore(keyStoreHandler);

    /* return the actual output size */
    *_aidl_return = signGenerateAttr.sign_size;
    if (error != ELE_NO_ERROR) {
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    } else {
        ALOGI("Signature generation succeed!");
        return ndk::ScopedAStatus::ok();
    }
}

::ndk::ScopedAStatus SecureEnclave::eleSignVerify(const std::vector<uint8_t>& in_key,
                                                  const std::vector<uint8_t>& in_message,
                                                  const std::vector<uint8_t>& in_signature,
                                                  int32_t in_keySecuritySize, int32_t in_keyType,
                                                  int32_t in_flags, int32_t in_signScheme,
                                                  int32_t in_saltLength) {
    ErrorType error = ELE_NO_ERROR;
    uint32_t keyStoreHandler = 0;
    uint32_t signVerifyHandle = 0;
    verify_sign_attr signVerifyAttr;

    error = checkDevice();
    if (error != ELE_NO_ERROR) {
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    }

    do {
        /* Open session and keystore */
        error = eleOpenSessionKeystore(&keyStoreHandler);
        if (error != ELE_NO_ERROR) {
            ALOGE("ELE session/keystore open failed!");
            break;
        }

        /* Open signature verify session */
        error = eleOps->eleSignVerifyOpen(&signVerifyHandle);
        if (error != ELE_NO_ERROR) {
            ALOGE("Signature verify session open failed!");
            break;
        }

        /* Signature verification */
        memset(&signVerifyAttr, 0, sizeof(verify_sign_attr));
        signVerifyAttr.key_lsb_addr = (uint8_t*)(in_key.data());
        signVerifyAttr.key_size = in_key.size();
        signVerifyAttr.msg_lsb_addr = (uint8_t*)(in_message.data());
        signVerifyAttr.msg_size = in_message.size();
        signVerifyAttr.sign_lsb_addr = (uint8_t*)(in_signature.data());
        signVerifyAttr.sign_size = in_signature.size();
        signVerifyAttr.key_security_size = in_keySecuritySize;
        signVerifyAttr.key_type = in_keyType;
        signVerifyAttr.flags = in_flags;
        signVerifyAttr.sign_scheme = in_signScheme;
        signVerifyAttr.salt_len = in_saltLength;
        error = eleOps->eleSignVerify(signVerifyHandle, &signVerifyAttr);
        if (error != ELE_NO_ERROR) {
            ALOGE("Signature verification failed!");
            break;
        };
    } while (false);

    /* Close signature verify session */
    if (signVerifyHandle != 0)
        eleOps->eleSignVerifyClose(signVerifyHandle);
    /* Close session and keystore */
    eleCloseSessionKeystore(keyStoreHandler);

    if (error != ELE_NO_ERROR) {
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    } else {
        ALOGI("Signature verification succeed!");
        return ndk::ScopedAStatus::ok();
    }
}

::ndk::ScopedAStatus SecureEnclave::eleMacOperation(int32_t in_keyId,
                                                    const std::vector<uint8_t>& in_payload,
                                                    std::vector<uint8_t>* in_mac,
                                                    int32_t in_macSize, int32_t in_flag,
                                                    int32_t in_algorithm, int32_t* _aidl_return) {
    ErrorType error = ELE_NO_ERROR;
    uint32_t keyStoreHandler = 0;
    uint32_t macHandle = 0;
    mac_operation_attr macOperationAttr;

    *_aidl_return = 0;

    error = checkDevice();
    if (error != ELE_NO_ERROR) {
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    }

    do {
        if (in_macSize > in_mac->size()) {
            ALOGE("mac buffer is too small!");
            error = ELE_INVALID_ARGS;
            break;
        }

        /* Open session and keystore */
        error = eleOpenSessionKeystore(&keyStoreHandler);
        if (error != ELE_NO_ERROR) {
            ALOGE("ELE session/keystore open failed!");
            break;
        }

        /* Open mac session */
        error = eleOps->eleMacOpen(keyStoreHandler, &macHandle);
        if (error != ELE_NO_ERROR) {
            ALOGE("MAC session failed!");
            break;
        }

        /* Perform mac operation */
        memset(&macOperationAttr, 0, sizeof(mac_operation_attr));
        macOperationAttr.key_id = in_keyId;
        macOperationAttr.payload_addr = (uint8_t*)(in_payload.data());
        macOperationAttr.payload_size = in_payload.size();
        macOperationAttr.mac_addr = in_mac->data();
        macOperationAttr.mac_size = in_macSize;
        macOperationAttr.flags = in_flag;
        macOperationAttr.algo = in_algorithm;
        error = eleOps->eleMacOperation(macHandle, &macOperationAttr);
        if (error != ELE_NO_ERROR) {
            ALOGE("MAC operation failed!");
            break;
        }
    } while (false);

    /* Close mac session */
    if (macHandle != 0)
        eleOps->eleMacClose(macHandle);
    /* Close session and keystore */
    eleCloseSessionKeystore(keyStoreHandler);

    if (error != ELE_NO_ERROR) {
        return ndk::ScopedAStatus::fromServiceSpecificError(error);
    } else {
        ALOGI("MAC operation succeed!");
        *_aidl_return = macOperationAttr.mac_size;
        return ndk::ScopedAStatus::ok();
    }
}

} // namespace ele
} // namespace hardware
} // namespace nxp
} // namespace aidl
