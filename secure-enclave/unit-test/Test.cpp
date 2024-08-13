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
#include "EleOperation.h"

#define KEY_STORE_ID 0x11111
#define KEY_STORE_NONCE (1000)
#define KEY_GROUP 1
#define KEY_ID 0
#define KEY_PUBKEY_SIZE 64

uint8_t hash_data[32] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A,
                         0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
                         0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F};

uint8_t iv_data[16] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                       0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};

uint8_t iv_data_aead[12] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B,
};

uint8_t aad_data_aead[16] = {
        0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
        0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
};

int test_signature_generate_verification(EleOperation *ops, uint32_t keyStoreHandler,
                                         uint32_t keyID, uint8_t *pubKey) {
    struct verify_sign_attr sign_verify_attr;
    struct gen_sign_attr sign_generate_attr;
    uint32_t signGenHandle = 0;
    uint32_t signVerifyHandle = 0;
    uint8_t signature[128];
    ErrorType error;
    int ret = 0;

    /* open signature generation session */
    error = ops->eleSignGenerateOpen(keyStoreHandler, &signGenHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: Failed to open sign generate session!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: Succeed to open sign generate session, ID:%d!", signGenHandle);
    }

    /* generate signature */
    memset(&sign_generate_attr, 0, sizeof(sign_generate_attr));
    memset(signature, 0, sizeof(signature));
    sign_generate_attr.key_id = keyID;
    sign_generate_attr.msg_lsb_addr = hash_data;
    sign_generate_attr.sign_lsb_addr = signature;
    sign_generate_attr.msg_size = sizeof(hash_data);
    sign_generate_attr.sign_size = sizeof(signature);
    sign_generate_attr.flags = ELE_SIGN_FLAGS_MESSAGE;
    sign_generate_attr.sign_scheme = PERMITTED_ALGO_ECDSA_SHA256;
    sign_generate_attr.salt_len = 0;
    error = ops->eleSignGenerate(signGenHandle, &sign_generate_attr);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: Generate signature failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: Generate signature succeed!");
        ALOGE("======== dump signature ========");
        for (int i = 0; i < sign_generate_attr.sign_size; i++) ALOGE("%02x", signature[i]);
        ALOGE("=================================");
    }

    /* open signature verification session */
    error = ops->eleSignVerifyOpen(&signVerifyHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: Failed to open sign verify session!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: Succeed to open sign verify session, ID:%d!", signGenHandle);
    }

    /* verify signature */
    memset(&sign_verify_attr, 0, sizeof(sign_verify_attr));
    sign_verify_attr.key_lsb_addr = pubKey;
    sign_verify_attr.msg_lsb_addr = hash_data;
    sign_verify_attr.sign_lsb_addr = signature;
    sign_verify_attr.msg_size = sizeof(hash_data);
    sign_verify_attr.sign_size = sign_generate_attr.sign_size;
    sign_verify_attr.key_size = KEY_PUBKEY_SIZE;
    sign_verify_attr.key_security_size = KEY_SIZE_ECC_NIST_256;
    sign_verify_attr.key_type = PUBKEY_TYPE_ECC_NIST;
    sign_verify_attr.flags = ELE_SIGN_FLAGS_MESSAGE;
    sign_verify_attr.sign_scheme = PERMITTED_ALGO_ECDSA_SHA256;
    sign_verify_attr.salt_len = 0;
    error = ops->eleSignVerify(signVerifyHandle, &sign_verify_attr);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: Verify signature failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: Verify signature succeed!");
    }

    ret = 0;
exit:
    /* close signature generation session */
    error = ops->eleSignGenerateClose(signGenHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: Failed to close signature generation session!");
        ret = -1;
    } else {
        ALOGE("Test: Succeed to close signature generation session!");
    }

    /* close verify signature session */
    error = ops->eleSignVerifyClose(signVerifyHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: Failed to close signature verify session!");
        ret = -1;
    } else {
        ALOGE("Test: Succeed to close signature verify session!");
    }

    return ret;
}

