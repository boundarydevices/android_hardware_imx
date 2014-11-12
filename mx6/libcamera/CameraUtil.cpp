/*
 * Copyright (C) 2008 The Android Open Source Project
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

#include "CameraUtil.h"

int convertPixelFormatToV4L2Format(PixelFormat format)
{
    int nFormat = 0;

    switch (format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            nFormat = v4l2_fourcc('N', 'V', '1', '2');
            break;

        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            nFormat = v4l2_fourcc('Y', 'U', '1', '2');
            break;

        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            nFormat = v4l2_fourcc('Y', 'U', 'Y', 'V');
            break;

        default:
            FLOGE("Error: format not supported!");
            break;
    }
    FLOGI("pixel format: 0x%x", nFormat);
    return nFormat;
}

PixelFormat convertV4L2FormatToPixelFormat(unsigned int format)
{
    PixelFormat nFormat = 0;

    switch (format) {
        case v4l2_fourcc('N', 'V', '1', '2'):
            nFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
            break;

        case v4l2_fourcc('Y', 'U', '1', '2'):
            nFormat = HAL_PIXEL_FORMAT_YCbCr_420_P;
            break;

        case v4l2_fourcc('Y', 'U', 'Y', 'V'):
            nFormat = HAL_PIXEL_FORMAT_YCbCr_422_I;
            break;

        default:
            FLOGE("Error: format not supported!");
            break;
    }
    FLOGI("pixel format: 0x%x", nFormat);
    return nFormat;
}

int convertStringToPixelFormat(const char *pFormat)
{
    if (pFormat == NULL) {
        return 0;
    }

    if (!strcmp(pFormat, "yuv420p")) {
        return HAL_PIXEL_FORMAT_YCbCr_420_P;
    }
    else if (!strcmp(pFormat, "yuv420sp")) {
        return HAL_PIXEL_FORMAT_YCbCr_420_SP;
    }
    else if (!strcmp(pFormat, "yuv422i-yuyv")) {
        return HAL_PIXEL_FORMAT_YCbCr_422_I;
    }
    else {
        FLOGE("format %s is not supported", pFormat);
        return BAD_VALUE;
    }
}

int convertStringToV4L2Format(const char *pFormat)
{
    if (pFormat == NULL) {
        return 0;
    }

    if (!strcmp(pFormat, "yuv420p")) {
        return v4l2_fourcc('Y', 'U', '1', '2');
    }
    else if (!strcmp(pFormat, "yuv420sp")) {
        return v4l2_fourcc('N', 'V', '1', '2');
    }
    else if (!strcmp(pFormat, "yuv422i-yuyv")) {
        return v4l2_fourcc('Y', 'U', 'Y', 'V');
    }
    else {
        FLOGE("format %s is not supported", pFormat);
        return BAD_VALUE;
    }
}

CameraFrame::~CameraFrame()
{
    reset();
}

void CameraFrame::initialize(buffer_handle_t *buf_h,
                             int              index)
{
    FSL_ASSERT(buf_h);
    private_handle_t *handle = (private_handle_t *)(*buf_h);
    mBufHandle = buf_h;
    mVirtAddr  =  (void *)handle->base;
    mPhyAddr   =   handle->phys;
    mSize      =   handle->size;
    mWidth     =  handle->width;
    mHeight    = handle->height;
    mFormat    = handle->format;

    mObserver  = NULL;
    atomic_init(&mRefCount, 0);
    mBufState  = BUFS_CREATE;
    mFrameType = INVALID_FRAME;
    mIndex     = index;
}

void CameraFrame::addState(CAMERA_BUFS_STATE state)
{
    mBufState |= state;
}

void CameraFrame::removeState(CAMERA_BUFS_STATE state)
{
    mBufState &= ~state;
}

void CameraFrame::addReference()
{
    atomic_fetch_add(&mRefCount, 1);
}

void CameraFrame::release()
{
    FSL_ASSERT(mRefCount > 0, "mRefCount=%d invalid value", mRefCount);

    int prevCount = atomic_fetch_sub(&mRefCount, 1);
    if ((prevCount == 1) && (mObserver != NULL)) {
        mObserver->handleFrameRelease(this);
    }
}

void CameraFrame::setObserver(CameraFrameObserver *observer)
{
    mObserver = observer;
}

void CameraFrame::reset()
{
    mBufHandle = NULL;
    mVirtAddr  = NULL;
    mPhyAddr   = 0;
    mObserver  = NULL;
    atomic_init(&mRefCount, 0);
    mBufState  = BUFS_CREATE;
    mFrameType = INVALID_FRAME;
}

// //////////CameraBufferProvider////////////////////
CameraBufferProvider::CameraBufferProvider()
{
    mBufferListeners.clear();
}

CameraBufferProvider::~CameraBufferProvider()
{
    mBufferListeners.clear();
}

void CameraBufferProvider::addBufferListener(CameraBufferListener *listener)
{
    CameraBufferListener *pNtf = NULL;
    size_t nSize               = mBufferListeners.size();

    for (size_t i = 0; i < nSize; i++) {
        pNtf = (CameraBufferListener *)mBufferListeners[i];
        if (pNtf == listener) {
            return;
        }
    }

    mBufferListeners.push((int)listener);
}

void CameraBufferProvider::removeBufferListener(CameraBufferListener *listener)
{
    CameraBufferListener *pNtf;
    size_t nSize = mBufferListeners.size();

    for (size_t i = 0; i < nSize; i++) {
        pNtf = (CameraBufferListener *)mBufferListeners[i];
        if (pNtf == listener) {
            mBufferListeners.removeAt(i);

            // break;
        }
    }
}

void CameraBufferProvider::clearBufferListeners()
{
    mBufferListeners.clear();
}

void CameraBufferProvider::dispatchBuffers(CameraFrame *pBuffer,
                                           int          num,
                                           BufferState  bufState)
{
    CameraBufferListener *listener;
    size_t nSize = mBufferListeners.size();

    for (size_t i = 0; i < nSize; i++) {
        listener = (CameraBufferListener *)mBufferListeners[i];
        switch (bufState) {
            case BUFFER_CREATE:
                FSL_ASSERT(pBuffer);
                listener->onBufferCreat(pBuffer, num);
                break;

            case BUFFER_DESTROY:
                listener->onBufferDestroy();
                break;
        } // end switch
    }     // end for
}

// //////////CameraFrameProvider////////////////////
CameraFrameProvider::CameraFrameProvider()
{
    mFrameListeners.clear();
}

CameraFrameProvider::~CameraFrameProvider()
{
    mFrameListeners.clear();
}

void CameraFrameProvider::addFrameListener(CameraFrameListener *listener)
{
    CameraFrameListener *pNtf;
    size_t nSize = mFrameListeners.size();

    for (size_t i = 0; i < nSize; i++) {
        pNtf = (CameraFrameListener *)mFrameListeners[i];
        if (pNtf == listener) {
            return;
        }
    }

    mFrameListeners.push((int)listener);
}

void CameraFrameProvider::removeFrameListener(CameraFrameListener *listener)
{
    CameraFrameListener *pNtf;
    size_t nSize = mFrameListeners.size();

    for (size_t i = 0; i < nSize; i++) {
        pNtf = (CameraFrameListener *)mFrameListeners[i];
        if (pNtf == listener) {
            mFrameListeners.removeAt(i);

            // break;
        }
    }
}

void CameraFrameProvider::clearFrameListeners()
{
    mFrameListeners.clear();
}

void CameraFrameProvider::dispatchCameraFrame(CameraFrame *frame)
{
    FSL_ASSERT(frame);
    CameraFrameListener *listener;
    size_t nSize = mFrameListeners.size();

    // add reference here to avoid frame release too early.
    frame->addReference();
    for (size_t i = 0; i < nSize; i++) {
        listener = (CameraFrameListener *)mFrameListeners[i];
        listener->handleCameraFrame(frame);
    }
    frame->release();
}

// ----------------CameraEventProvider----------
void CameraEventProvider::addEventListener(CameraEventListener *listener)
{
    CameraEventListener *pNtf;
    size_t nSize = mEventListeners.size();

    for (size_t i = 0; i < nSize; i++) {
        pNtf = (CameraEventListener *)mEventListeners[i];
        if (pNtf == listener) {
            return;
        }
    }

    mEventListeners.push((int)listener);
}

void CameraEventProvider::removeEventListener(CameraEventListener *listener)
{
    CameraEventListener *pNtf;
    size_t nSize = mEventListeners.size();

    for (size_t i = 0; i < nSize; i++) {
        pNtf = (CameraEventListener *)mEventListeners[i];
        if (pNtf == listener) {
            mEventListeners.removeAt(i);

            // break;
        }
    }
}

void CameraEventProvider::clearEventListeners()
{
    mEventListeners.clear();
}

void CameraEventProvider::dispatchEvent(sp<CameraEvent>& event)
{
    FSL_ASSERT(event != NULL);
    CameraEventListener *listener;
    size_t nSize = mEventListeners.size();

    for (size_t i = 0; i < nSize; i++) {
        listener = (CameraEventListener *)mEventListeners[i];
        listener->handleEvent(event);
    }
}

