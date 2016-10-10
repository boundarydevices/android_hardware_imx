/*
 * Copyright (C) 2016 Freescale Semiconductor, Inc.
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
#ifndef _UVCMJPEG_H
#define _UVCMJPEG_H

#include "USPStream.h"
#include "vpu_wrapper.h"
#include "DMAStream.h"

#define UVC_USE_MJPG "uvc_mjpg"

#define VPU_DEC_MAX_NUM_MEM_NUM 20
#define DEFAULT_FILL_DATA_UNIT  (16*1024)
#define DEFAULT_DELAY_BUFSIZE   (-1)
#define Align(ptr,align)     (((uintptr_t)ptr+(align)-1)/(align)*(align))
#define MAX_PREVIEW_BUFFER      8
#define MAX_FRAME_NUM                (30)
#define FRAME_SURPLUS                (0)
#define FRAME_ALIGN          (16)

typedef struct
{
    //virtual mem info
    int nVirtNum;
    unsigned int virtMem[VPU_DEC_MAX_NUM_MEM_NUM];

    //phy mem info
    int nPhyNum;
    unsigned int phyMem_virtAddr[VPU_DEC_MAX_NUM_MEM_NUM];
    unsigned int phyMem_phyAddr[VPU_DEC_MAX_NUM_MEM_NUM];
    unsigned int phyMem_cpuAddr[VPU_DEC_MAX_NUM_MEM_NUM];
    unsigned int phyMem_size[VPU_DEC_MAX_NUM_MEM_NUM];
}DecMemInfo;

typedef enum
{
    DEC_OUT_420,
    DEC_OUT_422H,
    DEC_OUT_422V,
    DEC_OUT_444,
    DEC_OUT_400,
    DEC_OUT_UNKNOWN
}DecOutColorFmt;


typedef struct
{
    // input setting
    int nCodec;

    // output info
    DecOutColorFmt eOutColorFmt;

    //advance option
    int nChromaInterleave;
    //int eColorFormat;
    int nMapType;
    int nTile2LinearEnable;
}DecContxt;

// stream uses DMABUF buffers which allcated in user space.
// that exports DMABUF handle.
class MJPGStream : public DMAStream
{
public:
    MJPGStream(Camera* device);
    virtual ~MJPGStream();

    StreamBuffer* mSensorBuffers[MAX_STREAM_BUFFERS];
    // configure device.
    virtual int32_t onDeviceConfigureLocked();
    // start device.
    virtual int32_t onDeviceStartLocked();
    // stop device.
    virtual int32_t onDeviceStopLocked();

    // get buffer from V4L2.
    virtual int32_t onFrameAcquireLocked();
    // put buffer back to V4L2.
    virtual int32_t onFrameReturnLocked(int32_t index, StreamBuffer& buf);

    // allocate buffers.
    virtual int32_t allocateBuffersLocked(){return 0;}
    int32_t allocateSensorBuffersLocked();
    // free buffers.
    virtual int32_t freeBuffersLocked(){return 0;}
    int32_t freeSensorBuffersLocked();

    // get device buffer required size.
    virtual int32_t getDeviceBufferSize();


    int VPUDec( unsigned char *InVirAddr, unsigned int inLen, unsigned int nUVCBufIdx);
    int ProcessInitInfo(VpuDecInitInfo* pInitInfo, DecMemInfo* pDecMemInfo, int*pOutFrmNum, unsigned char**, int32_t*);
    int FreeMemBlock(DecMemInfo* pDecMem);

    int MallocMemBlock(VpuMemInfo* pMemBlock,DecMemInfo* pDecMem);
    int ConvertCodecFormat(int codec, VpuCodStd* pCodec);



    unsigned char *mVPUPhyAddr[MAX_PREVIEW_BUFFER];
    unsigned char *mVPUVirtAddr[MAX_PREVIEW_BUFFER];

private:
    int VPUInit();
    int VPUExit();

private:
    int32_t mStreamSize;
    VpuDecHandle mVPUHandle;
    DecMemInfo mDecMemInfo;
    DecContxt mDecContxt;
    mutable Mutex mVPULock;
    DecOutColorFmt meOutColorFmt;
};

#endif