int test_keypair_mangement_and_signature(EleOperation *ops, uint32_t keyStoreHandler,
                                         uint32_t keyMgtHandle) {
    gen_key_attribute keyAttribute;
    key_attribute key_attr;
    uint32_t keyID = 0;
    ErrorType error;
    uint8_t *key_buf = nullptr;
    int ret = 0;

    /* generate key pair */
    key_buf = (uint8_t *)malloc(KEY_PUBKEY_SIZE);
    if (!key_buf) {
        ALOGE("Test: failed to allocate memory!");
        ret = -1;
        goto exit;
    }

    memset(&keyAttribute, 0, sizeof(gen_key_attribute));
    /* let the ELE choose key id automatically */
    keyID = KEY_ID;
    keyAttribute.pub_key_size = KEY_PUBKEY_SIZE;
    keyAttribute.key_group = KEY_GROUP;
    keyAttribute.type = KEY_TYPE_ECC_NIST;
    keyAttribute.size_bits = KEY_SIZE_ECC_NIST_256;
    keyAttribute.lifetime = STD_PERSISTENT;
    keyAttribute.usage = KEY_USAGE_SIGN_MSG | KEY_USAGE_VERIFY_MSG;
    keyAttribute.permit_algo = PERMITTED_ALGO_ECDSA_SHA256;
    keyAttribute.lifecycle = LIFE_CYCLE_CURRENT;
    keyAttribute.flags = OPERATION_SYNC;
    keyAttribute.pub_key_lsb_addr = key_buf;
    error = ops->eleGenerateKey(keyMgtHandle, &keyID, &keyAttribute);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: generate key pair failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: key pair generated successfully with ID: 0x%x!", keyID);
        ALOGE("======== dump public key ========");
        for (int i = 0; i < KEY_PUBKEY_SIZE; i++) ALOGE("%02x", key_buf[i]);
        ALOGE("=================================");
    }

    /* get key attribure */
    memset(&key_attr, 0, sizeof(key_attribute));
    error = ops->eleGetKeyAttr(keyMgtHandle, keyID, &key_attr);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: get key pair attribute failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: get key pair attribute successfully!");
        ALOGE("Test: asymmetric key type: 0x%x, size: 0x%x, lifetime: 0x%x, usage: 0x%x, algo: 0x%x, lifecycle: 0x%x",
              key_attr.type, key_attr.size_bits, key_attr.lifetime, key_attr.usage,
              key_attr.permit_algo, key_attr.lifecycle);
    }

    /* test signature generation and verification */
    ret = test_signature_generate_verification(ops, keyStoreHandler, keyID, key_buf);
    if (ret < 0) {
        ALOGE("Test: Signature generate and verify failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: Signature generate and verify succeed!");
    }

    ret = 0;

exit:
    /* delete the keypair */
    error = ops->eleDeleteKey(keyMgtHandle, keyID, OPERATION_SYNC);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: delete key pair failed!");
        ret = -1;
    } else {
        ALOGE("Test: delete key pair successfully!");
    }

    if (key_buf)
        free(key_buf);

    return ret;
}

