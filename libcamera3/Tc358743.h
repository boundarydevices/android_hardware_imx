/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
 * Copyright (C) 2016 Boundary Devices, Inc.
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

#ifndef _TC358743_H_
#define _TC358743_H_

#include "Camera.h"
#include "USPStream.h"

class Tc358743 : public Camera
{
public:
    Tc358743(int32_t id, int32_t facing, int32_t orientation, char* path);
    ~Tc358743();

    virtual status_t initSensorStaticData();
    virtual int getCaptureMode(int width, int height);

private:
    class TcStream : public USPStream {
    public:
        TcStream(Camera* device)
            : USPStream(device) {}
        virtual ~TcStream() {}

        // configure device.
        virtual int32_t onDeviceConfigureLocked();
    };
};

#endif
