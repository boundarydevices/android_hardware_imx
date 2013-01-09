/*
 * Copyright (C) 2012-2013 Freescale Semiconductor, Inc.
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

#ifndef _STREAM_ADAPTER_H
#define _STREAM_ADAPTER_H

#include "CameraUtil.h"
#include "messageQueue.h"
#include "PhysMemAdapter.h"
#include "JpegBuilder.h"
#include "DeviceAdapter.h"

using namespace android;

class StreamAdapter : public LightRefBase<StreamAdapter>,
                      public CameraFrameListener
{
public:
    StreamAdapter(int id);
    virtual ~StreamAdapter() {}

    virtual int initialize(int width, int height, int format,
                           int usage, int bufferNum);
    virtual int setPreviewWindow(const camera2_stream_ops_t* window);
    virtual int registerBuffers(int num_buffers, buffer_handle_t *buffers) {return 0;}

    virtual int configure(int fps, bool videoSnapshot)
    {
        FLOG_TRACE("StreamAdapter::configure");
        mPrepared = true;
        return 0;
    }
    virtual int start();
    virtual int stop();
    virtual int release();
    virtual int processFrame(CameraFrame *frame);
    virtual void applyRequest();

    void setDeviceAdapter(sp<DeviceAdapter>& device);
    void setMetadaManager(sp<MetadaManager>& metaManager);
    int getStreamId() {return mStreamId;}
    int getMaxBuffers() {return mMaxProducerBuffers;}

    int renderBuffer(StreamBuffer *buffer);
    int requestBuffer(StreamBuffer* buffer);
    int cancelBuffer(StreamBuffer *buffer);

    //CameraFrameListener
    void handleCameraFrame(CameraFrame *frame);
    void setErrorListener(CameraErrorListener *listener);

    enum StreamCommands {
        STREAM_START,
        STREAM_STOP,
        STREAM_FRAME,
        STREAM_EXIT
    };

    enum StreamStates {
        STREAM_INVALID = 0,
        STREAM_STARTED,
        STREAM_STOPPED,
        STREAM_EXITED
    };

    virtual bool handleStream();

    class StreamThread : public Thread {
    public:
        StreamThread(StreamAdapter *stream) :
            Thread(false), mStream(stream) {}

        virtual void onFirstRef() {
            run("RequestHandle", PRIORITY_DEFAULT);
        }

        virtual bool threadLoop() {
            return mStream->handleStream();
        }

    private:
        StreamAdapter *mStream;
    };

public:
    bool mPrepared;
    bool mStarted;

protected:
    int mStreamId;
    int mWidth;
    int mHeight;
    int mFormat;
    int mUsage;
    int mMaxProducerBuffers;
    const camera2_stream_ops_t *mNativeWindow;

    sp<DeviceAdapter> mDeviceAdapter;
    sp<StreamThread> mStreamThread;
    CMessageQueue mThreadQueue;
    int mStreamState;
    CameraErrorListener *mErrorListener;

    sp<MetadaManager> mMetadaManager;
    mutable Mutex mMutexRespond;
    mutable Condition mCondRespond;
};


class PreviewStream : public StreamAdapter, public CameraBufferProvider
{
public:
    PreviewStream(int id) : StreamAdapter(id) {}
    ~PreviewStream() {}

    virtual int configure(int fps, bool videoSnapshot);
    virtual int allocateBuffers(int width, int height,
                               int format, int numBufs);
    virtual int start();
    virtual int stop();
    virtual int release();
    virtual int processFrame(CameraFrame *frame);

    virtual int freeBuffers();
    virtual int registerBuffers(int num_buffers, buffer_handle_t *buffers);

    int getBufferIdx(buffer_handle_t *buf);

private:
    int mTotalBuffers;
    CameraFrame mCameraBuffer[MAX_PREVIEW_BUFFER];
};


class CaptureStream : public StreamAdapter
{
public:
    CaptureStream(int id);
    ~CaptureStream();

    virtual int initialize(int width, int height, int format,
                           int usage, int bufferNum);
    virtual int configure(int fps, bool videoSnapshot);

    virtual int start();
    virtual int stop();
    virtual int release();
    virtual int processFrame(CameraFrame *frame);
    virtual void applyRequest();

private:
    status_t makeJpegImage(StreamBuffer *dstBuf, StreamBuffer *srcBuf);

private:
    int mActualFormat;
    bool mVideoSnapShot;
    PhysMemAdapter *mPhysMemAdapter;
    sp<JpegBuilder> mJpegBuilder;

    bool mRequestStream;
    mutable sem_t mRespondSem;
};

#endif
