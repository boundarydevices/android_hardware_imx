/*
 *  Copyright 2023-2024 NXP.
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

#include "ImageUtils.h"

#include <graphics.h>
#include <hardware/gralloc.h>
#include <linux/dma-buf-imx.h>
#include <log/log.h>
#include <stdint.h>
#include <string.h>
#include <ui/GraphicBufferAllocator.h>
#include <ui/GraphicBufferMapper.h>
#include <ui/Rect.h>

#define ALIGN_PIXEL_4(x) ((x + 3) & ~3)
#define ALIGN_PIXEL_16(x) ((x + 15) & ~15)
#define ALIGN_PIXEL_32(x) ((x + 31) & ~31)

#define ENGINE_GPU_2D "GPU_2D"
#define ENGINE_GPU_3D "GPU_3D"
#define ENGINE_OCLCVT "OCLCVT"
#define ENGINE_IPU "IPU"
#define ENGINE_PXP "PXP"
#define ENGINE_DPU "DPU"
#define ENGINE_CPU "CPU"
#define ENGINE_BYPASS "BYPASS"

namespace android {

ImxEngine ValueToImxEngine(const std::string &value) {
    ImxEngine engine = ENG_G2D;

    if (value == ENGINE_DPU) {
        engine = ENG_DPU;
    } else if (value == ENGINE_GPU_2D) {
        engine = ENG_G2D;
    } else if (value == ENGINE_GPU_3D) {
        engine = ENG_G3D;
    } else if (value == ENGINE_OCLCVT) {
        engine = ENG_OCLCVT;
    } else if (value == ENGINE_PXP) {
        engine = ENG_PXP;
    } else if (value == ENGINE_IPU) {
        engine = ENG_IPU;
    } else if (value == ENGINE_CPU) {
        engine = ENG_CPU;
    } else if (value == ENGINE_BYPASS) {
        engine = ENG_BYPASS;
    }

    return engine;
}

int yuv422iResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth,
                  int dstHeight) {
    int i, j;
    int h_offset;
    int v_offset;
    int h_scale_ratio;
    int v_scale_ratio;
    int srcStride = srcWidth * 2;
    int dstStride = dstWidth * 2;
    int srcRow = 0;
    int srcCol = 0;
    uint32_t *pYUYVSrcStart = NULL;
    uint32_t *pYUYVDstStart = NULL;
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
    ALOGV("h_scale_ratio %d, v_scale_ratio %d, h_offset %d, v_offset %d", h_scale_ratio,
          v_scale_ratio, h_offset, v_offset);

    // handle 4 bytes each time
    pYUYVSrcStart = (uint32_t *)srcBuf;
    pYUYVDstStart = (uint32_t *)dstBuf;
    // 2 pixels, fill the buff
    for (i = 0; i < dstHeight; i += 1) {
        for (j = 0; j < dstWidth; j += 2) {
            srcRow = v_offset + i * v_scale_ratio;
            srcCol = h_offset + j * h_scale_ratio;
            pYUYVDstStart[dstStride * i / 4 + j / 2] =
                    pYUYVSrcStart[srcStride * srcRow / 4 + srcCol / 2];
        }
    }

    return 0;

enlarge:
    h_scale_ratio = dstWidth / srcWidth;
    v_scale_ratio = dstHeight / srcHeight;

    h_offset = (dstWidth - srcWidth * h_scale_ratio) / 2;
    v_offset = (dstHeight - srcHeight * v_scale_ratio) / 2;

    int h_offset_end = h_offset + srcWidth * h_scale_ratio;
    int v_offset_end = v_offset + srcHeight * v_scale_ratio;

    ALOGV("h_scale_ratio %d, v_scale_ratio %d, h_offset %d, v_offset %d, h_offset_end %d, "
          "v_offset_end %d",
          h_scale_ratio, v_scale_ratio, h_offset, v_offset, h_offset_end, v_offset_end);

    // handle 4 bytes each time
    pYUYVSrcStart = (uint32_t *)srcBuf;
    pYUYVDstStart = (uint32_t *)dstBuf;
    for (i = 0; i < dstHeight; i++) {
        // top, bottom black margin
        if ((i < v_offset) || (i >= v_offset_end)) {
            for (j = 0; j < dstWidth; j++) {
                dstBuf[dstStride * i + j * 2] = 0;       // y
                dstBuf[dstStride * i + j * 2 + 1] = 128; // uv
            }
            continue;
        }

        for (j = 0; j < dstWidth; j++) {
            // left, right black margin
            if ((j < h_offset) || (j >= h_offset_end)) {
                dstBuf[dstStride * i + j * 2] = 0;       // y
                dstBuf[dstStride * i + j * 2 + 1] = 128; // uv
                continue;
            }
            // 2 pixels, fill the buff
            if (j % 2 == 0) {
                srcRow = (i - v_offset) / v_scale_ratio;
                srcCol = (j - h_offset) / h_scale_ratio;
                pYUYVDstStart[dstStride * i / 4 + j / 2] =
                        pYUYVSrcStart[srcStride * srcRow / 4 + srcCol / 2];
            }
        }
    }

    return 0;
}

int yuv422spResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth,
                   int dstHeight, int dstStride, int srcHeightSpan) {
    int i, j, s;
    int h_offset;
    int v_offset;
    unsigned char *ptr, cc;
    int h_scale_ratio;
    int v_scale_ratio;
    int srcStride = srcWidth;
    int srcRow = 0;
    int srcCol = 0;
    uint16_t *pUVSrcStart = NULL;
    uint16_t *pUVDstStart = NULL;

    if (srcHeightSpan == 0)
        srcHeightSpan = srcHeight;

    if (!srcWidth || !srcHeightSpan || !dstWidth || !dstHeight)
        return -1;

    h_scale_ratio = srcWidth / dstWidth;
    v_scale_ratio = srcHeightSpan / dstHeight;

    if ((h_scale_ratio > 0) && (v_scale_ratio > 0))
        goto reduce;
    else if (h_scale_ratio + v_scale_ratio <= 1)
        goto enlarge;

    ALOGE("%s, not support resize %dx%d to %dx%d", __func__, srcWidth, srcHeight, dstWidth,
          dstHeight);

    return -1;

reduce:
    h_offset = (srcWidth - dstWidth * h_scale_ratio) / 2;
    v_offset = (srcHeightSpan - dstHeight * v_scale_ratio) / 2;
    ALOGV("h_scale_ratio %d, v_scale_ratio %d, h_offset %d, v_offset %d", h_scale_ratio,
          v_scale_ratio, h_offset, v_offset);

    // y
    for (i = 0; i < dstHeight; i += 1) {
        for (j = 0; j < dstWidth; j += 1) {
            srcRow = v_offset + i * v_scale_ratio;
            srcCol = h_offset + j * h_scale_ratio;

            ptr = srcBuf + srcRow * srcStride + srcCol;
            cc = ptr[0];

            ptr = dstBuf + i * dstStride + j;
            ptr[0] = cc;
        }
    }

    // uv
    pUVSrcStart = (uint16_t *)(srcBuf + srcStride * srcHeightSpan);
    pUVDstStart = (uint16_t *)(dstBuf + dstStride * dstHeight);
    uint16_t *pUV, uvVal;

    for (i = 0; i < dstHeight; i += 1) {
        srcRow = v_offset + i * v_scale_ratio;
        for (j = 0; j < dstWidth; j += 2) {
            srcCol = h_offset + j * h_scale_ratio;
            pUV = pUVSrcStart + (srcRow * srcStride) / 2 + srcCol / 2;
            uvVal = pUV[0];

            pUV = pUVDstStart + i * dstStride / 2 + j / 2;
            pUV[0] = uvVal;
        }
    }

    return 0;

enlarge:
    h_scale_ratio = dstWidth / srcWidth;
    v_scale_ratio = dstHeight / srcHeightSpan;
    h_offset = (dstWidth - srcWidth * h_scale_ratio) / 2;
    v_offset = (dstHeight - srcHeightSpan * v_scale_ratio) / 2;
    int h_offset_end = h_offset + srcWidth * h_scale_ratio;
    int v_offset_end = v_offset + srcHeightSpan * v_scale_ratio;
    ALOGV("h_scale_ratio %d, v_scale_ratio %d, h_offset %d, v_offset %d, h_offset_end %d, v_offset_end %d",
          h_scale_ratio, v_scale_ratio, h_offset, v_offset, h_offset_end, v_offset_end);

    // y
    for (i = 0; i < dstHeight; i++) {
        // top, bottom black margin
        if ((i < v_offset) || (i >= v_offset_end)) {
            for (j = 0; j < dstWidth; j++) {
                dstBuf[dstStride * i + j] = 0;
            }
            continue;
        }

        for (j = 0; j < dstWidth; j++) {
            // left, right black margin
            if ((j < h_offset) || (j >= h_offset_end)) {
                dstBuf[dstStride * i + j] = 0;
                continue;
            }

            srcRow = (i - v_offset) / v_scale_ratio;
            srcCol = (j - h_offset) / h_scale_ratio;
            dstBuf[dstStride * i + j] = srcBuf[srcStride * srcRow + srcCol];
        }
    }

    pUVSrcStart = (uint16_t *)(srcBuf + srcStride * srcHeightSpan);
    pUVDstStart = (uint16_t *)(dstBuf + dstStride * dstHeight);
    uint8_t *pUVDstStartEdge = (dstBuf + dstStride * dstHeight);
    // uv
    for (i = 0; i < dstHeight; i++) {
        // top, bottom black margin
        if ((i < v_offset) || (i >= v_offset_end)) {
            for (j = 0; j < dstWidth; j++) {
                pUVDstStartEdge[dstStride * i + j] = 128;
            }
            continue;
        }

        for (j = 0; j < dstWidth; j++) {
            // left, right black margin
            if ((j < h_offset) || (j >= h_offset_end)) {
                pUVDstStartEdge[dstStride * i + j] = 128;
                continue;
            }

            srcRow = (i - v_offset) / v_scale_ratio;
            srcCol = (j - h_offset) / h_scale_ratio;
            pUVDstStart[dstStride * i / 2 + j / 2] =
                    pUVSrcStart[srcStride * srcRow / 2 + srcCol / 2];
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
    int UVSrcStrideBytes = srcWidth;
    int UVDstStrideBytes = dstWidth;

    int WidthMargin = (srcWidth - dstWidth) / 2;
    int leftOffset = WidthMargin;

    int HeightMargin = (srcHeight - dstHeight) / 2;
    int topOffset = HeightMargin;

    // For NV12 format data, 4 Y corresponds to 1 set of UV components.
    // When the Cut image starts from an odd lines in width,
    // little difference in Y components between adjacent pixels,
    // but for UV, value will start from the V component, which will cause color inversion.
    // To workaround, always cut from even lines.
    if (leftOffset % 2 != 0) {
        leftOffset = leftOffset - 1;
    }

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

    for (int dstRow = 0; dstRow < dstHeight; dstRow += 2) {
        uint8_t *dstUVLine = dstUVBuf + dstRow / 2 * UVDstStrideBytes;
        int srcRow = (dstRow + topOffset) / 2;
        uint8_t *srcUVLine = srcUVBuf + srcRow * UVSrcStrideBytes;
        memcpy(dstUVLine, srcUVLine + leftOffset, UVDstStrideBytes);
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
    int UVSrcStrideBytes = srcWidth;
    int UVDstStrideBytes = dstWidth;

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
    memset(dstUVBuf, 128, HeightMargin / 2 * UVDstStrideBytes);
    memset(dstUVBuf + bottomOffset / 2 * UVDstStrideBytes, 128,
           HeightMargin / 2 * UVDstStrideBytes);

    // Fill the middle rows
    for (row = topOffset; row < bottomOffset; row += 2) {
        uint8_t *UVDstLine = dstUVBuf + row / 2 * UVDstStrideBytes;
        uint8_t *UVSrcLine = srcUVBuf + (row - topOffset) / 2 * UVSrcStrideBytes;

        memset(UVDstLine, 128, WidthMargin);
        memset(UVDstLine + rightOffset, 128, WidthMargin);
        memcpy(UVDstLine + WidthMargin, UVSrcLine, UVSrcStrideBytes);
    }

    return;
}

int yuv420spResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth,
                   int dstHeight) {
    int i, j;
    int h_offset;
    int v_offset;
    uint8_t *ptr, cc;
    int h_scale_ratio;
    int v_scale_ratio;
    int srcStride = srcWidth;
    int dstStride = dstWidth;
    int srcRow = 0;
    int srcCol = 0;
    uint16_t *pUVSrcStart = NULL;
    uint16_t *pUVDstStart = NULL;

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
    ALOGV("h_scale_ratio %d, v_scale_ratio %d, h_offset %d, v_offset %d", h_scale_ratio,
          v_scale_ratio, h_offset, v_offset);

    // y
    for (i = 0; i < dstHeight; i += 1) {
        for (j = 0; j < dstWidth; j += 1) {
            srcRow = v_offset + i * v_scale_ratio;
            srcCol = h_offset + j * h_scale_ratio;

            ptr = srcBuf + srcRow * srcStride + srcCol;
            cc = ptr[0];

            ptr = dstBuf + i * dstStride + j;
            ptr[0] = cc;
        }
    }

    // uv
    pUVSrcStart = (uint16_t *)(srcBuf + srcStride * srcHeight);
    pUVDstStart = (uint16_t *)(dstBuf + dstStride * dstHeight);
    uint16_t *pUV, uvVal;

    for (i = 0; i < dstHeight; i += 2) {
        srcRow = v_offset + i * v_scale_ratio;
        for (j = 0; j < dstWidth; j += 2) {
            srcCol = h_offset + j * h_scale_ratio;
            pUV = pUVSrcStart + (srcRow * srcStride) / 2 / 2 + srcCol / 2;
            uvVal = pUV[0];

            pUV = pUVDstStart + i / 2 * dstStride / 2 + j / 2;
            pUV[0] = uvVal;
        }
    }

    return 0;

enlarge:
    h_scale_ratio = dstWidth / srcWidth;
    v_scale_ratio = dstHeight / srcHeight;
    h_offset = (dstWidth - srcWidth * h_scale_ratio) / 2;
    v_offset = (dstHeight - srcHeight * v_scale_ratio) / 2;
    int h_offset_end = h_offset + srcWidth * h_scale_ratio;
    int v_offset_end = v_offset + srcHeight * v_scale_ratio;
    ALOGV("h_scale_ratio %d, v_scale_ratio %d, h_offset %d, v_offset %d, h_offset_end %d, v_offset_end %d",
          h_scale_ratio, v_scale_ratio, h_offset, v_offset, h_offset_end, v_offset_end);

    // y
    for (i = 0; i < dstHeight; i++) {
        // top, bottom black margin
        if ((i < v_offset) || (i >= v_offset_end)) {
            for (j = 0; j < dstWidth; j++) {
                dstBuf[dstStride * i + j] = 0;
            }
            continue;
        }

        for (j = 0; j < dstWidth; j++) {
            // left, right black margin
            if ((j < h_offset) || (j >= h_offset_end)) {
                dstBuf[dstStride * i + j] = 0;
                continue;
            }

            srcRow = (i - v_offset) / v_scale_ratio;
            srcCol = (j - h_offset) / h_scale_ratio;
            dstBuf[dstStride * i + j] = srcBuf[srcStride * srcRow + srcCol];
        }
    }

    pUVSrcStart = (uint16_t *)(srcBuf + srcStride * srcHeight);
    pUVDstStart = (uint16_t *)(dstBuf + dstStride * dstHeight);
    uint8_t *pUVDstStartEdge = (dstBuf + dstStride * dstHeight);
    // uv
    for (i = 0; i < dstHeight; i += 2) {
        // top, bottom black margin
        if ((i < v_offset) || (i >= v_offset_end)) {
            for (j = 0; j < dstWidth; j++) {
                pUVDstStartEdge[dstStride * (i / 2) + j] = 128;
            }
            continue;
        }

        for (j = 0; j < dstWidth; j++) {
            // left, right black margin
            if ((j < h_offset) || (j >= h_offset_end)) {
                pUVDstStartEdge[dstStride * (i / 2) + j] = 128;
                continue;
            }

            srcRow = (i - v_offset) / v_scale_ratio;
            srcCol = (j - h_offset) / h_scale_ratio;
            pUVDstStart[dstStride * (i / 2) / 2 + j / 2] =
                    pUVSrcStart[srcStride * (srcRow / 2) / 2 + srcCol / 2];
        }
    }

    return 0;
}

void Revert16BitEndian(uint8_t *pSrc, uint8_t *pDst, uint32_t pixels) {
    ALOGI("enter Revert16BitEndian, src %p, dst %p, pixels %d", pSrc, pDst, pixels);
    for (uint32_t i = 0; i < pixels; i++) {
        uint32_t offset = i * 2;
        uint8_t temp = pSrc[offset];
        pDst[offset] = pSrc[offset + 1];
        pDst[offset + 1] = temp;
    }

    return;
}

// 8mp support 10-bit Bayer BGBG/GRGR 0x30314742, takes up 16 bit of storage
void SbggrToRgb888(const uint16_t *src, uint8_t *rgb, int width, int height) {
    if (src == NULL) {
        ALOGE("%s: Error! rgb == NULL", __func__);
        return;
    }
    if (rgb == NULL) {
        ALOGE("%s: Error! yuv422i == NULL", __func__);
        return;
    }

    int src_width = width + 2;
    int src_height = height + 2;

    uint16_t *src_data = (uint16_t *)malloc(src_width * src_height * 2);
    if (src_data == NULL) {
        ALOGE("%s: src_data is null, memory allocation failed!", __func__);
        return;
    }

    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            src_data[src_width * (i + 1) + 1 + j] = src[width * i + j];
        }
    }

    // fill 2 rows, the first and the last
    for (int i = 0; i < width; i++) {
        src_data[1 + i] = src_data[src_width * 2 + 1 + i];
        src_data[src_width * (src_height - 1) + 1 + i] =
                src_data[src_width * (src_height - 3) + 1 + i];
    }

    // fill 2 columns, the first and the last
    for (int i = 0; i < src_height; i++) {
        src_data[i * src_width + 0] = src_data[i * src_width + 2];
        src_data[i * src_width + src_width - 1] = src_data[i * src_width + src_width - 3];
    }

    int dataIndex = 0;
    for (int i = 0; i < height; i++) {
        for (int j = 0; j < width; j++) {
            // 10bit value -> 8bit value, otherwise rgb888 would be much brighter than bg10
            uint16_t index_1 = src_data[i * src_width + j] >> 2;
            uint16_t index_2 = src_data[i * src_width + j + 1] >> 2;
            uint16_t index_3 = src_data[i * src_width + j + 2] >> 2;
            uint16_t index_4 = src_data[(i + 1) * src_width + j] >> 2;
            uint16_t index_5 = src_data[(i + 1) * src_width + j + 1] >> 2;
            uint16_t index_6 = src_data[(i + 1) * src_width + j + 2] >> 2;
            uint16_t index_7 = src_data[(i + 2) * src_width + j] >> 2;
            uint16_t index_8 = src_data[(i + 2) * src_width + j + 1] >> 2;
            uint16_t index_9 = src_data[(i + 2) * src_width + j + 2] >> 2;

            uint8_t index_r = 0;
            uint8_t index_g = 0;
            uint8_t index_b = 0;
            /* bggr */
            if ((i % 2 == 0) && (j % 2 == 0)) { // B
                // r
                index_r = (index_1 + index_3 + index_7 + index_9) / 4;
                // g
                index_g = (index_2 + index_4 + index_6 + index_8) / 4;
                // b
                index_b = index_5;
            }
            if ((i % 2 == 0) && (j % 2 != 0)) { // g
                // r
                index_r = (index_2 + index_8) / 2;
                // g
                index_g = index_5;
                // b
                index_b = (index_4 + index_6) / 2;
            }
            if ((i % 2 != 0) && (j % 2 == 0)) { // g
                // r
                index_r = (index_4 + index_6) / 2;
                // g
                index_g = index_5;
                // b
                index_b = (index_2 + index_8) / 2;
            }
            if ((i % 2 != 0) && (j % 2 != 0)) { // r
                // r
                index_r = index_5;
                // g
                index_g = (index_2 + index_4 + index_6 + index_8) / 4;
                // b
                index_b = (index_1 + index_3 + index_7 + index_9) / 4;
            }

            rgb[dataIndex] = index_r;
            rgb[dataIndex + 1] = index_g;
            rgb[dataIndex + 2] = index_b;
            dataIndex += 3;
        }
    }

    free(src_data);
}

