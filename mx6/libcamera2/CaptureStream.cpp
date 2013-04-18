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

#include <utils/StrongPointer.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include "StreamAdapter.h"
#include "PhysMemAdapter.h"
#include "CameraUtil.h"

CaptureStream::CaptureStream(int id)
    : StreamAdapter(id)
{
    mActualFormat = 0;
    mVideoSnapShot = false;
    mPhysMemAdapter = new PhysMemAdapter();
}

CaptureStream::~CaptureStream()
{
    delete mPhysMemAdapter;
}

int CaptureStream::initialize(int width, int height, int format,
                              int usage, int bufferNum)
{
    StreamAdapter::initialize(width, height, format, usage, bufferNum);

    mJpegBuilder = new JpegBuilder();
    if (mJpegBuilder == NULL) {
        FLOGE("Couldn't create JpegBuilder");
        return NO_MEMORY;
    }

    return NO_ERROR;
}

int CaptureStream::configure(int fps, bool videoSnapshot)
{
    FLOG_TRACE("CaptureStream::configure");
    int stride = 0;
    int index = -1;
    int ret = NO_ERROR;
    int errCode = 0;

    mJpegBuilder->reset();
    mJpegBuilder->setMetadaManager(mMetadaManager);

    if (mFormat == HAL_PIXEL_FORMAT_BLOB) {
        mActualFormat = mDeviceAdapter->getPicturePixelFormat();
        //fmt = HAL_PIXEL_FORMAT_YCbCr_420_SP;
    }
    else {
        mActualFormat = mFormat;
    }

    mVideoSnapShot = videoSnapshot;
    if (mVideoSnapShot) {
        FLOGE("%s video Snapshot", __FUNCTION__);
        mPrepared = true;
        return ret;
    }

    fAssert(mDeviceAdapter.get() != NULL);
    ret = mDeviceAdapter->setDeviceConfig(mWidth, mHeight, mActualFormat, fps);
    if (ret != NO_ERROR) {
        FLOGE("%s setDeviceConfig failed", __FUNCTION__);
        errCode = CAMERA2_MSG_ERROR_DEVICE;
        goto fail;
    }

    fAssert(mPhysMemAdapter != NULL);
    mDeviceAdapter->setCameraBufferProvide(mPhysMemAdapter);

    ret = mPhysMemAdapter->allocateBuffers(mWidth, mHeight,
                        mActualFormat, 2/*MAX_CAPTURE_BUFFER*/);
    if (ret != NO_ERROR) {
        FLOGE("%s allocateBuffers failed", __FUNCTION__);
        errCode = CAMERA2_MSG_ERROR_DEVICE;
        goto fail;
    }

    mPrepared = true;
    return NO_ERROR;

fail:
    mPhysMemAdapter->freeBuffers();
    FLOGE("Error occurred, performing cleanup");

    if (NULL != mErrorListener) {
        mErrorListener->handleError(errCode);
    }

    return BAD_VALUE;
}

int CaptureStream::start()
{
    FLOG_TRACE("CaptureStream::start");
    int ret = 0;
    StreamAdapter::start();

    if (mVideoSnapShot) {
        FLOGE("%s video Snapshot", __FUNCTION__);
        return ret;
    }

    fAssert(mDeviceAdapter.get() != NULL);
    ret = mDeviceAdapter->startImageCapture();
    if (ret != NO_ERROR) {
        FLOGE("Couldn't start preview for DeviceAdapter");
        return ret;
    }
    return NO_ERROR;
}

int CaptureStream::stop()
{
    FLOG_TRACE("CaptureStream::stop");
    StreamAdapter::stop();

    if (mVideoSnapShot) {
        FLOGE("%s video Snapshot", __FUNCTION__);
        return NO_ERROR;
    }

    if (mDeviceAdapter.get() != NULL) {
        mDeviceAdapter->stopImageCapture();
    }
    return NO_ERROR;
}

int CaptureStream::release()
{
    FLOG_TRACE("CaptureStream::release");
    StreamAdapter::release();
    if (mVideoSnapShot) {
        FLOGE("%s video Snapshot", __FUNCTION__);
        return NO_ERROR;
    }

    return mPhysMemAdapter->freeBuffers();
}

void CaptureStream::applyRequest()
{
    sem_wait(&mRespondSem);
}

int CaptureStream::processFrame(CameraFrame *frame)
{
    status_t ret = NO_ERROR;

    StreamBuffer buffer;
    ret = requestBuffer(&buffer);
    if (ret != NO_ERROR) {
        FLOGE("%s requestBuffer failed", __FUNCTION__);
        goto exit_err;
    }

    mJpegBuilder->reset();
    mJpegBuilder->setMetadaManager(mMetadaManager);
    ret = makeJpegImage(&buffer, frame);
    if (ret != NO_ERROR) {
        FLOGE("%s makeJpegImage failed", __FUNCTION__);
        goto exit_err;
    }

    buffer.mTimeStamp = frame->mTimeStamp;
    ret = renderBuffer(&buffer);
    if (ret != NO_ERROR) {
        FLOGE("%s renderBuffer failed", __FUNCTION__);
        goto exit_err;
    }

exit_err:
    sem_post(&mRespondSem);

    return ret;
}


