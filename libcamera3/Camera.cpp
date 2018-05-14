/*
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc.
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

#include <cstdlib>
#include <stdio.h>
#include <linux/videodev2.h>
#include <hardware/camera3.h>
#include <sync/sync.h>
#include <system/camera_metadata.h>
#include <system/graphics.h>
#include <utils/Mutex.h>
#include <sys/stat.h>
#include "CameraHAL.h"
#include "Metadata.h"
#include "Stream.h"
#include "UvcMJPGDevice.h"

//#define LOG_NDEBUG 0
#include <cutils/log.h>

#include "Camera.h"
#include "CameraUtils.h"
#include "Max9286Mipi.h"
#include "Ov5640Csi.h"
#include "Ov5640Mipi.h"
#include "Ov5642Csi.h"
#include "TVIN8DvDevice.h"
#include "TVINDevice.h"
#include "UvcDevice.h"
#include "Tc358743.h"
#include "VADCTVINDevice.h"
#include "VideoStream.h"

#define CAMERA_SYNC_TIMEOUT 5000 // in msecs

// Undefine u8 since the camera_metadata_ro_entry_t contains a u8 field
#ifdef u8
    #undef u8
#endif

extern "C" {
// Shim passed to the framework to close an opened device.
static int32_t close_device(hw_device_t* dev)
{
    camera3_device_t* cam_dev = reinterpret_cast<camera3_device_t*>(dev);
    Camera* cam = static_cast<Camera*>(cam_dev->priv);
    return cam->closeDev();
}
} // extern "C"

android::Mutex Camera::sStaticInfoLock(android::Mutex::PRIVATE);

Camera* Camera::createCamera(int32_t id, char* name, int32_t facing,
                             int32_t orientation, char* path)
{
    Camera* device = NULL;

    android::Mutex::Autolock al(sStaticInfoLock);

    if (strstr(name, OV5640MIPI_SENSOR_NAME)) {
        ALOGI("create id:%d ov5640 mipi device", id);
        device = new Ov5640Mipi(id, facing, orientation, path);
    }
    else if (strstr(name, OV5642CSI_SENSOR_NAME)) {
        ALOGI("create id:%d ov5642 csi device", id);
        device = new Ov5642Csi(id, facing, orientation, path);
    }
    else if (strstr(name, OV5640CSI_SENSOR_NAME)) {
        ALOGI("create id:%d ov5640 csi device", id);
        device = new Ov5640Csi(id, facing, orientation, path);
    }
    else if (strstr(name, UVC_SENSOR_NAME)) {
#ifdef BOARD_HAVE_VPU
        char uvcMJPGStr[92];
        int configUseMJPG = 0;

        property_get(UVC_USE_MJPG, uvcMJPGStr, DEFAULT_ERROR_NAME_str);

        if (uvcMJPGStr[0] == DEFAULT_ERROR_NAME)
            configUseMJPG = 0;
        else
            configUseMJPG = atoi(uvcMJPGStr);

        if(configUseMJPG == 0) {
            ALOGI("create id:%d usb camera device", id);
            device = UvcDevice::newInstance(id, name, facing, orientation, path);
        } else {
            ALOGI("DeviceAdapter: Create uvc device, config to use MJPG");
            device = new UvcMJPGDevice(id, facing, orientation, path);
        }
#else
        ALOGI("create id:%d usb camera device", id);
        device = UvcDevice::newInstance(id, name, facing, orientation, path);
#endif
    }
    else if (strstr(name, TC358743_NAME)) {
        ALOGI("create id:%d tc358743 device", id);
        device = new Tc358743(id, facing, orientation, path);
    }
    else if (strstr(name, OV5640_SENSOR_NAME)) {
#ifdef VADC_TVIN
        ALOGI("create id:%d TVin device for auto_sx", id);
        device = new VADCTVINDevice(id, facing, orientation, path);
#else
        ALOGI("create id:%d 5640-csi device", id);
        device = new Ov5640Csi(id, facing, orientation, path);
        device->usemx6s = 1;
#endif
    }
    else if (strstr(name, ADV7180_TVIN_NAME)) {
        char boardName[CAMERA_SENSOR_LENGTH];
        memset(boardName, 0, sizeof(boardName));
        property_get("ro.board.platform", boardName, DEFAULT_ERROR_NAME_str);

        if (strstr(boardName, IMX8_BOARD_NAME)) {
            ALOGI("create id:%d adv7180 tvin device on 8dv", id);
            device = new TVIN8DvDevice(id, facing, orientation, path);
        } else {
            ALOGI("create id:%d adv7180 tvin device", id);
            device = new TVINDevice(id, facing, orientation, path);
        }
    } else if (strstr(name, MAX9286MIPI_SENSOR_NAME)) {
        ALOGI("create id:%d Max9286Mipi device", id);
        device = new Max9286Mipi(id, facing, orientation, path);
    } else {
        ALOGE("doesn't support camera id:%d %s", id, name);
    }

    return device;
}

Camera::Camera(int32_t id, int32_t facing, int32_t orientation, char *path)
    : mId(id), mStaticInfo(NULL), mBusy(false), mCallbackOps(NULL), mStreams(NULL), mNumStreams(0), mTmpBuf(NULL), usemx6s(0)
{
    ALOGI("%s:%d: new camera device", __func__, mId);
    android::Mutex::Autolock al(mDeviceLock);

    camera_info::facing = facing;
    camera_info::orientation = orientation;
    strncpy(SensorData::mDevPath, path, CAMAERA_FILENAME_LENGTH);

    memset(&mDevice, 0, sizeof(mDevice));
    mDevice.common.tag = HARDWARE_DEVICE_TAG;
    mDevice.common.version = CAMERA_DEVICE_API_VERSION_3_0;
    mDevice.common.close = close_device;
    mDevice.ops = const_cast<camera3_device_ops_t *>(&sOps);
    mDevice.priv = this;
    memset(&m3aState, 0, sizeof(m3aState));
}

Camera::~Camera()
{
    ALOGI("%s:%d: destroy camera device", __func__, mId);
    android::Mutex::Autolock al(mDeviceLock);
    if (mStaticInfo != NULL) {
        free_camera_metadata(mStaticInfo);
    }

    if (mVideoStream != NULL) {
        mVideoStream->destroyStream();
        mVideoStream.clear();
        mVideoStream = NULL;
    }
}

void Camera::setPreviewPixelFormat()
{
    mPreviewPixelFormat = getMatchFormat(mVpuSupportFmt,
                          MAX_VPU_SUPPORT_FORMAT,
                          mAvailableFormats, MAX_SENSOR_FORMAT);
}

void Camera::setPicturePixelFormat()
{
    mPicturePixelFormat = getMatchFormat(mPictureSupportFmt,
                            MAX_PICTURE_SUPPORT_FORMAT,
                            mAvailableFormats, MAX_SENSOR_FORMAT);
}

int32_t Camera::openDev(const hw_module_t *module, hw_device_t **device)
{
    ALOGI("%s:%d: Opening camera device", __func__, mId);
    android::Mutex::Autolock al(mDeviceLock);

    if (mBusy) {
        ALOGE("%s:%d: Error! Camera device already opened", __func__, mId);
        return -EBUSY;
    }

    // open camera dev nodes, etc
    int32_t ret = mVideoStream->openDev(mDevPath);
    if (ret != 0) {
        ALOGE("can not open camera devpath:%s", mDevPath);
        return BAD_VALUE;
    }

    mBusy = true;
    mDevice.common.module = const_cast<hw_module_t*>(module);
    *device = &mDevice.common;
    return 0;
}

int32_t Camera::getInfo(struct camera_info *info)
{
    android::Mutex::Autolock al(mDeviceLock);

    info->facing = camera_info::facing;
    info->orientation = camera_info::orientation;
    info->device_version = mDevice.common.version;
    if (mStaticInfo == NULL) {
        int32_t ret = initSensorStaticData();
        if (ret != 0) {
            ALOGE("%s initSensorStaticData failed", __func__);
            return ret;
        }
        setPreviewPixelFormat();
        setPicturePixelFormat();
        mStaticInfo = Metadata::createStaticInfo(*this);
    }
    info->static_camera_characteristics = mStaticInfo;
    return 0;
}

int32_t Camera::closeDev()
{
    ALOGI("%s:%d: Closing camera device", __func__, mId);
    android::Mutex::Autolock al(mDeviceLock);

    if (!mBusy) {
        ALOGE("%s:%d: Error! Camera device not open", __func__, mId);
        return -EINVAL;
    }

#ifdef BOARD_HAVE_FLASHLIGHT
    // make sure flashlight is off
    if (ANDROID_FLASH_MODE_OFF != m3aState.flashMode) {
        m3aState.aeMode = ANDROID_CONTROL_AE_MODE_OFF;
        m3aState.flashMode = setFlashlight(ANDROID_FLASH_MODE_OFF);
    }
#endif

    // close camera dev nodes, etc
    mVideoStream->closeDev();

    mBusy = false;
    return 0;
}

int32_t Camera::initializeDev(const camera3_callback_ops_t *callback_ops)
{
    int32_t res;

    ALOGV("%s:%d: callback_ops=%p", __func__, mId, callback_ops);
    {
        android::Mutex::Autolock al(mDeviceLock);
        mCallbackOps = callback_ops;
    }
    // per-device specific initialization
    res = initDevice();
    if (res != 0) {
        ALOGE("%s:%d: Failed to initialize device!", __func__, mId);
        return res;
    }
    return 0;
}

int32_t Camera::configureStreams(camera3_stream_configuration_t *stream_config)
{
    camera3_stream_t *astream;
    sp<Stream> *newStreams = NULL;

    ALOGV("%s:%d: stream_config=%p", __func__, mId, stream_config);
    android::Mutex::Autolock al(mDeviceLock);

    if (stream_config == NULL) {
        ALOGE("%s:%d: NULL stream configuration array", __func__, mId);
        return -EINVAL;
    }
    if (stream_config->num_streams == 0) {
        ALOGE("%s:%d: Empty stream configuration array", __func__, mId);
        return -EINVAL;
    }

    // Create new stream array
    newStreams = new sp<Stream>[stream_config->num_streams];
    ALOGV("%s:%d: Number of Streams: %d", __func__, mId,
            stream_config->num_streams);

    // Mark all current streams unused for now
    for (int32_t i = 0; i < mNumStreams; i++)
        mStreams[i]->setReuse(false);
    // Fill new stream array with reused streams and new streams
    for (uint32_t i = 0; i < stream_config->num_streams; i++) {
        astream = stream_config->streams[i];
        if (astream->max_buffers > 0) {
            ALOGV("%s:%d: Reusing stream %d", __func__, mId, i);
            newStreams[i] = reuseStream(astream);
        } else {
            ALOGV("%s:%d: Creating new stream %d", __func__, mId, i);
            newStreams[i] = new Stream(mId, astream, this);
        }

        if (newStreams[i] == NULL) {
            ALOGE("%s:%d: Error processing stream %d", __func__, mId, i);
            goto err_out;
        }
        astream->priv = newStreams[i].get();
    }

    // Verify the set of streams in aggregate
    if (!isValidStreamSet(newStreams, stream_config->num_streams)) {
        ALOGE("%s:%d: Invalid stream set", __func__, mId);
        goto err_out;
    }

    // Destroy all old streams and replace stream array with new one
    destroyStreams(mStreams, mNumStreams);
    mStreams = newStreams;
    mNumStreams = stream_config->num_streams;

    return 0;

err_out:
    // Clean up temporary streams, preserve existing mStreams/mNumStreams
    destroyStreams(newStreams, stream_config->num_streams);
    return -EINVAL;
}

void Camera::destroyStreams(sp<Stream> *streams, int32_t count)
{
    if (streams == NULL)
        return;
    for (int32_t i = 0; i < count; i++) {
        // Only destroy streams that weren't reused
        if (streams[i] != NULL)
            streams[i].clear();
    }
    delete [] streams;
}

sp<Stream> Camera::reuseStream(camera3_stream_t *astream)
{
    sp<Stream> priv = reinterpret_cast<Stream*>(astream->priv);
    // Verify the re-used stream's parameters match
    if (!priv->isValidReuseStream(mId, astream)) {
        ALOGE("%s:%d: Mismatched parameter in reused stream", __func__, mId);
        return NULL;
    }
    // Mark stream to be reused
    priv->setReuse(true);
    return priv;
}

bool Camera::isValidStreamSet(sp<Stream> *streams, int32_t count)
{
    int32_t inputs = 0;
    int32_t outputs = 0;

    if (streams == NULL) {
        ALOGE("%s:%d: NULL stream configuration streams", __func__, mId);
        return false;
    }
    if (count == 0) {
        ALOGE("%s:%d: Zero count stream configuration streams", __func__, mId);
        return false;
    }
    // Validate there is at most one input stream and at least one output stream
    for (int32_t i = 0; i < count; i++) {
        // A stream may be both input and output (bidirectional)
        if (streams[i]->isInputType())
            inputs++;
        if (streams[i]->isOutputType())
            outputs++;
    }
    ALOGV("%s:%d: Configuring %d output streams and %d input streams",
            __func__, mId, outputs, inputs);
    if (outputs < 1) {
        ALOGE("%s:%d: Stream config must have >= 1 output", __func__, mId);
        return false;
    }
    if (inputs > 1) {
        ALOGE("%s:%d: Stream config must have <= 1 input", __func__, mId);
        return false;
    }
    // TODO: check for correct number of Bayer/YUV/JPEG/Encoder streams
    return true;
}

int32_t Camera::registerStreamBuffers(const camera3_stream_buffer_set_t *buf_set)
{
    ALOGV("%s:%d: buffer_set=%p", __func__, mId, buf_set);
    if (buf_set == NULL) {
        ALOGE("%s:%d: NULL buffer set", __func__, mId);
        return -EINVAL;
    }
    if (buf_set->stream == NULL) {
        ALOGE("%s:%d: NULL stream handle", __func__, mId);
        return -EINVAL;
    }
    return 0;
}

bool Camera::isValidTemplateType(int32_t type)
{
    return type >= CAMERA3_TEMPLATE_PREVIEW && type < CAMERA3_TEMPLATE_COUNT;
}

const camera_metadata_t* Camera::constructDefaultRequestSettings(int32_t type)
{
    ALOGI("%s:%d: type=%d", __func__, mId, type);

    android::Mutex::Autolock al(mDeviceLock);
    if (!isValidTemplateType(type)) {
        ALOGE("%s:%d: Invalid template request type: %d", __func__, mId, type);
        return NULL;
    }
    return mTemplates[type]->get();
}

int32_t Camera::processCaptureRequest(camera3_capture_request_t *request)
{
    if (request == NULL) {
        ALOGE("%s:%d: NULL request recieved", __func__, mId);
        return -EINVAL;
    }

    ALOGV("%s:%d: Request Frame:%d Settings:%p", __func__, mId,
            request->frame_number, request->settings);

    // NULL indicates use last settings
    if (request->settings != NULL) {
        android::Mutex::Autolock al(mDeviceLock);
        mSettings = new Metadata(request->settings);
    }

    if (request->input_buffer != NULL) {
        ALOGV("%s:%d: Reprocessing input buffer %p", __func__, mId,
                request->input_buffer);

        if (!isValidReprocessSettings(request->settings)) {
            ALOGE("%s:%d: Invalid settings for reprocess request: %p",
                    __func__, mId, request->settings);
            return -EINVAL;
        }
    } else {
        ALOGV("%s:%d: Capturing new frame.", __func__, mId);

        if (!isValidCaptureSettings(request->settings)) {
            ALOGE("%s:%d: Invalid settings for capture request: %p",
                    __func__, mId, request->settings);
            return -EINVAL;
        }
    }

    if (request->num_output_buffers <= 0) {
        ALOGE("%s:%d: Invalid number of output buffers: %d", __func__, mId,
                request->num_output_buffers);
        return -EINVAL;
    }

    // set preview/still capture stream.
    sp<Stream> preview = NULL, stillcap = NULL;
    sp<Metadata> meta = NULL;
    sp<VideoStream> devStream = NULL;
    camera3_callback_ops* callback = NULL;
    uint32_t fps = 30;
    {
        android::Mutex::Autolock al(mDeviceLock);
        for (int32_t i = 0; i < mNumStreams; i++) {
            sp<Stream>& stream = mStreams[i];
            if (stream->isPreview()) {
                preview = stream;
            }
            if (stream->isJpeg()) {
                stillcap = stream;
            }
        }

        camera_metadata_entry_t streams = mSettings->find(
                            ANDROID_CONTROL_AE_TARGET_FPS_RANGE);
        if (streams.count > 1) {
            if (streams.data.i32[0] > 15 && streams.data.i32[1] > 15) {
                fps = 30;
            }
            else {
                fps = 15;
            }
        }

        meta = mSettings;
        devStream = mVideoStream;
        callback = (camera3_callback_ops*)mCallbackOps;
    }
    sp<CaptureRequest> capture = new CaptureRequest();

    // configure VideoStream according to request type.
    if (request->settings != NULL) {
        if (meta->getRequestType() == TYPE_STILLCAP) {
            if (stillcap == NULL) {
                ALOGE("still capture intent but without jpeg stream");
                stillcap = preview;
            }
            stillcap->setFps(fps);
            devStream->configure(stillcap);
        }
        else {
            if (meta->getRequestType() != TYPE_SNAPSHOT) {
                preview->setFps(fps);
            }
            devStream->configure(preview);
        }
    }

    capture->init(request, callback, meta);

    return devStream->requestCapture(capture);

err_out:
    // TODO: this should probably be a total device failure; transient for now
    return -EINVAL;
}

bool Camera::isValidReprocessSettings(const camera_metadata_t* /*settings*/)
{
    // TODO: reject settings that cannot be reprocessed
    // input buffers unimplemented, use this to reject reprocessing requests
    ALOGE("%s:%d: Input buffer reprocessing not implemented", __func__, mId);
    return false;
}

