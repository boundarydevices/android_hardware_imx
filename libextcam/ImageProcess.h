/*
 * Copyright 2018-2023 NXP.
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

typedef int (*hwc_func1)(void *handle);
typedef int (*hwc_func3)(void *handle, void *arg1, void *arg2);
typedef int (*hwc_func4)(void *handle, void *arg1, void *arg2, void *arg3);

enum ImgFormat { NV16, NV12, YUYV, I420 };

class ImageProcess {
public:
    static ImageProcess *getInstance();
    ~ImageProcess();

    int handleFrame(uint32_t width, uint32_t height,
                    ImgFormat dst_fmt, ImgFormat src_fmt, uint64_t dstPhyAddr = 0,
                    uint64_t srcPhyAddr = 0);

private:
    ImageProcess();
    void *getHandle();
    void getModule(char *path, const char *name);

    int handleNV12Frame(uint64_t dstPhyAddr, uint64_t srcPhyAddr, uint32_t width, uint32_t height,
                        ImgFormat dst_fmt);
    void cl_NV12toI420(void *g2dHandle, uint64_t srcPhyAddr, uint64_t dstPhyAddr, int width,
                       int height, bool bInputCached, bool bOutputCached);
    void cl_Copy(void *g2dHandle, uint64_t srcPhyAddr, uint64_t dstPhyAddr, uint32_t size,
                 bool bInputCached, bool bOutputCached);

    int handleNV16Frame(uint64_t dstPhyAddr, uint64_t srcPhyAddr, uint32_t width, uint32_t height,
                        ImgFormat dst_fmt);
    int cl_NV16Src(void *g2dHandle, uint64_t srcPhyAddr, uint64_t dstPhyAddr, int width, int height,
                   bool bInputCached, bool bOutputCached, ImgFormat dst_fmt);

    int handleYUYVFrameByG2D(uint64_t dstPhyAddr, uint64_t srcPhyAddr, uint32_t width,
                             uint32_t height, ImgFormat dst_fmt);
    int handleYUYVFrameByG3D(uint64_t dstPhyAddr, uint64_t srcPhyAddr, uint32_t width,
                             uint32_t height, ImgFormat dst_fmt);
    int handleYUYVFrame(uint32_t width, uint32_t height,
                        uint64_t dstPhyAddr, uint64_t srcPhyAddr, ImgFormat dst_fmt);
    void cl_YUYVtoI420(void *g2dHandle, uint64_t srcPhyAddr, uint64_t dstPhyAddr, int width,
                       int height, bool bInputCached, bool bOutputCached);

private:
    static Mutex sLock;
    static ImageProcess *sInstance;

    void *mCLModule;
    void *mCLHandle;
    hwc_func1 mCLOpen;
    hwc_func1 mCLClose;
    hwc_func3 mCLBlit;
    hwc_func4 mCLCopy;
    hwc_func1 mCLFlush;
    hwc_func1 mCLFinish;
    Mutex mCLLock;

    void *mG2dModule;
    void *mG2dHandle;
    hwc_func1 mOpenEngine;
    hwc_func1 mCloseEngine;
    hwc_func1 mFinishEngine;
    hwc_func4 mCopyEngine;
    hwc_func3 mBlitEngine;
    Mutex mG2dLock;
};

} // namespace fsl
#endif
