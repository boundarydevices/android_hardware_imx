/*
 * Copyright 2023 NXP.
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

#include "ImageProcess.h"

#include <cutils/log.h>
#include <dlfcn.h>
#include <g2d.h>
#include <linux/ipu.h>
#include <stdio.h>
#include <system/graphics.h>
#include <cutils/properties.h>

#include "Composer.h"

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

ImageProcess *ImageProcess::sInstance(0);
Mutex ImageProcess::sLock(Mutex::PRIVATE);

static void Revert16BitEndian(uint8_t *pSrc, uint8_t *pDst, uint32_t pixels);

static bool IsCscSupportByCPU(int srcFormat, int dstFormat) {
    // yuyv -> nv12
    if (((dstFormat == HAL_PIXEL_FORMAT_YCbCr_420_888) ||
         (dstFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP)) &&
        (srcFormat == HAL_PIXEL_FORMAT_YCbCr_422_I))
        return true;

    // nv12 -> nv21
    if ((srcFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP) &&
        (dstFormat == HAL_PIXEL_FORMAT_YCrCb_420_SP))
        return true;

    // nv12 -> yv12
    if ((srcFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP) ||
        (srcFormat == HAL_PIXEL_FORMAT_YCbCr_420_888) && (dstFormat == HAL_PIXEL_FORMAT_YV12))
        return true;

    return false;
}

static bool IsCscSupportByG3D(int srcFomat, int dstFormat) {
    // yuyv -> nv12, nv16 -> nv12
    if (((dstFormat == HAL_PIXEL_FORMAT_YCbCr_420_888) ||
         (dstFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP)) &&
        ((srcFomat == HAL_PIXEL_FORMAT_YCbCr_422_I) ||
         (srcFomat == HAL_PIXEL_FORMAT_YCbCr_422_SP)))
        return true;

    return false;
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

ImageProcess *ImageProcess::getInstance() {
    Mutex::Autolock _l(sLock);
    if (sInstance != NULL) {
        return sInstance;
    }

    sInstance = new ImageProcess();
    return sInstance;
}

ImageProcess::ImageProcess()
      : mIpuFd(-1), mPxpFd(-1), mChannel(-1), mG2dModule(NULL), mCLModule(NULL) {
    /*
     * imx6dl support ENG_IPU device and ENG_PXP device.
     * imx6q and imx6qp support ENG_IPU device.
     * imx6sx and imx6sl support ENG_PXP device.
     * ENG_IPU can't handle NV21 format, so open ENG_PXP on some platform to handle it.
     */
    mIpuFd = open("/dev/mxc_ipu", O_RDWR, 0);
    mPxpFd = open("/dev/pxp_device", O_RDWR, 0);

    // When open pxp device, need allocate a channel at the same time.
    int32_t ret = -1;
    if (mPxpFd > 0) {
        ret = ioctl(mPxpFd, PXP_IOC_GET_CHAN, &mChannel);
        if (ret < 0) {
            ALOGE("%s:%d, PXP_IOC_GET_CHAN failed %d", __func__, __LINE__, ret);
            close(mPxpFd);
            mPxpFd = -1;
        }
    }
    ALOGI("mChannel %d", mChannel);

    char path[PATH_MAX] = {0};
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

    memset(path, 0, sizeof(path));
    getModule(path, CLENGINE);
    mCLModule = dlopen(path, RTLD_NOW);
    if (mCLModule == NULL) {
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
        ALOGW("opencl g2d device is used!\n");
    }
}

