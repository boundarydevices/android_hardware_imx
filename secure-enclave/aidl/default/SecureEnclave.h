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

#pragma once

#include <EleMessage.h>
#include <EleOperation.h>
#include <aidl/nxp/hardware/ele/BnSecureEnclave.h>
#include <cutils/log.h>

namespace aidl {
namespace nxp {
namespace hardware {
namespace ele {
/*
 * Only two keystore can be created in ELE, here we
 * use fixed keystore ID and nouce to provide services.
 *
 * This keystore ID should not be used anywhere else.
 */
const int KEY_STORE_ID = 0x1111;
const int KEY_STORE_NONCE = 1000;

class SecureEnclave : public BnSecureEnclave {
public:
    SecureEnclave();
    ~SecureEnclave();

    ::ndk::ScopedAStatus eleGenerateKey(int32_t in_keyId, int32_t in_pubKeySize,
                                        int32_t in_keyGroup, int32_t in_keyType, int32_t in_sizeBit,
                                        int32_t in_lifeTime, int32_t in_usage,
                                        int32_t in_permitAlgo, int32_t in_lifeCycle,
                                        int32_t in_flags, std::vector<uint8_t>* out_pubKey,
                                        int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus eleDeleteKey(int32_t in_keyId, int32_t in_flags) override;
    ::ndk::ScopedAStatus eleGetKeyAttr(int32_t in_keyId,
                                       ISecureEnclave::KeyAttribute* out_keyAttr) override;
    ::ndk::ScopedAStatus eleCipherOperation(int32_t in_keyId, const std::vector<uint8_t>& in_iv,
                                            int32_t in_flags, int32_t in_algo,
                                            const std::vector<uint8_t>& in_input,
                                            std::vector<uint8_t>* out_output,
                                            int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus eleCipherAEOperation(int32_t in_keyId, const std::vector<uint8_t>& in_iv,
                                              int32_t in_flags, int32_t in_algo,
                                              const std::vector<uint8_t>& in_aad,
                                              const std::vector<uint8_t>& in_input,
                                              std::vector<uint8_t>* out_output,
                                              int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus eleSignGenerate(int32_t in_keyId, const std::vector<uint8_t>& in_message,
                                         std::vector<uint8_t>* out_signature, int32_t in_flags,
                                         int32_t in_signScheme, int32_t in_saltLen,
                                         int32_t* _aidl_return) override;
    ::ndk::ScopedAStatus eleSignVerify(const std::vector<uint8_t>& in_key,
                                       const std::vector<uint8_t>& in_message,
                                       const std::vector<uint8_t>& in_signature,
                                       int32_t in_keySecuritySize, int32_t in_keyType,
                                       int32_t in_flags, int32_t in_signScheme,
                                       int32_t in_saltLength) override;
    ::ndk::ScopedAStatus eleMacOperation(int32_t in_keyId, const std::vector<uint8_t>& in_payload,
                                         std::vector<uint8_t>* in_mac, int32_t in_macSize,
                                         int32_t in_flag, int32_t in_algorithm,
                                         int32_t* _aidl_return) override;

private:
    void shutDownDevice(void);
    ErrorType checkDevice();
    ErrorType eleOpenSessionKeystore(uint32_t* keyStoreHandler);
    ErrorType eleCloseSessionKeystore(uint32_t keyStoreHandler);

    bool eleDeviceReady = false;
    std::unique_ptr<EleOperation> eleOps = nullptr;
};

} // namespace ele
} // namespace hardware
} // namespace nxp
} // namespace aidl
