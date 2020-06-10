/*
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

#ifndef _UVC_DEVICE_H
#define _UVC_DEVICE_H

#include "Camera.h"
#include "DMAStream.h"
#include "utils/CameraConfigurationParser.h"
using namespace cameraconfigparser;

class UvcDevice : public Camera
{
public:
    UvcDevice(int32_t id, int32_t facing, int32_t orientation,
              char* path, CscHw cam_copy_hw = GPU_2D, CscHw cam_csc_hw = GPU_2D, const char *hw_jpeg_enc = NULL,
              bool createStream = true, CameraSensorMetadata *cam_metadata = NULL);

    virtual ~UvcDevice();

    static Camera* newInstance(int32_t id, char* name, int32_t facing,
                               int32_t orientation, char* path,
                               CscHw cam_copy_hw, CscHw cam_csc_hw,const char *hw_jpeg_enc,
                               CameraSensorMetadata *cam_metadata);

    CameraSensorMetadata *mCameraMetadata;
    virtual status_t initSensorStaticData();
    virtual bool isHotplug() {return true;}

protected:
    class UvcStream : public DMAStream {
    public:
        UvcStream(Camera* device, const char* name, struct OmitFrame *omit_frame)
              : DMAStream(device) {
            strncpy(mUvcPath, name, CAMAERA_FILENAME_LENGTH-1);
            mOmitFrame = omit_frame;
        }
        virtual ~UvcStream() {}

        // configure device.
        virtual int32_t onDeviceConfigureLocked();
        // start device.
        virtual int32_t onDeviceStartLocked();
        // stop device.
        virtual int32_t onDeviceStopLocked();
        // get buffer from V4L2.
        virtual int32_t onFrameAcquireLocked();
        // put buffer back to V4L2.
        virtual int32_t onFrameReturnLocked(int32_t index, StreamBuffer& buf);

        // get device buffer required size.
        virtual int32_t getDeviceBufferSize();

    protected:
        char mUvcPath[CAMAERA_FILENAME_LENGTH];
        struct OmitFrame *mOmitFrame;
    };
};

#endif
