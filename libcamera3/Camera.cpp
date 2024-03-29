/*
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP
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

#include <hardware/camera3.h>
#include <linux/videodev2.h>
#include <stdio.h>
#include <sync/sync.h>
#include <sys/stat.h>
#include <system/camera_metadata.h>
#include <system/graphics.h>
#include <utils/Mutex.h>

#include <cstdlib>

#include "CameraHAL.h"
#include "Metadata.h"
#include "Stream.h"
#include "utils/CameraConfigurationParser.h"

// #define LOG_NDEBUG 0
#include <cutils/log.h>

#include "Camera.h"
#include "CameraUtils.h"
#include "ISPCamera.h"
#include "ImxCamera.h"
#include "UvcDevice.h"
#include "VideoStream.h"

#define CAMERA_SYNC_TIMEOUT 5000 // in msecs

extern "C" {
// Shim passed to the framework to close an opened device.
static int32_t close_device(hw_device_t *dev) {
    camera3_device_t *cam_dev = reinterpret_cast<camera3_device_t *>(dev);
    Camera *cam = static_cast<Camera *>(cam_dev->priv);
    return cam->closeDev();
}
} // extern "C"

android::Mutex Camera::sStaticInfoLock(android::Mutex::PRIVATE);

Camera *Camera::createCamera(int32_t id, char *path, CscHw cam_copy_hw, CscHw cam_csc_hw,
                             const char *jpeg_hw, CameraSensorMetadata *cam_metadata) {
    Camera *device = NULL;
    int facing;

    android::Mutex::Autolock al(sStaticInfoLock);
    if (strstr(cam_metadata->camera_type, BACK_CAMERA_NAME))
        facing = CAMERA_FACING_BACK;
    else if (strstr(cam_metadata->camera_type, FRONT_CAMERA_NAME))
        facing = CAMERA_FACING_FRONT;

    if (strstr(cam_metadata->camera_name, OV5640_SENSOR_NAME_V1) ||
        strstr(cam_metadata->camera_name, OV5640_SENSOR_NAME_V2) ||
        strstr(cam_metadata->camera_name, OV5640_SENSOR_NAME_V3))
        device = new ImxCamera(id, facing, cam_metadata->orientation, path, cam_copy_hw, cam_csc_hw,
                               jpeg_hw, cam_metadata);
    else if (strstr(cam_metadata->camera_name, ISP_SENSOR_NAME))
        device = new ISPCamera(id, facing, cam_metadata->orientation, path, cam_copy_hw, cam_csc_hw,
                               jpeg_hw, cam_metadata);
    else if (strstr(cam_metadata->camera_name, UVC_NAME))
        device = UvcDevice::newInstance(id, cam_metadata->camera_name, facing,
                                        cam_metadata->orientation, path, cam_copy_hw, cam_csc_hw,
                                        jpeg_hw, cam_metadata);
    return device;
}

Camera::Camera(int32_t id, int32_t facing, int32_t orientation, char *path, CscHw cam_copy_hw,
               CscHw cam_csc_hw, const char *hw_enc)
      : usemx6s(0),
        mId(id),
        mStaticInfo(NULL),
        mBusy(false),
        mCallbackOps(NULL),
        mStreams(NULL),
        mNumStreams(0),
        mTmpBuf(NULL) {
    ALOGI("%s:%d: new camera device", __func__, mId);
    android::Mutex::Autolock al(mDeviceLock);

    mCamBlitCopyType = cam_copy_hw;
    mCamBlitCscType = cam_csc_hw;

    strcpy(mJpegHw, hw_enc);
    camera_info::facing = facing;
    camera_info::orientation = orientation;
    strncpy(SensorData::mDevPath, path, CAMAERA_FILENAME_LENGTH);
    SensorData::mDevPath[CAMAERA_FILENAME_LENGTH - 1] = 0;

    memset(&mDevice, 0, sizeof(mDevice));
    mDevice.common.tag = HARDWARE_DEVICE_TAG;
#if ANDROID_SDK_VERSION >= 28
    mDevice.common.version = CAMERA_DEVICE_API_VERSION_3_5;
#else
    mDevice.common.version = CAMERA_DEVICE_API_VERSION_3_2;
#endif
    mDevice.common.close = close_device;
    mDevice.ops = const_cast<camera3_device_ops_t *>(&sOps);
    mDevice.priv = this;
    memset(&m3aState, 0, sizeof(m3aState));
}

Camera::~Camera() {
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

void Camera::setPreviewPixelFormat() {
    mPreviewPixelFormat = getMatchFormat(mVpuSupportFmt, MAX_VPU_SUPPORT_FORMAT, mAvailableFormats,
                                         MAX_SENSOR_FORMAT);
}

void Camera::setPicturePixelFormat() {
    mPicturePixelFormat = getMatchFormat(mPictureSupportFmt, MAX_PICTURE_SUPPORT_FORMAT,
                                         mAvailableFormats, MAX_SENSOR_FORMAT);
}

int32_t Camera::openDev(const hw_module_t *module, hw_device_t **device) {
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
    mDevice.common.module = const_cast<hw_module_t *>(module);
    *device = &mDevice.common;
    return 0;
}

int32_t Camera::getInfo(struct camera_info *info) {
    android::Mutex::Autolock al(mDeviceLock);

    info->facing = camera_info::facing;
    info->orientation = camera_info::orientation;
    info->device_version = mDevice.common.version;
    if (mStaticInfo == NULL) {
        int32_t ret = initSensorStaticData();
        if (ret != 0) {
            ALOGW("%s initSensorStaticData failed", __func__);
        }
        setPreviewPixelFormat();
        setPicturePixelFormat();
        mStaticInfo = Metadata::createStaticInfo(*this, *this);
    }
    info->static_camera_characteristics = mStaticInfo;
    return 0;
}

int32_t Camera::closeDev() {
    ALOGI("%s:%d: Closing camera device", __func__, mId);
    android::Mutex::Autolock al(mDeviceLock);

    if (!mBusy) {
        ALOGE("%s:%d: Error! Camera device not open", __func__, mId);
        return -EINVAL;
    }

    // close camera dev nodes, etc
    mVideoStream->closeDev();

    mBusy = false;
    return 0;
}

int32_t Camera::flushDev() {
    ALOGI("%s:%d: Flushing camera device", __func__, mId);
    android::Mutex::Autolock al(mDeviceLock);

    if (!mBusy) {
        ALOGE("%s:%d: Error! Camera device not opened yet", __func__, mId);
        return -EINVAL;
    }

    // flush camera dev nodes.
    return mVideoStream->flushDev();
}

int32_t Camera::initializeDev(const camera3_callback_ops_t *callback_ops) {
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

int32_t Camera::configureStreams(camera3_stream_configuration_t *stream_config) {
    camera3_stream_t *astream;
    sp<Stream> *newStreams = NULL;

    if (stream_config == NULL) {
        ALOGE("%s:%d: NULL stream configuration array", __func__, mId);
        return -EINVAL;
    }

    ALOGI("%s:%d: stream_config %p, num %d, streams %p, mode %d", __func__, mId, stream_config,
          stream_config->num_streams, stream_config->streams, stream_config->operation_mode);

    android::Mutex::Autolock al(mDeviceLock);

    if (stream_config->num_streams == 0) {
        ALOGE("%s:%d: Empty stream configuration array", __func__, mId);
        return -EINVAL;
    }

    for (uint32_t i = 0; i < stream_config->num_streams; i++) {
        camera3_stream_t *stream = stream_config->streams[i];
        if (stream == NULL) {
            ALOGE("stream config %d null", i);
            return -EINVAL;
        }

        ALOGI("config %d, type %d, res %dx%d, fmt 0x%x, usage 0x%x, maxbufs %d, priv %p, rotation "
              "%d",
              i, stream->stream_type, stream->width, stream->height, stream->format, stream->usage,
              stream->max_buffers, stream->priv, stream->rotation);

        if (((int)stream->width <= 0) || ((int)stream->height <= 0) || (stream->format == -1) ||
            !(stream->rotation >= 0 && stream->rotation <= 3)) {
            ALOGE("para error");
            return -EINVAL;
        }
    }

    // Create new stream array
    newStreams = new sp<Stream>[stream_config->num_streams];
    ALOGV("%s:%d: Number of Streams: %d", __func__, mId, stream_config->num_streams);

    // Mark all current streams unused for now
    for (int32_t i = 0; i < mNumStreams; i++) mStreams[i]->setReuse(false);
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

void Camera::destroyStreams(sp<Stream> *streams, int32_t count) {
    if (streams == NULL)
        return;
    for (int32_t i = 0; i < count; i++) {
        // Only destroy streams that weren't reused
        if (streams[i] != NULL)
            streams[i].clear();
    }
    delete[] streams;
}

sp<Stream> Camera::reuseStream(camera3_stream_t *astream) {
    sp<Stream> priv = reinterpret_cast<Stream *>(astream->priv);
    // Verify the re-used stream's parameters match
    if (!priv->isValidReuseStream(mId, astream)) {
        ALOGE("%s:%d: Mismatched parameter in reused stream", __func__, mId);
        return NULL;
    }
    // Mark stream to be reused
    priv->setReuse(true);
    return priv;
}

bool Camera::isValidStreamSet(sp<Stream> *streams, int32_t count) {
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
    ALOGV("%s:%d: Configuring %d output streams and %d input streams", __func__, mId, outputs,
          inputs);
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

int32_t Camera::registerStreamBuffers(const camera3_stream_buffer_set_t *buf_set) {
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

bool Camera::isValidTemplateType(int32_t type) {
    return type >= CAMERA3_TEMPLATE_PREVIEW && type < CAMERA3_TEMPLATE_COUNT;
}

const camera_metadata_t *Camera::constructDefaultRequestSettings(int32_t type) {
    ALOGI("%s:%d: type=%d", __func__, mId, type);

    android::Mutex::Autolock al(mDeviceLock);
    if (!isValidTemplateType(type)) {
        ALOGE("%s:%d: Invalid template request type: %d", __func__, mId, type);
        return NULL;
    }
    return mTemplates[type]->get();
}

int32_t Camera::processCaptureRequest(camera3_capture_request_t *request) {
    if (request == NULL) {
        ALOGE("%s:%d: NULL request recieved", __func__, mId);
        return -EINVAL;
    }

    ALOGV("%s:%d: Request Frame:%d Settings:%p", __func__, mId, request->frame_number,
          request->settings);

    // NULL indicates use last settings
    if (request->settings != NULL) {
        android::Mutex::Autolock al(mDeviceLock);
        mSettings = new Metadata(request->settings);
    }

    if (request->input_buffer != NULL) {
        ALOGV("%s:%d: Reprocessing input buffer %p", __func__, mId, request->input_buffer);

        if (!isValidReprocessSettings(request->settings)) {
            ALOGE("%s:%d: Invalid settings for reprocess request: %p", __func__, mId,
                  request->settings);
            return -EINVAL;
        }
    } else {
        ALOGV("%s:%d: Capturing new frame.", __func__, mId);

        if (!isValidCaptureSettings(request->settings)) {
            ALOGE("%s:%d: Invalid settings for capture request: %p", __func__, mId,
                  request->settings);
            return -EINVAL;
        }
    }

    if (request->num_output_buffers <= 0) {
        ALOGE("%s:%d: Invalid number of output buffers: %d", __func__, mId,
              request->num_output_buffers);
        return -EINVAL;
    }

    // set preview/still capture stream.
    sp<Stream> preview = NULL, stillcap = NULL, record = NULL, callbackStream = NULL;
    sp<Metadata> meta = NULL;
    sp<VideoStream> devStream = NULL;
    camera3_callback_ops *callback = NULL;
    uint32_t fps = 30;
    {
        android::Mutex::Autolock al(mDeviceLock);
        for (int32_t i = 0; i < mNumStreams; i++) {
            sp<Stream> &stream = mStreams[i];
            if (stream->isPreview()) {
                preview = stream;
            } else if (stream->isJpeg()) {
                stillcap = stream;
            } else if (stream->isRecord()) {
                record = stream;
            } else if (stream->isCallback()) {
                callbackStream = stream;
            }
        }

        if ((preview == NULL) && (stillcap == NULL) && (callbackStream == NULL) &&
            (record == NULL)) {
            ALOGI("%s: preview, stillcap and callback stream all are NULL", __func__);
            return -EINVAL;
        }

        camera_metadata_entry_t streams = mSettings->find(ANDROID_CONTROL_AE_TARGET_FPS_RANGE);
        if (streams.count > 1) {
            if (streams.data.i32[0] > 15 || streams.data.i32[1] > 15) {
                fps = 30;
            } else {
                fps = 15;
            }
        }

        meta = mSettings;
        devStream = mVideoStream;
        callback = (camera3_callback_ops *)mCallbackOps;
    }
    sp<CaptureRequest> capture = new CaptureRequest();

    // configure VideoStream according to request type.
    if (request->settings != NULL) {
        if (meta->getRequestType() == TYPE_STILLCAP) {
            if (stillcap == NULL) {
                ALOGE("still capture intent but without jpeg stream");
                if (preview != NULL) {
                    stillcap = preview;
                } else if (callbackStream != NULL) {
                    stillcap = callbackStream;
                }
            }
            if (stillcap != NULL) {
                stillcap->setFps(fps);
                devStream->configure(stillcap);
            } else {
                ALOGW("%s: TYPE_STILLCAP, no stream found to config", __func__);
            }
        } else if (preview != NULL) {
            if (meta->getRequestType() != TYPE_SNAPSHOT) {
                preview->setFps(fps);
            }
            devStream->configure(preview);
        } else if (callbackStream != NULL) {
            callbackStream->setFps(fps);
            devStream->configure(callbackStream);
        } else if (stillcap != NULL) {
            stillcap->setFps(fps);
            devStream->configure(stillcap);
        } else if (record != NULL) {
            record->setFps(fps);
            devStream->configure(record);
        } else {
            ALOGW("%s: RequestType = %d, but preview and callback stream is null", __func__,
                  meta->getRequestType());
        }
    }

    capture->init(request, callback, meta);

    return devStream->requestCapture(capture);
}

bool Camera::isValidReprocessSettings(const camera_metadata_t * /*settings*/) {
    // TODO: reject settings that cannot be reprocessed
    // input buffers unimplemented, use this to reject reprocessing requests
    ALOGE("%s:%d: Input buffer reprocessing not implemented", __func__, mId);
    return false;
}

