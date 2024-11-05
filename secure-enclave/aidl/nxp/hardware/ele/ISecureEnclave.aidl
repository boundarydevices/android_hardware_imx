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

package nxp.hardware.ele;
/**
 */
@VintfStability
interface ISecureEnclave {
	/**
	 * Supported key types.
	 *
	 * Refer to the EdgeLock Secure Enclave documentation
	 * for more details.
	 */
	@VintfStability
	@Backing(type="int")
	enum KeyType {
		KEY_TYPE_HMAC                   = 0x1100,
		KEY_TYPE_DERIVE                 = 0x1200,
		KEY_TYPE_AES                    = 0x2400,
		KEY_TYPE_SM4                    = 0x2405,
		PUBKEY_TYPE_RSA                 = 0x4001,
		PUBKEY_TYPE_ECC_BP_R1           = 0x4130,
		PUBKEY_TYPE_ECC_NIST            = 0x4112,
		KEY_TYPE_RSA                    = 0x7001,
		KEY_TYPE_ECC_BP_R1              = 0x7130,
		KEY_TYPE_ECC_NIST               = 0x7112,
		KEY_TYPE_OEM_IMPORT_MK_SK       = 0x9200,
		PUBKEY_TYPE_ECC_BP_T1           = 0xC180,
	}

	/**
	 * Supported key sizes in bits.
	 *
	 * Refer to the EdgeLock Secure Enclave documentation
	 * for more details.
	 */
	@VintfStability
	@Backing(type="int")
	enum KeySizeBit {
		KEY_SIZE_HMAC_224           = 224,
		KEY_SIZE_HMAC_256           = 256,
		KEY_SIZE_HMAC_384           = 384,
		KEY_SIZE_HMAC_512           = 512,
		KEY_SIZE_AES_128            = 128,
		KEY_SIZE_AES_192            = 192,
		KEY_SIZE_AES_256            = 256,
		KEY_SIZE_SM4_128            = 128,
		KEY_SIZE_RSA_2048           = 2048,
		KEY_SIZE_RSA_3072           = 3072,
		KEY_SIZE_RSA_4096           = 4096,
		KEY_SIZE_ECC_BP_R1_224      = 224,
		KEY_SIZE_ECC_BP_R1_256      = 256,
		KEY_SIZE_ECC_BP_R1_320      = 320,
		KEY_SIZE_ECC_BP_R1_384      = 384,
		KEY_SIZE_ECC_BP_R1_512      = 512,
		KEY_SIZE_ECC_NIST_224       = 224,
		KEY_SIZE_ECC_NIST_256       = 256,
		KEY_SIZE_ECC_NIST_384       = 384,
		KEY_SIZE_ECC_NIST_521       = 521,
		KEY_SIZE_ECC_BP_T1_224      = 224,
		KEY_SIZE_ECC_BP_T1_256      = 256,
		KEY_SIZE_ECC_BP_T1_320      = 320,
		KEY_SIZE_ECC_BP_T1_384      = 384,
		KEY_SIZE_OEM_IMPORT_MK_SK_128 = 128,
		KEY_SIZE_OEM_IMPORT_MK_SK_192 = 192,
		KEY_SIZE_OEM_IMPORT_MK_SK_256 = 256,
	}

	/**
	 * Supported key life time.
	 *
	 * Refer to the EdgeLock Secure Enclave documentation
	 * for more details.
	 */
	@VintfStability
	@Backing(type="int")
	enum KeyLifeTime {
		STD_VOLATILE		= 0x0,
		STD_PERSISTENT		= 0x1,
		STD_PERMANENT		= 0xff,
		ELE_IMPORT_VOLATILE	= 0xc0020000,
		ELE_IMPORT_PERSISTENT	= 0xc0020001,
		ELE_IMPORT_PERMANENT	= 0xc00200ff,
	}

