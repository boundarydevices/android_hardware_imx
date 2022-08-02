/*
 * Copyright 2018-2020 NXP.
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
#define LOG_TAG "ImageProcess"

#include <stdio.h>
#include <dlfcn.h>
#include <cutils/log.h>

#include "CameraUtils.h"
#include "ImageProcess.h"
#include "Composer.h"
#include "Memory.h"
#include "MemoryDesc.h"
#include "VideoStream.h"

#include <g2d.h>
#include <linux/ipu.h>
#include "opencl-2d.h"

using namespace cameraconfigparser;
extern "C" {
#include <linux/pxp_device.h>
}

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

ImageProcess* ImageProcess::sInstance(0);
Mutex ImageProcess::sLock(Mutex::PRIVATE);

static void Revert16BitEndianAndShift(uint8_t *pSrc, uint8_t *pDst, uint32_t pixels, int32_t v4l2Format);

static bool IsCscSupportByCPU(int srcFormat, int dstFormat)
{
    // yuyv -> nv12
    if ( ((dstFormat == HAL_PIXEL_FORMAT_YCbCr_420_888) ||
          (dstFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP)) &&
         (srcFormat == HAL_PIXEL_FORMAT_YCbCr_422_I) )
        return true;

    // nv12 -> nv21
    if ( (srcFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP) &&
         (dstFormat == HAL_PIXEL_FORMAT_YCrCb_420_SP) )
        return true;

    return false;
}

static bool IsCscSupportByG3D(int srcFomat, int dstFormat)
{
    // yuyv -> nv12
    if ( ((dstFormat == HAL_PIXEL_FORMAT_YCbCr_420_888) ||
          (dstFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP)) &&
         (srcFomat == HAL_PIXEL_FORMAT_YCbCr_422_I) )
        return true;

    return false;
}


static bool getDefaultG2DLib(char *libName, int size)
{
    char value[PROPERTY_VALUE_MAX];

    if((libName == NULL)||(size < (int)strlen(G2DENGINE) + (int)strlen(".so")))
        return false;

    memset(libName, 0, size);
    property_get("vendor.imx.default-g2d", value, "");
    if(strcmp(value, "") == 0) {
        ALOGI("No g2d lib available to be used!");
        return false;
    }
    else {
        strncpy(libName, G2DENGINE, strlen(G2DENGINE));
        strcat(libName, "-");
        strcat(libName, value);
        strcat(libName, ".so");
    }
    ALOGI("Default g2d lib: %s", libName);
    return true;
}

ImageProcess* ImageProcess::getInstance()
{
    Mutex::Autolock _l(sLock);
    if (sInstance != NULL) {
        return sInstance;
    }

    sInstance = new ImageProcess();
    return sInstance;
}

ImageProcess::ImageProcess()
    : mIpuFd(-1), mPxpFd(-1), mChannel(-1), mG2dModule(NULL), mCLModule(NULL)
{
    /*
     * imx6dl support IPU device and PXP device.
     * imx6q and imx6qp support IPU device.
     * imx6sx and imx6sl support PXP device.
     * IPU can't handle NV21 format, so open PXP on some platform to handle it.
     */
    mIpuFd = open("/dev/mxc_ipu", O_RDWR, 0);
    mPxpFd = open("/dev/pxp_device", O_RDWR, 0);

    //When open pxp device, need allocate a channel at the same time.
    int32_t ret = -1;
    if (mPxpFd > 0) {
        ret = ioctl(mPxpFd, PXP_IOC_GET_CHAN, &mChannel);
        if (ret < 0) {
            ALOGE("%s:%d, PXP_IOC_GET_CHAN failed %d", __func__, __LINE__ ,ret);
            close(mPxpFd);
            mPxpFd = -1;
        }
    }
    ALOGI("mChannel %d", mChannel);

    char path[PATH_MAX] = {0};
    char g2dlibName[PATH_MAX] = {0};
    if(getDefaultG2DLib(g2dlibName, PATH_MAX)){
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
    }
    else {
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

    memset(path, 0, sizeof(path));
    getModule(path, CLENGINE);
    mCLModule = dlopen(path, RTLD_NOW);
    if (mCLModule == NULL) {
        mCLOpen = NULL;
        mCLClose = NULL;
        mCLFlush = NULL;
        mCLFinish = NULL;
        mCLBlit = NULL;
        mCLHandle = NULL;
    }
    else {
        mCLOpen = (hwc_func1)dlsym(mCLModule, "cl_g2d_open");
        mCLClose = (hwc_func1)dlsym(mCLModule, "cl_g2d_close");
        mCLFlush = (hwc_func1)dlsym(mCLModule, "cl_g2d_flush");
        mCLFinish = (hwc_func1)dlsym(mCLModule, "cl_g2d_finish");
        mCLBlit = (hwc_func3)dlsym(mCLModule, "cl_g2d_blit");
        mCLCopy = (hwc_func4)dlsym(mCLModule, "cl_g2d_copy");
        ret = (*mCLOpen)((void*)&mCLHandle);
        if (ret != 0) {
            mCLHandle = NULL;
        }
    }

    if(mCLHandle != NULL) {
        ALOGW("opencl g2d device is used!\n");
    }
}

ImageProcess::~ImageProcess()
{
    if (mIpuFd > 0) {
        close(mIpuFd);
        mIpuFd = -1;
    }

    if (mPxpFd > 0) {
        close(mPxpFd);
        mPxpFd = -1;
    }

    if (mCLHandle != NULL) {
        (*mCLClose)(mCLHandle);
    }

    if (mCLModule != NULL) {
        dlclose(mCLModule);
    }

    if (mG2dHandle != NULL) {
        closeEngine(mG2dHandle);
    }

    if (mG2dModule != NULL) {
        dlclose(mG2dModule);
    }


    sInstance = NULL;
}

