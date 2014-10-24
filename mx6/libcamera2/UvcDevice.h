/*
 * Copyright (C) 2012-2014 Freescale Semiconductor, Inc.
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

#ifndef _UVC_DEVICE_H
#define _UVC_DEVICE_H

#include "CameraUtil.h"
#include "DeviceAdapter.h"

#define DEFAULT_PREVIEW_FPS (15)
#define DEFAULT_PREVIEW_W   (640)
#define DEFAULT_PREVIEW_H   (480)
#define DEFAULT_PICTURE_W   (640)
#define DEFAULT_PICTURE_H   (480)
#define FORMAT_STRING_LEN 64
#define MAX_DEQUEUE_WAIT_TIME  (5000)  //5000ms for uvc camera

using namespace android;

class UvcDevice : public DeviceAdapter {
public:
    UvcDevice();
    ~UvcDevice();

    virtual status_t initSensorInfo(const CameraInfo& info);
    virtual status_t setDeviceConfig(int         width,
                                     int         height,
                                     PixelFormat format,
                                     int         fps);
    virtual void setPreviewPixelFormat();
    virtual void setPicturePixelFormat();
    virtual status_t registerCameraBuffers(CameraFrame *pBuffer, int &num);
    virtual status_t fillCameraFrame(CameraFrame *frame);
    virtual CameraFrame * acquireCameraFrame();
    virtual status_t startDeviceLocked();
    virtual status_t stopDeviceLocked();

protected:
    status_t adjustPreviewResolutions();
    status_t setMaxPictureResolutions();
    void adjustSensorFormats(int *src, int len);

private:
    void convertYUYUToNV12(StreamBuffer *dst, StreamBuffer *src);
    void doColorConvert(StreamBuffer *dst, StreamBuffer *src);


protected:
    const char* pDevPath;
    int mDefaultFormat;
    bool mPreviewNeedCsc;
    bool mPictureNeedCsc;
    int mSensorFormats[MAX_SENSOR_FORMAT];
    CameraFrame mUvcBuffers[MAX_PREVIEW_BUFFER];
};

#endif // ifndef _UVC_DEVICE_H

