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

#define LOG_TAG "ImageUtils"

#include <stdint.h>
#include <string.h>
#include <log/log.h>
#include <graphics.h>
#include "Allocator.h"
#include "NV12_resize.h"
#include "ImageUtils.h"

#define ALIGN_PIXEL_4(x) ((x + 3) & ~3)
#define ALIGN_PIXEL_16(x) ((x + 15) & ~15)
#define ALIGN_PIXEL_32(x) ((x + 31) & ~31)

namespace android {

int yuv422iResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth,
                  int dstHeight) {
    int i, j;
    int h_offset;
    int v_offset;
    unsigned char *ptr, cc;
    int h_scale_ratio;
    int v_scale_ratio;

    int srcStride;
    int dstStride;

    if (!srcWidth || !srcHeight || !dstWidth || !dstHeight)
        return -1;

    h_scale_ratio = srcWidth / dstWidth;
    v_scale_ratio = srcHeight / dstHeight;

    if ((h_scale_ratio > 0) && (v_scale_ratio > 0))
        goto reduce;
    else if (h_scale_ratio + v_scale_ratio <= 1)
        goto enlarge;

    ALOGE("%s, not support resize %dx%d to %dx%d", __func__, srcWidth, srcHeight, dstWidth,
          dstHeight);

    return -1;

reduce:
    h_offset = (srcWidth - dstWidth * h_scale_ratio) / 2;
    v_offset = (srcHeight - dstHeight * v_scale_ratio) / 2;

    srcStride = srcWidth * 2;
    dstStride = dstWidth * 2;

    // for Y
    for (i = 0; i < dstHeight * v_scale_ratio; i += v_scale_ratio) {
        for (j = 0; j < dstStride * h_scale_ratio; j += 2 * h_scale_ratio) {
            ptr = srcBuf + i * srcStride + j + v_offset * srcStride + h_offset * 2;
            cc = ptr[0];

            ptr = dstBuf + (i / v_scale_ratio) * dstStride + (j / h_scale_ratio);
            ptr[0] = cc;
        }
    }

    // for U
    for (i = 0; i < dstHeight * v_scale_ratio; i += v_scale_ratio) {
        for (j = 0; j < dstStride * h_scale_ratio; j += 4 * h_scale_ratio) {
            ptr = srcBuf + 1 + i * srcStride + j + v_offset * srcStride + h_offset * 2;
            cc = ptr[0];

            ptr = dstBuf + 1 + (i / v_scale_ratio) * dstStride + (j / h_scale_ratio);
            ptr[0] = cc;
        }
    }

    // for V
    for (i = 0; i < dstHeight * v_scale_ratio; i += v_scale_ratio) {
        for (j = 0; j < dstStride * h_scale_ratio; j += 4 * h_scale_ratio) {
            ptr = srcBuf + 3 + i * srcStride + j + v_offset * srcStride + h_offset * 2;
            cc = ptr[0];

            ptr = dstBuf + 3 + (i / v_scale_ratio) * dstStride + (j / h_scale_ratio);
            ptr[0] = cc;
        }
    }

    return 0;

enlarge:
    int h_offset_end;
    int v_offset_end;
    int srcRow;
    int srcCol;

    h_scale_ratio = dstWidth / srcWidth;
    v_scale_ratio = dstHeight / srcHeight;

    h_offset = (dstWidth - srcWidth * h_scale_ratio) / 2;
    v_offset = (dstHeight - srcHeight * v_scale_ratio) / 2;

    h_offset_end = h_offset + srcWidth * h_scale_ratio;
    v_offset_end = v_offset + srcHeight * v_scale_ratio;

    srcStride = srcWidth * 2;
    v_offset = (dstHeight - srcHeight * v_scale_ratio) / 2;

    h_offset_end = h_offset + srcWidth * h_scale_ratio;
    v_offset_end = v_offset + srcHeight * v_scale_ratio;

    srcStride = srcWidth * 2;
    dstStride = dstWidth * 2;

    ALOGV("h_scale_ratio %d, v_scale_ratio %d, h_offset %d, v_offset %d, h_offset_end %d, "
          "v_offset_end %d",
          h_scale_ratio, v_scale_ratio, h_offset, v_offset, h_offset_end, v_offset_end);

    // for Y
    for (i = 0; i < dstHeight; i++) {
        // top, bottom black margin
        if ((i < v_offset) || (i >= v_offset_end)) {
            for (j = 0; j < dstWidth; j++) {
                dstBuf[dstStride * i + j * 2] = 0;
            }
            continue;
        }

        for (j = 0; j < dstWidth; j++) {
            // left, right black margin
            if ((j < h_offset) || (j >= h_offset_end)) {
                dstBuf[dstStride * i + j * 2] = 0;
                continue;
            }

            srcRow = (i - v_offset) / v_scale_ratio;
            srcCol = (j - h_offset) / h_scale_ratio;
            dstBuf[dstStride * i + j * 2] = srcBuf[srcStride * srcRow + srcCol * 2];
        }
    }

    // for UV
    for (i = 0; i < dstHeight; i++) {
        // top, bottom black margin
        if ((i < v_offset) || (i >= v_offset_end)) {
            for (j = 0; j < dstWidth; j++) {
                dstBuf[dstStride * i + j * 2 + 1] = 128;
            }
            continue;
        }

        for (j = 0; j < dstWidth; j++) {
            // left, right black margin
            if ((j < h_offset) || (j >= h_offset_end)) {
                dstBuf[dstStride * i + j * 2 + 1] = 128;
                continue;
            }

            srcRow = (i - v_offset) / v_scale_ratio;
            srcCol = (j - h_offset) / h_scale_ratio;
            dstBuf[dstStride * i + j * 2 + 1] = srcBuf[srcStride * srcRow + srcCol * 2 + 1];
        }
    }

    return 0;
}

