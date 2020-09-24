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

#ifndef _MMAP_STREAM_H
#define _MMAP_STREAM_H

#include <inttypes.h>
#include <hal_types.h>
#include "VideoStream.h"

namespace android {

using google_camera_hal::Stream;

// stream uses memory map buffers which allcated in kernel space.
class MMAPStream : public VideoStream
{
public:
    MMAPStream(CameraDeviceSessionHwlImpl *pSession);
    MMAPStream(CameraDeviceSessionHwlImpl *pSession, bool mplane);
    virtual ~MMAPStream();

    // configure device.
    virtual int32_t onDeviceConfigureLocked(uint32_t format, uint32_t width, uint32_t height, uint32_t fps);
    // start device.
    virtual int32_t onDeviceStartLocked();
    // stop device.
    virtual int32_t onDeviceStopLocked();

    // get buffer from V4L2.
    virtual ImxStreamBuffer* onFrameAcquireLocked();
    // put buffer back to V4L2.
    virtual int32_t onFrameReturnLocked(ImxStreamBuffer& buf);

    // allocate buffers.
    virtual int32_t allocateBuffersLocked() {return 0;}
    // free buffers.
    virtual int32_t freeBuffersLocked() {return 0;}

public:
    bool mPlane;
};

}  // namespace android

#endif