#define RGB2YUV(r, g, b, y, u, v)                  \
    y = (77 * r + 150 * g + 29 * b) >> 8;          \
    u = ((128 * b - 43 * r - 85 * g) >> 8) + 128;  \
    v = ((128 * r - 107 * g - 21 * b) >> 8) + 128; \
    y = y < 0 ? 0 : y;                             \
    u = u < 0 ? 0 : u;                             \
    v = v < 0 ? 0 : v;                             \
    y = y > 255 ? 255 : y;                         \
    u = u > 255 ? 255 : u;                         \
    v = v > 255 ? 255 : v

void Rgb888ToYuv422i(const uint8_t *rgb, uint8_t *yuv422i, int width, int height) {
    if (rgb == NULL) {
        ALOGE("%s: Error! rgb == NULL", __func__);
        return;
    }
    if (yuv422i == NULL) {
        ALOGE("%s: Error! yuv422i == NULL", __func__);
        return;
    }

    if (((width & 0x1) != 0) || ((height & 0x1) != 0)) {
        ALOGE("%s: width and height must be multiple of 2", __func__);
        return;
    }

    uint16_t i, j;
    uint8_t r, g, b;
    int y, u, v;

    for (i = 0; i < height; i++) {
        for (j = 0; j < width; j++) {
            r = *rgb++;
            g = *rgb++;
            b = *rgb++;
            RGB2YUV(b, g, r, y, u, v);
            if (j & 0x1) {
                *yuv422i++ = y;
                *yuv422i++ = u;
            } else {
                *yuv422i++ = y;
                *yuv422i++ = v;
            }
        }
    }

    return;
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

int AllocPhyBuffer(uint32_t width, uint32_t height, uint32_t format, ImxImageBuffer &outBufInfo,
                   bool bCached) {
    buffer_handle_t bufferHandle;
    uint32_t bufferStride;
    uint64_t usage = GRALLOC_USAGE_HW_CAMERA_WRITE | GRALLOC_USAGE_PRIVATE_3; // need to make sure physical contiguous memory

    if (bCached)
        usage |= GRALLOC_USAGE_SW_READ_OFTEN;

    auto status = GraphicBufferAllocator::get().allocate(width, height, format,
                                                         /*layerCount=*/1, usage, &bufferHandle,
                                                         &bufferStride, "NxpCamera");
    if (status != ::android::OK) {
        ALOGE("%s: failed to allocate buffer:%d x %d, format=%x, usage=%lx, ret=%d", __func__,
              width, height, format, usage, status);
        ;
        return BAD_VALUE;
    }

    void *vaddr = NULL;
    const ::android::Rect rect{0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height)};
    auto err = GraphicBufferMapper::get().lock(const_cast<native_handle_t *>(bufferHandle), usage,
                                               rect, &vaddr);
    if (err) {
        ALOGE("%s: GraphicBufferMapper lock failed!", __FUNCTION__);
        ::android::GraphicBufferMapper::get().unlock(bufferHandle);
        GraphicBufferAllocator::get().free(bufferHandle);
        return BAD_VALUE;
    }

    uint64_t allocatedSize;
    err = GraphicBufferMapper::get().getAllocationSize(const_cast<native_handle_t *>(bufferHandle),
                                                       &allocatedSize);
    if (err) {
        ALOGE("%s: GraphicBufferMapper getAllocationSize failed!", __FUNCTION__);
        GraphicBufferAllocator::get().free(bufferHandle);
        return BAD_VALUE;
    }

    int sharedFd = bufferHandle->data[0];
    uint64_t phyAddr = GetPhyAddrFromBuffer(sharedFd);
    ALOGV("%s, vaddr:%p,  phy:%p, size:%lu\n", __func__, vaddr, (void *)phyAddr, allocatedSize);

    outBufInfo.mFormat = format;
    outBufInfo.mWidth = width;
    outBufInfo.mHeight = height;
    outBufInfo.mVirtAddr = vaddr;
    outBufInfo.mPhyAddr = phyAddr;
    outBufInfo.mFd = sharedFd;
    outBufInfo.buffer = bufferHandle;
    outBufInfo.mSize = allocatedSize;
    outBufInfo.mStride = bufferStride;
    outBufInfo.mUsage = usage;

    return 0;
}