void *ImageProcess::getHandle()
{
    return mG2dHandle;
}

int ImageProcess::openEngine(void** handle)
{
    if (mOpenEngine == NULL) {
        return -EINVAL;
    }

    return (*mOpenEngine)((void*)handle);
}

int ImageProcess::closeEngine(void* handle)
{
    if (mCloseEngine == NULL) {
        return -EINVAL;
    }

    return (*mCloseEngine)(handle);
}

void ImageProcess::getModule(char *path, const char *name)
{
    snprintf(path, PATH_MAX, "%s/%s",
                                 LIB_PATH1, name);
    if (access(path, R_OK) == 0)
        return;
    snprintf(path, PATH_MAX, "%s/%s",
                                 LIB_PATH2, name);
    if (access(path, R_OK) == 0)
        return;
    return;
}

int ImageProcess::handleFrame(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf, CscHw hw_type)
{
    int ret = 0;

    if (srcBuf.mStream == NULL || dstBuf.mStream == NULL) {
        return -EINVAL;
    }

    ALOGV("ImageProcess::handleFrame, src: virt %p, phy 0x%llx, size %d, res %dx%d, format 0x%x, dst: virt %p, phy 0x%llx, size %d, res %dx%d, format 0x%x",
        srcBuf.mVirtAddr, srcBuf.mPhyAddr, srcBuf.mSize, srcBuf.mStream->width(), srcBuf.mStream->height(), srcBuf.mStream->format(),
        dstBuf.mVirtAddr, dstBuf.mPhyAddr, dstBuf.mSize, dstBuf.mStream->width(), dstBuf.mStream->height(), dstBuf.mStream->format());

    // unify HAL_PIXEL_FORMAT_YCbCr_420_SP to HAL_PIXEL_FORMAT_YCBCR_420_888
    if (srcBuf.mStream->format() == HAL_PIXEL_FORMAT_YCbCr_420_SP) {
        srcBuf.mStream->mFormat = HAL_PIXEL_FORMAT_YCBCR_420_888;
    }

    if (dstBuf.mStream->format() == HAL_PIXEL_FORMAT_YCbCr_420_SP) {
        dstBuf.mStream->mFormat = HAL_PIXEL_FORMAT_YCBCR_420_888;
    }

    switch (hw_type) {
    case GPU_2D:
        ret = handleFrameByGPU_2D(dstBuf, srcBuf);
        break;
    case GPU_3D:
        ret = handleFrameByGPU_3D(dstBuf, srcBuf);
        break;
    case DPU:
        ret = handleFrameByDPU(dstBuf, srcBuf);
        break;
    case PXP:
        ret = handleFrameByPXP(dstBuf, srcBuf);
        break;
    case IPU:
        ret = handleFrameByIPU(dstBuf, srcBuf);
        break;
    case CPU:
        ret = handleFrameByCPU(dstBuf, srcBuf);
        break;
    default:
        ALOGE("hw_type is not correct");
        return -EINVAL;
    }

    return ret;
}

int convertPixelFormatToG2DFormat(PixelFormat format)
{
    int nFormat = 0;

    switch (format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            nFormat = G2D_NV12;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            nFormat = G2D_YUYV;
            break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            nFormat = G2D_NV21;
            break;
        default:
            ALOGE("%s:%d, Error: format:0x%x not supported!", __func__, __LINE__, format);
            break;
    }
    return nFormat;
}

int ImageProcess::handleFrameByPXP(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf)
{
    ALOGV("%s", __func__);
    // pxp exists.
    if (mPxpFd <= 0) {
        return -EINVAL;
    }

    ImxStream *src = srcBuf.mStream;
    ImxStream *dst = dstBuf.mStream;
    struct pxp_config_data pxp_conf;
    struct pxp_layer_param *src_param = NULL, *out_param = NULL;
    int32_t ret = -1;


    memset(&pxp_conf, 0, sizeof(struct pxp_config_data));

    src_param = &(pxp_conf.s0_param);
    out_param = &(pxp_conf.out_param);

    /*
    * Initialize src parameters
    */
    src_param->paddr = srcBuf.mPhyAddr;

    src_param->width = src->width();
    src_param->height = src->height();
    src_param->color_key = -1;
    src_param->color_key_enable = 0;
    src_param->pixel_fmt = convertPixelFormatToV4L2Format(src->format());
    src_param->stride = src_param->width;
    pxp_conf.proc_data.srect.top = 0;
    pxp_conf.proc_data.srect.left = 0;
    pxp_conf.proc_data.srect.width = src->width();
    pxp_conf.proc_data.srect.height = src->height();

    /*
    * Initialize out parameters
    */
    out_param->paddr = dstBuf.mPhyAddr;
    out_param->width = dst->width();
    out_param->height = dst->height();
    out_param->pixel_fmt = convertPixelFormatToV4L2Format(dst->format());
    out_param->stride = out_param->width;
    pxp_conf.handle = mChannel;
    pxp_conf.proc_data.drect.top = 0;
    pxp_conf.proc_data.drect.left = 0;
    pxp_conf.proc_data.drect.width = dst->width();
    pxp_conf.proc_data.drect.height = dst->height();

    if((src_param->stride == 0) || (out_param->stride == 0)) {
        ALOGE("%s:%d, src stride %d, dst stride %d", __func__, __LINE__, src_param->stride, out_param->stride);
        return -EINVAL;
    }

    // The fb dirver treat r as bit[0:7], but PXP convert r to bit[24:31].
    // For preview, do some trick to set format as G2D_YVYU.
    if((src_param->pixel_fmt == PXP_PIX_FMT_YUYV) && (out_param->pixel_fmt == PXP_PIX_FMT_RGBA32)) {
        src_param->pixel_fmt = PXP_PIX_FMT_YVYU;
        out_param->pixel_fmt = PXP_PIX_FMT_ARGB32;
    }

    ALOGV("src: %dx%d, 0x%x, phy 0x%x, v4l2 0x%x, stride %d",
      src->width(), src->height(), src->format(), srcBuf.mPhyAddr, src_param->pixel_fmt, src_param->stride);
    ALOGV("dst: %dx%d, 0x%x, phy 0x%x, v4l2 0x%x, stride %d",
      dst->width(), dst->height(), dst->format(), dstBuf.mPhyAddr, out_param->pixel_fmt, out_param->stride);

    ret = ioctl(mPxpFd, PXP_IOC_CONFIG_CHAN, &pxp_conf);
    if(ret < 0) {
        ALOGE("%s:%d, PXP_IOC_CONFIG_CHAN failed %d", __func__, __LINE__ ,ret);
        return ret;
    }

    ret = ioctl(mPxpFd, PXP_IOC_START_CHAN, &(pxp_conf.handle));
    if(ret < 0) {
        ALOGE("%s:%d, PXP_IOC_START_CHAN failed %d", __func__, __LINE__ ,ret);
        return ret;
    }

    ret = ioctl(mPxpFd, PXP_IOC_WAIT4CMPLT, &pxp_conf);
    if(ret < 0) {
        ALOGE("%s:%d, PXP_IOC_WAIT4CMPLT failed %d", __func__, __LINE__ ,ret);
    }

    return ret;
}

