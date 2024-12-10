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

#define LOG_TAG "android.hardware.tv.hdmi.connection"
#include "HdmiConnectionMock.h"

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <fcntl.h>
#include <hardware/hdmi_cec.h>
#include <linux/cec.h>
#include <sys/ioctl.h>
#include <utils/Log.h>

#include "android-base/unique_fd.h"

using ndk::ScopedAStatus;

namespace android {
namespace hardware {
namespace tv {
namespace hdmi {
namespace connection {
namespace implementation {

void HdmiConnectionMock::serviceDied(void* cookie) {
    ALOGE("HdmiConnectionMock died");
    auto hdmi = static_cast<HdmiConnectionMock*>(cookie);
    hdmi->mHdmiThreadRun = false;
}

ScopedAStatus HdmiConnectionMock::getPortInfo(std::vector<HdmiPortInfo>* _aidl_return) {
    *_aidl_return = mPortInfos;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiConnectionMock::isConnected(int32_t portId, bool* _aidl_return) {
    // Maintain port connection status and update on hotplug event
    if (portId <= mTotalPorts && portId >= 1) {
        *_aidl_return = mPortConnectionStatus.at(portId - 1);
    } else {
        *_aidl_return = false;
    }

    return ScopedAStatus::ok();
}

ScopedAStatus HdmiConnectionMock::setCallback(
        const std::shared_ptr<IHdmiConnectionCallback>& callback) {
    if (mCallback != nullptr) {
        mCallback = nullptr;
    }

    if (callback != nullptr) {
        mCallback = callback;
        AIBinder_linkToDeath(this->asBinder().get(), mDeathRecipient.get(), 0 /* cookie */);
        pthread_create(&mThreadId, NULL, __threadLoop, this);
        pthread_setname_np(mThreadId, "hdmi_connection_loop");
    }
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiConnectionMock::setHpdSignal(HpdSignal signal, int32_t portId) {
    if (portId > mTotalPorts || portId < 1) {
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    if (!mHdmiThreadRun) {
        return ScopedAStatus::fromServiceSpecificError(
                static_cast<int32_t>(Result::FAILURE_INVALID_STATE));
    }
    mHpdSignal.at(portId - 1) = signal;
    return ScopedAStatus::ok();
}

ScopedAStatus HdmiConnectionMock::getHpdSignal(int32_t portId, HpdSignal* _aidl_return) {
    if (portId > mTotalPorts || portId < 1) {
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    *_aidl_return = mHpdSignal.at(portId - 1);
    return ScopedAStatus::ok();
}

void* HdmiConnectionMock::__threadLoop(void* user) {
    HdmiConnectionMock* const self = static_cast<HdmiConnectionMock*>(user);
    self->threadLoop();
    return 0;
}

int HdmiConnectionMock::readMessageFromFifo(unsigned char* buf, int msgCount) {
    if (msgCount <= 0 || !buf) {
        return 0;
    }

    int ret = -1;
    // Maybe blocked at driver
    ret = read(mInputFile, buf, msgCount);
    if (ret < 0) {
        ALOGE("read :%s failed, ret:%d\n", HDMI_MSG_IN_FIFO, ret);
        return -1;
    }

    return ret;
}

void HdmiConnectionMock::printEventBuf(const char* msg_buf, int len) {
    int i, size = 0;
    const int bufSize = MESSAGE_BODY_MAX_LENGTH * 3;
    // Use 2 characters for each byte in the message plus 1 space
    char buf[bufSize] = {0};

    // Messages longer than max length will be truncated.
    for (i = 0; i < len && size < bufSize; i++) {
        size += sprintf(buf + size, " %02x", msg_buf[i]);
    }
    ALOGD("%s, msg:%.*s", __FUNCTION__, size, buf);
}

void HdmiConnectionMock::handleHotplugMessage(unsigned char* msgBuf) {
    bool connected = ((msgBuf[3]) & 0xf) > 0;
    int32_t portId = static_cast<uint32_t>(msgBuf[0] & 0xf);

    if (portId > static_cast<int32_t>(mPortInfos.size()) || portId < 1) {
        ALOGD("ignore hot plug message, id %x does not exist", portId);
        return;
    }

    ALOGD("hot plug port id %x, is connected %x", (msgBuf[0] & 0xf), (msgBuf[3] & 0xf));
    mPortConnectionStatus.at(portId - 1) = connected;
    if (mPortInfos.at(portId - 1).type == HdmiPortType::OUTPUT) {
        mPhysicalAddress = (connected ? CEC_PHYS_ADDR_INVALID : ((msgBuf[1] << 8) | (msgBuf[2])));
        mPortInfos.at(portId - 1).physicalAddress = mPhysicalAddress;
        ALOGD("hot plug physical address %x", mPhysicalAddress);
    }

    if (mCallback != nullptr) {
        mCallback->onHotplugEvent(connected, portId);
    }
}

void HdmiConnectionMock::threadLoop() {
    ALOGD("threadLoop start.");
    // Open the cec node
    char* path = (char*)"/dev/cec0";
    base::unique_fd cecFd(::open(path, O_RDWR | O_NONBLOCK));
    if (cecFd.get() < 0) {
        ALOGE("faild to open %s, ret=%s\n", path, strerror(errno));
        return;
    }
    ALOGD("file open ok, fd = %d. path=%s", cecFd.get(), path);

    while (mHdmiThreadRun) {
        struct timeval tv = {1, 0};
        fd_set exFds;
        FD_ZERO(&exFds);
        FD_SET(cecFd.get(), &exFds);
        int res = select(cecFd.get() + 1, nullptr, nullptr, &exFds, &tv);
        if (res < 0)
            break;
        // CEC event
        if (FD_ISSET(cecFd.get(), &exFds)) {
            struct cec_event ev;
            if (ioctl(cecFd.get(), CEC_DQEVENT, &ev))
                continue;

            uint16_t phyaddr = ev.state_change.phys_addr;
            ALOGD("ev.event:%d,  phyaddr:0x%x, origin mPhysicalAddress:0x%x", ev.event, phyaddr,
                  mPhysicalAddress);
            if (phyaddr == mPhysicalAddress || phyaddr == CEC_PHYS_ADDR_INVALID) {
                ALOGE("the same with before or invalid, drop this phyaddr:0x%x", phyaddr);
                continue;
            }
            // update the plug info
            bool connected = (phyaddr == CEC_PHYS_ADDR_INVALID) ? false : true;
            mPortConnectionStatus.at(mPortId - 1) = connected;
            if (mPortInfos.at(mPortId - 1).type == HdmiPortType::OUTPUT) {
                mPhysicalAddress = connected ? phyaddr : CEC_PHYS_ADDR_INVALID;
                mPortInfos.at(mPortId - 1).physicalAddress = mPhysicalAddress;
                ALOGD("hot plug physical address: 0x%x", mPhysicalAddress);
            }

            if (mCallback != nullptr) {
                mCallback->onHotplugEvent(connected, mPortId);
            }
        }
    }
    ALOGD("thread end.");
}

bool HdmiConnectionMock::getPhysicalAddrFromEdid(uint16_t* phyaddr) {
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

HdmiConnectionMock::HdmiConnectionMock() {
    ALOGI("init the HDMI Connection HAL.");
    mCallback = nullptr;
    if (!getPhysicalAddrFromEdid(&mPhysicalAddress)) {
        mPhysicalAddress = 0x1000;
        ALOGE("getPhysicalAddrFromEdid failed. Fix the default input to hdmi1");
    }

    mPortInfos.resize(mTotalPorts);
    mPortConnectionStatus.resize(mTotalPorts);
    mHpdSignal.resize(mTotalPorts);
    mPortInfos[0] = {.type = HdmiPortType::OUTPUT,
                     .portId = mPortId,
                     .cecSupported = true,
                     .arcSupported = false,
                     .eArcSupported = false,
                     .physicalAddress = mPhysicalAddress};
    mPortConnectionStatus[0] = mPhysicalAddress == CEC_PHYS_ADDR_INVALID ? false : true;
    mHpdSignal[0] = HpdSignal::HDMI_HPD_PHYSICAL;
    mDeathRecipient = ndk::ScopedAIBinder_DeathRecipient(AIBinder_DeathRecipient_new(serviceDied));
}

}  // namespace implementation
}  // namespace connection
}  // namespace hdmi
}  // namespace tv
}  // namespace hardware
}  // namespace android
