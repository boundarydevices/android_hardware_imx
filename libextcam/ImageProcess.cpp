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
//#define LOG_NDEBUG 0
#define LOG_TAG "ImageProcess"

#include <stdio.h>
#include <dlfcn.h>
#include <cutils/log.h>
#include <cutils/properties.h>
#include <g2d.h>

#include "ImageProcess.h"
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

ImageProcess* ImageProcess::sInstance(0);
Mutex ImageProcess::sLock(Mutex::PRIVATE);

ImageProcess* ImageProcess::getInstance() {
    Mutex::Autolock _l(sLock);
    if (sInstance != NULL) {
        return sInstance;
    }

    sInstance = new ImageProcess();
    return sInstance;
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

ImageProcess::ImageProcess()
    : mCLModule(NULL) {
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
        mCLHandle = NULL;
    } else {
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

    if(mCLHandle != NULL) {
        ALOGI("%s: opencl g2d device is used!\n", __func__);
    }

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

    if(mG2dHandle != NULL) {
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

int ImageProcess::handleFrame(uint8_t *dstBuf, uint8_t *srcBuf, uint32_t width, uint32_t height,
        SrcFormat src_fmt, uint64_t dstPhyAddr, uint64_t srcPhyAddr) {
    int ret = 0;

    switch (src_fmt) {
    case NV12:
        ALOGV("%s: handle NV12 Frame\n", __func__);
        ret = handleNV12Frame(dstBuf, srcBuf, width, height);
        break;
    case NV16:
        ALOGV("%s: handle NV16 Frame!\n", __func__);
        ret = handleNV16Frame(dstBuf, srcBuf, width, height);
        break;
    case YUYV:
        ALOGV("%s: handle YUYV Frame!\n", __func__);
        ret = handleYUYVFrame(dstBuf, srcBuf, width, height, dstPhyAddr, srcPhyAddr);
        break;
    default:
        ALOGE("%s: src_fmt cannot be handled!\n", __func__);
        return -EINVAL;
    }

    return ret;
}

int ImageProcess::handleNV12Frame(uint8_t *dstBuf, uint8_t *srcBuf, uint32_t width, uint32_t height) {
    if (mCLHandle == NULL) {
        ALOGE("%s: mCLHandle is NULL!\n", __func__);
        return -EINVAL;
    }

    bool bOutputCached = true;
    //diffrent format, same resolution
    {
        Mutex::Autolock _l(mCLLock);
        cl_NV12toI420(mCLHandle, srcBuf, dstBuf, width, height, false, bOutputCached);
        (*mCLFlush)(mCLHandle);
        (*mCLFinish)(mCLHandle);
    }

    return 0;
}

void ImageProcess::cl_NV12toI420(void *g2dHandle, uint8_t *inputBuffer,
         uint8_t *outputBuffer, int width, int height, bool bInputCached, bool bOutputCached) {
    struct cl_g2d_surface src, dst;

    src.format = CL_G2D_NV12;
    src.usage = bInputCached ? CL_G2D_CPU_MEMORY : CL_G2D_DEVICE_MEMORY;
    src.planes[0] = (long)inputBuffer;
    src.planes[1] = (long)inputBuffer + width * height;
    src.left = 0;
    src.top = 0;
    src.right = width;
    src.bottom = height;
    src.stride = width;
    src.width  = width;
    src.height = height;

    dst.format = CL_G2D_I420;
    dst.usage = bOutputCached ? CL_G2D_CPU_MEMORY : CL_G2D_DEVICE_MEMORY;
    dst.planes[0] = (long)outputBuffer;
    dst.planes[1] = (long)outputBuffer + width * height;
    dst.planes[2] = (long)outputBuffer + width * height * 5 / 4;
    dst.left = 0;
    dst.top = 0;
    dst.right = width;
    dst.bottom = height;
    dst.stride = width;
    dst.width  = width;
    dst.height = height;

    (*mCLBlit)(g2dHandle, (void*)&src, (void*)&dst);
}

int ImageProcess::handleNV16Frame(uint8_t *dstBuf, uint8_t *srcBuf, uint32_t width, uint32_t height) {
    if (mCLHandle == NULL) {
        ALOGE("%s: mCLHandle is NULL!\n", __func__);
        return -EINVAL;
    }

    bool bOutputCached = true;
    //diffrent format, same resolution
    {
        Mutex::Autolock _l(mCLLock);
        cl_NV16toI420(mCLHandle, srcBuf, dstBuf, width, height, false, bOutputCached);
        (*mCLFlush)(mCLHandle);
        (*mCLFinish)(mCLHandle);
    }

    return 0;
}

void ImageProcess::cl_NV16toI420(void *g2dHandle, uint8_t *inputBuffer,
         uint8_t *outputBuffer, int width, int height, bool bInputCached, bool bOutputCached) {
    struct cl_g2d_surface src, dst;

    src.format = CL_G2D_NV16;
    src.usage = bInputCached ? CL_G2D_CPU_MEMORY : CL_G2D_DEVICE_MEMORY;
    src.planes[0] = (long)inputBuffer;
    src.planes[1] = (long)inputBuffer + width * height;
    src.left = 0;
    src.top = 0;
    src.right = width;
    src.bottom = height;
    src.stride = width;
    src.width  = width;
    src.height = height;

    dst.format = CL_G2D_I420;
    dst.usage = bOutputCached ? CL_G2D_CPU_MEMORY : CL_G2D_DEVICE_MEMORY;
    dst.planes[0] = (long)outputBuffer;
    dst.planes[1] = (long)outputBuffer + width * height;
    dst.planes[2] = (long)outputBuffer + width * height * 5 / 4;
    dst.left = 0;
    dst.top = 0;
    dst.right = width;
    dst.bottom = height;
    dst.stride = width;
    dst.width  = width;
    dst.height = height;

    (*mCLBlit)(g2dHandle, (void*)&src, (void*)&dst);
}

int ImageProcess::handleYUYVFrameByG3D(uint8_t *dstBuf, uint8_t *srcBuf, uint32_t width, uint32_t height) {

    if (mCLHandle == NULL) {
        ALOGE("%s: mCLHandle is NULL!\n", __func__);
        return -EINVAL;
    }

    bool bOutputCached = true;
    //diffrent format, same resolution
    {
        Mutex::Autolock _l(mCLLock);
        cl_YUYVtoI420(mCLHandle, srcBuf, dstBuf, width, height, false, bOutputCached);
        (*mCLFlush)(mCLHandle);
        (*mCLFinish)(mCLHandle);
    }

    return 0;
}

int ImageProcess::handleYUYVFrame(uint8_t *dstBuf, uint8_t *srcBuf, uint32_t width, uint32_t height, uint64_t dstPhyAddr, uint64_t srcPhyAddr) {
    if (mG2dHandle)
      return handleYUYVFrameByG2D(dstPhyAddr, srcPhyAddr, width, height);
    else
      return handleYUYVFrameByG3D(dstBuf, srcBuf, width, height);
}

void ImageProcess::cl_YUYVtoI420(void *g2dHandle, uint8_t *inputBuffer,
         uint8_t *outputBuffer, int width, int height, bool bInputCached, bool bOutputCached) {
    struct cl_g2d_surface src, dst;

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

    dst.format = CL_G2D_I420;
    dst.usage = bOutputCached ? CL_G2D_CPU_MEMORY : CL_G2D_DEVICE_MEMORY;
    dst.planes[0] = (long)outputBuffer;
    dst.planes[1] = (long)outputBuffer + width * height;
    dst.planes[2] = (long)outputBuffer + width * height * 5 / 4;
    dst.left = 0;
    dst.top = 0;
    dst.right = width;
    dst.bottom = height;
    dst.stride = width;
    dst.width  = width;
    dst.height = height;

    (*mCLBlit)(g2dHandle, (void*)&src, (void*)&dst);
}

int ImageProcess::handleYUYVFrameByG2D(uint64_t dstPhyAddr, uint64_t srcPhyAddr, uint32_t width, uint32_t height)
{
    if (mBlitEngine == NULL) {
        return -EINVAL;
    }

    int ret;
    void* g2dHandle = mG2dHandle;
    struct g2d_surface s_surface, d_surface;

    s_surface.format = G2D_YUYV;
    s_surface.planes[0] = (long)srcPhyAddr;
    s_surface.left = 0;
    s_surface.top = 0;
    s_surface.right = width;
    s_surface.bottom = height;
    s_surface.stride = width;
    s_surface.width  = width;
    s_surface.height = height;
    s_surface.rot    = G2D_ROTATION_0;

    d_surface.format = G2D_I420;
    d_surface.planes[0] = (long)dstPhyAddr;
    d_surface.planes[1] = d_surface.planes[0] + width * height;
    d_surface.planes[2] = d_surface.planes[1] + width * height / 4;
    d_surface.left = 0;
    d_surface.top = 0;
    d_surface.right = width;
    d_surface.bottom = height;
    d_surface.stride = width;
    d_surface.width  = width;
    d_surface.height = height;
    d_surface.rot    = G2D_ROTATION_0;

    Mutex::Autolock _l(mG2dLock);
    ret = mBlitEngine(g2dHandle, (void*)&s_surface, (void*)&d_surface);
    if (ret)
        goto finish_blit;

    mFinishEngine(g2dHandle);

finish_blit:
    return ret;
}

}