int ImageProcess::handleFrameByIPU(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf)
{
    ALOGV("%s", __func__);
    if (mIpuFd <= 0) {
        return -EINVAL;
    }

    ImxStream *src = srcBuf.mStream;
    ImxStream *dst = dstBuf.mStream;
    if (dst->format() == HAL_PIXEL_FORMAT_YCrCb_420_SP) {
        return -EINVAL;
    }

    struct ipu_task mTask;
    memset(&mTask, 0, sizeof(mTask));

    mTask.input.width = src->width();
    mTask.input.height = src->height();
    mTask.input.crop.pos.x = 0;
    mTask.input.crop.pos.y = 0;
    mTask.input.crop.w = src->width();
    mTask.input.crop.h = src->height();
    mTask.input.format = convertPixelFormatToV4L2Format(src->format());
    mTask.input.paddr = srcBuf.mPhyAddr;

    mTask.output.format = convertPixelFormatToV4L2Format(dst->format());
    mTask.output.width = dst->width();
    mTask.output.height = dst->height();
    mTask.output.crop.pos.x = 0;
    mTask.output.crop.pos.y = 0;
    mTask.output.crop.w = dst->width();
    mTask.output.crop.h = dst->height();
    mTask.output.rotate = 0;
    mTask.output.paddr = dstBuf.mPhyAddr;

    int32_t ret = IPU_CHECK_ERR_INPUT_CROP;
    while(ret != IPU_CHECK_OK && ret > IPU_CHECK_ERR_MIN) {
        ret = ioctl(mIpuFd, IPU_CHECK_TASK, &mTask);
        ALOGV("%s:%d, IPU_CHECK_TASK ret=%d", __func__, __LINE__, ret);
        switch(ret) {
            case IPU_CHECK_OK:
                break;
            case IPU_CHECK_ERR_SPLIT_INPUTW_OVER:
                mTask.input.crop.w -= 8;
                break;
            case IPU_CHECK_ERR_SPLIT_INPUTH_OVER:
                mTask.input.crop.h -= 8;
                break;
            case IPU_CHECK_ERR_SPLIT_OUTPUTW_OVER:
                mTask.output.crop.w -= 8;
                break;
            case IPU_CHECK_ERR_SPLIT_OUTPUTH_OVER:
                mTask.output.crop.h -= 8;;
                break;
            default:
                ALOGE("%s:%d, IPU_CHECK_TASK ret=%d", __func__, __LINE__, ret);
                return ret;
        }
    }

    ret = ioctl(mIpuFd, IPU_QUEUE_TASK, &mTask);
    if(ret < 0) {
        ALOGE("%s:%d, IPU_QUEUE_TASK failed %d", __func__, __LINE__ ,ret);
    }

    return ret;
}

int ImageProcess::handleFrameByG2DCopy(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf)
{
    // gpu 2d exists.
    if (mCopyEngine == NULL) {
        return -EINVAL;
    }

    ImxStream *src = srcBuf.mStream;
    ImxStream *dst = dstBuf.mStream;

    int dstFormat = convertPixelFormatToV4L2Format(dst->format());
    int srcFormat = convertPixelFormatToV4L2Format(src->format());
    // can't do csc for YUV.
    if ((dst->format() != src->format()) &&
        (dstFormat != srcFormat)) {
        return -EINVAL;
    }

    void* g2dHandle = getHandle();
    int size = (srcBuf.mSize > dstBuf.mSize) ? dstBuf.mSize : srcBuf.mSize;

    struct g2d_buf s_buf, d_buf;
    s_buf.buf_paddr = srcBuf.mPhyAddr;
    s_buf.buf_vaddr = srcBuf.mVirtAddr;
    d_buf.buf_paddr = dstBuf.mPhyAddr;
    d_buf.buf_vaddr = dstBuf.mVirtAddr;

    Mutex::Autolock _l(mG2dLock);
    int ret = mCopyEngine(g2dHandle, (void*)&d_buf, (void*)&s_buf,
                 (void*)(intptr_t)size);
    if (ret == 0) {
        mFinishEngine(g2dHandle);
    }

    return ret;
}

