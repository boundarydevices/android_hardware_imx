/*
 * Copyright 2023-2025 NXP.
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
#include <cutils/native_handle.h>
#include "ImageUtils.h"

namespace fsl {

using namespace android;

typedef int (*hwc_func1)(void* handle);
typedef int (*hwc_func3)(void* handle, void* arg1, void* arg2);
typedef int (*hwc_func4)(void* handle, void* arg1, void* arg2, void* arg3);

typedef OCL_RESULT (*ocl_open)(OCL_OPEN_FLAG flag, OCL_HANDLE* handle);
typedef OCL_RESULT (*ocl_setParam)(OCL_HANDLE handle, OCL_PARAM_INDEX index, void* param);
typedef OCL_RESULT (*ocl_getParam)(OCL_HANDLE handle, OCL_PARAM_INDEX index, void* param);
typedef OCL_RESULT (*ocl_convert)(OCL_HANDLE handle, OCL_BUFFER* in_buf, OCL_BUFFER* out_buf);
typedef OCL_RESULT (*ocl_close)(OCL_HANDLE handle);

class ImageProcess {
public:
    static ImageProcess* getInstance();
    ~ImageProcess();

    int ConvertImage(ImxImageBuffer& dst, ImxImageBuffer& src, ImxEngine engine,
                     bool debug = false);
    void SetMiddleBuffers(std::vector<ImxImageBuffer *> &MiddleBuffers);

    buffer_handle_t createBufferHandle(ImxImageBuffer& imxBuf);
    void destroyBufferHandle(buffer_handle_t buffer);

private:
    int convertNV12toNV21(ImxImageBuffer& dst, ImxImageBuffer& src);
    int ConvertImageByPXP(ImxImageBuffer& dst, ImxImageBuffer& src);
    int ConvertImageByIPU(ImxImageBuffer& dst, ImxImageBuffer& src);
    int ConvertImageByG2DCopy(ImxImageBuffer& dst, ImxImageBuffer& src);
    int ConvertImageByG2DBlit(ImxImageBuffer& dst, ImxImageBuffer& src);
    int ConvertImageByGPU_3D(ImxImageBuffer& dst, ImxImageBuffer& src);
    int ConvertImageByCPU(ImxImageBuffer& dst, ImxImageBuffer& src);
    int ConvertImageByDPU(ImxImageBuffer& dstBuf, ImxImageBuffer& srcBuf);
    int ConvertImageByGPU_2D(ImxImageBuffer& dstBuf, ImxImageBuffer& srcBuf);
    int ConvertImageByG2D(ImxImageBuffer& dstBuf, ImxImageBuffer& srcBuf, ImxEngine engine);
    void convertYUYVtoNV12SP(uint8_t* inputBuffer, uint8_t* outputBuffer, int width, int height,
                             int srcStride, int dstStride);
    void convertNV12toYV12(uint8_t* inputBuffer, uint8_t* outputBuffer, int width, int height);
    int resizeWrapper(ImxImageBuffer& src, ImxImageBuffer& dst, ImxEngine engine);

    void cl_Copy(void* g2dHandle, uint8_t* output, uint8_t* input, uint32_t size, bool bInputCached,
                 bool bOutputCached, bool bUsePhyAddr = true);
    void cl_Csc(void* g2dHandle, uint8_t* inputBuffer, uint8_t* outputBuffer, int width, int height,
                int srcStride, int dstStride, int srcHeightSpan, bool bInputCached,
                bool bOutputCached, uint32_t inFmt, uint32_t outFmt, bool bUsePhyAddr = true);

    void* getHandle();
    int openEngine(void** handle);
    int closeEngine(void* handle);
    void getModule(char* path, const char* name);
    bool getDefaultG2DLib(char *libName, int size);
    void LockG2dAddr(ImxImageBuffer& imxBuf);
    void UnLockG2dAddr(ImxImageBuffer& imxBuf);

    int ConvertImageByOclCvt(ImxImageBuffer& dst, ImxImageBuffer& src);
    void ImxImageBufferToOclBuffer(ImxImageBuffer& imxImgBuf, OCL_BUFFER& oclBuf,
                                   OCL_FORMAT& oclFmt);

private:
    ImageProcess();
    static Mutex sLock;
    static ImageProcess* sInstance;
    bool mDebug = false;

    typedef int (ImageProcess::*ConvertByEngine)(ImxImageBuffer&, ImxImageBuffer&);
    ConvertByEngine g_EngFuncList[ENG_NUM] = {&ImageProcess::ConvertImageByGPU_2D,
                                              &ImageProcess::ConvertImageByDPU,
                                              &ImageProcess::ConvertImageByGPU_3D,
                                              &ImageProcess::ConvertImageByOclCvt,
                                              &ImageProcess::ConvertImageByIPU,
                                              &ImageProcess::ConvertImageByPXP,
                                              &ImageProcess::ConvertImageByCPU};

    int mIpuFd;
    int mPxpFd;
    int mChannel;

    void* mG2dModule;
    void* mG2dHandle;
    hwc_func1 mOpenEngine;
    hwc_func1 mCloseEngine;
    hwc_func1 mFinishEngine;
    hwc_func4 mCopyEngine;
    hwc_func3 mBlitEngine;
    Mutex mG2dLock;

    void* mHelperHandle;
    hwc_func1 mLockSurface;
    hwc_func1 mUnlockSurface;

    void* mCLModule;
    void* mCLHandle;
    hwc_func1 mCLOpen;
    hwc_func1 mCLClose;
    hwc_func3 mCLBlit;
    hwc_func4 mCLCopy;
    hwc_func1 mCLFlush;
    hwc_func1 mCLFinish;
    Mutex mCLLock;

    bool mbVIVG2D;

    void* mImxOclCvtModule;
    OCL_HANDLE mHOcl;
    ocl_open m_ocl_open;
    ocl_setParam m_ocl_setParam;
    ocl_getParam m_ocl_getParam;
    ocl_convert m_ocl_convert;
    ocl_close m_ocl_close;
};

} // namespace fsl
#endif
