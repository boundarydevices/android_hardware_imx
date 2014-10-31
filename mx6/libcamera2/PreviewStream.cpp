/*
 * Copyright (C) 2012-2014 Freescale Semiconductor, Inc.
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


int PreviewStream::configure(int fps, bool videoSnapshot)
{
    FLOG_TRACE("PreviewStream %s running", __FUNCTION__);
    int ret = NO_ERROR;
    int errCode = 0;

    fAssert(mDeviceAdapter.get() != NULL);
    ret = mDeviceAdapter->setDeviceConfig(mWidth, mHeight, mFormat, fps);
    if (ret != NO_ERROR) {
        FLOGE("%s setDeviceConfig failed", __FUNCTION__);
        errCode = CAMERA2_MSG_ERROR_DEVICE;
        goto fail;
    }

    mDeviceAdapter->setCameraBufferProvide(this);
    ret = allocateBuffers(mWidth, mHeight, mFormat, mMaxProducerBuffers);
    if (ret != NO_ERROR) {
        FLOGE("%s allocateBuffers failed", __FUNCTION__);
        errCode = CAMERA2_MSG_ERROR_REQUEST;
        goto fail;
    }

    mPrepared = true;
    return NO_ERROR;

fail:
    freeBuffers();
    FLOGE("Error occurred, performing cleanup");

    if (NULL != mErrorListener) {
        mErrorListener->handleError(errCode);
    }

    return BAD_VALUE;
}

int PreviewStream::allocateBuffers(int width, int height,
                        int format, int numBufs)
{
    int index = -1;
    int ret = NO_ERROR;

    //In DeviceAdapter::handleFrameRelease, if mPreviewing is false,
    //will not dec mRefCount. This will happen when performance is low.
    //So need zero ref count.
    for (int i = 0; i < mTotalBuffers; i++) {
       // FLOGI("==== PreviewStream::allocateBuffers, i %d, state %d, ref %d",
         //   i, mCameraBuffer[i].getState(), mCameraBuffer[i].getRefCount());

        mCameraBuffer[i].ZeroRefCount();
    }

    for (int i = 0; i < mMaxProducerBuffers; i++) {
        buffer_handle_t *buf_h = NULL;
        ret = mNativeWindow->dequeue_buffer(mNativeWindow, &buf_h);
        if (ret != 0) {
            FLOGE("dequeueBuffer failed: %s (%d)", strerror(-ret), -ret);
            if (ENODEV == ret) {
                FLOGE("Preview surface abandoned!");
                mNativeWindow = NULL;
            }
            return ret;
        }

        index = getBufferIdx(buf_h);
        if (index < 0 || index >= mTotalBuffers) {
            FLOGE("%s dequeue invalid buffer", __FUNCTION__);
            return BAD_VALUE;
        }
        mCameraBuffer[index].setState(CameraFrame::BUFS_FREE);
		if(mDeviceAdapter.get() && mDeviceAdapter->UseMJPG()) {
			mDeviceAdapter.get()->mVPUPhyAddr[i] = (unsigned char*)mCameraBuffer[index].mPhyAddr;
            mDeviceAdapter.get()->mVPUVirtAddr[i] = (unsigned char*)mCameraBuffer[index].mVirtAddr;
            FLOGI("allocateBuffers, index %d, phyAddr 0x%x", index, mCameraBuffer[index].mPhyAddr);
		}
    }

    for (int i = 0; i < mTotalBuffers; i++) {
        int state = mCameraBuffer[i].getState();
        if (state != CameraFrame::BUFS_FREE) {
            mCameraBuffer[i].setState(CameraFrame::BUFS_IN_SERVICE);

            // The frame held in service.
            // Make sure we dont add one more reference
            // count for it
            if(!mCameraBuffer[i].getRefCount())
                mCameraBuffer[i].addReference();
        }

        if(mDeviceAdapter.get() && mDeviceAdapter->UseMJPG()) {
            mCameraBuffer[i].mBindUVCBufIdx = -1;
            mCameraBuffer[i].mpFrameBuf = NULL;
        }
    }

    dispatchBuffers(&mCameraBuffer[0], mTotalBuffers, BUFFER_CREATE);

    return ret;
}

int PreviewStream::freeBuffers()
{
    status_t ret = NO_ERROR;

    GraphicBufferMapper& mapper = GraphicBufferMapper::get();

    // Give the buffers back to display here -  sort of free it
    if (mNativeWindow) {
        for (int i = 0; i < mTotalBuffers; i++) {
            mapper.unlock(mCameraBuffer[i].mBufHandle);
            ret = mNativeWindow->cancel_buffer(mNativeWindow,
                                               &mCameraBuffer[i].mBufHandle);
            if (ENODEV == ret) {
                FLOGE("Preview surface abandoned!");
                mNativeWindow = NULL;
                return -ret;
            }
            else if (NO_ERROR != ret) {
                FLOGE("cancel_buffer() failed: %s (%d)", strerror(-ret), -ret);
                return -ret;
            }
        }
    }
    else {
        FLOGE("mNativeWindow is NULL");
    }

    // /Clear the frames with camera adapter map
    dispatchBuffers(NULL, 0, BUFFER_DESTROY);

    return ret;
}

int PreviewStream::getBufferIdx(buffer_handle_t *buf)
{
    if (buf == NULL) {
        FLOGE("%s invalid param", __FUNCTION__);
        return -1;
    }

    int index = -1;
    for (int i=0; i < mTotalBuffers; i++) {
        if (mCameraBuffer[i].mBufHandle == *buf) {
            index = i;
            break;
        }
    }

    return index;
}

int PreviewStream::registerBuffers(int num_buffers, buffer_handle_t *buffers)
{
    if (buffers == NULL || num_buffers > MAX_PREVIEW_BUFFER) {
        FLOGE("%s buffer num %d too large", __FUNCTION__, num_buffers);
        return BAD_VALUE;
    }

    mTotalBuffers = num_buffers;
    FLOGI("%s total %d buffer", __FUNCTION__, num_buffers);
    GraphicBufferMapper& mapper = GraphicBufferMapper::get();
    Rect bounds;
    memset(mCameraBuffer, 0, sizeof(mCameraBuffer));

    bounds.left   = 0;
    bounds.top    = 0;
    bounds.right  = mWidth;
    bounds.bottom = mHeight;
    void *pVaddr = NULL;

    for (int i=0; i < num_buffers; i++) {
        mapper.lock(buffers[i], mUsage, bounds, &pVaddr);
        mCameraBuffer[i].initialize(buffers[i], i);
        mCameraBuffer[i].mWidth  = mWidth;
        mCameraBuffer[i].mHeight = mHeight;
        mCameraBuffer[i].mFormat = mFormat;
        mCameraBuffer[i].setState(CameraFrame::BUFS_IN_SERVICE);
    }

    return 0;
}

int PreviewStream::start()
{
    FLOG_TRACE("PreviewStream %s running", __FUNCTION__);
    int ret = 0;
    StreamAdapter::start();

    fAssert(mDeviceAdapter.get() != NULL);
    ret = mDeviceAdapter->startPreview();
    if (ret != NO_ERROR) {
        FLOGE("Couldn't start preview for DeviceAdapter");
        return ret;
    }
    return NO_ERROR;
}

int PreviewStream::stop()
{
    FLOG_TRACE("PreviewStream %s running", __FUNCTION__);
    if (mDeviceAdapter.get() != NULL) {
        mDeviceAdapter->stopPreview();
    }

    StreamAdapter::stop();
    return NO_ERROR;
}

int PreviewStream::release()
{
    FLOG_TRACE("PreviewStream %s running", __FUNCTION__);
    StreamAdapter::release();
    return freeBuffers();
}

int PreviewStream::processFrame(CameraFrame *frame)
{
    status_t ret = NO_ERROR;

    if (mShowFps) {
        showFps();
    }

    ret = renderBuffer(frame);
    if (ret != NO_ERROR) {
        FLOGE("%s renderBuffer failed, state %d", __FUNCTION__, frame->getState());
        goto err_exit;
    }
    //the frame held in service.
    frame->addReference();

    StreamBuffer buffer;
    ret = requestBuffer(&buffer);
    if (ret != NO_ERROR) {
        FLOGE("%s requestBuffer failed", __FUNCTION__);
        goto err_exit;
    }

    for (int i = 0; i < mTotalBuffers; i++) {
        if (mCameraBuffer[i].mBufHandle == buffer.mBufHandle) {
            //release frame from service.
            mCameraBuffer[i].release();
            break;
        }
    }

err_exit:
    sem_post(&mRespondSem);

    return ret;
}

