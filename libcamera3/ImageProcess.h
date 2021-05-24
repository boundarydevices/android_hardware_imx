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

#ifndef _FSL_IMAGE_PROCESS_H
#define _FSL_IMAGE_PROCESS_H

#include <stdint.h>
#include <utils/Mutex.h>
#include "Stream.h"
#include "utils/CameraConfigurationParser.h"

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

    int handleFrame(StreamBuffer& dst, StreamBuffer& src, CscHw);

private:
    int convertNV12toNV21(StreamBuffer& dst, StreamBuffer& src);
    int handleFrameByPXP(StreamBuffer& dst, StreamBuffer& src);
    int handleFrameByIPU(StreamBuffer& dst, StreamBuffer& src);
    int handleFrameByG2DCopy(StreamBuffer& dst, StreamBuffer& src);
    int handleFrameByG2DBlit(StreamBuffer& dst, StreamBuffer& src);
    int handleFrameByGPU_3D(StreamBuffer& dst, StreamBuffer& src);
    int handleFrameByCPU(StreamBuffer& dst, StreamBuffer& src);
    int handleFrameByDPU(StreamBuffer& dstBuf, StreamBuffer& srcBuf);
    int handleFrameByGPU_2D(StreamBuffer& dstBuf, StreamBuffer& srcBuf);
    int handleFrameByG2D(StreamBuffer& dstBuf, StreamBuffer& srcBuf);
    void YUYVCopyByLine(uint8_t *dst, uint32_t dstWidth, uint32_t dstHeight,
             uint8_t *src, uint32_t srcWidth, uint32_t srcHeight);
    void convertYUYVtoNV12SP(uint8_t *inputBuffer, uint8_t *outputBuffer,
             int width, int height);
    void cl_YUYVCopyByLine(void *g2dHandle,
             uint8_t *dst, uint32_t dstWidth,
             uint32_t dstHeight, uint8_t *src,
             uint32_t srcWidth, uint32_t srcHeight, bool bInputCached);
    void cl_YUYVtoNV12SP(void *g2dHandle, uint8_t *inputBuffer,
             uint8_t *outputBuffer, int width, int height);
    void *getHandle();
    int openEngine(void** handle);
    int closeEngine(void* handle);
    void getModule(char *path, const char *name);

private:
    ImageProcess();
    static Mutex sLock;
    static ImageProcess* sInstance;
    static thread_local void *sHandle;

    int mIpuFd;
    int mPxpFd;
    int mChannel;
    int m2DEnable;

    hwc_func1 mOpenEngine;
    hwc_func1 mCloseEngine;
    hwc_func1 mFinishEngine;
    hwc_func4 mCopyEngine;
    hwc_func3 mBlitEngine;

    void *mG2dModule;
    void *mCLModule;
    void *mCLHandle;
    hwc_func1 mCLOpen;
    hwc_func1 mCLClose;
    hwc_func3 mCLBlit;
    hwc_func1 mCLFlush;
    hwc_func1 mCLFinish;
};

}
#endif
