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

#ifndef _CAMERA_UTILS_H
#define _CAMERA_UTILS_H

#undef LOG_TAG
#define LOG_TAG "FslCameraHAL"
#include <utils/Log.h>

#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/time.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <utils/threads.h>
#include <utils/RefBase.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <camera/CameraParameters.h>
#include <utils/Vector.h>
#include <utils/KeyedVector.h>
#include <cutils/properties.h>
#include <hardware_legacy/power.h>
#include <ui/PixelFormat.h>
#include <hardware/camera3.h>
#include "gralloc_priv.h"

#define MAX_CAMERAS 2

#define FACE_CAMERA_NAME "camera_name"
#define FACE_CAMERA_ORIENT "camera_orient"
#define DEFAULT_ERROR_NAME '0'
#define DEFAULT_ERROR_NAME_str "0"

#define UVC_SENSOR_NAME "uvc"
#define OV5640MIPI_SENSOR_NAME "ov5640_mipi"
#define OV5642CSI_SENSOR_NAME "ov5642_camera"
#define OV5640CSI_SENSOR_NAME "ov5640_camera"
#define ADV7180_TVIN_NAME "adv7180_decoder"

#define CAMAERA_FILENAME_LENGTH 256
#define CAMERA_SENSOR_LENGTH    92
#define CAMERA_FORMAT_LENGTH    32
#define CAMER_PARAM_BUFFER_SIZE 512
#define PARAMS_DELIMITER ","

#define MAX_RESOLUTION_SIZE   64
#define MAX_FPS_RANGE   4
#define MAX_SENSOR_FORMAT 20

#define MAX_VPU_SUPPORT_FORMAT 2
#define MAX_PICTURE_SUPPORT_FORMAT 2

#define CAMERA_SYNC_TIMEOUT 5000 // in msecs
#define MAX_STREAM_BUFFERS 32

#define CAMERA_GRALLOC_USAGE_JPEG GRALLOC_USAGE_HW_TEXTURE | \
    GRALLOC_USAGE_HW_RENDER |                           \
    GRALLOC_USAGE_SW_READ_RARELY |                      \
    GRALLOC_USAGE_SW_WRITE_NEVER

#define CAMERA_GRALLOC_USAGE GRALLOC_USAGE_HW_TEXTURE | \
    GRALLOC_USAGE_HW_RENDER |                           \
    GRALLOC_USAGE_SW_READ_RARELY |                      \
    GRALLOC_USAGE_SW_WRITE_NEVER |                      \
    GRALLOC_USAGE_FORCE_CONTIGUOUS

#define NUM_PREVIEW_BUFFER      2
#define NUM_CAPTURE_BUFFER      1

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))

using namespace android;

int convertPixelFormatToV4L2Format(PixelFormat format, bool invert=false);

class Metadata;
class Stream;

// 3aState
struct autoState
{
    uint8_t aeMode;
    uint8_t afMode;
    uint8_t awbMode;
    uint8_t aeState;
    uint8_t afState;
    uint8_t awbState;
    int32_t afTriggerId;
    int32_t aeTriggerId;
};

class StreamBuffer
{
public:
    StreamBuffer();
    ~StreamBuffer();
    void initialize(buffer_handle_t* buf_h);
    // buffer width, height, format is in Stream.
    sp<Stream> mStream;
    int32_t mAcquireFence;

    buffer_handle_t* mBufHandle;
    void*   mVirtAddr;
    int32_t mPhyAddr;
    size_t  mSize;
    int32_t mFd;
};

enum RequestType {
    TYPE_PREVIEW = 1,
    TYPE_SNAPSHOT = 2,
    TYPE_STILLCAP = 3
};

class CaptureRequest : public LightRefBase<CaptureRequest>
{
public:
    CaptureRequest();
    ~CaptureRequest();

    void init(camera3_capture_request* request, camera3_callback_ops* callback,
              sp<Metadata> settings);
    int32_t onCaptureDone(StreamBuffer* buffer);
    int32_t onSettingsDone(sp<Metadata> meta);

public:
    uint32_t mFrameNumber;
    sp<Metadata> mSettings;
    uint32_t mOutBuffersNumber;
    StreamBuffer* mOutBuffers[MAX_STREAM_BUFFERS];

    camera3_capture_request* mRequest;
    camera3_callback_ops *mCallbackOps;
};

class SensorData
{
public:
    SensorData();
    virtual ~SensorData();

    int getCaptureMode(int width, int height);

    PixelFormat getPreviewPixelFormat() {
        return mPreviewPixelFormat;
    }

    PixelFormat getPicturePixelFormat() {
        return mPicturePixelFormat;
    }

    PixelFormat getMatchFormat(int *sfmt, int  slen,
                               int *dfmt, int  dlen);

    int32_t getSensorFormat(int32_t availFormat);

protected:
    int32_t changeSensorFormats(int *src, int *dst, int len);
    status_t adjustPreviewResolutions();
    status_t setMaxPictureResolutions();

public:
    int mPreviewResolutions[MAX_RESOLUTION_SIZE];
    int mPreviewResolutionCount;
    int mPictureResolutions[MAX_RESOLUTION_SIZE];
    int mPictureResolutionCount;
    int mAvailableFormats[MAX_SENSOR_FORMAT];
    int mAvailableFormatCount;
    nsecs_t mMinFrameDuration;
    nsecs_t mMaxFrameDuration;
    int mTargetFpsRange[MAX_FPS_RANGE];
    int mMaxWidth;
    int mMaxHeight;
    float mPhysicalWidth;
    float mPhysicalHeight;
    float mFocalLength;

    // preview and picture format.
    PixelFormat mPicturePixelFormat;
    PixelFormat mPreviewPixelFormat;

    // vpu and capture limitation.
    int mVpuSupportFmt[MAX_VPU_SUPPORT_FORMAT];
    int mPictureSupportFmt[MAX_PICTURE_SUPPORT_FORMAT];

    int mSensorFormats[MAX_SENSOR_FORMAT];
    int mSensorFormatCount;
    char mDevPath[CAMAERA_FILENAME_LENGTH];
};

#endif
