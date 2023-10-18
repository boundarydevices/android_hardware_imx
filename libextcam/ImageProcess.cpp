/*
 * Copyright 2021-2023 NXP.
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
// #define LOG_NDEBUG 0
#define LOG_TAG "ImageProcess"

#include "ImageProcess.h"

#include <cutils/log.h>
#include <cutils/properties.h>
#include <dlfcn.h>
#include <g2d.h>
#include <stdio.h>

#include "Composer.h"
#include "Memory.h"
#include "MemoryDesc.h"
#include "opencl-2d.h"

#if defined(__LP64__)
#define LIB_PATH1 "/system/lib64"
#define LIB_PATH2 "/vendor/lib64"
#else
#define LIB_PATH1 "/system/lib"
#define LIB_PATH2 "/vendor/lib"
#endif

#define CLENGINE "libg2d-opencl.so"
#define G2DENGINE "libg2d"

namespace fsl {

static int yuv422iResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth,
                   int dstHeight, bool srcFromHwDec = false);

static int yuv422spResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth,
                   int dstHeight, bool srcFromHwDec = false);

ImageProcess *ImageProcess::sInstance(0);
Mutex ImageProcess::sLock(Mutex::PRIVATE);

ImageProcess *ImageProcess::getInstance() {
    Mutex::Autolock _l(sLock);
    if (sInstance != NULL) {
        return sInstance;
    }

    sInstance = new ImageProcess();
    return sInstance;
}

static bool getDefaultG2DLib(char *libName, int size) {
    char value[PROPERTY_VALUE_MAX];

    if ((libName == NULL) || (size < (int)strlen(G2DENGINE) + (int)strlen(".so")))
        return false;

    memset(libName, 0, size);
    property_get("vendor.imx.default-g2d", value, "");
    if (strcmp(value, "") == 0) {
        ALOGI("No g2d lib available to be used!");
        return false;
    } else {
        strncpy(libName, G2DENGINE, strlen(G2DENGINE));
        strcat(libName, "-");
        strcat(libName, value);
        strcat(libName, ".so");
    }
    ALOGI("Default g2d lib: %s", libName);
    return true;
}

ImageProcess::ImageProcess() : mCLModule(NULL) {
    int32_t ret = -1;
    char path[PATH_MAX] = {0};

    memset(path, 0, sizeof(path));
    getModule(path, CLENGINE);
    mCLModule = dlopen(path, RTLD_NOW);
    if (mCLModule == NULL) {
        ALOGE("%s: No mCLModule to be used!\n", __func__);
        mCLOpen = NULL;
        mCLClose = NULL;
        mCLFlush = NULL;
        mCLFinish = NULL;
        mCLBlit = NULL;
        mCLCopy = NULL;
        mCLHandle = NULL;
    } else {
        mCLOpen = (hwc_func1)dlsym(mCLModule, "cl_g2d_open");
        mCLClose = (hwc_func1)dlsym(mCLModule, "cl_g2d_close");
        mCLFlush = (hwc_func1)dlsym(mCLModule, "cl_g2d_flush");
        mCLFinish = (hwc_func1)dlsym(mCLModule, "cl_g2d_finish");
        mCLBlit = (hwc_func3)dlsym(mCLModule, "cl_g2d_blit");
        mCLCopy = (hwc_func4)dlsym(mCLModule, "cl_g2d_copy");
        ret = (*mCLOpen)((void *)&mCLHandle);
        if (ret != 0) {
            mCLHandle = NULL;
        }
    }

    if (mCLHandle != NULL) {
        ALOGI("%s: opencl g2d device is used!\n", __func__);
    }

    char g2dlibName[PATH_MAX] = {0};
    if (getDefaultG2DLib(g2dlibName, PATH_MAX)) {
        getModule(path, g2dlibName);
        mG2dModule = dlopen(path, RTLD_NOW);
    }

    if (mG2dModule == NULL) {
        mOpenEngine = NULL;
        mCloseEngine = NULL;
        mFinishEngine = NULL;
        mCopyEngine = NULL;
        mBlitEngine = NULL;
        mG2dHandle = NULL;
    } else {
        mOpenEngine = (hwc_func1)dlsym(mG2dModule, "g2d_open");
        mCloseEngine = (hwc_func1)dlsym(mG2dModule, "g2d_close");
        mFinishEngine = (hwc_func1)dlsym(mG2dModule, "g2d_finish");
        mCopyEngine = (hwc_func4)dlsym(mG2dModule, "g2d_copy");
        mBlitEngine = (hwc_func3)dlsym(mG2dModule, "g2d_blit");
        ret = mOpenEngine(&mG2dHandle);
        if (ret != 0) {
            mG2dHandle = NULL;
        }
    }

    if (mG2dHandle != NULL) {
        ALOGI("%s: g2d device is used!\n", __func__);
    }
}

ImageProcess::~ImageProcess() {
    if (mCLHandle != NULL) {
        (*mCLClose)(mCLHandle);
    }

    if (mCLModule != NULL) {
        dlclose(mCLModule);
    }

    if (mG2dHandle != NULL) {
        mCloseEngine(mG2dHandle);
    }

    if (mG2dModule != NULL) {
        dlclose(mG2dModule);
    }

    sInstance = NULL;
}

void ImageProcess::getModule(char *path, const char *name) {
    snprintf(path, PATH_MAX, "%s/%s", LIB_PATH1, name);
    if (access(path, R_OK) == 0)
        return;
    snprintf(path, PATH_MAX, "%s/%s", LIB_PATH2, name);
    if (access(path, R_OK) == 0)
        return;
    return;
}

int ImageProcess::handleFrame(uint32_t width, uint32_t height, ImgFormat dst_fmt, ImgFormat src_fmt,
                              uint64_t dstPhyAddr, uint64_t srcPhyAddr, uint32_t srcWidth, uint32_t srcHeight,
                              void *srcVirtAddr, void *dstVirtAddr) {
    int ret = 0;

    ALOGV("%s: src_fmt %d, dst_fmt %d, src size %dx%d, dst size %dx%d, srcvirt %p, srcPhy %p, dstvirt %p, dstPhy %p",
        __func__, src_fmt, dst_fmt, srcWidth, srcHeight, width, height, srcVirtAddr, (void *)srcPhyAddr, dstVirtAddr, (void *)dstPhyAddr);

    if ((src_fmt == dst_fmt) && ((srcWidth != width) || (srcHeight != height))) {
        ret = resize(src_fmt, srcWidth, srcHeight, width, height, srcVirtAddr, srcPhyAddr, dstVirtAddr, dstPhyAddr);
        return ret;
    }

    if (!((dst_fmt == NV12) || (dst_fmt == I420))) {
        ALOGI("%s: unsupported dst_fmt %d", __func__, dst_fmt);
        return -1;
    }

    switch (src_fmt) {
        case NV12:
            ALOGV("%s: handle NV12 Frame\n", __func__);
            ret = handleNV12Frame(dstPhyAddr, srcPhyAddr, width, height, dst_fmt);
            break;
        case NV16:
            ALOGV("%s: handle NV16 Frame!\n", __func__);
            ret = handleNV16Frame(dstPhyAddr, srcPhyAddr, width, height, dst_fmt, srcWidth, srcHeight);
            break;
        case YUYV:
            ALOGV("%s: handle YUYV Frame!\n", __func__);
            ret = handleYUYVFrame(width, height, dstPhyAddr, srcPhyAddr, dst_fmt);
            break;
        default:
            ALOGE("%s: src_fmt cannot be handled!\n", __func__);
            return -EINVAL;
    }

    return ret;
}

int ImageProcess::CPUResize(ImgFormat fmt, void *dstVirtAddr, void *srcVirtAddr,
    uint32_t dstWidth, uint32_t dstHeight, uint32_t srcWidth, uint32_t srcHeight) {
    int ret = 0;

    if (fmt == NV16)
        ret = yuv422spResize((uint8_t *)srcVirtAddr, srcWidth, srcHeight, (uint8_t *)dstVirtAddr, dstWidth, dstHeight, true);
    else if (fmt == YUYV)
        ret = yuv422iResize((uint8_t *)srcVirtAddr, srcWidth, srcHeight, (uint8_t *)dstVirtAddr, dstWidth, dstHeight, true);
    else {
        ret = -EINVAL;
        ALOGE("%s: unsupported format %d", __func__, fmt);
    }

    return ret;
}

int ImageProcess::resize(ImgFormat fmt, uint32_t srcWidth, uint32_t srcHeight, uint32_t dstWidth, uint32_t dstHeight,
  void *srcVirtAddr, uint64_t srcPhyAddr, void *dstVirtAddr, uint64_t dstPhyAddr) {
    int ret = 0;

    if (mG2dHandle)
        ret = handleYUYVFrameByG2D(dstPhyAddr, srcPhyAddr, dstWidth, dstHeight, fmt, fmt, srcWidth, srcHeight, true);
    else
       ret = CPUResize(fmt, dstVirtAddr, srcVirtAddr, dstWidth, dstHeight, srcWidth, srcHeight);

    return ret;
}


int ImageProcess::handleNV12Frame(uint64_t dstPhyAddr, uint64_t srcPhyAddr, uint32_t width,
                                  uint32_t height, ImgFormat dst_fmt) {
    if (mCLHandle == NULL) {
        ALOGE("%s: mCLHandle is NULL!\n", __func__);
        return -EINVAL;
    }

    // Fix me! The g3d input buffer is HWDecoder output buffer, it's uncached, ref
    // HwDecoder::allocateOutputBuffer().
    // The g3d output buffer is mYu12Frame, it's cached, ref AllocatedFramePhyMem::allocate().
    // Currently, the GPU only support using physical address for uncached memory.
    // Otherwise the physical address will be taken as virtual one, leading crash.
    // After GPU fix the issue, will set cacheable to false for g3d output buffer.
    // The trick applies in other places useing opencl code.

    if (dst_fmt == NV12) {
        cl_Copy(mCLHandle, srcPhyAddr, dstPhyAddr, width * height * 3 / 2, false, false);
        return 0;
    }

    // diffrent format, same resolution
    {
        Mutex::Autolock _l(mCLLock);
        cl_NV12toI420(mCLHandle, srcPhyAddr, dstPhyAddr, width, height, false, false);
        (*mCLFlush)(mCLHandle);
        (*mCLFinish)(mCLHandle);
    }

    return 0;
}

void ImageProcess::cl_Copy(void *g2dHandle, uint64_t srcPhyAddr, uint64_t dstPhyAddr, uint32_t size,
                           bool bInputCached, bool bOutputCached) {
    struct cl_g2d_buf g2d_output_buf;
    struct cl_g2d_buf g2d_input_buf;

    g2d_output_buf.buf_paddr = dstPhyAddr;
    g2d_output_buf.buf_size = size;
    g2d_output_buf.use_phy = true;
    g2d_output_buf.usage = bOutputCached ? CL_G2D_CACHED_MEMORY : CL_G2D_UNCACHED_MEMORY;

    g2d_input_buf.buf_paddr = srcPhyAddr;
    g2d_input_buf.buf_size = size;
    g2d_input_buf.use_phy = true;
    g2d_input_buf.usage = bInputCached ? CL_G2D_CACHED_MEMORY : CL_G2D_UNCACHED_MEMORY;

    (*mCLCopy)(g2dHandle, &g2d_output_buf, &g2d_input_buf, (void *)(intptr_t)size);
}

void ImageProcess::cl_NV12toI420(void *g2dHandle, uint64_t srcPhyAddr, uint64_t dstPhyAddr,
                                 int width, int height, bool bInputCached, bool bOutputCached) {
    struct cl_g2d_surface src, dst;

    src.format = CL_G2D_NV12;
    src.usage = bInputCached ? CL_G2D_CACHED_MEMORY : CL_G2D_UNCACHED_MEMORY;
    src.planes[0] = (long)srcPhyAddr;
    src.planes[1] = (long)srcPhyAddr + width * height;
    src.left = 0;
    src.top = 0;
    src.right = width;
    src.bottom = height;
    src.stride = width;
    src.width = width;
    src.height = height;
    src.usePhyAddr = true;

    dst.format = CL_G2D_I420;
    dst.usage = bOutputCached ? CL_G2D_CACHED_MEMORY : CL_G2D_UNCACHED_MEMORY;
    dst.planes[0] = (long)dstPhyAddr;
    dst.planes[1] = (long)dstPhyAddr + width * height;
    dst.planes[2] = (long)dstPhyAddr + width * height * 5 / 4;
    dst.left = 0;
    dst.top = 0;
    dst.right = width;
    dst.bottom = height;
    dst.stride = width;
    dst.width = width;
    dst.height = height;
    dst.usePhyAddr = true;

    (*mCLBlit)(g2dHandle, (void *)&src, (void *)&dst);
}

int ImageProcess::handleNV16Frame(uint64_t dstPhyAddr, uint64_t srcPhyAddr, uint32_t width,
                                  uint32_t height, ImgFormat dst_fmt, uint32_t srcWidth, uint32_t srcHeight) {
    if (mCLHandle == NULL) {
        ALOGE("%s: mCLHandle is NULL!\n", __func__);
        return -EINVAL;
    }

    Mutex::Autolock _l(mCLLock);

    int ret = cl_NV16Src(mCLHandle, srcPhyAddr, dstPhyAddr, width, height, false, false, dst_fmt, srcWidth, srcHeight);

    (*mCLFlush)(mCLHandle);
    (*mCLFinish)(mCLHandle);

    return ret;
}

int ImageProcess::cl_NV16Src(void *g2dHandle, uint64_t srcPhyAddr, uint64_t dstPhyAddr, int width,
                             int height, bool bInputCached, bool bOutputCached, ImgFormat dst_fmt, int srcWidth, int srcHeight) {
    if (!((dst_fmt == NV12) || (dst_fmt == I420))) {
        ALOGE("%s: unsupported dst_fmt %d", __func__, dst_fmt);
        return -1;
    }

    struct cl_g2d_surface src, dst;

    src.format = CL_G2D_NV16;
    src.usage = bInputCached ? CL_G2D_CACHED_MEMORY : CL_G2D_UNCACHED_MEMORY;
    src.planes[0] = (long)srcPhyAddr;
    src.planes[1] = (long)srcPhyAddr + srcWidth * srcHeight;
    src.left = 0;
    src.top = 0;
    src.right = width;
    src.bottom = height;
    src.stride = width;
    src.width = width;
    src.height = height;
    src.usePhyAddr = true;

    dst.format = (dst_fmt == I420) ? CL_G2D_I420 : CL_G2D_NV12;
    dst.usage = bOutputCached ? CL_G2D_CACHED_MEMORY : CL_G2D_UNCACHED_MEMORY;
    dst.planes[0] = (long)dstPhyAddr;
    dst.planes[1] = (long)dstPhyAddr + width * height;
    dst.planes[2] = (long)dstPhyAddr + width * height * 5 / 4;
    dst.left = 0;
    dst.top = 0;
    dst.right = width;
    dst.bottom = height;
    dst.stride = width;
    dst.width = width;
    dst.height = height;
    dst.usePhyAddr = true;

    (*mCLBlit)(g2dHandle, (void *)&src, (void *)&dst);

    return 0;
}

int ImageProcess::handleYUYVFrameByG3D(uint64_t dstPhyAddr, uint64_t srcPhyAddr, uint32_t width,
                                       uint32_t height, ImgFormat dst_fmt) {
    if (mCLHandle == NULL) {
        ALOGE("%s: mCLHandle is NULL!\n", __func__);
        return -EINVAL;
    }

    // diffrent format, same resolution
    {
        Mutex::Autolock _l(mCLLock);
        cl_YUYVtoI420(mCLHandle, srcPhyAddr, dstPhyAddr, width, height, false, false);
        (*mCLFlush)(mCLHandle);
        (*mCLFinish)(mCLHandle);
    }

    return 0;
}

int ImageProcess::handleYUYVFrame(uint32_t width, uint32_t height, uint64_t dstPhyAddr,
                                  uint64_t srcPhyAddr, ImgFormat dst_fmt) {
    if (mG2dHandle)
        return handleYUYVFrameByG2D(dstPhyAddr, srcPhyAddr, width, height, dst_fmt, YUYV, width, height, true);
    else
        return handleYUYVFrameByG3D(dstPhyAddr, srcPhyAddr, width, height, dst_fmt);
}

void ImageProcess::cl_YUYVtoI420(void *g2dHandle, uint64_t srcPhyAddr, uint64_t dstPhyAddr,
                                 int width, int height, bool bInputCached, bool bOutputCached) {
    struct cl_g2d_surface src, dst;

    src.format = CL_G2D_YUYV;
    src.usage = bInputCached ? CL_G2D_CACHED_MEMORY : CL_G2D_UNCACHED_MEMORY;
    src.planes[0] = (long)srcPhyAddr;
    src.left = 0;
    src.top = 0;
    src.right = width;
    src.bottom = height;
    src.stride = width;
    src.width = width;
    src.height = height;
    src.usePhyAddr = true;

    dst.format = CL_G2D_I420;
    dst.usage = bOutputCached ? CL_G2D_CACHED_MEMORY : CL_G2D_UNCACHED_MEMORY;
    dst.planes[0] = (long)dstPhyAddr;
    dst.planes[1] = (long)dstPhyAddr + width * height;
    dst.planes[2] = (long)dstPhyAddr + width * height * 5 / 4;
    dst.left = 0;
    dst.top = 0;
    dst.right = width;
    dst.bottom = height;
    dst.stride = width;
    dst.width = width;
    dst.height = height;
    dst.usePhyAddr = true;

    (*mCLBlit)(g2dHandle, (void *)&src, (void *)&dst);
}

int ImageProcess::G2DFmtFromFslFmt(ImgFormat fslFmt) {
    enum g2d_format g2dFmt = G2D_NV12;

    switch (fslFmt) {
        case I420:
          g2dFmt = G2D_I420;
          break;
        case NV12:
          g2dFmt = G2D_NV12;
          break;
        case NV16:
          g2dFmt = G2D_NV16;
          break;
        case YUYV:
          g2dFmt = G2D_YUYV;
          break;
        default:
          ALOGW("%s: unsupported fsl format %d, use default g2d format %d", __func__, fslFmt, g2dFmt);
          break;
    }

    return g2dFmt;
}

int ImageProcess::handleYUYVFrameByG2D(uint64_t dstPhyAddr, uint64_t srcPhyAddr, uint32_t width,
                                       uint32_t height, ImgFormat dst_fmt, ImgFormat src_fmt, uint32_t srcWidth, uint32_t srcHeight, bool srcFromHwDec) {
    if (mBlitEngine == NULL) {
        return -EINVAL;
    }

    ALOGV("%s: src: fmt 0x%x, size %dx%d, dst: fmt 0x%x, size %dx%d",
        __func__, src_fmt, srcWidth, srcHeight, dst_fmt, width, height);

    uint32_t orgSrcHeight = srcHeight;
    if (srcFromHwDec) {
        if (srcHeight == 1088)
            srcHeight = 1080;
        else if (srcHeight == 608)
            srcHeight = 600;
    }

    int ret;
    void *g2dHandle = mG2dHandle;
    struct g2d_surface s_surface, d_surface;

    enum g2d_format g2dFmtSrc = (enum g2d_format)G2DFmtFromFslFmt(src_fmt);
    enum g2d_format g2dFmtDst = (enum g2d_format)G2DFmtFromFslFmt(dst_fmt);

    s_surface.format = g2dFmtSrc;
    // set 3 planes to cover YUYV/NV16/NV12/I420
    s_surface.planes[0] = (long)srcPhyAddr;
    s_surface.planes[1] = s_surface.planes[0] + srcWidth * orgSrcHeight;
    s_surface.planes[2] = s_surface.planes[1] + srcWidth * orgSrcHeight / 4;
    s_surface.left = 0;
    s_surface.top = 0;
    s_surface.right = srcWidth;
    s_surface.bottom = srcHeight;
    s_surface.stride = srcWidth;
    s_surface.width = srcWidth;
    s_surface.height = srcHeight;
    s_surface.rot = G2D_ROTATION_0;

    d_surface.format = g2dFmtDst;
    d_surface.planes[0] = (long)dstPhyAddr;
    d_surface.planes[1] = d_surface.planes[0] + width * height;
    d_surface.planes[2] = d_surface.planes[1] + width * height / 4;
    d_surface.left = 0;
    d_surface.top = 0;
    d_surface.right = width;
    d_surface.bottom = height;
    d_surface.stride = width;
    d_surface.width = width;
    d_surface.height = height;
    d_surface.rot = G2D_ROTATION_0;

    Mutex::Autolock _l(mG2dLock);
    ret = mBlitEngine(g2dHandle, (void *)&s_surface, (void *)&d_surface);
    if (ret)
        goto finish_blit;

    mFinishEngine(g2dHandle);

finish_blit:
    return ret;
}

fsl::ImgFormat ImageProcess::FslFmtFromFourcc(uint32_t fourcc) {
    fsl::ImgFormat format = NV12;

    if (fourcc == V4L2_PIX_FMT_NV12)
        format = NV12;
    else if (fourcc == V4L2_PIX_FMT_NV16)
        format = NV16;
    else if (fourcc == V4L2_PIX_FMT_YUV420)
        format = I420;
    else if (fourcc == V4L2_PIX_FMT_YUYV)
        format = YUYV;

    return format;
}

static int yuv422iResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth,
                  int dstHeight, bool srcFromHwDec) {
    int i, j;
    int h_offset;
    int v_offset;
    unsigned char *ptr, cc;
    int h_scale_ratio;
    int v_scale_ratio;

    int srcStride;
    int dstStride;

    if (srcFromHwDec) {
        if (srcHeight == 1088)
            srcHeight = 1080;
        else if (srcHeight == 608)
            srcHeight = 600;
    }

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


static int yuv422spResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth,
                   int dstHeight, bool srcFromHwDec) {
    int i, j, s;
    int h_offset;
    int v_offset;
    unsigned char *ptr, cc;
    int h_scale_ratio;
    int v_scale_ratio;
    int OrgSrcHeight = srcHeight;

    s = 0;

    if (srcFromHwDec) {
        if (srcHeight == 1088)
            srcHeight = 1080;
        else if (srcHeight == 608)
            srcHeight = 600;
    }

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
    uint8_t *pUVSrcStart = srcBuf + srcWidth * OrgSrcHeight; // use OrgSrcHeight (as 1088) to omit invalid 8 lines
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

} // namespace fsl