int ImageProcess::handleFrameByG2DBlit(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf)
{
    if (mBlitEngine == NULL) {
        return -EINVAL;
    }

    ImxStream *src = srcBuf.mStream;
    ImxStream *dst = dstBuf.mStream;
    ImxStreamBuffer resizeBuf = {0};

    // can't do csc for some formats.
    if (!(((dst->format() == HAL_PIXEL_FORMAT_YCbCr_420_888) ||
         (dst->format() == HAL_PIXEL_FORMAT_YCbCr_420_SP) ||
         ((dst->format() == HAL_PIXEL_FORMAT_YCrCb_420_SP) &&
         (src->format() == HAL_PIXEL_FORMAT_YCbCr_422_I)) ||
         ((src->format() == HAL_PIXEL_FORMAT_YCbCr_422_I) &&
         (dst->format() == HAL_PIXEL_FORMAT_YCbCr_422_I))))) {
        return -EINVAL;
    }

    // Adapt for Camra2.apk. The picture resolution may differ from preview resolution.
    // If resize for preview stream, there will be obvious changes in the preview.
    if ( ((src->width() != dst->width()) || (src->height() != dst->height())) && dst->isPreview() ) {
        ALOGW("%s: resize from %dx%d to %dx%d, skip preview stream",
            __func__, src->width(), src->height(), dst->width(), dst->height());
        return 0;
    }

    int ret;
    void* g2dHandle = getHandle();
    struct g2d_buf s_buf, d_buf;
    struct g2d_surface s_surface, d_surface;

    s_buf.buf_paddr = srcBuf.mPhyAddr;
    s_buf.buf_vaddr = srcBuf.mVirtAddr;
    d_buf.buf_paddr = dstBuf.mPhyAddr;
    d_buf.buf_vaddr = dstBuf.mVirtAddr;

    // zoom feature
    uint32_t crop_left = 0;
    uint32_t crop_top = 0;
    uint32_t crop_width = src->width();
    uint32_t crop_height =  src->height();

    // currently, just suppport zoom in.
    if (src->mZoomRatio > 1.0) {
        crop_width = src->width()/src->mZoomRatio;
        crop_height = src->height()/src->mZoomRatio;
        crop_left = (src->width() - crop_width)/2;
        crop_top = (src->height() - crop_height)/2;
    }

    s_surface.format = (g2d_format)convertPixelFormatToG2DFormat(src->format());
    s_surface.planes[0] = (long)s_buf.buf_paddr;
    s_surface.left = crop_left;
    s_surface.top = crop_top;
    s_surface.right = crop_left + crop_width;
    s_surface.bottom = crop_top + crop_height;
    s_surface.stride = src->width();
    s_surface.width  = src->width();
    s_surface.height = src->height();
    s_surface.rot    = G2D_ROTATION_0;

    ALOGV("%s: crop from (%d, %d), size %dx%d, srcBuf.mFormatSize %d, mZoomRatio %f",
        __func__, crop_left, crop_top, crop_width, crop_height, srcBuf.mFormatSize, src->mZoomRatio);

    if ((src->format() == dst->format()) || (src->mZoomRatio <= 1.0)) { // just scale or just csc
        d_surface.format = (g2d_format)convertPixelFormatToG2DFormat(dst->format());
        d_surface.planes[0] = (long)d_buf.buf_paddr;
        d_surface.planes[1] = (long)d_buf.buf_paddr + dst->width() * dst->height();
        d_surface.left = 0;
        d_surface.top = 0;
        d_surface.right = dst->width();
        d_surface.bottom = dst->height();
        d_surface.stride = dst->width();
        d_surface.width  = dst->width();
        d_surface.height = dst->height();
        d_surface.rot    = G2D_ROTATION_0;

        Mutex::Autolock _l(mG2dLock);
        ret = mBlitEngine(g2dHandle, (void*)&s_surface, (void*)&d_surface);
        if (ret)
            goto finish_blit;

        mFinishEngine(g2dHandle);
    } else {
        struct g2d_surface tmp_surface;

        resizeBuf.mFormatSize = srcBuf.mFormatSize;
        resizeBuf.mSize = (resizeBuf.mFormatSize + PAGE_SIZE) & (~(PAGE_SIZE - 1));
        ret = AllocPhyBuffer(resizeBuf);
        if (ret) {
            ALOGE("%s:%d AllocPhyBuffer failed", __func__, __LINE__);
            return BAD_VALUE;
        }

        // first scale on same format as source
        tmp_surface.format = (g2d_format)convertPixelFormatToG2DFormat(src->format());
        tmp_surface.planes[0] = (long)resizeBuf.mPhyAddr;
        tmp_surface.planes[1] = (long)resizeBuf.mPhyAddr + dst->width() * dst->height();
        tmp_surface.left = 0;
        tmp_surface.top = 0;
        tmp_surface.right = dst->width();
        tmp_surface.bottom = dst->height();
        tmp_surface.stride = dst->width();
        tmp_surface.width  = dst->width();
        tmp_surface.height = dst->height();
        tmp_surface.rot    = G2D_ROTATION_0;

        Mutex::Autolock _l(mG2dLock);
        ret = mBlitEngine(g2dHandle, (void*)&s_surface, (void*)&tmp_surface);
        if (ret)
            goto finish_blit;

        mFinishEngine(g2dHandle);

        // then csc to dst format
        d_surface.format = (g2d_format)convertPixelFormatToG2DFormat(dst->format());
        d_surface.planes[0] = (long)d_buf.buf_paddr;
        d_surface.planes[1] = (long)d_buf.buf_paddr + dst->width() * dst->height();
        d_surface.left = 0;
        d_surface.top = 0;
        d_surface.right = dst->width();
        d_surface.bottom = dst->height();
        d_surface.stride = dst->width();
        d_surface.width  = dst->width();
        d_surface.height = dst->height();
        d_surface.rot    = G2D_ROTATION_0;

        ret = mBlitEngine(g2dHandle, (void*)&tmp_surface, (void*)&d_surface);
        if (ret)
            goto finish_blit;

        mFinishEngine(g2dHandle);
    }

finish_blit:
    FreePhyBuffer(resizeBuf);
    return ret;
}

