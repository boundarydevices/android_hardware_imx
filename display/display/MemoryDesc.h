/*
 * Copyright 2017 NXP
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

#ifndef FSL_MEMORYDESC_H_
#define FSL_MEMORYDESC_H_

#include <stdint.h>
#include <utils/Mutex.h>

namespace fsl {

using android::Mutex;

enum {
    FORMAT_RGBA8888 = 1,
    FORMAT_RGBX8888 = 2,
    FORMAT_RGB888   = 3,
    FORMAT_RGB565   = 4,
    FORMAT_BGRA8888 = 5,
    FORMAT_RGBA1010102 = 0x2B,
    FORMAT_RGBAFP16 = 0x16,
    FORMAT_BLOB  = 0x21,
    FORMAT_YV12  = 0x32315659, // YCrCb 4:2:0 Planar
    FORMAT_NV16  = 0x10, // NV16
    FORMAT_NV21  = 0x11, // NV21
    FORMAT_YUYV  = 0x14, // YUY2
    FORMAT_I420  = 0x101,
    FORMAT_NV12  = 0x103,
    FORMAT_NV12_TILED = 0x104,
    FORMAT_NV12_G1_TILED = 0x105,
    FORMAT_NV12_G2_TILED = 0x106,
    FORMAT_NV12_G2_TILED_COMPRESSED = 0x107,
    FORMAT_P010                  = 0x108,
    FORMAT_P010_TILED            = 0x109,
    FORMAT_P010_TILED_COMPRESSED = 0x110,
};

struct MemoryDesc
{
    static const int sMagic = 0x31415920;
    MemoryDesc();
    bool isValid();
    int checkFormat();

    int mMagic;
    int mFlag;
    int mWidth;
    int mHeight;
    int mFormat;
    int mFslFormat;
    int mStride;
    int mBpp;
    int mSize;
    int64_t mProduceUsage;
    int64_t mConsumeUsage;
};

}
#endif /* GRALLOC_PRIV_H_ */
