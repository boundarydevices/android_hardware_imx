/*
 * Copyright (C) 2008 The Android Open Source Project
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

/*
 * Copyright 2009-2012 Freescale Semiconductor, Inc.
 */
#ifndef V4L2_CAP_DEVICE_BASE_H
#define V4L2_CAP_DEVICE_BASE_H
#include <linux/videodev2.h>

#include "CaptureDeviceInterface.h"

namespace android{

    class V4l2CapDeviceBase : public CaptureDeviceInterface{
    public:

        virtual CAPTURE_DEVICE_RET SetDevName(const char * deviceName, const char *devPath = NULL);
        virtual CAPTURE_DEVICE_RET GetDevName(char * deviceName);
        virtual CAPTURE_DEVICE_RET GetDevType(CAMERA_TYPE *pType);
        virtual CAPTURE_DEVICE_RET DevOpen(int cameraId);
        virtual CAPTURE_DEVICE_RET EnumDevParam(DevParamType devParamType, void *retParam);
        virtual CAPTURE_DEVICE_RET DevSetConfig(struct capture_config_t *pCapcfg);
        virtual CAPTURE_DEVICE_RET DevAllocateBuf(DMA_BUFFER *DevBufQue, unsigned int *pBufQueNum);
        virtual CAPTURE_DEVICE_RET DevRegisterBufs(DMA_BUFFER *DevBufQue, unsigned int *pBufQueNum);
        virtual CAPTURE_DEVICE_RET DevPrepare();
        virtual CAPTURE_DEVICE_RET DevStart();
        virtual CAPTURE_DEVICE_RET DevDequeue(unsigned int *pBufQueIdx);
        virtual CAPTURE_DEVICE_RET DevQueue( unsigned int BufQueIdx);
        virtual CAPTURE_DEVICE_RET DevStop();
        virtual CAPTURE_DEVICE_RET DevDeAllocate();
        virtual CAPTURE_DEVICE_RET DevClose();

    protected:

        V4l2CapDeviceBase();
        virtual ~V4l2CapDeviceBase();
        virtual CAPTURE_DEVICE_RET V4l2Open(int cameraId);
        virtual CAPTURE_DEVICE_RET V4l2EnumParam(DevParamType devParamType, void *retParam);
        virtual CAPTURE_DEVICE_RET V4l2EnumFmt(void *retParam);
        virtual CAPTURE_DEVICE_RET V4l2EnumSizeFps(void *retParam);
        virtual CAPTURE_DEVICE_RET V4l2SetConfig(struct capture_config_t *pCapcfg);
        virtual CAPTURE_DEVICE_RET V4l2AllocateBuf(DMA_BUFFER *DevBufQue, unsigned int *pBufQueNum);
        virtual CAPTURE_DEVICE_RET V4l2RegisterBufs(DMA_BUFFER *DevBufQue, unsigned int *pBufQueNum);
        virtual CAPTURE_DEVICE_RET V4l2Prepare();
        virtual CAPTURE_DEVICE_RET V4l2Start();
        virtual CAPTURE_DEVICE_RET V4l2Dequeue(unsigned int *pBufQueIdx);
        virtual CAPTURE_DEVICE_RET V4l2Queue(unsigned int BufQueIdx);
        virtual CAPTURE_DEVICE_RET V4l2Stop();
        virtual CAPTURE_DEVICE_RET V4l2DeAlloc();
        virtual CAPTURE_DEVICE_RET V4l2Close();
        virtual CAPTURE_DEVICE_RET V4l2ConfigInput(struct capture_config_t *pCapcfg);
        virtual CAPTURE_DEVICE_RET V4l2GetCaptureMode(struct capture_config_t *pCapcfg, unsigned int *pMode);
        virtual CAPTURE_DEVICE_RET V4l2SetRot(struct capture_config_t *pCapcfg);

        char         mCaptureDeviceName[CAMAERA_FILENAME_LENGTH];
        char         mInitalDeviceName[CAMERA_SENSOR_LENGTH];
        int          mCameraDevice;
        unsigned int mFmtParamIdx;
        unsigned int mSizeFPSParamIdx;
        unsigned int mRequiredFmt;
        unsigned int mBufQueNum;
        int          mQueuedBufNum;
        DMA_BUFFER mCaptureBuffers[MAX_CAPTURE_BUF_QUE_NUM];
        struct   capture_config_t mCapCfg;
        CAMERA_TYPE  mCameraType;

    };
};

#endif
