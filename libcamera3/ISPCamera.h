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

#ifndef __ISPCAMERA_H
#define __ISPCAMERA_H

#include "Camera.h"
#include "MMAPStream.h"
#include "utils/CameraConfigurationParser.h"

using namespace cameraconfigparser;
class ISPCamera : public Camera {
public:
    ISPCamera(int32_t id, int32_t facing, int32_t orientation, char *path, CscHw cam_copy_hw,
              CscHw cam_csc_hw, const char *hw_jpeg_enc, CameraSensorMetadata *cam_metadata);
    ~ISPCamera();

    CameraSensorMetadata *mCameraMetadata;
    virtual status_t initSensorStaticData();
    virtual PixelFormat getPreviewPixelFormat();

private:
    class ISPCameraMMAPStream : public MMAPStream {
    public:
        ISPCameraMMAPStream(Camera *device, struct OmitFrame *omit_frame) : MMAPStream(device) {
            mOmitFrame = omit_frame;
        }
        virtual ~ISPCameraMMAPStream() {}

        // configure device.
        virtual int32_t onDeviceConfigureLocked();
        struct OmitFrame *mOmitFrame;
    };
};

#endif