ImageProcess::~ImageProcess() {
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

void *ImageProcess::getHandle() {
    return mG2dHandle;
}

int ImageProcess::openEngine(void **handle) {
    if (mOpenEngine == NULL) {
        return -EINVAL;
    }

    return (*mOpenEngine)((void *)handle);
}

int ImageProcess::closeEngine(void *handle) {
    if (mCloseEngine == NULL) {
        return -EINVAL;
    }

    return (*mCloseEngine)(handle);
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

int ImageProcess::ConvertImage(ImxImageBuffer &dstBuf, ImxImageBuffer &srcBuf, ImxEngine engine) {
    int ret = 0;

    if (!((engine == ENG_NOTCARE) || (engine >= ENG_MIN && engine < ENG_NUM))) {
        ALOGE("%s: invalid engine %d", __func__, engine);
        return -EINVAL;
    }

    ALOGV("ImageProcess::ConvertImage, src: virt %p, phy 0x%lx, size %d, res %ux%u, format 0x%x, "
          "dst: virt %p, phy 0x%lx, size %d, res %ux%u, format 0x%x, engine %d",
          srcBuf.mVirtAddr, srcBuf.mPhyAddr, (int)srcBuf.mSize, srcBuf.mWidth,
          srcBuf.mHeight, srcBuf.mFormat, dstBuf.mVirtAddr, dstBuf.mPhyAddr,
          (int)dstBuf.mSize, dstBuf.mWidth, dstBuf.mHeight, dstBuf.mFormat, engine);

    // unify HAL_PIXEL_FORMAT_YCbCr_420_SP to HAL_PIXEL_FORMAT_YCBCR_420_888
    if (srcBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP) {
        srcBuf.mFormat = HAL_PIXEL_FORMAT_YCBCR_420_888;
    }

    if (dstBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP) {
        dstBuf.mFormat = HAL_PIXEL_FORMAT_YCBCR_420_888;
    }

    if (engine != ENG_NOTCARE) {
        ret = (this->*g_EngFuncList[engine])(dstBuf, srcBuf);
        return ret;
    }

    // If ENG_NOTCARE, go through all engines until convert ok.
    for (int i = ENG_MIN; i < ENG_NUM; i++) {
        ret = (this->*g_EngFuncList[i])(dstBuf, srcBuf);
        if (ret == 0)
            return 0;
    }

    return ret;
}

int convertPixelFormatToG2DFormat(int format) {
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

int ImageProcess::ConvertImageByPXP(ImxImageBuffer &dstBuf, ImxImageBuffer &srcBuf) {
    ALOGV("%s", __func__);
    // pxp exists.
    if (mPxpFd <= 0) {
        return -EINVAL;
    }

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

    src_param->width = srcBuf.mWidth;
    src_param->height = srcBuf.mHeight;
    src_param->color_key = -1;
    src_param->color_key_enable = 0;
    src_param->pixel_fmt = convertPixelFormatToV4L2Format(srcBuf.mFormat);
    src_param->stride = src_param->width;
    pxp_conf.proc_data.srect.top = 0;
    pxp_conf.proc_data.srect.left = 0;
    pxp_conf.proc_data.srect.width = srcBuf.mWidth;
    pxp_conf.proc_data.srect.height = srcBuf.mHeight;

    /*
     * Initialize out parameters
     */
    out_param->paddr = dstBuf.mPhyAddr;
    out_param->width = dstBuf.mWidth;
    out_param->height = dstBuf.mHeight;
    out_param->pixel_fmt = convertPixelFormatToV4L2Format(dstBuf.mFormat);
    out_param->stride = out_param->width;
    pxp_conf.handle = mChannel;
    pxp_conf.proc_data.drect.top = 0;
    pxp_conf.proc_data.drect.left = 0;
    pxp_conf.proc_data.drect.width = dstBuf.mWidth;
    pxp_conf.proc_data.drect.height = dstBuf.mHeight;

    if ((src_param->stride == 0) || (out_param->stride == 0)) {
        ALOGE("%s:%d, src stride %d, dst stride %d", __func__, __LINE__, src_param->stride,
              out_param->stride);
        return -EINVAL;
    }

    // The fb dirver treat r as bit[0:7], but ENG_PXP convert r to bit[24:31].
    // For preview, do some trick to set format as G2D_YVYU.
    if ((src_param->pixel_fmt == PXP_PIX_FMT_YUYV) &&
        (out_param->pixel_fmt == PXP_PIX_FMT_RGBA32)) {
        src_param->pixel_fmt = PXP_PIX_FMT_YVYU;
        out_param->pixel_fmt = PXP_PIX_FMT_ARGB32;
    }

    ALOGV("src: %ux%u, 0x%x, phy 0x%lx, v4l2 0x%x, stride %d", srcBuf.mWidth, srcBuf.mHeight,
          srcBuf.mFormat, srcBuf.mPhyAddr, src_param->pixel_fmt, src_param->stride);
    ALOGV("dst: %ux%u, 0x%x, phy 0x%lx, v4l2 0x%x, stride %d", dstBuf.mWidth, dstBuf.mHeight,
          dstBuf.mFormat, dstBuf.mPhyAddr, out_param->pixel_fmt, out_param->stride);

    ret = ioctl(mPxpFd, PXP_IOC_CONFIG_CHAN, &pxp_conf);
    if (ret < 0) {
        ALOGE("%s:%d, PXP_IOC_CONFIG_CHAN failed %d", __func__, __LINE__, ret);
        return ret;
    }

    ret = ioctl(mPxpFd, PXP_IOC_START_CHAN, &(pxp_conf.handle));
    if (ret < 0) {
        ALOGE("%s:%d, PXP_IOC_START_CHAN failed %d", __func__, __LINE__, ret);
        return ret;
    }

    ret = ioctl(mPxpFd, PXP_IOC_WAIT4CMPLT, &pxp_conf);
    if (ret < 0) {
        ALOGE("%s:%d, PXP_IOC_WAIT4CMPLT failed %d", __func__, __LINE__, ret);
    }

    return ret;
}

int ImageProcess::ConvertImageByIPU(ImxImageBuffer &dstBuf, ImxImageBuffer &srcBuf) {
    ALOGV("%s", __func__);
    if (mIpuFd <= 0) {
        return -EINVAL;
    }

    if (dstBuf.mFormat == HAL_PIXEL_FORMAT_YCrCb_420_SP) {
        return -EINVAL;
    }

    struct ipu_task mTask;
    memset(&mTask, 0, sizeof(mTask));

    mTask.input.width = srcBuf.mWidth;
    mTask.input.height = srcBuf.mHeight;
    mTask.input.crop.pos.x = 0;
    mTask.input.crop.pos.y = 0;
    mTask.input.crop.w = srcBuf.mWidth;
    mTask.input.crop.h = srcBuf.mHeight;
    mTask.input.format = convertPixelFormatToV4L2Format(srcBuf.mFormat);
    mTask.input.paddr = srcBuf.mPhyAddr;

    mTask.output.format = convertPixelFormatToV4L2Format(dstBuf.mFormat);
    mTask.output.width = dstBuf.mWidth;
    mTask.output.height = dstBuf.mHeight;
    mTask.output.crop.pos.x = 0;
    mTask.output.crop.pos.y = 0;
    mTask.output.crop.w = dstBuf.mWidth;
    mTask.output.crop.h = dstBuf.mHeight;
    mTask.output.rotate = 0;
    mTask.output.paddr = dstBuf.mPhyAddr;

    int32_t ret = IPU_CHECK_ERR_INPUT_CROP;
    while (ret != IPU_CHECK_OK && ret > IPU_CHECK_ERR_MIN) {
        ret = ioctl(mIpuFd, IPU_CHECK_TASK, &mTask);
        ALOGV("%s:%d, IPU_CHECK_TASK ret=%d", __func__, __LINE__, ret);
        switch (ret) {
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
                mTask.output.crop.h -= 8;
                ;
                break;
            default:
                ALOGE("%s:%d, IPU_CHECK_TASK ret=%d", __func__, __LINE__, ret);
                return ret;
        }
    }

    ret = ioctl(mIpuFd, IPU_QUEUE_TASK, &mTask);
    if (ret < 0) {
        ALOGE("%s:%d, IPU_QUEUE_TASK failed %d", __func__, __LINE__, ret);
    }

    return ret;
}

int ImageProcess::ConvertImageByG2DCopy(ImxImageBuffer &dstBuf, ImxImageBuffer &srcBuf) {
    // gpu 2d exists.
    if (mCopyEngine == NULL) {
        return -EINVAL;
    }

    int dstFormat = convertPixelFormatToV4L2Format(dstBuf.mFormat);
    int srcFormat = convertPixelFormatToV4L2Format(srcBuf.mFormat);
    // can't do csc for YUV.
    if ((dstBuf.mFormat != srcBuf.mFormat) && (dstFormat != srcFormat)) {
        return -EINVAL;
    }

    void *g2dHandle = getHandle();
    int size = (srcBuf.mSize > dstBuf.mSize) ? dstBuf.mSize : srcBuf.mSize;

    struct g2d_buf s_buf, d_buf;
    s_buf.buf_paddr = srcBuf.mPhyAddr;
    s_buf.buf_vaddr = srcBuf.mVirtAddr;
    d_buf.buf_paddr = dstBuf.mPhyAddr;
    d_buf.buf_vaddr = dstBuf.mVirtAddr;

    Mutex::Autolock _l(mG2dLock);
    int ret = mCopyEngine(g2dHandle, (void *)&d_buf, (void *)&s_buf, (void *)(intptr_t)size);
    if (ret == 0) {
        mFinishEngine(g2dHandle);
    }

    return ret;
}

int ImageProcess::ConvertImageByG2DBlit(ImxImageBuffer &dstBuf, ImxImageBuffer &srcBuf) {
    if (mBlitEngine == NULL) {
        return -EINVAL;
    }

    ImxImageBuffer resizeBuf;
    memset(&resizeBuf, 0, sizeof(resizeBuf));

    // can't do csc for some formats.
    if (!(((dstBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_420_888) ||
           (dstBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP) ||
           ((dstBuf.mFormat == HAL_PIXEL_FORMAT_YCrCb_420_SP) &&
            (srcBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_422_I)) ||
           ((srcBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_422_I) &&
            (dstBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_422_I))))) {
        return -EINVAL;
    }

    int ret;
    void *g2dHandle = getHandle();
    struct g2d_buf s_buf, d_buf;
    struct g2d_surface s_surface, d_surface;

    s_buf.buf_paddr = srcBuf.mPhyAddr;
    s_buf.buf_vaddr = srcBuf.mVirtAddr;
    d_buf.buf_paddr = dstBuf.mPhyAddr;
    d_buf.buf_vaddr = dstBuf.mVirtAddr;

    // zoom feature
    uint32_t crop_left = 0;
    uint32_t crop_top = 0;
    uint32_t crop_width = srcBuf.mWidth;
    uint32_t crop_height = srcBuf.mHeight;

    // currently, just suppport zoom in.
    if (srcBuf.mZoomRatio > 1.0) {
        crop_width = srcBuf.mWidth / srcBuf.mZoomRatio;
        crop_height = srcBuf.mHeight / srcBuf.mZoomRatio;
        crop_left = (srcBuf.mWidth - crop_width) / 2;
        crop_top = (srcBuf.mHeight - crop_height) / 2;
    }

    s_surface.format = (g2d_format)convertPixelFormatToG2DFormat(srcBuf.mFormat);
    s_surface.planes[0] = (long)s_buf.buf_paddr;
    s_surface.left = crop_left;
    s_surface.top = crop_top;
    s_surface.right = crop_left + crop_width;
    s_surface.bottom = crop_top + crop_height;
    s_surface.stride = srcBuf.mWidth;
    s_surface.width = srcBuf.mWidth;
    s_surface.height = srcBuf.mHeight;
    s_surface.rot = G2D_ROTATION_0;

    ALOGV("%s: crop from (%d, %d), size %dx%d, srcBuf.mFormatSize %d, mZoomRatio %f", __func__,
          crop_left, crop_top, crop_width, crop_height, (int)srcBuf.mFormatSize, srcBuf.mZoomRatio);

    if ((srcBuf.mFormat == dstBuf.mFormat) ||
        (srcBuf.mZoomRatio <= 1.0 &&
         (srcBuf.mWidth == dstBuf.mWidth &&
          srcBuf.mHeight == dstBuf.mHeight))) { // just scale or just csc
        d_surface.format = (g2d_format)convertPixelFormatToG2DFormat(dstBuf.mFormat);
        d_surface.planes[0] = (long)d_buf.buf_paddr;
        d_surface.planes[1] = (long)d_buf.buf_paddr + dstBuf.mWidth * dstBuf.mHeight;
        d_surface.left = 0;
        d_surface.top = 0;
        d_surface.right = dstBuf.mWidth;
        d_surface.bottom = dstBuf.mHeight;
        d_surface.stride = dstBuf.mWidth;
        d_surface.width = dstBuf.mWidth;
        d_surface.height = dstBuf.mHeight;
        d_surface.rot = G2D_ROTATION_0;

        Mutex::Autolock _l(mG2dLock);
        ret = mBlitEngine(g2dHandle, (void *)&s_surface, (void *)&d_surface);
        if (ret)
            goto finish_blit;

        mFinishEngine(g2dHandle);
    } else {
        struct g2d_surface tmp_surface;

        resizeBuf.mFormatSize =
                getSizeByForamtRes(srcBuf.mFormat, dstBuf.mWidth, dstBuf.mHeight, false);
        resizeBuf.mSize = (resizeBuf.mFormatSize + PAGE_SIZE) & (~(PAGE_SIZE - 1));
        ret = AllocPhyBuffer(resizeBuf);
        if (ret) {
            ALOGE("%s:%d AllocPhyBuffer failed", __func__, __LINE__);
            return BAD_VALUE;
        }
        ALOGV("%s: resizeBuf.mFormatSize %d, resizeBuf.mSize %d", __func__,
              (int)resizeBuf.mFormatSize, (int)resizeBuf.mSize);

        // first scale on same format as source
        tmp_surface.format = (g2d_format)convertPixelFormatToG2DFormat(srcBuf.mFormat);
        tmp_surface.planes[0] = (long)resizeBuf.mPhyAddr;
        tmp_surface.planes[1] = (long)resizeBuf.mPhyAddr + dstBuf.mWidth * dstBuf.mHeight;
        tmp_surface.left = 0;
        tmp_surface.top = 0;
        tmp_surface.right = dstBuf.mWidth;
        tmp_surface.bottom = dstBuf.mHeight;
        tmp_surface.stride = dstBuf.mWidth;
        tmp_surface.width = dstBuf.mWidth;
        tmp_surface.height = dstBuf.mHeight;
        tmp_surface.rot = G2D_ROTATION_0;

        Mutex::Autolock _l(mG2dLock);
        ret = mBlitEngine(g2dHandle, (void *)&s_surface, (void *)&tmp_surface);
        if (ret)
            goto finish_blit;

        mFinishEngine(g2dHandle);

        // then csc to dst format
        d_surface.format = (g2d_format)convertPixelFormatToG2DFormat(dstBuf.mFormat);
        d_surface.planes[0] = (long)d_buf.buf_paddr;
        d_surface.planes[1] = (long)d_buf.buf_paddr + dstBuf.mWidth * dstBuf.mHeight;
        d_surface.left = 0;
        d_surface.top = 0;
        d_surface.right = dstBuf.mWidth;
        d_surface.bottom = dstBuf.mHeight;
        d_surface.stride = dstBuf.mWidth;
        d_surface.width = dstBuf.mWidth;
        d_surface.height = dstBuf.mHeight;
        d_surface.rot = G2D_ROTATION_0;

        ret = mBlitEngine(g2dHandle, (void *)&tmp_surface, (void *)&d_surface);
        if (ret)
            goto finish_blit;

        mFinishEngine(g2dHandle);
    }

finish_blit:
    FreePhyBuffer(resizeBuf);
    return ret;
}

static void LockG2dAddr(ImxImageBuffer &imxBuf) {
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

static void UnLockG2dAddr(ImxImageBuffer &imxBuf) {
    fsl::Composer *mComposer = fsl::Composer::getInstance();
    fsl::Memory *handle = (fsl::Memory *)imxBuf.buffer;
    if (handle)
        mComposer->unlockSurface(handle);

    return;
}

int ImageProcess::ConvertImageByG2D(ImxImageBuffer &dstBuf, ImxImageBuffer &srcBuf,
                                   ImxEngine engine) {
    int ret = 0;

    if ((srcBuf.mFormat == dstBuf.mFormat) && (srcBuf.mWidth == dstBuf.mWidth) &&
        (srcBuf.mHeight == dstBuf.mHeight) && (HAL_PIXEL_FORMAT_RAW16 == srcBuf.mFormat)) {
        Revert16BitEndian((uint8_t *)srcBuf.mVirtAddr, (uint8_t *)dstBuf.mVirtAddr,
                          srcBuf.mWidth * srcBuf.mHeight);
        return ret;
    }

    if (mBlitEngine && (engine == ENG_G2D)) {
        LockG2dAddr(srcBuf);
        LockG2dAddr(dstBuf);
    }

    if ((srcBuf.mFormat == dstBuf.mFormat) && (srcBuf.mWidth == dstBuf.mWidth) &&
        (srcBuf.mHeight == dstBuf.mHeight) && (srcBuf.mZoomRatio <= 1.0)) {
        ret = ConvertImageByG2DCopy(dstBuf, srcBuf);
    } else {
        ret = ConvertImageByG2DBlit(dstBuf, srcBuf);
    }

    if (mBlitEngine && (engine == ENG_G2D)) {
        UnLockG2dAddr(srcBuf);
        UnLockG2dAddr(dstBuf);
    }

    return ret;
}

int ImageProcess::ConvertImageByDPU(ImxImageBuffer &dstBuf, ImxImageBuffer &srcBuf) {
    return ConvertImageByG2D(dstBuf, srcBuf, ENG_DPU);
}

int ImageProcess::ConvertImageByGPU_2D(ImxImageBuffer &dstBuf, ImxImageBuffer &srcBuf) {
    return ConvertImageByG2D(dstBuf, srcBuf, ENG_G2D);
}

int ImageProcess::convertNV12toNV21(ImxImageBuffer &dstBuf, ImxImageBuffer &srcBuf) {
    int Ysize = 0, UVsize = 0;
    uint8_t *srcIn, *dstOut;
    uint32_t *UVout;
    int size = (srcBuf.mSize > dstBuf.mSize) ? dstBuf.mSize : srcBuf.mSize;

    Ysize = srcBuf.mWidth * srcBuf.mHeight;
    UVsize = srcBuf.mWidth * srcBuf.mHeight >> 2;
    srcIn = (uint8_t *)srcBuf.mVirtAddr;
    dstOut = (uint8_t *)dstBuf.mVirtAddr;
    UVout = (uint32_t *)(dstOut + Ysize);

    if (mCopyEngine != NULL) {
        void *g2dHandle = getHandle();
        struct g2d_buf s_buf, d_buf;
        s_buf.buf_paddr = srcBuf.mPhyAddr;
        s_buf.buf_vaddr = srcBuf.mVirtAddr;
        d_buf.buf_paddr = dstBuf.mPhyAddr;
        d_buf.buf_vaddr = dstBuf.mVirtAddr;
        mCopyEngine(g2dHandle, (void *)&d_buf, (void *)&s_buf, (void *)(intptr_t)size);
        mFinishEngine(g2dHandle);
    } else {
        memcpy(dstOut, srcIn, size);
    }

    for (int k = 0; k < UVsize / 2; k++) {
        __asm volatile("rev16 %0, %0" : "+r"(*UVout));
        UVout += 1;
    }

    return 0;
}

void ImageProcess::convertNV12toYV12(uint8_t *inputBuffer, uint8_t *outputBuffer, int width,
                                     int height) {
    int size = width * height;
    // Y
    memcpy(outputBuffer, inputBuffer, size);
    // V
    uint8_t *ptrV1 = outputBuffer + size;
    uint8_t *ptrV2 = inputBuffer + size + 1;
    // U
    uint8_t *ptrU1 = outputBuffer + size * 5 / 4;
    uint8_t *ptrU2 = inputBuffer + size;
    int n = 0;
    while (n < size / 4) {
        *(ptrV1) = *(ptrV2);
        ptrV1++;
        ptrV2 = ptrV2 + 2;

        *(ptrU1) = *(ptrU2);
        ptrU1++;
        ptrU2 = ptrU2 + 2;
        n++;
    }
}

static void Revert16BitEndian(uint8_t *pSrc, uint8_t *pDst, uint32_t pixels) {
    ALOGI("enter Revert16BitEndian, src %p, dst %p, pixels %d", pSrc, pDst, pixels);

    for (uint32_t i = 0; i < pixels; i++) {
        uint32_t offset = i * 2;
        pDst[offset] = pSrc[offset + 1];
        pDst[offset + 1] = pSrc[offset];
    }

    return;
}

int ImageProcess::ConvertImageByGPU_3D(ImxImageBuffer &dstBuf, ImxImageBuffer &srcBuf) {
    // opencl g2d exists.
    if (mCLHandle == NULL) {
        return -EINVAL;
    }

    int ret = 0;
    ImxImageBuffer resizeBuf;
    memset(&resizeBuf, 0, sizeof(resizeBuf));
    bool bResize = false;

    // Set output cache attrib based on usage.
    // For input cache attrib, hard code to false, reason as below.
    // 1) For DMA buffer type, the v4l2 buffer is allocated by ion in HAL, and it's un-cacheable.
    // 2) For MMAP buffer type, the v4l2 buffer is allocated by driver and should be cacheable.
    //    The v4l2 buffer will only be read by ENG_CPU.
    //    GPU3D uses physical address, no need to flush the input buffer.
    bool bOutputCached = dstBuf.mUsage & (USAGE_SW_READ_OFTEN | USAGE_SW_WRITE_OFTEN);

    ALOGV("ConvertImageByGPU_3D, bOutputCached %d, usage 0x%lx, res src %ux%u, dst %ux%u, format "
          "src 0x%x, dst 0x%x, size %d",
          bOutputCached, dstBuf.mUsage, srcBuf.mWidth, srcBuf.mHeight, dstBuf.mWidth, dstBuf.mHeight,
          srcBuf.mFormat, dstBuf.mFormat, (int)srcBuf.mFormatSize);

    // Fix me! Currently, the GPU only support using physical address for uncached memory.
    // Otherwise the physical address will be taken as virtual one, leading crash.
    // Will remove the hard code after GPU fix the issue.
    bOutputCached = false;

    // case 1: same format, same resolution, copy
    if ((srcBuf.mFormat == dstBuf.mFormat) && (srcBuf.mWidth == dstBuf.mWidth) &&
        (srcBuf.mHeight == dstBuf.mHeight)) {
        if (HAL_PIXEL_FORMAT_RAW16 == srcBuf.mFormat)
            Revert16BitEndian((uint8_t *)srcBuf.mVirtAddr, (uint8_t *)dstBuf.mVirtAddr,
                              srcBuf.mWidth * srcBuf.mHeight);
        else {
            Mutex::Autolock _l(mCLLock);

            cl_Copy(mCLHandle, (uint8_t *)dstBuf.mPhyAddr, (uint8_t *)srcBuf.mPhyAddr,
                    srcBuf.mFormatSize, false, bOutputCached);

            (*mCLFlush)(mCLHandle);
            (*mCLFinish)(mCLHandle);
        }

        return 0;
    }

    // case 2: same format, different resolution, resize
    if (srcBuf.mFormat == dstBuf.mFormat) {
        resizeWrapper(srcBuf, dstBuf, ENG_G3D);
        return 0;
    }

    // filter out unsupported CSC format
    if (false == IsCscSupportByG3D(srcBuf.mFormat, dstBuf.mFormat)) {
        ALOGE("%s:%d, G3D don't support format convert from 0x%x to 0x%x", __func__, __LINE__,
              srcBuf.mFormat, dstBuf.mFormat);
        return -EINVAL;
    }

    // case 3: diffrent format, different resolution
    // first resize, then go through case 4.
    if ((srcBuf.mWidth != dstBuf.mWidth) || (srcBuf.mHeight != dstBuf.mHeight)) {
        resizeBuf.mFormatSize =
                getSizeByForamtRes(srcBuf.mFormat, dstBuf.mWidth, dstBuf.mHeight, false);
        resizeBuf.mSize = (resizeBuf.mFormatSize + PAGE_SIZE) & (~(PAGE_SIZE - 1));
        ret = AllocPhyBuffer(resizeBuf);
        if (ret) {
            ALOGE("%s:%d AllocPhyBuffer failed", __func__, __LINE__);
            return -EINVAL;
        }

        resizeWrapper(srcBuf, resizeBuf, ENG_G3D);
        SwitchImxBuf(srcBuf, resizeBuf);

        bResize = true;
    }

    // case 4: diffrent format, same resolution
    {
        Mutex::Autolock _l(mCLLock);
        cl_csc(mCLHandle, (uint8_t *)srcBuf.mPhyAddr, (uint8_t *)dstBuf.mPhyAddr,
                        dstBuf.mWidth, dstBuf.mHeight, srcBuf.mHeightSpan, false, bOutputCached, srcBuf.mFormat, dstBuf.mFormat);

        (*mCLFlush)(mCLHandle);
        (*mCLFinish)(mCLHandle);
    }

    if (bResize) {
        SwitchImxBuf(srcBuf, resizeBuf);
        FreePhyBuffer(resizeBuf);
    }

    return 0;
}

int ImageProcess::ConvertImageByCPU(ImxImageBuffer &dstBuf, ImxImageBuffer &srcBuf) {
    ImxImageBuffer resizeBuf;
    memset(&resizeBuf, 0, sizeof(resizeBuf));
    int ret;
    bool bResize = false;

    // case 1: same format, same resolution, copy
    if ((srcBuf.mFormat == dstBuf.mFormat) && (srcBuf.mWidth == dstBuf.mWidth) &&
        (srcBuf.mHeight == dstBuf.mHeight)) {
        if (HAL_PIXEL_FORMAT_RAW16 == srcBuf.mFormat)
            Revert16BitEndian((uint8_t *)srcBuf.mVirtAddr, (uint8_t *)dstBuf.mVirtAddr,
                              srcBuf.mWidth * srcBuf.mHeight);
        else {
            memcpy((uint8_t *)dstBuf.mVirtAddr, (uint8_t *)srcBuf.mVirtAddr, dstBuf.mFormatSize);
        }

        return 0;
    }

    // case 2: same format, different resolution, resize
    if (srcBuf.mFormat == dstBuf.mFormat) {
        ret = resizeWrapper(srcBuf, dstBuf, ENG_CPU);
        return ret;
    }

    // filter out unsupported CSC format
    if (false == IsCscSupportByCPU(srcBuf.mFormat, dstBuf.mFormat)) {
        ALOGE("%s:%d, Software don't support format convert from 0x%x to 0x%x", __func__, __LINE__,
              srcBuf.mFormat, dstBuf.mFormat);
        return -EINVAL;
    }

    // case 3: diffrent format, different resolution
    // first resize, then go through case 4.
    if ((srcBuf.mWidth != dstBuf.mWidth) || (srcBuf.mHeight != dstBuf.mHeight)) {
        resizeBuf.mFormatSize =
                getSizeByForamtRes(srcBuf.mFormat, dstBuf.mWidth, dstBuf.mHeight, false);
        resizeBuf.mSize = (resizeBuf.mFormatSize + PAGE_SIZE) & (~(PAGE_SIZE - 1));
        ret = AllocPhyBuffer(resizeBuf);
        if (ret) {
            ALOGE("%s:%d AllocPhyBuffer failed", __func__, __LINE__);
            return -EINVAL;
        }

        resizeWrapper(srcBuf, resizeBuf, ENG_CPU);
        SwitchImxBuf(srcBuf, resizeBuf);
        bResize = true;
    }

    // case 4: diffrent format, same resolution
    if (((dstBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_420_888) ||
         (dstBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP)) &&
        (srcBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_422_I)) {
        convertYUYVtoNV12SP((uint8_t *)srcBuf.mVirtAddr, (uint8_t *)dstBuf.mVirtAddr, dstBuf.mWidth,
                            dstBuf.mHeight);
    } else if ((srcBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP) &&
               (dstBuf.mFormat == HAL_PIXEL_FORMAT_YCrCb_420_SP)) {
        convertNV12toNV21(dstBuf, srcBuf);
    } else if ((srcBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP ||
                (srcBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_420_888)) &&
               (dstBuf.mFormat == HAL_PIXEL_FORMAT_YV12)) {
        convertNV12toYV12((uint8_t *)srcBuf.mVirtAddr, (uint8_t *)dstBuf.mVirtAddr, dstBuf.mWidth,
                          dstBuf.mHeight);
    } else {
        ALOGE("%s:%d should not enter here", __func__, __LINE__);
    }

    if (bResize) {
        SwitchImxBuf(srcBuf, resizeBuf);
        FreePhyBuffer(resizeBuf);
    }

    return 0;
}

void ImageProcess::cl_Copy(void *g2dHandle, uint8_t *output, uint8_t *input, uint32_t size,
                           bool bInputCached, bool bOutputCached) {
    struct cl_g2d_buf g2d_output_buf;
    struct cl_g2d_buf g2d_input_buf;

    g2d_output_buf.buf_paddr = (uint64_t)output;
    g2d_output_buf.buf_size = size;
    g2d_output_buf.use_phy = true;
    g2d_output_buf.usage = bOutputCached ? CL_G2D_CACHED_MEMORY : CL_G2D_UNCACHED_MEMORY;

    g2d_input_buf.buf_paddr = (uint64_t)input;
    g2d_input_buf.buf_size = size;
    g2d_input_buf.use_phy = true;
    g2d_input_buf.usage = bInputCached ? CL_G2D_CACHED_MEMORY : CL_G2D_UNCACHED_MEMORY;

    (*mCLCopy)(g2dHandle, &g2d_output_buf, &g2d_input_buf, (void *)(intptr_t)size);
}

void ImageProcess::cl_csc(void *g2dHandle, uint8_t *inputBuffer, uint8_t *outputBuffer,
                                   int width, int height, int srcHeightSpan, bool bInputCached, bool bOutputCached, uint32_t inFmt, uint32_t outFmt) {

    struct cl_g2d_surface src, dst;

    src.format = (cl_g2d_format)convertPixelFormatToCLFormat(inFmt);
    src.usage = bInputCached ? CL_G2D_CACHED_MEMORY : CL_G2D_UNCACHED_MEMORY;
    src.planes[0] = (long)inputBuffer;
    src.planes[1] = (long)inputBuffer + width * srcHeightSpan;
    src.left = 0;
    src.top = 0;
    src.right = width;
    src.bottom = height;
    src.stride = width;
    src.width = width;
    src.height = height;
    src.usePhyAddr = true;

    dst.format = (cl_g2d_format)convertPixelFormatToCLFormat(outFmt);
    dst.usage = bOutputCached ? CL_G2D_CACHED_MEMORY : CL_G2D_UNCACHED_MEMORY;
    dst.planes[0] = (long)outputBuffer;
    dst.planes[1] = (long)outputBuffer + width * height;
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

void ImageProcess::convertYUYVtoNV12SP(uint8_t *inputBuffer, uint8_t *outputBuffer, int width,
                                       int height) {
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
                // use bitwise operation to get data from src to improve performance.
                *pYDstOffset = ((value & 0x000000ff) >> 0) | ((value & 0x00ff0000) >> 8) |
                        ((value2 & 0x000000ff) << 16) | ((value2 & 0x00ff0000) << 8);
                pYDstOffset += 1;

#ifdef PLATFORM_VERSION_4
                *pUVDstOffset = ((value & 0xff000000) >> 24) | ((value & 0x0000ff00) >> 0) |
                        ((value2 & 0xff000000) >> 8) | ((value2 & 0x0000ff00) << 16);
#else
                *pUVDstOffset = ((value & 0x0000ff00) >> 8) | ((value & 0xff000000) >> 16) |
                        ((value2 & 0x0000ff00) << 8) | ((value2 & 0xff000000) << 0);
#endif
                pUVDstOffset += 1;
                pYSrcOffset += 2;
            }
        } else {
            for (w = 0; w < nWidthDiv4; w++) {
                value = (*pYSrcOffset);
                value2 = (*(pYSrcOffset + 1));
                *pYDstOffset = ((value & 0x000000ff) >> 0) | ((value & 0x00ff0000) >> 8) |
                        ((value2 & 0x000000ff) << 16) | ((value2 & 0x00ff0000) << 8);
                pYSrcOffset += 2;
                pYDstOffset += 1;
            }
        }
    }
}

int ImageProcess::resizeWrapper(ImxImageBuffer &srcBuf, ImxImageBuffer &dstBuf, ImxEngine engine) {
    int ret;
    ALOGV("enter resizeWrapper");

    if (srcBuf.mFormat != dstBuf.mFormat) {
        ALOGE("%s: format are differet, src 0x%x, dst 0x%x", __func__, srcBuf.mFormat,
              dstBuf.mFormat);
        return BAD_VALUE;
    }

    if ((srcBuf.mWidth == dstBuf.mWidth) && (srcBuf.mHeight == dstBuf.mHeight)) {
        ALOGE("%s: resolution are same, %dx%d", __func__, srcBuf.mWidth, srcBuf.mHeight);
        return BAD_VALUE;
    }

    if ((engine != ENG_G2D) && (engine != ENG_DPU))
        goto cpu_resize;

    ret = ConvertImageByG2D(dstBuf, srcBuf, engine);
    if (ret == 0) {
        ALOGV("%s: resize format 0x%x, res %dx%d to %dx%d by g2d ok", __func__, srcBuf.mFormat,
              srcBuf.mWidth, srcBuf.mHeight, dstBuf.mWidth, dstBuf.mHeight);
        return 0;
    }

cpu_resize:
    // cpu resize
    if (srcBuf.mFormat == HAL_PIXEL_FORMAT_YCBCR_422_I)
        ret = yuv422iResize((uint8_t *)srcBuf.mVirtAddr, srcBuf.mWidth, srcBuf.mHeight,
                            (uint8_t *)dstBuf.mVirtAddr, dstBuf.mWidth, dstBuf.mHeight);
    else if (srcBuf.mFormat == HAL_PIXEL_FORMAT_YCBCR_422_SP)
        ret = yuv422spResize((uint8_t *)srcBuf.mVirtAddr, srcBuf.mWidth, srcBuf.mHeight,
                             (uint8_t *)dstBuf.mVirtAddr, dstBuf.mWidth, dstBuf.mHeight, srcBuf.mHeightSpan);
    else if ((srcBuf.mFormat == HAL_PIXEL_FORMAT_YCBCR_420_888) ||
             (srcBuf.mFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP)) {
        ret = yuv420spResize((uint8_t *)srcBuf.mVirtAddr, srcBuf.mWidth, srcBuf.mHeight,
                             (uint8_t *)dstBuf.mVirtAddr, dstBuf.mWidth, dstBuf.mHeight);
    } else
        ALOGE("%s: resize by ENG_CPU, unsupported format 0x%x", __func__, srcBuf.mFormat);

    ALOGV("%s: resize format 0x%x, res %dx%d to %dx%d by cpu, ret %d", __func__, srcBuf.mFormat,
          srcBuf.mWidth, srcBuf.mHeight, dstBuf.mWidth, dstBuf.mHeight, ret);

    return ret;
}

} // namespace fsl
