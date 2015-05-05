/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include "SurfaceAdapter.h"


typedef struct tRGB{
     unsigned char r;
     unsigned char g;
     unsigned char b;
}RGB;

static RGB yuvTorgb(unsigned char Y, unsigned char U, unsigned char V){
    RGB rgb;
    rgb.r = (int)((Y&0xff) + 1.4075 * ((V&0xff)-128));
    rgb.g = (int)((Y&0xff) - 0.3455 * ((U&0xff)-128) - 0.7169*((V&0xff)-128));
    rgb.b = (int)((Y&0xff) + 1.779 * ((U&0xff)-128));

    return rgb;
}


static void YUY2ToRGB(unsigned char *src, unsigned char *dst, int width, int height){
	int i;
	int j;
    int numOfPixel = width * height;
    unsigned char *rgb = dst;
    int lineWidth = 2*width;

    for(i=0; i<height; i++){
        int startY = i*lineWidth;
        for(j = 0; j < lineWidth; j+=4){
            int Y1 = j + startY;
            int Y2 = Y1+2;
            int U = Y1+1;
            int V = Y1+3;
            int index = (Y1>>1)*3;
            RGB tmp = yuvTorgb(src[Y1], src[U], src[V]);
            rgb[index+0] = tmp.r;
            rgb[index+1] = tmp.g;
            rgb[index+2] = tmp.b;
            index += 3;
            tmp = yuvTorgb(src[Y2], src[U], src[V]);
            rgb[index+0] = tmp.r;
            rgb[index+1] = tmp.g;
            rgb[index+2] = tmp.b;
        }
    }
    return;
}



SurfaceAdapter::SurfaceAdapter()
    : mNativeWindow(NULL), mFrameWidth(0), mFrameHeight(0),
      mBufferCount(0), mBufferSize(0), mFormat(0), mQueueableCount(0)
{
    memset(mCameraBuffer, 0, sizeof(mCameraBuffer));
#ifdef NO_GPU
    mTmpBuf = NULL;
#endif
}

SurfaceAdapter::~SurfaceAdapter()
{
    memset(mCameraBuffer, 0, sizeof(mCameraBuffer));
    clearBufferListeners();
}

int SurfaceAdapter::setNativeWindowAttribute(int width,
                                             int height,
                                             int format,
                                             int numBufs)
{
    status_t err = NO_ERROR;

    if (NULL == mNativeWindow) {
        FLOGE("SurfaceAdapter: allocateBuffer invalid parameters");
        return BAD_VALUE;
    }

    // Set gralloc usage bits for window.
    err = mNativeWindow->set_usage(mNativeWindow, CAMERA_GRALLOC_USAGE);
    if (err != 0) {
        FLOGE("native_window_set_usage failed: %s (%d)", strerror(-err), -err);
        if (ENODEV == err) {
            FLOGE("Preview surface abandoned!");
            mNativeWindow = NULL;
        }

        return BAD_VALUE;
    }

    FLOGI("Number of buffers set to NativeWindow %d", numBufs);

    // /Set the number of buffers needed for camera preview
    err = mNativeWindow->set_buffer_count(mNativeWindow, numBufs);
    if (err != 0) {
        FLOGE("native_window_set_buffer_count failed: %s (%d)", strerror(
                  -err), -err);
        if (ENODEV == err) {
            FLOGE("Preview surface abandoned!");
            mNativeWindow = NULL;
        }

        return BAD_VALUE;
    }

    // Set window geometry
    ALOGI("set_buffers_geometry, w %d, h %d, format 0x%x", width, height, format);
    err = mNativeWindow->set_buffers_geometry(mNativeWindow,
                                              width, height, format);

    if (err != 0) {
        FLOGE("native_window_set_buffers_geometry failed: %s (%d)", strerror(
                  -err), -err);
        if (ENODEV == err) {
            FLOGE("Preview surface abandoned!");
            mNativeWindow = NULL;
        }

        return BAD_VALUE;
    }


    return err;
}

int SurfaceAdapter::allocatePreviewBuffer(int width,
                                          int height,
                                          int format,
                                          int numBufs)
{
    status_t err   = NO_ERROR;
    int undequeued = 0;

    if ((NULL == mNativeWindow) || (numBufs == 0)) {
        FLOGE("allocatePreviewBuffer invalid parameters");
        return BAD_VALUE;
    }

    err = setNativeWindowAttribute(width, height, format, numBufs);
    if (err) {
        FLOGE("setNativeWindowAttribute failed.");
        return err;
    }

    mBufferCount = numBufs;
    mNativeWindow->get_min_undequeued_buffer_count(mNativeWindow, &undequeued);
    mQueueableCount = mBufferCount - undequeued;

    err = allocateBuffer(width, height, format, numBufs, mQueueableCount);
    return err;
}