int test_cipher_operation(EleOperation *ops, uint32_t keyStoreHandler, uint32_t keyMgtHandle) {
    cipher_operation_attr sym_key_op_attr;
    gen_key_attribute keyAttribute;
    ErrorType error;
    uint32_t cipherHandle = 0;
    uint8_t ciphered_data[32];
    uint8_t deciphered_data[32];
    uint32_t keyID = 0;
    int ret = 0;

    /* generate symmetric key for AES */
    memset(&keyAttribute, 0, sizeof(gen_key_attribute));
    /* let the ELE choose key id automatically */
    keyID = KEY_ID;
    keyAttribute.key_group = KEY_GROUP;
    keyAttribute.type = KEY_TYPE_AES;
    keyAttribute.size_bits = KEY_SIZE_AES_256;
    keyAttribute.lifetime = STD_PERSISTENT;
    keyAttribute.usage = KEY_USAGE_ENCRYPT | KEY_USAGE_DECRYPT;
    keyAttribute.permit_algo = PERMITTED_ALGO_ALL_CIPHER;
    keyAttribute.lifecycle = LIFE_CYCLE_CURRENT;
    keyAttribute.flags = OPERATION_SYNC;
    error = ops->eleGenerateKey(keyMgtHandle, &keyID, &keyAttribute);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: generate symmetric key failed!");
        return -1;
    } else {
        ALOGE("Test: symmetric key generated successfully with ID: 0x%x!", keyID);
    }

    /* open key cipher */
    error = ops->eleOpenCipher(keyStoreHandler, &cipherHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: open key cipher failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: open key cipher succeed!");
    }

    /* encrypt with symmetric key */
    memset(&sym_key_op_attr, 0, sizeof(cipher_operation_attr));
    sym_key_op_attr.key_id = keyID;
    sym_key_op_attr.iv_addr = iv_data;
    sym_key_op_attr.iv_size = sizeof(iv_data);
    sym_key_op_attr.flags = CIPHER_ONE_GO_FLAGS_ENCRYPT;
    sym_key_op_attr.algo = PERMITTED_ALGO_CBC_NO_PADDING;
    sym_key_op_attr.input_addr = hash_data;
    sym_key_op_attr.input_size = sizeof(hash_data);
    sym_key_op_attr.output_addr = ciphered_data;
    sym_key_op_attr.output_size = sizeof(ciphered_data);
    error = ops->eleCipherOperation(cipherHandle, &sym_key_op_attr);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: cipher encrypt failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: cipher encrypt succeed!");
        ALOGE("======== dump encrypted data ========");
        for (int i = 0; i < sizeof(ciphered_data); i++) ALOGE("%02x", ciphered_data[i]);
        ALOGE("=================================");
    }

    /* decrypt with symmetric key */
    memset(&sym_key_op_attr, 0, sizeof(cipher_operation_attr));
    sym_key_op_attr.key_id = keyID;
    sym_key_op_attr.iv_addr = iv_data;
    sym_key_op_attr.iv_size = sizeof(iv_data);
    sym_key_op_attr.flags = CIPHER_ONE_GO_FLAGS_DECRYPT;
    sym_key_op_attr.algo = PERMITTED_ALGO_CBC_NO_PADDING;
    sym_key_op_attr.input_addr = ciphered_data;
    sym_key_op_attr.input_size = sizeof(ciphered_data);
    sym_key_op_attr.output_addr = deciphered_data;
    sym_key_op_attr.output_size = sizeof(deciphered_data);
    error = ops->eleCipherOperation(cipherHandle, &sym_key_op_attr);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: cipher decrypt failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: cipher decrypt succeed!");
        ALOGE("======== dump decrypted data ========");
        for (int i = 0; i < sizeof(deciphered_data); i++) ALOGE("%02x", deciphered_data[i]);
        ALOGE("=================================");

        ALOGE("======== dump original data ========");
        for (int i = 0; i < sizeof(hash_data); i++) ALOGE("%02x", hash_data[i]);
        ALOGE("=================================");

        if (memcmp(deciphered_data, hash_data, sizeof(hash_data))) {
            ALOGE("Test: The decrypted data is not equal with the original data!");
            ret = -1;
            goto exit;
        } else {
            ALOGE("Test: The decrypted data matches original data!");
        }
    }

    ret = 0;

exit:
    /* delete the key */
    error = ops->eleDeleteKey(keyMgtHandle, keyID, OPERATION_SYNC);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: delete symmetric cipher key failed!");
    } else {
        ALOGE("Test: delete symmetric cipher key successfully!");
    }

    /* close cipher */
    error = ops->eleCloseCipher(cipherHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: key cipher close failed!");
    } else {
        ALOGE("Test: key cipher close successfully!");
    }

    return ret;
}