//do advanced character set.
int32_t Camera::processSettings(sp<Metadata> settings, uint32_t frame)
{
    if (settings == NULL || settings->isEmpty()) {
        ALOGE("invalid settings");
        return 0;
    }

    // auto exposure control.
    camera_metadata_entry_t entry = settings->find(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER);
    if (entry.count > 0) {
        // ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START
        m3aState.aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
        ALOGI("ae precature trigger");
    }
    else {
        m3aState.aeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
    }

    entry = settings->find(ANDROID_CONTROL_AE_PRECAPTURE_ID);
    if (entry.count > 0) {
        m3aState.aeTriggerId = entry.data.i32[0];
    }

    int64_t timestamp = 0;
    timestamp = systemTime();
    settings->addInt64(ANDROID_SENSOR_TIMESTAMP, 1, &timestamp);

#ifdef BOARD_HAVE_FLASHLIGHT
    // flash support
    uint8_t flash_mode = ANDROID_FLASH_MODE_OFF;
    uint8_t flash_state = ANDROID_FLASH_STATE_READY;
    uint8_t ae_mode = ANDROID_CONTROL_AE_MODE_OFF;

    entry = settings->find(ANDROID_FLASH_MODE);
    if (entry.count > 0)
        flash_mode = entry.data.u8[0];

    entry = settings->find(ANDROID_FLASH_STATE);
    if (entry.count > 0)
        flash_state = entry.data.u8[0];

    entry = settings->find(ANDROID_CONTROL_AE_MODE);
    if (entry.count > 0)
        ae_mode = entry.data.u8[0];

    if (ae_mode != m3aState.aeMode) {
        m3aState.aeMode = ae_mode;
        // Override flash_mode param in case of always on
        switch (m3aState.aeMode) {
            case ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH:
                flash_mode = ANDROID_FLASH_MODE_TORCH;
                break;
            case ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH:
            case ANDROID_CONTROL_AE_MODE_ON_AUTO_FLASH_REDEYE:
                // Not supported, should set m3aState.aeState to
                // ANDROID_CONTROL_AE_STATE_FLASH_REQUIRED
                // when HAL thinks that a flash is required.
                break;
            case ANDROID_CONTROL_AE_MODE_OFF:
            case ANDROID_CONTROL_AE_MODE_ON:
            default:
                if (m3aState.flashMode == ANDROID_FLASH_MODE_TORCH)
                    flash_mode = ANDROID_FLASH_MODE_OFF;
                break;
        };
    }

    if (flash_mode != m3aState.flashMode) {
        // update mode/state if different than last
        m3aState.flashMode = setFlashlight(flash_mode);
        if ((m3aState.flashMode != flash_mode) &&
            (m3aState.flashMode == ANDROID_FLASH_MODE_OFF))
            flash_state = ANDROID_FLASH_STATE_UNAVAILABLE;
    }

    m3aState.flashState = flash_state;
    settings->addUInt8(ANDROID_FLASH_STATE, 1, &m3aState.flashState);
    settings->addUInt8(ANDROID_FLASH_MODE, 1, &m3aState.flashMode);
#endif

    // auto focus control.
    entry = settings->find(ANDROID_CONTROL_AF_MODE);
    if (entry.count == 0) {
        ALOGE("%s: No AF mode entry!", __FUNCTION__);
        return BAD_VALUE;
    }
    uint8_t afMode = (entry.count > 0) ?
        entry.data.u8[0] : (uint8_t)ANDROID_CONTROL_AF_MODE_OFF;

    entry = settings->find(ANDROID_CONTROL_AF_TRIGGER);
    if (entry.count > 0) {
        // save trigger value
        uint8_t trigger = entry.data.u8[0];

        // check if a ROI has been provided
        entry = settings->find(ANDROID_CONTROL_AF_REGIONS);
        if (entry.count > 0) {
            int xavg = (entry.data.i32[0] + entry.data.i32[2]) / 2;
            int yavg = (entry.data.i32[1] + entry.data.i32[3]) / 2;
            ALOGV("%s: AF region: x %d y %d", __FUNCTION__, xavg, yavg);
            setAutoFocusRegion(xavg, yavg);
        }

        // get and save trigger ID
        entry = settings->find(ANDROID_CONTROL_AF_TRIGGER_ID);
        if (entry.count > 0)
            m3aState.afTriggerId = entry.data.i32[0];

        // process trigger type
        ALOGV("trigger: %d afMode %d afTriggerId %d", trigger, afMode, m3aState.afTriggerId);
        switch (trigger) {
            case ANDROID_CONTROL_AF_TRIGGER_CANCEL:
                // in case of continuous focus, cancel means to stop manual focus only
                if ((afMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_VIDEO) ||
                    (afMode == ANDROID_CONTROL_AF_MODE_CONTINUOUS_PICTURE))
                    m3aState.afState = doAutoFocus(afMode);
                break;
            case ANDROID_CONTROL_AF_TRIGGER_START:
                m3aState.afState = doAutoFocus(afMode);
                break;
            case ANDROID_CONTROL_AF_TRIGGER_IDLE:
                m3aState.afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
                break;
            default:
                ALOGE("unknown trigger: %d", trigger);
                m3aState.afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
        }
    } else {
        m3aState.afState = getAutoFocusStatus(afMode);
    }
    settings->addInt32(ANDROID_CONTROL_AF_TRIGGER_ID, 1, &m3aState.afTriggerId);
    settings->addUInt8(ANDROID_CONTROL_AF_STATE, 1, &m3aState.afState);

    // auto white balance control.
    m3aState.awbState = ANDROID_CONTROL_AWB_STATE_INACTIVE;
    settings->addUInt8(ANDROID_CONTROL_AWB_STATE, 1, &m3aState.awbState);

    settings->addUInt8(ANDROID_CONTROL_AE_STATE, 1, &m3aState.aeState);
    settings->addInt32(ANDROID_CONTROL_AE_PRECAPTURE_ID, 1, &m3aState.aeTriggerId);

    notifyShutter(frame, timestamp);

    return 0;
}

