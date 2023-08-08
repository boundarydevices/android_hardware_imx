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

#define TLOG_TAG "firmwareloader-cose"

#include <assert.h>
#include <cppbor.h>
#include <cppbor_parse.h>
#include <inttypes.h>
#include <openssl/bn.h>
#include <openssl/crypto.h>
#include <openssl/ec.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <stddef.h>
#include <cutils/log.h>
#include <array>
#include <optional>
#include <vector>

#include "cose.h"

#ifdef __COSE_HOST__
#define COSE_PRINT_ERROR(...)         \
    if (!gSilenceErrors) {            \
        ALOGE(__VA_ARGS__); \
    }
#else
#define COSE_PRINT_ERROR(...) \
    if (!gSilenceErrors) {    \
        ALOGE(__VA_ARGS__);   \
    }
#endif

static bool gSilenceErrors = false;

bool coseSetSilenceErrors(bool value) {
    bool old = gSilenceErrors;
    gSilenceErrors = value;
    return old;
}

using BIGNUM_Ptr = std::unique_ptr<BIGNUM, std::function<void(BIGNUM*)>>;
using EC_KEY_Ptr = std::unique_ptr<EC_KEY, std::function<void(EC_KEY*)>>;
using ECDSA_SIG_Ptr =
        std::unique_ptr<ECDSA_SIG, std::function<void(ECDSA_SIG*)>>;
using EVP_CIPHER_CTX_Ptr =
        std::unique_ptr<EVP_CIPHER_CTX, std::function<void(EVP_CIPHER_CTX*)>>;

using SHA256Digest = std::array<uint8_t, SHA256_DIGEST_LENGTH>;

static std::vector<uint8_t> coseBuildToBeSigned(
        const std::vector<uint8_t>& encodedProtectedHeaders,
        const std::vector<uint8_t>& data) {
    cppbor::Array sigStructure;
    sigStructure.add("Signature1");
    sigStructure.add(encodedProtectedHeaders);

    // We currently don't support Externally Supplied Data (RFC 8152
    // section 4.3) so external_aad is the empty bstr
    std::vector<uint8_t> emptyExternalAad;
    sigStructure.add(emptyExternalAad);
    sigStructure.add(data);
    return sigStructure.encode();
}

static std::vector<uint8_t> coseEncodeHeaders(
        const cppbor::Map& protectedHeaders) {
    if (protectedHeaders.size() == 0) {
        cppbor::Bstr emptyBstr(std::vector<uint8_t>({}));
        return emptyBstr.encode();
    }
    return protectedHeaders.encode();
}

static std::optional<std::vector<uint8_t>> getRandom(size_t numBytes) {
    std::vector<uint8_t> output;
    output.resize(numBytes);
    if (RAND_bytes(output.data(), numBytes) != 1) {
        COSE_PRINT_ERROR("RAND_bytes: failed getting %zu random\n", numBytes);
        return {};
    }
    return output;
}

static SHA256Digest sha256(const std::vector<uint8_t>& data) {
    SHA256Digest ret;
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, data.data(), data.size());
    SHA256_Final((unsigned char*)ret.data(), &ctx);
    return ret;
}

static std::optional<std::vector<uint8_t>> signEcDsaDigest(
        const std::vector<uint8_t>& key,
        const SHA256Digest& dataDigest) {
    const unsigned char* k = key.data();
    auto ecKey =
            EC_KEY_Ptr(d2i_ECPrivateKey(nullptr, &k, key.size()), EC_KEY_free);
    if (!ecKey) {
        COSE_PRINT_ERROR("Error parsing EC private key\n");
        return {};
    }

    auto sig = ECDSA_SIG_Ptr(
            ECDSA_do_sign(dataDigest.data(), dataDigest.size(), ecKey.get()),
            ECDSA_SIG_free);
    if (!sig) {
        COSE_PRINT_ERROR("Error signing digest:\n");
        return {};
    }
    size_t len = i2d_ECDSA_SIG(sig.get(), nullptr);
    std::vector<uint8_t> signature;
    signature.resize(len);
    unsigned char* p = (unsigned char*)signature.data();
    i2d_ECDSA_SIG(sig.get(), &p);
    return signature;
}

static std::optional<std::vector<uint8_t>> signEcDsa(
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& data) {
    return signEcDsaDigest(key, sha256(data));
}

static bool ecdsaSignatureDerToCose(
        const std::vector<uint8_t>& ecdsaDerSignature,
        std::vector<uint8_t>& ecdsaCoseSignature) {
    const unsigned char* p = ecdsaDerSignature.data();
    auto sig =
            ECDSA_SIG_Ptr(d2i_ECDSA_SIG(nullptr, &p, ecdsaDerSignature.size()),
                          ECDSA_SIG_free);
    if (!sig) {
        COSE_PRINT_ERROR("Error decoding DER signature\n");
        return false;
    }

    const BIGNUM* rBn;
    const BIGNUM* sBn;
    ECDSA_SIG_get0(sig.get(), &rBn, &sBn);

    /*
     * Older versions of OpenSSL also do not have BN_bn2binpad,
     * so we need to use BN_bn2bin with the correct offsets.
     * Each of the output values is a 32-byte big-endian number,
     * while the inputs are BIGNUMs stored in host format.
     * We can insert the padding ourselves by zeroing the output array,
     * then placing the output of BN_bn2bin so its end aligns
     * with the end of the 32-byte big-endian number.
     */
    auto rBnSize = BN_num_bytes(rBn);
    if (rBnSize < 0 || static_cast<size_t>(rBnSize) > kEcdsaValueSize) {
        COSE_PRINT_ERROR("Invalid ECDSA r value size (%d)\n", rBnSize);
        return false;
    }
    auto sBnSize = BN_num_bytes(sBn);
    if (sBnSize < 0 || static_cast<size_t>(sBnSize) > kEcdsaValueSize) {
        COSE_PRINT_ERROR("Invalid ECDSA s value size (%d)\n", sBnSize);
        return false;
    }

    ecdsaCoseSignature.clear();
    ecdsaCoseSignature.resize(kEcdsaSignatureSize, 0);
    if (BN_bn2bin(rBn, ecdsaCoseSignature.data() + kEcdsaValueSize - rBnSize) !=
        rBnSize) {
        COSE_PRINT_ERROR("Error encoding r\n");
        return false;
    }
    if (BN_bn2bin(sBn, ecdsaCoseSignature.data() + kEcdsaSignatureSize -
                               sBnSize) != sBnSize) {
        COSE_PRINT_ERROR("Error encoding s\n");
        return false;
    }
    return true;
}

