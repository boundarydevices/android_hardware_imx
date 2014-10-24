/*
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
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

#ifndef _UVC_MJPG_DEVICE_H
#define _UVC_MJPG_DEVICE_H

#include "UvcDevice.h"
#include "vpu_wrapper.h"

#define UVC_USE_MJPG "uvc_mjpg"

#define VPU_DEC_MAX_NUM_MEM_NUM	20
#define MAX_FRAME_NUM		(30)
#define FRAME_SURPLUS		(0)
#define FRAME_ALIGN		(16)

#define Align(ptr,align)	(((unsigned int)ptr+(align)-1)/(align)*(align))

#define DEFAULT_FILL_DATA_UNIT	(16*1024)
#define DEFAULT_DELAY_BUFSIZE	(-1)
#define FILE_MODE_MAX_FRAME_LEN	(1024*1024)	//1M bytes


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
	FILE* fin;
	FILE* fout;
	int nMaxNum;
	int nDisplay;	
	int nFbNo;
	int nCodec;
	int nInWidth;
	int nInHeight;
	int nSkipMode;
	int nDelayBufSize; /*<0: invalid*/

	// internal testing for repeat
	int nRepeatNum;
	int nOffset;
	int nUnitDataSize;
	int nUintDataNum;

	// output info
	int nWidth;
	int nHeight;
	int nFrameNum;
	int nErr;
	DecOutColorFmt eOutColorFmt;
	int nDecFps;
	int nTotalFps;

	//advance option
	int nChromaInterleave;
	int nMapType;
	int nTile2LinearEnable;
}DecContxt;



//uvc resolution map to vpu resolution
//since vpu need 16 pixels align
typedef struct tagResMap {
    int nUVCWidth;
    int nUVCHeight;
    int nVPUWidth;
    int nVPUHeight;    
}TResMap;


class UvcMJPGDevice : public UvcDevice {
public:
    UvcMJPGDevice();
    ~UvcMJPGDevice();

    virtual status_t initSensorInfo(const CameraInfo& info);
    virtual status_t setDeviceConfig(int         width,
                                     int         height,
                                     PixelFormat format,
                                     int         fps);
    virtual void setPreviewPixelFormat();
    virtual status_t fillCameraFrame(CameraFrame *frame);
    virtual CameraFrame * acquireCameraFrame();
    virtual status_t startDeviceLocked();
    virtual status_t stopDeviceLocked();

	virtual bool     UseMJPG() { return true; }

private:
    int VPUInit();
    int VPUExit();
    //CameraFrame *VPUDec(CameraFrame **ppDeviceBuf, unsigned int BufNum, void *OutVirBuf, void *InVirBuf, unsigned int outSize, unsigned int w, unsigned int h);
    CameraFrame *VPUDec(CameraFrame **ppDeviceBuf, unsigned int BufNum, u8 *InVirAddr, u32 inLen, unsigned int nUVCBufIdx, unsigned int w, unsigned int h);
    int ProcessInitInfo(VpuDecInitInfo* pInitInfo, DecMemInfo* pDecMemInfo, int*pOutFrmNum,
         unsigned char **pPhyAddr, unsigned char **pVirtAddr, unsigned int registerNum);
    int FreeMemBlock(DecMemInfo* pDecMem);
    int MallocMemBlock(VpuMemInfo* pMemBlock,DecMemInfo* pDecMem);
    int ConvertCodecFormat(int codec, VpuCodStd* pCodec);

    status_t GetUVCResFromVPURes(int vpuWidth, int vpuHeight, int *pnUVCWidth, int *pnUVCHeight);
        
private:
    
  // VPU related parameters
    VpuDecHandle mVPUHandle;
    DecMemInfo mDecMemInfo;    
    DecContxt mDecContxt;   
    DecOutColorFmt meOutColorFmt;
    mutable Mutex mVPULock;

    int mResCount;
    TResMap mResMap[MAX_RESOLUTION_SIZE];
};




#endif // ifndef _UVC_MJPG_DEVICE_H

