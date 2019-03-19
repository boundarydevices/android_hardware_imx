/*
 * Copyright (C) 2018 The Android Open Source Project
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

#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cutils/uevent.h>
#include <cutils/properties.h>
#include <cutils/log.h>
#include <errno.h>

#define UEVENT_MSG_LEN 2048
#define RPMSG_CAN_EVENT  "vendor.vehicle.event"
#define RPMSG_CAN_REGISTER "vendor.vehicle.register"
static void
handle_events(int uevent_fd)
{
    char msg[UEVENT_MSG_LEN+2];
    int n;
    int i;
    char *cp;
    n = uevent_kernel_multicast_recv(uevent_fd, msg, UEVENT_MSG_LEN);
    if (n <= 0 || n >= UEVENT_MSG_LEN) return;

    // add two '\0' which means this is the end of msg
    msg[n] = '\0';
    msg[n+1] = '\0';
    cp = msg;
    while (*cp) {
        if (!strncmp(cp, "STATE=VEHICLE_RPMSG_EVENT=0", strlen("STATE=VEHICLE_RPMSG_EVENT=0"))) {
            if (property_set(RPMSG_CAN_EVENT, "0") < 0)
                ALOGE("%s: could not set property RPMSG_CAN_EVENT", __FUNCTION__);
        } else if (!strncmp(cp, "STATE=VEHICLE_RPMSG_EVENT=1", strlen("STATE=VEHICLE_RPMSG_EVENT=1"))) {
            if (property_set(RPMSG_CAN_EVENT, "1") < 0)
                ALOGE("%s: could not set property RPMSG_CAN_EVENT", __FUNCTION__);
        } else if (!strncmp(cp, "STATE=VEHICLE_RPMSG_REGISTER=0", strlen("STATE=VEHICLE_RPMSG_REGISTER=0"))) {
            if (property_set(RPMSG_CAN_REGISTER, "0") < 0)
                ALOGE("%s: could not set property RPMSG_CAN_REGISTER", __FUNCTION__);
        } else if (!strncmp(cp, "STATE=VEHICLE_RPMSG_REGISTER=1", strlen("STATE=VEHICLERPMSG_REGISTER=1"))) {
            if (property_set(RPMSG_CAN_REGISTER, "1") < 0)
                ALOGE("%s: could not set property RPMSG_CAN_REGISTER", __FUNCTION__);
        }

        /* the format of msg is as below. it include "\0" which separate different info.
         * change@/devices/platform/imx_rpmsg/90100000.rpmsg1/virtio1/virtio1.rpmsg-vehicle-channel.-1.1/extcon/extcon2\0
         * ACTION=change\0
         * DEVPATH=/devices/platform/imx_rpmsg/90100000.rpmsg1/virtio1/virtio1.rpmsg-vehicle-channel.-1.1/extcon/extcon2\0
         * SUBSYSTEM=extcon\0
         * NAME=virtio1.rpmsg-vehicle-channel.-1.1\0
         * STATE=VEHICLERPMSG_EVENT=0\0
         */
        if (*cp) { cp += strlen(cp) + 1;}
    }
}

int main(int argc,char *argv[])
{
    int epoll_fd, uevent_fd;
    int nevents = 0;
    struct epoll_event ev;
    uevent_fd = uevent_open_socket(64 * 1024, true);
    if (uevent_fd < 0) {
        ALOGE("uevent_init: uevent_open_socket failed\n");
        return -1;
    }
    epoll_fd = epoll_create(64);
    if (epoll_fd == -1) {
        ALOGE("epoll_create failed; errno=%d", errno);
        return -1;
    }

    ev.events = EPOLLIN;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, uevent_fd, &ev) == -1) {
        ALOGE("epoll_ctl failed; errno=%d", errno);
        return -1;
    }

    for ( ; ; ) {
        struct epoll_event events[64];
        nevents = epoll_wait(epoll_fd, events, 64, -1);
        if (nevents < 0) {
            ALOGE("%s: wait the event failed", __FUNCTION__);
            continue;
        }
        for (int n = 0; n < nevents; ++n) {
            handle_events(uevent_fd);
        }
    }
    return 0;
}