std::unique_ptr<cppbor::Item> coseSignEcDsa(const std::vector<uint8_t>& key,
                                            uint8_t keyId,
                                            const std::vector<uint8_t>& data,
                                            cppbor::Map protectedHeaders,
                                            cppbor::Map unprotectedHeaders,
                                            bool detachContent,
                                            bool tagged) {
    protectedHeaders.add(COSE_LABEL_ALG, COSE_ALG_ECDSA_256);
    unprotectedHeaders.add(COSE_LABEL_KID, cppbor::Bstr(std::vector(1, keyId)));

    // Canonicalize the headers to ensure a predictable layout
    protectedHeaders.canonicalize(true);
    unprotectedHeaders.canonicalize(true);

    std::vector<uint8_t> encodedProtectedHeaders =
            coseEncodeHeaders(protectedHeaders);
    std::vector<uint8_t> toBeSigned =
            coseBuildToBeSigned(encodedProtectedHeaders, data);

    std::optional<std::vector<uint8_t>> derSignature =
            signEcDsa(key, toBeSigned);
    if (!derSignature) {
        COSE_PRINT_ERROR("Error signing toBeSigned data\n");
        return {};
    }
    std::vector<uint8_t> coseSignature;
    if (!ecdsaSignatureDerToCose(derSignature.value(), coseSignature)) {
        COSE_PRINT_ERROR(
                "Error converting ECDSA signature from DER to COSE format\n");
        return {};
    }

    auto coseSign1 = std::make_unique<cppbor::Array>();
    coseSign1->add(encodedProtectedHeaders);
    coseSign1->add(std::move(unprotectedHeaders));
    if (detachContent) {
        cppbor::Null nullValue;
        coseSign1->add(std::move(nullValue));
    } else {
        coseSign1->add(data);
    }
    coseSign1->add(coseSignature);

    if (tagged) {
        return std::make_unique<cppbor::SemanticTag>(COSE_TAG_SIGN1,
                                                     coseSign1.release());
    } else {
        return coseSign1;
    }
}

bool coseIsSigned(CoseByteView data, size_t* signatureLength) {
    auto [item, pos, err] = cppbor::parse(data.data(), data.size());
    if (item) {
        for (size_t i = 0; i < item->semanticTagCount(); i++) {
            if (item->semanticTag(i) == COSE_TAG_SIGN1) {
                if (signatureLength) {
                    *signatureLength = std::distance(data.data(), pos);
                }
                return true;
            }
        }
    }
    return false;
}

static bool checkEcDsaSignature(const SHA256Digest& digest,
                                const uint8_t* signature,
                                const uint8_t* publicKey,
                                size_t publicKeySize) {
    auto rBn =
            BIGNUM_Ptr(BN_bin2bn(signature, kEcdsaValueSize, nullptr), BN_free);
    if (rBn.get() == nullptr) {
        COSE_PRINT_ERROR("Error creating BIGNUM for r\n");
        return false;
    }

    auto sBn = BIGNUM_Ptr(
            BN_bin2bn(signature + kEcdsaValueSize, kEcdsaValueSize, nullptr),
            BN_free);
    if (sBn.get() == nullptr) {
        COSE_PRINT_ERROR("Error creating BIGNUM for s\n");
        return false;
    }

    auto sig = ECDSA_SIG_Ptr(ECDSA_SIG_new(), ECDSA_SIG_free);
    if (!sig) {
        COSE_PRINT_ERROR("Error allocating ECDSA_SIG\n");
        return false;
    }

    ECDSA_SIG_set0(sig.get(), rBn.release(), sBn.release());

    const unsigned char* k = publicKey;
    auto ecKey =
            EC_KEY_Ptr(d2i_EC_PUBKEY(nullptr, &k, publicKeySize), EC_KEY_free);
    if (!ecKey) {
        COSE_PRINT_ERROR("Error parsing EC public key\n");
        return false;
    }

    int rc = ECDSA_do_verify(digest.data(), digest.size(), sig.get(),
                             ecKey.get());
    if (rc != 1) {
        COSE_PRINT_ERROR("Error verifying signature (rc=%d)\n", rc);
        return false;
    }

    return true;
}