// do advanced character set.
int32_t Camera::processSettings(sp<Metadata> settings, uint32_t frame) {
    if (settings == NULL || settings->isEmpty()) {
        ALOGE("invalid settings");
        return 0;
    }

    // auto exposure control.
    camera_metadata_entry_t entry = settings->find(ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER);
    if (entry.count > 0) {
        // ANDROID_CONTROL_AE_PRECAPTURE_TRIGGER_START
        m3aState.aeState = ANDROID_CONTROL_AE_STATE_CONVERGED;
        ALOGV("ae precature trigger");
    } else {
        m3aState.aeState = ANDROID_CONTROL_AE_STATE_INACTIVE;
    }

    entry = settings->find(ANDROID_CONTROL_AE_PRECAPTURE_ID);
    if (entry.count > 0) {
        m3aState.aeTriggerId = entry.data.i32[0];
    }

    int64_t timestamp = 0;
    timestamp = systemTime(SYSTEM_TIME_BOOTTIME);
    settings->addInt64(ANDROID_SENSOR_TIMESTAMP, 1, &timestamp);

    settings->addUInt8(ANDROID_CONTROL_AE_STATE, 1, &m3aState.aeState);

    // auto focus control.
    m3aState.afState = ANDROID_CONTROL_AF_STATE_INACTIVE;
    settings->addUInt8(ANDROID_CONTROL_AF_STATE, 1, &m3aState.afState);

    // auto white balance control.
    m3aState.awbState = ANDROID_CONTROL_AWB_STATE_INACTIVE;
    settings->addUInt8(ANDROID_CONTROL_AWB_STATE, 1, &m3aState.awbState);

    entry = settings->find(ANDROID_CONTROL_AF_TRIGGER_ID);
    if (entry.count > 0) {
        m3aState.afTriggerId = entry.data.i32[0];
    }

    settings->addInt32(ANDROID_CONTROL_AF_TRIGGER_ID, 1, &m3aState.afTriggerId);
    settings->addInt32(ANDROID_CONTROL_AE_PRECAPTURE_ID, 1, &m3aState.aeTriggerId);

    notifyShutter(frame, timestamp);

    return 0;
}