	/**
	 * Supported key usage.
	 *
	 * Refer to the EdgeLock Secure Enclave documentation
	 * for more details.
	 */
	@VintfStability
	@Backing(type="int")
	enum KeyUsage {
		KEY_USAGE_ENCRYPT	= (0x1 << 8),
		KEY_USAGE_DECRYPT	= (0x1 << 9),
		KEY_USAGE_SIGN_MSG	= (0x1 << 10),
		KEY_USAGE_VERIFY_MSG	= (0x1 << 11),
		KEY_USAGE_SIGN_HASH	= (0x1 << 12),
		KEY_USAGE_VERIFY_HASH	= (0x1 << 13),
		KEY_USAGE_DERIVE	= (0x1 << 14),
	}

	/**
	 * Supported key algorithm.
	 *
	 * Refer to the EdgeLock Secure Enclave documentation
	 * for more details.
	 */
	@VintfStability
	@Backing(type="int")
	enum PermitAlgorithms {
		PERMITTED_ALGO_SHA224	        = 0x02000008,
		PERMITTED_ALGO_SHA256	        = 0x02000009,
		PERMITTED_ALGO_SHA384	        = 0x0200000a,
		PERMITTED_ALGO_SHA512	        = 0x0200000b,
		PERMITTED_ALGO_SM3	        = 0x02000014,
		PERMITTED_ALGO_HMAC_SHA256	= 0x03800009,
		PERMITTED_ALGO_HMAC_SHA384	= 0x0380000a,
		PERMITTED_ALGO_CMAC		= 0x03c00200,
		PERMITTED_ALGO_CTR		= 0x04c01000,
		PERMITTED_ALGO_CFB		= 0x04c01100,
		PERMITTED_ALGO_OFB		= 0x04c01200,
		PERMITTED_ALGO_ECB_NO_PADDING	= 0x04404400,
		PERMITTED_ALGO_CBC_NO_PADDING	= 0x04404000,
		PERMITTED_ALGO_CCM		= 0x05500100,
		PERMITTED_ALGO_GCM		= 0x05500200,
		PERMITTED_ALGO_CHACHA20_POLY1305 = 0x05100500,
		PERMITTED_ALGO_RSA_PKCS1_V15_SHA224 = 0x06000208,
		PERMITTED_ALGO_RSA_PKCS1_V15_SHA256 = 0x06000209,
		PERMITTED_ALGO_RSA_PKCS1_V15_SHA384 = 0x0600020a,
		PERMITTED_ALGO_RSA_PKCS1_V15_SHA512 = 0x0600020b,
		PERMITTED_ALGO_RSA_PKCS1_PSS_MGF1_SHA224 = 0x06000308,
		PERMITTED_ALGO_RSA_PKCS1_PSS_MGF1_SHA256 = 0x06000309,
		PERMITTED_ALGO_RSA_PKCS1_PSS_MGF1_SHA384 = 0x0600030a,
		PERMITTED_ALGO_RSA_PKCS1_PSS_MGF1_SHA512 = 0x0600030b,
		PERMITTED_ALGO_ECDSA_SHA224	= 0x06000608,
		PERMITTED_ALGO_ECDSA_SHA256	= 0x06000609,
		PERMITTED_ALGO_ECDSA_SHA384	= 0x0600060a,
		PERMITTED_ALGO_ECDSA_SHA512	= 0x0600060b,
		PERMITTED_ALGO_HMAC_KDF_SHA256	= 0x08000109,
		PERMITTED_ALGO_ALL_CIPHER	= 0x84C0FF00,
		PERMITTED_ALGO_ALL_AEAD		= 0x8550FF00,
		PERMITTED_ALGO_OTH_KEK_CBC	= 0x84404000,
		PERMITTED_ALGO_ECDH_HKDF_SHA256 = 0x09020109,
		PERMITTED_ALGO_ECDH_HKDF_SHA384 = 0x0902010A,
		PERMITTED_ALGO_HKDF_EXTRACT_SHA256  = 0x08000409,
		PERMITTED_ALGO_HKDF_EXTRACT_SHA384  = 0x0800040A,
		PERMITTED_ALGO_HKDF_EXTRACT_SHA_ANY = 0x080004FF,
		PERMITTED_ALGO_HKDF_EXPAND_SHA256   = 0x08000509,
		PERMITTED_ALGO_HKDF_EXPAND_SHA384   = 0x0800050A,
		PERMITTED_ALGO_TLS1_3_EARLY_SECRET_SHA256     = 0x8800D009,
		PERMITTED_ALGO_TLS1_3_EARLY_SECRET_SHA384     = 0x8800D00A,
		PERMITTED_ALGO_TLS1_3_HANDSHAKE_SECRET_SHA256 = 0x8800D109,
		PERMITTED_ALGO_TLS1_3_HANDSHAKE_SECRET_SHA384 = 0x8800D10A,
		PERMITTED_ALGO_TLS1_3_MASTER_SECRET_SHA256    = 0x8800D209,
		PERMITTED_ALGO_TLS1_3_MASTER_SECRET_SHA384    = 0x8800D20A,
		PERMITTED_ALGO_ATTEST_CMAC         = 0x83C00200,
		PERMITTED_ALGO_ATTEST_ECDSA_SHA224 = 0x86000608,
		PERMITTED_ALGO_ATTEST_ECDSA_SHA256 = 0x86000609,
		PERMITTED_ALGO_ATTEST_ECDSA_SHA384 = 0x8600060A,
		PERMITTED_ALGO_ATTEST_ECDSA_SHA512 = 0x8600060B,
	}