void Camera::notifyShutter(uint32_t frame_number, uint64_t timestamp)
{
    int32_t res;
    struct timespec ts;

    // If timestamp is 0, get timestamp from right now instead
    if (timestamp == 0) {
        ALOGW("%s:%d: No timestamp provided, using CLOCK_BOOTTIME",
                __func__, mId);
        res = clock_gettime(CLOCK_BOOTTIME, &ts);
        if (res == 0) {
            timestamp = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        } else {
            ALOGE("%s:%d: No timestamp and failed to get CLOCK_BOOTTIME %s(%d)",
                    __func__, mId, strerror(errno), errno);
        }
    }
    camera3_notify_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = CAMERA3_MSG_SHUTTER;
    m.message.shutter.frame_number = frame_number;
    m.message.shutter.timestamp = timestamp;
    mCallbackOps->notify(mCallbackOps, &m);
}

void Camera::dumpDev(int32_t fd)
{
    ALOGV("%s:%d: Dumping to fd %d", __func__, mId, fd);
    android::Mutex::Autolock al(mDeviceLock);

    dprintf(fd, "Camera ID: %d (Busy: %d)\n", mId, mBusy);

    // TODO: dump all settings
    dprintf(fd, "Most Recent Settings: (%p)\n", mSettings.get());

    dprintf(fd, "Number of streams: %d\n", mNumStreams);
    for (int32_t i = 0; i < mNumStreams; i++) {
        if(mStreams[i] == NULL)
            continue;

        dprintf(fd, "Stream %d/%d:\n", i, mNumStreams);
        mStreams[i]->dump(fd);
    }
}

