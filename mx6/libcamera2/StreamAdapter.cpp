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

#include "StreamAdapter.h"


StreamAdapter::StreamAdapter(int id)
    : mPrepared(false), mStarted(false), mStreamId(id), mWidth(0), mHeight(0), mFormat(0), mUsage(0),
      mMaxProducerBuffers(0), mNativeWindow(NULL), mStreamState(STREAM_INVALID)
{
}

int StreamAdapter::initialize(int width, int height, int format, int usage, int bufferNum)
{
    mWidth = width;
    mHeight = height;
    mFormat = format;
    mUsage = usage;
    mMaxProducerBuffers = bufferNum;
    return 0;
}

int StreamAdapter::setPreviewWindow(const camera2_stream_ops_t* window)
{
    mNativeWindow = window;
    return 0;
}

void StreamAdapter::setDeviceAdapter(sp<DeviceAdapter>& device)
{
    mDeviceAdapter = device;
}

void StreamAdapter::setMetadaManager(sp<MetadaManager>& metaManager)
{
    mMetadaManager = metaManager;
}

void StreamAdapter::setErrorListener(CameraErrorListener *listener)
{
    mErrorListener = listener;
}

int StreamAdapter::start()
{
    FLOG_TRACE("StreamAdapter %s running", __FUNCTION__);
    mStreamThread = new StreamThread(this);
    mThreadQueue.postSyncMessage(new SyncMessage(STREAM_START, 0));

    fAssert(mDeviceAdapter.get() != NULL);
    mDeviceAdapter->addFrameListener(this);
    mStarted = true;
    return NO_ERROR;
}

int StreamAdapter::stop()
{
    FLOG_TRACE("StreamAdapter %s running", __FUNCTION__);
    if (mDeviceAdapter.get() != NULL) {
        mDeviceAdapter->removeFrameListener(this);;
    }

    if (mStreamThread.get() != NULL) {
        mThreadQueue.postSyncMessage(new SyncMessage(STREAM_STOP, 0));
    }
    FLOG_TRACE("StreamAdapter %s end", __FUNCTION__);

    mStarted = false;
    return NO_ERROR;
}

int StreamAdapter::release()
{
    FLOG_TRACE("StreamAdapter %s running", __FUNCTION__);
    if (mStreamThread.get() == NULL) {
        return NO_ERROR;
    }

    mThreadQueue.postSyncMessage(new SyncMessage(STREAM_EXIT, 0));
    mStreamThread->requestExitAndWait();
    mStreamThread.clear();
    mStreamState = STREAM_INVALID;
    mPrepared = false;

    return NO_ERROR;
}

bool StreamAdapter::handleStream()
{
    bool shouldLive = true;
    int ret = 0;
    CameraFrame *frame = NULL;

    sp<CMessage> msg = mThreadQueue.waitMessage(THREAD_WAIT_TIMEOUT);
    if (msg == 0) {
        if (mStreamState == STREAM_STARTED) {
            FLOGI("%s: get invalid message", __FUNCTION__);
        }
        return shouldLive;
    }


    switch (msg->what) {
        case STREAM_FRAME:
            frame = (CameraFrame *)msg->arg0;
            if (!frame || !frame->mBufHandle) {
                FLOGI("%s invalid frame", __FUNCTION__);
                break;
            }

            if (mStreamState == STREAM_STARTED) {
                ret = processFrame(frame);
                if (!ret) {
                    //the frame release from StreamThread.
                    frame->release();
                    break;
                }

            }

            //the frame release from StreamThread.
            frame->release();
            cancelBuffer(frame);
            if (ret != 0) {
                mErrorListener->handleError(ret);
                if (ret <= CAMERA2_MSG_ERROR_DEVICE) {
                    FLOGI("stream thread dead because of error...");
                    mStreamState = STREAM_EXITED;
                }
            }

            break;

        case STREAM_START:
            FLOGI("stream thread received STREAM_START command");
            if (mStreamState == STREAM_EXITED) {
                FLOGI("can't start stream thread, thread dead...");
            }
            else {
                mStreamState = STREAM_STARTED;
            }

            break;

        case STREAM_STOP:
            FLOGI("stream thread received STREAM_STOP command");
            if (mStreamState == STREAM_EXITED) {
                FLOGI("can't stop stream thread, thread dead...");
            }
            else {
                mStreamState = STREAM_STOPPED;
            }

            break;

        case STREAM_EXIT:
            FLOGI("stream thread exiting...");
            mStreamState = STREAM_EXITED;
            shouldLive = false;
            break;

        default:
            FLOGE("Invalid stream Thread Command 0x%x.", msg->what);
            break;
    } // end switch

    return shouldLive;
}

