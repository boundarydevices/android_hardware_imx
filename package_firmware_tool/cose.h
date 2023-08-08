/*
 * Copyright (C) 2021 The Android Open Source Project
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

#pragma once

#include <stddef.h>
#include <optional>
#include <string_view>
#include <vector>

// From https://tools.ietf.org/html/rfc8152
constexpr int COSE_LABEL_ALG = 1;
constexpr int COSE_LABEL_KID = 4;
constexpr int COSE_LABEL_IV = 5;
constexpr int COSE_LABEL_KEY_KTY = 1;
constexpr int COSE_LABEL_KEY_ALG = 3;
constexpr int COSE_LABEL_KEY_SYMMETRIC_KEY = -1;
constexpr int COSE_TAG_ENCRYPT = 96;
constexpr int COSE_TAG_SIGN1 = 18;
constexpr int COSE_KEY_TYPE_SYMMETRIC = 4;

constexpr char COSE_CONTEXT_ENCRYPT[] = "Encrypt";
constexpr char COSE_CONTEXT_ENC_RECIPIENT[] = "Enc_Recipient";

// From "COSE Algorithms" registry
constexpr int COSE_ALG_A128GCM = 1;
constexpr int COSE_ALG_ECDSA_256 = -7;

// Trusty-specific COSE constants
constexpr int COSE_LABEL_TRUSTY = -65537;

constexpr size_t kEcdsaValueSize = 32;
constexpr size_t kEcdsaSignatureSize = 2 * kEcdsaValueSize;

constexpr size_t kAesGcmIvSize = 12;
constexpr size_t kAesGcmTagSize = 16;
constexpr size_t kAes128GcmKeySize = 16;

using CoseByteView = std::basic_string_view<uint8_t>;

using GetKeyFn =
        std::function<std::tuple<std::unique_ptr<uint8_t[]>, size_t>(uint8_t)>;
using DecryptFn = std::function<bool(CoseByteView key,
                                     CoseByteView nonce,
                                     uint8_t* encryptedData,
                                     size_t encryptedDataSize,
                                     CoseByteView additionalAuthenticatedData,
                                     size_t* outPlaintextSize)>;

/**
 * coseSetSilenceErrors() - Enable or disable the silencing of errors;
 * @value: New value of the flag, %true if errors should be silenced.
 *
 * Return: the old value of the flag.
 */
bool coseSetSilenceErrors(bool value);

/**
 * coseSignEcDsa() - Sign the given data using ECDSA and emit a COSE CBOR blob.
 * @key:
 *      DER-encoded private key.
 * @keyId:
 *      Key identifier, an unsigned 1-byte integer.
 * @data:
 *      Block of data to sign and optionally encode inside the COSE signature
 *      structure.
 * @protectedHeaders:
 *      Protected headers for the COSE structure. The function may add its own
 *      additional entries.
 * @unprotectedHeaders:
 *      Unprotected headers for the COSE structure. The function may add its
 *      own additional entries.
 * @detachContent:
 *      Whether to detach the data, i.e., not include @data in the returned
 *      ```COSE_Sign1``` structure.
 * @tagged:
 *      Whether to return the tagged ```COSE_Sign1_Tagged``` or the untagged
 *      ```COSE_Sign1``` structure.
 *
 * This function signs a given block of data with ECDSA-SHA256 and encodes both
 * the data and the signature using the COSE encoding from RFC 8152. The caller
 * may specify whether the data is included or detached from the returned
 * structure using the @detachContent paramenter, as well as additional
 * context-specific header values with the @protectedHeaders and
 * @unprotectedHeaders parameters.
 *
 * Return: A unique pointer to a &struct cppbor::Item containing the
 *         ```COSE_Sign1``` structure if the signing algorithm succeeds,
 *         or an uninitalized pointer otherwise.
 */
std::unique_ptr<cppbor::Item> coseSignEcDsa(const std::vector<uint8_t>& key,
                                            uint8_t keyId,
                                            const std::vector<uint8_t>& data,
                                            cppbor::Map protectedHeaders,
                                            cppbor::Map unprotectedHeaders,
                                            bool detachContent,
                                            bool tagged);

/**
 * coseIsSigned() - Check if a block of bytes is a COSE signature emitted
 *                  by coseSignEcDsa().
 * @data:            Input data.
 * @signatureLength: If not NULL, output argument where the total length
 *                   of the signature structure will be stored.
 *
 * This function checks if the given data is a COSE signature structure
 * emitted by coseSignEcDsa(), and returns the size of the signature if needed.
 *
 * Return: %true if the signature structure is valid, %false otherwise.
 */
bool coseIsSigned(CoseByteView data, size_t* signatureLength);

