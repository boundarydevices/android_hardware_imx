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

#ifndef _TVIN_8DV__DEVICE_H_
#define _TVIN_8DV_DEVICE_H_

#include "Camera.h"
#include "USPStream.h"

// v4l2 resolution map to gralloc resolution
typedef struct tagResMap {
    uint32_t v4l2Width;
    uint32_t v4l2Height;
    uint32_t streamWidth;
    uint32_t streamHeight;
} TResMap;

class TVIN8DvDevice : public Camera
{
public:
    TVIN8DvDevice(int32_t id, int32_t facing, int32_t orientation, char *path);
    ~TVIN8DvDevice();

    virtual status_t initSensorStaticData();
    virtual PixelFormat getPreviewPixelFormat();

    virtual int32_t getV4l2Res(uint32_t streamWidth, uint32_t streamHeight, uint32_t *pV4l2Width, uint32_t *pV4l2Height);

    virtual int32_t allocTmpBuf(uint32_t size);
    virtual void freeTmpBuf();

private:
    class TVin8DvStream : public USPStream
    {
    public:
        TVin8DvStream(Camera *device)
            : USPStream(device)
        {
        }
        virtual ~TVin8DvStream()
        {
        }

        // configure device.
        virtual int32_t onDeviceConfigureLocked();
    };

private:
    v4l2_std_id mSTD;
    uint32_t mResCount;
    TResMap mResMap[MAX_RESOLUTION_SIZE];
};

#endif