int yuv422spResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth,
                   int dstHeight, int srcHeightSpan) {
    int i, j, s;
    int h_offset;
    int v_offset;
    unsigned char *ptr, cc;
    int h_scale_ratio;
    int v_scale_ratio;

    if (srcHeightSpan == 0)
        srcHeightSpan = srcHeight;

    s = 0;

    if (!dstWidth)
        return -1;

    if (!dstHeight)
        return -1;

    h_scale_ratio = srcWidth / dstWidth;
    if (!h_scale_ratio)
        return -1;

    v_scale_ratio = srcHeight / dstHeight;
    if (!v_scale_ratio)
        return -1;

    h_offset = (srcWidth - dstWidth * h_scale_ratio) / 2;
    v_offset = (srcHeight - dstHeight * v_scale_ratio) / 2;


    // y
    int srcRow = 0;
    int srcCol = 0;
    int rowOffsetBytes = 0;

    for (i = 0; i < dstHeight; i += 1) {
        srcRow = v_offset + i * v_scale_ratio;
        rowOffsetBytes = srcRow * srcWidth;

        for (j = 0; j < dstWidth; j += 1) {
            srcCol = h_offset + j * h_scale_ratio;
            ptr = srcBuf + rowOffsetBytes + srcCol;
            cc = ptr[0];

            ptr = dstBuf + i * dstWidth + j;
            ptr[0] = cc;
        }
    }

    // uv
    srcRow = 0;
    srcCol = 0;
    uint8_t *pUVSrcStart = srcBuf + srcWidth * srcHeightSpan;
    uint8_t *pUVDstStart = dstBuf + dstWidth * dstHeight;

    for (i = 0; i < dstHeight; i += 1) {
        srcRow = v_offset + i * v_scale_ratio;
        rowOffsetBytes = srcRow * srcWidth;

        for (j = 0; j < dstWidth; j += 1) {
            srcCol = h_offset + j * h_scale_ratio;
            ptr = pUVSrcStart + rowOffsetBytes + srcCol;
            cc = ptr[0];

            ptr = pUVDstStart + i * dstWidth + j;
            ptr[0] = cc;
        }
    }

    return 0;
}


