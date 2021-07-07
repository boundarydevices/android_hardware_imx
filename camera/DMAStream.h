/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
 * Copyright 2020 NXP.
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

#ifndef _DMA_STREAM_H
#define _DMA_STREAM_H

#include "MMAPStream.h"

namespace android {

// stream uses DMABUF buffers which allcated in user space.
// that exports DMABUF handle.
class DMAStream : public MMAPStream
{
public:
    DMAStream(CameraDeviceSessionHwlImpl *pSession);
    DMAStream(bool mplane, CameraDeviceSessionHwlImpl *pSession);
    virtual ~DMAStream();

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
    virtual int32_t allocateBuffersLocked();
    // free buffers.
    virtual int32_t freeBuffersLocked();

    // get device buffer required size.
    virtual int32_t getDeviceBufferSize();

private:
    int32_t mStreamSize;
   // bool mPlane;
};

} // namespace android {

#endif
