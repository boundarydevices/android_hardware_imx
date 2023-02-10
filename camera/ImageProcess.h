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
//#include "ImxStream.h"
#include "CameraConfigurationParser.h"

namespace fsl {

using namespace android;
using namespace cameraconfigparser;

typedef int (*hwc_func1)(void* handle);
typedef int (*hwc_func3)(void* handle, void* arg1, void* arg2);
typedef int (*hwc_func4)(void* handle, void* arg1, void* arg2, void* arg3);

class ImageProcess
{
public:
    static ImageProcess* getInstance();
    ~ImageProcess();

    int handleFrame(ImxStreamBuffer& dst, ImxStreamBuffer& src, CscHw hw_type);
    int resizeWrapper(ImxStreamBuffer& src, ImxStreamBuffer& dst, CscHw hw_type);

private:
    int convertNV12toNV21(ImxStreamBuffer& dst, ImxStreamBuffer& src);
    int handleFrameByPXP(ImxStreamBuffer& dst, ImxStreamBuffer& src);
    int handleFrameByIPU(ImxStreamBuffer& dst, ImxStreamBuffer& src);
    int handleFrameByG2DCopy(ImxStreamBuffer& dst, ImxStreamBuffer& src);
    int handleFrameByG2DBlit(ImxStreamBuffer& dst, ImxStreamBuffer& src);
    int handleFrameByGPU_3D(ImxStreamBuffer& dst, ImxStreamBuffer& src);
    int handleFrameByCPU(ImxStreamBuffer& dst, ImxStreamBuffer& src);
    int handleFrameByDPU(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf);
    int handleFrameByGPU_2D(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf);
    int handleFrameByG2D(ImxStreamBuffer& dstBuf, ImxStreamBuffer& srcBuf, CscHw hw_type);
    void YUYVCopyByLine(uint8_t *dst, uint32_t dstWidth, uint32_t dstHeight,
             uint8_t *src, uint32_t srcWidth, uint32_t srcHeight);
    void convertYUYVtoNV12SP(uint8_t *inputBuffer, uint8_t *outputBuffer,
             int width, int height);
    void convertNV12toYV12(uint8_t *inputBuffer, uint8_t *outputBuffer,
             int width, int height);

    void cl_Copy(void *g2dHandle, uint8_t *output, uint8_t *input, uint32_t size, bool bInputCached, bool bOutputCached);

    void cl_YUYVCopyByLine(void *g2dHandle,
             uint8_t *dst, uint32_t dstWidth,
             uint32_t dstHeight, uint8_t *src,
             uint32_t srcWidth, uint32_t srcHeight, bool bInputCached, bool bOutputCached);
    void cl_YUYVtoNV12SP(void *g2dHandle, uint8_t *inputBuffer,
             uint8_t *outputBuffer, int width, int height, bool bInputCached, bool bOutputCached);
    void *getHandle();
    int openEngine(void** handle);
    int closeEngine(void* handle);
    void getModule(char *path, const char *name);

private:
    ImageProcess();
    static Mutex sLock;
    static ImageProcess* sInstance;

    int mIpuFd;
    int mPxpFd;
    int mChannel;

    void *mG2dModule;
    void *mG2dHandle;
    hwc_func1 mOpenEngine;
    hwc_func1 mCloseEngine;
    hwc_func1 mFinishEngine;
    hwc_func4 mCopyEngine;
    hwc_func3 mBlitEngine;
    Mutex mG2dLock;

    void *mCLModule;
    void *mCLHandle;
    hwc_func1 mCLOpen;
    hwc_func1 mCLClose;
    hwc_func3 mCLBlit;
    hwc_func4 mCLCopy;
    hwc_func1 mCLFlush;
    hwc_func1 mCLFinish;
    Mutex mCLLock;
};

}
#endif
