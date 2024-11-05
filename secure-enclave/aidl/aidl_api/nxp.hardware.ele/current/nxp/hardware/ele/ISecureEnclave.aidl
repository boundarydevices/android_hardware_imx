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
///////////////////////////////////////////////////////////////////////////////
// THIS FILE IS IMMUTABLE. DO NOT EDIT IN ANY CASE.                          //
///////////////////////////////////////////////////////////////////////////////

// This file is a snapshot of an AIDL file. Do not edit it manually. There are
// two cases:
// 1). this is a frozen version file - do not edit this in any case.
// 2). this is a 'current' file. If you make a backwards compatible change to
//     the interface (from the latest frozen version), the build system will
//     prompt you to update this file with `m <name>-update-api`.
//
// You must not make a backward incompatible change to any AIDL file built
// with the aidl_interface module type with versions property set. The module
// type is used to build AIDL files in a way that they can be used across
// independently updatable components of the system. If a device is shipped
// with such a backward incompatible change, it has a high risk of breaking
// later when a module using the interface is updated, e.g., Mainline modules.

package nxp.hardware.ele;
@VintfStability
interface ISecureEnclave {
  int eleGenerateKey(in int keyId, in int pubKeySize, in int keyGroup, in int keyType, in int sizeBit, in int lifeTime, in int usage, in int permitAlgo, in int lifeCycle, in int flags, out byte[] pubKey);
  void eleDeleteKey(in int keyId, in int flags);
  void eleGetKeyAttr(in int keyId, out nxp.hardware.ele.ISecureEnclave.KeyAttribute keyAttr);
  int eleCipherOperation(in int keyId, in byte[] iv, in int flags, in int algo, in byte[] input, out byte[] output);
  int eleCipherAEOperation(in int keyId, in byte[] iv, in int flags, in int algo, in byte[] aad, in byte[] input, out byte[] output);
  int eleSignGenerate(in int keyId, in byte[] message, out byte[] signature, in int flags, in int signScheme, in int saltLen);
  void eleSignVerify(in byte[] key, in byte[] message, in byte[] signature, in int keySecuritySize, in int keyType, in int flags, in int signScheme, in int saltLength);
  int eleMacOperation(in int keyId, in byte[] payload, inout byte[] mac, in int macSize, in int flag, in int algorithm);
  @Backing(type="int") @VintfStability
  enum KeyType {
    KEY_TYPE_HMAC = 0x1100,
    KEY_TYPE_DERIVE = 0x1200,
    KEY_TYPE_AES = 0x2400,
    KEY_TYPE_SM4 = 0x2405,
    PUBKEY_TYPE_RSA = 0x4001,
    PUBKEY_TYPE_ECC_BP_R1 = 0x4130,
    PUBKEY_TYPE_ECC_NIST = 0x4112,
    KEY_TYPE_RSA = 0x7001,
    KEY_TYPE_ECC_BP_R1 = 0x7130,
    KEY_TYPE_ECC_NIST = 0x7112,
    KEY_TYPE_OEM_IMPORT_MK_SK = 0x9200,
    PUBKEY_TYPE_ECC_BP_T1 = 0xC180,
  }
  @Backing(type="int") @VintfStability
  enum KeySizeBit {
    KEY_SIZE_HMAC_224 = 224,
    KEY_SIZE_HMAC_256 = 256,
    KEY_SIZE_HMAC_384 = 384,
    KEY_SIZE_HMAC_512 = 512,
    KEY_SIZE_AES_128 = 128,
    KEY_SIZE_AES_192 = 192,
    KEY_SIZE_AES_256 = 256,
    KEY_SIZE_SM4_128 = 128,
    KEY_SIZE_RSA_2048 = 2048,
    KEY_SIZE_RSA_3072 = 3072,
    KEY_SIZE_RSA_4096 = 4096,
    KEY_SIZE_ECC_BP_R1_224 = 224,
    KEY_SIZE_ECC_BP_R1_256 = 256,
    KEY_SIZE_ECC_BP_R1_320 = 320,
    KEY_SIZE_ECC_BP_R1_384 = 384,
    KEY_SIZE_ECC_BP_R1_512 = 512,
    KEY_SIZE_ECC_NIST_224 = 224,
    KEY_SIZE_ECC_NIST_256 = 256,
    KEY_SIZE_ECC_NIST_384 = 384,
    KEY_SIZE_ECC_NIST_521 = 521,
    KEY_SIZE_ECC_BP_T1_224 = 224,
    KEY_SIZE_ECC_BP_T1_256 = 256,
    KEY_SIZE_ECC_BP_T1_320 = 320,
    KEY_SIZE_ECC_BP_T1_384 = 384,
    KEY_SIZE_OEM_IMPORT_MK_SK_128 = 128,
    KEY_SIZE_OEM_IMPORT_MK_SK_192 = 192,
    KEY_SIZE_OEM_IMPORT_MK_SK_256 = 256,
  }
  @Backing(type="int") @VintfStability
  enum KeyLifeTime {
    STD_VOLATILE = 0x0,
    STD_PERSISTENT = 0x1,
    STD_PERMANENT = 0xff,
    ELE_IMPORT_VOLATILE = 0xc0020000,
    ELE_IMPORT_PERSISTENT = 0xc0020001,
    ELE_IMPORT_PERMANENT = 0xc00200ff,
  }
  @Backing(type="int") @VintfStability
  enum KeyUsage {
    KEY_USAGE_ENCRYPT = (0x1 << 8) /* 256 */,
    KEY_USAGE_DECRYPT = (0x1 << 9) /* 512 */,
    KEY_USAGE_SIGN_MSG = (0x1 << 10) /* 1024 */,
    KEY_USAGE_VERIFY_MSG = (0x1 << 11) /* 2048 */,
    KEY_USAGE_SIGN_HASH = (0x1 << 12) /* 4096 */,
    KEY_USAGE_VERIFY_HASH = (0x1 << 13) /* 8192 */,
    KEY_USAGE_DERIVE = (0x1 << 14) /* 16384 */,
  }
  @Backing(type="int") @VintfStability
  enum PermitAlgorithms {
    PERMITTED_ALGO_SHA224 = 0x02000008,
    PERMITTED_ALGO_SHA256 = 0x02000009,
    PERMITTED_ALGO_SHA384 = 0x0200000a,
    PERMITTED_ALGO_SHA512 = 0x0200000b,
    PERMITTED_ALGO_SM3 = 0x02000014,
    PERMITTED_ALGO_HMAC_SHA256 = 0x03800009,
    PERMITTED_ALGO_HMAC_SHA384 = 0x0380000a,
    PERMITTED_ALGO_CMAC = 0x03c00200,
    PERMITTED_ALGO_CTR = 0x04c01000,
    PERMITTED_ALGO_CFB = 0x04c01100,
    PERMITTED_ALGO_OFB = 0x04c01200,
    PERMITTED_ALGO_ECB_NO_PADDING = 0x04404400,
    PERMITTED_ALGO_CBC_NO_PADDING = 0x04404000,
    PERMITTED_ALGO_CCM = 0x05500100,
    PERMITTED_ALGO_GCM = 0x05500200,
    PERMITTED_ALGO_CHACHA20_POLY1305 = 0x05100500,
    PERMITTED_ALGO_RSA_PKCS1_V15_SHA224 = 0x06000208,
    PERMITTED_ALGO_RSA_PKCS1_V15_SHA256 = 0x06000209,
    PERMITTED_ALGO_RSA_PKCS1_V15_SHA384 = 0x0600020a,
    PERMITTED_ALGO_RSA_PKCS1_V15_SHA512 = 0x0600020b,
    PERMITTED_ALGO_RSA_PKCS1_PSS_MGF1_SHA224 = 0x06000308,
    PERMITTED_ALGO_RSA_PKCS1_PSS_MGF1_SHA256 = 0x06000309,
    PERMITTED_ALGO_RSA_PKCS1_PSS_MGF1_SHA384 = 0x0600030a,
    PERMITTED_ALGO_RSA_PKCS1_PSS_MGF1_SHA512 = 0x0600030b,
    PERMITTED_ALGO_ECDSA_SHA224 = 0x06000608,
    PERMITTED_ALGO_ECDSA_SHA256 = 0x06000609,
    PERMITTED_ALGO_ECDSA_SHA384 = 0x0600060a,
    PERMITTED_ALGO_ECDSA_SHA512 = 0x0600060b,
    PERMITTED_ALGO_HMAC_KDF_SHA256 = 0x08000109,
    PERMITTED_ALGO_ALL_CIPHER = 0x84C0FF00,
    PERMITTED_ALGO_ALL_AEAD = 0x8550FF00,
    PERMITTED_ALGO_OTH_KEK_CBC = 0x84404000,
    PERMITTED_ALGO_ECDH_HKDF_SHA256 = 0x09020109,
    PERMITTED_ALGO_ECDH_HKDF_SHA384 = 0x0902010A,
    PERMITTED_ALGO_HKDF_EXTRACT_SHA256 = 0x08000409,
    PERMITTED_ALGO_HKDF_EXTRACT_SHA384 = 0x0800040A,
    PERMITTED_ALGO_HKDF_EXTRACT_SHA_ANY = 0x080004FF,
    PERMITTED_ALGO_HKDF_EXPAND_SHA256 = 0x08000509,
    PERMITTED_ALGO_HKDF_EXPAND_SHA384 = 0x0800050A,
    PERMITTED_ALGO_TLS1_3_EARLY_SECRET_SHA256 = 0x8800D009,
    PERMITTED_ALGO_TLS1_3_EARLY_SECRET_SHA384 = 0x8800D00A,
    PERMITTED_ALGO_TLS1_3_HANDSHAKE_SECRET_SHA256 = 0x8800D109,
    PERMITTED_ALGO_TLS1_3_HANDSHAKE_SECRET_SHA384 = 0x8800D10A,
    PERMITTED_ALGO_TLS1_3_MASTER_SECRET_SHA256 = 0x8800D209,
    PERMITTED_ALGO_TLS1_3_MASTER_SECRET_SHA384 = 0x8800D20A,
    PERMITTED_ALGO_ATTEST_CMAC = 0x83C00200,
    PERMITTED_ALGO_ATTEST_ECDSA_SHA224 = 0x86000608,
    PERMITTED_ALGO_ATTEST_ECDSA_SHA256 = 0x86000609,
    PERMITTED_ALGO_ATTEST_ECDSA_SHA384 = 0x8600060A,
    PERMITTED_ALGO_ATTEST_ECDSA_SHA512 = 0x8600060B,
  }
  @Backing(type="int") @VintfStability
  enum KeyLifeCycle {
    LIFE_CYCLE_CURRENT = 0x0,
    LIFE_CYCLE_OPEN = 0x1,
    LIFE_CYCLE_CLOSED = 0x2,
    LIFE_CYCLE_CLOSED_LOCKED = 0x4,
  }
  @Backing(type="int") @VintfStability
  enum KeyOperationFlag {
    OPERATION_MONOTONIC = (0x1 << 5) /* 32 */,
    OPERATION_SYNC = (0x1 << 7) /* 128 */,
  }
  @Backing(type="int") @VintfStability
  enum CipherOperation {
    CIPHER_ONE_GO_FLAGS_DECRYPT = (0 << 0) /* 0 */,
    CIPHER_ONE_GO_FLAGS_ENCRYPT = (1 << 0) /* 1 */,
    CIPHER_ONE_GO_FLAGS_FULL_IV = (1 << 1) /* 2 */,
    CIPHER_ONE_GO_FLAGS_COUNTER_IV = (1 << 2) /* 4 */,
  }
  @Backing(type="int") @VintfStability
  enum SignatureMessageFlags {
    ELE_SIGN_FLAGS_DIGEST = 0,
    ELE_SIGN_FLAGS_MESSAGE = 1,
  }
  @Backing(type="int") @VintfStability
  enum MacOperation {
    MAC_ONE_GO_GENERATION = (1 << 0) /* 1 */,
    MAC_ONE_GO_VERIFICATION = (0 << 0) /* 0 */,
  }
  enum MacSizeBytes {
    MAC_LENGTH_SHA256 = 32,
    MAC_LENGTH_SHA384 = 48,
    MAC_LENGTH_CMAC = 16,
  }
  @VintfStability
  parcelable KeyAttribute {
    int type;
    int sizeBit;
    int lifeTime;
    int usage;
    int permitAlgo;
    int lifeCycle;
  }
}
