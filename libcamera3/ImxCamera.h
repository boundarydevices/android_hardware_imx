/*
 *  Copyright 2019 NXP.
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

#ifndef __IMXCAMERA_H
#define __IMXCAMERA_H

#include "Camera.h"
#include "DMAStream.h"
#include "utils/CameraConfigurationParser.h"

using namespace cameraconfigparser;
class ImxCamera: public Camera
{
public:
    ImxCamera(int32_t id, int32_t facing, int32_t orientation, char *path, CscHw cam_copy_hw,
              CscHw cam_csc_hw, const char *hw_jpeg_enc, CameraSensorMetadata *cam_metadata, char *subdev_path);
    ~ImxCamera();

    CameraSensorMetadata *mCameraMetadata;
    virtual status_t initSensorStaticData();
    virtual PixelFormat getPreviewPixelFormat();

    virtual uint8_t doAutoFocus(uint8_t mode);
    virtual uint8_t getAutoFocusStatus(uint8_t mode);
    virtual void    setAutoFocusRegion(int x, int y);

private:
    bool mAFSupported = false;
    char mAFDevPath[CAMAERA_FILENAME_LENGTH];
    int isAutoFocusSupported(void);
    class ImxCameraMMAPStream : public MMAPStream
    {
    public:
        ImxCameraMMAPStream(Camera *device, struct OmitFrame *omit_frame)
            : MMAPStream(device) { mOmitFrame = omit_frame; }
        virtual ~ImxCameraMMAPStream() {}

        virtual int getCaptureMode(int width, int height);
        // configure device.
        virtual int32_t onDeviceConfigureLocked();
        struct OmitFrame *mOmitFrame;
    };
    class ImxCameraDMAStream : public DMAStream
    {
    public:
        ImxCameraDMAStream(Camera *device, struct OmitFrame *omit_frame)
            :  DMAStream(device, true) {
               mV4l2BufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
               mOmitFrame = omit_frame;
        }
        virtual ~ImxCameraDMAStream() {}

        virtual int getCaptureMode(int width, int height);
        // configure device.
        virtual int32_t onDeviceConfigureLocked();
        struct OmitFrame *mOmitFrame;
    };
};

#endif