int test_cipher_ae_operation(EleOperation *ops, uint32_t keyStoreHandler, uint32_t keyMgtHandle) {
    cipher_ae_operation_attr sym_key_ae_op_addr;
    gen_key_attribute keyAttribute;
    ErrorType error;
    uint32_t cipherHandle = 0;
    uint8_t ciphered_aead_data[48];
    uint8_t deciphered_aead_data[32];
    uint32_t keyID = 0;
    int ret = 0;

    /* generate symmetric key for AES CBC/GCM */
    memset(&keyAttribute, 0, sizeof(gen_key_attribute));
    /* let the ELE choose key id automatically */
    keyID = KEY_ID;
    keyAttribute.key_group = KEY_GROUP;
    keyAttribute.type = KEY_TYPE_AES;
    keyAttribute.size_bits = KEY_SIZE_AES_256;
    keyAttribute.lifetime = STD_PERSISTENT;
    keyAttribute.usage = KEY_USAGE_ENCRYPT | KEY_USAGE_DECRYPT;
    keyAttribute.permit_algo = PERMITTED_ALGO_ALL_AEAD;
    keyAttribute.lifecycle = LIFE_CYCLE_CURRENT;
    keyAttribute.flags = OPERATION_SYNC;
    error = ops->eleGenerateKey(keyMgtHandle, &keyID, &keyAttribute);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: generate cipher ae key failed!");
        return -1;
    } else {
        ALOGE("Test: cipher ae key generated successfully with ID: 0x%x!", keyID);
    }

    /* open key cipher */
    error = ops->eleOpenCipher(keyStoreHandler, &cipherHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: open key cipher failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: open key cipher succeed!");
    }

    /* encryption */
    memset(&sym_key_ae_op_addr, 0, sizeof(sym_key_ae_op_addr));
    sym_key_ae_op_addr.key_id = keyID;
    sym_key_ae_op_addr.iv_addr = iv_data_aead;
    sym_key_ae_op_addr.iv_size = sizeof(iv_data_aead);
    sym_key_ae_op_addr.flags = CIPHER_ONE_GO_FLAGS_ENCRYPT;
    sym_key_ae_op_addr.algo = PERMITTED_ALGO_CCM;
    sym_key_ae_op_addr.aad_addr = aad_data_aead;
    sym_key_ae_op_addr.aad_size = sizeof(aad_data_aead);
    sym_key_ae_op_addr.input_addr = hash_data;
    sym_key_ae_op_addr.input_size = sizeof(hash_data);
    sym_key_ae_op_addr.output_addr = ciphered_aead_data;
    sym_key_ae_op_addr.output_size = sizeof(ciphered_aead_data);
    error = ops->eleCipherAEOperation(cipherHandle, &sym_key_ae_op_addr);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: cipher aead encrypt failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: cipher aead encrypt succeed!");
        ALOGE("======== dump encrypted data ========");
        for (int i = 0; i < sizeof(ciphered_aead_data); i++) ALOGE("%02x", ciphered_aead_data[i]);
        ALOGE("=================================");
    }

    /* dencryption */
    memset(&sym_key_ae_op_addr, 0, sizeof(sym_key_ae_op_addr));
    sym_key_ae_op_addr.key_id = keyID;
    sym_key_ae_op_addr.iv_addr = iv_data_aead;
    sym_key_ae_op_addr.iv_size = sizeof(iv_data_aead);
    sym_key_ae_op_addr.flags = CIPHER_ONE_GO_FLAGS_DECRYPT;
    sym_key_ae_op_addr.algo = PERMITTED_ALGO_CCM;
    sym_key_ae_op_addr.aad_addr = aad_data_aead;
    sym_key_ae_op_addr.aad_size = sizeof(aad_data_aead);
    sym_key_ae_op_addr.input_addr = ciphered_aead_data;
    sym_key_ae_op_addr.input_size = sizeof(ciphered_aead_data);
    sym_key_ae_op_addr.output_addr = deciphered_aead_data;
    sym_key_ae_op_addr.output_size = sizeof(deciphered_aead_data);
    error = ops->eleCipherAEOperation(cipherHandle, &sym_key_ae_op_addr);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: cipher aead decrypt failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: cipher aead decrypt succeed!");
        ALOGE("======== dump decrypted data ========");
        for (int i = 0; i < sizeof(deciphered_aead_data); i++)
            ALOGE("%02x", deciphered_aead_data[i]);
        ALOGE("=================================");

        ALOGE("======== dump original data ========");
        for (int i = 0; i < sizeof(hash_data); i++) ALOGE("%02x", hash_data[i]);
        ALOGE("=================================");

        if (memcmp(deciphered_aead_data, hash_data, sizeof(hash_data))) {
            ALOGE("Test: The decrypted data is not equal with the original data!");
            ret = -1;
            goto exit;
        } else {
            ALOGE("Test: The decrypted data matches original data!");
        }
    }

    ret = 0;