int FreePhyBuffer(buffer_handle_t buffer) {
    if (buffer == NULL) {
        ALOGE("%s: buffer NULL", __FUNCTION__);
        return BAD_VALUE;
    }

    auto err = ::android::GraphicBufferMapper::get().unlock(buffer);
    if (err) {
        ALOGE("%s: GraphicBufferMapper unlock failed!", __FUNCTION__);
        return BAD_VALUE;
    }

    GraphicBufferAllocator::get().free(buffer);

    return 0;
}

uint64_t GetPhyAddrFromBuffer(int bufFd) {
    uint64_t phyAddr = 0;
    struct dmabuf_imx_phys_data data;
    int fd_;
    fd_ = open("/dev/dmabuf_imx", O_RDONLY | O_CLOEXEC);
    if (fd_ < 0) {
        ALOGE("open /dev/dmabuf_imx failed: %s", strerror(errno));
        return 0;
    }
    data.dmafd = bufFd;
    if (ioctl(fd_, DMABUF_GET_PHYS, &data) < 0) {
        ALOGE("%s DMABUF_GET_PHYS  failed", __func__);
        close(fd_);
        return 0;
    } else {
        phyAddr = data.phys;
    }
    close(fd_);

    return phyAddr;
}

int UnlockPhyBuffer(buffer_handle_t buffer) {
    auto err = ::android::GraphicBufferMapper::get().unlock(buffer);
    if (err) {
        ALOGE("%s: GraphicBufferMapper unlock failed!", __FUNCTION__);
        return -1;
    }

    return 0;
}

