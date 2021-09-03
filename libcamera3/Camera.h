/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
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

#ifndef _CAMERA_H_
#define _CAMERA_H_

#include <hardware/hardware.h>
#include <hardware/camera3.h>
#include <utils/Mutex.h>
#include "Metadata.h"
#include "Stream.h"
#include "CameraUtils.h"
#include "utils/CameraConfigurationParser.h"

using namespace cameraconfigparser;
class VideoStream;
// Camera represents a physical camera on a device.
// This is constructed when the HAL module is loaded, one per physical camera.
// It is opened by the framework, and must be closed before it can be opened
// again.
// This is an abstract class, containing all logic and data shared between all
// camera devices (front, back, etc) and common to the ISP.
class Camera : public camera_info, public SensorData
{
public:
    // id is used to distinguish cameras. 0 <= id < NUM_CAMERAS.
    // module is a handle to the HAL module, used when the device is opened.
    Camera(int32_t id, int32_t facing, int32_t orientation, char* path, CscHw, CscHw, const char *);
    virtual ~Camera();

    static Camera* createCamera(int32_t id,
                                char* path, CscHw cam_copy_hw, CscHw cam_csc_hw,const char *hw_jpeg,
                                CameraSensorMetadata *cam_metadata, char* subdev_path);
    // do advanced character set.
    int32_t processSettings(sp<Metadata> settings, uint32_t frame);
    // Common Camera Device Operations (see <hardware/camera_common.h>)
    int32_t openDev(const hw_module_t *module, hw_device_t **device);
    int32_t getInfo(struct camera_info *info);
    int32_t closeDev();
    int32_t flushDev();
    virtual bool isHotplug() {return false;}

    // Camera v3 Device Operations (see <hardware/camera3.h>)
    int32_t initializeDev(const camera3_callback_ops_t *callback_ops);
    int32_t configureStreams(camera3_stream_configuration_t *stream_list);
    int32_t registerStreamBuffers(const camera3_stream_buffer_set_t *buf_set);
    const camera_metadata_t *constructDefaultRequestSettings(int32_t type);
    int32_t processCaptureRequest(camera3_capture_request_t *request);
    CscHw getBlitCopyHw() {return mCamBlitCopyType;}
    CscHw getBlitCscHw() {return mCamBlitCscType;}

    char *getHwEncoder() {return mJpegHw;};
    void dumpDev(int32_t fd);
    int32_t usemx6s;

    CscHw mCamBlitCopyType;
    CscHw mCamBlitCscType;

    char mJpegHw[32] = { 0 };;
    // some camera's resolution is not 16 pixels aligned, while gralloc is 16
    // pixels aligned.
    // Is just copy data from v4l2 to gralloc buffer, image  distortion
    virtual int32_t getV4l2Res(uint32_t streamWidth, uint32_t streamHeight, uint32_t *pV4l2Width, uint32_t *pV4l2Height);

    virtual int32_t allocTmpBuf(uint32_t /*size*/)
    {
        return 0;
    }
    virtual void freeTmpBuf(){};
    uint8_t *getTmpBuf()
    {
        return mTmpBuf;
    }

protected:
    // Initialize static camera characteristics for individual device
    virtual status_t initSensorStaticData() = 0;

    virtual uint8_t doAutoFocus(uint8_t mode) {
#pragma unused(mode)
        return ANDROID_CONTROL_AF_STATE_INACTIVE;
    }
    virtual uint8_t getAutoFocusStatus(uint8_t mode) {
#pragma unused(mode)
        return ANDROID_CONTROL_AF_STATE_INACTIVE;
    }
    virtual void setAutoFocusRegion(int x, int y) {
#pragma unused(x)
#pragma unused(y)
        return;
    }

    virtual void setPreviewPixelFormat();
    virtual void setPicturePixelFormat();

    // Verify settings are valid for a capture
    virtual bool isValidCaptureSettings(const camera_metadata_t *);
    // Separate initialization method for individual devices when opened
    virtual int32_t initDevice();
    // Accessor used by initDevice() to set the templates' metadata
    int32_t setTemplate(int32_t type, camera_metadata_t *static_info);
    // Prettyprint32_t template names
    const char* templateToString(int32_t type);

    // Initialize each template metadata controls
    int32_t setPreviewTemplate();
    int32_t setStillTemplate();
    int32_t setRecordTemplate();
    int32_t setSnapshotTemplate();
    int32_t setZslTemplate();
    int32_t setManualTemplate();

private:
    // Camera device handle returned to framework for use
    camera3_device_t mDevice;
    // Reuse a stream already created by this device
    sp<Stream> reuseStream(camera3_stream_t *astream);
    // Destroy all streams in a stream array, and the array itself
    void destroyStreams(sp<Stream> *array, int32_t count);
    // Verify a set of streams is valid in aggregate
    bool isValidStreamSet(sp<Stream> *array, int32_t count);
    // Verify settings are valid for reprocessing an input buffer
    bool isValidReprocessSettings(const camera_metadata_t *settings);
    // Send a shutter notify message with start of exposure time
    void notifyShutter(uint32_t frame_number, uint64_t timestamp);
    // Is type a valid template type (and valid index int32_to mTemplates)
    bool isValidTemplateType(int32_t type);

    // Identifier used by framework to distinguish cameras
    const int32_t mId;
    // camera_metadata structure containing static characteristics
    camera_metadata_t *mStaticInfo;
    // Busy flag indicates camera is in use
    bool mBusy;
    // Camera device operations handle shared by all devices
    const static camera3_device_ops_t sOps;
    // Methods used to call back into the framework
    const camera3_callback_ops_t *mCallbackOps;
    // Lock protecting the Camera object for modifications
    android::Mutex mDeviceLock;
    // Lock protecting only static camera characteristics, which may
    // be accessed without the camera device open
    static android::Mutex sStaticInfoLock;
    // Array of handles to streams currently in use by the device
    sp<Stream> *mStreams;
    // Number of streams in mStreams
    int32_t mNumStreams;
    // Static array of standard camera settings templates
    sp<Metadata> mTemplates[CAMERA3_TEMPLATE_COUNT];
    // Most recent request settings seen, memoized to be reused
    sp<Metadata> mSettings;

protected:
    sp<VideoStream> mVideoStream;
    autoState m3aState;
    uint8_t *mTmpBuf;  // used for soft csc temp buffer
};

#endif // CAMERA_H_
