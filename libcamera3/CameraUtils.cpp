/*
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc.
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

#include "CameraUtils.h"
#include <linux/videodev2.h>
#include "Metadata.h"
#include "Stream.h"

using namespace android;

int convertPixelFormatToV4L2Format(PixelFormat format, bool invert)
{
    int nFormat = 0;

    switch (format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            // IPU doesn't support NV21, so treat this two format as the same.
            nFormat = v4l2_fourcc('N', 'V', '1', '2');
            break;

        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            if (!invert) {
                nFormat = v4l2_fourcc('Y', 'U', '1', '2');
            }
            else {
                nFormat = v4l2_fourcc('Y', 'V', '1', '2');
            }
            break;

        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            nFormat = v4l2_fourcc('Y', 'U', 'Y', 'V');
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            nFormat = v4l2_fourcc('N', 'V', '1', '6');
            break;
        case HAL_PIXEL_FORMAT_YCbCr_444_888:
            nFormat = v4l2_fourcc('Y', 'U', 'V', '4');
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
            nFormat = v4l2_fourcc('N', 'V', '1', '2');
            break;

        default:
            ALOGE("Error: format:0x%x not supported!", format);
            break;
    }
    ALOGV("v4l2 format: %c%c%c%c", nFormat&0xFF, (nFormat>>8)&0xFF,
                            (nFormat>>16)&0xFF, (nFormat>>24)&0xFF);
    return nFormat;
}

StreamBuffer::StreamBuffer()
{
}

StreamBuffer::~StreamBuffer()
{
}

void StreamBuffer::initialize(buffer_handle_t* buf_h)
{
    if (buf_h == NULL) {
        return;
    }

    private_handle_t *handle = (private_handle_t *)(*buf_h);
    mBufHandle = buf_h;
    mVirtAddr  = (void *)handle->base;
    mPhyAddr   = handle->phys;
    mSize      = handle->size;

    //for uvc jpeg stream
    mpFrameBuf  = NULL;

}

//--------------------CaptureRequest----------------------
CaptureRequest::CaptureRequest()
    : mOutBuffersNumber(0)
{
    for (uint32_t i = 0; i < MAX_STREAM_BUFFERS; i++) {
        mOutBuffers[i] = NULL;
    }
}

CaptureRequest::~CaptureRequest()
{
    for (uint32_t i = 0; i < mOutBuffersNumber; i++) {
        if (mOutBuffers[i] != NULL)
            delete mOutBuffers[i];
    }
}

void CaptureRequest::init(camera3_capture_request* request,
              camera3_callback_ops* callback,
              sp<Metadata> settings)
{
    mFrameNumber = request->frame_number;
    mSettings = settings;
    mOutBuffersNumber = request->num_output_buffers;
    mRequest = request;
    mCallbackOps = callback;

    ALOGV("CaptureRequest fm:%d, bn:%d", mFrameNumber, mOutBuffersNumber);
    for (uint32_t i = 0; i < request->num_output_buffers; i++) {
        mOutBuffers[i] = new StreamBuffer();
        mOutBuffers[i]->mStream = reinterpret_cast<Stream*>(
                            request->output_buffers[i].stream->priv);
        mOutBuffers[i]->mAcquireFence = request->output_buffers[i].acquire_fence;
        mOutBuffers[i]->initialize(request->output_buffers[i].buffer);
    }
}

int32_t CaptureRequest::onCaptureError()
{
    camera3_stream_buffer_t cameraBuffer;
    camera3_capture_result_t result;

    cameraBuffer.status = CAMERA3_BUFFER_STATUS_ERROR;
    cameraBuffer.acquire_fence = -1;
    cameraBuffer.release_fence = -1;

    memset(&result, 0, sizeof(result));
    result.frame_number = mFrameNumber;
    result.result = NULL;
    result.num_output_buffers = 1;
    result.output_buffers = &cameraBuffer;

    for (uint32_t i=0; i<mOutBuffersNumber; i++) {
        StreamBuffer* out = mOutBuffers[i];
        cameraBuffer.stream = out->mStream->stream();
        cameraBuffer.buffer = out->mBufHandle;
        mCallbackOps->process_capture_result(mCallbackOps, &result);
    }

    return 0;
}

int32_t CaptureRequest::onCaptureDone(StreamBuffer* buffer)
{
    if (buffer == NULL || buffer->mBufHandle == NULL || mCallbackOps == NULL) {
        return 0;
    }

    camera3_stream_buffer_t cameraBuffer;
    cameraBuffer.stream = buffer->mStream->stream();
    cameraBuffer.buffer = buffer->mBufHandle;
    cameraBuffer.status = CAMERA3_BUFFER_STATUS_OK;
    cameraBuffer.acquire_fence = -1;
    cameraBuffer.release_fence = -1;

    camera3_capture_result_t result;
    memset(&result, 0, sizeof(result));
    result.frame_number = mFrameNumber;
    result.result = NULL;
    result.num_output_buffers = 1;
    result.output_buffers = &cameraBuffer;

    ALOGV("onCaptureDone fm:%d", mFrameNumber);
    mCallbackOps->process_capture_result(mCallbackOps, &result);
    return 0;
}

int32_t CaptureRequest::onSettingsDone(sp<Metadata> meta)
{
    if (meta == NULL || (meta->get() == NULL) || mCallbackOps == NULL) {
        return 0;
    }

    camera_metadata_entry_t entry = meta->find(ANDROID_SENSOR_TIMESTAMP);
    if (entry.count <= 0) {
        ALOGW("invalid meta data");
        return 0;
    }

    camera3_capture_result_t result;
    memset(&result, 0, sizeof(result));
    result.frame_number = mFrameNumber;
    result.result = meta->get();
    result.num_output_buffers = 0;
    result.output_buffers = NULL;

    ALOGV("onSettingsDone fm:%d", mFrameNumber);
    mCallbackOps->process_capture_result(mCallbackOps, &result);
    return 0;
}

//------------------SensorData------------------------
SensorData::SensorData()
{
    mVpuSupportFmt[0] = HAL_PIXEL_FORMAT_YCbCr_420_SP;
    mVpuSupportFmt[1] = HAL_PIXEL_FORMAT_YCbCr_420_P;

    mPictureSupportFmt[0] = HAL_PIXEL_FORMAT_YCbCr_420_SP;
    mPictureSupportFmt[1] = HAL_PIXEL_FORMAT_YCbCr_422_I;

    memset(mSensorFormats, 0, sizeof(mSensorFormats));
    memset(mAvailableFormats, 0, sizeof(mAvailableFormats));

    memset(mPreviewResolutions, 0, sizeof(mPreviewResolutions));
    memset(mPictureResolutions, 0, sizeof(mPictureResolutions));
}

SensorData::~SensorData()
{
}

int32_t SensorData::getSensorFormat(int32_t availFormat)
{
    for (int32_t i=0; i<mSensorFormatCount; i++) {
        if (availFormat == mSensorFormats[i]) {
            return availFormat;
        }
    }
    // return the first sensor format by default.
    return mSensorFormats[0];
}

int32_t SensorData::changeSensorFormats(int *src, int *dst, int len)
{
    if (src == NULL || dst == NULL || len == 0) {
        ALOGE("%s invalid parameters", __func__);
        return 0;
    }

    int32_t k = 0;
    for (int32_t i=0; i<len && i<MAX_SENSOR_FORMAT; i++) {
        switch (src[i]) {
            case v4l2_fourcc('N', 'V', '1', '2'):
                dst[k++] = HAL_PIXEL_FORMAT_YCbCr_420_SP;
                break;

            case v4l2_fourcc('Y', 'V', '1', '2'):
                dst[k++] = HAL_PIXEL_FORMAT_YCbCr_420_P;
                break;

            case v4l2_fourcc('Y', 'U', 'Y', 'V'):
                dst[k++] = HAL_PIXEL_FORMAT_YCbCr_422_I;
                break;

            case v4l2_fourcc('B', 'L', 'O', 'B'):
                dst[k++] = HAL_PIXEL_FORMAT_BLOB;
                break;

            case v4l2_fourcc('R', 'A', 'W', 'S'):
                dst[k++] = HAL_PIXEL_FORMAT_RAW16;
                break;
            case v4l2_fourcc('N', 'V', '1', '6'):
                dst[k++] = HAL_PIXEL_FORMAT_YCbCr_422_SP;
                break;
            case v4l2_fourcc('Y', 'U', 'V', '4'):
                dst[k++] = HAL_PIXEL_FORMAT_YCbCr_444_888;

            default:
                ALOGE("Error: format:%c%c%c%c not supported!", src[i]&0xFF,
                      (src[i]>>8)&0xFF, (src[i]>>16)&0xFF, (src[i]>>24)&0xFF);
                break;
        }
    }

    return k;
}

int SensorData::getCaptureMode(int width, int height)
{
    int capturemode = 0;

    if ((width == 640) && (height == 480)) {
        capturemode = 0;
    }
    else if ((width == 320) && (height == 240)) {
        capturemode = 1;
    }
    else if ((width == 720) && (height == 480)) {
        capturemode = 2;
    }
    else if ((width == 720) && (height == 576)) {
        capturemode = 3;
    }
    else if ((width == 1280) && (height == 720)) {
        capturemode = 4;
    }
    else if ((width == 1920) && (height == 1080)) {
        capturemode = 5;
    }
    else if ((width == 2592) && (height == 1944)) {
        capturemode = 6;
    }
    else if ((width == 176) && (height == 144)) {
        capturemode = 7;
    }
    else if ((width == 1024) && (height == 768)) {
        capturemode = 8;
    }
    else {
        ALOGE("width:%d height:%d is not supported.", width, height);
    }
    return capturemode;
}

status_t SensorData::adjustPreviewResolutions()
{
    int xTmp, yTmp, xMax, yMax, idx;
    idx = 0;
    xTmp = xMax = mPreviewResolutions[0];
    yTmp = yMax = mPreviewResolutions[1];
    for (int i=0; i<MAX_RESOLUTION_SIZE; i+=2) {
        if (mPreviewResolutions[i] > xMax) {
            xMax = mPreviewResolutions[i];
            yMax = mPreviewResolutions[i+1];
            idx = i;
        }
    }

    mPreviewResolutions[0] = xMax;
    mPreviewResolutions[1] = yMax;
    mPreviewResolutions[idx] = xTmp;
    mPreviewResolutions[idx+1] = yTmp;

    return 0;
}

status_t SensorData::setMaxPictureResolutions()
{
    int xMax, yMax;
    xMax = mPictureResolutions[0];
    yMax = mPictureResolutions[1];

    for (int i=0; i<MAX_RESOLUTION_SIZE; i+=2) {
        if (mPictureResolutions[i] > xMax || mPictureResolutions[i+1] > yMax) {
            xMax = mPictureResolutions[i];
            yMax = mPictureResolutions[i+1];
        }
    }

    mMaxWidth = xMax;
    mMaxHeight = yMax;

    return 0;
}

PixelFormat SensorData::getMatchFormat(int *sfmt, int  slen,
                                         int *dfmt, int  dlen)
{
    if ((sfmt == NULL) || (slen == 0) || (dfmt == NULL) || (dlen == 0)) {
        ALOGE("getMatchFormat invalid parameters");
        return 0;
    }

    PixelFormat matchFormat = 0;
    bool live = true;
    for (int i = 0; i < slen && live; i++) {
        for (int j = 0; j < dlen; j++) {
            if (sfmt[i] == dfmt[j]) {
                matchFormat = dfmt[j];
                live        = false;
                break;
            }
        }
    }

    return matchFormat;
}

