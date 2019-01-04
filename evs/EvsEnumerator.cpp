/*
 * Copyright (C) 2016 The Android Open Source Project
 * Copyright 2019 NXP.
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

#include "EvsEnumerator.h"
#include "V4l2Capture.h"
#include "FakeCapture.h"
#include "EvsDisplay.h"

#include <cutils/properties.h>
#include <dirent.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>

namespace android {
namespace hardware {
namespace automotive {
namespace evs {
namespace V1_0 {
namespace implementation {

#define HWC_PATH_LENGTH 64
#define BUFFER_SIZE 512
#define EPOLL_MAX_EVENTS 8
#define MEDIA_FILE_PATH "/dev"
#define EVS_VIDEO_READY "vendor.evs.video.ready"
#define EVS_FAKE_SENSOR "mxc_isi.0.capture"
#define EVS_FAKE_NAME   "fake.camera"
#define EVS_FAKE_PROP   "vendor.evs.fake.enable"

// NOTE:  All members values are static so that all clients operate on the same state
//        That is to say, this is effectively a singleton despite the fact that HIDL
//        constructs a new instance for each client.
std::list<EvsEnumerator::CameraRecord>   EvsEnumerator::sCameraList;
wp<EvsDisplay>                           EvsEnumerator::sActiveDisplay;


bool EvsEnumerator::EnumAvailableVideo() {
    unsigned videoCount   = 0;
    unsigned captureCount = 0;
    bool videoReady = false;

    int enableFake = property_get_int32(EVS_FAKE_PROP, 0);
    if (enableFake != 0) {
        sCameraList.emplace_back(EVS_FAKE_SENSOR, EVS_FAKE_NAME);
        captureCount++;
    }

    // For every video* entry in the dev folder, see if it reports suitable capabilities
    // WARNING:  Depending on the driver implementations this could be slow, especially if
    //           there are timeouts or round trips to hardware required to collect the needed
    //           information.  Platform implementers should consider hard coding this list of
    //           known good devices to speed up the startup time of their EVS implementation.
    //           For example, this code might be replaced with nothing more than:
    //                   sCameraList.emplace_back("/dev/video0");
    //                   sCameraList.emplace_back("/dev/video1");
    ALOGI("Starting dev/video* enumeration");
    DIR* dir = opendir("/dev");
    if (!dir) {
        LOG_FATAL("Failed to open /dev folder\n");
    }
    struct dirent* entry;
    FILE *fp = NULL;
    char devPath[HWC_PATH_LENGTH];
    char value[HWC_PATH_LENGTH];
    while ((entry = readdir(dir)) != nullptr) {
        // We're only looking for entries starting with 'video'
        if (strncmp(entry->d_name, "video", 5) == 0) {
            std::string deviceName("/dev/");
            deviceName += entry->d_name;
            videoCount++;
            if (qualifyCaptureDevice(deviceName.c_str())) {
                snprintf(devPath, HWC_PATH_LENGTH,
                    "/sys/class/video4linux/%s/name", entry->d_name);
                if ((fp = fopen(devPath, "r")) == nullptr) {
                    ALOGE("can't open %s", devPath);
                    continue;
                }
                if(fgets(value, sizeof(value), fp) == nullptr) {
                    fclose(fp);
                    ALOGE("can't read %s", devPath);
                    continue;
                }
                fclose(fp);
                ALOGI("enum name:%s path:%s", value, deviceName.c_str());
                sCameraList.emplace_back(value, deviceName.c_str());
                captureCount++;
            }
        }
    }

    if (captureCount != 0) {
        videoReady = true;
        if (property_set(EVS_VIDEO_READY, "1") < 0)
            ALOGE("Can not set property %s", EVS_VIDEO_READY);
    }

    closedir(dir);
    ALOGI("Found %d qualified video capture devices of %d checked\n", captureCount, videoCount);
    return videoReady;
}

EvsEnumerator::EvsEnumerator() {
    ALOGD("EvsEnumerator created");

    if (!EnumAvailableVideo())
        mPollVideoFileThread = new PollVideoFileThread();
}

EvsEnumerator::PollVideoFileThread::PollVideoFileThread()
    :Thread(false), mINotifyFd(-1), mINotifyWd(-1), mEpollFd(-1)
{
}

void EvsEnumerator::PollVideoFileThread::onFirstRef()
{
    run("VideoFile-Poll-Thread", PRIORITY_NORMAL);
}

int32_t EvsEnumerator::PollVideoFileThread::readyToRun()
{
    epoll_event eventItem;
    mINotifyFd = inotify_init();
    if (mINotifyFd < 0) {
        ALOGE("Fail to initialize inotify fd, error:%s",strerror(errno));
        return -1;
    }
    mINotifyWd = inotify_add_watch(mINotifyFd, MEDIA_FILE_PATH, IN_CREATE);
    if (mINotifyWd < 0) {
        ALOGE("Fail to add watch for %s,error:%s", MEDIA_FILE_PATH, strerror(errno));
        close(mINotifyFd);
        return -1;
    }

    mEpollFd = epoll_create(1);
    if (mEpollFd == -1) {
        ALOGE("Fail to create epoll instance, error:%s",strerror(errno));
        inotify_rm_watch(mINotifyFd,mINotifyWd);
        close(mINotifyFd);
        return -1;
    }

    memset(&eventItem, 0, sizeof(epoll_event));
    eventItem.events = EPOLLIN;
    eventItem.data.fd = mINotifyFd;
    int result = epoll_ctl(mEpollFd, EPOLL_CTL_ADD, mINotifyFd, &eventItem);
    if (result == -1) {
        ALOGE("Fail to add inotify to epoll instance, error:%s",strerror(errno));
        inotify_rm_watch(mINotifyFd,mINotifyWd);
        close(mINotifyFd);
        close(mEpollFd);
        return -1;
    }
    return 0;
}

bool EvsEnumerator::PollVideoFileThread::threadLoop()
{
    int numEpollEvent = 0;
    epoll_event epollItems[EPOLL_MAX_EVENTS];
    numEpollEvent = epoll_wait(mEpollFd, epollItems, EPOLL_MAX_EVENTS, -1);
    if (numEpollEvent <= 0) {
        ALOGE("Fail to wait requested events,numEpollEvent:%d,error:%s",numEpollEvent,strerror(errno));
        return true;
    }

    for (int i=0; i < numEpollEvent; i++) {
        if (epollItems[i].events & (EPOLLERR|EPOLLHUP)) {
            continue;
        }
        if (epollItems[i].events & EPOLLIN) {
            char buf[BUFFER_SIZE];
            int numINotifyItem = read(mINotifyFd, buf, BUFFER_SIZE);
            if (numINotifyItem < 0) {
                ALOGE("Fail to read from INotifyFd,error:%s",strerror(errno));
                continue;
            }

            //Each successful read returns a buffer containing one or more of struct inotify_event
            //The length of each inotify_event structure is sizeof(struct inotify_event)+len.
            for (char *inotifyItemBuf = buf; inotifyItemBuf < buf+numINotifyItem;) {
                struct inotify_event *inotifyItem = (struct inotify_event *)inotifyItemBuf;
                if (strstr(inotifyItem->name,"media")) {
                    //detect /dev/media* has been created
                    if(EnumAvailableVideo()) {
                        inotify_rm_watch(mINotifyFd,mINotifyWd);
                        close(mEpollFd);
                        close(mINotifyFd);
                        return false;
                    }
                }
                inotifyItemBuf += sizeof(struct inotify_event) + inotifyItem->len;
            }
        }
    }

    return true;
}


// Methods from ::android::hardware::automotive::evs::V1_0::IEvsEnumerator follow.
Return<void> EvsEnumerator::getCameraList(getCameraList_cb _hidl_cb)  {
    ALOGD("getCameraList");

    const unsigned numCameras = sCameraList.size();

    // Build up a packed array of CameraDesc for return
    hidl_vec<CameraDesc> hidlCameras;
    hidlCameras.resize(numCameras);
    unsigned i = 0;
    for (const auto& cam : sCameraList) {
        hidlCameras[i++] = cam.desc;
    }

    // Send back the results
    ALOGD("reporting %zu cameras available", hidlCameras.size());
    _hidl_cb(hidlCameras);

    // HIDL convention says we return Void if we sent our result back via callback
    return Void();
}


Return<sp<IEvsCamera>> EvsEnumerator::openCamera(const hidl_string& cameraId) {
    ALOGD("openCamera");

    // Is this a recognized camera id?
    CameraRecord *pRecord = findCameraById(cameraId);
    if (!pRecord) {
        ALOGE("Requested camera %s not found", cameraId.c_str());
        return nullptr;
    }

    // Has this camera already been instantiated by another caller?
    sp<EvsCamera> pActiveCamera = pRecord->activeInstance.promote();
    if (pActiveCamera != nullptr) {
        ALOGW("Killing previous camera because of new caller");
        closeCamera(pActiveCamera);
    }

    // Construct a camera instance for the caller
    std::string fakeCamera(EVS_FAKE_NAME);
    if (fakeCamera == pRecord->desc.cameraId) {
        pActiveCamera = new FakeCapture(pRecord->desc.cameraId.c_str());
    }
    else {
        pActiveCamera = new V4l2Capture(pRecord->desc.cameraId.c_str());
    }
    pActiveCamera->openup(pRecord->desc.cameraId.c_str());
    pRecord->activeInstance = pActiveCamera;
    if (pActiveCamera == nullptr) {
        ALOGE("Failed to allocate new EvsCamera object for %s\n", cameraId.c_str());
    }

    return pActiveCamera;
}


Return<void> EvsEnumerator::closeCamera(const ::android::sp<IEvsCamera>& pCamera) {
    ALOGD("closeCamera");

    if (pCamera == nullptr) {
        ALOGE("Ignoring call to closeCamera with null camera ptr");
        return Void();
    }

    // Get the camera id so we can find it in our list
    std::string cameraId;
    pCamera->getCameraInfo([&cameraId](CameraDesc desc) {
                               cameraId = desc.cameraId;
                           }
    );

    // Find the named camera
    CameraRecord *pRecord = findCameraById(cameraId);

    // Is the display being destroyed actually the one we think is active?
    if (!pRecord) {
        ALOGE("Asked to close a camera whose name isn't recognized");
    } else {
        sp<EvsCamera> pActiveCamera = pRecord->activeInstance.promote();

        if (pActiveCamera == nullptr) {
            ALOGE("Somehow a camera is being destroyed when the enumerator didn't know one existed");
        } else if (pActiveCamera != pCamera) {
            // This can happen if the camera was aggressively reopened, orphaning this previous instance
            ALOGW("Ignoring close of previously orphaned camera - why did a client steal?");
        } else {
            // Drop the active camera
            pActiveCamera->shutdown();
            pRecord->activeInstance = nullptr;
        }
    }

    return Void();
}


Return<sp<IEvsDisplay>> EvsEnumerator::openDisplay() {
    ALOGD("openDisplay");

    // If we already have a display active, then we need to shut it down so we can
    // give exclusive access to the new caller.
    sp<EvsDisplay> pActiveDisplay = sActiveDisplay.promote();
    if (pActiveDisplay != nullptr) {
        ALOGW("Killing previous display because of new caller");
        closeDisplay(pActiveDisplay);
    }

    // Create a new display interface and return it
    pActiveDisplay = new EvsDisplay();
    sActiveDisplay = pActiveDisplay;

    ALOGD("Returning new EvsDisplay object %p", pActiveDisplay.get());
    return pActiveDisplay;
}


Return<void> EvsEnumerator::closeDisplay(const ::android::sp<IEvsDisplay>& pDisplay) {
    ALOGD("closeDisplay");

    // Do we still have a display object we think should be active?
    sp<EvsDisplay> pActiveDisplay = sActiveDisplay.promote();
    if (pActiveDisplay == nullptr) {
        ALOGE("Somehow a display is being destroyed when the enumerator didn't know one existed");
    } else if (sActiveDisplay != pDisplay) {
        ALOGW("Ignoring close of previously orphaned display - why did a client steal?");
    } else {
        // Drop the active display
        pActiveDisplay->forceShutdown();
        sActiveDisplay = nullptr;
    }

    return Void();
}


Return<DisplayState> EvsEnumerator::getDisplayState()  {
    ALOGD("getDisplayState");

    // Do we still have a display object we think should be active?
    sp<IEvsDisplay> pActiveDisplay = sActiveDisplay.promote();
    if (pActiveDisplay != nullptr) {
        return pActiveDisplay->getDisplayState();
    } else {
        return DisplayState::NOT_OPEN;
    }
}


bool EvsEnumerator::qualifyCaptureDevice(const char* deviceName) {
    class FileHandleWrapper {
    public:
        FileHandleWrapper(int fd)   { mFd = fd; }
        ~FileHandleWrapper()        { if (mFd > 0) close(mFd); }
        operator int() const        { return mFd; }
    private:
        int mFd = -1;
    };


    FileHandleWrapper fd = open(deviceName, O_RDWR, 0);
    if (fd < 0) {
        return false;
    }

    v4l2_capability caps;
    int result = ioctl(fd, VIDIOC_QUERYCAP, &caps);
    if (result  < 0) {
        return false;
    }
    if (((caps.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE) == 0) ||
        ((caps.capabilities & V4L2_CAP_STREAMING)     == 0)) {
        return false;
    }

    // Enumerate the available capture formats (if any)
    v4l2_fmtdesc formatDescription;
    formatDescription.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    for (int i=0; true; i++) {
        formatDescription.index = i;
        if (ioctl(fd, VIDIOC_ENUM_FMT, &formatDescription) == 0) {
            switch (formatDescription.pixelformat)
            {
                case V4L2_PIX_FMT_YUYV:     return true;
                case V4L2_PIX_FMT_NV21:     return true;
                case V4L2_PIX_FMT_NV16:     return true;
                case V4L2_PIX_FMT_YVU420:   return true;
                case V4L2_PIX_FMT_RGB32:    return true;
#ifdef V4L2_PIX_FMT_ARGB32  // introduced with kernel v3.17
                case V4L2_PIX_FMT_ARGB32:   return true;
                case V4L2_PIX_FMT_XRGB32:   return true;
#endif // V4L2_PIX_FMT_ARGB32
                default:                    break;
            }
        } else {
            // No more formats available
            break;
        }
    }

    // If we get here, we didn't find a usable output format
    return false;
}


EvsEnumerator::CameraRecord* EvsEnumerator::findCameraById(const std::string& cameraId) {
    // Find the named camera
    // the cameraId from evs app is camera name.
    // and sometimes it is camera dev path.
    for (auto &&cam : sCameraList) {
        if (strstr(cam.name.c_str(), cameraId.c_str()) ||
                (cam.desc.cameraId == cameraId)) {
            // Found a match!
            return &cam;
        }
    }

    // We didn't find a match
    return nullptr;
}


} // namespace implementation
} // namespace V1_0
} // namespace evs
} // namespace automotive
} // namespace hardware
} // namespace android