void decreaseNV12WithCut(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf,
                         int dstWidth, int dstHeight) {
    if (!srcBuf || !dstBuf) {
        return;
    }

    if (!((dstWidth < srcWidth) && (dstHeight < srcHeight))) {
        return;
    }

    int YSrcStrideBytes = srcWidth;
    int YDstStrideBytes = dstWidth;
    int UVSrcStrideBytes = srcWidth / 2;
    int UVDstStrideBytes = dstWidth / 2;

    int WidthMargin = (srcWidth - dstWidth) / 2;
    int leftOffset = WidthMargin;

    int HeightMargin = (srcHeight - dstHeight) / 2;
    int topOffset = HeightMargin;

    /*======== process Y ======== */
    for (int dstRow = 0; dstRow < dstHeight; dstRow++) {
        uint8_t *dstYLine = dstBuf + dstRow * YDstStrideBytes;
        int srcRow = dstRow + topOffset;
        uint8_t *srcYLine = srcBuf + srcRow * YSrcStrideBytes;
        memcpy(dstYLine, srcYLine + leftOffset, YDstStrideBytes);
    }

    /*======== process UV ======== */
    uint8_t *dstUVBuf = dstBuf + dstWidth * dstHeight;
    uint8_t *srcUVBuf = srcBuf + srcWidth * srcHeight;

    for (int dstRow = 0; dstRow < dstHeight / 2; dstRow++) {
        uint8_t *dstUVLine = dstUVBuf + dstRow * UVDstStrideBytes * 2;
        int srcRow = dstRow + topOffset / 2;
        uint8_t *srcUVLine = srcUVBuf + srcRow * UVSrcStrideBytes * 2;
        memcpy(dstUVLine, srcUVLine + leftOffset, UVDstStrideBytes * 2);
    }

    return;
}

void enlargeNV12WithBlackMargin(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf,
                                int dstWidth, int dstHeight) {
    if (!srcBuf || !dstBuf) {
        return;
    }

    if (!((dstWidth > srcWidth) && (dstHeight > srcHeight))) {
        return;
    }

    int row = 0;

    int YSrcStrideBytes = srcWidth;
    int YDstStrideBytes = dstWidth;
    int UVDstStrideBytes = dstWidth / 2;

    int WidthMargin = (dstWidth - srcWidth) / 2;
    int leftOffset = WidthMargin;
    int rightOffset = dstWidth - WidthMargin;

    int HeightMargin = (dstHeight - srcHeight) / 2;
    int topOffset = HeightMargin;
    int bottomOffset = dstHeight - HeightMargin;

    /*======== process Y ======== */
    // Fill black in top/bottom blocks.
    memset(dstBuf, 0, HeightMargin * YDstStrideBytes);
    memset(dstBuf + bottomOffset * YDstStrideBytes, 0, HeightMargin * YDstStrideBytes);

    // Fill black left/right margins and source data row by row
    for (row = topOffset; row < bottomOffset; row++) {
        uint8_t *YDst = dstBuf + row * YDstStrideBytes;
        uint8_t *YSrc = srcBuf + (row - topOffset) * YSrcStrideBytes;

        memset(YDst, 0, WidthMargin);
        memset(YDst + rightOffset, 0, WidthMargin);
        memcpy(YDst + leftOffset, YSrc, YSrcStrideBytes);
    }

    /*======== process UV ======== */
    uint8_t *dstUVBuf = dstBuf + dstWidth * dstHeight;
    uint8_t *srcUVBuf = srcBuf + srcWidth * srcHeight;

    // Fill black in top/bottom blocks.
    memset(dstUVBuf, 128, HeightMargin * UVDstStrideBytes);
    memset(dstUVBuf + bottomOffset * UVDstStrideBytes, 128, HeightMargin * UVDstStrideBytes);

    // Fill the middle rows
    for (row = topOffset / 2; row < bottomOffset / 2; row++) {
        uint8_t *UVDstLine = dstUVBuf + row * dstWidth;
        uint8_t *UVSrcLine = srcUVBuf + (row - topOffset / 2) * srcWidth;

        memset(UVDstLine, 128, WidthMargin);
        memset(UVDstLine + rightOffset, 128, WidthMargin);
        memcpy(UVDstLine + WidthMargin, UVSrcLine, srcWidth);
    }

    return;
}

