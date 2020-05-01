/*
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc.
 * Copyright 2018 NXP.
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

#ifndef _VIDEO_STREAM_H
#define _VIDEO_STREAM_H

#include <utils/threads.h>
#include "MessageQueue.h"
#include "CameraUtils.h"
#include "Stream.h"
#include "Camera.h"
#include "utils/CameraConfigurationParser.h"

using namespace android;

class Camera;

class ConfigureParam
{
public:
    int32_t mWidth;
    int32_t mHeight;
    int32_t mFormat;
    int32_t mFps;
    int32_t mBuffers;
    int32_t mIsJpeg;
};

class VideoStream : public Stream
{
public:
    VideoStream(Camera* device);
    virtual ~VideoStream();
    void destroyStream();

    // configure device stream.
    int32_t configure(sp<Stream> stream);
    //send capture request for stream.
    int32_t requestCapture(sp<CaptureRequest> req);

    // open/close device stream.
    int32_t openDev(const char* name);
    int32_t closeDev();
    int32_t flushDev();

    void setOmitFrameCount(uint32_t omitCount) { mOmitFrmCount = omitCount; }

    int32_t getWidth() {return mWidth;}
    int32_t getHeight() {return mHeight;}

private:
    // message type.
    static const int32_t MSG_CONFIG = 0x100;
    static const int32_t MSG_FRAME = 0x103;
    static const int32_t MSG_CLOSE = 0x104;
    static const int32_t MSG_EXIT  = 0x105;
    static const int32_t MSG_FLUSH = 0x106;

    // device stream state.
    static const int32_t STATE_INVALID = 0x201;
    static const int32_t STATE_CONFIG = 0x202;
    static const int32_t STATE_START = 0x203;
    static const int32_t STATE_STOP  = 0x204;
    static const int32_t STATE_ERROR  = 0x205;

protected:
    // handle configure message internally.
    int32_t handleConfigureLocked(ConfigureParam* params);
    virtual int32_t onDeviceConfigureLocked() = 0;
    // handle start message internally.
    int32_t handleStartLocked(bool force);
    virtual int32_t onDeviceStartLocked() = 0;
    // handle stop message internally.
    int32_t handleStopLocked(bool force);
    virtual int32_t onDeviceStopLocked() = 0;
    // handle flush.
    int32_t handleFlushLocked();
    virtual int32_t onFlushLocked();
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
    int32_t flushDevLocked();

private:
    class MessageThread : public Thread
    {
    public:
        MessageThread(VideoStream *device)
            : Thread(false), mStream(device)
            {}

        virtual void onFirstRef() {
            run("MessageThread", PRIORITY_URGENT_DISPLAY);
        }

        virtual status_t readyToRun() {
            return 0;
        }

        virtual bool threadLoop() {
            int ret = mStream->handleMessage();
            if (ret != 0) {
                ALOGI("%s exit...", __func__);
                mStream.clear();
                mStream = NULL;
                return false;
            }

            // loop until we need to quit
            return true;
        }

    private:
        sp<VideoStream> mStream;
    };

protected:
    CMessageQueue mMessageQueue;
    sp<MessageThread> mMessageThread;
    int32_t mState;

    List< sp<CaptureRequest> > mRequests;
    int32_t mChanged;

    // camera dev node.
    int32_t mDev;
    uint32_t mAllocatedBuffers;
    enum v4l2_memory mV4l2MemType;
    enum v4l2_buf_type mV4l2BufType;
    uint32_t mOmitFrames;
    uint32_t mOmitFrmCount;
};

#endif
