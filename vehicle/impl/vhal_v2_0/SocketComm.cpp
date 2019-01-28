/*
 * Copyright (C) 2017 The Android Open Source Project
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

#define LOG_TAG "SocketComm"

#include <android/hardware/automotive/vehicle/2.0/IVehicle.h>
#include <android/log.h>
#include <log/log.h>
#include <linux/netlink.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "SocketComm.h"

// Socket to use when communicating with Host PC
typedef struct user_msg_info {
        struct nlmsghdr hdr;
        char  msg[1024];
} user_socket_info;

#define MAX_PLOAD 1024
#define PROTOCOL_ID 30

#define SYNC_COMMANDS "sync"

namespace android {
namespace hardware {
namespace automotive {
namespace vehicle {
namespace V2_0 {

namespace impl {

SocketComm::SocketComm() {
    // Initialize member vars
    mCurSockFd = -1;
    mExit      =  0;
    mSockFd    = -1;
}


SocketComm::~SocketComm() {
    stop();
}

int SocketComm::connect() {
    sockaddr_in cliAddr;
    socklen_t cliLen = sizeof(cliAddr);
    int cSockFd = accept(mSockFd, reinterpret_cast<struct sockaddr*>(&cliAddr), &cliLen);

    if (cSockFd >= 0) {
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mCurSockFd = cSockFd;
        }
        ALOGD("%s: Incoming connection received on socket %d", __FUNCTION__, cSockFd);
    } else {
        cSockFd = -1;
    }

    return cSockFd;
}

int SocketComm::open() {
    int retVal;
    struct sockaddr_nl servAddr;

    mSockFd = socket(AF_NETLINK, SOCK_RAW, PROTOCOL_ID);
    if (mSockFd < 0) {
        ALOGE("%s: socket() failed, mSockFd=%d, errno=%d", __FUNCTION__, mSockFd, errno);
        mSockFd = -1;
        return -errno;
    }

    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.nl_family = AF_NETLINK;
    servAddr.nl_pid = getpid();
    servAddr.nl_groups = 0;

    retVal = bind(mSockFd, (struct sockaddr *)&servAddr, sizeof(servAddr));
    if(retVal < 0) {
        ALOGE("%s: Error on binding: retVal=%d, errno=%d", __FUNCTION__, retVal, errno);
        close(mSockFd);
        return -errno;
    }

    std::vector<uint8_t> msg = std::vector<uint8_t>(sizeof(SYNC_COMMANDS));
    memcpy(msg.data(), SYNC_COMMANDS, sizeof(SYNC_COMMANDS));
    write(msg);

    return 0;
}

std::vector<uint8_t> SocketComm::read() {
    int ret;
    user_socket_info u_info;
    struct sockaddr_nl nl_socket;
    socklen_t nl_socket_len = sizeof(nl_socket);
    int message_size;

    memset(&nl_socket, 0, sizeof(nl_socket));
    memset(&u_info, 0, sizeof(user_socket_info));
    nl_socket.nl_family = AF_NETLINK;
    nl_socket.nl_pid = 0;
    nl_socket.nl_groups = 0;

    ret = recvfrom(mSockFd, &u_info, sizeof(u_info), 0, (struct sockaddr *)&nl_socket, &nl_socket_len);

    message_size = u_info.hdr.nlmsg_len - sizeof(struct nlmsghdr);
    if(ret < 0) {
        ALOGE("recv message failed \n");
        return std::vector<uint8_t>();
    } else {
        std::vector<uint8_t> msg = std::vector<uint8_t> (message_size);
        memcpy(msg.data(), u_info.msg, message_size);
        return msg;
    }
}

void SocketComm::stop() {
    if (mExit == 0) {
        std::lock_guard<std::mutex> lock(mMutex);
        mExit = 1;

        // Close emulator socket if it is open
        if (mCurSockFd != -1) {
            close(mCurSockFd);
            mCurSockFd = -1;
        }

        if (mSockFd != -1) {
            close(mSockFd);
            mSockFd = -1;
        }
    }
}

int SocketComm::write(const std::vector<uint8_t>& data) {
    int ret;
    struct sockaddr_nl daddr;
    struct nlmsghdr *nlh = NULL;
    memset(&daddr, 0, sizeof(daddr));
    daddr.nl_family = AF_NETLINK;
    // 0 means this message is to kernel
    daddr.nl_pid = 0;
    daddr.nl_groups = 0;

    nlh = (struct nlmsghdr *)malloc(NLMSG_SPACE(MAX_PLOAD));
    memset(nlh, 0, sizeof(struct nlmsghdr));
    nlh->nlmsg_len = NLMSG_LENGTH(data.size());
    nlh->nlmsg_flags = 0;
    nlh->nlmsg_type = 0;
    nlh->nlmsg_seq = 0;
    nlh->nlmsg_pid = getpid();
    memcpy(NLMSG_DATA(nlh), data.data(), data.size());
    ret = sendto(mSockFd, nlh, nlh->nlmsg_len, 0, (struct sockaddr *)&daddr, sizeof(struct sockaddr_nl));
    if(!ret) {
        ALOGE("send message failed.\n");
        return -1;
    }

    return 0;
}


}  // impl

}  // namespace V2_0
}  // namespace vehicle
}  // namespace automotive
}  // namespace hardware
}  // namespace android

