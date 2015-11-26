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

#ifndef _DEVICE_STREAM_H
#define _DEVICE_STREAM_H

#include <utils/threads.h>
#include "MessageQueue.h"
#include "CameraUtils.h"
#include "Stream.h"
#include "Camera.h"

using namespace android;

class Camera;

class DeviceStream : public Stream
{
public:
    DeviceStream(Camera* device);
    virtual ~DeviceStream();

    // configure device stream.
    int32_t configure(sp<Stream> stream);
    //send capture request for stream.
    int32_t requestCapture(sp<CaptureRequest> req);

    // open/close device stream.
    int32_t openDev(const char* name);
    int32_t closeDev();

    virtual void* getG2dHandle() {return g2dHandle;}

private:
    // message type.
    static const int32_t MSG_CONFIG = 0x100;
    static const int32_t MSG_FRAME = 0x103;
    static const int32_t MSG_CLOSE = 0x104;
    static const int32_t MSG_EXIT  = 0x105;

    // device stream state.
    static const int32_t STATE_INVALID = 0x201;
    static const int32_t STATE_CONFIG = 0x202;
    static const int32_t STATE_START = 0x203;
    static const int32_t STATE_STOP  = 0x204;

protected:
    // handle configure message internally.
    int32_t handleConfigureLocked();
    virtual int32_t onDeviceConfigureLocked() = 0;
    // handle start message internally.
    int32_t handleStartLocked(bool force);
    virtual int32_t onDeviceStartLocked() = 0;
    // handle stop message internally.
    int32_t handleStopLocked(bool force);
    virtual int32_t onDeviceStopLocked() = 0;
    // handle frame message internally.
    int32_t handleCaptureFrame();

    // process capture request with lock.
    int32_t processCaptureRequest(StreamBuffer& src, sp<CaptureRequest> req);
    // process capture advanced settings with lock.
    int32_t processCaptureSettings(sp<CaptureRequest> req);
    // get buffer from V4L2.
    StreamBuffer* acquireFrameLocked();
    virtual int32_t onFrameAcquireLocked() = 0;
    // put buffer back to V4L2.
    int32_t returnFrameLocked(StreamBuffer& buf);
    virtual int32_t onFrameReturnLocked(int32_t index, StreamBuffer& buf) = 0;
    // get buffer index.
    int32_t getBufferIndexLocked(StreamBuffer& buf);

    // allocate buffers.
    virtual int32_t allocateBuffersLocked() = 0;
    // free buffers.
    virtual int32_t freeBuffersLocked() = 0;

    int32_t handleMessage();

private:
    class MessageThread : public Thread
    {
    public:
        MessageThread(DeviceStream *device)
            : Thread(false), mStream(device)
            {}

        virtual void onFirstRef() {
            run("MessageThread", PRIORITY_URGENT_DISPLAY);
        }

        virtual status_t readyToRun() {
            g2d_open(&mStream->g2dHandle);
            return 0;
        }

        virtual bool threadLoop() {
            int ret = mStream->handleMessage();
            if (ret != 0) {
                ALOGI("%s exit...", __func__);
                g2d_close(mStream->g2dHandle);
                mStream.clear();
                mStream = NULL;
                return false;
            }

            // loop until we need to quit
            return true;
        }

    private:
        sp<DeviceStream> mStream;
    };

protected:
    CMessageQueue mMessageQueue;
    sp<MessageThread> mMessageThread;
    int32_t mState;

    List< sp<CaptureRequest> > mRequests;
    int32_t mChanged;

    // camera dev node.
    int32_t mDev;
    void *g2dHandle;
    uint32_t mAllocatedBuffers;
};

#endif