void Camera::notifyShutter(uint32_t frame_number, uint64_t timestamp) {
    int32_t res;
    struct timespec ts;

    // If timestamp is 0, get timestamp from right now instead
    if (timestamp == 0) {
        ALOGW("%s:%d: No timestamp provided, using CLOCK_BOOTTIME", __func__, mId);
        res = clock_gettime(CLOCK_BOOTTIME, &ts);
        if (res == 0) {
            timestamp = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
        } else {
            ALOGE("%s:%d: No timestamp and failed to get CLOCK_BOOTTIME %s(%d)", __func__, mId,
                  strerror(errno), errno);
        }
    }
    camera3_notify_msg_t m;
    memset(&m, 0, sizeof(m));
    m.type = CAMERA3_MSG_SHUTTER;
    m.message.shutter.frame_number = frame_number;
    m.message.shutter.timestamp = timestamp;
    mCallbackOps->notify(mCallbackOps, &m);
}

void Camera::dumpDev(int32_t fd) {
    ALOGV("%s:%d: Dumping to fd %d", __func__, mId, fd);
    android::Mutex::Autolock al(mDeviceLock);

    dprintf(fd, "Camera ID: %d (Busy: %d)\n", mId, mBusy);

    // TODO: dump all settings
    dprintf(fd, "Most Recent Settings: (%p)\n", mSettings.get());

    dprintf(fd, "Number of streams: %d\n", mNumStreams);
    for (int32_t i = 0; i < mNumStreams; i++) {
        if (mStreams[i] == NULL)
            continue;

        dprintf(fd, "Stream %d/%d:\n", i, mNumStreams);
        mStreams[i]->dump(fd);
    }
}