bool coseCheckEcDsaSignature(const std::vector<uint8_t>& signatureCoseSign1,
                             const std::vector<uint8_t>& detachedContent,
                             const std::vector<uint8_t>& publicKey) {
    auto [item, _, message] = cppbor::parse(signatureCoseSign1);
    if (item == nullptr) {
        COSE_PRINT_ERROR("Passed-in COSE_Sign1 is not valid CBOR\n");
        return false;
    }
    const cppbor::Array* array = item->asArray();
    if (array == nullptr) {
        COSE_PRINT_ERROR("Value for COSE_Sign1 is not an array\n");
        return false;
    }
    if (array->size() != 4) {
        COSE_PRINT_ERROR("Value for COSE_Sign1 is not an array of size 4\n");
        return false;
    }

    const cppbor::Bstr* encodedProtectedHeadersBstr = (*array)[0]->asBstr();
    if (encodedProtectedHeadersBstr == nullptr) {
        COSE_PRINT_ERROR("Value for encodedProtectedHeaders is not a bstr\n");
        return false;
    }
    const std::vector<uint8_t> encodedProtectedHeaders =
            encodedProtectedHeadersBstr->value();

    const cppbor::Map* unprotectedHeaders = (*array)[1]->asMap();
    if (unprotectedHeaders == nullptr) {
        COSE_PRINT_ERROR("Value for unprotectedHeaders is not a map\n");
        return false;
    }

    std::vector<uint8_t> data;
    const cppbor::Simple* payloadAsSimple = (*array)[2]->asSimple();
    if (payloadAsSimple != nullptr) {
        if (payloadAsSimple->asNull() == nullptr) {
            COSE_PRINT_ERROR("Value for payload is not null or a bstr\n");
            return false;
        }
    } else {
        const cppbor::Bstr* payloadAsBstr = (*array)[2]->asBstr();
        if (payloadAsBstr == nullptr) {
            COSE_PRINT_ERROR("Value for payload is not null or a bstr\n");
            return false;
        }
        data = payloadAsBstr->value();  // TODO: avoid copy
    }

    if (data.size() > 0 && detachedContent.size() > 0) {
        COSE_PRINT_ERROR("data and detachedContent cannot both be non-empty\n");
        return false;
    }

    const cppbor::Bstr* signatureBstr = (*array)[3]->asBstr();
    if (signatureBstr == nullptr) {
        COSE_PRINT_ERROR("Value for signature is not a bstr\n");
        return false;
    }
    const std::vector<uint8_t>& coseSignature = signatureBstr->value();
    if (coseSignature.size() != kEcdsaSignatureSize) {
        COSE_PRINT_ERROR("COSE signature length is %zu, expected %zu\n",
                         coseSignature.size(), kEcdsaSignatureSize);
        return false;
    }

    // The last field is the payload, independently of how it's transported (RFC
    // 8152 section 4.4). Since our API specifies only one of |data| and
    // |detachedContent| can be non-empty, it's simply just the non-empty one.
    auto& signaturePayload = data.size() > 0 ? data : detachedContent;
    std::vector<uint8_t> toBeSigned =
            coseBuildToBeSigned(encodedProtectedHeaders, signaturePayload);
    if (!checkEcDsaSignature(sha256(toBeSigned), coseSignature.data(),
                             publicKey.data(), publicKey.size())) {
        COSE_PRINT_ERROR("Signature check failed\n");
        return false;
    }
    return true;
}

/*
 * Strict signature verification code
 */
static const uint8_t kSignatureHeader[] = {
        // CBOR bytes
        0xD2,
        0x84,
        0x54,
        0xA2,
        0x01,
        // Algorithm identifier
        0x26,
        // CBOR bytes
        0x3A,
        0x00,
        0x01,
        0x00,
        0x00,
        0x82,
        0x69,
        // "TrustyApp"
        0x54,
        0x72,
        0x75,
        0x73,
        0x74,
        0x79,
        0x41,
        0x70,
        0x70,
        // Version
        0x01,
        // CBOR bytes
        0xA1,
        0x04,
        0x41,
};
static const uint8_t kSignatureHeaderPart2[] = {0xF6, 0x58, 0x40};
static const uint8_t kSignature1Header[] = {
        // CBOR bytes
        0x84,
        0x6A,
        // "Signature1"
        0x53,
        0x69,
        0x67,
        0x6E,
        0x61,
        0x74,
        0x75,
        0x72,
        0x65,
        0x31,
        // CBOR bytes
        0x54,
        0xA2,
        0x01,
        // Algorithm identifier
        0x26,
        // CBOR bytes
        0x3A,
        0x00,
        0x01,
        0x00,
        0x00,
        0x82,
        0x69,
        // "TrustyApp"
        0x54,
        0x72,
        0x75,
        0x73,
        0x74,
        0x79,
        0x41,
        0x70,
        0x70,
        // Version
        0x01,
        // CBOR bytes
        0x40,
};

/*
 * Fixed offset constants
 */
constexpr size_t kSignatureKeyIdOffset = sizeof(kSignatureHeader);
constexpr size_t kSignatureHeaderPart2Offset = kSignatureKeyIdOffset + 1;
constexpr size_t kSignatureOffset =
        kSignatureHeaderPart2Offset + sizeof(kSignatureHeaderPart2);
constexpr size_t kPayloadOffset = kSignatureOffset + kEcdsaSignatureSize;

