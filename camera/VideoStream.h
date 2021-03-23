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

#ifndef _IMX_STREAM_H
#define _IMX_STREAM_H

#include <linux/videodev2.h>
#include "CameraUtils.h"

namespace android {

class CameraDeviceSessionHwlImpl;

class VideoStream : public ImxStream
{
public:
    VideoStream(CameraDeviceSessionHwlImpl *pSession);
    virtual ~VideoStream();

    // open/close device stream.
    int32_t openDev(const char* name);
    int32_t closeDev();
//    int32_t flushDev();

    void setOmitFrameCount(uint32_t omitCount) { mOmitFrmCount = omitCount; }

    // configure device.
    virtual int32_t onDeviceConfigureLocked(uint32_t format, uint32_t width, uint32_t height, uint32_t fps) = 0; 
    // start device.
    virtual int32_t onDeviceStartLocked()  = 0;
    // stop device.
    virtual int32_t onDeviceStopLocked() = 0;

    // get buffer from V4L2.
    virtual ImxStreamBuffer* onFrameAcquireLocked()  = 0;
    // put buffer back to V4L2.
    virtual int32_t onFrameReturnLocked(ImxStreamBuffer& buf)  = 0;

    // allocate buffers.
    virtual int32_t allocateBuffersLocked()  = 0;
    // free buffers.
    virtual int32_t freeBuffersLocked()  = 0;

    virtual int32_t onFlushLocked();

    // Wrapper function for easy use when capture intent changed, succh as take picture when preview.
    // If same config, do nothing. If already start, need fisrt stop, free buffer, then config, alloc buffer, start.
    virtual int32_t ConfigAndStart(uint32_t format, uint32_t width, uint32_t height, uint32_t fps);
    virtual int32_t Stop();

    void SetBufferNumber(uint32_t num) { mNumBuffers = num; }

    virtual int32_t ISPProcess(void *pMeta) { return 0; };

protected:
    virtual int32_t postConfigure(uint32_t format, uint32_t width, uint32_t height, uint32_t fps);

public:
    uint32_t mFps = 0;
    uint32_t mNumBuffers = 0;


protected:
//    bool mPlane;
//    char mPath[CAMERA_SENSOR_LENGTH];
    int mDev = 0;
    uint32_t mAllocatedBuffers = 0;
    enum v4l2_memory mV4l2MemType = V4L2_MEMORY_MMAP;
    enum v4l2_buf_type mV4l2BufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    uint32_t mOmitFrames;
    uint32_t mOmitFrmCount;

    ImxStreamBuffer* mBuffers[MAX_STREAM_BUFFERS];
    bool mCustomDriver;
    bool mRegistered;
    bool mbStart;

    CameraDeviceSessionHwlImpl *mSession;
};

}  // namespace android

#endif