static void LockG2dAddr(ImxStreamBuffer& imxBuf)
{
    fsl::MemoryDesc desc;
    fsl::Memory *handle = NULL;
    fsl::Composer *mComposer = fsl::Composer::getInstance();

    if (imxBuf.buffer == NULL) {
        ALOGE("%s: mFd %d, buffer %p", __func__, imxBuf.mFd, imxBuf.buffer);
        return;
    }

    handle = (fsl::Memory *)imxBuf.buffer;
    mComposer->lockSurface(handle);
    imxBuf.mPhyAddr = handle->phys;

    return;
}

static void UnLockG2dAddr(ImxStreamBuffer& imxBuf)
{
    fsl::Composer *mComposer = fsl::Composer::getInstance();
    fsl::Memory *handle = (fsl::Memory *)imxBuf.buffer;
    if (handle)
        mComposer->unlockSurface(handle);

    return;
}

int ImageProcess::handleFrameByG2D(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf, CscHw hw_type)
{
    int ret = 0;

    ImxStream *src = srcBuf.mStream;
    ImxStream *dst = dstBuf.mStream;

    if ((src->format() == dst->format()) &&
        (src->width() == dst->width()) &&
        (src->height() == dst->height()) &&
        (HAL_PIXEL_FORMAT_RAW16 == src->format())) {
        Revert16BitEndianAndShift((uint8_t *)srcBuf.mVirtAddr, (uint8_t *)dstBuf.mVirtAddr, src->width()*src->height(), ((VideoStream *)src)->mV4l2Format);
        return ret;
    }

    if (mBlitEngine && (hw_type == GPU_2D)) {
        LockG2dAddr(srcBuf);
        LockG2dAddr(dstBuf);
    }

    if ((src->format() == dst->format()) &&
        (src->width() == dst->width()) &&
        (src->height() == dst->height()) &&
        (src->mZoomRatio <= 1.0)) {
        ret = handleFrameByG2DCopy(dstBuf, srcBuf);
    } else {
        ret = handleFrameByG2DBlit(dstBuf, srcBuf);
    }

    if (mBlitEngine && (hw_type == GPU_2D)) {
        UnLockG2dAddr(srcBuf);
        UnLockG2dAddr(dstBuf);
    }

    return ret;
}

int ImageProcess::handleFrameByDPU(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf)
{
    return handleFrameByG2D(dstBuf, srcBuf, DPU);
}

int ImageProcess::handleFrameByGPU_2D(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf)
{
    return handleFrameByG2D(dstBuf, srcBuf, GPU_2D);
}

int ImageProcess::convertNV12toNV21(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf)
{
    ImxStream *src = srcBuf.mStream;
    //ImxStream *dst = dstBuf.mStream;

    int Ysize = 0, UVsize = 0;
    uint8_t *srcIn, *dstOut;
    uint32_t *UVout;
    int size = (srcBuf.mSize > dstBuf.mSize) ? dstBuf.mSize : srcBuf.mSize;

    Ysize  = src->width() * src->height();
    UVsize = src->width() * src->height() >> 2;
    srcIn = (uint8_t *)srcBuf.mVirtAddr;
    dstOut = (uint8_t *)dstBuf.mVirtAddr;
    UVout = (uint32_t *)(dstOut + Ysize);

    if (mCopyEngine != NULL) {
        void* g2dHandle = getHandle();
        struct g2d_buf s_buf, d_buf;
        s_buf.buf_paddr = srcBuf.mPhyAddr;
        s_buf.buf_vaddr = srcBuf.mVirtAddr;
        d_buf.buf_paddr = dstBuf.mPhyAddr;
        d_buf.buf_vaddr = dstBuf.mVirtAddr;
        mCopyEngine(g2dHandle, (void*)&d_buf, (void*)&s_buf,
                     (void*)(intptr_t)size);
        mFinishEngine(g2dHandle);
    }
    else {
        memcpy(dstOut, srcIn, size);
    }

    for (int k = 0; k < UVsize/2; k++) {
        __asm volatile ("rev16 %0, %0" : "+r"(*UVout));
        UVout += 1;
    }

    return 0;
}

static void Revert16BitEndianAndShift(uint8_t *pSrc, uint8_t *pDst, uint32_t pixels, int32_t v4l2Format)
{
    ALOGI("enter Revert16BitEndianAndShift, src %p, dst %p, pixels %d, v4l2Format 0x%x", pSrc, pDst, pixels, v4l2Format);

    for(uint32_t i = 0; i < pixels; i++) {
        uint32_t offset = i*2;
        pDst[offset] = pSrc[offset + 1];
        pDst[offset + 1] = pSrc[offset];
    }

    // left shift 2 bits for 10 bits raw data
    if ((v4l2Format == V4L2_PIX_FMT_SBGGR10) || (v4l2Format == V4L2_PIX_FMT_SGBRG10) ||
        (v4l2Format == V4L2_PIX_FMT_SGRBG10) || (v4l2Format == V4L2_PIX_FMT_SRGGB10)) {
        ALOGI("left shift 2 bits");
        uint16_t *pWordDst = (uint16_t *)pDst;
        for(uint32_t i = 0; i < pixels; i++) {
            *pWordDst = (*pWordDst) << 2;
            pWordDst++;
        }
    }

    return;
}


