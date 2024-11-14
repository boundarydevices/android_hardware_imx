/*
 * Copyright (C) 2022 The Android Open Source Project
 * Copyright 2023 NXP
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

#define LOG_TAG "android.hardware.tv.hdmi.cec"
#include "HdmiCecMock.h"

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <fcntl.h>
#include <hardware/hardware.h>
#include <hardware/hdmi_cec.h>
#include <linux/cec.h>
#include <utils/Log.h>

using ndk::ScopedAStatus;

namespace android {
namespace hardware {
namespace tv {
namespace hdmi {
namespace cec {
namespace implementation {

std::shared_ptr<IHdmiCecCallback> HdmiCecMock::mCallback = nullptr;

void HdmiCecMock::serviceDied(void* cookie) {
    ALOGE("HdmiCecMock died");
    auto hdmiCecMock = static_cast<HdmiCecMock*>(cookie);
    hdmiCecMock->mCecThreadRun = false;
}

ScopedAStatus HdmiCecMock::addLogicalAddress(CecLogicalAddress addr, Result* _aidl_return) {
    if (!mDevice) {
        ALOGE("mDevice is null, cec not support");
        *_aidl_return = Result::SUCCESS;
        return ScopedAStatus::ok();
    }
    // Have a list to maintain logical addresses
    mLogicalAddresses.push_back(addr);
    int ret = mDevice->add_logical_address(mDevice, static_cast<cec_logical_address_t>(addr));

    switch (ret) {
        case 0:
            *_aidl_return = Result::SUCCESS;
            break;
        case -EINVAL:
            *_aidl_return = Result::FAILURE_INVALID_ARGS;
            break;
        case -ENOTSUP:
            *_aidl_return = Result::FAILURE_NOT_SUPPORTED;
            break;
        case -EBUSY:
            *_aidl_return = Result::FAILURE_BUSY;
            break;
        default:
            *_aidl_return = Result::FAILURE_UNKNOWN;
    }

    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCecMock::clearLogicalAddress() {
    if (!mDevice) {
        ALOGE("mDevice is null, cec not support");
        return ScopedAStatus::ok();
    }
    // Remove logical address from the list
    mLogicalAddresses = {};
    mDevice->clear_logical_address(mDevice);
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCecMock::enableAudioReturnChannel(int32_t portId __unused, bool enable __unused) {
    // Maintain ARC status
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCecMock::getCecVersion(int32_t* _aidl_return) {
    if (!mDevice) {
        ALOGE("mDevice is null, cec not support");
    } else {
        // Maintain a cec version and return it
        mDevice->get_version(mDevice, &mCecVersion);
    }
    *_aidl_return = mCecVersion;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCecMock::getPhysicalAddress(int32_t* _aidl_return) {
    if (!mDevice) {
        ALOGE("mDevice is null, cec not support");
    } else {
        // Compare physical address with the value in edid, update the address if different
        uint16_t edidPhyaddr = CEC_PHYS_ADDR_INVALID;
        getPhysicalAddrFromEdid(&edidPhyaddr);
        ALOGV("getPhysicalAddrFromEdid  edidPhyaddr:0x%x,  mPhysicalAddress:0x%x", edidPhyaddr,
              mPhysicalAddress);

        mDevice->get_physical_address(mDevice, &mPhysicalAddress);
        if (mPhysicalAddress != edidPhyaddr && edidPhyaddr != CEC_PHYS_ADDR_INVALID) {
            mPhysicalAddress = edidPhyaddr;
        }
    }
    *_aidl_return = mPhysicalAddress;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCecMock::getVendorId(int32_t* _aidl_return) {
    if (!mDevice) {
        ALOGE("mDevice is null, cec not support");
    } else {
        mDevice->get_vendor_id(mDevice, &mCecVendorId);
    }
    *_aidl_return = mCecVendorId;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCecMock::sendMessage(const CecMessage& message, SendMessageResult* _aidl_return) {
    if (!mDevice) {
        ALOGE("mDevice is null, cec not support");
        *_aidl_return = SendMessageResult::SUCCESS;
        return ScopedAStatus::ok();
    }
    if (message.body.size() == 0) {
        *_aidl_return = SendMessageResult::NACK;
    } else {
        cec_message_t legacyMessage {
            .initiator = static_cast<cec_logical_address_t>(message.initiator),
            .destination = static_cast<cec_logical_address_t>(message.destination),
            .length = message.body.size(),
        };
        for (size_t i = 0; i < message.body.size(); ++i) {
            legacyMessage.body[i] = static_cast<unsigned char>(message.body[i]);
        }
        *_aidl_return = static_cast<SendMessageResult>(mDevice->send_message(mDevice, &legacyMessage));
    }
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCecMock::setCallback(const std::shared_ptr<IHdmiCecCallback>& callback) {
    if (!mDevice) {
        ALOGE("mDevice is null, cec not support");
        return ScopedAStatus::ok();
    }
    // If callback is null, mCallback is also set to null so we do not call the old callback.
    mCallback = callback;

    if (callback != nullptr) {
        AIBinder_linkToDeath(this->asBinder().get(), mDeathRecipient.get(), 0 /* cookie */);
        mDevice->register_event_callback(mDevice, eventCallback, nullptr);
    }
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCecMock::setLanguage(const std::string& language) {
    if (!mDevice) {
        ALOGE("mDevice is null, cec not support");
        return ScopedAStatus::ok();
    }
    if (language.size() != 3) {
        LOG(ERROR) << "Wrong language code: expected 3 letters, but it was " << language.size()
                   << ".";
        return ScopedAStatus::ok();
    }
    // TODO Validate if language is a valid language code
    const char* languageStr = language.c_str();
    int convertedLanguage = ((languageStr[0] & 0xFF) << 16) | ((languageStr[1] & 0xFF) << 8) |
                            (languageStr[2] & 0xFF);
    mDevice->set_option(mDevice, HDMI_OPTION_SET_LANG, convertedLanguage);
    mOptionLanguage = convertedLanguage;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCecMock::enableWakeupByOtp(bool value) {
    if (!mDevice) {
        ALOGE("mDevice is null, cec not support");
        return ScopedAStatus::ok();
    }
    mDevice->set_option(mDevice, HDMI_OPTION_WAKEUP, value ? 1 : 0);
    mOptionWakeUp = value;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCecMock::enableCec(bool value) {
    if (!mDevice) {
        ALOGE("mDevice is null, cec not support");
        return ScopedAStatus::ok();
    }
    mDevice->set_option(mDevice, HDMI_OPTION_ENABLE_CEC, value ? 1 : 0);
    mOptionEnableCec = value;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiCecMock::enableSystemCecControl(bool value) {
    if (!mDevice) {
        ALOGE("mDevice is null, cec not support");
        return ScopedAStatus::ok();
    }
    mDevice->set_option(mDevice, HDMI_OPTION_SYSTEM_CEC_CONTROL, value ? 1 : 0);
    mOptionSystemCecControl = value;
    return ScopedAStatus::ok();
}

void* HdmiCecMock::__threadLoop(void* user) {
    HdmiCecMock* const self = static_cast<HdmiCecMock*>(user);
    self->threadLoop();
    return 0;
}

int HdmiCecMock::readMessageFromFifo(unsigned char* buf, int msgCount) {
    if (msgCount <= 0 || !buf) {
        return 0;
    }

    int ret = -1;
    // Maybe blocked at driver
    ret = read(mInputFile, buf, msgCount);
    if (ret < 0) {
        ALOGE("read :%s failed, ret:%d\n", CEC_MSG_IN_FIFO, ret);
        return -1;
    }

    return ret;
}

int HdmiCecMock::sendMessageToFifo(const CecMessage& message) {
    unsigned char msgBuf[CEC_MESSAGE_BODY_MAX_LENGTH + 1] = {0};
    int ret = -1;

    msgBuf[0] = ((static_cast<uint8_t>(message.initiator) & 0xf) << 4) |
                (static_cast<uint8_t>(message.destination) & 0xf);

    size_t length = std::min(static_cast<size_t>(message.body.size()),
                             static_cast<size_t>(CEC_MESSAGE_BODY_MAX_LENGTH));
    for (size_t i = 0; i < length; ++i) {
        msgBuf[i + 1] = static_cast<unsigned char>(message.body[i]);
    }

    // Open the output pipe for writing outgoing cec message
    mOutputFile = open(CEC_MSG_OUT_FIFO, O_WRONLY | O_CLOEXEC);
    if (mOutputFile < 0) {
        ALOGD("file open failed for writing");
        return -1;
    }

    // Write message into the output pipe
    ret = write(mOutputFile, msgBuf, length + 1);
    close(mOutputFile);
    if (ret < 0) {
        ALOGE("write :%s failed, ret:%d\n", CEC_MSG_OUT_FIFO, ret);
        return -1;
    }
    return ret;
}

void HdmiCecMock::printCecMsgBuf(const char* msg_buf, int len) {
    int i, size = 0;
    const int bufSize = CEC_MESSAGE_BODY_MAX_LENGTH * 3;
    // Use 2 characters for each byte in the message plus 1 space
    char buf[bufSize] = {0};

    // Messages longer than max length will be truncated.
    for (i = 0; i < len && size < bufSize; i++) {
        size += sprintf(buf + size, " %02x", msg_buf[i]);
    }
    ALOGD("%s, msg:%.*s", __FUNCTION__, size, buf);
}

void HdmiCecMock::handleCecMessage(unsigned char* msgBuf, int msgSize) {
    CecMessage message;
    size_t length = std::min(static_cast<size_t>(msgSize - 1),
                             static_cast<size_t>(CEC_MESSAGE_BODY_MAX_LENGTH));
    message.body.resize(length);

    for (size_t i = 0; i < length; ++i) {
        message.body[i] = static_cast<uint8_t>(msgBuf[i + 1]);
        ALOGD("msg body %x", message.body[i]);
    }

    message.initiator = static_cast<CecLogicalAddress>((msgBuf[0] >> 4) & 0xf);
    ALOGD("msg init %hhd", message.initiator);
    message.destination = static_cast<CecLogicalAddress>((msgBuf[0] >> 0) & 0xf);
    ALOGD("msg dest %hhd", message.destination);

    if (mCallback != nullptr) {
        mCallback->onCecMessage(message);
    }
}

void HdmiCecMock::threadLoop() {
    ALOGD("threadLoop start.");
    unsigned char msgBuf[CEC_MESSAGE_BODY_MAX_LENGTH];
    int r = -1;

    // Open the input pipe
    while (mInputFile < 0) {
        usleep(1000 * 1000);
        mInputFile = open(CEC_MSG_IN_FIFO, O_RDONLY | O_CLOEXEC);
    }
    ALOGD("file open ok, fd = %d.", mInputFile);

    while (mCecThreadRun) {
        if (!mOptionSystemCecControl) {
            usleep(1000 * 1000);
            continue;
        }

        memset(msgBuf, 0, sizeof(msgBuf));
        // Try to get a message from dev.
        // echo -n -e '\x04\x83' >> /dev/cec
        r = readMessageFromFifo(msgBuf, CEC_MESSAGE_BODY_MAX_LENGTH);
        if (r <= 1) {
            // Ignore received ping messages
            continue;
        }

        printCecMsgBuf((const char*)msgBuf, r);

        if (((msgBuf[0] >> 4) & 0xf) == 0xf) {
            // The message is a hotplug event, handled by HDMI HAL.
            continue;
        }

        handleCecMessage(msgBuf, r);
    }

    ALOGD("thread end.");
}

bool HdmiCecMock::getPhysicalAddrFromEdid(uint16_t* phyaddr) {
    using AidlIComposer = aidl::android::hardware::graphics::composer3::IComposer;
    using AidlIComposerClient = aidl::android::hardware::graphics::composer3::IComposerClient;
    using aidl::android::hardware::graphics::composer3::DisplayIdentification;
    std::shared_ptr<AidlIComposer> mAidlComposer;
    std::shared_ptr<AidlIComposerClient> mAidlComposerClient;
    bool ret = false;
    DisplayIdentification id = {0};

    std::string instance_name =
            ::android::base::GetProperty(std::string("debug.sf.hwc_service_name"),
                                         std::string("default"));
    const std::string ComposerServiceName =
            std::string(AidlIComposer::descriptor) + "/" + instance_name;
    mAidlComposer = AidlIComposer::fromBinder(
            ndk::SpAIBinder(AServiceManager_waitForService(ComposerServiceName.c_str())));

    // set androidui overlay property
    if (!::android::base::SetProperty("vendor.androidui.overlay", "enable")) {
        ALOGE("HdmiCec set androidui overlay property enable failed.");
        goto finish;
    }

    // get edid from hwc composer
    if (mAidlComposer->createClient(&mAidlComposerClient).isOk()) {
        if (mAidlComposerClient->getDisplayIdentificationData(0, &id).isOk()) {
            uint8_t* outData = &(id.data)[0];
            ALOGV("id.port:%d,  id.data.size():%zu", id.port, id.data.size());
            for (unsigned long i = 0; i < id.data.size(); i = i + 16) {
                ALOGV("edid 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
                      outData[i], outData[i + 1], outData[i + 2], outData[i + 3], outData[i + 4],
                      outData[i + 5], outData[i + 6], outData[i + 7], outData[i + 8],
                      outData[i + 9], outData[i + 10], outData[i + 11], outData[i + 12],
                      outData[i + 13], outData[i + 14], outData[i + 15]);
            }
            if (outData[0x7e] != 1) {
                ALOGE("No Externsion blocks");
                goto finish;
            }
            // get physical address from edid Extension blocks
            // Check the Tag of each Data Block, start from Video Data
            // Video Data -> Audio Data -> Speaker Allocation -> Vendor-Specific
            uint8_t idx = 0x84;
            uint8_t idxDataLen = outData[idx] & 0x1f;
            if (((outData[idx] >> 5) & 0x7) == 2) {
                ALOGV("Video Data idx:0x%x,  value:0x%x,  dataLen:0x%x", idx, outData[idx],
                      idxDataLen);
                idx = idx + idxDataLen + 1;
                idxDataLen = outData[idx] & 0x1f;
            }
            if (((outData[idx] >> 5) & 0x7) == 1) {
                ALOGV("Audio Data idx:0x%x,  value:0x%x,  dataLen:0x%x", idx, outData[idx],
                      idxDataLen);
                idx = idx + idxDataLen + 1;
                idxDataLen = outData[idx] & 0x1f;
            }
            if (((outData[idx] >> 5) & 0x7) == 4) {
                ALOGV("Speaker Allocation idx:0x%x,  value:0x%x,  dataLen:0x%x", idx, outData[idx],
                      idxDataLen);
                idx = idx + idxDataLen + 1;
                idxDataLen = outData[idx] & 0x1f;
            }
            if (((outData[idx] >> 5) & 0x7) == 3) {
                ALOGV("Vendor-Specific idx:0x%x,  value:0x%x,  dataLen:0x%x", idx, outData[idx],
                      idxDataLen);
            } else {
                ALOGI("here just a workaround for the samsung TV, Add 3 bytes for idxVSDB");
                idx = idx + 3;
                idxDataLen = outData[idx] & 0x1f;
                ALOGV("Vendor-Specific idx:0x%x,  value:0x%x,  dataLen:0x%x", idx, outData[idx],
                      idxDataLen);
            }

            uint32_t HdmiIdentifier = 0x000C03;
            if ((HdmiIdentifier & 0x00ffffffff) !=
                (uint32_t)(outData[idx + 3] << 16 | outData[idx + 2] << 8 | outData[idx + 1])) {
                ALOGE("HdmiIdentifier check failed:0x%x %x %x", outData[idx + 3], outData[idx + 2],
                      outData[idx + 1]);
                goto finish;
            }

            uint8_t idxPhysicalMSB = idx + 4;
            uint8_t idxPhysicalLSB = idx + 5;
            ALOGV("PhysicalMSB:0x%x,  PhysicalLSB:0x%x", outData[idxPhysicalMSB],
                  outData[idxPhysicalLSB]);

            *phyaddr = (unsigned short)(outData[idxPhysicalMSB] << 8 | outData[idxPhysicalLSB]);
            ret = true;
        } else {
            ALOGE("getDisplayIdentificationData failed.");
            goto finish;
        }

    } else {
        ALOGE("Can't create AidlComposerClient");
        goto finish;
    }

finish:
    // set androidui overlay property
    if (!::android::base::SetProperty("vendor.androidui.overlay", "disable")) {
        ALOGE("HdmiCec set androidui overlay property disable failed.");
        return ret;
    }

    return ret;
}

HdmiCecMock::HdmiCecMock() {
    ALOGI("init the HDMI CEC HAL.");
    mCallback = nullptr;
    if (!getPhysicalAddrFromEdid(&mPhysicalAddress)) {
        mPhysicalAddress = 0x1000;
        ALOGE("getPhysicalAddrFromEdid failed. Fix the default input to hdmi1");
    }

    hdmi_cec_device_t* hdmi_cec_device;
    int ret = open_hdmi_cec(HDMI_CEC_HARDWARE_INTERFACE, TO_HW_DEVICE_T_OPEN(&hdmi_cec_device),
                            mPhysicalAddress);
    if (ret < 0) {
        ALOGE("failed to init the HDMI CEC HAL.");
    }
    mDevice = hdmi_cec_device;

    mDeathRecipient = ndk::ScopedAIBinder_DeathRecipient(AIBinder_DeathRecipient_new(serviceDied));
}

}  // namespace implementation
}  // namespace cec
}  // namespace hdmi
}  // namespace tv
}  // namespace hardware
}  // namespace android