status_t CaptureStream::makeJpegImage(StreamBuffer *dstBuf, StreamBuffer *srcBuf)
{
    status_t ret = NO_ERROR;
    int encodeQuality = 100, thumbQuality = 100;
    int thumbWidth, thumbHeight;
    JpegParams *mainJpeg = NULL, *thumbJpeg = NULL;
    void *rawBuf = NULL, *thumbBuf = NULL;
    size_t imageSize = 0;

    if (dstBuf == NULL || srcBuf == NULL) {
        FLOGE("%s invalid param", __FUNCTION__);
        return BAD_VALUE;
    }

    sp<MemoryHeapBase> rawFrame(
                    new MemoryHeapBase(srcBuf->mSize, 0, "rawFrame"));
    rawBuf = rawFrame->getBase();
    if (rawBuf == MAP_FAILED) {
        FLOGE("%s new MemoryHeapBase failed", __FUNCTION__);
        return BAD_VALUE;
    }

    sp<MemoryHeapBase> thumbFrame(
                new MemoryHeapBase(srcBuf->mSize, 0, "thumbFrame"));
    thumbBuf = thumbFrame->getBase();
    if (thumbBuf == MAP_FAILED) {
        FLOGE("%s new MemoryHeapBase failed", __FUNCTION__);
        return BAD_VALUE;
    }

    ret = mMetadaManager->getJpegQuality(encodeQuality);
    if (ret != NO_ERROR) {
        FLOGE("%s getJpegQuality failed", __FUNCTION__);
        return BAD_VALUE;
    }

    if ((encodeQuality < 0) || (encodeQuality > 100)) {
        encodeQuality = 100;
    }

    ret = mMetadaManager->getJpegThumbQuality(thumbQuality);
    if (ret != NO_ERROR) {
        FLOGE("%s getJpegThumbQuality failed", __FUNCTION__);
        return BAD_VALUE;
    }

    if ((thumbQuality < 0) || (thumbQuality > 100)) {
        thumbQuality = 100;
    }

    mainJpeg = new JpegParams((uint8_t *)srcBuf->mVirtAddr,
                       srcBuf->mSize, (uint8_t *)rawBuf,
                       srcBuf->mSize, encodeQuality,
                       srcBuf->mWidth, srcBuf->mHeight,
                       srcBuf->mWidth, srcBuf->mHeight,
                       mActualFormat);

    ret = mMetadaManager->getJpegThumbSize(thumbWidth, thumbHeight);
    if (ret != NO_ERROR) {
        FLOGE("%s getJpegThumbSize failed", __FUNCTION__);
        goto err_out;
    }

    if ((thumbWidth > 0) && (thumbHeight > 0)) {
        int thumbSize   = 0;
        int thumbFormat = convertPixelFormatToV4L2Format(mActualFormat);
        switch (thumbFormat) {
            case v4l2_fourcc('N', 'V', '1', '2'):
                thumbSize = thumbWidth * thumbHeight * 3 / 2;
                break;

            case v4l2_fourcc('Y', 'U', '1', '2'):
                thumbSize = thumbWidth * thumbHeight * 3 / 2;
                break;

            case v4l2_fourcc('Y', 'U', 'Y', 'V'):
                thumbSize = thumbWidth * thumbHeight * 2;
                break;

            default:
                FLOGE("Error: %s format not supported", __FUNCTION__);
                goto err_out;
        }
        thumbSize = srcBuf->mSize;
        thumbJpeg = new JpegParams((uint8_t *)srcBuf->mVirtAddr,
                           srcBuf->mSize,
                           (uint8_t *)thumbBuf,
                           thumbSize,
                           thumbQuality,
                           srcBuf->mWidth,
                           srcBuf->mHeight,
                           thumbWidth,
                           thumbHeight,
                           mActualFormat);
    }

    mJpegBuilder->prepareImage(dstBuf);
    ret = mJpegBuilder->encodeImage(mainJpeg, thumbJpeg);
    if (ret != NO_ERROR) {
        FLOGE("%s encodeImage failed", __FUNCTION__);
        goto err_out;
    }

    imageSize = mJpegBuilder->getImageSize();
    ret = mJpegBuilder->buildImage(dstBuf);
    if (ret != NO_ERROR) {
        FLOGE("%s buildImage failed", __FUNCTION__);
        goto err_out;
    }

err_out:
    if (mainJpeg) {
        delete mainJpeg;
    }

    if (thumbJpeg) {
        delete thumbJpeg;
    }

    return ret;
}