int SurfaceAdapter::allocatePictureBuffer(int width,
                                          int height,
                                          int format,
                                          int numBufs)
{
    status_t err = NO_ERROR;

    if ((NULL == mNativeWindow) || (numBufs == 0)) {
        FLOGE("allocatePictureBuffer invalid parameters");
        return BAD_VALUE;
    }

    err = setNativeWindowAttribute(width, height, format, numBufs);
    if (err) {
        FLOGE("setNativeWindowAttribute failed.");
        return err;
    }

    mBufferCount    = numBufs;
    mQueueableCount = numBufs;

    err = allocateBuffer(width, height, format, numBufs, mQueueableCount);
    return err;
}

int SurfaceAdapter::allocateBuffer(int width,
                                   int height,
                                   int format,
                                   int numBufs,
                                   int maxQCount)
{
    status_t err;
    int i                       = -1;
    GraphicBufferMapper& mapper = GraphicBufferMapper::get();
    Rect bounds;

    if ((NULL == mNativeWindow) || (numBufs == 0)) {
        FLOGE("allocateBuffer invalid parameters");
        return BAD_VALUE;
    }

    memset(mCameraBuffer, 0, sizeof(mCameraBuffer));

    // lock the initial queueable buffers
    bounds.left   = 0;
    bounds.top    = 0;
    bounds.right  = width;
    bounds.bottom = height;
    void *pVaddr = NULL;
    int   stride = 0;

    for (i = 0; i < numBufs; i++) {
        buffer_handle_t *buf_h = NULL;
        stride = 0;
        pVaddr = NULL;

        // TODO(XXX): Do we need to keep stride information in camera hal?
        err = mNativeWindow->dequeue_buffer(mNativeWindow, &buf_h, &stride);
        if (err != 0) {
            FLOGE("dequeueBuffer failed: %s (%d)", strerror(-err), -err);
            if (ENODEV == err) {
                FLOGE("Preview surface abandoned!");
                mNativeWindow = NULL;
            }
            goto fail;
        }

        mapper.lock(*buf_h, CAMERA_GRALLOC_USAGE, bounds, &pVaddr);
        mCameraBuffer[i].initialize(buf_h, i);
        mCameraBuffer[i].mWidth  = width;
        mCameraBuffer[i].mHeight = height;
    }

    for (i = 0; i < maxQCount; i++) {
        mNativeWindow->lock_buffer(mNativeWindow, mCameraBuffer[i].mBufHandle);
    }

    // return the rest of the buffers back to ANativeWindow
    for (i = maxQCount; i >= 0 && i < numBufs; i++) {
        err = mNativeWindow->cancel_buffer(mNativeWindow,
                                           mCameraBuffer[i].mBufHandle);
        if (err != 0) {
            FLOGE("cancel_buffer failed: %s (%d)", strerror(-err), -err);
            if (ENODEV == err) {
                FLOGE("Preview surface abandoned!");
                mNativeWindow = NULL;
            }

            goto fail;
        }
        mapper.unlock(*mCameraBuffer[i].mBufHandle);

        // the frame held in SurfaceAdapter.
        mCameraBuffer[i].addReference();
    }

    dispatchBuffers(&mCameraBuffer[0], numBufs, BUFFER_CREATE);

    mFormat      = format;
    mBufferSize  = mCameraBuffer[0].mSize;
    mFrameWidth  = width;
    mFrameHeight = height;

    ALOGI("SurfaceAdapter::allocateBuffer, mBufferSize %d", mBufferSize);

#ifdef NO_GPU
    mTmpBuf = (unsigned char*)malloc(mBufferSize);
    if(mTmpBuf == NULL) {
        ALOGE("malloc mTmpBuf failed, bytes %d", mBufferSize);
        goto fail;
    }
#endif

    return NO_ERROR;

fail:

    // need to cancel buffers if any were dequeued
    for (int start = 0; start < i && i > 0; start++) {
        int err = mNativeWindow->cancel_buffer(mNativeWindow,
                                               mCameraBuffer[start].mBufHandle);
        if (err != 0) {
            FLOGE("cancelBuffer failed w/ error 0x%08x", err);
            break;
        }
    }

    freeBuffer();
    FLOGE("Error occurred, performing cleanup");

    if (NULL != mErrorListener) {
        mErrorListener->handleError(ERROR_TINY);
    }

    return BAD_VALUE;
}

