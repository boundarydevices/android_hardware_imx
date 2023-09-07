#include <android/hardware/usb/gadget/1.0/IUsbGadget.h>
#include <cutils/log.h>
#include <gtest/gtest.h>
#include <stdio.h>
#include <unistd.h>
using android::hardware::Return;
using android::hardware::usb::gadget::V1_0::GadgetFunction;
using android::hardware::usb::gadget::V1_0::IUsbGadget;

void UsbGtest() {
    ALOGI("Enter the UsbModeTest Gtest!!!");
    android::sp<IUsbGadget> gadget = IUsbGadget::getService();
    Return<void> ret;
    uint8_t sleep_delay = 2;
    int i = 0;
    while (i < 100) {
        ret = gadget->setCurrentUsbFunctions(static_cast<uint64_t>(GadgetFunction::MTP), nullptr,
                                             0);
        if (!ret.isOk()) {
            ALOGI("Failed to switch to MTP(%s)", ret.description().c_str());
        }
        sleep(sleep_delay);
        ret = gadget->setCurrentUsbFunctions(static_cast<uint64_t>(GadgetFunction::ADB), nullptr,
                                             0);
        if (!ret.isOk()) {
            ALOGI("Failed to switch to ADB(%s)", ret.description().c_str());
        }
        sleep(sleep_delay);
        ret = gadget->setCurrentUsbFunctions(static_cast<uint64_t>(GadgetFunction::PTP), nullptr,
                                             0);
        if (!ret.isOk()) {
            ALOGI("Failed to switch to PTP(%s)", ret.description().c_str());
        }
        sleep(sleep_delay);
        ret = gadget->setCurrentUsbFunctions(static_cast<uint64_t>(GadgetFunction::RNDIS), nullptr,
                                             0);
        if (!ret.isOk()) {
            ALOGI("Failed to switch to RNDIS(%s)", ret.description().c_str());
        }
        sleep(sleep_delay);
        i = i + 1;
    }
}

TEST(UsbModeTest, SetUsbMode) {
    UsbGtest();
}