int ImageProcess::handleFrameByGPU_3D(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf)
{
    // opencl g2d exists.
    if (mCLHandle == NULL) {
        return -EINVAL;
    }

    ImxStream *src = srcBuf.mStream;
    ImxStream *dst = dstBuf.mStream;

    int ret = 0;
    ImxStreamBuffer resizeBuf = {0};
    bool bResize = false;

    // Set output cache attrib based on usage.
    // For input cache attrib, hard code to false, reason as below.
    // 1) For DMA buffer type, the v4l2 buffer is allocated by ion in HAL, and it's un-cacheable.
    // 2) For MMAP buffer type, the v4l2 buffer is allocated by driver and should be cacheable.
    //    The v4l2 buffer will only be read by CPU.
    //    GPU3D uses physical address, no need to flush the input buffer.
    bool bOutputCached = dst->usage() & (USAGE_SW_READ_OFTEN | USAGE_SW_WRITE_OFTEN);

    ALOGV("handleFrameByGPU_3D, bOutputCached %d, usage 0x%llx, res src %dx%d, dst %dx%d, format src 0x%x, dst 0x%x, size %d",
       bOutputCached, dst->usage(), src->width(), src->height(), dst->width(), dst->height(), src->format(), dst->format(), srcBuf.mFormatSize);

    // case 1: same format, same resolution, copy
    if ( (src->format() == dst->format()) &&
         (src->width() == dst->width()) &&
         (src->height() == dst->height()) ) {
        if (HAL_PIXEL_FORMAT_RAW16 == src->format())
            Revert16BitEndianAndShift((uint8_t *)srcBuf.mVirtAddr, (uint8_t *)dstBuf.mVirtAddr, src->width()*src->height(), ((VideoStream *)src)->mV4l2Format);
        else {
            Mutex::Autolock _l(mCLLock);

            if ((src->format() == HAL_PIXEL_FORMAT_YCbCr_420_888) || (src->format() == HAL_PIXEL_FORMAT_YCbCr_420_SP)) {
                cl_Copy(mCLHandle, (uint8_t *)dstBuf.mVirtAddr, (uint8_t *)srcBuf.mVirtAddr, srcBuf.mFormatSize, false, bOutputCached);
            }
            else
                cl_YUYVCopyByLine(mCLHandle, (uint8_t *)dstBuf.mVirtAddr,
                    dst->width(), dst->height(),
                   (uint8_t *)srcBuf.mVirtAddr, src->width(), src->height(), false, bOutputCached);

            (*mCLFlush)(mCLHandle);
            (*mCLFinish)(mCLHandle);
        }

        return 0;
    }

    // case 2: same format, different resolution, resize
    if (src->format() == dst->format()) {
        resizeWrapper(srcBuf, dstBuf, GPU_3D);
        return 0;
    }

    // filter out unsupported CSC format
    if ( false == IsCscSupportByG3D(src->format(), dst->format()) ) {
        ALOGE("%s:%d, G3D don't support format convert from 0x%x to 0x%x",
                 __func__, __LINE__, src->format(), dst->format());
        return -EINVAL;
    }

    // case 3: diffrent format, different resolution
    // first resize, then go through case 4.
    if ( (src->width() != dst->width()) ||
         (src->height() != dst->height()) ) {
        resizeBuf.mFormatSize = getSizeByForamtRes(src->format(), dst->width(), dst->height(), false);
        resizeBuf.mSize = (resizeBuf.mFormatSize + PAGE_SIZE) & (~(PAGE_SIZE - 1));
        ret = AllocPhyBuffer(resizeBuf);
        if (ret) {
            ALOGE("%s:%d AllocPhyBuffer failed", __func__, __LINE__);
            return -EINVAL;
        }
        resizeBuf.mStream = new ImxStream(dst->width(), dst->height(), src->format(), 0, 0);

        resizeWrapper(srcBuf, resizeBuf, GPU_3D);
        SwitchImxBuf(srcBuf, resizeBuf);

        bResize = true;
    }

    // case 4: diffrent format, same resolution
    {
        Mutex::Autolock _l(mCLLock);
        cl_YUYVtoNV12SP(mCLHandle, (uint8_t *)srcBuf.mVirtAddr,
                    (uint8_t *)dstBuf.mVirtAddr, dst->width(), dst->height(), false, bOutputCached);

        (*mCLFlush)(mCLHandle);
        (*mCLFinish)(mCLHandle);
    }

    if (bResize) {
        SwitchImxBuf(srcBuf, resizeBuf);
        FreePhyBuffer(resizeBuf);
        delete(resizeBuf.mStream);
    }

    return 0;
}

