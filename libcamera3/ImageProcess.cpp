/*
 * Copyright 2018 NXP.
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
#include <stdio.h>
#include <dlfcn.h>
#include <cutils/log.h>

#include "Stream.h"
#include "CameraUtils.h"
#include "ImageProcess.h"

#include <g2d.h>
#include <linux/ipu.h>
#include "opencl-2d.h"

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

#define GPUENGINE "libg2d.so"
#define CLENGINE "libopencl-2d.so"

namespace fsl {

ImageProcess* ImageProcess::sInstance(0);
Mutex ImageProcess::sLock(Mutex::PRIVATE);

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
    : mIpuFd(-1), mPxpFd(-1), mChannel(-1), m2DEnable(0), mG2dModule(NULL), mCLModule(NULL)
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

    char path[PATH_MAX] = {0};
    getModule(path, GPUENGINE);
    mG2dModule = dlopen(path, RTLD_NOW);
    if (mG2dModule == NULL) {
        mOpenEngine = NULL;
        mCloseEngine = NULL;
        mFinishEngine = NULL;
        mCopyEngine = NULL;
        mBlitEngine = NULL;
    }
    else {
        mOpenEngine = (hwc_func1)dlsym(mG2dModule, "g2d_open");
        mCloseEngine = (hwc_func1)dlsym(mG2dModule, "g2d_close");
        mFinishEngine = (hwc_func1)dlsym(mG2dModule, "g2d_finish");
        mCopyEngine = (hwc_func4)dlsym(mG2dModule, "g2d_copy");
        mBlitEngine = (hwc_func3)dlsym(mG2dModule, "g2d_blit");
    }

    mTls.tls = 0;
    mTls.has_tls = 0;
    pthread_mutex_init(&mTls.lock, NULL);

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
        ret = (*mCLOpen)((void*)&mCLHandle);
        if (ret != 0) {
            mCLHandle = NULL;
        }
    }

    if (property_get_bool("vendor.camera.2d.enable", false)) {
        m2DEnable = 1;
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

    if (mG2dModule != NULL) {
        dlclose(mG2dModule);
    }

    if (mCLModule != NULL) {
        dlclose(mCLModule);
    }
}

void *ImageProcess::getHandle()
{
    void *handle = thread_store_get(&mTls);
    if (handle != NULL) {
        return handle;
    }

    if (mOpenEngine == NULL) {
        return NULL;
    }

    handle = malloc(sizeof(void*));
    if (handle == NULL) {
        return NULL;
    }

    openEngine(&handle);
    thread_store_set(&mTls, handle, threadDestructor);
    return handle;
}

void ImageProcess::threadDestructor(void *handle)
{
    if (handle == NULL) {
        return;
    }

    ImageProcess::getInstance()->closeEngine(handle);
    free(handle);
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

int ImageProcess::handleFrame(StreamBuffer& dstBuf, StreamBuffer& srcBuf)
{
    int ret = 0;

    if (srcBuf.mStream == NULL || dstBuf.mStream == NULL) {
        return -EINVAL;
    }

    do {
        // firstly try GPU.
        ret = handleFrameByGPU(dstBuf, srcBuf);
        if (ret == 0) {
            break;
        }

        // try gpu 2d.
        ret = handleFrameBy2D(dstBuf, srcBuf);
        if (ret == 0) {
            break;
        }

        // try ipu.
        ret = handleFrameByIPU(dstBuf, srcBuf);
        if (ret == 0) {
            break;
        }

        // try pxp.
        ret = handleFrameByPXP(dstBuf, srcBuf);
        if (ret == 0) {
            break;
        }

        // try opencl.
        ret = handleFrameByOpencl(dstBuf, srcBuf);
        if (ret == 0) {
            break;
        }

        // no hardware exists.
        ret = handleFrameByCPU(dstBuf, srcBuf);
    } while(false);

    return ret;
}

static int pxp_get_bpp(unsigned int format)
{
  switch(format) {
  case PXP_PIX_FMT_RGB565:
  case PXP_PIX_FMT_BGR565:
    return 16;
  case PXP_PIX_FMT_XRGB32:
  case PXP_PIX_FMT_XBGR32:
  case PXP_PIX_FMT_ARGB32:
  case PXP_PIX_FMT_ABGR32:
  case PXP_PIX_FMT_BGRA32:
  case PXP_PIX_FMT_BGRX32:
  case PXP_PIX_FMT_RGBA32:
    return 32;
  case PXP_PIX_FMT_UYVY:
  case PXP_PIX_FMT_VYUY:
  case PXP_PIX_FMT_YUYV:
  case PXP_PIX_FMT_YVYU:
    return 16;
  /* for the multi-plane format,
   * only return the bits number
   * for Y plane
   */
  case PXP_PIX_FMT_NV12:
  case PXP_PIX_FMT_NV21:
  case PXP_PIX_FMT_NV16:
  case PXP_PIX_FMT_NV61:
  case PXP_PIX_FMT_YVU420P:
  case PXP_PIX_FMT_YUV420P:
    return 8;
  default:
    ALOGE("%s: unsupported format for getting bpp\n", __func__);
  }
  return 0;
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

