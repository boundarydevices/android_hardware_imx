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

#ifndef _OV5642_CSI_H
#define _OV5642_CSI_H

#include "Camera.h"
#include "USPStream.h"

class Ov5642Csi : public Camera
{
public:
    Ov5642Csi(int32_t id, int32_t facing, int32_t orientation, char* path);
    ~Ov5642Csi();

    virtual status_t initSensorStaticData();

    virtual uint8_t doAutoFocus(uint8_t mode);
    virtual uint8_t getAutoFocusStatus(uint8_t mode);
    virtual void    setAutoFocusRegion(int x, int y);

private:
    class OvStream : public USPStream {
    public:
        OvStream(Camera* device)
            : USPStream(device) {}
        virtual ~OvStream() {}

        // configure device.
        virtual int32_t onDeviceConfigureLocked();
    };
};

#endif