const char *Camera::templateToString(int32_t type) {
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
        case CAMERA3_TEMPLATE_MANUAL:
            return "CAMERA3_TEMPLATE_MANUAL";
    }
    // TODO: support vendor templates
    return "Invalid template type!";
}

int32_t Camera::setTemplate(int32_t type, camera_metadata_t *settings) {
    android::Mutex::Autolock al(mDeviceLock);

    if (!isValidTemplateType(type)) {
        ALOGE("%s:%d: Invalid template request type: %d", __func__, mId, type);
        return -EINVAL;
    }

    if (mTemplates[type] != NULL && !mTemplates[type]->isEmpty()) {
        ALOGI("%s:%d: Setting already constructed template type %s(%d)", __func__, mId,
              templateToString(type), type);
        return 0;
    }

    // Make a durable copy of the underlying metadata
    mTemplates[type] = new Metadata(settings);
    if (mTemplates[type]->isEmpty()) {
        ALOGE("%s:%d: Failed to clone metadata %p for template type %s(%d)", __func__, mId,
              settings, templateToString(type), type);
        return -EINVAL;
    }
    return 0;
}

int32_t Camera::initDevice() {
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
    res = setManualTemplate();
    if (res)
        return res;

    return 0;
}

int32_t Camera::setPreviewTemplate() {
    Metadata base;
    // Create standard settings templates from copies of base metadata
    // TODO: use vendor tags in base metadata
    Metadata::createSettingTemplate(base, *this, CAMERA3_TEMPLATE_PREVIEW);

    // Setup default preview controls
    int32_t res =
            base.add1UInt8(ANDROID_CONTROL_CAPTURE_INTENT, ANDROID_CONTROL_CAPTURE_INTENT_PREVIEW);

    if (res)
        return res;
    // TODO: set fast auto-focus, auto-whitebalance, auto-exposure, auto flash
    return setTemplate(CAMERA3_TEMPLATE_PREVIEW, base.get());
}