bool strictCheckEcDsaSignature(const uint8_t* packageStart,
                               size_t packageSize,
                               GetKeyFn keyFn,
                               const uint8_t** outPackageStart,
                               size_t* outPackageSize) {
    if (packageSize < kPayloadOffset) {
        COSE_PRINT_ERROR("Passed-in COSE_Sign1 is not large enough\n");
        return false;
    }

    if (CRYPTO_memcmp(packageStart, kSignatureHeader,
                      sizeof(kSignatureHeader))) {
        COSE_PRINT_ERROR("Passed-in COSE_Sign1 is not valid CBOR\n");
        return false;
    }

    uint8_t kid = packageStart[kSignatureKeyIdOffset];
    auto [publicKey, publicKeySize] = keyFn(kid);
    if (!publicKey) {
        COSE_PRINT_ERROR("Failed to retrieve public key\n");
        return false;
    }

    if (CRYPTO_memcmp(packageStart + kSignatureHeaderPart2Offset,
                      kSignatureHeaderPart2, sizeof(kSignatureHeaderPart2))) {
        COSE_PRINT_ERROR("Passed-in COSE_Sign1 is not valid CBOR\n");
        return false;
    }

    // The Signature1 structure encodes the payload as a bstr wrapping the
    // actual contents (even if they already are CBOR), so we need to manually
    // prepend a CBOR bstr header to the payload
    constexpr size_t kMaxPayloadSizeHeaderSize = 9;
    size_t payloadSize = packageSize - kPayloadOffset;
    size_t payloadSizeHeaderSize = cppbor::headerSize(payloadSize);
    assert(payloadSizeHeaderSize <= kMaxPayloadSizeHeaderSize);

    uint8_t payloadSizeHeader[kMaxPayloadSizeHeaderSize];
    const uint8_t* payloadHeaderEnd =
            cppbor::encodeHeader(cppbor::BSTR, payloadSize, payloadSizeHeader,
                                 payloadSizeHeader + kMaxPayloadSizeHeaderSize);
    assert(payloadHeaderEnd == payloadSizeHeader + payloadSizeHeaderSize);

    SHA256Digest digest;
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, kSignature1Header, sizeof(kSignature1Header));
    SHA256_Update(&ctx, payloadSizeHeader, payloadSizeHeaderSize);
    SHA256_Update(&ctx, packageStart + kPayloadOffset, payloadSize);
    SHA256_Final(digest.data(), &ctx);

    if (!checkEcDsaSignature(digest, packageStart + kSignatureOffset,
                             publicKey.get(), publicKeySize)) {
        COSE_PRINT_ERROR("Signature check failed\n");
        return false;
    }

    if (outPackageStart != nullptr) {
        *outPackageStart = packageStart + kPayloadOffset;
    }
    if (outPackageSize != nullptr) {
        *outPackageSize = payloadSize;
    }
    return true;
}

static std::tuple<std::unique_ptr<uint8_t[]>, size_t> coseBuildGcmAad(
        std::string_view context,
        std::basic_string_view<uint8_t> encodedProtectedHeaders,
        std::basic_string_view<uint8_t> externalAad) {
    cppbor::Array encStructure;
    encStructure.add(context);
    encStructure.add(cppbor::ViewBstr(encodedProtectedHeaders));
    encStructure.add(cppbor::ViewBstr(externalAad));

    auto encStructureSize = encStructure.encodedSize();
    std::unique_ptr<uint8_t[]> encStructureEncoded(
            new (std::nothrow) uint8_t[encStructureSize]);
    if (!encStructureEncoded) {
        return {};
    }

    auto* encStructureEnd = encStructureEncoded.get() + encStructureSize;
    auto* p = encStructure.encode(encStructureEncoded.get(), encStructureEnd);
    if (p == nullptr) {
        return {};
    }
    assert(p == encStructureEnd);

    return {std::move(encStructureEncoded), encStructureSize};
}

static std::optional<std::vector<uint8_t>> encryptAes128Gcm(
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& nonce,
        const std::vector<uint8_t>& data,
        std::basic_string_view<uint8_t> additionalAuthenticatedData) {
    if (key.size() != kAes128GcmKeySize) {
        COSE_PRINT_ERROR("key is not kAes128GcmKeySize bytes, got %zu\n",
                         key.size());
        return {};
    }
    if (nonce.size() != kAesGcmIvSize) {
        COSE_PRINT_ERROR("nonce is not kAesGcmIvSize bytes, got %zu\n",
                         nonce.size());
        return {};
    }

    // The result is the ciphertext followed by the tag (kAesGcmTagSize bytes).
    std::vector<uint8_t> encryptedData;
    encryptedData.resize(data.size() + kAesGcmTagSize);
    unsigned char* ciphertext = (unsigned char*)encryptedData.data();
    unsigned char* tag = ciphertext + data.size();

    auto ctx = EVP_CIPHER_CTX_Ptr(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (ctx.get() == nullptr) {
        COSE_PRINT_ERROR("EVP_CIPHER_CTX_new: failed, error 0x%lx\n",
                         static_cast<unsigned long>(ERR_get_error()));
        return {};
    }

    if (EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_gcm(), NULL, NULL, NULL) !=
        1) {
        COSE_PRINT_ERROR("EVP_EncryptInit_ex: failed, error 0x%lx\n",
                         static_cast<unsigned long>(ERR_get_error()));
        return {};
    }

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, kAesGcmIvSize,
                            NULL) != 1) {
        COSE_PRINT_ERROR(
                "EVP_CIPHER_CTX_ctrl: failed setting nonce length, "
                "error 0x%lx\n",
                static_cast<unsigned long>(ERR_get_error()));
        return {};
    }

    if (EVP_EncryptInit_ex(ctx.get(), NULL, NULL, key.data(), nonce.data()) !=
        1) {
        COSE_PRINT_ERROR("EVP_EncryptInit_ex: failed, error 0x%lx\n",
                         static_cast<unsigned long>(ERR_get_error()));
        return {};
    }

    int numWritten;
    if (additionalAuthenticatedData.size() > 0) {
        if (EVP_EncryptUpdate(ctx.get(), NULL, &numWritten,
                              additionalAuthenticatedData.data(),
                              additionalAuthenticatedData.size()) != 1) {
            fprintf(stderr,
                    "EVP_EncryptUpdate: failed for "
                    "additionalAuthenticatedData, error 0x%lx\n",
                    static_cast<unsigned long>(ERR_get_error()));
            return {};
        }
        if ((size_t)numWritten != additionalAuthenticatedData.size()) {
            fprintf(stderr,
                    "EVP_EncryptUpdate: Unexpected outl=%d (expected %zu) "
                    "for additionalAuthenticatedData\n",
                    numWritten, additionalAuthenticatedData.size());
            return {};
        }
    }

    if (data.size() > 0) {
        if (EVP_EncryptUpdate(ctx.get(), ciphertext, &numWritten, data.data(),
                              data.size()) != 1) {
            COSE_PRINT_ERROR("EVP_EncryptUpdate: failed, error 0x%lx\n",
                             static_cast<unsigned long>(ERR_get_error()));
            return {};
        }
        if ((size_t)numWritten != data.size()) {
            fprintf(stderr,
                    "EVP_EncryptUpdate: Unexpected outl=%d (expected %zu)\n",
                    numWritten, data.size());
            ;
            return {};
        }
    }

    if (EVP_EncryptFinal_ex(ctx.get(), ciphertext + numWritten, &numWritten) !=
        1) {
        COSE_PRINT_ERROR("EVP_EncryptFinal_ex: failed, error 0x%lx\n",
                         static_cast<unsigned long>(ERR_get_error()));
        return {};
    }
    if (numWritten != 0) {
        COSE_PRINT_ERROR("EVP_EncryptFinal_ex: Unexpected non-zero outl=%d\n",
                         numWritten);
        return {};
    }

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_GET_TAG, kAesGcmTagSize,
                            tag) != 1) {
        COSE_PRINT_ERROR(
                "EVP_CIPHER_CTX_ctrl: failed getting tag, "
                "error 0x%lx\n",
                static_cast<unsigned long>(ERR_get_error()));
        return {};
    }

    return encryptedData;
}

