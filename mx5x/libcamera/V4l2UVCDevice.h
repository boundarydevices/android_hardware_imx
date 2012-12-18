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
#ifndef V4L2_UVC_DEVICE_H
#define V4L2_UVC_DEVICE_H

#include <linux/videodev2.h>
#include "V4l2CapDeviceBase.h"

#define MAX_DEV_NAME_LENGTH 10
#define MAX_CAPTURE_CONFIG  20
#define MAX_SUPPORTED_FMT  10
#define MAX_CSC_SUPPORT_FMT 1

namespace android{

struct CscConversion {
    //if the srcFormat is support by sensor.
    bool isSensorSupport;
    //if the dstFormat is support by sensor.
    bool isOverlapWithSensor;
    int width;
    int height;
    int srcStride;
    int dstStride;
    unsigned int srcFormat;
    unsigned int dstFormat;
    unsigned char* srcVirt;
    unsigned char* dstVirt;
    int srcPhy;
    int dstPhy;

    void(*cscConvert)(struct CscConversion* param);
};

class V4l2UVCDevice : public V4l2CapDeviceBase{
public:
    V4l2UVCDevice();//{mCameraType = CAMERA_TYPE_UVC;}
    ~V4l2UVCDevice(){}

protected:
    CAPTURE_DEVICE_RET V4l2Open(int cameraId);
    CAPTURE_DEVICE_RET V4l2RegisterBufs(DMA_BUFFER *DevBufQue, unsigned int *pBufQueNum);
    CAPTURE_DEVICE_RET V4l2Prepare();
    CAPTURE_DEVICE_RET V4l2Dequeue(unsigned int *pBufQueIdx);
    CAPTURE_DEVICE_RET V4l2Queue(unsigned int BufQueIdx);
    CAPTURE_DEVICE_RET V4l2DeAlloc();
    CAPTURE_DEVICE_RET V4l2EnumFmt(void *retParam);
    CAPTURE_DEVICE_RET V4l2EnumSizeFps(void *retParam);
    CAPTURE_DEVICE_RET V4l2SetConfig(struct capture_config_t *pCapcfg);

private:
    static void convertYUYUToNV12(struct CscConversion* param);
    unsigned int countActualCscFmt();
    bool needDoCsc(unsigned int);
    void selectCscFunction(unsigned int format);
    unsigned int queryCscSourceFormat(unsigned int format);
    //DMA_BUFFER mCameraBuffer[MAX_CAPTURE_BUF_QUE_NUM];
    //mCaptureBuffers defined in parent class store buffers allocated from user space.
    //mUvcBuffers store the buffers allocated from uvc driver.
    DMA_BUFFER mUvcBuffers[MAX_CAPTURE_BUF_QUE_NUM];

    //store sensor configuration here.
    unsigned int mCaptureConfigNum;
    struct capture_config_t mCaptureConfig[MAX_CAPTURE_CONFIG];
    struct capture_config_t* mCurrentConfig;

    //for jpeg encoder support yuyv. this case, should not covert.
    bool mEnableCSC;
    //stores nedd csc format. 
    struct CscConversion mCscGroup[MAX_CSC_SUPPORT_FMT];
    struct CscConversion* mDoCsc;
    unsigned int mActualCscFmt[MAX_CSC_SUPPORT_FMT];
    unsigned int mActualCscFmtCnt;
    //stores sensor support format.
    unsigned int mSensorSupportFmt[MAX_SUPPORTED_FMT];
    unsigned int mSensorFmtCnt;
    unsigned int mCscFmtCnt;
};

};
#endif