const char* Camera::templateToString(int32_t type)
{
    switch (type) {
    case CAMERA3_TEMPLATE_PREVIEW:
        return "CAMERA3_TEMPLATE_PREVIEW";
    case CAMERA3_TEMPLATE_STILL_CAPTURE:
        return "CAMERA3_TEMPLATE_STILL_CAPTURE";
    case CAMERA3_TEMPLATE_VIDEO_RECORD:
        return "CAMERA3_TEMPLATE_VIDEO_RECORD";
    case CAMERA3_TEMPLATE_VIDEO_SNAPSHOT:
        return "CAMERA3_TEMPLATE_VIDEO_SNAPSHOT";
    case CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG:
        return "CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG";
    }
    // TODO: support vendor templates
    return "Invalid template type!";
}

int32_t Camera::setTemplate(int32_t type, camera_metadata_t *settings)
{
    android::Mutex::Autolock al(mDeviceLock);

    if (!isValidTemplateType(type)) {
        ALOGE("%s:%d: Invalid template request type: %d", __func__, mId, type);
        return -EINVAL;
    }

    if (mTemplates[type] != NULL && !mTemplates[type]->isEmpty()) {
        ALOGI("%s:%d: Setting already constructed template type %s(%d)",
                __func__, mId, templateToString(type), type);
        return 0;
    }

    // Make a durable copy of the underlying metadata
    mTemplates[type] = new Metadata(settings);
    if (mTemplates[type]->isEmpty()) {
        ALOGE("%s:%d: Failed to clone metadata %p for template type %s(%d)",
                __func__, mId, settings, templateToString(type), type);
        return -EINVAL;
    }
    return 0;
}