static std::unique_ptr<cppbor::Item> coseEncryptAes128Gcm(
        std::string_view context,
        const std::vector<uint8_t>& key,
        const std::vector<uint8_t>& data,
        const std::vector<uint8_t>& externalAad,
        cppbor::Map protectedHeaders,
        cppbor::Map unprotectedHeaders,
        std::optional<cppbor::Array> recipients) {
    std::optional<std::vector<uint8_t>> iv = getRandom(kAesGcmIvSize);
    if (!iv) {
        COSE_PRINT_ERROR("Error generating encryption IV\n");
        return {};
    }
    unprotectedHeaders.add(COSE_LABEL_IV, iv.value());
    protectedHeaders.add(COSE_LABEL_ALG, COSE_ALG_A128GCM);

    // Canonicalize the headers to ensure a predictable layout
    protectedHeaders.canonicalize(true);
    unprotectedHeaders.canonicalize(true);

    std::vector<uint8_t> encodedProtectedHeaders =
            coseEncodeHeaders(protectedHeaders);
    std::basic_string_view encodedProtectedHeadersView{
            encodedProtectedHeaders.data(), encodedProtectedHeaders.size()};
    std::basic_string_view externalAadView{externalAad.data(),
                                           externalAad.size()};
    auto [gcmAad, gcmAadSize] = coseBuildGcmAad(
            context, encodedProtectedHeadersView, externalAadView);
    std::basic_string_view gcmAadView{gcmAad.get(), gcmAadSize};

    std::optional<std::vector<uint8_t>> ciphertext =
            encryptAes128Gcm(key, iv.value(), data, gcmAadView);
    if (!ciphertext) {
        COSE_PRINT_ERROR("Error encrypting data\n");
        return {};
    }

    auto coseArray = std::make_unique<cppbor::Array>();
    coseArray->add(encodedProtectedHeaders);
    coseArray->add(std::move(unprotectedHeaders));
    coseArray->add(ciphertext.value());
    if (recipients) {
        coseArray->add(std::move(recipients.value()));
    }
    return coseArray;
}

std::unique_ptr<cppbor::Item> coseEncryptAes128GcmKeyWrap(
        const std::vector<uint8_t>& key,
        uint8_t keyId,
        const std::vector<uint8_t>& data,
        const std::vector<uint8_t>& externalAad,
        cppbor::Map protectedHeaders,
        cppbor::Map unprotectedHeaders,
        bool tagged) {
    /* Generate and encrypt the CEK */
    std::optional<std::vector<uint8_t>> contentEncryptionKey =
            getRandom(kAes128GcmKeySize);
    if (!contentEncryptionKey) {
        COSE_PRINT_ERROR("Error generating encryption key\n");
        return {};
    }

    /* Build a COSE_Key structure for our CEK */
    cppbor::Map coseKey{
            COSE_LABEL_KEY_KTY,           COSE_KEY_TYPE_SYMMETRIC,
            COSE_LABEL_KEY_ALG,           COSE_ALG_A128GCM,
            COSE_LABEL_KEY_SYMMETRIC_KEY, contentEncryptionKey.value(),
    };
    coseKey.canonicalize(true);

    cppbor::Map keyUnprotectedHeaders{
            COSE_LABEL_KID,
            cppbor::Bstr(std::vector(1, keyId)),
    };
    auto encContentEncryptionKey = coseEncryptAes128Gcm(
            COSE_CONTEXT_ENC_RECIPIENT, key, coseKey.encode(), {}, {},
            std::move(keyUnprotectedHeaders), {});
    if (!encContentEncryptionKey) {
        COSE_PRINT_ERROR("Error wrapping encryption key\n");
        return {};
    }

    cppbor::Array recipients{encContentEncryptionKey.release()};
    auto coseEncrypt = coseEncryptAes128Gcm(
            COSE_CONTEXT_ENCRYPT, std::move(contentEncryptionKey.value()), data,
            externalAad, std::move(protectedHeaders),
            std::move(unprotectedHeaders), std::move(recipients));
    if (!coseEncrypt) {
        COSE_PRINT_ERROR("Error encrypting firmware package\n");
        return {};
    }

    if (tagged) {
        return std::make_unique<cppbor::SemanticTag>(COSE_TAG_ENCRYPT,
                                                     coseEncrypt.release());
    } else {
        return coseEncrypt;
    }
}

