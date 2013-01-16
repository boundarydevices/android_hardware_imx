/*
 * Copyright (C) 2012-2013 Freescale Semiconductor, Inc.
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
#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>
#include <ui/PixelFormat.h>
#include <system/window.h>
#include <system/camera.h>
#include "gralloc_priv.h"
#include <linux/videodev2.h>
#include <hardware/camera2.h>
#include "MetadaManager.h"

using namespace android;

//#define CAMERA_HAL_DEBUG
#ifdef CAMERA_HAL_DEBUG
# define FLOG_RUNTIME(format, ...) ALOGI((format), ## __VA_ARGS__)
//# define FLOG_TRACE(format, ...) ALOGI((format), ## __VA_ARGS__)
#else // ifdef CAMERA_HAL_DEBUG
# define FLOG_RUNTIME(format, ...)
//# define FLOG_TRACE(format, ...)
#endif // ifdef CAMERA_HAL_DEBUG

#define FLOG_TRACE(format, ...) ALOGI((format), ## __VA_ARGS__)
#define FLOGI(format, ...) ALOGI((format), ## __VA_ARGS__)
#define FLOGW(format, ...) ALOGW((format), ## __VA_ARGS__)
#define FLOGE(format, ...) ALOGE((format), ## __VA_ARGS__)

#define fAssert(e) ((e) ? (void)0 : __assert2(__FILE__, __LINE__, __func__, #e))

#define UVC_SENSOR_NAME "uvc"
#define OV5640_SENSOR_NAME "ov5640"
#define OV5642_SENSOR_NAME "ov5642"
#define V4LSTREAM_WAKE_LOCK "V4LCapture"

#define MAX_PREVIEW_BUFFER      6
#define MAX_CAPTURE_BUFFER      3
#define NUM_PREVIEW_BUFFER      4
#define NUM_RECORD_BUFFER       4
#define NUM_CAPTURE_BUFFER      2

#define CAMAERA_FILENAME_LENGTH 256
#define CAMERA_SENSOR_LENGTH    32
#define CAMERA_FORMAT_LENGTH    32
#define CAMER_PARAM_BUFFER_SIZE 512
#define PARAMS_DELIMITER ","
#define THREAD_WAIT_TIMEOUT 5000 * 1000 * 1000LL

#define MAX_RESOLUTION_SIZE   64
#define MAX_FPS_RANGE   4
#define MAX_SENSOR_FORMAT 20

#define CAMERA_GRALLOC_USAGE GRALLOC_USAGE_HW_TEXTURE | \
    GRALLOC_USAGE_HW_RENDER |                           \
    GRALLOC_USAGE_SW_READ_RARELY |                      \
    GRALLOC_USAGE_SW_WRITE_NEVER |                      \
    GRALLOC_USAGE_FORCE_CONTIGUOUS

#define ARRAY_SIZE(x) (sizeof(x)/sizeof((x)[0]))

#define CAMERA_MAX(x, y) (x) > (y) ? (x) : (y)

int         convertPixelFormatToV4L2Format(PixelFormat format);
PixelFormat convertV4L2FormatToPixelFormat(unsigned int format);
int         convertStringToPixelFormat(const char *pFormat);
int         convertStringToV4L2Format(const char *pFormat);

struct VideoMetadataBuffer
{
    size_t phyOffset;
    size_t length;
};

struct CameraInfo : public camera_info
{
    char name[CAMERA_SENSOR_LENGTH];
    char devPath[CAMAERA_FILENAME_LENGTH];
};

struct SensorInfo
{
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
    float mFocalLength;
};

struct VideoInfo
{
    struct v4l2_capability     cap;
    struct v4l2_format         format;
    struct v4l2_streamparm     param;
    struct v4l2_buffer         buf;
    struct v4l2_requestbuffers rb;
    bool                       isStreamOn;
    int                        width;
    int                        height;
    int                        formatIn;
    int                        framesizeIn;
};

class CameraFrame;

class CameraFrameObserver {
public:
    CameraFrameObserver() {}

    virtual ~CameraFrameObserver() {}

    virtual void handleFrameRelease(CameraFrame *buffer) = 0;

private:
    CameraFrameObserver(const CameraFrameObserver&);
    CameraFrameObserver& operator=(const CameraFrameObserver&);
};

struct StreamBuffer {
    int mWidth;
    int mHeight;
    int mFormat;
    void *mVirtAddr;
    int mPhyAddr;
    size_t mSize;
    buffer_handle_t mBufHandle;
    nsecs_t mTimeStamp;
};

class CameraFrame : public StreamBuffer {
public:
    enum CAMERA_BUFS_STATE {
        BUFS_CREATE      = -1,
        BUFS_FREE        = 0,
        BUFS_IN_CAPTURE  = 1,
        BUFS_IN_RECORDER = 2,
        BUFS_IN_PREIVIEW = 4,
        BUFS_IN_DRIVER   = 8,
        BUFS_IN_SERVICE  = 0x10
    };
    enum FrameType {
        INVALID_FRAME = 0,
        IMAGE_FRAME   = 1,
        PREVIEW_FRAME = 2,
    };

    CameraFrame() {}

    ~CameraFrame();

    void initialize(buffer_handle_t  buf_h,
                    int              index);
    void addState(CAMERA_BUFS_STATE state);
    void setState(CAMERA_BUFS_STATE state);
    int getState();
    void removeState(CAMERA_BUFS_STATE state);
    void release();
    void addReference();
    void setObserver(CameraFrameObserver *observer);
    void reset();

private:
    CameraFrame(const CameraFrame&);
    CameraFrame& operator=(const CameraFrame&);

public:
    FrameType mFrameType;
    int mIndex;

private:
    int mRefCount;
    int mBufState;
    CameraFrameObserver *mObserver;
};

enum CAMERA_ERROR {
    ERROR_FATAL = 1,
    ERROR_TINY  = 2,
};

class CameraErrorListener {
public:
    virtual void handleError(int err) = 0;
    virtual ~CameraErrorListener() {}
};

class CameraBufferListener {
public:
    virtual void onBufferCreat(CameraFrame *pBuffer, int num) = 0;
    virtual void onBufferDestroy() = 0;
    virtual ~CameraBufferListener() {}
};

class CameraBufferProvider {
public:
    enum BufferState {
        BUFFER_CREATE  = 1,
        BUFFER_DESTROY = 2,
    };
    CameraBufferProvider();
    virtual ~CameraBufferProvider();

    virtual int allocateBuffers(int width, int height,
                               int format, int numBufs) = 0;
    virtual int freeBuffers() = 0;

    void addBufferListener(CameraBufferListener *listener);
    void removeBufferListener(CameraBufferListener *listener);
    void clearBufferListeners();

    void dispatchBuffers(CameraFrame *pBuffer, int num, BufferState bufState);

private:
    Vector<int> mBufferListeners;
};

class CameraFrameListener {
public:
    virtual void handleCameraFrame(CameraFrame *frame) = 0;
    virtual ~CameraFrameListener() {}
};

class CameraFrameProvider {
public:
    CameraFrameProvider();
    virtual ~CameraFrameProvider();

    virtual int getFrameSize()  = 0;
    virtual int getFrameCount() = 0;
    void addFrameListener(CameraFrameListener *listener);
    void removeFrameListener(CameraFrameListener *listener);
    void clearFrameListeners();

    void dispatchCameraFrame(CameraFrame *frame);

private:
    Vector<int> mFrameListeners;
};

class CameraEvent : public LightRefBase<CameraEvent>{
public:
    enum CameraEventType {
        EVENT_INVALID = 0x0,
        EVENT_SHUTTER = 0x1,
        EVENT_FOCUS   = 0x2,
        EVENT_ZOOM    = 0x4,
        EVENT_FACE    = 0x8
    };

    CameraEvent()
        : mData(NULL), mEventType(EVENT_INVALID)
    {}

    void *mData;
    CameraEventType mEventType;
};

class CameraEventListener {
public:
    virtual void handleEvent(sp<CameraEvent>& event) = 0;
    virtual ~CameraEventListener() {}
};

class CameraEventProvider {
public:
    CameraEventProvider() {
        mEventListeners.clear();
    }

    void addEventListener(CameraEventListener *listerner);
    void removeEventListener(CameraEventListener *listerner);
    void clearEventListeners();
    void dispatchEvent(sp<CameraEvent>& event);

    virtual ~CameraEventProvider() {
        mEventListeners.clear();
    }

private:
    Vector<int> mEventListeners;
};

#endif // ifndef _CAMERA_UTILS_H
