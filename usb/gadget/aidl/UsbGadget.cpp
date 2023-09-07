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

#define LOG_TAG "android.hardware.usb.gadget.aidl-service"

#include "UsbGadget.h"

#include <aidl/android/frameworks/stats/IStats.h>
#include <android-base/parsebool.h>
#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/inotify.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace aidl {
namespace android {
namespace hardware {
namespace usb {
namespace gadget {

#define STOP_MONITOR_FLAG 100

volatile bool gadgetPullup;

// Used for debug.
static void displayInotifyEvent(struct inotify_event *i) {
    ALOGE("    wd =%2d; ", i->wd);
    if (i->cookie > 0) ALOGE("cookie =%4d; ", i->cookie);

    ALOGE("mask = ");
    if (i->mask & IN_ACCESS) ALOGE("IN_ACCESS ");
    if (i->mask & IN_ATTRIB) ALOGE("IN_ATTRIB ");
    if (i->mask & IN_CLOSE_NOWRITE) ALOGE("IN_CLOSE_NOWRITE ");
    if (i->mask & IN_CLOSE_WRITE) ALOGE("IN_CLOSE_WRITE ");
    if (i->mask & IN_CREATE) ALOGE("IN_CREATE ");
    if (i->mask & IN_DELETE) ALOGE("IN_DELETE ");
    if (i->mask & IN_DELETE_SELF) ALOGE("IN_DELETE_SELF ");
    if (i->mask & IN_IGNORED) ALOGE("IN_IGNORED ");
    if (i->mask & IN_ISDIR) ALOGE("IN_ISDIR ");
    if (i->mask & IN_MODIFY) ALOGE("IN_MODIFY ");
    if (i->mask & IN_MOVE_SELF) ALOGE("IN_MOVE_SELF ");
    if (i->mask & IN_MOVED_FROM) ALOGE("IN_MOVED_FROM ");
    if (i->mask & IN_MOVED_TO) ALOGE("IN_MOVED_TO ");
    if (i->mask & IN_OPEN) ALOGE("IN_OPEN ");
    if (i->mask & IN_Q_OVERFLOW) ALOGE("IN_Q_OVERFLOW ");
    if (i->mask & IN_UNMOUNT) ALOGE("IN_UNMOUNT ");
    ALOGE("\n");

    if (i->len > 0) ALOGE("        name = %s\n", i->name);
}

static void *monitorFfs(void *param) {
    UsbGadget *usbGadget = (UsbGadget *)param;
    char buf[BUFFER_SIZE];
    bool writeUdc = true, stopMonitor = false;
    struct epoll_event events[EPOLL_EVENTS];
    steady_clock::time_point disconnect;

    while (!stopMonitor) {
        bool descriptorWritten = true;
        for (int i = 0; i < static_cast<int>(usbGadget->mEndpointList.size()); i++) {
            if (access(usbGadget->mEndpointList.at(i).c_str(), R_OK)) {
                descriptorWritten = false;
                break;
            }
        }

        // notify here if the endpoints are already present.
        if (descriptorWritten) {
            usleep(PULL_UP_DELAY);
            if (!access(string("/sys/class/udc/" + GADGET_NAME).c_str(), F_OK)) {
                if (!!WriteStringToFile(GADGET_NAME, PULLUP_PATH)) {
                    lock_guard<mutex> lock(usbGadget->mLock);
                    usbGadget->mCurrentUsbFunctionsApplied = true;
                    gadgetPullup = true;
                    writeUdc = false;
                    ALOGI("GADGET pulled up");
                    usbGadget->mCv.notify_all();
                }
            }
        }
        int nrEvents =
                epoll_wait(usbGadget->mEpollFd, events, EPOLL_EVENTS, gadgetPullup ? -1 : 2000);
        if (nrEvents <= 0) {
            continue;
        }

        for (int i = 0; i < nrEvents; i++) {
            ALOGI("event=%u on fd=%d\n", events[i].events, events[i].data.fd);

            if (events[i].data.fd == usbGadget->mInotifyFd) {
                // Process all of the events in buffer returned by read().
                int numRead = read(usbGadget->mInotifyFd, buf, BUFFER_SIZE);
                for (char *p = buf; p < buf + numRead;) {
                    struct inotify_event *event = (struct inotify_event *)p;
                    if (DEBUG) displayInotifyEvent(event);

                    p += sizeof(struct inotify_event) + event->len;

                    bool descriptorPresent = true;
                    for (int j = 0; j < static_cast<int>(usbGadget->mEndpointList.size()); j++) {
                        if (access(usbGadget->mEndpointList.at(j).c_str(), R_OK)) {
                            if (DEBUG) ALOGI("%s absent", usbGadget->mEndpointList.at(j).c_str());
                            descriptorPresent = false;
                            break;
                        }
                    }

                    if (!descriptorPresent && !writeUdc) {
                        if (DEBUG) ALOGI("endpoints not up");
                        writeUdc = true;
                        disconnect = std::chrono::steady_clock::now();
                    } else if (descriptorPresent && writeUdc) {
                        steady_clock::time_point temp = steady_clock::now();

                        if (std::chrono::duration_cast<microseconds>(temp - disconnect).count() <
                            PULL_UP_DELAY)
                            usleep(PULL_UP_DELAY);

                        if (!access(string("/sys/class/udc/" + GADGET_NAME).c_str(), F_OK)) {
                            if (!!WriteStringToFile(GADGET_NAME, PULLUP_PATH)) {
                                lock_guard<mutex> lock(usbGadget->mLock);
                                usbGadget->mCurrentUsbFunctionsApplied = true;
                                ALOGI("GADGET pulled up");
                                writeUdc = false;
                                gadgetPullup = true;
                                // notify the main thread to signal userspace.
                                usbGadget->mCv.notify_all();
                            }
                        }
                    }
                }
            } else {
                uint64_t flag;
                int numRead = read(usbGadget->mEventFd, &flag, sizeof(flag));
                if (numRead < 0) {
                    ALOGE("Error readding event fd");
                }
                if (flag == STOP_MONITOR_FLAG) {
                    stopMonitor = true;
                    break;
                }
            }
        }
    }
    return NULL;
}

static void *monitorUsbSysfsPath() {
    while (true) {
        if (!access(string("/sys/class/udc/" + GADGET_NAME).c_str(), F_OK)) {
            if (::android::base::ParseBool(GetProperty("vendor.sys.usb.adb.disabled", "")) !=
                ::android::base::ParseBoolResult::kFalse)
                SetProperty("vendor.sys.usb.adb.disabled", "0");
        } else {
            if (::android::base::ParseBool(GetProperty("vendor.sys.usb.adb.disabled", "")) !=
                ::android::base::ParseBoolResult::kTrue)
                SetProperty("vendor.sys.usb.adb.disabled", "1");
        }
        std::this_thread::sleep_for(1s);
    }
    return NULL;
}

UsbGadget::UsbGadget() : mMonitorCreated(false), mCurrentUsbFunctionsApplied(false) {
    mCurrentUsbFunctions = GadgetFunction::NONE;
    if (access(OS_DESC_PATH, R_OK) != 0) ALOGE("configfs setup not done yet");
    std::thread(monitorUsbSysfsPath).detach();
}

static int unlinkFunctions(const char *path) {
    DIR *config = opendir(path);
    struct dirent *function;
    char filepath[MAX_FILE_PATH_LENGTH];
    int ret = 0;

    if (config == NULL) return -1;

    // d_type does not seems to be supported in /config
    // so filtering by name.
    while (((function = readdir(config)) != NULL)) {
        if ((strstr(function->d_name, FUNCTION_NAME) == NULL)) continue;
        // build the path for each file in the folder.
        sprintf(filepath, "%s/%s", path, function->d_name);
        ret = remove(filepath);
        if (ret) {
            ALOGE("Unable  remove file %s errno:%d", filepath, errno);
            break;
        }
    }

    closedir(config);
    return ret;
}

static int addEpollFd(const unique_fd &epfd, const unique_fd &fd) {
    struct epoll_event event;
    int ret;

    event.data.fd = fd;
    event.events = EPOLLIN;

    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &event);
    if (ret) ALOGE("epoll_ctl error %d", errno);