int ImageProcess::handleFrameByCPU(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf)
{
    ImxStream *src = srcBuf.mStream;
    ImxStream *dst = dstBuf.mStream;
    ImxStreamBuffer resizeBuf = {0};
    int ret;
    bool bResize = false;

    // case 1: same format, same resolution, copy
    if ( (src->format() == dst->format()) &&
         (src->width() == dst->width()) &&
         (src->height() == dst->height()) ) {
        if (HAL_PIXEL_FORMAT_RAW16 == src->format())
            Revert16BitEndianAndShift((uint8_t *)srcBuf.mVirtAddr, (uint8_t *)dstBuf.mVirtAddr, src->width()*src->height(), ((VideoStream *)src)->mV4l2Format);
        else {
            if (src->format() == HAL_PIXEL_FORMAT_YCBCR_420_888)
                memcpy((uint8_t *)dstBuf.mVirtAddr, (uint8_t *)srcBuf.mVirtAddr, dstBuf.mFormatSize);
            else
                YUYVCopyByLine((uint8_t *)dstBuf.mVirtAddr, dst->width(), dst->height(),
                  (uint8_t *)srcBuf.mVirtAddr, src->width(), src->height());
        }

        return 0;
    }

    // case 2: same format, different resolution, resize
    if (src->format() == dst->format()) {
        ret = resizeWrapper(srcBuf, dstBuf, CPU);
        return ret;
    }

    // filter out unsupported CSC format
    if ( false == IsCscSupportByCPU(src->format(), dst->format()) ) {
        ALOGE("%s:%d, Software don't support format convert from 0x%x to 0x%x",
                 __func__, __LINE__, src->format(), dst->format());
        return -EINVAL;
    }

    // case 3: diffrent format, different resolution
    // first resize, then go through case 4.
    if ( (src->width() != dst->width()) ||
         (src->height() != dst->height()) ) {
        resizeBuf.mFormatSize = getSizeByForamtRes(src->format(), dst->width(), dst->height(), false);
        resizeBuf.mSize = (resizeBuf.mFormatSize + PAGE_SIZE) & (~(PAGE_SIZE - 1));
        ret = AllocPhyBuffer(resizeBuf);
        if (ret) {
            ALOGE("%s:%d AllocPhyBuffer failed", __func__, __LINE__);
            return -EINVAL;
        }
        resizeBuf.mStream = new ImxStream(dst->width(), dst->height(), src->format(), 0, 0);

        resizeWrapper(srcBuf, resizeBuf, CPU);
        SwitchImxBuf(srcBuf, resizeBuf);
        bResize = true;
    }

    // case 4: diffrent format, same resolution
    if (((dst->format() == HAL_PIXEL_FORMAT_YCbCr_420_888) ||
         (dst->format() == HAL_PIXEL_FORMAT_YCbCr_420_SP)) &&
        (src->format() == HAL_PIXEL_FORMAT_YCbCr_422_I)) {
        convertYUYVtoNV12SP((uint8_t *)srcBuf.mVirtAddr,
                    (uint8_t *)dstBuf.mVirtAddr, dst->width(), dst->height());
    } else if ((src->format() == HAL_PIXEL_FORMAT_YCbCr_420_SP) &&
               (dst->format() == HAL_PIXEL_FORMAT_YCrCb_420_SP)) {
        convertNV12toNV21(dstBuf, srcBuf);
    }
    else {
        ALOGE("%s:%d should not enter here", __func__, __LINE__);
    }

    if (bResize) {
        SwitchImxBuf(srcBuf, resizeBuf);
        FreePhyBuffer(resizeBuf);
        delete(resizeBuf.mStream);
    }

    return 0;
}


void ImageProcess::cl_Copy(void *g2dHandle,
         uint8_t *output, uint8_t *input, uint32_t size, bool bInputCached, bool bOutputCached)
{
    struct cl_g2d_buf g2d_output_buf;
    struct cl_g2d_buf g2d_input_buf;

    g2d_output_buf.buf_vaddr = output;
    g2d_output_buf.buf_size = size;
    g2d_output_buf.usage = bOutputCached ? CL_G2D_CPU_MEMORY : CL_G2D_DEVICE_MEMORY;

    g2d_input_buf.buf_vaddr = input;
    g2d_input_buf.buf_size = size;
    g2d_input_buf.usage = bInputCached ? CL_G2D_CPU_MEMORY : CL_G2D_DEVICE_MEMORY;

    (*mCLCopy)(g2dHandle, &g2d_output_buf, &g2d_input_buf, (void*)(intptr_t)size);
}

void ImageProcess::cl_YUYVCopyByLine(void *g2dHandle,
         uint8_t *output, uint32_t dstWidth,
         uint32_t dstHeight, uint8_t *input,
         uint32_t srcWidth, uint32_t srcHeight, bool bInputCached, bool bOutputCached)
{
    struct cl_g2d_surface src,dst;
    src.format = CL_G2D_YUYV;
    src.usage = bInputCached ? CL_G2D_CPU_MEMORY : CL_G2D_DEVICE_MEMORY;
    src.planes[0] = (long)input;
    src.left = 0;
    src.top = 0;
    src.right  = srcWidth;
    src.bottom = srcHeight;
    src.stride = srcWidth;
    src.width  = srcWidth;
    src.height = srcHeight;

    dst.format = CL_G2D_YUYV;
    dst.usage = bOutputCached ? CL_G2D_CPU_MEMORY : CL_G2D_DEVICE_MEMORY;
    dst.planes[0] = (long)output;
    dst.left = 0;
    dst.top = 0;
    dst.right  = dstWidth;
    dst.bottom = dstHeight;
    dst.stride = dstWidth;
    dst.width  = dstWidth;
    dst.height = dstHeight;

    (*mCLBlit)(g2dHandle, (void*)&src, (void*)&dst);
}
void ImageProcess::cl_YUYVtoNV12SP(void *g2dHandle, uint8_t *inputBuffer,
         uint8_t *outputBuffer, int width, int height, bool bInputCached, bool bOutputCached)
{
    struct cl_g2d_surface src,dst;
    src.format = CL_G2D_YUYV;
    src.usage = bInputCached ? CL_G2D_CPU_MEMORY : CL_G2D_DEVICE_MEMORY;
    src.planes[0] = (long)inputBuffer;
    src.left = 0;
    src.top = 0;
    src.right = width;
    src.bottom = height;
    src.stride = width;
    src.width  = width;
    src.height = height;

    dst.format = CL_G2D_NV12;
    dst.usage = bOutputCached ? CL_G2D_CPU_MEMORY : CL_G2D_DEVICE_MEMORY;
    dst.planes[0] = (long)outputBuffer;
    dst.planes[1] = (long)outputBuffer + width * height;
    dst.left = 0;
    dst.top = 0;
    dst.right = width;
    dst.bottom = height;
    dst.stride = width;
    dst.width  = width;
    dst.height = height;

    (*mCLBlit)(g2dHandle, (void*)&src, (void*)&dst);
}

