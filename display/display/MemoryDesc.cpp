/*
 * Copyright 2017-2018 NXP.
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

#include <cutils/log.h>

#include "Memory.h"
#include "MemoryDesc.h"
#include "Composer.h"

namespace fsl {

MemoryDesc::MemoryDesc()
   : mMagic(sMagic), mFlag(0), mWidth(0),
     mHeight(0), mFormat(0), mFslFormat(0), mStride(0),
     mBpp(0), mSize(0), mProduceUsage(0),
     mConsumeUsage(0)
{
}

bool MemoryDesc::isValid()
{
    return (mMagic == sMagic);
}

int MemoryDesc::checkFormat()
{
    size_t size, alignedw, alignedh, bpp = 0;

    if (mWidth == 0 || mHeight == 0) {
        ALOGE("%s width and height not set", __func__);
        return -EINVAL;
    }

    switch (mFslFormat) {
        case FORMAT_RGBA8888:
        case FORMAT_RGBX8888:
        case FORMAT_BGRA8888:
        case FORMAT_RGBA1010102:
            bpp = 4;
        case FORMAT_RGB888:
        case FORMAT_RGB565:
        case FORMAT_RGBAFP16: {
            if (mFslFormat == FORMAT_RGB565) {
                bpp = 2;
            }
            else if (mFslFormat == FORMAT_RGB888) {
                bpp = 3;
            }
            else if (mFslFormat == FORMAT_RGBAFP16) {
                bpp = 8;
            }

            /*
             * Vivante HAL needs 64 pixel alignment in width and 64 pixel
             * alignment in height when render with tile buffer.
             * tile buffer needs additional 128 byte size.
             *
             * Here we assume the buffer will be used by Vivante HAL...
             */
#if defined (IMX8)
            if (mProduceUsage & USAGE_GPU_TILED_VIV) {
                alignedw = ALIGN_PIXEL_64(mWidth);
                alignedh = ALIGN_PIXEL_64(mHeight);
                size = alignedw * alignedh * bpp + 128;
            }
            else
#elif !defined (USE_SW_OPENGL)
            if (true) {
                Composer *composer = Composer::getInstance();
                alignedw = mWidth; alignedh = mHeight;
                composer->alignTile((int*)&alignedw, (int*)&alignedh, mFslFormat, mProduceUsage);
                size = alignedw * alignedh * bpp + 128;
            }
            else
#endif
            {
                alignedw = ALIGN_PIXEL_16(mWidth);
                alignedh = ALIGN_PIXEL_16(mHeight);
                size = alignedw * alignedh * bpp;
            }

            if (mProduceUsage & USAGE_HW_VIDEO_ENCODER) {
                mProduceUsage |= USAGE_HW_COMPOSER | USAGE_HW_2D | USAGE_HW_RENDER |
                                 USAGE_SW_WRITE_OFTEN | USAGE_SW_READ_OFTEN;
            }
            } break;

        case FORMAT_BLOB:
            alignedw = mWidth;
            alignedh = mHeight;
            size = alignedw * alignedh;
            mFslFormat = FORMAT_NV12;
            if(mHeight != 1)
                ALOGW("%s, BLOB format, h %d is not 1 !!!", __func__, mHeight);
            break;

        case FORMAT_NV12:
        case FORMAT_NV21:
        case FORMAT_NV12_G1_TILED:
        case FORMAT_NV12_G2_TILED:
        case FORMAT_NV12_G2_TILED_COMPRESSED:
            alignedw = ALIGN_PIXEL_16(mWidth);
            alignedh = ALIGN_PIXEL_4(mHeight);
            size = alignedw * alignedh * 3 / 2;
            //hantro vpu need more buffer size for decoding
            if(mProduceUsage & USAGE_PADDING_BUFFER) {
                alignedh = ALIGN_PIXEL_16(mHeight);
                size = alignedw * alignedh * 3 / 2 + alignedw * alignedh / 4 + 32;
            }
            break;

        case FORMAT_P010:
        case FORMAT_P010_TILED:
        case FORMAT_P010_TILED_COMPRESSED:
            if (mFslFormat == FORMAT_P010) {
                alignedw = ALIGN_PIXEL_16(mWidth) * 5 / 4;
            }
            else {
                alignedw = ALIGN_PIXEL_16(mWidth) * 5 / 4;
            }
            alignedh = ALIGN_PIXEL_4(mHeight);
            size = alignedw * alignedh * 3 / 2 * 5 / 4;
            //hantro vpu need more buffer size for decoding
            if(mProduceUsage & USAGE_PADDING_BUFFER) {
                alignedh = ALIGN_PIXEL_16(mHeight);
                size = alignedw * alignedh * 3 / 2 * 5 / 4 + alignedw * alignedh / 4 + 32;
            }
            break;

        case FORMAT_I420:
        case FORMAT_YV12: {
            alignedw = ALIGN_PIXEL_32(mWidth);
            alignedh = ALIGN_PIXEL_4(mHeight);
            int c_stride = (alignedw/2+15)/16*16;
            size = alignedw * alignedh + c_stride * alignedh;
            } break;

        case FORMAT_NV16:
        case FORMAT_YUYV:
            alignedw = ALIGN_PIXEL_16(mWidth);
            alignedh = ALIGN_PIXEL_4(mHeight);
            size = alignedw * alignedh * 2;
            break;

        case FORMAT_NV12_TILED:
            alignedw = ALIGN_PIXEL_256(mWidth);
            alignedh = ALIGN_PIXEL_256(mHeight);
            size = alignedw * alignedh * 3 / 2;
            break;

        default:
            ALOGE("%s unsupported format:0x%x", __func__, mFslFormat);
            return -EINVAL;
    }

    mSize = size;
    mBpp = bpp;
    mStride = alignedw;

    return 0;
}

}
