/*
 * Copyright (C) 2012-2016 Freescale Semiconductor, Inc.
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

#ifndef _UVC_MJPEG_DEVICE_H
#define _UVC_MJPEG_DEVICE_H

#include "Camera.h"
#include "DMAStream.h"
#include "MJPGStream.h"

class UvcMJPGDevice : public Camera
{
public:
    UvcMJPGDevice(int32_t id, int32_t facing, int32_t orientation,
              char* path, bool createStream = true);
    virtual ~UvcMJPGDevice();
    virtual status_t initSensorStaticData();
    virtual bool isHotplug() {return true;}

protected:

    class UvcStream : public MJPGStream {
        public:

            UvcStream(Camera* device, const char* name)
                : MJPGStream(device) {
                    strncpy(mUvcPath, name, CAMAERA_FILENAME_LENGTH-1);
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
    };
};

#endif