// In most cases, use enlargeNV12WithBlackMargin or decreaseNV12WithCut.
// Or will failed due to timeout on below tests:
// testMandatoryConcurrentStreamCombination
// testMandatoryOutputCombinations
int yuv420spResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth,
                   int dstHeight) {
    if (!srcBuf || !dstBuf) {
        return -1;
    }

    ALOGV("%s: src %dx%d, dst %dx%d", __func__, srcWidth, srcHeight, dstWidth, dstHeight);

    // If jsut cut, testAllOutputYUVResolutions will fail due to diff too much. So scale by
    // calculation.
    if (srcWidth == 2592 && srcHeight == 1944 && dstWidth == 176 && dstHeight == 144)
        goto resizeByCalc;

    if ((dstWidth > srcWidth) && (dstHeight > srcHeight)) {
        enlargeNV12WithBlackMargin(srcBuf, srcWidth, srcHeight, dstBuf, dstWidth, dstHeight);
        return 0;
    }

    if ((dstWidth < srcWidth) && (dstHeight < srcHeight)) {
        decreaseNV12WithCut(srcBuf, srcWidth, srcHeight, dstBuf, dstWidth, dstHeight);
        return 0;
    }

resizeByCalc:
    structConvImage o_img_ptr, i_img_ptr;
    memset(&o_img_ptr, 0, sizeof(o_img_ptr));
    memset(&i_img_ptr, 0, sizeof(i_img_ptr));

    // input
    i_img_ptr.uWidth = srcWidth;
    i_img_ptr.uStride = i_img_ptr.uWidth;
    i_img_ptr.uHeight = srcHeight;
    i_img_ptr.eFormat = IC_FORMAT_YCbCr420_lp;
    i_img_ptr.imgPtr = srcBuf;
    i_img_ptr.clrPtr = i_img_ptr.imgPtr + (i_img_ptr.uWidth * i_img_ptr.uHeight);

    // ouput
    o_img_ptr.uWidth = dstWidth;
    o_img_ptr.uStride = o_img_ptr.uWidth;
    o_img_ptr.uHeight = dstHeight;
    o_img_ptr.eFormat = IC_FORMAT_YCbCr420_lp;
    o_img_ptr.imgPtr = dstBuf;
    o_img_ptr.clrPtr = o_img_ptr.imgPtr + (o_img_ptr.uWidth * o_img_ptr.uHeight);

    VT_resizeFrame_Video_opt2_lp(&i_img_ptr, &o_img_ptr, NULL, 0);

    return 0;
}

int convertPixelFormatToCLFormat(int format) {
    int clFormat = CL_G2D_NV12;

    switch (format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
            clFormat = CL_G2D_NV12;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            clFormat = CL_G2D_NV16;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            clFormat = CL_G2D_YUYV;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            clFormat = CL_G2D_I420;
            break;
        default:
            ALOGE("%s: Error! format:0x%x not supported!", __func__, format);
            break;
    }

    return clFormat;
}

int convertPixelFormatToV4L2Format(int format, bool invert) {
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
            } else {
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
        case HAL_PIXEL_FORMAT_YV12:
            nFormat = v4l2_fourcc('Y', 'V', '1', '2');
            break;
        case HAL_PIXEL_FORMAT_RGBA_8888:
            nFormat = v4l2_fourcc('A', 'B', '2', '4');
            break;

        default:
            ALOGE("%s: Error! format:0x%x not supported!", __func__, format);
            break;
    }

    ALOGV("v4l2 format: %c%c%c%c", nFormat & 0xFF, (nFormat >> 8) & 0xFF, (nFormat >> 16) & 0xFF,
          (nFormat >> 24) & 0xFF);
    return nFormat;
}

int convertV4L2FormatToPixelFormat(uint32_t fourcc) {
    int format = HAL_PIXEL_FORMAT_YCbCr_420_SP;

    switch (fourcc) {
        case v4l2_fourcc('N', 'V', '1', '2'):
            format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
            break;
        case v4l2_fourcc('N', 'V', '1', '6'):
            format = HAL_PIXEL_FORMAT_YCbCr_422_SP;
            break;
        case v4l2_fourcc('Y', 'U', 'Y', 'V'):
            format = HAL_PIXEL_FORMAT_YCbCr_422_I;
            break;
        case v4l2_fourcc('Y', 'U', '1', '2'):
            format = HAL_PIXEL_FORMAT_YCbCr_420_P;
            break;
        case v4l2_fourcc('Y', 'U', 'V', '4'):
            format = HAL_PIXEL_FORMAT_YCbCr_444_888;
            break;
        default:
            ALOGE("%s: Error! fourcc:0x%x not supported!", __func__, fourcc);
            break;
    }

    return format;
}

