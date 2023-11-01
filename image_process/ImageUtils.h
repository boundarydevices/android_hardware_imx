/*
 *  Copyright 2023 NXP.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef IMAGE_UTILS_H
#define IMAGE_UTILS_H

#include <linux/videodev2.h>
#include <graphics_ext.h>
#include "Memory.h"
#include "MemoryDesc.h"
#include "opencl-2d.h"

namespace android {

typedef struct tag_imx_image_buffer {
    uint32_t mFormat;
    uint32_t mWidth;
    uint32_t mHeight;
    uint32_t mStride;
    uint32_t mHeightSpan; // handle C920 1080p MJPG hardware decoded to 1088p yuyv/nv16
    void *mVirtAddr;
    uint64_t mPhyAddr;
    int32_t mFd;
    size_t mSize; // great than mFormatSize due to alignment.
    size_t mFormatSize; // the actual sthe allocated buffer size, usually size caculated by format and resolution.
    buffer_handle_t buffer; // G2D need in lockSurface()
    float mZoomRatio; // just g2d/dpu support, set in source ImxImageBuffer
    uint32_t mUsage;  // currently used to decide cache/un-cache.
    void *mPrivate; // user context
} ImxImageBuffer;

int yuv422iResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth, int dstHeight);
// If srcHeightSpan is not given, will set to srcHeight in the func.
int yuv422spResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth, int dstHeight, int srcHeightSpan = 0);
int yuv420spResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth, int dstHeight);
int convertPixelFormatToCLFormat(int format);
int convertPixelFormatToV4L2Format(int format, bool invert = false);
int convertV4L2FormatToPixelFormat(uint32_t fourcc);
int32_t getSizeByForamtRes(int32_t format, uint32_t width, uint32_t height, bool align);
int AllocPhyBuffer(ImxImageBuffer &imxBuf);
int FreePhyBuffer(ImxImageBuffer &imxBuf);
void SwitchImxBuf(ImxImageBuffer &imxBufA, ImxImageBuffer &imxBufB);

} // namespace android

#endif // IMAGE_UTILS_H
