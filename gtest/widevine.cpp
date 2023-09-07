/*
 * Copyright (C) 2020 The Android Open Source Project
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

#ifndef DRM_HAL_CLEARKEY_TEST_H
#define DRM_HAL_CLEARKEY_TEST_H

#include <android/hardware/drm/1.0/ICryptoFactory.h>
#include <android/hardware/drm/1.0/ICryptoPlugin.h>
#include <android/hardware/drm/1.0/IDrmFactory.h>
#include <android/hardware/drm/1.0/IDrmPlugin.h>
#include <android/hardware/drm/1.0/types.h>
#include <android/hidl/allocator/1.0/IAllocator.h>
#include <gtest/gtest.h>
#include <hidl/HidlSupport.h>
#include <hidl/ServiceManagement.h>
#include <hidlmemory/mapping.h>
#include <log/log.h>
#include <openssl/aes.h>

#include <random>

#include "drm_vts_helper.h"

using ::android::sp;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::drm::V1_0::ICryptoFactory;
using ::android::hardware::drm::V1_0::IDrmFactory;
using ::android::hidl::allocator::V1_0::IAllocator;
using ::android::hidl::memory::V1_0::IMemory;
using ::drm_vts::hidl_array;
using ::drm_vts::PrintParamInstanceToString;

using std::map;
using std::mt19937;
using std::string;
using std::vector;
/**
 * These clearkey tests use white box knowledge of the legacy clearkey
 * plugin to verify that the HIDL HAL services and interfaces are working.
 * It is not intended to verify any vendor's HAL implementation. If you
 * are looking for vendor HAL tests, see drm_hal_vendor_test.cpp
 */
#define ASSERT_OK(ret) ASSERT_TRUE(ret.isOk())
#define EXPECT_OK(ret) EXPECT_TRUE(ret.isOk())

namespace android {
namespace hardware {
namespace drm {
namespace V1_0 {
namespace vts {
class Widevinetest : public ::testing::TestWithParam<const char*> {
public:
    void SetUp() override {
        const ::testing::TestInfo* const test_info =
                ::testing::UnitTest::GetInstance()->current_test_info();
        ALOGD("Running test %s.%s", test_info->test_case_name(), test_info->name());

        const std::string instanceName = GetParam();
        ;
        drmFactory = IDrmFactory::getService(instanceName);
        ASSERT_NE(nullptr, drmFactory.get());
        cryptoFactory = ICryptoFactory::getService(instanceName);
        ASSERT_NE(nullptr, cryptoFactory.get());

        const bool drmClearKey = drmFactory->isCryptoSchemeSupported(kClearKeyUUID);
        const bool cryptoClearKey = cryptoFactory->isCryptoSchemeSupported(kClearKeyUUID);
        EXPECT_EQ(drmClearKey, cryptoClearKey);
        const bool supportsClearKey = drmClearKey && cryptoClearKey;

        const bool drmCommonPsshBox = drmFactory->isCryptoSchemeSupported(kCommonPsshBoxUUID);
        const bool cryptoCommonPsshBox = cryptoFactory->isCryptoSchemeSupported(kCommonPsshBoxUUID);
        EXPECT_EQ(drmCommonPsshBox, cryptoCommonPsshBox);
        const bool supportsCommonPsshBox = drmCommonPsshBox && cryptoCommonPsshBox;

        EXPECT_EQ(supportsClearKey, supportsCommonPsshBox);
        correspondsToThisTest = supportsClearKey && supportsCommonPsshBox;
    }

protected:
    static constexpr uint8_t kCommonPsshBoxUUID[16] = {0x10, 0x77, 0xEF, 0xEC, 0xC0, 0xB2,
                                                       0x4D, 0x02, 0xAC, 0xE3, 0x3C, 0x1E,
                                                       0x52, 0xE2, 0xFB, 0x4B};

    // To be used in mpd to specify drm scheme for players
    static constexpr uint8_t kClearKeyUUID[16] = {0xE2, 0x71, 0x9D, 0x58, 0xA9, 0x85, 0xB3, 0xC9,
                                                  0x78, 0x1A, 0xB0, 0x30, 0xAF, 0x78, 0xD3, 0x0E};

    android::sp<IDrmFactory> drmFactory;
    android::sp<ICryptoFactory> cryptoFactory;

    bool correspondsToThisTest;
};

static const uint8_t kInvalidUUID[16] = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
                                         0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80};

TEST_P(Widevinetest, IsSupportWidevine) {
    EXPECT_FALSE(drmFactory->isCryptoSchemeSupported(kInvalidUUID));
    EXPECT_FALSE(cryptoFactory->isCryptoSchemeSupported(kInvalidUUID));
}
TEST_P(Widevinetest, EmptyPluginUUIDNotSupported) {
    hidl_array<uint8_t, 16> emptyUUID;
    memset(emptyUUID.data(), 0, 16);
    EXPECT_FALSE(drmFactory->isCryptoSchemeSupported(emptyUUID));
    EXPECT_FALSE(cryptoFactory->isCryptoSchemeSupported(emptyUUID));
}
TEST_P(Widevinetest, EmptyContentTypeNotSupported) {
    hidl_string empty;
    EXPECT_FALSE(drmFactory->isContentTypeSupported(empty));
}
TEST_P(Widevinetest, InvalidContentTypeNotSupported) {
    hidl_string invalid("abcdabcd");
    EXPECT_FALSE(drmFactory->isContentTypeSupported(invalid));
}
TEST_P(Widevinetest, ValidContentTypeSupported) {
    hidl_string cencType("cenc");
    EXPECT_TRUE(drmFactory->isContentTypeSupported(cencType));
}
TEST_P(Widevinetest, CreateInvalidDrmPlugin) {
    hidl_string packageName("android.hardware.drm.test");
    auto res = drmFactory->createPlugin(kInvalidUUID, packageName,
                                        [&](Status status, const sp<IDrmPlugin>& plugin) {
                                            EXPECT_EQ(Status::ERROR_DRM_CANNOT_HANDLE, status);
                                            EXPECT_EQ(nullptr, plugin.get());
                                        });
    EXPECT_OK(res);
}
TEST_P(Widevinetest, CreateInvalidCryptoPlugin) {
    hidl_vec<uint8_t> initVec;
    auto res = cryptoFactory->createPlugin(kInvalidUUID, initVec,
                                           [&](Status status, const sp<ICryptoPlugin>& plugin) {
                                               EXPECT_EQ(Status::ERROR_DRM_CANNOT_HANDLE, status);
                                               EXPECT_EQ(nullptr, plugin.get());
                                           });
    EXPECT_OK(res);
}

INSTANTIATE_TEST_SUITE_P(PerInstance, Widevinetest, testing::Values("widevine"));

} // namespace vts
} // namespace V1_0
} // namespace drm
} // namespace hardware
} // namespace android

#endif // DRM_HAL_CLEARKEY_TEST_H