void ImageProcess::YUYVCopyByLine(uint8_t *dst, uint32_t dstWidth,
     uint32_t dstHeight, uint8_t *src, uint32_t srcWidth, uint32_t srcHeight)
{
    uint32_t i;
    int BytesPerPixel = 2;
    uint8_t *pDstLine = dst;
    uint8_t *pSrcLine = src;
    uint32_t bytesPerSrcLine = BytesPerPixel * srcWidth;
    uint32_t bytesPerDstLine = BytesPerPixel * dstWidth;
    uint32_t marginWidh = dstWidth - srcWidth;
    uint16_t *pYUV;

    if ((srcWidth > dstWidth) || (srcHeight > dstHeight)) {
        ALOGW("%s, para error", __func__);
        return;
    }

    for (i = 0; i < srcHeight; i++) {
        memcpy(pDstLine, pSrcLine, bytesPerSrcLine);

        // black margin, Y:0, U:128, V:128
        for (uint32_t j = 0; j < marginWidh; j++) {
            pYUV = (uint16_t *)(pDstLine + bytesPerSrcLine + j * BytesPerPixel);
            *pYUV = 0x8000;
        }

        pSrcLine += bytesPerSrcLine;
        pDstLine += bytesPerDstLine;
    }

    return;
}

void ImageProcess::convertYUYVtoNV12SP(uint8_t *inputBuffer,
            uint8_t *outputBuffer, int width, int height)
{
#define u32 unsigned int
#define u8 unsigned char

    u32 h, w;
    u32 nHeight = height;
    u32 nWidthDiv4 = width / 4;

    u32 *pYSrcOffset = (u32 *)inputBuffer;
    u32 value = 0;
    u32 value2 = 0;

    u32 *pYDstOffset = (u32 *)outputBuffer;
    u32 *pUVDstOffset = (u32 *)(((u8 *)(outputBuffer)) + width * height);

    for (h = 0; h < nHeight; h++) {
        if (!(h & 0x1)) {
            for (w = 0; w < nWidthDiv4; w++) {
                value = (*pYSrcOffset);
                value2 = (*(pYSrcOffset + 1));
                //use bitwise operation to get data from src to improve performance.
                *pYDstOffset = ((value & 0x000000ff) >> 0) |
                               ((value & 0x00ff0000) >> 8) |
                               ((value2 & 0x000000ff) << 16) |
                               ((value2 & 0x00ff0000) << 8);
                pYDstOffset += 1;

#ifdef PLATFORM_VERSION_4
                *pUVDstOffset = ((value & 0xff000000) >> 24) |
                                ((value & 0x0000ff00) >> 0) |
                                ((value2 & 0xff000000) >> 8) |
                                ((value2 & 0x0000ff00) << 16);
#else
                *pUVDstOffset = ((value & 0x0000ff00) >> 8) |
                                ((value & 0xff000000) >> 16) |
                                ((value2 & 0x0000ff00) << 8) |
                                ((value2 & 0xff000000) << 0);
#endif
                pUVDstOffset += 1;
                pYSrcOffset += 2;
            }
        } else {
            for (w = 0; w < nWidthDiv4; w++) {
                value = (*pYSrcOffset);
                value2 = (*(pYSrcOffset + 1));
                *pYDstOffset = ((value & 0x000000ff) >> 0) |
                               ((value & 0x00ff0000) >> 8) |
                               ((value2 & 0x000000ff) << 16) |
                               ((value2 & 0x00ff0000) << 8);
                pYSrcOffset += 2;
                pYDstOffset += 1;
            }
        }
    }
}

int ImageProcess::resizeWrapper(ImxStreamBuffer& srcBuf, ImxStreamBuffer& dstBuf, CscHw hw_type)
{
    int ret;
    ImxStream *src = srcBuf.mStream;
    ImxStream *dst = dstBuf.mStream;

    ALOGV("enter resizeWrapper");

    if (src == NULL || dst == NULL) {
        ALOGE("%s: src %p, dst %p", __func__, src, dst);
        return BAD_VALUE;
    }

    if (src->format() != dst->format()) {
        ALOGE("%s: format are differet, src 0x%x, dst 0x%x", __func__, src->format(), dst->format());
        return BAD_VALUE;
    }

    if ( (src->width() == dst->width()) &&
         (src->height() == dst->height()) ) {
        ALOGE("%s: resolution are same, %dx%d", __func__, src->width(), src->height());
        return BAD_VALUE;
    }

    // Adapt for Camra2.apk. The picture resolution may differ from preview resolution.
    // If resize for preview stream, there will be obvious changes in the preview when taking picture.
    if (dst->isPreview() && src->isPictureIntent()) {
        ALOGW("%s: resize from %dx%d to %dx%d, skip preview stream while taking picture",
            __func__, src->width(), src->height(), dst->width(), dst->height());
        return 0;
    }

    ret = handleFrameByG2D(dstBuf, srcBuf, hw_type);
    if (ret == 0) {
        ALOGV("%s: resize format 0x%x, res %dx%d to %dx%d by g2d ok",
           __func__, src->format(), src->width(), src->height(), dst->width(), dst->height());
        return 0;
    }

cpu_resize:
    if (src->format() == HAL_PIXEL_FORMAT_YCBCR_422_I)
        ret = yuv422iResize((uint8_t *)srcBuf.mVirtAddr, src->width(), src->height(),
                            (uint8_t *)dstBuf.mVirtAddr, dst->width(), dst->height());
    else if (src->format() == HAL_PIXEL_FORMAT_YCBCR_422_SP )
        ret = yuv422spResize((uint8_t *)srcBuf.mVirtAddr, src->width(), src->height(),
                             (uint8_t *)dstBuf.mVirtAddr, dst->width(), dst->height());
    else if ((src->format() == HAL_PIXEL_FORMAT_YCBCR_420_888) || (src->format() == HAL_PIXEL_FORMAT_YCbCr_420_SP)) {
        ret = yuv420spResize((uint8_t *)srcBuf.mVirtAddr, src->width(), src->height(),
                             (uint8_t *)dstBuf.mVirtAddr, dst->width(), dst->height());
    }
    else
        ALOGE("%s: resize by CPU, unsupported format 0x%x", __func__, src->format());

    ALOGV("%s: resize format 0x%x, res %dx%d to %dx%d by cpu, ret %d",
        __func__, src->format(), src->width(), src->height(), dst->width(), dst->height(), ret);

    return ret;
}

}