	/**
	 * Supported key lifecycle:
	 * LIFE_CYCLE_CURRENT: Key is usable in current device lifecycle (open or close).
	 * LIFE_CYCLE_OPEN: Key is only usable in device open lifecycle.
	 * LIFE_CYCLE_CLOSED: Key is only usable in device closed lifecycle.
	 * LIFE_CYCLE_CLOSED_LOCKED: Key is only usable in device closed and locked lifecycle.
	 *
	 * Refer to the EdgeLock Secure Enclave documentation for more details.
	 */
	@VintfStability
	@Backing(type="int")
	enum KeyLifeCycle {
		LIFE_CYCLE_CURRENT		= 0x0,
		LIFE_CYCLE_OPEN			= 0x1,
		LIFE_CYCLE_CLOSED		= 0x2,
		LIFE_CYCLE_CLOSED_LOCKED	= 0x4,
	}

	/**
	 * Supported key operation flag:
	 * OPERATION_MONOTONIC: When used in conjunction with OPERATION_SYNC, the request
	 *                      would cause increasement on internal monotonic counter. It
	 *                      can be used to prevent the rollback attack. However, as the
	 *                      number of monotonic counter operation is limited, user should
	 *                      carefully use this operation.
	 *
	 * OPERATION_SYNC: When a SYNC is invoked, all the keys present in the key group are
	 *                 pushed to the NVM.
	 *
	 * Refer to the EdgeLock Secure Enclave documentation
	 * for more details.
	 */
	@VintfStability
	@Backing(type="int")
	enum KeyOperationFlag {
		OPERATION_MONOTONIC	= (0x1 << 5),
		OPERATION_SYNC		= (0x1 << 7),
	}

	/**
	 * Supported flags when doing cipher operation:
	 * CIPHER_ONE_GO_FLAGS_DECRYPT: Decryption.
	 * CIPHER_ONE_GO_FLAGS_ENCRYPT: Encryption.
	 * CIPHER_ONE_GO_FLAGS_FULL_IV: Use full IV generated internally. This is only valid
	 *                              for authenticated encryption and decryption.
	 * CIPHER_ONE_GO_FLAGS_COUNTER_IV: Use counter IV generation. The caller must supply 4
	 *                                 bytes of IV data, the rest is generated internally
	 *                                 using a counter. This is only valid for authenticated
	 *                                 encryption and decryption.
	 *
	 * Refer to the EdgeLock Secure Enclave documentation for more details.
	 */
	@VintfStability
	@Backing(type="int")
	enum CipherOperation {
		CIPHER_ONE_GO_FLAGS_DECRYPT    = (0 << 0),
		CIPHER_ONE_GO_FLAGS_ENCRYPT    = (1 << 0),
		CIPHER_ONE_GO_FLAGS_FULL_IV    = (1 << 1),
		CIPHER_ONE_GO_FLAGS_COUNTER_IV = (1 << 2)
	}

