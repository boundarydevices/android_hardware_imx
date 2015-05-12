/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2012-2015 Freescale Semiconductor, Inc.
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
#include "gralloc_priv.h"
#include <linux/videodev2.h>
#include <hardware/camera.h>

using namespace android;

#define CAMERA_HAL_DEBUG
#ifdef CAMERA_HAL_DEBUG
# define FLOG_RUNTIME(format, ...) ALOGI((format), ## __VA_ARGS__)
# define FLOG_TRACE(format, ...) ALOGI((format), ## __VA_ARGS__)
#else // ifdef CAMERA_HAL_DEBUG
# define FLOG_RUNTIME(format, ...)
# define FLOG_TRACE(format, ...)
#endif // ifdef CAMERA_HAL_DEBUG

#define FLOGI(format, ...) ALOGI((format), ## __VA_ARGS__)
#define FLOGW(format, ...) ALOGW((format), ## __VA_ARGS__)
#define FLOGE(format, ...) ALOGE((format), ## __VA_ARGS__)

#define FSL_ASSERT(cond, ...) ALOG_ASSERT((cond), ## __VA_ARGS__)

#define UVC_SENSOR_NAME "uvc"
#define OV5640_SENSOR_NAME "csi"
#define OV5642_SENSOR_NAME "ov5642"
#define ADV7180_TVIN_NAME "adv7180_decoder"
#define VADC_TVIN_NAME "mx6s-csi"
#define V4LSTREAM_WAKE_LOCK "V4LCapture"

#define MAX_PREVIEW_BUFFER      6
#define MAX_CAPTURE_BUFFER      3
#define DISPLAY_WAIT_TIMEOUT    5000
#define CAMAERA_FILENAME_LENGTH 256
#define CAMERA_SENSOR_LENGTH    92
#define CAMERA_FORMAT_LENGTH    32
#define CAMER_PARAM_BUFFER_SIZE 512
#define PARAMS_DELIMITER ","
#define THREAD_WAIT_TIMEOUT 500 * 1000 * 1000

#define MAX_SENSOR_FORMAT 20
#define FORMAT_STRING_LEN 64

#define CAMERA_GRALLOC_USAGE GRALLOC_USAGE_HW_TEXTURE | \
    GRALLOC_USAGE_HW_RENDER |                           \
    GRALLOC_USAGE_SW_READ_RARELY |                      \
    GRALLOC_USAGE_SW_WRITE_NEVER |                      \
    GRALLOC_USAGE_FORCE_CONTIGUOUS

#define CAMERA_MAX(x, y) (x) > (y) ? (x) : (y)

int         convertPixelFormatToV4L2Format(PixelFormat format);
PixelFormat convertV4L2FormatToPixelFormat(unsigned int format);
int         convertStringToPixelFormat(const char *pFormat);
int         convertStringToV4L2Format(const char *pFormat);

#define MAX_DEQUEUE_WAIT_TIME  (5000)  //5000ms for uvc/tvin camera

typedef struct tagMemmapBuf
{
        unsigned char *start; //vir
        size_t offset; //phy
        unsigned int length;
}MemmapBuf;


int GetDevPath(const char  *pCameraName,
               char        *pCameraDevPath,
               unsigned int pathLen);

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


class CameraFrame {
public:
    enum CAMERA_BUFS_STATE {
        BUFS_CREATE      = 0,
        BUFS_IN_CAPTURE  = 1,
        BUFS_IN_RECORDER = 2,
        BUFS_IN_PREIVIEW = 4,
        BUFS_IN_DRIVER   = 8
    };
    enum FrameType {
        INVALID_FRAME = 0,
        IMAGE_FRAME   = 1,
        PREVIEW_FRAME = 2,
    };

    CameraFrame() {}

    ~CameraFrame();

    void initialize(buffer_handle_t *buf_h,
                    int              index);
    void addState(CAMERA_BUFS_STATE state);
    void removeState(CAMERA_BUFS_STATE state);
    CAMERA_BUFS_STATE getState();
    void release();
    void addReference();
    void setObserver(CameraFrameObserver *observer);
    void reset();
#ifdef NO_GPU
    void backupYUYV();
#endif
private:
    CameraFrame(const CameraFrame&);
    CameraFrame& operator=(const CameraFrame&);

public:
    buffer_handle_t *mBufHandle;
    void *mVirtAddr;
    int mPhyAddr;
    size_t mSize;
    int mWidth;
    int mHeight;
    int mFormat;
    FrameType mFrameType;
    int mIndex;
#ifdef NO_GPU
    void *mBackupVirtAddr;
#endif

private:
    CameraFrameObserver *mObserver;
    atomic_int mRefCount;
    int                  mBufState;
    mutable Mutex mCFLock;
};

enum CAMERA_ERROR {
    ERROR_FATAL = 1,
    ERROR_TINY  = 2,
};

class CameraErrorListener {
public:
    virtual void handleError(CAMERA_ERROR err) = 0;
    virtual ~CameraErrorListener() {}
};

class CameraBufferListener {
public:
    virtual void onBufferCreat(CameraFrame *pBuffer,
                               int          num) = 0;
    virtual void onBufferDestroy()               = 0;
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

    virtual int allocatePreviewBuffer(int width,
                                      int height,
                                      int format,
                                      int numBufs) = 0;
    virtual int allocatePictureBuffer(int width,
                                      int height,
                                      int format,
                                      int numBufs) = 0;
    virtual int freeBuffer()                       = 0;
    virtual int maxQueueableBuffers()              = 0;

    void        addBufferListener(CameraBufferListener *listener);
    void        removeBufferListener(CameraBufferListener *listener);
    void        clearBufferListeners();

    void        dispatchBuffers(CameraFrame *pBuffer,
                                int          num,
                                BufferState  bufState);

private:
    Vector<int> mBufferListeners;
};

class CameraFrameListener {
public:
    virtual void handleCameraFrame(CameraFrame *frame) = 0;
    virtual ~CameraFrameListener() {}
    virtual bool IsListenForVideo() { return false; }
};

class CameraFrameProvider {
public:
    CameraFrameProvider();
    virtual ~CameraFrameProvider();

    virtual int getFrameSize()  = 0;
    virtual int getFrameCount() = 0;
    void        addFrameListener(CameraFrameListener *listener);
    void        removeFrameListener(CameraFrameListener *listener);
    void        clearFrameListeners();

    void        dispatchCameraFrame(CameraFrame *frame);
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