static bool decryptAes128GcmInPlace(
        std::basic_string_view<uint8_t> key,
        std::basic_string_view<uint8_t> nonce,
        uint8_t* encryptedData,
        size_t encryptedDataSize,
        std::basic_string_view<uint8_t> additionalAuthenticatedData,
        size_t* outPlaintextSize) {
    assert(outPlaintextSize != nullptr);

    int ciphertextSize = int(encryptedDataSize) - kAesGcmTagSize;
    if (ciphertextSize < 0) {
        COSE_PRINT_ERROR("encryptedData too small\n");
        return false;
    }
    if (key.size() != kAes128GcmKeySize) {
        COSE_PRINT_ERROR("key is not kAes128GcmKeySize bytes, got %zu\n",
                         key.size());
        return {};
    }
    if (nonce.size() != kAesGcmIvSize) {
        COSE_PRINT_ERROR("nonce is not kAesGcmIvSize bytes, got %zu\n",
                         nonce.size());
        return false;
    }
    unsigned char* ciphertext = encryptedData;
    unsigned char* tag = ciphertext + ciphertextSize;

    /*
     * Decrypt the data in place. OpenSSL and BoringSSL support this as long as
     * the plaintext buffer completely overlaps the ciphertext.
     */
    unsigned char* plaintext = encryptedData;

    auto ctx = EVP_CIPHER_CTX_Ptr(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (ctx.get() == nullptr) {
        COSE_PRINT_ERROR("EVP_CIPHER_CTX_new: failed, error 0x%lx\n",
                         static_cast<unsigned long>(ERR_get_error()));
        return false;
    }

    if (EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_gcm(), NULL, NULL, NULL) !=
        1) {
        COSE_PRINT_ERROR("EVP_DecryptInit_ex: failed, error 0x%lx\n",
                         static_cast<unsigned long>(ERR_get_error()));
        return false;
    }

    if (EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_IVLEN, kAesGcmIvSize,
                            NULL) != 1) {
        COSE_PRINT_ERROR(
                "EVP_CIPHER_CTX_ctrl: failed setting nonce length, "
                "error 0x%lx\n",
                static_cast<unsigned long>(ERR_get_error()));
        return false;
    }

    if (EVP_DecryptInit_ex(ctx.get(), NULL, NULL, key.data(), nonce.data()) !=
        1) {
        COSE_PRINT_ERROR("EVP_DecryptInit_ex: failed, error 0x%lx\n",
                         static_cast<unsigned long>(ERR_get_error()));
        return false;
    }

    int numWritten;
    if (additionalAuthenticatedData.size() > 0) {
        if (EVP_DecryptUpdate(ctx.get(), NULL, &numWritten,
                              additionalAuthenticatedData.data(),
                              additionalAuthenticatedData.size()) != 1) {
            COSE_PRINT_ERROR(
                    "EVP_DecryptUpdate: failed for "
                    "additionalAuthenticatedData, error 0x%lx\n",
                    static_cast<unsigned long>(ERR_get_error()));
            return false;
        }
        if ((size_t)numWritten != additionalAuthenticatedData.size()) {
            COSE_PRINT_ERROR(
                    "EVP_DecryptUpdate: Unexpected outl=%d "
                    "(expected %zd) for additionalAuthenticatedData\n",
                    numWritten, additionalAuthenticatedData.size());
            return false;
        }
    }

    if (EVP_DecryptUpdate(ctx.get(), plaintext, &numWritten, ciphertext,
                          ciphertextSize) != 1) {
        COSE_PRINT_ERROR("EVP_DecryptUpdate: failed, error 0x%lx\n",
                         static_cast<unsigned long>(ERR_get_error()));
        return false;
    }
    if (numWritten != ciphertextSize) {
        COSE_PRINT_ERROR(
                "EVP_DecryptUpdate: Unexpected outl=%d "
                "(expected %d)\n",
                numWritten, ciphertextSize);
        return false;
    }

    if (!EVP_CIPHER_CTX_ctrl(ctx.get(), EVP_CTRL_GCM_SET_TAG, kAesGcmTagSize,
                             tag)) {
        COSE_PRINT_ERROR(
                "EVP_CIPHER_CTX_ctrl: failed setting expected tag, "
                "error 0x%lx\n",
                static_cast<unsigned long>(ERR_get_error()));
        return false;
    }

    int ret =
            EVP_DecryptFinal_ex(ctx.get(), plaintext + numWritten, &numWritten);
    if (ret != 1) {
        COSE_PRINT_ERROR("EVP_DecryptFinal_ex: failed, error 0x%lx\n",
                         static_cast<unsigned long>(ERR_get_error()));
        return false;
    }
    if (numWritten != 0) {
        COSE_PRINT_ERROR("EVP_DecryptFinal_ex: Unexpected non-zero outl=%d\n",
                         numWritten);
        return false;
    }

    *outPlaintextSize = ciphertextSize;
    return true;
}