	/**
	 * Supported signature message type:
	 * ELE_SIGN_FLAGS_DIGEST: Input is the message digest.
	 * ELE_SIGN_FLAGS_MESSAGE: Input is the actual message.
	 *
	 * Refer to the EdgeLock Secure Enclave documentation
	 * for more details.
	 */
	@VintfStability
	@Backing(type="int")
	enum SignatureMessageFlags {
		ELE_SIGN_FLAGS_DIGEST 	= (0),
		ELE_SIGN_FLAGS_MESSAGE	= (1),
	}

	/**
	 * Supported MAC operation type:
	 * MAC_ONE_GO_GENERATION: MAC generation.
	 * MAC_ONE_GO_VERIFICATION: MAC verification.
	 *
	 * Refer to the EdgeLock Secure Enclave documentation
	 * for more details.
	 */
	@VintfStability
	@Backing(type="int")
	enum MacOperation {
		MAC_ONE_GO_GENERATION = (1 << 0),
		MAC_ONE_GO_VERIFICATION = (0 << 0)
	}

	/**
	 * Supported MAC size in bytes.
	 * MAC_LENGTH_SHA256: MAC size for default HMAC SHA256 algorithm.
	 * MAC_LENGTH_SHA384: MAC size for default HMAC SHA384 algorithm.
	 * MAC_LENGTH_CMAC: MAC size for default AES CMAC algorithm.
	 *
	 * ELE also supports truncated length MAC algorithms, refer to the
	 * EdgeLock Secure Enclave documentation for more details.
	 */
	enum MacSizeBytes {
		MAC_LENGTH_SHA256 = 32,
		MAC_LENGTH_SHA384 = 48,
		MAC_LENGTH_CMAC = 16,
	}

	/**
	 * Key attribute type which caller can extract from ELE:
	 * type: The key type.
	 * sizeBit: Key size in bits.
	 * lifeTime: The key lifetime.
	 * usage: The key usage.
	 * permitAlgo: The key permitted algorithm.
	 * lifeCycle: The key lifecycle.
	 */
	@VintfStability
	parcelable KeyAttribute {
		int type;
		int sizeBit;
		int lifeTime;
		int usage;
		int permitAlgo;
		int lifeCycle;
	}

	/**
	 * Generate a new key based on the passed arguments.
	 *
	 * @param keyId The wanted key identifier. Pass 0x00000000 to let the ELE choose
	 *              suitable key identifier.
	 *
	 * @param pubKeySize Size in bytes of the public key generated. It must be 0 for
	 *                   symmetric keys.
	 *
	 * @param keyGroup Indicates the generated key group, it must be a value in the
	 *                 range [0, 99]. Note, persistent and volatile keys cannot be
	 *                 store in the same key group.
	 *
	 * @param keyType The type of the key which the caller want to generate. See 'KeyType'
	 *                for the supported symmetric and asymmetric key types.
	 *
	 * @param sizeBit The key size in bits.
	 *
	 * @param lifeTime The key lifetime. See 'KeyLifeTime' for possible values.
	 *
	 * @param usage The usage of the key. See 'KeyUsage' for possible values. As these
	 *              values are bitmap values, several usage could be set at the same time.
	 *
	 * @param permitAlgo Permitted algorithm of the key. See 'PermitAlgorithms' for possible
	 *                   values.
	 *
	 * @param lifeCycle Key lifecycle. See 'KeyLifeCycle' for possible values.
	 *
	 * @param flags Bit field indicating the requested operations. See 'KeyOperationFlag'
	 *              for possible values.
	 *
	 * @param pubKey This is output parameter. This byte array will hold the output public
	 *               key for asymmetric keypair, make sure the buffer size is equal to or
	 *               bigger than required.
	 *               Note it can't be null even for symmetric keys.
	 *
	 * @return
	 * On success, it returns the actual key identifier. On failure, 0 was returned for
	 * invalid key identifier. Exception or error status will also be returned, so caller
	 * will have to catch the exception or handle the error status correctly.
	 */
	int eleGenerateKey(in int keyId, in int pubKeySize,
				in int keyGroup, in int keyType,
				in int sizeBit, in int lifeTime,
				in int usage, in int permitAlgo,
				in int lifeCycle, in int flags,
				out byte[] pubKey);