    return ret;
}

ScopedAStatus UsbGadget::getCurrentUsbFunctions(const shared_ptr<IUsbGadgetCallback> &callback,
                                                int64_t in_transactionId) {
    if (callback == nullptr) {
        return ScopedAStatus::fromExceptionCode(EX_NULL_POINTER);
    }
    ScopedAStatus ret = callback->getCurrentUsbFunctionsCb(mCurrentUsbFunctions,
                                                           mCurrentUsbFunctionsApplied
                                                                   ? Status::FUNCTIONS_APPLIED
                                                                   : Status::FUNCTIONS_NOT_APPLIED,
                                                           in_transactionId);
    if (!ret.isOk())
        ALOGE("Call to getCurrentUsbFunctionsCb failed %s", ret.getDescription().c_str());

    return ScopedAStatus::ok();
}

ScopedAStatus UsbGadget::getUsbSpeed(const shared_ptr<IUsbGadgetCallback> &callback,
                                     int64_t in_transactionId) {
    std::string current_speed;
    if (ReadFileToString(string("/sys/class/udc/" + GADGET_NAME + SPEED_PATH).c_str(),
                         &current_speed)) {
        current_speed = Trim(current_speed);
        ALOGI("current USB speed is %s", current_speed.c_str());
        if (current_speed == "low-speed")
            mUsbSpeed = UsbSpeed::LOWSPEED;
        else if (current_speed == "full-speed")
            mUsbSpeed = UsbSpeed::FULLSPEED;
        else if (current_speed == "high-speed")
            mUsbSpeed = UsbSpeed::HIGHSPEED;
        else if (current_speed == "super-speed")
            mUsbSpeed = UsbSpeed::SUPERSPEED;
        else if (current_speed == "super-speed-plus")
            mUsbSpeed = UsbSpeed::SUPERSPEED_10Gb;
        else if (current_speed == "UNKNOWN")
            mUsbSpeed = UsbSpeed::UNKNOWN;
        else
            mUsbSpeed = UsbSpeed::UNKNOWN;
    } else {
        ALOGE("Fail to read current speed");
        mUsbSpeed = UsbSpeed::UNKNOWN;
    }

    if (callback) {
        ScopedAStatus ret = callback->getUsbSpeedCb(mUsbSpeed, in_transactionId);

        if (!ret.isOk()) ALOGE("Call to getUsbSpeedCb failed %s", ret.getDescription().c_str());
    }

    return ScopedAStatus::ok();
}

Status UsbGadget::tearDownGadget() {
    ALOGI("tear down the gadget");

    if (!WriteStringToFile("none", PULLUP_PATH)) ALOGI("Gadget cannot be pulled down");

    if (!WriteStringToFile("0", DEVICE_CLASS_PATH)) return Status::ERROR;
    if (!WriteStringToFile("0", DEVICE_SUB_CLASS_PATH)) return Status::ERROR;
    if (!WriteStringToFile("0", DEVICE_PROTOCOL_PATH)) return Status::ERROR;
    if (!WriteStringToFile("0", DESC_USE_PATH)) return Status::ERROR;

    if (mMonitorCreated) {
        uint64_t flag = STOP_MONITOR_FLAG;
        ssize_t ret;

        // Stop the monitor thread by writing into signal fd.
        ret = TEMP_FAILURE_RETRY(write(mEventFd, &flag, sizeof(flag)));
        if (ret < 0) {
            ALOGE("Error writing errno=%d", errno);
        } else if (ret < sizeof(flag)) {
            ALOGE("Short write length=%zd", ret);
        }

        ALOGI("mMonitor signalled to exit");
        mMonitor->join();
        mMonitorCreated = false;
        ALOGI("mMonitor destroyed");
    } else {
        ALOGI("mMonitor not running");
    }

    if (unlinkFunctions(CONFIG_PATH)) return Status::ERROR;

    mInotifyFd.reset(-1);
    mEventFd.reset(-1);
    mEpollFd.reset(-1);
    mEndpointList.clear();

    return Status::SUCCESS;
}

ScopedAStatus UsbGadget::reset(const shared_ptr<IUsbGadgetCallback> &callback,
                               int64_t in_transactionId) {
    if (!WriteStringToFile("none", PULLUP_PATH)) {
        ALOGI("Gadget cannot be pulled down");
        return ScopedAStatus::fromServiceSpecificError(-1);
    }
    usleep(DISCONNECT_WAIT_US);

    if (!WriteStringToFile(GADGET_NAME, PULLUP_PATH)) {
        ALOGI("Gadget cannot be pulled up");
        return ScopedAStatus::fromServiceSpecificError(-1);
    }

    if (callback) callback->resetCb(Status::SUCCESS, in_transactionId);

    return ScopedAStatus::ok();
}

static int linkFunction(const char *function, int index) {
    char functionPath[MAX_FILE_PATH_LENGTH];
    char link[MAX_FILE_PATH_LENGTH];

    sprintf(functionPath, "%s%s", FUNCTIONS_PATH, function);
    sprintf(link, "%s%d", FUNCTION_PATH, index);
    if (symlink(functionPath, link)) {
        ALOGE("Cannot create symlink %s -> %s errno:%d", link, functionPath, errno);
        return -1;
    }
    return 0;
}

static Status setVidPid(const char *vid, const char *pid) {
    if (!WriteStringToFile(vid, VENDOR_ID_PATH)) return Status::ERROR;

    if (!WriteStringToFile(pid, PRODUCT_ID_PATH)) return Status::ERROR;

    return Status::SUCCESS;
}

static Status validateAndSetVidPid(int64_t functions) {
    Status ret = Status::SUCCESS;

    switch (functions) {
        case GadgetFunction::MTP:
            WriteStringToFile("mtp", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x4ee1");
            break;
        case GadgetFunction::ADB | GadgetFunction::MTP:
            WriteStringToFile("adb|mtp", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x4ee2");
            break;
        case GadgetFunction::RNDIS:
            WriteStringToFile("rndis", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x4ee3");
            break;
        case GadgetFunction::ADB | GadgetFunction::RNDIS:
            WriteStringToFile("adb|rndis", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x4ee4");
            break;
        case GadgetFunction::PTP:
            WriteStringToFile("ptp", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x4ee5");
            break;
        case GadgetFunction::ADB | GadgetFunction::PTP:
            WriteStringToFile("adb|ptp", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x4ee6");
            break;
        case GadgetFunction::ADB:
            WriteStringToFile("adb", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x4ee7");
            break;
        case GadgetFunction::MIDI:
            WriteStringToFile("midi", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x4ee8");
            break;
        case GadgetFunction::ADB | GadgetFunction::MIDI:
            WriteStringToFile("adb|midi", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x4ee9");
            break;
        case GadgetFunction::NCM:
            WriteStringToFile("ncm", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x4eeb");
            break;
        case GadgetFunction::ADB | GadgetFunction::NCM:
            WriteStringToFile("adb|ncm", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x4eec");
            break;
        case GadgetFunction::ADB | GadgetFunction::UVC:
            WriteStringToFile("adb|uvc", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x4eed");
            break;
        case GadgetFunction::UVC:
            WriteStringToFile("uvc", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x4eef");
            break;
        case GadgetFunction::ACCESSORY:
            WriteStringToFile("accessory", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x2d00");
            break;
        case GadgetFunction::ADB | GadgetFunction::ACCESSORY:
            WriteStringToFile("adb|accessory", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x2d01");
            break;
        case GadgetFunction::AUDIO_SOURCE:
            WriteStringToFile("audio_source", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x2d02");
            break;
        case GadgetFunction::ADB | GadgetFunction::AUDIO_SOURCE:
            WriteStringToFile("adb|audio_source", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x2d03");
            break;
        case GadgetFunction::ACCESSORY | GadgetFunction::AUDIO_SOURCE:
            WriteStringToFile("accessory|audio_source", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x2d04");
            break;
        case GadgetFunction::ADB | GadgetFunction::ACCESSORY | GadgetFunction::AUDIO_SOURCE:
            WriteStringToFile("adb|accessory|audio_source", CONFIGURATION_PATH);
            ret = setVidPid("0x18d1", "0x2d05");
            break;
        default:
            ALOGE("Combination not supported");
            ret = Status::CONFIGURATION_NOT_SUPPORTED;
    }
    return ret;
}

Status UsbGadget::setupFunctions(long functions, const shared_ptr<IUsbGadgetCallback> &callback,
                                 uint64_t timeout, int64_t in_transactionId) {
    std::unique_lock<std::mutex> lk(mLock);

    unique_fd inotifyFd(inotify_init());
    if (inotifyFd < 0) {
        ALOGE("inotify init failed");
        return Status::ERROR;
    }

    bool ffsEnabled = false;
    if (timeout == 0) {
        ALOGI("timeout not setup");
    }

    int i = 0;

    if (((functions & GadgetFunction::MTP) != 0)) {
        ffsEnabled = true;
        ALOGI("setCurrentUsbFunctions mtp");
        if (!WriteStringToFile("1", DESC_USE_PATH)) return Status::ERROR;

        if (inotify_add_watch(inotifyFd, "/dev/usb-ffs/mtp/", IN_ALL_EVENTS) == -1)
            return Status::ERROR;

        if (linkFunction("ffs.mtp", i++)) return Status::ERROR;

        // Add endpoints to be monitored.
        mEndpointList.push_back("/dev/usb-ffs/mtp/ep1");
        mEndpointList.push_back("/dev/usb-ffs/mtp/ep2");
        mEndpointList.push_back("/dev/usb-ffs/mtp/ep3");
    } else if (((functions & GadgetFunction::PTP) != 0)) {
        ffsEnabled = true;
        ALOGI("setCurrentUsbFunctions ptp");
        if (!WriteStringToFile("1", DESC_USE_PATH)) return Status::ERROR;

        if (inotify_add_watch(inotifyFd, "/dev/usb-ffs/ptp/", IN_ALL_EVENTS) == -1)
            return Status::ERROR;

        if (linkFunction("ffs.ptp", i++)) return Status::ERROR;

        // Add endpoints to be monitored.
        mEndpointList.push_back("/dev/usb-ffs/ptp/ep1");
        mEndpointList.push_back("/dev/usb-ffs/ptp/ep2");
        mEndpointList.push_back("/dev/usb-ffs/ptp/ep3");
    }

    if ((functions & GadgetFunction::MIDI) != 0) {
        ALOGI("setCurrentUsbFunctions MIDI");
        if (linkFunction("midi.gs5", i++)) return Status::ERROR;
    }

    if ((functions & GadgetFunction::ACCESSORY) != 0) {
        ALOGI("setCurrentUsbFunctions Accessory");
        if (linkFunction("accessory.gs2", i++)) return Status::ERROR;
    }

    if ((functions & GadgetFunction::AUDIO_SOURCE) != 0) {
        ALOGI("setCurrentUsbFunctions Audio Source");
        if (linkFunction("audio_source.gs3", i++)) return Status::ERROR;
    }

    if ((functions & GadgetFunction::RNDIS) != 0) {
        ALOGI("setCurrentUsbFunctions rndis");
        if (linkFunction("rndis.gs4", i++)) return Status::ERROR;
    }

    if ((functions & GadgetFunction::NCM) != 0) {
        ALOGE("setCurrentUsbFunctions ncm");
        if (linkFunction("ncm.gs6", i++)) return Status::ERROR;
    }

    if ((functions & GadgetFunction::ADB) != 0) {
        ffsEnabled = true;
        ALOGI("setCurrentUsbFunctions Adb");
        if (!WriteStringToFile("1", DESC_USE_PATH)) return Status::ERROR;
        if (inotify_add_watch(inotifyFd, "/dev/usb-ffs/adb/", IN_ALL_EVENTS) == -1)
            return Status::ERROR;

        if (linkFunction("ffs.adb", i++)) return Status::ERROR;
        mEndpointList.push_back("/dev/usb-ffs/adb/ep1");
        mEndpointList.push_back("/dev/usb-ffs/adb/ep2");
        ALOGI("Service started");
    }

    if ((functions & GadgetFunction::UVC) != 0) {
        ALOGI("setCurrentUsbFunctions uvc");
        if (linkFunction("uvc.0", i++)) return Status::ERROR;
    }

    // Pull up the gadget right away when there are no ffs functions.
    if (!ffsEnabled) {
        if (!WriteStringToFile(GADGET_NAME, PULLUP_PATH)) return Status::ERROR;
        mCurrentUsbFunctionsApplied = true;
        if (callback)
            callback->setCurrentUsbFunctionsCb(functions, Status::SUCCESS, in_transactionId);
        return Status::SUCCESS;
    }

    unique_fd eventFd(eventfd(0, 0));
    if (eventFd == -1) {
        ALOGE("mEventFd failed to create %d", errno);
        return Status::ERROR;
    }

    unique_fd epollFd(epoll_create(2));
    if (epollFd == -1) {
        ALOGE("mEpollFd failed to create %d", errno);
        return Status::ERROR;
    }

    if (addEpollFd(epollFd, inotifyFd) == -1) return Status::ERROR;
    if (addEpollFd(epollFd, eventFd) == -1) return Status::ERROR;

    mEpollFd = move(epollFd);
    mInotifyFd = move(inotifyFd);
    mEventFd = move(eventFd);
    gadgetPullup = false;

    // Monitors the ffs paths to pull up the gadget when descriptors are written.
    // Also takes of the pulling up the gadget again if the userspace process
    // dies and restarts.
    mMonitor = unique_ptr<thread>(new thread(monitorFfs, this));
    mMonitorCreated = true;
    if (DEBUG) ALOGI("Mainthread in Cv");

    if (callback) {
        if (mCv.wait_for(lk, timeout * 1ms, [] { return gadgetPullup; })) {
            ALOGI("monitorFfs signalled true");
        } else {
            ALOGI("monitorFfs signalled error");
            // continue monitoring as the descriptors might be written at a later
            // point.
        }
        ScopedAStatus ret =
                callback->setCurrentUsbFunctionsCb(functions,
                                                   gadgetPullup ? Status::SUCCESS : Status::ERROR,
                                                   in_transactionId);
        if (!ret.isOk()) ALOGE("setCurrentUsbFunctionsCb error %s", ret.getDescription().c_str());
    }

    return Status::SUCCESS;
}

ScopedAStatus UsbGadget::setCurrentUsbFunctions(int64_t functions,
                                                const shared_ptr<IUsbGadgetCallback> &callback,
                                                int64_t timeoutMs, int64_t in_transactionId) {
    std::unique_lock<std::mutex> lk(mLockSetCurrentFunction);

    mCurrentUsbFunctions = functions;
    mCurrentUsbFunctionsApplied = false;

    // Unlink the gadget and stop the monitor if running.
    Status status = tearDownGadget();
    if (status != Status::SUCCESS) {
        goto error;
    }

    ALOGI("Returned from tearDown gadget");

    if ((functions & GadgetFunction::RNDIS) == 0) {
        if (rmdir(RNDIS_PATH) && errno != ENOENT) ALOGE("Error remove %s", RNDIS_PATH);
    } else if ((functions & GadgetFunction::RNDIS)) {
        if (mkdir(RNDIS_PATH, 644) && errno != EEXIST) goto error;
    }

    // Leave the gadget pulled down to give time for the host to sense disconnect.
    usleep(DISCONNECT_WAIT_US);

    if (functions == GadgetFunction::NONE) {
        if (callback == NULL)
            return ScopedAStatus::fromServiceSpecificErrorWithMessage(-1, "callback == NULL");
        ScopedAStatus ret =
                callback->setCurrentUsbFunctionsCb(functions, Status::SUCCESS, in_transactionId);
        if (!ret.isOk())
            ALOGE("Error while calling setCurrentUsbFunctionsCb %s", ret.getDescription().c_str());
        return ScopedAStatus::
                fromServiceSpecificErrorWithMessage(-1,
                                                    "Error while calling setCurrentUsbFunctionsCb");
    }

    status = validateAndSetVidPid(functions);
    if (status != Status::SUCCESS) {
        goto error;
    }

    status = setupFunctions(functions, callback, timeoutMs, in_transactionId);
    if (status != Status::SUCCESS) {
        goto error;
    }

    ALOGI("Usb Gadget setcurrent functions called successfully");
    return ScopedAStatus::fromServiceSpecificErrorWithMessage(-1,
                                                              "Usb Gadget setcurrent functions "
                                                              "called successfully");

error:
    ALOGI("Usb Gadget setcurrent functions failed");
    if (callback == NULL)
        return ScopedAStatus::
                fromServiceSpecificErrorWithMessage(-1, "Usb Gadget setcurrent functions failed");
    ScopedAStatus ret = callback->setCurrentUsbFunctionsCb(functions, status, in_transactionId);
    if (!ret.isOk())
        ALOGE("Error while calling setCurrentUsbFunctionsCb %s", ret.getDescription().c_str());
    return ScopedAStatus::
            fromServiceSpecificErrorWithMessage(-1, "Error while calling setCurrentUsbFunctionsCb");
}
} // namespace gadget
} // namespace usb
} // namespace hardware
} // namespace android
} // namespace aidl