void StreamAdapter::handleCameraFrame(CameraFrame *frame)
{
    if (!frame || !frame->mBufHandle) {
        FLOGI("%s invalid frame", __FUNCTION__);
        return;
    }

    //the frame processed in StreamThread.
    frame->addReference();
    mThreadQueue.postMessage(new CMessage(STREAM_FRAME, (int)frame));
}

void StreamAdapter::applyRequest()
{
    Mutex::Autolock _l(mMutexRespond);
    mCondRespond.wait(mMutexRespond);
}

int StreamAdapter::processFrame(CameraFrame *frame)
{
    status_t ret = NO_ERROR;
    int size;

    StreamBuffer buffer;
    int err = requestBuffer(&buffer);
    if (ret != NO_ERROR) {
        FLOGE("%s requestBuffer failed", __FUNCTION__);
        goto err_ext;
    }

    size = (frame->mSize > buffer.mSize) ? buffer.mSize : frame->mSize;
    memcpy(buffer.mVirtAddr, (void *)frame->mVirtAddr, size);
    buffer.mTimeStamp = frame->mTimeStamp;
    err = renderBuffer(&buffer);
    if (ret != NO_ERROR) {
        FLOGE("%s renderBuffer failed", __FUNCTION__);
        goto err_ext;
    }

err_ext:
    mCondRespond.signal();

    return ret;
}

int StreamAdapter::requestBuffer(StreamBuffer* buffer)
{
    buffer_handle_t *buf;
    int i = 0;
    GraphicBufferMapper& mapper = GraphicBufferMapper::get();
    Rect  bounds;
    void *pVaddr;

    if (NULL == mNativeWindow) {
        FLOGE("mNativeWindow is null");
        return BAD_VALUE;
    }

    int err = mNativeWindow->dequeue_buffer(mNativeWindow, &buf);
    if (err != 0) {
        FLOGE("dequeueBuffer failed: %s (%d)", strerror(-err), -err);
        if (ENODEV == err) {
            FLOGE("Preview surface abandoned!");
            mNativeWindow = NULL;
        }

        return BAD_VALUE;
    }

    bounds.left   = 0;
    bounds.top    = 0;
    bounds.right  = mWidth;
    bounds.bottom = mHeight;

    // lock buffer before sending to FrameProvider for filling
    mapper.lock(*buf, mUsage, bounds, &pVaddr);

    private_handle_t *handle = (private_handle_t *)(*buf);
    buffer->mWidth = mWidth;
    buffer->mHeight = mHeight;
    buffer->mFormat = mFormat;
    buffer->mVirtAddr = pVaddr;
    buffer->mPhyAddr = handle->phys;
    buffer->mSize = handle->size;
    buffer->mBufHandle = *buf;

    return 0;
}

int StreamAdapter::renderBuffer(StreamBuffer *buffer)
{
    status_t ret = NO_ERROR;

    GraphicBufferMapper& mapper = GraphicBufferMapper::get();

    // unlock buffer before sending to stream
    mapper.unlock(buffer->mBufHandle);

    ret = mNativeWindow->enqueue_buffer(mNativeWindow, buffer->mTimeStamp,
                                        &buffer->mBufHandle);
    if (ret != 0) {
        FLOGE("Surface::queueBuffer returned error %d", ret);
    }

    return ret;
}

int StreamAdapter::cancelBuffer(StreamBuffer *buffer)
{
    status_t ret = NO_ERROR;

    GraphicBufferMapper& mapper = GraphicBufferMapper::get();

    mapper.unlock(buffer->mBufHandle);

    ret = mNativeWindow->cancel_buffer(mNativeWindow, &buffer->mBufHandle);
    if (ret != 0) {
        FLOGE("Surface::queueBuffer returned error %d", ret);
    }

    return ret;
}