	/**
	 * Delete the key which was generated before.
	 *
	 * @param keyId key identifier of the key which would be deleted.
	 *
	 * @param flags Bit field indicating the requested operations. See 'KeyOperationFlag'
	 *              for possible values.
	 *
	 * Exception or error status will be returned for errors, so caller will have to catch
	 * the exception or handle the error status correctly.
	 */
	void eleDeleteKey(in int keyId, in int flags);

	/**
	 * Retrive the key attribute which was generated before.
	 *
	 * @param keyId key identifier of the key.
	 *
	 * @param keyAttr This is output parameter. It contains the key attributes on success.
	 *
	 * Exception or error status will be returned for errors, so caller will have to catch
	 * the exception or handle the error status correctly.
	 */
	void eleGetKeyAttr(in int keyId, out KeyAttribute keyAttr);

	/**
	 * Perform a one-shot cipher operation.
	 *
	 * @param keyId key identifier of the key which would be used to do the cipher.
	 *
	 * @param iv Initialization vector. The length of the iv must match exactly with the
	 *           actual size. It must be 0 for algorithms not using an IV.
	 *
	 * @param flags Bit field indicating the requested operations. See 'CipherOperation'
	 *              for possible values.
	 *
	 * @param algo Cipher algorithm to use. It can be one of ECB NO PADDING, CBC NO PADDING,
	 *             CTR, CFB, OFB or all cipher, see 'PermitAlgorithms'.
	 *
	 * @param input The input text. In case of "encrypt" mode, the input is the plaintext.
	 *              In case of "decrypt" mode, the input is ciphertext.
	 *              The length of the input buffer must match exactly with the actual input
	 *              data.
	 *
	 * @param output This is output parameter. This byte array will hold the output data of
	 *               the cipher operation. It's ciphertext in "encrypt" mode and plaintext in
	 *               "decrypt" mode.
	 *               Make sure the buffer size is equal to or bigger than required.
	 *
	 * @return
	 * On success, it returns the actual data length of the cipher operation. On failure, 0 or
	 * the required buffer size would be returned.
	 * Exception or error status will be returned for errors, so caller will have to catch
	 * the exception or handle the error status correctly.
	 */
	int eleCipherOperation(in int keyId, in byte[] iv,
				in int flags, in int algo,
				in byte[] input,
				out byte[] output);

	/**
	 * Perform a one-shot authenticated encryption and decryption.
	 *
	 * @param keyId key identifier of the key which would be used to do the cipher.
	 *
	 * @param iv Initialization vector. The length of the iv must match exactly with the
	 *           actual size. It must be 0 for algorithms not using an IV.
	 *
	 * @param flags Bit field indicating the requested operations. See 'CipherOperation'
	 *              for possible values.
	 *
	 * @param algo Cipher algorithm to use. It can be one of CCM, GCM and all AEAD (CCM, GCM),
	 *             see 'PermitAlgorithms'.
	 *
	 *
	 * @param aad Additional authenticated data. The length of the aad must match exactly with
	 *            the actual size.
	 *
	 * @param input The input text. In case of "encrypt" mode, the input is the plaintext.
	 *              In case of "decrypt" mode, the input is ciphertext and the 16 bytes TAG.
	 *              The length of the input buffer must match exactly with the actual input
	 *              data.
	 *              Refer to the EdgeLock Secure Enclave documentation for more details.
	 *
	 * @param output This is output parameter. This byte array will hold the output data of
	 *               the operation.
	 *               In case of "encrypt" mode, it's ciphertext and 16 bytes TAG. If the IV
	 *               was generated partially or entirely by the ELE, the output will also
	 *               include 12 bytes IV.
	 *               In case of "decrypt" mode, it's plaintext.
	 *               Refer to the EdgeLock Secure Enclave documentation for more details.
	 *               Make sure the buffer size is equal to or bigger than required.
	 *
	 * @return
	 * On success, it returns the actual data length of the AEAD operation. On failure, 0 or
	 * the required buffer size would be returned.
	 * Exception or error status will be returned for errors, so caller will have to catch
	 * the exception or handle the error status correctly.
	 */
	int eleCipherAEOperation(in int keyId, in byte[] iv,
					in int flags, in int algo,
					in byte[] aad,
					in byte [] input,
					out byte[] output);

