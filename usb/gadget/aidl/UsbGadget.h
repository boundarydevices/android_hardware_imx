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

#pragma once

#include <aidl/android/hardware/usb/gadget/BnUsbGadget.h>
#include <aidl/android/hardware/usb/gadget/BnUsbGadgetCallback.h>
#include <aidl/android/hardware/usb/gadget/GadgetFunction.h>
#include <aidl/android/hardware/usb/gadget/IUsbGadget.h>
#include <aidl/android/hardware/usb/gadget/IUsbGadgetCallback.h>
#include <android-base/file.h>
#include <android-base/parseint.h>
#include <android-base/properties.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>
#include <sched.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <utils/Log.h>

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

namespace aidl {
namespace android {
namespace hardware {
namespace usb {
namespace gadget {

using ::aidl::android::hardware::usb::gadget::GadgetFunction;
using ::aidl::android::hardware::usb::gadget::IUsbGadget;
using ::aidl::android::hardware::usb::gadget::IUsbGadgetCallback;
using ::aidl::android::hardware::usb::gadget::Status;
using ::aidl::android::hardware::usb::gadget::UsbSpeed;
using ::android::base::GetProperty;
using ::android::base::ParseUint;
using ::android::base::ReadFileToString;
using ::android::base::SetProperty;
using ::android::base::Trim;
using ::android::base::unique_fd;
using ::android::base::WriteStringToFile;
using ::ndk::ScopedAStatus;
using ::std::shared_ptr;
using ::std::string;

using ::std::lock_guard;
using ::std::move;
using ::std::mutex;
using ::std::thread;
using ::std::unique_ptr;
using ::std::vector;
using ::std::chrono::steady_clock;
using namespace std::chrono;
using namespace std::chrono_literals;

constexpr int BUFFER_SIZE = 512;
constexpr int MAX_FILE_PATH_LENGTH = 256;
constexpr int EPOLL_EVENTS = 10;
constexpr bool DEBUG = false;
constexpr int DISCONNECT_WAIT_US = 100000;
constexpr int PULL_UP_DELAY = 500000;

#define GADGET_PATH "/config/usb_gadget/g1/"
#define PULLUP_PATH GADGET_PATH "UDC"
#define VENDOR_ID_PATH GADGET_PATH "idVendor"
#define PRODUCT_ID_PATH GADGET_PATH "idProduct"
#define DEVICE_CLASS_PATH GADGET_PATH "bDeviceClass"
#define DEVICE_SUB_CLASS_PATH GADGET_PATH "bDeviceSubClass"
#define DEVICE_PROTOCOL_PATH GADGET_PATH "bDeviceProtocol"
#define DESC_USE_PATH GADGET_PATH "os_desc/use"
#define OS_DESC_PATH GADGET_PATH "os_desc/b.1"
#define CONFIG_PATH GADGET_PATH "configs/b.1/"
#define FUNCTIONS_PATH GADGET_PATH "functions/"
#define FUNCTION_NAME "function"
#define FUNCTION_PATH CONFIG_PATH FUNCTION_NAME
#define RNDIS_PATH FUNCTIONS_PATH "rndis.gs4"
#define CONFIGURATION_PATH CONFIG_PATH "strings/0x409/configuration"

#define USB_CONTROLLER "vendor.usb.config"
#define GADGET_NAME GetProperty(USB_CONTROLLER, "")
#define SPEED_PATH "/current_speed"

struct UsbGadget : public BnUsbGadget {
    UsbGadget();

    unique_fd mInotifyFd;
    unique_fd mEventFd;
    unique_fd mEpollFd;

    unique_ptr<thread> mMonitor;
    volatile bool mMonitorCreated;
    vector<string> mEndpointList;

    // protects the CV.
    std::mutex mLock;
    std::condition_variable mCv;

    // Makes sure that only one request is processed at a time.
    std::mutex mLockSetCurrentFunction;
    long mCurrentUsbFunctions;
    bool mCurrentUsbFunctionsApplied;
    UsbSpeed mUsbSpeed;

    ScopedAStatus setCurrentUsbFunctions(int64_t functions,
                                         const shared_ptr<IUsbGadgetCallback>& callback,
                                         int64_t timeoutMs, int64_t in_transactionId) override;

    ScopedAStatus getCurrentUsbFunctions(const shared_ptr<IUsbGadgetCallback>& callback,
                                         int64_t in_transactionId) override;

    ScopedAStatus reset(const shared_ptr<IUsbGadgetCallback>& callback,
                        int64_t in_transactionId) override;

    ScopedAStatus getUsbSpeed(const shared_ptr<IUsbGadgetCallback>& callback,
                              int64_t in_transactionId) override;

private:
    Status tearDownGadget();
    Status getUsbGadgetIrqPath();
    Status setupFunctions(long functions, const shared_ptr<IUsbGadgetCallback>& callback,
                          uint64_t timeout, int64_t in_transactionId);
};

} // namespace gadget
} // namespace usb
} // namespace hardware
} // namespace android
} // namespace aidl