int SurfaceAdapter::freeBuffer()
{
    status_t ret = NO_ERROR;

    GraphicBufferMapper& mapper = GraphicBufferMapper::get();

    // Give the buffers back to display here -  sort of free it
    if (mNativeWindow) {
        for (int i = 0; i < mBufferCount; i++) {
            mapper.unlock(*mCameraBuffer[i].mBufHandle);
            ret = mNativeWindow->cancel_buffer(mNativeWindow,
                                               mCameraBuffer[i].mBufHandle);
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

    memset(mCameraBuffer, 0, sizeof(mCameraBuffer));

    // /Clear the frames with camera adapter map
    dispatchBuffers(NULL, 0, BUFFER_DESTROY);

#ifdef NO_GPU
    if(mTmpBuf) {
        free(mTmpBuf);
        mTmpBuf = NULL;
    }
#endif

    return ret;
}

void SurfaceAdapter::setErrorListener(CameraErrorListener *listener)
{
    mErrorListener = listener;
}

int SurfaceAdapter::setPreviewWindow(preview_stream_ops_t *window)
{
    // /Note that Display Adapter cannot work without a valid window object
    if (!window) {
        FLOGE("NULL window object passed to DisplayAdapter");
        return BAD_VALUE;
    }

    // /Destroy the existing window object, if it exists
    destroy();

    // /Move to new window obj
    mNativeWindow = window;

    return NO_ERROR;
}

void SurfaceAdapter::destroy()
{
    mNativeWindow = NULL;
    mBufferCount  = 0;
    mBufferSize   = 0;
}

int SurfaceAdapter::maxQueueableBuffers()
{
    return mQueueableCount;
}

void SurfaceAdapter::renderBuffer(buffer_handle_t *bufHandle)
{
    status_t ret = NO_ERROR;

    GraphicBufferMapper& mapper = GraphicBufferMapper::get();

    // unlock buffer before sending to display
    mapper.unlock(*bufHandle);

#ifdef NO_GPU
    private_handle_t *handle = (private_handle_t *)(*bufHandle);
    memcpy(mTmpBuf, (void *)handle->base, mBufferSize);
    YUY2ToRGB(mTmpBuf, (unsigned char *)handle->base, mFrameWidth, mFrameHeight);
#endif

    ret = mNativeWindow->enqueue_buffer(mNativeWindow, bufHandle);
    if (ret != 0) {
        FLOGE("Surface::queueBuffer returned error %d", ret);
    }
}

void SurfaceAdapter::cancelBuffer(buffer_handle_t *bufHandle)
{
    status_t ret = NO_ERROR;

    GraphicBufferMapper& mapper = GraphicBufferMapper::get();

    mapper.unlock(*bufHandle);

    ret = mNativeWindow->cancel_buffer(mNativeWindow, bufHandle);
    if (ret != 0) {
        FLOGE("Surface::queueBuffer returned error %d", ret);
    }
}

CameraFrame * SurfaceAdapter::requestBuffer()
{
    status_t err;
    buffer_handle_t *buf;
    int i = 0;
    int stride; // dummy variable to get stride
    GraphicBufferMapper& mapper = GraphicBufferMapper::get();
    Rect  bounds;
    void *pVaddr;

    if (NULL == mNativeWindow) {
        FLOGE("mNativeWindow is null");
        return NULL;
    }

    err = mNativeWindow->dequeue_buffer(mNativeWindow, &buf, &stride);
    if (err != 0) {
        FLOGE("dequeueBuffer failed: %s (%d)", strerror(-err), -err);
        if (ENODEV == err) {
            FLOGE("Preview surface abandoned!");
            mNativeWindow = NULL;
        }

        return NULL;
    }

    err = mNativeWindow->lock_buffer(mNativeWindow, buf);
    if (err != 0) {
        FLOGE("lockbuffer failed: %s (%d)", strerror(-err), -err);
        if (ENODEV == err) {
            FLOGE("Preview surface abandoned!");
            mNativeWindow = NULL;
        }

        return NULL;
    }

    for (i = 0; i < mBufferCount; i++) {
        if (mCameraBuffer[i].mBufHandle == buf)
            break;
    }

    // lock buffer before sending to FrameProvider for filling
    bounds.left   = 0;
    bounds.top    = 0;
    bounds.right  = mFrameWidth;
    bounds.bottom = mFrameHeight;

    mapper.lock(*mCameraBuffer[i].mBufHandle,
                CAMERA_GRALLOC_USAGE,
                bounds,
                &pVaddr);

    return &mCameraBuffer[i];
}

