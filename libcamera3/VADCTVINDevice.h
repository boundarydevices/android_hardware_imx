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

#ifndef _VADCTVIN_DEVICE_H_
#define _VADCTVIN_DEVICE_H_

#include "Camera.h"
#include "MMAPStream.h"

class VADCTVINDevice : public Camera
{
public:
    VADCTVINDevice(int32_t id, int32_t facing, int32_t orientation, char* path);
    ~VADCTVINDevice();

    virtual status_t initSensorStaticData();

private:
    class VADCTVinStream : public MMAPStream
    {
        public:
            VADCTVinStream(Camera* device)
                : MMAPStream(device) {}
            virtual ~VADCTVinStream() {}

            StreamBuffer* mV4L2Buffers[MAX_STREAM_BUFFERS];
            // configure device.
            virtual int32_t onDeviceConfigureLocked();
            // start device.
            virtual int32_t onDeviceStartLocked();
            // stop device.
            virtual int32_t onDeviceStopLocked();

            int32_t allocateFrameBuffersLocked();
            // get buffer from V4L2.
            virtual int32_t onFrameAcquireLocked();

            // allocate buffers.
            virtual int32_t allocateBuffersLocked() {return 0;}
            // free buffers.
            virtual int32_t freeBuffersLocked() {return 0;}
            // free CSC buffers.
            virtual int32_t freeFrameBuffersLocked();
            virtual int32_t getFormatSize();
            //int32_t mIonFd;
        protected:
            int32_t mIonFd;
        private:

    };
private:
    v4l2_std_id mSTD;
};

#endif