/**
 * coseCheckEcDsaSignature() - Check if a given COSE signature structure is
 *                             valid.
 * @signatureCoseSign1: Input COSE signature structure.
 * @detachedContent:    Additional data to include in the signature.
 *                      Corresponds to the @detachedContent parameter passed to
 *                      coseSignEcDsa().
 * @publicKey:          Public key in DER encoding.
 *
 * Returns: %true if the signature verification passes, %false otherwise.
 */
bool coseCheckEcDsaSignature(const std::vector<uint8_t>& signatureCoseSign1,
                             const std::vector<uint8_t>& detachedContent,
                             const std::vector<uint8_t>& publicKey);

/**
 * strictCheckEcDsaSignature() - Check a given COSE signature in strict mode.
 * @packageStart:       Pointer to the start of the signed input package.
 * @packageSize:        Size of the signed input package.
 * @keyFn:              Function to call with a key id that returns the public
 *                      key for that id.
 * @outPackageStart:    If not NULL, output argument where the start of the
 *                      payload will be stored.
 * @outPackageSize:     If not NULL, output argument where the size of the
 *                      payload will be stored.
 *
 * This function performs a strict verification of the COSE signature of a
 * package. Instead of parsing the COSE structure, the function compares the
 * raw bytes against a set of exact patterns, and fails if the bytes do not
 * match. The actual signature and payload are also assumed to start at fixed
 * offsets from @packageStart.
 *
 * Returns: %true if the signature verification passes, %false otherwise.
 */
bool strictCheckEcDsaSignature(const uint8_t* packageStart,
                               size_t packageSize,
                               GetKeyFn keyFn,
                               const uint8_t** outPackageStart,
                               size_t* outPackageSize);

/**
 * coseEncryptAes128GcmKeyWrap() - Encrypt a block of data using AES-128-GCM
 *                                 and a randomly-generated wrapped CEK.
 * @key:
 *      Key encryption key (KEK), 16 bytes in size.
 * @keyId:
 *      Key identifier for the KEK, an unsigned 1-byte integer.
 * @data:
 *      Input data to encrypt.
 * @externalAad:
 *      Additional authentication data to pass to AES-GCM.
 * @protectedHeaders:
 *      Protected headers for the COSE structure. The function may add its own
 *      additional entries.
 * @unprotectedHeaders:
 *      Unprotected headers for the COSE structure. The function may add its
 *      own additional entries.
 * @tagged:
 *      Whether to return the tagged ```COSE_Encrypt_Tagged``` or the untagged
 *      ```COSE_Encrypt``` structure.
 *
 * This function generates a random key content encryption key (CEK) and wraps
 * it using AES-128-GCM, then encrypts a given block of data with AES-128-GCM
 * with the wrapped CEK and encodes both the data and CEK using the COSE
 * encoding from RFC 8152.
 *
 * The caller may specify additional context-specific header values with the
 * @protectedHeaders and @unprotectedHeaders parameters.
 *
 * Return: A unique pointer to a &struct cppbor::Item containing the
 *         ```COSE_Encrypt``` structure if the encryption succeeds,
 *         or an default-initialized &std::unique_ptr otherwise.
 */
std::unique_ptr<cppbor::Item> coseEncryptAes128GcmKeyWrap(
        const std::vector<uint8_t>& key,
        uint8_t keyId,
        const std::vector<uint8_t>& data,
        const std::vector<uint8_t>& externalAad,
        cppbor::Map protectedHeaders,
        cppbor::Map unprotectedHeaders,
        bool tagged);

/**
 * coseDecryptAes128GcmKeyWrapInPlace() - Decrypt a block of data containing a
 *                                        wrapped key using AES-128-GCM.
 * @item:               CBOR item containing a ```COSE_Encrypt``` structure.
 * @keyFn:              Function to call with a key id that returns the key
 *                      encryption key (KEK) for that id.
 * @externalAad:        Additional authentication data to pass to AES-GCM.
 * @checkTag:           Whether to check the CBOR semantic tag of @item.
 * @outPackageStart:    The output argument where the start of the
 *                      payload will be stored. Must not be %NULL.
 * @outPackageSize:     The output argument where the size of the
 *                      payload will be stored. Must not be %NULL.
 *
 * This function decrypts a ciphertext encrypted with AES-128-GCM and encoded
 * in a ```COSE_Encrypt0_Tagged``` structure. The function performs in-place
 * decryption and overwrites the ciphertext with the plaintext, and returns
 * the pointer and size of the plaintext in @outPackageStart and
 * @outPackageSize, respectively.
 *
 * Returns: %true if the decryption succeeds, %false otherwise.
 */
bool coseDecryptAes128GcmKeyWrapInPlace(
        const std::unique_ptr<cppbor::Item>& item,
        GetKeyFn keyFn,
        const std::vector<uint8_t>& externalAad,
        bool checkTag,
        const uint8_t** outPackageStart,
        size_t* outPackageSize,
        DecryptFn decryptFn = DecryptFn());
