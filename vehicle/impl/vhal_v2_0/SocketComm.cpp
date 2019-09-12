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
#include <arpa/inet.h>
#include <log/log.h>
#include <linux/netlink.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "SocketComm.h"

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

SocketComm::SocketComm(MessageProcessor* messageProcessor)
    : mListenFd(-1), mMessageProcessor(messageProcessor) {}

SocketComm::~SocketComm() {
}

void SocketComm::start() {

    mListenThread = std::make_unique<std::thread>(std::bind(&SocketComm::listenThread, this));
}

void SocketComm::stop() {
    if (mListenFd > 0) {
        ::close(mListenFd);
        if (mListenThread->joinable()) {
            mListenThread->join();
        }
        mListenFd = -1;
    }
}

void SocketComm::sendMessage(emulator::EmulatorMessage const& msg) {
    std::lock_guard<std::mutex> lock(mMutex);
    for (std::unique_ptr<SocketConn> const& conn : mOpenConnections) {
        conn->sendMessage(msg);
    }
}

int SocketComm::listen() {
    int retVal;
    struct sockaddr_nl servAddr;

    mListenFd = socket(AF_NETLINK, SOCK_RAW, PROTOCOL_ID);
    if (mListenFd < 0) {
        ALOGE("%s: socket() failed, mSockFd=%d, errno=%d", __FUNCTION__, mListenFd, errno);
        mListenFd = -1;
        return mListenFd;
    }

    memset(&servAddr, 0, sizeof(servAddr));
    servAddr.nl_family = AF_NETLINK;
    servAddr.nl_pid = getpid();
    servAddr.nl_groups = 0;

    retVal = bind(mListenFd, (struct sockaddr *)&servAddr, sizeof(servAddr));
    if(retVal < 0) {
        ALOGE("%s: Error on binding: retVal=%d, errno=%d", __FUNCTION__, retVal, errno);
        close(mListenFd);
        mListenFd = -1;
        return mListenFd;
    }

    return mListenFd;
}

void SocketComm::listenThread() {
    int listenFd = listen();

    SocketConn* conn = new SocketConn(mMessageProcessor, listenFd);

    std::vector<uint8_t> msg = std::vector<uint8_t>(sizeof(SYNC_COMMANDS));
    memcpy(msg.data(), SYNC_COMMANDS, sizeof(SYNC_COMMANDS));
    conn->write(msg);

    conn->start();
    {
            std::lock_guard<std::mutex> lock(mMutex);
            mOpenConnections.push_back(std::unique_ptr<SocketConn>(conn));
    }
}

/**
 * Called occasionally to clean up connections that have been closed.
 */
void SocketComm::removeClosedConnections() {
    std::lock_guard<std::mutex> lock(mMutex);
    std::remove_if(mOpenConnections.begin(), mOpenConnections.end(),
                   [](std::unique_ptr<SocketConn> const& c) { return !c->isOpen(); });
}

SocketConn::SocketConn(MessageProcessor* messageProcessor, int sfd)
    : CommConn(messageProcessor), mSockFd(sfd) {}

std::vector<uint8_t> SocketConn::read() {
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
    if(ret < 0) {
        ALOGE("recv message failed \n");
        return std::vector<uint8_t>();
    } else {
        message_size = u_info.hdr.nlmsg_len - sizeof(struct nlmsghdr);
        std::vector<uint8_t> msg = std::vector<uint8_t> (message_size);
        memcpy(msg.data(), u_info.msg, message_size);
        return msg;
    }
}

void SocketConn::stop() {
    if (mSockFd > 0) {
        close(mSockFd);
        mSockFd = -1;
    }
}

int SocketConn::write(const std::vector<uint8_t>& data) {
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

