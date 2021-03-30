#define LOG_TAG "wv_client"

#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>

#include <wv_client.h>

namespace android {

class WvClientTest: public testing::Test {};

TEST(WvClientTest, HelloWorld) {
    printf("Hello World!");
}

TEST(WvClientTest, WvSetSecureModeTest) {
    set_secure_pipe(0);
    set_secure_pipe(1);
}

} // namespace android

