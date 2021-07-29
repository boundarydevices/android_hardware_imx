/*
 *  Copyright 2020 NXP.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef CAMERA_UTILS_H
#define CAMERA_UTILS_H

#ifndef LOG_TAG
#define LOG_TAG "NXPCamera"
#endif

#include <utils/Log.h>
#include <inttypes.h>
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
#include <utils/Vector.h>
#include <utils/KeyedVector.h>
#include <cutils/properties.h>
#include <ui/PixelFormat.h>
#include <graphics_ext.h>
#include "Memory.h"
#include "hal_camera_metadata.h"
#include <hal_types.h>
#include "CameraConfigurationParser.h"

#define UVC_NAME "uvc"
#define ISP_SENSOR_NAME "viv_v4l2"

#define BACK_CAMERA_NAME "back"
#define FRONT_CAMERA_NAME "front"
#define MAX_CAMERAS 2

#define FACE_CAMERA_NAME "camera_name"
#define FACE_CAMERA_ORIENT "camera_orient"
#define DEFAULT_ERROR_NAME '0'
#define DEFAULT_ERROR_NAME_str "0"
#define IMX8_BOARD_NAME "imx8"
#define IMX7_BOARD_NAME "imx7"

#define CAMAERA_FILENAME_LENGTH 256
#define CAMERA_SENSOR_LENGTH    92
#define CAMERA_FORMAT_LENGTH    32
#define CAMER_PARAM_BUFFER_SIZE 512
#define PARAMS_DELIMITER ","

#define MAX_RESOLUTION_SIZE   64
#define MAX_FPS_RANGE 12
#define MAX_SENSOR_FORMAT 20

#define MAX_VPU_SUPPORT_FORMAT 2
#define MAX_PICTURE_SUPPORT_FORMAT 2

#define CAMERA_SYNC_TIMEOUT 5000 // in msecs
#define MAX_STREAM_BUFFERS 32

#define CAMERA_GRALLOC_USAGE_JPEG GRALLOC_USAGE_HW_TEXTURE | \
    GRALLOC_USAGE_HW_RENDER |                           \
    GRALLOC_USAGE_SW_READ_RARELY |                      \
    GRALLOC_USAGE_SW_WRITE_NEVER |                      \
    GRALLOC_USAGE_HW_CAMERA_WRITE

#define CAMERA_GRALLOC_USAGE GRALLOC_USAGE_HW_TEXTURE |         \
                                 GRALLOC_USAGE_HW_RENDER |      \
                                 GRALLOC_USAGE_SW_READ_NEVER | \
                                 GRALLOC_USAGE_SW_WRITE_NEVER | \
                                 GRALLOC_USAGE_HW_CAMERA_WRITE

#define NUM_PREVIEW_BUFFER      2
#define NUM_CAPTURE_BUFFER      1

#define ARRAY_SIZE(a) (sizeof(a) / sizeof(a[0]))
#define  ALIGN_PIXEL_4(x)  ((x+ 3) & ~3)
#define  ALIGN_PIXEL_16(x)  ((x+ 15) & ~15)
#define  ALIGN_PIXEL_32(x)  ((x+ 31) & ~31)

#define FUNC_TRACE() ALOGI("enter into %s", __func__)

namespace android {
using google_camera_hal::CameraDeviceStatus;
using google_camera_hal::HalCameraMetadata;

class ImxStream
{
public:
    ImxStream() {}

    virtual ~ImxStream() = default;

    ImxStream(uint32_t width, uint32_t height, int32_t format, uint64_t usage, int32_t id, bool bPreview = false)
    {
        mWidth = width;
        mHeight = height;
        mFormat = format;
        mUsage = usage;
        mId = id;
        mbPreview = bPreview;
    }

    uint32_t width() {return mWidth;}
    uint32_t height() {return mHeight;}
    int32_t format() {return mFormat;}
    uint64_t usage() {return mUsage;}
    int32_t id() {return mId;}
    bool isPreview() {return mbPreview;}

public:
    uint32_t mWidth = 0;
    uint32_t mHeight = 0;
    int32_t mFormat = 0;
    uint64_t mUsage = 0;
    int32_t mId = 0;
    bool mbPreview = false;
};

struct SensorSet
{
    // parameters from init.rc
    char mPropertyName[PROPERTY_VALUE_MAX];
    int32_t mFacing;
    int32_t mOrientation;

    // parameters by enum dynamically.
    char mSensorName[PROPERTY_VALUE_MAX];
    char mDevPath[CAMAERA_FILENAME_LENGTH];

    // parameters for extension.
    int32_t mResourceCost;
    uint32_t mConflictingSize;
    char** mConflictingDevices;

    // indicate sensor plug in/out.
    bool mExisting;
};

typedef struct tag_nxp_srream_buffer {
    void* mVirtAddr;
    uint64_t mPhyAddr;
    size_t mSize;        // the allocated buffer size, usually great than mFormatSize due to alignment.
    size_t mFormatSize;  // the actual size caculated by format and resolution.
    int32_t index;
    buffer_handle_t buffer;
    int32_t mFd;
    ImxStream *mStream;
} ImxStreamBuffer;

int getCaptureMode(int fd, int width, int height);
int convertPixelFormatToV4L2Format(PixelFormat format, bool invert = false);
int32_t changeSensorFormats(int *src, int *dst, int len);
int32_t getSizeByForamtRes(int32_t format, uint32_t width, uint32_t height, bool align);
cameraconfigparser::PhysicalMetaMapPtr ClonePhysicalDeviceMap(const cameraconfigparser::PhysicalMetaMapPtr& src);

int AllocPhyBuffer(ImxStreamBuffer &imxBuf);
int FreePhyBuffer(ImxStreamBuffer &imxBuf);

int yuv422iResize(uint8_t *srcBuf,
        int      srcWidth,
        int      srcHeight,
        uint8_t *dstBuf,
        int      dstWidth,
        int      dstHeight);

int yuv422spResize(uint8_t *srcBuf,
        int      srcWidth,
        int      srcHeight,
        uint8_t *dstBuf,
        int      dstWidth,
        int      dstHeight);

int yuv420spResize(uint8_t *srcBuf,
        int      srcWidth,
        int      srcHeight,
        uint8_t *dstBuf,
        int      dstWidth,
        int      dstHeight);

} // namespace android

#endif  // CAMERA_UTILS_H