int32_t Camera::initDevice()
{
    int32_t res;

    // Use base settings to create all other templates and set them
    res = setPreviewTemplate();
    if (res)
        return res;
    res = setStillTemplate();
    if (res)
        return res;
    res = setRecordTemplate();
    if (res)
        return res;
    res = setSnapshotTemplate();
    if (res)
        return res;
    res = setZslTemplate();
    if (res)
        return res;

    return 0;
}

int32_t Camera::setPreviewTemplate()
{
    Metadata base;
    // Create standard settings templates from copies of base metadata
    // TODO: use vendor tags in base metadata
    Metadata::createSettingTemplate(base, *this, CAMERA3_TEMPLATE_PREVIEW);

    // Setup default preview controls
    int32_t res = base.add1UInt8(ANDROID_CONTROL_CAPTURE_INTENT,
                            ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW);

    if (res)
        return res;
    // TODO: set fast auto-focus, auto-whitebalance, auto-exposure, auto flash
    return setTemplate(CAMERA3_TEMPLATE_PREVIEW, base.get());
}

int32_t Camera::setStillTemplate()
{
    Metadata base;
    // Create standard settings templates from copies of base metadata
    // TODO: use vendor tags in base metadata
    Metadata::createSettingTemplate(base, *this, CAMERA3_TEMPLATE_STILL_CAPTURE);

    int32_t res = base.add1UInt8(ANDROID_CONTROL_CAPTURE_INTENT,
                            ANDROID_CONTROL_CAPTURE_INTENT_STILL_CAPTURE);
    // Setup default still capture controls
    if (res)
        return res;
    // TODO: set fast auto-focus, auto-whitebalance, auto-exposure, auto flash
    return setTemplate(CAMERA3_TEMPLATE_STILL_CAPTURE, base.get());
}