int GetBufferInfoFromHandle(buffer_handle_t bufferHandle, ImxImageBuffer &outBufInfo) {
    GraphicBufferMapper &mapper = GraphicBufferMapper::getInstance();

    uint64_t width, height, usage;
    auto err = mapper.getWidth(const_cast<native_handle_t *>(bufferHandle), &width);
    if (err) {
        ALOGE("%s: GraphicBufferMapper getWidth failed!", __FUNCTION__);
        return BAD_VALUE;
    }
    err = mapper.getHeight(const_cast<native_handle_t *>(bufferHandle), &height);
    if (err) {
        ALOGE("%s: GraphicBufferMapper getHeight failed!", __FUNCTION__);
        return BAD_VALUE;
    }
    err = mapper.getUsage(const_cast<native_handle_t *>(bufferHandle), &usage);
    if (err) {
        ALOGE("%s: GraphicBufferMapper getUsage failed!", __FUNCTION__);
        return BAD_VALUE;
    }

    uint32_t format;
    err = mapper.getPixelFormatRequested(const_cast<native_handle_t *>(bufferHandle),
                                         reinterpret_cast<ui::PixelFormat *>(&format));
    if (err) {
        ALOGE("%s: GraphicBufferMapper getPixelFormatRequested failed!", __FUNCTION__);
        return BAD_VALUE;
    }

    void *vaddr = NULL;
    const ::android::Rect rect{0, 0, static_cast<int32_t>(width), static_cast<int32_t>(height)};
    err = mapper.lock(const_cast<native_handle_t *>(bufferHandle), usage, rect, &vaddr);
    if (err) {
        ALOGE("%s: GraphicBufferMapper lock failed!", __FUNCTION__);
        return BAD_VALUE;
    }

    uint64_t allocatedSize;
    err = mapper.getAllocationSize(const_cast<native_handle_t *>(bufferHandle), &allocatedSize);
    if (err) {
        ALOGE("%s: GraphicBufferMapper getAllocationSize failed!", __FUNCTION__);
        return BAD_VALUE;
    }

    int sharedFd = bufferHandle->data[0];
    uint64_t phyAddr = GetPhyAddrFromBuffer(sharedFd);
    ALOGV("%s: vaddr:%p,  phy:%p, size:%lu\n", __func__, vaddr, (void *)phyAddr, allocatedSize);

    outBufInfo.mFormat = format;
    outBufInfo.mWidth = (uint32_t)width;
    outBufInfo.mHeight = (uint32_t)height;
    outBufInfo.mVirtAddr = vaddr;
    outBufInfo.mPhyAddr = phyAddr;
    outBufInfo.mFd = sharedFd;
    outBufInfo.buffer = bufferHandle;
    outBufInfo.mSize = allocatedSize;

    return 0;
}

void SwitchImxBuf(ImxImageBuffer &imxBufA, ImxImageBuffer &imxBufB) {
    ImxImageBuffer tmpBuf = imxBufA;
    imxBufA = imxBufB;
    imxBufB = tmpBuf;

    return;
}

int GetAllocationSize(buffer_handle_t handle, uint64_t &allocatedSize) {
    GraphicBufferMapper &mapper = GraphicBufferMapper::getInstance();
    int err = mapper.getAllocationSize(handle, &allocatedSize);
    if (err) {
        ALOGE("%s: GraphicBufferMapper getAllocationSize failed!", __FUNCTION__);
        return BAD_VALUE;
    }

    return 0;
}

int GetUsage(buffer_handle_t handle, uint64_t &usage) {
    GraphicBufferMapper &mapper = GraphicBufferMapper::getInstance();
    int err = mapper.getUsage(handle, &usage);
    if (err) {
        ALOGE("%s: GraphicBufferMapper getUsage failed!", __FUNCTION__);
        return BAD_VALUE;
    }

    return 0;
}

} // namespace android
