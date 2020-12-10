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
#include <log/log.h>
#include <android/hardware_buffer.h>
#include "EvsCamera.h"
#include <system/camera_metadata.h>
#include <android-base/file.h>
#include <cutils/properties.h>

namespace android {
namespace hardware {
namespace automotive {
namespace evs {
namespace V1_1 {
namespace implementation {

void EvsCamera::EvsAppRecipient::serviceDied(uint64_t /*cookie*/,
        const ::android::wp<::android::hidl::base::V1_0::IBase>& /*who*/)
{
    mCamera->releaseResource();
}

void EvsCamera::releaseResource(void)
{
    shutdown();
}

EvsCamera::EvsCamera(const char *videoName, const camera_metadata_t *metadata)
{
    ALOGD("EvsCamera instantiated");

    // Initialize the stream params.
    mFormat = fsl::FORMAT_RGB888;
    mDeqIdx = -1;
    mDescription.v1.cameraId = videoName;
    mLogiccam = isLogicalCamera(metadata);
    if (mLogiccam) {
        std::unique_ptr<ConfigManager>  config;
        int enableFake = property_get_int32(EVS_FAKE_PROP, 0);
        if (enableFake != 0)
            config =
                ConfigManager::Create("/vendor/etc/automotive/evs/fake_evs_configuration.xml");
        else
            config =
                ConfigManager::Create("/vendor/etc/automotive/evs/imx_evs_configuration.xml");

        vector<string> cameraList = config->getCameraIdList();
        for (auto&cam : cameraList) {
            CameraDesc aCamera;
            unique_ptr<ConfigManager::CameraInfo> &tempInfo =
                config->getCameraInfo(cam);
            if (tempInfo != nullptr) {
                aCamera.metadata.setToExternal(
                    (uint8_t *)tempInfo->characteristics,
                    get_camera_metadata_size(tempInfo->characteristics));
            }
            aCamera.v1.cameraId = cam;
            mLogicDescription[cam] = aCamera;
        }
    }
    // the camera metadata also need pass to EvsCamera, app may get metadata from api like getPhysicalCameraInfo
    // SV will call this api to get metadata. you may also can get the metadata from api in EvsEnumerator
    if (metadata != NULL) {
        mDescription.metadata.setToExternal((uint8_t *)metadata, get_camera_metadata_size(metadata));
    }
    mFramesInUse = 0;
    mFramesAllowed = 0;
}

EvsCamera::~EvsCamera()
{
    ALOGD("EvsCamera being destroyed");
}

void EvsCamera::openup(const char *deviceName)
{
    // Initialize the video device
    if (!onOpen(deviceName)) {
        ALOGE("Failed to open v4l device %s\n", deviceName);
        return;
    }

    mCameraControls = enumerateCameraControls();
}

// convert mxc_isi.x.capture in evs xml to /dev/video*
std::string EvsCamera::getVideoDevice(const std::string videoname)
{
    DIR *vidDir = NULL;
    char CamDevice[64];
    struct dirent *dirEntry;
    std::string buffer;
    std::string retVideo;
    char mDevPath[64];

    vidDir = opendir("/sys/class/video4linux");
    if (vidDir == NULL) {
        return NULL;
    }

    while ((dirEntry = readdir(vidDir)) != NULL) {
        if (strncmp(dirEntry->d_name, "video", 5)) {
            continue;
        }

        sprintf(CamDevice, "/sys/class/video4linux/%s/name", dirEntry->d_name);
        if (!android::base::ReadFileToString(std::string(CamDevice), &buffer)) {
            ALOGE("can't read video device name");
            break;
        }

        // the string read through ReadFileToString have '\n' in last byte
        // so we just need compare length (buffer.length() - 1)
        if (!strncmp(videoname.c_str(), buffer.c_str(), (buffer.length() - 1) )) {
            sprintf(mDevPath, "/dev/%s", dirEntry->d_name);
            retVideo = mDevPath;
            return retVideo;
            break;
        }
    }
    return NULL;
}

// if it's v4l2 device, it will return /dev/video*
// if it's fake camera, it will return mxc_isi.x.capture directly
std::unordered_set<std::string> EvsCamera::getPhysicalCameraInLogic(const camera_metadata_t *metadata)
{
    // Look for physical camera identifiers
    camera_metadata_ro_entry entry;
    std::unordered_set<std::string> physicalCameras;
    std::string cam;

    int rc = find_camera_metadata_ro_entry(metadata,
                                           ANDROID_LOGICAL_MULTI_CAMERA_PHYSICAL_IDS,
                                           &entry);

    if (rc != 0) {
        // No capabilities are found.
        ALOGE("No physical camera ID is found for a logical camera device");
    }

    const uint8_t *ids = entry.data.u8;
    size_t start = 0;
    for (size_t i = 0; i < entry.count; ++i) {
        if (ids[i] == '\0') {
            if (start != i) {
                std::string id(reinterpret_cast<const char *>(ids + start));
                if (property_get_int32("vendor.evs.fake.enable", 0))
                    physicalCameras.emplace(id);
                else {
                    cam = getVideoDevice(id);
                    physicalCameras.emplace(cam);
                }
            }
            start = i + 1;
        }
    }

    return physicalCameras;
}

bool EvsCamera::isLogicalCamera(const camera_metadata_t *metadata)
{
    if (metadata == nullptr) {
        // A logical camera device must have a valid camera metadata.
        return false;
    }

    // Looking for LOGICAL_MULTI_CAMERA capability from metadata.
    camera_metadata_ro_entry_t entry;
    int rc = find_camera_metadata_ro_entry(metadata,
                      ANDROID_REQUEST_AVAILABLE_CAPABILITIES,
                      &entry);
    if (0 != rc) {
        // No capabilities are found.
        return false;
    }

    for (size_t i = 0; i < entry.count; ++i) {
        uint8_t cap = entry.data.u8[i];
        if (cap == ANDROID_REQUEST_AVAILABLE_CAPABILITIES_LOGICAL_MULTI_CAMERA) {
            return true;
        }
    }

    return false;

}
//
// This gets called if another caller "steals" ownership of the camera
//
void EvsCamera::shutdown()
{
    ALOGD("EvsCamera shutdown");

    // Make sure our output stream is cleaned up
    // (It really should be already)
    stopVideoStream();

    // Close our video capture device
    onClose();

    // Destroy memory.
    onMemoryDestroy();
}

Return<void> EvsCamera::getCameraInfo_1_1(getCameraInfo_1_1_cb _hidl_cb) {

    // Send back our self description
    _hidl_cb(mDescription);
    return Void();
}


Return<void> EvsCamera::getPhysicalCameraInfo(const hidl_string& id,
                                     getCameraInfo_1_1_cb _hidl_cb) {
    // This method works exactly same as getCameraInfo_1_1() in EVS HW module.
    if (mLogiccam)
        _hidl_cb(mLogicDescription[id]);
    else
        _hidl_cb(mDescription);
    return Void();
}

// Methods from ::android::hardware::automotive::evs::V1_0::IEvsCamera follow.
Return<void> EvsCamera::getCameraInfo(getCameraInfo_cb _hidl_cb) {
    ALOGD("getCameraInfo");

    // Send back our self description
    _hidl_cb(mDescription.v1);
    return Void();
}


Return<EvsResult> EvsCamera::setMaxFramesInFlight(uint32_t bufferCount) {
    ALOGD("setMaxFramesInFlight");
    // If we've been displaced by another owner of the camera,
    // then we can't do anything else
    if (!isOpen()) {
        ALOGW("ignoring call when camera has been lost.");
        return EvsResult::OWNERSHIP_LOST;
    }

    // We cannot function without at least one video buffer to send data
    if (bufferCount < 1) {
        ALOGE("Ignoring with less than one buffer requested");
        return EvsResult::INVALID_ARG;
    }

    if (bufferCount > MAX_BUFFERS_IN_FLIGHT)
        return EvsResult::BUFFER_NOT_AVAILABLE;

     onIncreaseMemoryBuffer(bufferCount);
    // Update our internal state
    return EvsResult::OK;
}

Return<int32_t> EvsCamera::getExtendedInfo(uint32_t /*opaqueIdentifier*/)
{
    ALOGD("getExtendedInfo");
    // Return zero by default as required by the spec
    return 0;
}

Return<EvsResult> EvsCamera::setExtendedInfo(uint32_t /*opaqueIdentifier*/,
                                                int32_t /*opaqueValue*/)
{
    ALOGD("setExtendedInfo");
    // If we've been displaced by another owner of the camera,
    // then we can't do anything else
    if (!isOpen()) {
        ALOGW("ignoring setExtendedInfo call when camera has been lost.");
        return EvsResult::OWNERSHIP_LOST;
    }

    // We don't store any device specific information in this implementation
    return EvsResult::INVALID_ARG;
}

Return<EvsResult> EvsCamera::startVideoStream(
        const ::android::sp<IEvsCameraStream_1_0>& stream)
{
    ALOGD("startVideoStream");
    // If we've been displaced by another owner of the camera,
    // then we can't do anything else
    if (!isOpen()) {
        ALOGW("ignoring startVideoStream call when camera has been lost.");
        return EvsResult::OWNERSHIP_LOST;
    }

    int prevRunMode;
    {
        std::unique_lock <std::mutex> lock(mLock);
        // Set the state of our background thread
        prevRunMode = mRunMode.fetch_or(RUN);
    }

    if (prevRunMode & RUN) {
        // The background thread is running, so we can't start a new stream
        ALOGE("Already in RUN state, so we can't start a new streaming thread");
        return EvsResult::UNDERLYING_SERVICE_ERROR;
    }

    sp<EvsAppRecipient> appRecipient = nullptr;
    {
        std::lock_guard<std::mutex> lock(mLock);
        if (mStream.get() != nullptr) {
            ALOGE("ignoring startVideoStream call when a stream is running.");
            return EvsResult::STREAM_ALREADY_RUNNING;
        }

        // Record the user's callback for use when we have a frame ready
        mStream = stream;
        mStream_1_1 = IEvsCameraStream_1_1::castFrom(mStream).withDefault(nullptr);
        mEvsAppRecipient = new EvsAppRecipient(this);
        appRecipient = mEvsAppRecipient;
    }

    // Set up the video stream.
    if (!onStart()) {
        ALOGE("underlying camera start stream failed");
        {
            std::lock_guard<std::mutex> lock(mLock);
            // No need to hold onto this if we failed to start.
            mStream = nullptr;
            mEvsAppRecipient = nullptr;
        }
        shutdown();
        return EvsResult::UNDERLYING_SERVICE_ERROR;
    }
    stream->linkToDeath(appRecipient, 0);

    std::unique_lock <std::mutex> lock(mLock);
    // Fire up a thread to receive and dispatch the video frames
    mCaptureThread = std::thread([this](){collectFrames();});


    return EvsResult::OK;
}

Return<void> EvsCamera::stopVideoStream()
{
    ALOGD("stopVideoStream");

    int prevRunMode;
    std::thread thread;
    {
        std::unique_lock <std::mutex> lock(mLock);
        // Tell the background thread to stop
        prevRunMode = mRunMode.fetch_or(STOPPING);
        thread.swap(mCaptureThread);
    }

    if (prevRunMode == STOPPED) {
        std::unique_lock <std::mutex> lock(mLock);
        // The background thread wasn't running, so set the flag back to STOPPED
        mRunMode = STOPPED;
    }
    else if (prevRunMode & STOPPING) {
        ALOGE("stopStream called while stream is already stopping.");
        ALOGE("Reentrancy is not supported!");
    }
    else {
        {
            std::unique_lock <std::mutex> lock(mLock);
            mRunMode = STOPPED;
        }

        // Tell the capture device to stop (and block until it does)
        onStop();
        // Block until the background thread is stopped
        if (thread.joinable()) {
            thread.join();
        }

        ALOGD("Capture thread stopped.");
    }


    ::android::sp<IEvsCameraStream_1_0> stream = nullptr;
    sp<EvsAppRecipient> appRecipient = nullptr;
    {
        std::unique_lock <std::mutex> lock(mLock);
        stream = mStream;
        appRecipient = mEvsAppRecipient;

        // Drop our reference to the client's stream receiver
        mEvsAppRecipient = nullptr;
        if (appRecipient != nullptr)
            mStream->unlinkToDeath(appRecipient);
    }

    if (mStream_1_1 != nullptr) {
        EvsEventDesc event;
        event.aType = EvsEventType::STREAM_STOPPED;
        auto result = mStream_1_1->notify(event);
        if (!result.isOk()) {
            ALOGE("Error delivering end of stream event");
        }
    } else if (mStream != nullptr) {
        // Send one last NULL frame to signal the actual end of stream
        BufferDesc_1_0 nullBuff = {};
        auto result =mStream->deliverFrame(nullBuff);
        if (!result.isOk()) {
            ALOGE("Error delivering end of stream marker");
        }
    }

    return Void();
}

Return<void> EvsCamera::getParameterList(getParameterList_cb _hidl_cb) {
    hidl_vec<CameraParam> hidlCtrls;
    // we can get control info through hidlCtrls.resize(mCameraInfo->controls.size());
    // mCameraInfo is read from /vendor/etc/automotive/evs/evs_sample_configuration.xml
    // xml file will record spport feature.
    // TODO: will support read feature list from xml
    _hidl_cb(hidlCtrls);
    return Void();
}

Return<void> EvsCamera::getIntParameterRange(CameraParam id,
                     getIntParameterRange_cb _hidl_cb) {
   ALOGD("getIntParameterRange id %d", id);
    _hidl_cb(0, 0, 0);

    return Void();
}

Return<void> EvsCamera::doneWithFrame(const BufferDesc_1_0& buffer)
{
    ALOGV("doneWithFrame index %d", buffer.bufferId);
    mFramesInUse--;
    doneWithFrame_impl(buffer.bufferId, buffer.memHandle, "");
    return Void();
}

Return<EvsResult> EvsCamera::doneWithFrame_1_1(const hidl_vec<BufferDesc_1_1>& buffers)  {
    mFramesInUse--;
    for (auto&& buffer : buffers) {
        doneWithFrame_impl(buffer.bufferId, buffer.buffer.nativeHandle, buffer.deviceId);
    }

    return EvsResult::OK;
}

// This is the async callback from the thread that tells us a frame is ready
void EvsCamera::forwardFrame(std::vector<struct forwardframe> &fwframes)
{
    // Assemble the buffer description we'll transmit below
    BufferDesc_1_1 bufDesc_1_1 = {};

    ::android::sp<IEvsCameraStream_1_0> stream = nullptr;
    {
        std::unique_lock <std::mutex> lock(mLock);
        stream = mStream;
    }

    if (mFramesInUse > mFramesAllowed) {
        ALOGI("Skipped a frame because too many are in flight");
        return;
    }

    // Issue the (asynchronous) callback to the client
    if (mStream_1_1 != nullptr) {
        int i = 0;
        hidl_vec<BufferDesc_1_1> frames;
        frames.resize(fwframes.size());
        for (auto &fr : fwframes) {
            AHardwareBuffer_Desc* pDesc =
                    reinterpret_cast<AHardwareBuffer_Desc *>(&bufDesc_1_1.buffer.description);
            pDesc->width      = fr.buf->width;
            pDesc->height     = fr.buf->height;
            pDesc->stride     = fr.buf->stride;
            pDesc->format     = fr.buf->fslFormat;
            pDesc->usage      = fr.buf->usage;
            bufDesc_1_1.deviceId   = fr.deviceid;
            bufDesc_1_1.bufferId   = fr.index;
            bufDesc_1_1.buffer.nativeHandle  = fr.buf;

            frames[i++] = bufDesc_1_1;
        }

        mFramesInUse++;
        auto result = mStream_1_1->deliverFrame_1_1(frames);
        if (result.isOk()) {
            ALOGV("Delivered buffer as id %d",
                  bufDesc_1_1.bufferId);
            return;
        }
    } else {
        AHardwareBuffer_Desc* pDesc =
                     reinterpret_cast<AHardwareBuffer_Desc *>(&bufDesc_1_1.buffer.description);
        pDesc->width      = fwframes[0].buf->width;
        pDesc->height     = fwframes[0].buf->height;
        pDesc->stride     = fwframes[0].buf->stride;
        pDesc->format     = fwframes[0].buf->fslFormat;
        pDesc->usage      = fwframes[0].buf->usage;

        BufferDesc_1_0 bufDesc_1_0 = {
            pDesc->width,
            pDesc->height,
            pDesc->stride,
            bufDesc_1_1.pixelSize,
            static_cast<uint32_t>(pDesc->format),
            static_cast<uint32_t>(pDesc->usage),
            static_cast<uint32_t>(fwframes[0].index),
            fwframes[0].buf
        };
        mFramesInUse++;
        auto result = mStream->deliverFrame(bufDesc_1_0);
        if (result.isOk()) {
            ALOGV("Delivered buffer as id %d",
                 bufDesc_1_1.bufferId);
            return;
        }
    }
    // This can happen if the client dies and is likely unrecoverable.
    // To avoid consuming resources generating failing calls, we stop sending
    // frames.  Note, however, that the stream remains in the "STREAMING" state
    // until cleaned up on the main thread.
    ALOGE("Frame delivery call failed in the transport layer.");
    for (auto &fr : fwframes) {
        onFrameReturn(fr.index, fr.deviceid);
    }
}

// This runs on a background thread to receive and dispatch video frames
void EvsCamera::collectFrames()
{
    int runMode;
    {
        std::unique_lock <std::mutex> lock(mLock);
        runMode = mRunMode;
    }

    std::vector<struct forwardframe> physicalCamera;
    // Run until our atomic signal is cleared
    while (runMode == RUN) {
        // Wait for a buffer to be ready
        physicalCamera.clear();
        onFrameCollect(physicalCamera);
        if (physicalCamera.size() > 0) {
            forwardFrame(physicalCamera);
        }

        std::unique_lock <std::mutex> lock(mLock);
        runMode = mRunMode;
    }

    // Mark ourselves stopped
    ALOGD("%s thread ending", __func__);
}

Return<void> EvsCamera::doneWithFrame_impl(const uint32_t bufferId,
                                          const buffer_handle_t memHandle,
                                          std::string deviceid)
{
    ALOGV("doneWithFrame_impl index %d", bufferId);
    // If we've been displaced by another owner of the camera
    // then we can't do anything else
    if (!isOpen()) {
        ALOGW("ignoring doneWithFrame call when camera has been lost.");
    }

    if (memHandle == nullptr) {
        ALOGE("ignoring doneWithFrame called with null handle");
        return Void();
    }

    onFrameReturn(bufferId, deviceid);

    return Void();
}

Return<EvsResult> EvsCamera::pauseVideoStream() {
    // Default implementation does not support this.
    return EvsResult::UNDERLYING_SERVICE_ERROR;
}

Return<EvsResult> EvsCamera::resumeVideoStream() {
    // Default implementation does not support this.
    return EvsResult::UNDERLYING_SERVICE_ERROR;
}

Return<EvsResult> EvsCamera::setMaster() {
    // Default implementation does not expect multiple subscribers and therefore
    // return a success code always.
    return EvsResult::OK;
}

Return<EvsResult> EvsCamera::forceMaster(const sp<IEvsDisplay_1_0>&) {
    // Default implementation does not expect multiple subscribers and therefore
    // return a success code always.
    return EvsResult::OK;
}

Return<EvsResult> EvsCamera::unsetMaster() {
    // Default implementation does not expect multiple subscribers and therefore
    // return a success code always.
    return EvsResult::OK;
}

Return<void> EvsCamera::setIntParameter(CameraParam id, int32_t value,
                                        setIntParameter_cb _hidl_cb) {
    uint32_t v4l2cid = V4L2_CID_BASE;
    hidl_vec<int32_t> values;
    values.resize(1);
    if (!convertToV4l2CID(id, v4l2cid)) {
        _hidl_cb(EvsResult::INVALID_ARG, values);
    } else {
        EvsResult result = EvsResult::OK;
        v4l2_control control = {v4l2cid, value};
        if (setParameter(control) < 0 ||
            getParameter(control) < 0) {
            result = EvsResult::UNDERLYING_SERVICE_ERROR;
        }
        values[0] = control.value;
        _hidl_cb(result, values);
    }
    return Void();
}

Return<void> EvsCamera::getIntParameter(CameraParam id,
                                        getIntParameter_cb _hidl_cb) {
    // Default implementation does not support this.
    uint32_t v4l2cid = V4L2_CID_BASE;
    hidl_vec<int32_t> values;
    values.resize(1);
    if (!convertToV4l2CID(id, v4l2cid)) {
        _hidl_cb(EvsResult::INVALID_ARG, values);
    } else {
        EvsResult result = EvsResult::OK;
        v4l2_control control = {v4l2cid, 0};
        if (getParameter(control) < 0) {
            result = EvsResult::INVALID_ARG;
        }

        // Report a result
        values[0] = control.value;
        _hidl_cb(result, values);
    }

    return Void();
}

bool EvsCamera::convertToV4l2CID(CameraParam id, uint32_t& v4l2cid) {
    switch (id) {
        case CameraParam::BRIGHTNESS:
            v4l2cid = V4L2_CID_BRIGHTNESS;
            break;
        case CameraParam::CONTRAST:
            v4l2cid = V4L2_CID_CONTRAST;
            break;
        case CameraParam::AUTO_WHITE_BALANCE:
            v4l2cid = V4L2_CID_AUTO_WHITE_BALANCE;
            break;
        case CameraParam::WHITE_BALANCE_TEMPERATURE:
            v4l2cid = V4L2_CID_WHITE_BALANCE_TEMPERATURE;
            break;
        case CameraParam::SHARPNESS:
            v4l2cid = V4L2_CID_SHARPNESS;
            break;
        case CameraParam::AUTO_EXPOSURE:
            v4l2cid = V4L2_CID_EXPOSURE_AUTO;
            break;
        case CameraParam::ABSOLUTE_EXPOSURE:
            v4l2cid = V4L2_CID_EXPOSURE_ABSOLUTE;
            break;
        case CameraParam::AUTO_FOCUS:
            v4l2cid = V4L2_CID_FOCUS_AUTO;
            break;
        case CameraParam::ABSOLUTE_FOCUS:
            v4l2cid = V4L2_CID_FOCUS_ABSOLUTE;
            break;
        case CameraParam::ABSOLUTE_ZOOM:
            v4l2cid = V4L2_CID_ZOOM_ABSOLUTE;
            break;
        default:
            ALOGI("Camera parameter is unknown.");
            return false;
    }

    return mCameraControls.find(v4l2cid) != mCameraControls.end();
}

Return<void>
EvsCamera::importExternalBuffers(const hidl_vec<BufferDesc_1_1>& /* buffers */,
                          importExternalBuffers_cb _hidl_cb) {
    ALOGW("%s is not implemented yet.", __FUNCTION__);
    _hidl_cb(EvsResult::UNDERLYING_SERVICE_ERROR, 0);
    return {};
}

Return<EvsResult> EvsCamera::setExtendedInfo_1_1(uint32_t opaqueIdentifier,
                                     const hidl_vec<uint8_t>& opaqueValue) {
    mExtInfo.insert_or_assign(opaqueIdentifier, opaqueValue);
    return EvsResult::OK;
}

Return<void> EvsCamera::getExtendedInfo_1_1(uint32_t opaqueIdentifier,
                                      getExtendedInfo_1_1_cb _hidl_cb) {
    const auto it = mExtInfo.find(opaqueIdentifier);
    hidl_vec<uint8_t> value;
    auto status = EvsResult::OK;
    if (it == mExtInfo.end()) {
        status = EvsResult::INVALID_ARG;
    } else {
        value = mExtInfo[opaqueIdentifier];
    }

     _hidl_cb(status, value);
    return Void();
}

} // namespace implementation
} // namespace V1_0
} // namespace evs
} // namespace automotive
} // namespace hardware
} // namespace android