int32_t Camera::setRecordTemplate()
{
    Metadata base;
    // Create standard settings templates from copies of base metadata
    // TODO: use vendor tags in base metadata
    Metadata::createSettingTemplate(base, *this, CAMERA3_TEMPLATE_VIDEO_RECORD);

    int32_t res = base.add1UInt8(ANDROID_CONTROL_CAPTURE_INTENT,
                            ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_RECORD);
    // Setup default video record controls
    if (res)
        return res;
    // TODO: set slow auto-focus, auto-whitebalance, auto-exposure, flash off
    return setTemplate(CAMERA3_TEMPLATE_VIDEO_RECORD, base.get());
}

int32_t Camera::setSnapshotTemplate()
{
    Metadata base;
    // Create standard settings templates from copies of base metadata
    // TODO: use vendor tags in base metadata
    Metadata::createSettingTemplate(base, *this, CAMERA3_TEMPLATE_VIDEO_SNAPSHOT);

    int32_t res = base.add1UInt8(ANDROID_CONTROL_CAPTURE_INTENT,
                            ANDROID_CONTROL_CAPTURE_INTENT_VIDEO_SNAPSHOT);
    // Setup default video snapshot controls
    if (res)
        return res;
    // TODO: set slow auto-focus, auto-whitebalance, auto-exposure, flash off
    return setTemplate(CAMERA3_TEMPLATE_VIDEO_SNAPSHOT, base.get());
}

