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

#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android-base/stringprintf.h>
#include <android-base/file.h>
#include <android-base/unique_fd.h>

namespace android {
namespace hardware {
namespace automotive {
namespace evs {
namespace V1_1 {
namespace implementation {

#define HWC_PATH_LENGTH 64
#define BUFFER_SIZE 512
#define EPOLL_MAX_EVENTS 8
#define MEDIA_FILE_PATH "/dev"
#define EVS_VIDEO_READY "vendor.evs.video.ready"
#define EVS_FAKE_SENSOR "mxc_isi.0.capture"
#define EVS_FAKE_LOGIC_CAMERA "group0"
#define EVS_FAKE_NAME   "fake.camera"
#define EVS_FAKE_LOGIC_NAME   "fake.logic.camera"
#define EVS_FAKE_PROP   "vendor.evs.fake.enable"
#define FAKE_CAMERA_WIDTH 1920
#define FAKE_CAMERA_HEIGHT 1024

// Default camera output image resolution if the evs_app do not configure
const std::array<int32_t, 2> kDefaultResolution = {1280, 720};

using ::android::base::EqualsIgnoreCase;
using ::android::base::StringPrintf;
using ::android::base::WriteStringToFd;

// NOTE:  All members values are static so that all clients operate on the same state
//        That is to say, this is effectively a singleton despite the fact that HIDL
//        constructs a new instance for each client.
std::list<EvsEnumerator::CameraRecord>   EvsEnumerator::sCameraList;
wp<EvsDisplay>                           EvsEnumerator::sActiveDisplay;
std::mutex                               EvsEnumerator::sLock;
std::unique_ptr<ConfigManager>        EvsEnumerator::sConfigManager;

bool EvsEnumerator::filterVideoFromConfigure(char *deviceName) {
    if (sConfigManager == nullptr)
        return true;

    vector<string>::iterator index;
    vector<string> cameraList =
                sConfigManager->getCameraIdList();
    index = find(cameraList.begin(), cameraList.end(), deviceName);
    if(index != cameraList.end())
        return true;
    else
        return false;
}

bool EvsEnumerator::EnumAvailableVideo() {
    unsigned videoCount   = 0;
    unsigned captureCount = 0;
    bool videoReady = false;
    CameraDesc_1_1 aCamera;

    if (sConfigManager == nullptr) {
        /* loads and initializes ConfigManager in a separate thread */
        sConfigManager =
            ConfigManager::Create("/vendor/etc/automotive/evs/imx_evs_configuration.xml");
    }

    // if it's one fake camera, need fill the metadata in xml to sCameraList.
    // otherwise app will not get the metadata
    int enableFake = property_get_int32(EVS_FAKE_PROP, 0);
    if (enableFake != 0) {
        CameraRecord camrec_group(EVS_FAKE_LOGIC_CAMERA, NULL);
        unique_ptr<ConfigManager::CameraGroupInfo> &tmpInfo =
            sConfigManager->getCameraGroupInfo(EVS_FAKE_LOGIC_CAMERA);
        if (tmpInfo != nullptr) {
            aCamera.metadata.setToExternal(
                (uint8_t *)tmpInfo->characteristics,
                get_camera_metadata_size(tmpInfo->characteristics)
            );
            camera_metadata_entry_t streamCfgs;
            if (!find_camera_metadata_entry(
                reinterpret_cast<camera_metadata_t *>(aCamera.metadata.data()),
                ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                &streamCfgs)) {
                RawStreamConfig *ptr = reinterpret_cast<RawStreamConfig *>(streamCfgs.data.i32);
                //set the first stream resolution to FAKE_CAMERA_WIDTH/FAKE_CAMERA_HEIGHT
                ptr->width = FAKE_CAMERA_WIDTH;
                ptr->height = FAKE_CAMERA_HEIGHT;
            }

            int32_t err = add_camera_metadata_entry(
                reinterpret_cast<camera_metadata_t *>(aCamera.metadata.data()),
                ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
                streamCfgs.data.i32,
                calculate_camera_metadata_entry_data_size(
                    get_camera_metadata_tag_type(
                    ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS
                    ), streamCfgs.count));

            if (err) {
                ALOGE("Failed to add stream configurations to metadata, ignored");
            }
        }

        aCamera.v1.cameraId = EVS_FAKE_LOGIC_NAME;
        camrec_group.desc = aCamera;
        sCameraList.push_back(camrec_group);
        captureCount++;

        CameraRecord camrec_single(EVS_FAKE_SENSOR, NULL);
        unique_ptr<ConfigManager::CameraInfo> &tempInfo =
            sConfigManager->getCameraInfo(EVS_FAKE_SENSOR);
        if (tempInfo != nullptr) {
            aCamera.metadata.setToExternal(
                (uint8_t *)tempInfo->characteristics,
                get_camera_metadata_size(tempInfo->characteristics)
            );
        }
        aCamera.v1.cameraId = EVS_FAKE_NAME;
        camrec_single.desc = aCamera;
        sCameraList.push_back(camrec_single);

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
    int len_val;
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
                // last byte is '\n' if get the string through fgets
                // it cause issue that can't find item for camera. set the last byte as '\0'
                len_val = strlen(value) - 1;
                fclose(fp);
                value[len_val] = '\0';
                ALOGI("enum name:%s path:%s", value, deviceName.c_str());
                if (!filterVideoFromConfigure(value)) {
                    continue;
                }
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

EvsEnumerator::EvsEnumerator(sp<IAutomotiveDisplayProxyService> proxyService) {
    ALOGD("EvsEnumerator created");

    if (proxyService == nullptr)
         ALOGD("proxy server is null");
    if (!EnumAvailableVideo())
        mPollVideoFileThread = new PollVideoFileThread();
}

Return<void> EvsEnumerator::getDisplayIdList(getDisplayIdList_cb _list_cb) {
    hidl_vec<uint8_t> ids;
    ids.resize(1);
    ids[0] = 0;
    _list_cb(ids);
    return Void();
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

Return<void> EvsEnumerator::getCameraList_1_1(getCameraList_1_1_cb _hidl_cb)  {
    ALOGD("getCameraList_1_1");

    {
        std::unique_lock<std::mutex> lock(sLock);
        if (sCameraList.size() < 1) {
            // No qualified device has been found.  Wait until new device is ready,
               ALOGD("Timer expired.  No new device has been added.");
        }
    }
    hidl_vec<CameraDesc_1_1> hidlCameras;
    if (sConfigManager == nullptr) {

        const unsigned numCameras = sCameraList.size();
        hidlCameras.resize(numCameras);
        unsigned i = 0;
        CameraDesc_1_1 aCamera;
        for (auto&cam : sCameraList) {
             aCamera.v1.cameraId = cam.name.c_str();
             hidlCameras[i++] = aCamera;
        }
    } else {
        auto camGroups = sConfigManager->getCameraGroupIdList();
        // Build up a packed array of CameraDesc for return
        const unsigned numCameras = sCameraList.size();
        const unsigned numGroup = camGroups.size();
        hidlCameras.resize(numCameras + numGroup);
        unsigned i = 0;
        CameraDesc_1_1 aCamera;

        for (auto&cam : sCameraList) {
            unique_ptr<ConfigManager::CameraInfo> &tempInfo =
                sConfigManager->getCameraInfo(cam.name);
            if (tempInfo != nullptr) {
                aCamera.metadata.setToExternal(
                    (uint8_t *)tempInfo->characteristics,
                     get_camera_metadata_size(tempInfo->characteristics)
                );
            }
#if 0
        camera_metadata_entry_t streamCfgs;
        if (!find_camera_metadata_entry(
              reinterpret_cast<camera_metadata_t *>(tempInfo->characteristics),
              ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS,
              &streamCfgs)) {
              ALOGE("do not find ANDROID_SCALER_AVAILABLE_STREAM_CONFIGURATIONS in CameraMetadata");
        }
#endif

            aCamera.v1.cameraId = cam.name.c_str();

            hidlCameras[i++] = aCamera;
        }

        // Adding camera groups that represent logical camera devices
        for (auto&& id : camGroups) {
            unique_ptr<ConfigManager::CameraGroupInfo> &tempInfo =
                sConfigManager->getCameraGroupInfo(id);
            CameraRecord camrec(id.c_str(), NULL);
            if (tempInfo != nullptr) {

                aCamera.metadata.setToExternal(
                    (uint8_t *)tempInfo->characteristics,
                     get_camera_metadata_size(tempInfo->characteristics)
                );
            }

            aCamera.v1.cameraId = id;
            camrec.desc = aCamera;
            sCameraList.push_back(camrec);
            hidlCameras[i++] = aCamera;
        }

    }
    _hidl_cb(hidlCameras);
    return Void();
}

// Methods from ::android::hardware::automotive::evs::V1_0::IEvsEnumerator follow.
Return<void> EvsEnumerator::getCameraList(getCameraList_cb _hidl_cb)  {
    ALOGD("getCameraList");

    const unsigned numCameras = sCameraList.size();

    // Build up a packed array of CameraDesc for return
    hidl_vec<CameraDesc_1_0> hidlCameras;
    hidlCameras.resize(numCameras);
    unsigned i = 0;
    CameraDesc_1_0 aCamera;
    for (const auto& cam : sCameraList) {
        aCamera.cameraId = cam.name.c_str();
        hidlCameras[i++] = aCamera;
    }

    // Send back the results
    ALOGD("reporting %zu cameras available", hidlCameras.size());
    _hidl_cb(hidlCameras);

    // HIDL convention says we return Void if we sent our result back via callback
    return Void();
}

bool EvsEnumerator::validStreamCfg(const Stream& streamCfg) {

    return (streamCfg.width > 0) &&
           (streamCfg.height > 0);
}

Return<sp<IEvsCamera_1_1>> EvsEnumerator::openCamera_1_1(const hidl_string& cameraId,
                                                         const Stream& streamCfg) {
    ALOGD("openCamera_1_1 %s", cameraId.c_str());
    CameraRecord *pRecord = findCameraById(cameraId);
    if (pRecord == nullptr) {
        ALOGD("camera does not exist!");
        return nullptr;
    }
    sp<EvsCamera> pActiveCamera = pRecord->activeInstance.promote();
    if (pActiveCamera != nullptr) {
        closeCamera(pActiveCamera);
    }

    std::string fakeCamera(EVS_FAKE_NAME);
    std::string fakeLogicCamera(EVS_FAKE_LOGIC_NAME);
    if (fakeCamera == pRecord->desc.v1.cameraId || fakeLogicCamera == pRecord->desc.v1.cameraId) {
        // the camera is fake, and it's logic camera, it will beeen fakeLogicCamera
        //  the camera is fake, and it's phsical camera, it will beeen fakeCamera
        pActiveCamera = new FakeCapture(pRecord->desc.v1.cameraId.c_str(),
                 reinterpret_cast<camera_metadata_t *>(pRecord->desc.metadata.data()));
    } else {

        if (sConfigManager != nullptr && validStreamCfg(streamCfg)) {
            unique_ptr<ConfigManager::CameraInfo> &camInfo = sConfigManager->getCameraInfo(cameraId);
            /* currently do not support group metadta */
            //unique_ptr<ConfigManager::CameraGroupInfo> &camInfo = sConfigManager->getCameraGroupInfo(cameraId);
            int32_t streamId = -1, area = INT_MIN;

            if (camInfo != nullptr ) {
                for (auto& [id, cfg] : camInfo->streamConfigurations) {
                    // RawConfiguration has id, width, height, format, direction, and
                    // fps.
                    if (cfg[3] == static_cast<uint32_t>(streamCfg.format)) {
                        if (cfg[1] == streamCfg.width &&
                            cfg[2] == streamCfg.height) {
                            // Find exact match.
                            streamId = id;
                            break;
                        } else if (streamCfg.width  > cfg[1] &&
                                   streamCfg.height > cfg[2] &&
                                   cfg[1] * cfg[2] > area) {
                             streamId = id;
                             area = cfg[1] * cfg[2];
                        }
                    }
                }

            pActiveCamera = new V4l2Capture(pRecord->desc.v1.cameraId.c_str(), pRecord->name.c_str(),
                       camInfo->streamConfigurations[streamId][1],
                       camInfo->streamConfigurations[streamId][2],
                       camInfo->streamConfigurations[streamId][3],
                       reinterpret_cast<camera_metadata_t *>(pRecord->desc.metadata.data()));
            } else {
                pActiveCamera = new V4l2Capture(pRecord->desc.v1.cameraId.c_str(),
                                                    pRecord->name.c_str(),
                                                   kDefaultResolution[0],
                                                   kDefaultResolution[1],
                                                   HAL_PIXEL_FORMAT_RGB_888,
                                                   reinterpret_cast<camera_metadata_t *>(pRecord->desc.metadata.data()));
            }
        } else {
            pActiveCamera = new V4l2Capture(pRecord->desc.v1.cameraId.c_str(),
                                               pRecord->name.c_str(),
                                               kDefaultResolution[0],
                                               kDefaultResolution[1],
                                               HAL_PIXEL_FORMAT_RGB_888,
                                               reinterpret_cast<camera_metadata_t *>(pRecord->desc.metadata.data()));
        }
    }

    if (pActiveCamera == nullptr) {
        ALOGD("Failed to create new EvsV4lCamera object for ");
        return nullptr;
    }

    pActiveCamera->openup(pRecord->desc.v1.cameraId.c_str());
    pRecord->activeInstance = pActiveCamera;

    return pActiveCamera;
}

Return<sp<IEvsCamera_1_0>> EvsEnumerator::openCamera(const hidl_string& cameraId) {
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
    pActiveCamera = new V4l2Capture(pRecord->desc.v1.cameraId.c_str(),
                                               pRecord->name.c_str(),
                                               kDefaultResolution[0],
                                               kDefaultResolution[1],
                                               HAL_PIXEL_FORMAT_RGB_888,
                                               nullptr);

    pActiveCamera->openup(pRecord->desc.v1.cameraId.c_str());
    pRecord->activeInstance = pActiveCamera;
    if (pActiveCamera == nullptr) {
        ALOGE("Failed to allocate new EvsCamera object for %s\n", pRecord->desc.v1.cameraId.c_str());
    }

    return pActiveCamera;
}


Return<void> EvsEnumerator::closeCamera(const ::android::sp<IEvsCamera_1_0>& pCamera) {
    ALOGD("closeCamera");

    if (pCamera == nullptr) {
        ALOGE("Ignoring call to closeCamera with null camera ptr");
        return Void();
    }

    // Get the camera id so we can find it in our list
    std::string cameraId;
    pCamera->getCameraInfo([&cameraId](CameraDesc_1_0 desc) {
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

Return<sp<IEvsDisplay_1_1>> EvsEnumerator::openDisplay_1_1(uint8_t port) {
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

    ALOGD("Returning new EvsDisplay object %p %d", pActiveDisplay.get(), port);
    return pActiveDisplay;
}

Return<sp<IEvsDisplay_1_0>> EvsEnumerator::openDisplay() {
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


Return<void> EvsEnumerator::closeDisplay(const ::android::sp<IEvsDisplay_1_0>& pDisplay) {
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


Return<EvsDisplayState> EvsEnumerator::getDisplayState()  {
    ALOGD("getDisplayState");

    // Do we still have a display object we think should be active?
    sp<IEvsDisplay> pActiveDisplay = sActiveDisplay.promote();
    if (pActiveDisplay != nullptr) {
        return pActiveDisplay->getDisplayState();
    } else {
        return EvsDisplayState::NOT_OPEN;
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
                (cam.desc.v1.cameraId == cameraId)) {
            // Found a match!
            return &cam;
        }
    }

    // We didn't find a match
    return nullptr;
}

// there is not Ultrasonics sensor on imx device, so have no UltrasonicsArray implement
Return<sp<IEvsUltrasonicsArray>> EvsEnumerator::openUltrasonicsArray(
     const hidl_string& ultrasonicsArrayId) {
     (void)ultrasonicsArrayId;
     return sp<IEvsUltrasonicsArray>();
}
Return<void> EvsEnumerator::getUltrasonicsArrayList(getUltrasonicsArrayList_cb _hidl_cb) {
     hidl_vec<UltrasonicsArrayDesc> ultrasonicsArrayDesc;
     _hidl_cb(ultrasonicsArrayDesc);
     return Void();
}

Return<void> EvsEnumerator::closeUltrasonicsArray(
    const ::android::sp<IEvsUltrasonicsArray>& evsUltrasonicsArray)  {
    (void)evsUltrasonicsArray;
    return Void();
}

Return<void> EvsEnumerator::debug(const hidl_handle& fd , const hidl_vec<hidl_string>& options) {
    if (fd.getNativeHandle() != nullptr && fd->numFds > 0) {
        cmdDump(fd->data[0], options);
    } else {
        LOG(ERROR) << "Given file descriptor is not valid.";
    }

    return {};
}

void EvsEnumerator::cmdDump(int fd, const hidl_vec<hidl_string>& options) {
    if (options.size() == 0) {
        WriteStringToFd("No option is given.\n", fd);
        cmdHelp(fd);
        return;
    }

    const std::string option = options[0];
    if (EqualsIgnoreCase(option, "--help")) {
        cmdHelp(fd);
    } else if (EqualsIgnoreCase(option, "--list")) {
        cmdList(fd, options);
    } else if (EqualsIgnoreCase(option, "--dump")) {
        cmdDumpDevice(fd, options);
    } else {
        WriteStringToFd(StringPrintf("Invalid option: %s\n", option.c_str()),fd);
        cmdHelp(fd);
    }
}

void EvsEnumerator::cmdHelp(int fd) {
    WriteStringToFd("--help: shows this help.\n"
                    "--list: [option1|option2|...|all]: lists all the dump options: option1 or option2 or ... or all\n"
                    "available to EvsEnumerator Hal.\n"
                    "--dump option1: shows current status of the option1\n"
                    "--dump option2: shows current status of the option2\n"
                    "--dump all: shows current status of all the options\n", fd);
    return;
}

void EvsEnumerator::cmdList(int fd, const hidl_vec<hidl_string>& options) {
    bool listoption1 = false;
    bool listoption2 = false;
    if (options.size() > 1) {
        const std::string option = options[1];
        const bool listAll = EqualsIgnoreCase(option, "all");
        listoption1 = listAll || EqualsIgnoreCase(option, "option1");
        listoption2 = listAll || EqualsIgnoreCase(option, "option2");
        if (!listoption1 && !listoption2) {
            WriteStringToFd(StringPrintf("Unrecognized option is ignored.\n\n"),fd);
            cmdHelp(fd);
            return;
        }
        if(listoption1) {
            WriteStringToFd(StringPrintf("list option1 dump options, default is --list listoption1.\n"),fd);
         }

        if(listoption2) {
            WriteStringToFd(StringPrintf("list option2 dump options, default is --list listoption2.\n"),fd);
        }
    } else {
        WriteStringToFd(StringPrintf("Invalid input, need to append list option.\n\n"),fd);
        cmdHelp(fd);
     }
}

void EvsEnumerator::cmdDumpDevice(int fd, const hidl_vec<hidl_string>& options) {
    bool listoption1 = false;
    bool listoption2 = false;
    if (options.size() > 1) {
        const std::string option = options[1];
        const bool listAll = EqualsIgnoreCase(option, "all");
        listoption1 = listAll || EqualsIgnoreCase(option, "option1");
        listoption2 = listAll || EqualsIgnoreCase(option, "option2");
        if (!listoption1 && !listoption2) {
            WriteStringToFd(StringPrintf("Unrecognized option is ignored.\n\n"),fd);
            cmdHelp(fd);
            return;
        }
        if(listoption1) {
            WriteStringToFd(StringPrintf("dump option1 info.\n"),fd);
        }
        if(listoption2) {
            WriteStringToFd(StringPrintf("dump option2 info.\n"),fd);
        }
    } else {
        WriteStringToFd(StringPrintf("Invalid input, need to append dump option.\n\n"),fd);
        cmdHelp(fd);
    }
}

} // namespace implementation
} // namespace V1_1
} // namespace evs
} // namespace automotive
} // namespace hardware
} // namespace android
