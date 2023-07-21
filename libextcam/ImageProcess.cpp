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
        ALOGW("%s: opencl g2d device is used!\n", __func__);
    }
}

ImageProcess::~ImageProcess() {
    if (mCLHandle != NULL) {
        (*mCLClose)(mCLHandle);
    }

    if (mCLModule != NULL) {
        dlclose(mCLModule);
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

int ImageProcess::handleFrame(uint8_t *dstBuf, uint8_t *srcBuf, uint32_t width, uint32_t height, SrcFormat src_fmt) {
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
}