int32_t Camera::setStillTemplate() {
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

int32_t Camera::setRecordTemplate() {
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

int32_t Camera::setSnapshotTemplate() {
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

int32_t Camera::setZslTemplate() {
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

int32_t Camera::setManualTemplate() {
    Metadata base;
    // Create manual template from copies of base metadata
    // TODO: use vendor tags in base metadata
    Metadata::createSettingTemplate(base, *this, CAMERA3_TEMPLATE_MANUAL);

    int32_t res =
            base.add1UInt8(ANDROID_CONTROL_CAPTURE_INTENT, ANDROID_CONTROL_CAPTURE_INTENT_MANUAL);
    // Setup default manual controls
    if (res)
        return res;
    // TODO: set reprocessing parameters for manual input queue
    return setTemplate(CAMERA3_TEMPLATE_MANUAL, base.get());
}

bool Camera::isValidCaptureSettings(const camera_metadata_t *settings __unused) {
    // TODO: reject settings that cannot be captured
    return true;
}

int32_t Camera::getV4l2Res(uint32_t streamWidth, uint32_t streamHeight, uint32_t *pV4l2Width,
                           uint32_t *pV4l2Height) {
    if ((pV4l2Width == NULL) || (pV4l2Height == NULL)) {
        ALOGE("%s, para null", __func__);
        return BAD_VALUE;
    }

    *pV4l2Width = streamWidth;
    *pV4l2Height = streamHeight;
    return NO_ERROR;
}

//---------------------------------------------------------
extern "C" {
// Get handle to camera from device priv data
static Camera *camdev_to_camera(const camera3_device_t *dev) {
    return reinterpret_cast<Camera *>(dev->priv);
}

static int32_t initialize(const camera3_device_t *dev, const camera3_callback_ops_t *callback_ops) {
    return camdev_to_camera(dev)->initializeDev(callback_ops);
}

static int32_t configure_streams(const camera3_device_t *dev,
                                 camera3_stream_configuration_t *stream_list) {
    return camdev_to_camera(dev)->configureStreams(stream_list);
}

static int32_t register_stream_buffers(const camera3_device_t *dev,
                                       const camera3_stream_buffer_set_t *buffer_set) {
    return camdev_to_camera(dev)->registerStreamBuffers(buffer_set);
}

static const camera_metadata_t *construct_default_request_settings(const camera3_device_t *dev,
                                                                   int32_t type) {
    return camdev_to_camera(dev)->constructDefaultRequestSettings(type);
}

static int32_t process_capture_request(const camera3_device_t *dev,
                                       camera3_capture_request_t *request) {
    return camdev_to_camera(dev)->processCaptureRequest(request);
}

static void dump(const camera3_device_t *dev, int32_t fd) {
    camdev_to_camera(dev)->dumpDev(fd);
}

static int32_t flush(const camera3_device_t *dev) {
    return camdev_to_camera(dev)->flushDev();
}

} // extern "C"

const camera3_device_ops_t Camera::sOps = {
        .initialize = initialize,
        .configure_streams = configure_streams,
        .register_stream_buffers = register_stream_buffers,
        .construct_default_request_settings = construct_default_request_settings,
        .process_capture_request = process_capture_request,
        .get_metadata_vendor_tag_ops = NULL,
        .dump = dump,
        .flush = flush,
        .reserved = {0},
};