exit:
    /* delete the key */
    error = ops->eleDeleteKey(keyMgtHandle, keyID, OPERATION_SYNC);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: delete key failed!");
        goto exit;
    } else {
        ALOGE("Test: delete key successfully!");
    }

    /* close cipher */
    error = ops->eleCloseCipher(cipherHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: key cipher close failed!");
    } else {
        ALOGE("Test: key cipher close successfully!");
    }

    return ret;
}

int main() {
    uint32_t keyStoreHandler = 0;
    uint32_t keyMgtHandle = 0;
    ErrorType error;
    int ret = 0;

    EleOperation ops(MU_CHANNEL_PLAT_HSM_SECONDARY);
    if (ops.eleOpenDeviceNode() != ELE_NO_ERROR) {
        ALOGE("Test: Failed to open ELE device nodes!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: Succeed to open ELE device nodes!");
    }

    if (ops.eleOpenSession() != ELE_NO_ERROR) {
        ALOGE("Test: Failed to open ELE session!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: Succeed to open ELE session!");
    }

    /* open/load keystore */
    error = ops.eleOpenKeyStore(KEY_STORE_ID, KEY_STORE_NONCE,
                                KEY_STORE_OPERATION_CREATE | OPERATION_SYNC, &keyStoreHandler);
    if (error == ELE_COMMAND_KEYSTORE_CONFLICT) {
        ALOGE("Test: Keystore already existed, loading it...!");
        error = ops.eleOpenKeyStore(KEY_STORE_ID, KEY_STORE_NONCE, KEY_STORE_OPERATION_LOAD,
                                    &keyStoreHandler);
        if (error != ELE_NO_ERROR) {
            ALOGE("Test: Keystore load failed!");
            ret = -1;
            goto exit;
        } else {
            ALOGE("Test: Keystore load successfully!");
        }
    } else if (error != ELE_NO_ERROR) {
        ALOGE("Test: Keystore open failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: Keystore open successfully!");
    }

    /* open keymangement */
    error = ops.eleOpenKeyManagement(keyStoreHandler, &keyMgtHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: key management open failed!");
        ret = -1;
        goto exit;
    } else {
        ALOGE("Test: key management open succeed!");
    }

    /* asymmetric key test */
    ret = test_keypair_mangement_and_signature(&ops, keyStoreHandler, keyMgtHandle);
    if (ret)
        goto exit;

    /* symmetric cipher operation test */
    ret = test_cipher_operation(&ops, keyStoreHandler, keyMgtHandle);
    if (ret)
        goto exit;

    /* symmetric cipher authenticated encryption operation test */
    ret = test_cipher_ae_operation(&ops, keyStoreHandler, keyMgtHandle);
    if (ret)
        goto exit;

    /* Finally! */
    ALOGE("Test: ********************************");
    ALOGE("Test: ******** ALL TESTS PASS ********");
    ALOGE("Test: ********************************");

    ret = 0;

exit:
    error = ops.eleCloseKeyManagement(keyMgtHandle);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: key management close failed!");
    } else {
        ALOGE("Test: key management close successfully!");
    }

    error = ops.eleCloseKeyStore(keyStoreHandler);
    if (error != ELE_NO_ERROR) {
        ALOGE("Test: Keystore close failed!");
    } else {
        ALOGE("Test: Keystore close successfully!");
    }

    if (ops.eleCloseSession() != ELE_NO_ERROR) {
        ALOGE("Test: Failed to close ELE session!");
    } else {
        ALOGE("Test: Succeed to close ELE session!");
    }

    return ret;
}