	/**
	 * Generate the signature with a asymmetric keypair.
	 *
	 * @param keyId key identifier of the key which would be used.
	 *
	 * @param message The message would be signed. The length of the message must match
	 *                exactly with the actual size.
	 *
	 * @param signature The generated signature.
	 *                  Make sure the buffer size is equal to or bigger than required.
	 *
	 * @param flags Bit field indicating the requested operations, see "SignatureMessageFlags"
	 *              for possible values.
	 *
	 * @param signScheme Signature scheme to use. See "PermitAlgorithms" for possible
	 *                   signature algorithms.
	 *
	 * @param saltLen Salt length in bytes, if applicable (0 otherwise).
	 *
	 * @return
	 * On success, it returns the actual data length of the signature. On failure, 0 or
	 * the required buffer size would be returned.
	 * Exception or error status will be returned for errors, so caller will have to catch
	 * the exception or handle the error status correctly.
	 */
	int eleSignGenerate(in int keyId, in byte[] message,
				out byte[] signature,
				in int flags, in int signScheme,
				in int saltLen);

	/**
	 * Verify the signature.
	 *
	 * @param key public key used to verify the signature. The length of the key must
	 *            match exactly with the actual size.
	 *
	 * @param message The message to be signed. The length of the message must match
	 *                exactly with the actual size.
	 *
	 * @param signature The signature to be verified. The length of the signature must
	 *                  match exactly with the actual size.
	 *
	 * @param keySecuritySize Keypair security size in bits.
	 *
	 * @param keyType public key type.
	 *
	 * @param flags Bit field indicating the requested operations, see "SignatureMessageFlags"
	 *              for possible values.
	 *
	 * @param signScheme Signature scheme to use. See "PermitAlgorithms" for possible
	 *                   signature algorithms.
	 *
	 * @param saltLen Salt length in bytes, if applicable (0 otherwise).
	 *
	 * Exception or error status will be returned for errors, so caller will have to catch
	 * the exception or handle the error status correctly.
	 */
	void eleSignVerify(in byte[] key,
				in byte[] message,
				in byte[] signature,
				in int keySecuritySize,
				in int keyType,
				in int flags,
				in int signScheme,
				in int saltLength);

	/**
	 * Generate or verify MAC (Message authentication Code).
	 *
	 * @param keyId Identifier of the key to be used to generate or verify the MAC.
	 *
	 * @param payload The payload used to generate or verify the mac. The length of the
	 *                payload must match exactly with the actual size.
	 *
	 * @param mac The buffer to contain the MAC for generation or verification process.
	 *            The length of the buffer must be larger than or equal to the actual MAC.
	 *
	 * @param macSize The size in bytes of the expected MAC. Refer to the EdgeLock Secure
	 *                Enclave documentation for the possible size for different MAC algorithms.
	 *
	 * @param flag Bit field indicating the requested operations (MAC generation or verification).
	 *
	 * @param algorithm Algorithms to use. Refer to the EdgeLock Secure Enclave documentation for
	 *                  more details.
	 *
	 * @return
	 * On success, it returns the actual data length of the MAC. On failure, 0 would be returned.
	 * Exception or error status will be returned for errors, so caller will have to catch
	 * the exception or handle the error status correctly.
	 */
	int eleMacOperation(in int keyId,
				in byte[] payload,
				inout byte[] mac,
				in int macSize,
				in int flag,
				in int algorithm);
}