static bool coseDecryptAes128GcmInPlace(
        std::string_view context,
        const std::unique_ptr<cppbor::Item>& item,
        std::basic_string_view<uint8_t> key,
        const std::vector<uint8_t>& externalAad,
        const uint8_t** outPlaintextStart,
        size_t* outPlaintextSize,
        DecryptFn keyDecryptFn) {
    assert(outPlaintextStart != nullptr);
    assert(outPlaintextSize != nullptr);

    auto* itemArray = item->asArray();
    if (itemArray == nullptr) {
        COSE_PRINT_ERROR("Encrypted data is not a CBOR array\n");
        return false;
    }
    if (itemArray->size() < 3 || itemArray->size() > 4) {
        COSE_PRINT_ERROR("Invalid COSE encryption array size, got %zu\n",
                         itemArray->size());
        return false;
    }

    auto* encodedProtectedHeaders = itemArray->get(0)->asViewBstr();
    if (encodedProtectedHeaders == nullptr) {
        COSE_PRINT_ERROR(
                "Failed to retrieve protected headers "
                "from COSE encryption structure\n");
        return false;
    }

    auto [protectedHeaders, pos, err] =
            cppbor::parseWithViews(encodedProtectedHeaders->view().data(),
                                   encodedProtectedHeaders->view().size());
    if (!protectedHeaders) {
        COSE_PRINT_ERROR("Failed to parse protected headers\n");
        return false;
    }

    auto protectedHeadersMap = protectedHeaders->asMap();
    if (protectedHeadersMap == nullptr) {
        COSE_PRINT_ERROR("Invalid protected headers CBOR type\n");
        return false;
    }

    /* Validate alg to ensure the data was encrypted with AES-128-GCM */
    auto& alg_item = protectedHeadersMap->get(COSE_LABEL_ALG);
    if (alg_item == nullptr) {
        COSE_PRINT_ERROR("Missing alg field in COSE encryption structure\n");
        return false;
    }
    auto* alg = alg_item->asInt();
    if (alg == nullptr) {
        COSE_PRINT_ERROR(
                "Wrong CBOR type for alg value in protected headers\n");
        return false;
    }
    if (alg->value() != COSE_ALG_A128GCM) {
        COSE_PRINT_ERROR("Invalid COSE algorithm, got %" PRId64 "\n",
                         alg->value());
        return false;
    }

    auto* unprotectedHeaders = itemArray->get(1)->asMap();
    if (unprotectedHeaders == nullptr) {
        COSE_PRINT_ERROR(
                "Failed to retrieve unprotected headers "
                "from COSE encryption structure\n");
        return false;
    }

    auto& iv_item = unprotectedHeaders->get(COSE_LABEL_IV);
    if (iv_item == nullptr) {
        COSE_PRINT_ERROR("Missing IV field in COSE encryption structure\n");
        return false;
    }
    auto* iv = iv_item->asViewBstr();
    if (iv == nullptr) {
        COSE_PRINT_ERROR("Wrong CBOR type for IV value in protected headers\n");
        return false;
    }

    auto ciphertext = itemArray->get(2)->asViewBstr();
    if (ciphertext == nullptr) {
        COSE_PRINT_ERROR(
                "Failed to retrieve ciphertext "
                "from COSE encryption structure\n");
        return false;
    }

    std::basic_string_view externalAadView{externalAad.data(),
                                           externalAad.size()};
    auto [gcmAad, gcmAadSize] = coseBuildGcmAad(
            context, encodedProtectedHeaders->view(), externalAadView);
    std::basic_string_view gcmAadView{gcmAad.get(), gcmAadSize};

    if (!keyDecryptFn(key, iv->view(),
                      const_cast<uint8_t*>(ciphertext->view().data()),
                      ciphertext->view().size(), gcmAadView,
                      outPlaintextSize)) {
        return false;
    }

    *outPlaintextStart = ciphertext->view().data();
    return true;
}

