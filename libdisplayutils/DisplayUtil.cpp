/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Copyright 2021-2022 NXP.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "DisplayUtil.h"
#include "MemoryDesc.h"
#include <android/hardware/graphics/common/1.2/types.h>
#include <android-base/stringprintf.h>
#include <android-base/strings.h>
#include <cutils/log.h>
#include <hardware/gralloc.h>
#include <graphics_ext.h>
#include <inttypes.h>

namespace fsl {

using android::hardware::graphics::common::V1_2::BufferUsage;

int convert_gralloc_format_to_nxp_format(int format)
{
    int fslFormat = 0;
    switch (format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            fslFormat = FORMAT_RGBA8888;
            break;
        case HAL_PIXEL_FORMAT_RGBX_8888:
            fslFormat = FORMAT_RGBX8888;
            break;
        case HAL_PIXEL_FORMAT_BGRA_8888:
            fslFormat = FORMAT_BGRA8888;
            break;
        case HAL_PIXEL_FORMAT_RGB_888:
            fslFormat = FORMAT_RGB888;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
            fslFormat = FORMAT_RGB565;
            break;
        case HAL_PIXEL_FORMAT_YV12:
            fslFormat = FORMAT_YV12;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            fslFormat = FORMAT_NV16;
            break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            fslFormat = FORMAT_NV21;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            fslFormat = FORMAT_YUYV;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            fslFormat = FORMAT_I420;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            fslFormat = FORMAT_NV12;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
            fslFormat = FORMAT_NV12;
            break;
        case HAL_PIXEL_FORMAT_BLOB:
            fslFormat = FORMAT_BLOB;
            break;
        case HAL_PIXEL_FORMAT_NV12_TILED:
            fslFormat = FORMAT_NV12_TILED;
            break;
        case HAL_PIXEL_FORMAT_NV12_G1_TILED:
            fslFormat = FORMAT_NV12_G1_TILED;
            break;
        case HAL_PIXEL_FORMAT_NV12_G2_TILED:
            fslFormat = FORMAT_NV12_G2_TILED;
            break;
        case HAL_PIXEL_FORMAT_NV12_G2_TILED_COMPRESSED:
            fslFormat = FORMAT_NV12_G2_TILED_COMPRESSED;
            break;
        case HAL_PIXEL_FORMAT_YCBCR_P010:
            fslFormat = FORMAT_YCBCR_P010;
            break;
        case HAL_PIXEL_FORMAT_P010:
            fslFormat = FORMAT_P010;
            break;
        case HAL_PIXEL_FORMAT_P010_TILED:
            fslFormat = FORMAT_P010_TILED;
            break;
        case HAL_PIXEL_FORMAT_P010_TILED_COMPRESSED:
            fslFormat = FORMAT_P010_TILED_COMPRESSED;
            break;
        case HAL_PIXEL_FORMAT_RGBA_1010102:
            fslFormat = FORMAT_RGBA1010102;
            break;
        case HAL_PIXEL_FORMAT_RGBA_FP16:
            fslFormat = FORMAT_RGBAFP16;
            break;
        case HAL_PIXEL_FORMAT_RAW16:
            fslFormat = FORMAT_RAW16;
            break;
        case HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED:
            fslFormat = FORMAT_NV12;
            break;
        default:
            ALOGE("%s invalid format:0x%x", __func__, format);
            break;
    }

    return fslFormat;
}

std::string getNxpFormatString(int format)
{
    switch (format) {
        case FORMAT_RGBA8888:
            return "RGBA8888";
        case FORMAT_RGBX8888:
            return "RGBX8888";
        case FORMAT_BGRA8888:
            return "BGRA8888";
        case FORMAT_RGB888:
            return "RGB888";
        case FORMAT_RGB565:
            return "RGB565";
        case FORMAT_RGBA1010102:
            return "RGBA1010102";
        case FORMAT_RGBAFP16:
            return "RGBAFP16";
        case FORMAT_BLOB:
            return "BLOB";
        case FORMAT_YV12:
            return "YV12";
        case FORMAT_NV16:
            return "NV16";
        case FORMAT_NV21:
            return "NV21";
        case FORMAT_YUYV:
            return "YUYV";
        case FORMAT_I420:
            return "I420";
        case FORMAT_NV12:
            return "NV12";
        case FORMAT_NV12_TILED:
            return "NV12_TILED";
        case FORMAT_NV12_G1_TILED:
            return "NV12_G1_TILED";
        case FORMAT_NV12_G2_TILED:
            return "NV12_G2_TILED";
        case FORMAT_NV12_G2_TILED_COMPRESSED:
            return "NV12_G2_TILED_COMPRESSED";
        case FORMAT_YCBCR_P010:
            return "YCBCR_P010";
        case FORMAT_P010:
            return "P010";
        case FORMAT_P010_TILED:
            return "P010_TILED";
        case FORMAT_P010_TILED_COMPRESSED:
            return "P010_TILED_COMPRESSED";
        case FORMAT_RAW16:
            return "FORMAT_RAW16";
        default:
            return android::base::StringPrintf("Unknown(0x%x)",format);
    }
}

std::string getGrallocFormatString(int format)
{
    int nxpFormat = convert_gralloc_format_to_nxp_format(format);
    return getNxpFormatString(nxpFormat);
}

std::string getUsageString(uint64_t bufferUsage)
{
    using Underlying = typename std::underlying_type<BufferUsage>::type;

    Underlying usage = static_cast<Underlying>(bufferUsage);

    std::vector<std::string> usages;
    if (usage & BufferUsage::CAMERA_INPUT) {
        usage &= ~static_cast<Underlying>(BufferUsage::CAMERA_INPUT);
        usages.push_back("CAMERA_INPUT");
    }
    if (usage & BufferUsage::CAMERA_OUTPUT) {
        usage &= ~static_cast<Underlying>(BufferUsage::CAMERA_OUTPUT);
        usages.push_back("CAMERA_OUTPUT");
    }
    if (usage & BufferUsage::COMPOSER_CURSOR) {
        usage &= ~static_cast<Underlying>(BufferUsage::COMPOSER_CURSOR);
        usages.push_back("COMPOSER_CURSOR");
    }
    if (usage & BufferUsage::COMPOSER_OVERLAY) {
        usage &= ~static_cast<Underlying>(BufferUsage::COMPOSER_OVERLAY);
        usages.push_back("COMPOSER_OVERLAY");
    }
    if (usage & BufferUsage::CPU_READ_OFTEN) {
        usage &= ~static_cast<Underlying>(BufferUsage::CPU_READ_OFTEN);
        usages.push_back("CPU_READ_OFTEN");
    }
    if (usage & BufferUsage::CPU_READ_NEVER) {
        usage &= ~static_cast<Underlying>(BufferUsage::CPU_READ_NEVER);
        usages.push_back("CPU_READ_NEVER");
    }
    if (usage & BufferUsage::CPU_READ_RARELY) {
        usage &= ~static_cast<Underlying>(BufferUsage::CPU_READ_RARELY);
        usages.push_back("CPU_READ_RARELY");
    }
    if (usage & BufferUsage::CPU_WRITE_NEVER) {
        usage &= ~static_cast<Underlying>(BufferUsage::CPU_WRITE_NEVER);
        usages.push_back("CPU_WRITE_NEVER");
    }
    if (usage & BufferUsage::CPU_WRITE_OFTEN) {
        usage &= ~static_cast<Underlying>(BufferUsage::CPU_WRITE_OFTEN);
        usages.push_back("CPU_WRITE_OFTEN");
    }
    if (usage & BufferUsage::CPU_WRITE_RARELY) {
        usage &= ~static_cast<Underlying>(BufferUsage::CPU_WRITE_RARELY);
        usages.push_back("CPU_WRITE_RARELY");
    }
    if (usage & BufferUsage::GPU_RENDER_TARGET) {
        usage &= ~static_cast<Underlying>(BufferUsage::GPU_RENDER_TARGET);
        usages.push_back("GPU_RENDER_TARGET");
    }
    if (usage & BufferUsage::GPU_TEXTURE) {
        usage &= ~static_cast<Underlying>(BufferUsage::GPU_TEXTURE);
        usages.push_back("GPU_TEXTURE");
    }
    if (usage & BufferUsage::PROTECTED) {
        usage &= ~static_cast<Underlying>(BufferUsage::PROTECTED);
        usages.push_back("PROTECTED");
    }
    if (usage & BufferUsage::RENDERSCRIPT) {
        usage &= ~static_cast<Underlying>(BufferUsage::RENDERSCRIPT);
        usages.push_back("RENDERSCRIPT");
    }
    if (usage & BufferUsage::VIDEO_DECODER) {
        usage &= ~static_cast<Underlying>(BufferUsage::VIDEO_DECODER);
        usages.push_back("VIDEO_DECODER");
    }
    if (usage & BufferUsage::VIDEO_ENCODER) {
        usage &= ~static_cast<Underlying>(BufferUsage::VIDEO_ENCODER);
        usages.push_back("VIDEO_ENCODER");
    }

    if (usage & GRALLOC_USAGE_PRIVATE_0 ) {
        usage &= ~static_cast<Underlying>(GRALLOC_USAGE_PRIVATE_0);
        usages.push_back("PRIVATE_0");
    }
    if (usage & GRALLOC_USAGE_PRIVATE_1 ) {
        usage &= ~static_cast<Underlying>(GRALLOC_USAGE_PRIVATE_1);
        usages.push_back("PRIVATE_1");
    }
    if (usage & GRALLOC_USAGE_PRIVATE_2 ) {
        usage &= ~static_cast<Underlying>(GRALLOC_USAGE_PRIVATE_2);
        usages.push_back("PRIVATE_2");
    }
    if (usage & GRALLOC_USAGE_PRIVATE_3 ) {
        usage &= ~static_cast<Underlying>(GRALLOC_USAGE_PRIVATE_3);
        usages.push_back("PRIVATE_3");
    }
    if (usage & BufferUsage::VENDOR_MASK) {
        usages.push_back(android::base::StringPrintf("UnknownUsageBits-%" PRIu64 , usage));
    }

    return android::base::Join(usages, '|');
}
}
