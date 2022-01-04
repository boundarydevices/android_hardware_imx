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

#ifndef _FSL_IMAGE_PROCESS_H
#define _FSL_IMAGE_PROCESS_H

#include <stdint.h>
#include <utils/Mutex.h>
#include "ExternalCameraUtils.h"

namespace fsl {
using namespace android;

typedef int (*hwc_func1)(void* handle);
typedef int (*hwc_func3)(void* handle, void* arg1, void* arg2);
typedef int (*hwc_func4)(void* handle, void* arg1, void* arg2, void* arg3);

enum SrcFormat {
    NV16,
    NV12,
};

class ImageProcess {
public:
    static ImageProcess* getInstance();
    ~ImageProcess();

    int handleFrame(uint8_t *dstBuf, uint8_t *srcBuf, uint32_t width, uint32_t height, SrcFormat src_fmt);

private:
    ImageProcess();
    void *getHandle();
    void getModule(char *path, const char *name);

    int handleNV12Frame(uint8_t *dstBuf, uint8_t *srcBuf, uint32_t width, uint32_t height);
    void cl_NV12toI420(void *g2dHandle, uint8_t *inputBuffer,
            uint8_t *outputBuffer, int width, int height, bool bInputCached, bool bOutputCached);

    int handleNV16Frame(uint8_t *dstBuf, uint8_t *srcBuf, uint32_t width, uint32_t height);
    void cl_NV16toI420(void *g2dHandle, uint8_t *inputBuffer,
            uint8_t *outputBuffer, int width, int height, bool bInputCached, bool bOutputCached);

private:
    static Mutex sLock;
    static ImageProcess* sInstance;

    void *mCLModule;
    void *mCLHandle;
    hwc_func1 mCLOpen;
    hwc_func1 mCLClose;
    hwc_func3 mCLBlit;
    hwc_func1 mCLFlush;
    hwc_func1 mCLFinish;
    Mutex mCLLock;
};

}
#endif