int32_t Camera::setZslTemplate()
{
    Metadata base;
    // Create standard settings templates from copies of base metadata
    // TODO: use vendor tags in base metadata
    Metadata::createSettingTemplate(base, *this, CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG);

    int32_t res = base.add1UInt8(ANDROID_CONTROL_CAPTURE_INTENT,
                            ANDROID_CONTROL_CAPTURE_INTENT_ZERO_SHUTTER_LAG);
    // Setup default zero shutter lag controls
    if (res)
        return res;
    // TODO: set reprocessing parameters for zsl input queue
    return setTemplate(CAMERA3_TEMPLATE_ZERO_SHUTTER_LAG, base.get());
}

bool Camera::isValidCaptureSettings(const camera_metadata_t* settings)
{
    // TODO: reject settings that cannot be captured
    return true;
}

int32_t Camera::getV4l2Res(uint32_t streamWidth, uint32_t streamHeight, uint32_t *pV4l2Width, uint32_t *pV4l2Height)
{
    if ((pV4l2Width == NULL) || (pV4l2Height == NULL)) {
        ALOGE("%s, para null", __func__);
        return BAD_VALUE;
    }

    *pV4l2Width = streamWidth;
    *pV4l2Height = streamHeight;
    return NO_ERROR;
}

#ifdef BOARD_HAVE_FLASHLIGHT
#define TORCH_PATH      "/sys/class/leds/torch"
#define FLASH_PATH      "/sys/class/leds/flash"
#define PATH_LEN 256

