#include <android-base/logging.h>
#include <android-base/properties.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>

TEST(Trusty_TEST, RO_Test) {
    EXPECT_STREQ(android::base::GetProperty("ro.hardware.keystore", "").c_str(), "trusty");
    EXPECT_STREQ(android::base::GetProperty("ro.hardware.gatekeeper", "").c_str(), "trusty");
    EXPECT_STREQ(android::base::GetProperty("vendor.storageproxyd", "").c_str(), "trusty");
}
