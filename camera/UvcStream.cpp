/*
 * Copyright (C) 2012-2016 Freescale Semiconductor, Inc.
 * Copyright 2020 NXP.
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

#define LOG_TAG "UVCStream"

#include "CameraConfigurationParser.h"
#include "UvcStream.h"

namespace android {

int32_t UvcStream::onDeviceConfigureLocked(uint32_t format, uint32_t width, uint32_t height, uint32_t fps)
{
    ALOGI("%s", __func__);

    if (mDev <= 0) {
        // usb camera should open dev node again.
        // because when stream off, the dev node must close.
        mDev = open(mUvcPath, O_RDWR);
        if (mDev <= 0) {
            ALOGE("%s invalid fd handle", __func__);
            return BAD_VALUE;
        }
    }

    return DMAStream::onDeviceConfigureLocked(format, width, height, fps);
}

int32_t UvcStream::onDeviceStopLocked()
{
    ALOGI("%s", __func__);
    int32_t ret = DMAStream::onDeviceStopLocked();
    // usb camera must close device after stream off.
    if (mDev > 0) {
        close(mDev);
        mDev = -1;
    }

    return ret;
}

int32_t UvcStream::onDeviceStartLocked()
{
    ALOGI("%s", __func__);
    return DMAStream::onDeviceStartLocked();
}

ImxStreamBuffer* UvcStream::onFrameAcquireLocked()
{
    ALOGV("%s", __func__);
    return DMAStream::onFrameAcquireLocked();
}

int32_t UvcStream::onFrameReturnLocked(ImxStreamBuffer& buf)
{
    ALOGV("%s", __func__);
    return DMAStream::onFrameReturnLocked(buf);
}

// usb camera require the specific buffer size.
int32_t UvcStream::getDeviceBufferSize()
{
    int32_t size = 0;
    switch (mFormat) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            size = ((mWidth + 16) & (~15)) * mHeight * 3 / 2;
            break;

         case HAL_PIXEL_FORMAT_YCbCr_420_P: {
            int32_t stride = (mWidth+31)/32*32;
            int32_t c_stride = (stride/2+15)/16*16;
            size = (stride + c_stride) * mHeight;
             break;
         }

         case HAL_PIXEL_FORMAT_YCbCr_422_I:
            size = mWidth * mHeight * 2;
             break;

        default:
            ALOGE("Error: %s format not supported", __func__);
            break;
    }

    return size;
}

} // namespace android
