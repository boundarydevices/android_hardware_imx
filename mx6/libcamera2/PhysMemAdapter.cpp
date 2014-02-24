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


#include "PhysMemAdapter.h"
#include <ion/ion.h>

PhysMemAdapter::PhysMemAdapter()
    : mIonFd(-1), mFrameWidth(0), mFrameHeight(0),
      mBufferCount(0), mBufferSize(0), mFormat(0)
{
    memset(mCameraBuffer, 0, sizeof(mCameraBuffer));
    mIonFd = ion_open();
}

PhysMemAdapter::~PhysMemAdapter()
{
    memset(mCameraBuffer, 0, sizeof(mCameraBuffer));
    clearBufferListeners();
    ion_close(mIonFd);
}

int PhysMemAdapter::allocateBuffers(int width,int height,
                                   int format, int numBufs)
{
    if (mIonFd <= 0) {
        FLOGE("try to allocate buffer from ion in preview or ion invalid");
        return BAD_VALUE;
    }

    int size = 0;
    if ((width == 0) || (height == 0)) {
        FLOGE("allocateBufferFromIon: width or height = 0");
        return BAD_VALUE;
    }
    switch (format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            size = width * ((height + 16) & (~15)) * 3 / 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            size = width * height * 3 / 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            size = width * height * 2;
            break;

        default:
            FLOGE("Error: format not supported int ion alloc");
            return BAD_VALUE;
    }

    unsigned char *ptr = NULL;
    int sharedFd;
    int phyAddr;
    ion_user_handle_t ionHandle;
    size = (size + PAGE_SIZE) & (~(PAGE_SIZE - 1));

    FLOGI("allocateBufferFromIon buffer num:%d", numBufs);
    for (int i = 0; i < numBufs; i++) {
        ionHandle = -1;
        int err = ion_alloc(mIonFd, size, 8, 1, 0, &ionHandle);
        if (err) {
            FLOGE("ion_alloc failed.");
            return BAD_VALUE;
        }

        err = ion_map(mIonFd,
                      ionHandle,
                      size,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      0,
                      &ptr,
                      &sharedFd);
        if (err) {
            FLOGE("ion_map failed.");
            return BAD_VALUE;
        }
        phyAddr = ion_phys(mIonFd, ionHandle);
        if (phyAddr == 0) {
            FLOGE("ion_phys failed.");
            return BAD_VALUE;
        }
        FLOG_RUNTIME("phyalloc ptr:0x%x, phy:0x%x, size:%d",
                     (int)ptr,
                     phyAddr,
                     size);
        mCameraBuffer[i].reset();
        mCameraBuffer[i].mIndex     = i;
        mCameraBuffer[i].mWidth     = width;
        mCameraBuffer[i].mHeight    = height;
        mCameraBuffer[i].mFormat    = format;
        mCameraBuffer[i].mVirtAddr  = ptr;
        mCameraBuffer[i].mPhyAddr   = phyAddr;
        mCameraBuffer[i].mSize      =  size;
        mCameraBuffer[i].mBufHandle = (buffer_handle_t)ionHandle;
        mCameraBuffer[i].setState(CameraFrame::BUFS_FREE);
        close(sharedFd);
    }

    mBufferCount    = numBufs;
    mFormat         = format;
    mBufferSize     = mCameraBuffer[0].mSize;
    mFrameWidth     = width;
    mFrameHeight    = height;

    dispatchBuffers(&mCameraBuffer[0], numBufs, BUFFER_CREATE);

    return NO_ERROR;
}

int PhysMemAdapter::freeBuffers()
{
    if (mIonFd <= 0) {
        FLOGE("try to free buffer from ion in preview or ion invalid");
        return BAD_VALUE;
    }

    FLOGI("freeBufferToIon buffer num:%d", mBufferCount);
    for (int i = 0; i < mBufferCount; i++) {
        ion_user_handle_t ionHandle =
            (ion_user_handle_t)mCameraBuffer[i].mBufHandle;
        ion_free(mIonFd, ionHandle);
        munmap(mCameraBuffer[i].mVirtAddr, mCameraBuffer[i].mSize);
    }

    memset(mCameraBuffer, 0, sizeof(mCameraBuffer));
    dispatchBuffers(NULL, 0, BUFFER_DESTROY);
    return NO_ERROR;
}