int ImageProcess::handleFrameByPXP(StreamBuffer& dstBuf, StreamBuffer& srcBuf)
{
    ALOGV("%s", __func__);
    // pxp exists.
    if (mPxpFd <= 0) {
        return -EINVAL;
    }

    sp<Stream> src, dst;
    src = srcBuf.mStream;
    dst = dstBuf.mStream;
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

int ImageProcess::handleFrameByIPU(StreamBuffer& dstBuf, StreamBuffer& srcBuf)
{
    ALOGV("%s", __func__);
    if (mIpuFd <= 0) {
        return -EINVAL;
    }

    sp<Stream> src, dst;
    src = srcBuf.mStream;
    dst = dstBuf.mStream;
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

int ImageProcess::handleFrameByGPU(StreamBuffer& dstBuf, StreamBuffer& srcBuf)
{
    // gpu 2d exists.
    if (mCopyEngine == NULL) {
        return -EINVAL;
    }

    sp<Stream> src, dst;
    src = srcBuf.mStream;
    dst = dstBuf.mStream;
    // can't do resize for YUV.
    if (dst->width() != src->width() ||
         dst->height() != src->height()) {
        return -EINVAL;
    }

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
    int ret = mCopyEngine(g2dHandle, (void*)&d_buf, (void*)&s_buf,
                 (void*)(intptr_t)size);
    if (ret == 0) {
        mFinishEngine(g2dHandle);
    }

    return ret;
}

int ImageProcess::handleFrameBy2D(StreamBuffer& dstBuf, StreamBuffer& srcBuf)
{
    if (mBlitEngine == NULL) {
        return -EINVAL;
    }
    //only can work on 8mm platform.
    if (m2DEnable == 0) {
        return -EINVAL;
    }

    sp<Stream> src, dst;
    src = srcBuf.mStream;
    dst = dstBuf.mStream;
    // can't do csc for some formats.
    if (!(((dst->format() == HAL_PIXEL_FORMAT_YCbCr_420_888) ||
         (dst->format() == HAL_PIXEL_FORMAT_YCbCr_420_SP) ||
         (dst->format() == HAL_PIXEL_FORMAT_YCrCb_420_SP)) &&
         (src->format() == HAL_PIXEL_FORMAT_YCbCr_422_I))) {
        return -EINVAL;
    }

    int ret;
    void* g2dHandle = getHandle();
    struct g2d_buf s_buf, d_buf;
    struct g2d_surface s_surface, d_surface;

    s_buf.buf_paddr = srcBuf.mPhyAddr;
    s_buf.buf_vaddr = srcBuf.mVirtAddr;
    d_buf.buf_paddr = dstBuf.mPhyAddr;
    d_buf.buf_vaddr = dstBuf.mVirtAddr;

    s_surface.format = (g2d_format)convertPixelFormatToG2DFormat(src->format());
    s_surface.planes[0] = (long)s_buf.buf_paddr;
    s_surface.left = 0;
    s_surface.top = 0;
    s_surface.right = src->width();
    s_surface.bottom = src->height();
    s_surface.stride = src->width();
    s_surface.width  = src->width();
    s_surface.height = src->height();
    s_surface.rot    = G2D_ROTATION_0;

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

    ret = mBlitEngine(g2dHandle, (void*)&s_surface, (void*)&d_surface);

    if (ret == 0) {
        mFinishEngine(g2dHandle);
    }

    return ret;

}

int ImageProcess::convertNV12toNV21(StreamBuffer& dstBuf, StreamBuffer& srcBuf)
{
    sp<Stream> src, dst;
    src = srcBuf.mStream;
    dst = dstBuf.mStream;

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

int ImageProcess::handleFrameByOpencl(StreamBuffer& dstBuf, StreamBuffer& srcBuf)
{
    // opencl g2d exists.
    if (mCLHandle == NULL) {
        return -EINVAL;
    }

    sp<Stream> src, dst;
    src = srcBuf.mStream;
    dst = dstBuf.mStream;

    if ((src->width() != dst->width()) || (src->height() != dst->height())) {
        ALOGE("%s:%d, Software don't support resize", __func__, __LINE__);
        return -EINVAL;
    }

    if (((dst->format() == HAL_PIXEL_FORMAT_YCbCr_420_888) ||
         (dst->format() == HAL_PIXEL_FORMAT_YCbCr_420_SP)) &&
        (src->format() == HAL_PIXEL_FORMAT_YCbCr_422_I)) {
        cl_YUYVtoNV12SP(mCLHandle, (uint8_t *)srcBuf.mVirtAddr,
                    (uint8_t *)dstBuf.mVirtAddr, dst->width(), dst->height());
    } else if (src->format() == dst->format()) {
        cl_YUYVCopyByLine(mCLHandle, (uint8_t *)dstBuf.mVirtAddr,
                 dst->width(), dst->height(),
                (uint8_t *)srcBuf.mVirtAddr, src->width(), src->height(), true);
    } else {
        ALOGI("%s:%d, opencl don't support format convert from 0x%x to 0x%x",
                 __func__, __LINE__, src->format(), dst->format());
        return -EINVAL;
    }

    (*mCLFlush)(mCLHandle);
    (*mCLFinish)(mCLHandle);

    return 0;
}

int ImageProcess::handleFrameByCPU(StreamBuffer& dstBuf, StreamBuffer& srcBuf)
{
    sp<Stream> src, dst;
    src = srcBuf.mStream;
    dst = dstBuf.mStream;

    if ((src->width() != dst->width()) || (src->height() != dst->height())) {
        ALOGE("%s:%d, Software don't support resize", __func__, __LINE__);
        return -EINVAL;
    }

    if (((dst->format() == HAL_PIXEL_FORMAT_YCbCr_420_888) ||
         (dst->format() == HAL_PIXEL_FORMAT_YCbCr_420_SP)) &&
        (src->format() == HAL_PIXEL_FORMAT_YCbCr_422_I)) {
        convertYUYVtoNV12SP((uint8_t *)srcBuf.mVirtAddr,
                    (uint8_t *)dstBuf.mVirtAddr, dst->width(), dst->height());
    } else if ((src->format() == HAL_PIXEL_FORMAT_YCbCr_420_SP) &&
               (dst->format() == HAL_PIXEL_FORMAT_YCrCb_420_SP)) {
        convertNV12toNV21(dstBuf, srcBuf);
    } else if (src->format() == dst->format()) {
        YUYVCopyByLine((uint8_t *)dstBuf.mVirtAddr, dst->width(), dst->height(),
                 (uint8_t *)srcBuf.mVirtAddr, src->width(), src->height());
    } else {
        ALOGE("%s:%d, Software don't support format convert from 0x%x to 0x%x",
                 __func__, __LINE__, src->format(), dst->format());
        return -EINVAL;
    }

    return 0;
}

void ImageProcess::cl_YUYVCopyByLine(void *g2dHandle,
         uint8_t *output, uint32_t dstWidth,
         uint32_t dstHeight, uint8_t *input,
         uint32_t srcWidth, uint32_t srcHeight, bool bInputCached)
{
    struct cl_g2d_surface src,dst;

    src.format = CL_G2D_YUYV;
    if(bInputCached){
        //Input is buffer from usb v4l2 driver
        //cachable buffer
        src.usage = CL_G2D_CPU_MEMORY;
    }
    else
        src.usage = CL_G2D_DEVICE_MEMORY;

    src.planes[0] = (long)input;
    src.left = 0;
    src.top = 0;
    src.right  = srcWidth;
    src.bottom = srcHeight;
    src.stride = srcWidth;
    src.width  = srcWidth;
    src.height = srcHeight;

    dst.format = CL_G2D_YUYV;
    dst.usage = CL_G2D_DEVICE_MEMORY;
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
         uint8_t *outputBuffer, int width, int height)
{
    struct cl_g2d_surface src,dst;
    src.format = CL_G2D_YUYV;
    src.usage = CL_G2D_DEVICE_MEMORY;
    src.planes[0] = (long)inputBuffer;
    src.left = 0;
    src.top = 0;
    src.right = width;
    src.bottom = height;
    src.stride = width;
    src.width  = width;
    src.height = height;

    dst.format = CL_G2D_NV12;
    dst.usage = CL_G2D_DEVICE_MEMORY;
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

}
