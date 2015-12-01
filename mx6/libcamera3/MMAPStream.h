/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
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

#ifndef _MMAP_STREAM_H
#define _MMAP_STREAM_H

#include "VideoStream.h"

// stream uses memory map buffers which allcated in kernel space.
class MMAPStream : public VideoStream
{
public:
    MMAPStream(Camera* device);
    virtual ~MMAPStream();

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

    // allocate buffers.
    virtual int32_t allocateBuffersLocked() {return 0;}
    // free buffers.
    virtual int32_t freeBuffersLocked() {return 0;}

private:

};

#endif