bool coseDecryptAes128GcmKeyWrapInPlace(
        const std::unique_ptr<cppbor::Item>& item,
        GetKeyFn keyFn,
        const std::vector<uint8_t>& externalAad,
        bool checkTag,
        const uint8_t** outPackageStart,
        size_t* outPackageSize,
        DecryptFn keyDecryptFn) {
    assert(outPackageStart != nullptr);
    assert(outPackageSize != nullptr);

    if (!keyDecryptFn) {
        keyDecryptFn = &decryptAes128GcmInPlace;
    }

    if (checkTag) {
        if (item->semanticTagCount() != 1) {
            ALOGE("Invalid COSE_Encrypt tag count, expected 1 got %zd\n",
                  item->semanticTagCount());
            return false;
        }
        if (item->semanticTag() != COSE_TAG_ENCRYPT) {
            ALOGE("Invalid COSE_Encrypt semantic tag: %" PRIu64 "\n",
                  item->semanticTag());
            return false;
        }
    }

    auto* itemArray = item->asArray();
    if (itemArray == nullptr) {
        COSE_PRINT_ERROR("Encrypted data is not a CBOR array\n");
        return false;
    }
    if (itemArray->size() != 4) {
        COSE_PRINT_ERROR("Invalid COSE_Encrypt array size, got %zu\n",
                         itemArray->size());
        return false;
    }

    auto* recipientsArray = itemArray->get(3)->asArray();
    if (recipientsArray == nullptr) {
        COSE_PRINT_ERROR(
                "Failed to retrieve recipients "
                "from COSE_Encrypt structure\n");
        return false;
    }
    if (recipientsArray->size() != 1) {
        COSE_PRINT_ERROR("Invalid recipients array size, got %zu\n",
                         recipientsArray->size());
        return false;
    }

    auto& recipient = recipientsArray->get(0);
    auto* recipientArray = recipient->asArray();
    if (recipientArray == nullptr) {
        COSE_PRINT_ERROR("COSE_Recipient is not a CBOR array\n");
        return false;
    }
    if (recipientArray->size() != 3) {
        COSE_PRINT_ERROR(
                "Invalid COSE_Recipient structure array size, "
                "got %zu\n",
                recipientArray->size());
        return false;
    }

    auto* unprotectedHeaders = recipientArray->get(1)->asMap();
    if (unprotectedHeaders == nullptr) {
        COSE_PRINT_ERROR(
                "Failed to retrieve unprotected headers "
                "from COSE_Recipient structure\n");
        return false;
    }

    auto& keyIdItem = unprotectedHeaders->get(COSE_LABEL_KID);
    if (keyIdItem == nullptr) {
        COSE_PRINT_ERROR("Missing key id field in COSE_Recipient\n");
        return false;
    }
    auto* keyIdBytes = keyIdItem->asViewBstr();
    if (keyIdBytes == nullptr) {
        COSE_PRINT_ERROR("Wrong CBOR type for key id in COSE_Recipient\n");
        return false;
    }
    if (keyIdBytes->view().size() != 1) {
        COSE_PRINT_ERROR("Invalid key id field length, got %zu\n",
                         keyIdBytes->view().size());
        return false;
    }

    auto keyId = keyIdBytes->view()[0];
    auto [keyEncryptionKeyStart, keyEncryptionKeySize] = keyFn(keyId);
    if (!keyEncryptionKeyStart) {
        COSE_PRINT_ERROR("Failed to retrieve decryption key\n");
        return false;
    }

    std::basic_string_view<uint8_t> keyEncryptionKey{
            keyEncryptionKeyStart.get(), keyEncryptionKeySize};
    const uint8_t* coseKeyStart;
    size_t coseKeySize;
    if (!coseDecryptAes128GcmInPlace(COSE_CONTEXT_ENC_RECIPIENT, recipient,
                                     keyEncryptionKey, {}, &coseKeyStart,
                                     &coseKeySize, keyDecryptFn)) {
        COSE_PRINT_ERROR("Failed to decrypt COSE_Key structure\n");
        return false;
    }

    auto [coseKey, pos, err] =
            cppbor::parseWithViews(coseKeyStart, coseKeySize);
    if (!coseKey) {
        COSE_PRINT_ERROR("Failed to parse COSE_Key structure\n");
        return false;
    }

    auto* coseKeyMap = coseKey->asMap();
    if (coseKeyMap == nullptr) {
        COSE_PRINT_ERROR("COSE_Key structure is not an array\n");
        return false;
    }

    auto& ktyItem = coseKeyMap->get(COSE_LABEL_KEY_KTY);
    if (ktyItem == nullptr) {
        COSE_PRINT_ERROR("Missing kty field of COSE_Key\n");
        return false;
    }
    auto* kty = ktyItem->asInt();
    if (kty == nullptr) {
        COSE_PRINT_ERROR("Wrong CBOR type for kty field of COSE_Key\n");
        return false;
    }
    if (kty->value() != COSE_KEY_TYPE_SYMMETRIC) {
        COSE_PRINT_ERROR("Invalid COSE_Key key type: %" PRId64 "\n",
                         kty->value());
        return false;
    }

    auto& algItem = coseKeyMap->get(COSE_LABEL_KEY_ALG);
    if (algItem == nullptr) {
        COSE_PRINT_ERROR("Missing alg field of COSE_Key\n");
        return false;
    }
    auto* alg = algItem->asInt();
    if (alg == nullptr) {
        COSE_PRINT_ERROR("Invalid CBOR type for alg field of COSE_Key\n");
        return false;
    }
    if (alg->value() != COSE_ALG_A128GCM) {
        COSE_PRINT_ERROR("Invalid COSE_Key algorithm value: %" PRId64 "\n",
                         alg->value());
        return false;
    }

    auto& contentEncryptionKeyItem =
            coseKeyMap->get(COSE_LABEL_KEY_SYMMETRIC_KEY);
    if (contentEncryptionKeyItem == nullptr) {
        COSE_PRINT_ERROR("Missing key field in COSE_Key\n");
        return false;
    }
    auto* contentEncryptionKey = contentEncryptionKeyItem->asViewBstr();
    if (contentEncryptionKey == nullptr) {
        COSE_PRINT_ERROR("Wrong CBOR type for key field of COSE_Key\n");
        return false;
    }
    auto contentEncryptionKeySize = contentEncryptionKey->view().size();
    if (contentEncryptionKeySize != kAes128GcmKeySize) {
        COSE_PRINT_ERROR("Invalid content encryption key size, got %zu\n",
                         contentEncryptionKeySize);
        return false;
    }

    if (!coseDecryptAes128GcmInPlace(COSE_CONTEXT_ENCRYPT, item,
                                     contentEncryptionKey->view(), externalAad,
                                     outPackageStart, outPackageSize,
                                     decryptAes128GcmInPlace)) {
        COSE_PRINT_ERROR("Failed to decrypt payload\n");
        return false;
    }

    return true;
}
