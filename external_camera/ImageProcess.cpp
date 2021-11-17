/*
 * Copyright 2021 NXP.
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

static bool getDefaultG2DLib(char *libName, int size) {
    char value[255];

    if((libName == NULL)||(size < (int)strlen(G2DENGINE) + (int)strlen(".so")))
        return false;

    memset(libName, 0, size);
    property_get("vendor.imx.default-g2d", value, "");
    if(strcmp(value, "") == 0) {
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

ImageProcess* ImageProcess::getInstance() {
    Mutex::Autolock _l(sLock);
    if (sInstance != NULL) {
        return sInstance;
    }

    sInstance = new ImageProcess();
    return sInstance;
}

ImageProcess::ImageProcess()
    : mG2dModule(NULL), mCLModule(NULL) {
    int32_t ret = -1;
    char path[PATH_MAX] = {0};
    char g2dlibName[PATH_MAX] = {0};
    if(getDefaultG2DLib(g2dlibName, PATH_MAX)) {
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
        ALOGI("No mCLModule to be used!\n");
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
        ALOGW("opencl g2d device is used!\n");
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
        closeEngine(mG2dHandle);
    }

    if (mG2dModule != NULL) {
        dlclose(mG2dModule);
    }

    sInstance = NULL;
}

int ImageProcess::closeEngine(void* handle) {
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

int ImageProcess::handleFrame(uint8_t *dstBuf, uint8_t *srcBuf, uint32_t width, uint32_t height, CscHw hw_type) {
    int ret = 0;

    switch (hw_type) {
    case GPU_3D:
        ret = handleFrameByGPU_3D(dstBuf, srcBuf, width, height);
        break;
    default:
        ALOGE("hw_type is not correct");
        return -EINVAL;
    }

    return ret;
}

int ImageProcess::handleFrameByGPU_3D(uint8_t *dstBuf, uint8_t *srcBuf, uint32_t width, uint32_t height) {
    if (mCLHandle == NULL) {
        ALOGE("mCLHandle is NULL!\n");
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
}