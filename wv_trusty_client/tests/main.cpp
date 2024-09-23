#define LOG_TAG "wv_client"

#include <gtest/gtest.h>
#include <stdlib.h>
#include <string.h>
#include <hwsecure_client.h>

namespace android {

class WvClientTest : public testing::Test {};

TEST(WvClientTest, HelloWorld) {
    printf("Hello World!");
}

TEST(WvClientTest, WvSetSecureModeTest) {
    set_g2d_secure_pipe(0);

    enum g2d_secure_mode mode;
    mode = get_g2d_secure_pipe();
    EXPECT_EQ(mode, NON_SECURE);

    set_g2d_secure_pipe(1);
    mode = get_g2d_secure_pipe();
    EXPECT_EQ(mode, SECURE);
}

} // namespace android