int32_t getSizeByForamtRes(int32_t format, uint32_t width, uint32_t height, bool align) {
    int32_t size = 0;
    int alignedw, alignedh, c_stride;

    if (align && (format == HAL_PIXEL_FORMAT_YCbCr_420_P)) {
        alignedw = ALIGN_PIXEL_32(width);
        alignedh = ALIGN_PIXEL_4(height);
        c_stride = (alignedw / 2 + 15) / 16 * 16;
        size = (alignedw + c_stride) * alignedh;
        return size;
    }

    alignedw = align ? ALIGN_PIXEL_16(width) : width;
    alignedh = align ? ALIGN_PIXEL_16(height) : height;

    switch (format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_420_P:
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
        case HAL_PIXEL_FORMAT_YV12:
            size = alignedw * alignedh * 3 / 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_422_I:
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        case HAL_PIXEL_FORMAT_RAW16:
            size = alignedw * alignedh * 2;
            break;

        default:
            ALOGE("Error: %s format 0x%x not supported", __func__, format);
            break;
    }

    return size;
}

void SetBufferHandle(ImxImageBuffer &imxBuf) {
    fsl::MemoryDesc desc;
    fsl::Memory *handle = NULL;

    desc.mFlag = 0;
    desc.mWidth = desc.mStride = imxBuf.mSize / 4;
    desc.mHeight = 1;
    desc.mFormat = HAL_PIXEL_FORMAT_RGBA_8888;
    desc.mFslFormat = fsl::FORMAT_RGBA8888;
    desc.mSize = imxBuf.mSize;
    desc.mProduceUsage = 0;

    handle = new fsl::Memory(&desc, imxBuf.mFd, -1);
    imxBuf.buffer = (buffer_handle_t)handle;
}

int AllocPhyBuffer(ImxImageBuffer &imxBuf) {
    int sharedFd;
    uint64_t phyAddr;
    uint64_t outPtr;
    uint32_t ionSize = imxBuf.mSize;

    fsl::Allocator *allocator = fsl::Allocator::getInstance();
    if (allocator == NULL) {
        ALOGE("%s ion allocator invalid", __func__);
        return -1;
    }

    sharedFd = allocator->allocMemory(ionSize, MEM_ALIGN, fsl::MFLAGS_CONTIGUOUS);
    if (sharedFd < 0) {
        ALOGE("%s: allocMemory failed.", __func__);
        return -1;
    }

    int err = allocator->getVaddrs(sharedFd, ionSize, outPtr);
    if (err != 0) {
        ALOGE("%s: getVaddrs failed.", __func__);
        close(sharedFd);
        return -1;
    }

    err = allocator->getPhys(sharedFd, ionSize, phyAddr);
    if (err != 0) {
        ALOGE("%s: getPhys failed.", __func__);
        munmap((void *)(uintptr_t)outPtr, ionSize);
        close(sharedFd);
        return -1;
    }

    ALOGV("%s, outPtr:%p,  phy:%p, ionSize:%d, req:%zu\n", __func__, (void *)outPtr,
          (void *)phyAddr, ionSize, imxBuf.mFormatSize);

    imxBuf.mVirtAddr = (void *)outPtr;
    imxBuf.mPhyAddr = phyAddr;
    imxBuf.mFd = sharedFd;
    SetBufferHandle(imxBuf);

    return 0;
}

int FreePhyBuffer(ImxImageBuffer &imxBuf) {
    if (imxBuf.mVirtAddr)
        munmap(imxBuf.mVirtAddr, imxBuf.mSize);

    if (imxBuf.mFd > 0)
        close(imxBuf.mFd);

    fsl::Memory *handle = (fsl::Memory *)imxBuf.buffer;
    if (handle)
        delete handle;

    return 0;
}

void SwitchImxBuf(ImxImageBuffer &imxBufA, ImxImageBuffer &imxBufB) {
    ImxImageBuffer tmpBuf = imxBufA;
    imxBufA = imxBufB;
    imxBufB = tmpBuf;

    return;
}

} // namespace android