uint8_t Camera::setFlashlight(uint8_t mode)
{
    FILE *file;
    int32_t brightness = -1;
    bool on = false;
    char max_path[PATH_LEN], path[PATH_LEN];

    // Special case, always override value when ALWAYS_FLASH
    if (m3aState.aeMode == ANDROID_CONTROL_AE_MODE_ON_ALWAYS_FLASH)
        mode = ANDROID_FLASH_MODE_TORCH;

    switch (mode) {
        case ANDROID_FLASH_MODE_SINGLE:
            snprintf(max_path, PATH_LEN, "%s/max_brightness", FLASH_PATH);
            snprintf(path, PATH_LEN, "%s/brightness", FLASH_PATH);
            brightness = property_get_int32("persist.camera.flash.brightness", -1);
            on = true;
            break;
        case ANDROID_FLASH_MODE_TORCH:
            on = true;
        case ANDROID_FLASH_MODE_OFF:
        default:
            snprintf(max_path, PATH_LEN, "%s/max_brightness", TORCH_PATH);
            snprintf(path, PATH_LEN, "%s/brightness", TORCH_PATH);
            brightness = property_get_int32("persist.camera.torch.brightness", -1);
            break;
    };

    if (on && (brightness < 0)) {
        /* Read maximum value */
        file = fopen(max_path, "r");
        if (!file) {
            ALOGE("can not open file %s\n", max_path);
            return ANDROID_FLASH_MODE_OFF;
        }
        fscanf(file, "%d", &brightness);
        fclose(file);
    }

    file = fopen(path, "w");
    if (!file) {
        ALOGE("can not open file %s\n", path);
        return ANDROID_FLASH_MODE_OFF;
    }
    fprintf(file, "%d", brightness);
    fclose(file);

    return mode;
}
#endif

//---------------------------------------------------------
extern "C" {
// Get handle to camera from device priv data
static Camera *camdev_to_camera(const camera3_device_t *dev)
{
    return reinterpret_cast<Camera*>(dev->priv);
}

static int32_t initialize(const camera3_device_t *dev,
        const camera3_callback_ops_t *callback_ops)
{
    return camdev_to_camera(dev)->initializeDev(callback_ops);
}

static int32_t configure_streams(const camera3_device_t *dev,
        camera3_stream_configuration_t *stream_list)
{
    return camdev_to_camera(dev)->configureStreams(stream_list);
}

static int32_t register_stream_buffers(const camera3_device_t *dev,
        const camera3_stream_buffer_set_t *buffer_set)
{
    return camdev_to_camera(dev)->registerStreamBuffers(buffer_set);
}

static const camera_metadata_t *construct_default_request_settings(
        const camera3_device_t *dev, int32_t type)
{
    return camdev_to_camera(dev)->constructDefaultRequestSettings(type);
}

static int32_t process_capture_request(const camera3_device_t *dev,
        camera3_capture_request_t *request)
{
    return camdev_to_camera(dev)->processCaptureRequest(request);
}

static void dump(const camera3_device_t *dev, int32_t fd)
{
    camdev_to_camera(dev)->dumpDev(fd);
}

static int32_t flush(const camera3_device_t*)
{
    ALOGE("%s: unimplemented.", __func__);
    return -1;
}

} // extern "C"

const camera3_device_ops_t Camera::sOps = {
    .initialize = initialize,
    .configure_streams = configure_streams,
    .register_stream_buffers = register_stream_buffers,
    .construct_default_request_settings
        = construct_default_request_settings,
    .process_capture_request = process_capture_request,
    .get_metadata_vendor_tag_ops = NULL,
    .dump = dump,
    .flush = flush,
    .reserved = {0},
};

