/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2012-2016 Freescale Semiconductor, Inc.
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

#include "YuvToJpegEncoder.h"
#include <ui/PixelFormat.h>
#include <hardware/hardware.h>
#include "NV12_resize.h"

#ifdef BOARD_HAVE_VPU
#include "vpu_wrapper.h"
#endif

#define Align(ptr,align)	(((uintptr_t)ptr+(align)-1)/(align)*(align))
#define VPU_ENC_MAX_NUM_MEM_REQS	(6)
#define MAX_FRAME_NUM	(4)

#ifdef BOARD_HAVE_VPU
typedef struct{
	//virtual mem info
	int nVirtNum;
	unsigned int virtMem[VPU_ENC_MAX_NUM_MEM_REQS];

	//phy mem info
	int nPhyNum;
	unsigned int phyMem_virtAddr[VPU_ENC_MAX_NUM_MEM_REQS];
	unsigned int phyMem_phyAddr[VPU_ENC_MAX_NUM_MEM_REQS];
	unsigned int phyMem_cpuAddr[VPU_ENC_MAX_NUM_MEM_REQS];
	unsigned int phyMem_size[VPU_ENC_MAX_NUM_MEM_REQS];
}EncMemInfo;


int EncOutFrameBufCreateRegisterFrame(VpuCodStd eFormat,int nInColor,
	VpuFrameBuffer* pOutRegisterFrame,int nInCnt,int nWidth,int nHeight,
	EncMemInfo* pOutEncMemInfo, int nInRot,int* pOutSrcStride,int nInAlign,int nInMapType)
{
	int i;
	VpuEncRetCode ret;
	int yStride;
	int uvStride;
	int ySize;
	int uvSize;
	int mvSize;
	VpuMemDesc vpuMem;
	unsigned char* ptr;
	unsigned char* ptrVirt;
	int nPadW;
	int nPadH;
	int multifactor=1;

//    ALOGI("==== EOFBCRF, eFormat %d, nInColor %d, nInAlign %d, nInMapType %d",
  //      eFormat, nInColor, nInAlign, nInMapType);

	nPadW=Align(nWidth,16);
	nPadH=Align(nHeight,16);
	if((nInRot==90)||(nInRot==270)){
		yStride=nPadH;
		ySize=yStride*nPadW;
	}
	else	{
		yStride=nPadW;
		ySize=yStride*nPadH;
	}
	if(VPU_V_MJPG==eFormat)	{
		switch(nInColor){
			case 0:	//4:2:0
				uvStride=yStride/2;
				uvSize=ySize/4;
				mvSize=uvSize;
				break;
			case 1:	//4:2:2 hor
				uvStride=yStride/2;
				uvSize=ySize/2;
				mvSize=uvSize;
				break;
			case 2:	//4:2:2 ver
				uvStride=yStride;
				uvSize=ySize/2;
				mvSize=uvSize;
				break;
			case 3:	//4:4:4
				uvStride=yStride;
				uvSize=ySize;
				mvSize=uvSize;
				break;
			case 4:	//4:0:0
				uvStride=0;
				uvSize=0;
				mvSize=uvSize;
				break;
			default:	//4:2:0
				ALOGE("unknown color format: %d ",nInColor);
				uvStride=yStride/2;
				uvSize=ySize/4;
				mvSize=uvSize;
				break;
		}
    }else {
		//4:2:0 for all video
		uvStride=yStride/2;
		uvSize=ySize/4;
		mvSize=uvSize;
	}

	if(nInMapType==2){
		//only consider Y since interleave must be enabled
		multifactor=2;	//for field, we need to consider alignment for top and bot
	}

	//we need to align the Y/Cb/Cr address
	if(nInAlign>1){
		ySize=Align(ySize,multifactor*nInAlign);
		uvSize=Align(uvSize,nInAlign);
	}

	for(i=0;i<nInCnt;i++){
		vpuMem.nSize=ySize+uvSize*2+mvSize+nInAlign;
		ret=VPU_EncGetMem(&vpuMem);
		if(VPU_ENC_RET_SUCCESS!=ret){
			ALOGE("%s: vpu malloc frame buf failure: ret=0x%X ",__FUNCTION__,ret);
			return -1;//OMX_ErrorInsufficientResources;
		}

		ptr=(unsigned char*)vpuMem.nPhyAddr;
		ptrVirt=(unsigned char*)vpuMem.nVirtAddr;

		/*align the base address*/
		if(nInAlign>1){
			ptr=(unsigned char*)Align(ptr,nInAlign);
			ptrVirt=(unsigned char*)Align(ptrVirt,nInAlign);
		}

		/* fill stride info */
		pOutRegisterFrame[i].nStrideY=yStride;
		pOutRegisterFrame[i].nStrideC=uvStride;

		/* fill phy addr*/
		pOutRegisterFrame[i].pbufY=ptr;
		pOutRegisterFrame[i].pbufCb=ptr+ySize;
		pOutRegisterFrame[i].pbufCr=ptr+ySize+uvSize;
		pOutRegisterFrame[i].pbufMvCol=ptr+ySize+uvSize*2;

		/* fill virt addr */
		pOutRegisterFrame[i].pbufVirtY=ptrVirt;
		pOutRegisterFrame[i].pbufVirtCb=ptrVirt+ySize;
		pOutRegisterFrame[i].pbufVirtCr=ptrVirt+ySize+uvSize;
		pOutRegisterFrame[i].pbufVirtMvCol=ptrVirt+ySize+uvSize*2;

		/* fill bottom address for field tile*/
		if(nInMapType==2){
			pOutRegisterFrame[i].pbufY_tilebot=pOutRegisterFrame[i].pbufY+ySize/2;
			pOutRegisterFrame[i].pbufCb_tilebot=pOutRegisterFrame[i].pbufCr;
			pOutRegisterFrame[i].pbufVirtY_tilebot=pOutRegisterFrame[i].pbufVirtY+ySize/2;
			pOutRegisterFrame[i].pbufVirtCb_tilebot=pOutRegisterFrame[i].pbufVirtCr;
		}
		else	{
			pOutRegisterFrame[i].pbufY_tilebot=0;
			pOutRegisterFrame[i].pbufCb_tilebot=0;
			pOutRegisterFrame[i].pbufVirtY_tilebot=0;
			pOutRegisterFrame[i].pbufVirtCb_tilebot=0;
		}

		//record memory info for release
		pOutEncMemInfo->phyMem_phyAddr[pOutEncMemInfo->nPhyNum]=vpuMem.nPhyAddr;
		pOutEncMemInfo->phyMem_virtAddr[pOutEncMemInfo->nPhyNum]=vpuMem.nVirtAddr;
		pOutEncMemInfo->phyMem_cpuAddr[pOutEncMemInfo->nPhyNum]=vpuMem.nCpuAddr;
		pOutEncMemInfo->phyMem_size[pOutEncMemInfo->nPhyNum]=vpuMem.nSize;
		pOutEncMemInfo->nPhyNum++;

	}

	*pOutSrcStride=nWidth;//nPadW;
	return i;
}

int EncFreeMemBlock(EncMemInfo* pEncMem)
{
	int i;
	VpuMemDesc vpuMem;
	VpuEncRetCode vpuRet;
	int retOk=1;

	//free virtual mem
	for(i=0;i<pEncMem->nVirtNum;i++){
		free((void*)(uintptr_t)pEncMem->virtMem[i]);
	}

	//free physical mem
	for(i=0;i<pEncMem->nPhyNum;i++)	{
		vpuMem.nPhyAddr=pEncMem->phyMem_phyAddr[i];
		vpuMem.nVirtAddr=pEncMem->phyMem_virtAddr[i];
		vpuMem.nCpuAddr=pEncMem->phyMem_cpuAddr[i];
		vpuMem.nSize=pEncMem->phyMem_size[i];
		vpuRet=VPU_EncFreeMem(&vpuMem);
		if(vpuRet!=VPU_ENC_RET_SUCCESS){
			ALOGE("%s: free vpu memory failure : ret=%d ",__FUNCTION__,(unsigned int)vpuRet);
			retOk=0;
		}
	}

	return retOk;
}


int EncMallocMemBlock(VpuMemInfo* pMemBlock,EncMemInfo* pEncMem)
{
	int i;
	unsigned char * ptr=NULL;
	int size;

	for(i=0;i<pMemBlock->nSubBlockNum;i++){
     /*   ALOGI("==== EncMallocMemBlock, i %d, align %d, size %d, memType %d",
            i, pMemBlock->MemSubBlock[i].nAlignment, pMemBlock->MemSubBlock[i].nSize,
            pMemBlock->MemSubBlock[i].MemType); */

		size=pMemBlock->MemSubBlock[i].nAlignment+pMemBlock->MemSubBlock[i].nSize;
		if(pMemBlock->MemSubBlock[i].MemType==VPU_MEM_VIRT){
			ptr=(unsigned char *)malloc(size);
			if(ptr==NULL)	{
				ALOGE("%s: get virtual memory failure, size=%d ",__FUNCTION__,(unsigned int)size);
				goto failure;
			}
			pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char*)Align(ptr,pMemBlock->MemSubBlock[i].nAlignment);

			//record virtual base addr
			pEncMem->virtMem[pEncMem->nVirtNum]=(uintptr_t)ptr;
			pEncMem->nVirtNum++;
		}
		else{ // if(memInfo.MemSubBlock[i].MemType==VPU_MEM_PHY)
			VpuMemDesc vpuMem;
			VpuEncRetCode ret;
			vpuMem.nSize=size;
			ret=VPU_EncGetMem(&vpuMem);
			if(ret!=VPU_ENC_RET_SUCCESS){
				ALOGE("%s: get vpu memory failure, size=%d, ret=%d ",__FUNCTION__,size,ret);
				goto failure;
			}
			pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char*)Align(vpuMem.nVirtAddr,pMemBlock->MemSubBlock[i].nAlignment);
			pMemBlock->MemSubBlock[i].pPhyAddr=(unsigned char*)Align(vpuMem.nPhyAddr,pMemBlock->MemSubBlock[i].nAlignment);

			//record physical base addr
			pEncMem->phyMem_phyAddr[pEncMem->nPhyNum]=(unsigned int)vpuMem.nPhyAddr;
			pEncMem->phyMem_virtAddr[pEncMem->nPhyNum]=(unsigned int)vpuMem.nVirtAddr;
			pEncMem->phyMem_cpuAddr[pEncMem->nPhyNum]=(unsigned int)vpuMem.nCpuAddr;
			pEncMem->phyMem_size[pEncMem->nPhyNum]=size;
			pEncMem->nPhyNum++;
		}
    }

	return 1;

failure:
	EncFreeMemBlock(pEncMem);
	return 0;
}

int vpu_encode(void *inYuv,
                             void* inYuvPhy,
                             int   Width,
                             int   Height,
                             int   /*quality*/,
                             int   color,
                             void *outBuf,
                             int   outSize,
                             int colorFormat)
{
	VpuEncRetCode ret;
	int size=0;
	VpuVersionInfo ver;
	VpuWrapperVersionInfo w_ver;
	VpuEncHandle handle=0;
	VpuMemInfo sMemInfo;
	EncMemInfo sEncMemInfo;
	VpuEncOpenParamSimp sEncOpenParamSimp;
	VpuEncInitInfo sEncInitInfo;
	VpuFrameBuffer sFrameBuf[MAX_FRAME_NUM];
	int nBufNum;
	int nSrcStride;
	VpuEncEncParam sEncEncParam;
	char* tempOut=0;
	static FILE* fpjpg=0;

	memset(&sMemInfo,0,sizeof(VpuMemInfo));
	memset(&sEncMemInfo,0,sizeof(EncMemInfo));
	memset(&sFrameBuf,0,sizeof(VpuFrameBuffer)*MAX_FRAME_NUM);

	ret=VPU_EncLoad();
	if (ret!=VPU_ENC_RET_SUCCESS){
		ALOGE("load vpu encoder failure !");
		return 0;
	}
	ret=VPU_EncGetVersionInfo(&ver);
	if (ret!=VPU_ENC_RET_SUCCESS){
		ALOGE("vpu get version info failure: ret=%d ",ret);
		goto finish;
	}
	ALOGI("vpu lib version : major.minor.rel=%d.%d.%d ",ver.nLibMajor,ver.nLibMinor,ver.nLibRelease);
	ALOGI("vpu fw version : major.minor.rel_rcode=%d.%d.%d_r%d ",ver.nFwMajor,ver.nFwMinor,ver.nFwRelease,ver.nFwCode);

	ret=(VpuEncRetCode)VPU_EncGetWrapperVersionInfo(&w_ver);
	if (ret!=VPU_ENC_RET_SUCCESS){
		ALOGE("%s: vpu get wrapper version failure: ret=%d ",__FUNCTION__,ret);
		goto finish;
	}
	//ALOGI("vpu wrapper version : major.minor.rel=%d.%d.%d: %s ",w_ver.nMajor,w_ver.nMinor,w_ver.nRelease,w_ver.pBinary);

	//query memory
	ret=VPU_EncQueryMem(&sMemInfo);
	if (ret!=VPU_ENC_RET_SUCCESS){
		ALOGE("%s: vpu query memory failure: ret=0x%X ",__FUNCTION__,ret);
		goto finish;
	}

	//malloc memory for vpu
	if(0==EncMallocMemBlock(&sMemInfo,&sEncMemInfo))
	{
		ALOGE("%s: malloc memory failure: ",__FUNCTION__);
		goto finish;
	}

	memset(&sEncOpenParamSimp,0,sizeof(VpuEncOpenParamSimp));
	sEncOpenParamSimp.eFormat=VPU_V_MJPG;
	sEncOpenParamSimp.sMirror=VPU_ENC_MIRDIR_NONE;
	sEncOpenParamSimp.nPicWidth= Width;
	sEncOpenParamSimp.nPicHeight=Height;
	sEncOpenParamSimp.nRotAngle=0;
	sEncOpenParamSimp.nFrameRate=30;
	sEncOpenParamSimp.nBitRate=0;
	sEncOpenParamSimp.nGOPSize=30;
	sEncOpenParamSimp.nChromaInterleave=1;
    sEncOpenParamSimp.eColorFormat=(VpuColorFormat)colorFormat;

	//open vpu
	ret=VPU_EncOpenSimp(&handle, &sMemInfo,&sEncOpenParamSimp);
	if (ret!=VPU_ENC_RET_SUCCESS){
		ALOGE("%s: vpu open failure: ret=0x%X ",__FUNCTION__,ret);
		goto finish;
	}

	//get initinfo
	ret=VPU_EncGetInitialInfo(handle,&sEncInitInfo);
	if(VPU_ENC_RET_SUCCESS!=ret){
		ALOGE("%s: init vpu failure ",__FUNCTION__);
		goto finish;
	}

	nBufNum=sEncInitInfo.nMinFrameBufferCount;
//	ALOGI("==== Init OK: min buffer cnt: %d, alignment: %d ",sEncInitInfo.nMinFrameBufferCount,sEncInitInfo.nAddressAlignment);
	//fill frameBuf[]
	if(-1==EncOutFrameBufCreateRegisterFrame(sEncOpenParamSimp.eFormat,color,sFrameBuf, nBufNum,Width, Height, &sEncMemInfo,0,&nSrcStride,sEncInitInfo.nAddressAlignment,0)){
		ALOGE("%s: allocate vpu frame buffer failure ",__FUNCTION__);
		goto finish;
	}

	//register frame buffs
	ret=VPU_EncRegisterFrameBuffer(handle, sFrameBuf, nBufNum,nSrcStride);
	if(VPU_ENC_RET_SUCCESS!=ret){
		ALOGE("%s: vpu register frame failure: ret=0x%X ",__FUNCTION__,ret);
		goto finish;
	}

	//allocate temporary physical output buffer
	memset(&sMemInfo,0,sizeof(VpuMemInfo));
	sMemInfo.nSubBlockNum=1;
	sMemInfo.MemSubBlock[0].MemType=VPU_MEM_PHY;
	sMemInfo.MemSubBlock[0].nAlignment=sEncInitInfo.nAddressAlignment;//8;
	sMemInfo.MemSubBlock[0].nSize=outSize;
	if(0==EncMallocMemBlock(&sMemInfo,&sEncMemInfo))	{
		ALOGE("%s: malloc memory failure: ",__FUNCTION__);
		goto finish;
	}

	//encode frame
	memset(&sEncEncParam,0,sizeof(VpuEncEncParam));
	sEncEncParam.eFormat=VPU_V_MJPG;
	sEncEncParam.nPicWidth=Width;
	sEncEncParam.nPicHeight=Height;
	sEncEncParam.nFrameRate=30;
	sEncEncParam.nQuantParam=10;
	sEncEncParam.nInPhyInput=(uintptr_t)inYuvPhy;
	sEncEncParam.nInVirtInput=(uintptr_t)inYuv;
	sEncEncParam.nInInputSize=(color==0)?(Width*Height*3/2):(Width*Height*2);
	sEncEncParam.nInPhyOutput=(uintptr_t)sMemInfo.MemSubBlock[0].pPhyAddr;
	sEncEncParam.nInVirtOutput=(uintptr_t)sMemInfo.MemSubBlock[0].pVirtAddr;
	sEncEncParam.nInOutputBufLen=outSize;

	ret=VPU_EncEncodeFrame(handle, &sEncEncParam);
	if(VPU_ENC_RET_SUCCESS!=ret){
		ALOGE("%s, vpu encode frame failure: ret=0x%X ",__FUNCTION__,ret);
		if(VPU_ENC_RET_FAILURE_TIMEOUT==ret){
			VPU_EncReset(handle);
		}
	}

	if((sEncEncParam.eOutRetCode & VPU_ENC_OUTPUT_DIS)||(sEncEncParam.eOutRetCode & VPU_ENC_OUTPUT_SEQHEADER)){
		size=sEncEncParam.nOutOutputSize;
		//ALOGI("encode succeed, output size: %d ",size);
	}
	else{
		ALOGE("%s, vpu encode frame failure: no output,  ret=0x%X ",__FUNCTION__,sEncEncParam.eOutRetCode);
	}

	memcpy(outBuf,(void*)(uintptr_t)sEncEncParam.nInVirtOutput,sEncEncParam.nOutOutputSize);

finish:

	//close vpu
	if(handle!=0){
		ret=VPU_EncClose(handle);
		if (ret!=VPU_ENC_RET_SUCCESS){
			ALOGE("%s: vpu close failure: ret=%d ",__FUNCTION__,ret);
		}
	}

	//unload
	ret=VPU_EncUnLoad();
	if (ret!=VPU_ENC_RET_SUCCESS){
		ALOGE("%s: vpu unload failure: ret=%d \r\n",__FUNCTION__,ret);
	}

	//release mem
	if(0==EncFreeMemBlock(&sEncMemInfo)){
		ALOGE("%s: free memory failure:  ",__FUNCTION__);
	}

	return size;
}
#endif

YuvToJpegEncoder * YuvToJpegEncoder::create(int format) {
    // Only ImageFormat.NV21 and ImageFormat.YUY2 are supported
    // for now.
    if (format == HAL_PIXEL_FORMAT_YCbCr_420_SP) {
        return new Yuv420SpToJpegEncoder();
    } else if (format == HAL_PIXEL_FORMAT_YCbCr_422_I) {
        return new Yuv422IToJpegEncoder();
    } else if (format == HAL_PIXEL_FORMAT_YCbCr_422_SP) {
        return new Yuv422SpToJpegEncoder();
    } else {
        ALOGE("YuvToJpegEncoder:create format:%d not support", format);
        return NULL;
    }
}

YuvToJpegEncoder::YuvToJpegEncoder()
    : supportVpu(false),
      fNumPlanes(1),
      color(1),
      mColorFormat(0)
{}

int YuvToJpegEncoder::encode(void *inYuv,
                             void* inYuvPhy,
                             int   inWidth,
                             int   inHeight,
                             int   quality,
                             void *outBuf,
                             int   outSize,
                             int   outWidth,
                             int   outHeight) {
#ifdef BOARD_HAVE_VPU
    //use vpu to encode
	if((inWidth == outWidth) && (inHeight == outHeight) && supportVpu){
		int size;
		size=vpu_encode(inYuv, inYuvPhy, outWidth, outHeight,quality,color,outBuf,outSize, mColorFormat);
		return size;
	}
#endif

    jpeg_compress_struct  cinfo;
    jpegBuilder_error_mgr sk_err;
    uint8_t *resize_src = NULL;
    jpegBuilder_destination_mgr dest_mgr((uint8_t *)outBuf, outSize);


    memset(&cinfo, 0, sizeof(cinfo));
    if ((inWidth != outWidth) || (inHeight != outHeight)) {
        resize_src = (uint8_t *)malloc(outSize);
        yuvResize((uint8_t *)inYuv,
                  inWidth,
                  inHeight,
                  resize_src,
                  outWidth,
                  outHeight);
        inYuv = resize_src;
    }

    cinfo.err = jpeg_std_error(&sk_err);
    jpeg_create_compress(&cinfo);

    cinfo.dest = &dest_mgr;

    setJpegCompressStruct(&cinfo, outWidth, outHeight, quality);

    jpeg_start_compress(&cinfo, TRUE);

    compress(&cinfo, (uint8_t *)inYuv);
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);
    if (resize_src != NULL) {
        free(resize_src);
    }

    return dest_mgr.jpegsize;
}

void YuvToJpegEncoder::setJpegCompressStruct(jpeg_compress_struct *cinfo,
                                             int                   width,
                                             int                   height,
                                             int                   quality) {
    cinfo->image_width      = width;
    cinfo->image_height     = height;
    cinfo->input_components = 3;
    cinfo->in_color_space   = JCS_YCbCr;
    jpeg_set_defaults(cinfo);

    jpeg_set_quality(cinfo, quality, TRUE);
    jpeg_set_colorspace(cinfo, JCS_YCbCr);
    cinfo->raw_data_in = TRUE;
    cinfo->dct_method  = JDCT_IFAST;
    configSamplingFactors(cinfo);
}

// /////////////////////////////////////////////////////////////////
Yuv420SpToJpegEncoder::Yuv420SpToJpegEncoder() :
    YuvToJpegEncoder() {
    fNumPlanes = 2;
    color=0;
#ifdef BOARD_HAVE_VPU
    supportVpu = true;
    mColorFormat = VPU_COLOR_420;
#endif
}

void Yuv420SpToJpegEncoder::compress(jpeg_compress_struct *cinfo,
                                     uint8_t              *yuv) {
    JSAMPROW   y[16];
    JSAMPROW   cb[8];
    JSAMPROW   cr[8];
    JSAMPARRAY planes[3];

    planes[0] = y;
    planes[1] = cb;
    planes[2] = cr;

    int width         = cinfo->image_width;
    int height        = cinfo->image_height;
    uint8_t *yPlanar  = yuv;
    uint8_t *vuPlanar = yuv + width * height;
    uint8_t *uRows    = new uint8_t[8 * (width >> 1)];
    uint8_t *vRows    = new uint8_t[8 * (width >> 1)];

    // process 16 lines of Y and 8 lines of U/V each time.
    while (cinfo->next_scanline < cinfo->image_height) {
        // deitnerleave u and v
        deinterleave(vuPlanar, uRows, vRows, cinfo->next_scanline, width, height);

        for (int i = 0; i < 16; i++) {
            // y row
            y[i] = yPlanar + (cinfo->next_scanline + i) * width;

            // construct u row and v row
            if ((i & 1) == 0) {
                // height and width are both halved because of downsampling
                int offset = (i >> 1) * (width >> 1);
                cb[i / 2] = uRows + offset;
                cr[i / 2] = vRows + offset;
            }
        }
        jpeg_write_raw_data(cinfo, planes, 16);
    }
    delete[] uRows;
    delete[] vRows;
}

void Yuv420SpToJpegEncoder::deinterleave(uint8_t *vuPlanar,
                                         uint8_t *uRows,
                                         uint8_t *vRows,
                                         int      rowIndex,
                                         int      width,
                                         int      height) {
    for (int row = 0; row < 8; ++row) {
        int hoff = (rowIndex >> 1) + row;
        if (hoff >= (height >> 1)) {
            return;
        }
        int offset  = hoff * width;
        uint8_t *vu = vuPlanar + offset;
        for (int i = 0; i < (width >> 1); ++i) {
            int index = row * (width >> 1) + i;
            uRows[index] = vu[0];
            vRows[index] = vu[1];
            vu          += 2;
        }
    }
}

void Yuv420SpToJpegEncoder::configSamplingFactors(jpeg_compress_struct *cinfo) {
    // cb and cr are horizontally downsampled and vertically downsampled as
    // well.
    cinfo->comp_info[0].h_samp_factor = 2;
    cinfo->comp_info[0].v_samp_factor = 2;
    cinfo->comp_info[1].h_samp_factor = 1;
    cinfo->comp_info[1].v_samp_factor = 1;
    cinfo->comp_info[2].h_samp_factor = 1;
    cinfo->comp_info[2].v_samp_factor = 1;
}

int Yuv420SpToJpegEncoder::yuvResize(uint8_t *srcBuf,
                                     int      srcWidth,
                                     int      srcHeight,
                                     uint8_t *dstBuf,
                                     int      dstWidth,
                                     int      dstHeight)
{
    if (!srcBuf || !dstBuf) {
        return -1;
    }

    structConvImage o_img_ptr, i_img_ptr;
    memset(&o_img_ptr, 0, sizeof(o_img_ptr));
    memset(&i_img_ptr, 0, sizeof(i_img_ptr));

    // input
    i_img_ptr.uWidth  =  srcWidth;
    i_img_ptr.uStride =  i_img_ptr.uWidth;
    i_img_ptr.uHeight =  srcHeight;
    i_img_ptr.eFormat = IC_FORMAT_YCbCr420_lp;
    i_img_ptr.imgPtr  = srcBuf;
    i_img_ptr.clrPtr  = i_img_ptr.imgPtr + (i_img_ptr.uWidth * i_img_ptr.uHeight);

    // ouput
    o_img_ptr.uWidth  = dstWidth;
    o_img_ptr.uStride = o_img_ptr.uWidth;
    o_img_ptr.uHeight = dstHeight;
    o_img_ptr.eFormat = IC_FORMAT_YCbCr420_lp;
    o_img_ptr.imgPtr  = dstBuf;
    o_img_ptr.clrPtr  = o_img_ptr.imgPtr + (o_img_ptr.uWidth * o_img_ptr.uHeight);

    VT_resizeFrame_Video_opt2_lp(&i_img_ptr, &o_img_ptr, NULL, 0);

    return 0;
}

// /////////////////////////////////////////////////////////////////////////////
Yuv422IToJpegEncoder::Yuv422IToJpegEncoder() :
    YuvToJpegEncoder() {
    fNumPlanes = 1;
    color=1;
#ifdef BOARD_HAVE_VPU
    mColorFormat = VPU_COLOR_422H;
#endif
}

void Yuv422IToJpegEncoder::compress(jpeg_compress_struct *cinfo,
                                    uint8_t              *yuv) {
    JSAMPROW   y[16];
    JSAMPROW   cb[16];
    JSAMPROW   cr[16];
    JSAMPARRAY planes[3];

    planes[0] = y;
    planes[1] = cb;
    planes[2] = cr;

    int width      = cinfo->image_width;
    int height     = cinfo->image_height;
    uint8_t *yRows = new uint8_t[16 * width];
    uint8_t *uRows = new uint8_t[16 * (width >> 1)];
    uint8_t *vRows = new uint8_t[16 * (width >> 1)];

    uint8_t *yuvOffset = yuv;

    // process 16 lines of Y and 16 lines of U/V each time.
    while (cinfo->next_scanline < cinfo->image_height) {
        deinterleave(yuvOffset,
                     yRows,
                     uRows,
                     vRows,
                     cinfo->next_scanline,
                     width,
                     height);

        for (int i = 0; i < 16; i++) {
            // y row
            y[i] = yRows + i * width;

            // construct u row and v row
            // width is halved because of downsampling
            int offset = i * (width >> 1);
            cb[i] = uRows + offset;
            cr[i] = vRows + offset;
        }

        jpeg_write_raw_data(cinfo, planes, 16);
    }
    delete[] yRows;
    delete[] uRows;
    delete[] vRows;
}

void Yuv422IToJpegEncoder::deinterleave(uint8_t *yuv,
                                        uint8_t *yRows,
                                        uint8_t *uRows,
                                        uint8_t *vRows,
                                        int      rowIndex,
                                        int      width,
                                        int      /*height*/) {
    for (int row = 0; row < 16; ++row) {
        uint8_t *yuvSeg = yuv + (rowIndex + row) * width * 2;
        for (int i = 0; i < (width >> 1); ++i) {
            int indexY = row * width + (i << 1);
            int indexU = row * (width >> 1) + i;
            yRows[indexY]     = yuvSeg[0];
            yRows[indexY + 1] = yuvSeg[2];
            uRows[indexU]     = yuvSeg[1];
            vRows[indexU]     = yuvSeg[3];
            yuvSeg           += 4;
        }
    }
}

void Yuv422IToJpegEncoder::configSamplingFactors(jpeg_compress_struct *cinfo) {
    // cb and cr are horizontally downsampled and vertically downsampled as
    // well.
    cinfo->comp_info[0].h_samp_factor = 2;
    cinfo->comp_info[0].v_samp_factor = 2;
    cinfo->comp_info[1].h_samp_factor = 1;
    cinfo->comp_info[1].v_samp_factor = 2;
    cinfo->comp_info[2].h_samp_factor = 1;
    cinfo->comp_info[2].v_samp_factor = 2;
}

int Yuv422IToJpegEncoder::yuvResize(uint8_t *srcBuf,
                                    int      srcWidth,
                                    int      srcHeight,
                                    uint8_t *dstBuf,
                                    int      dstWidth,
                                    int      dstHeight)
{
    int i, j, s;
    int h_offset;
    int v_offset;
    unsigned char *ptr, cc;
    int h_scale_ratio;
    int v_scale_ratio;

    s = 0;

_resize_begin:

    if (!dstWidth) return -1;

    if (!dstHeight) return -1;

    h_scale_ratio = srcWidth / dstWidth;
    if (!h_scale_ratio) return -1;

    v_scale_ratio = srcHeight / dstHeight;
    if (!v_scale_ratio) return -1;

    h_offset = (srcWidth - dstWidth * h_scale_ratio) / 2;
    v_offset = (srcHeight - dstHeight * v_scale_ratio) / 2;

    for (i = 0; i < dstHeight * v_scale_ratio; i += v_scale_ratio)
    {
        for (j = 0; j < dstWidth * h_scale_ratio; j += h_scale_ratio)
        {
            ptr = srcBuf + i * srcWidth + j + v_offset * srcWidth + h_offset;
            cc  = ptr[0];

            ptr    = dstBuf + (i / v_scale_ratio) * dstWidth + (j / h_scale_ratio);
            ptr[0] = cc;
        }
    }

    srcBuf += srcWidth * srcHeight;
    dstBuf += dstWidth * dstHeight;

    if (s < 2)
    {
        if (!s++)
        {
            srcWidth  >>= 1;
            srcHeight >>= 1;

            dstWidth  >>= 1;
            dstHeight >>= 1;
        }

        goto _resize_begin;
    }

    return 0;
}

// /////////////////////////////////////////////////////////////////////////////////////////////
Yuv422SpToJpegEncoder::Yuv422SpToJpegEncoder() :
    YuvToJpegEncoder() {
        fNumPlanes = 1;
        color=1;
#ifdef BOARD_HAVE_VPU
        supportVpu = true;
        mColorFormat = VPU_COLOR_422H;
#endif
    }

void Yuv422SpToJpegEncoder::compress(jpeg_compress_struct *cinfo,
        uint8_t              *yuv) {
    JSAMPROW   y[16];
    JSAMPROW   cb[16];
    JSAMPROW   cr[16];
    JSAMPARRAY planes[3];

    planes[0] = y;
    planes[1] = cb;
    planes[2] = cr;

    int width      = cinfo->image_width;
    int height     = cinfo->image_height;
    uint8_t *yRows = new uint8_t[16 * width];
    uint8_t *uRows = new uint8_t[16 * (width >> 1)];
    uint8_t *vRows = new uint8_t[16 * (width >> 1)];

    uint8_t *yuvOffset = yuv;

    // process 16 lines of Y and 16 lines of U/V each time.
    while (cinfo->next_scanline < cinfo->image_height) {
        deinterleave(yuvOffset,
                yRows,
                uRows,
                vRows,
                cinfo->next_scanline,
                width,
                height);

        for (int i = 0; i < 16; i++) {
            // y row
            y[i] = yRows + i * width;

            // construct u row and v row
            // width is halved because of downsampling
            int offset = i * (width >> 1);
            cb[i] = uRows + offset;
            cr[i] = vRows + offset;
        }

        jpeg_write_raw_data(cinfo, planes, 16);
    }
    delete[] yRows;
    delete[] uRows;
    delete[] vRows;
}

void Yuv422SpToJpegEncoder::deinterleave(uint8_t *yuv,
        uint8_t *yRows,
        uint8_t *uRows,
        uint8_t *vRows,
        int      rowIndex,
        int      width,
        int      /*height*/) {
    for (int row = 0; row < 16; ++row) {
        uint8_t *yuvSeg = yuv + (rowIndex + row) * width * 2;
        for (int i = 0; i < (width >> 1); ++i) {
            int indexY = row * width + (i << 1);
            int indexU = row * (width >> 1) + i;
            yRows[indexY]     = yuvSeg[0];
            yRows[indexY + 1] = yuvSeg[2];
            uRows[indexU]     = yuvSeg[1];
            vRows[indexU]     = yuvSeg[3];
            yuvSeg           += 4;
        }
    }
}

void Yuv422SpToJpegEncoder::configSamplingFactors(jpeg_compress_struct *cinfo) {
    // cb and cr are horizontally downsampled and vertically downsampled as
    // well.
    cinfo->comp_info[0].h_samp_factor = 2;
    cinfo->comp_info[0].v_samp_factor = 2;
    cinfo->comp_info[1].h_samp_factor = 1;
    cinfo->comp_info[1].v_samp_factor = 2;
    cinfo->comp_info[2].h_samp_factor = 1;
    cinfo->comp_info[2].v_samp_factor = 2;
}

int Yuv422SpToJpegEncoder::yuvResize(uint8_t *srcBuf,
        int      srcWidth,
        int      srcHeight,
        uint8_t *dstBuf,
        int      dstWidth,
        int      dstHeight)
{
    int i, j, s;
    int h_offset;
    int v_offset;
    unsigned char *ptr, cc;
    int h_scale_ratio;
    int v_scale_ratio;

    s = 0;

_resize_begin:

    if (!dstWidth) return -1;

    if (!dstHeight) return -1;

    h_scale_ratio = srcWidth / dstWidth;
    if (!h_scale_ratio) return -1;

    v_scale_ratio = srcHeight / dstHeight;
    if (!v_scale_ratio) return -1;

    h_offset = (srcWidth - dstWidth * h_scale_ratio) / 2;
    v_offset = (srcHeight - dstHeight * v_scale_ratio) / 2;

    for (i = 0; i < dstHeight * v_scale_ratio; i += v_scale_ratio)
    {
        for (j = 0; j < dstWidth * h_scale_ratio; j += h_scale_ratio)
        {
            ptr = srcBuf + i * srcWidth + j + v_offset * srcWidth + h_offset;
            cc  = ptr[0];

            ptr    = dstBuf + (i / v_scale_ratio) * dstWidth + (j / h_scale_ratio);
            ptr[0] = cc;
        }
    }

    srcBuf += srcWidth * srcHeight;
    dstBuf += dstWidth * dstHeight;

    if (s < 2)
    {
        if (!s++)
        {
            srcWidth  >>= 1;
            srcHeight >>= 1;

            dstWidth  >>= 1;
            dstHeight >>= 1;
        }

        goto _resize_begin;
    }

    return 0;
}


void jpegBuilder_error_exit(j_common_ptr cinfo)
{
    jpegBuilder_error_mgr *error = (jpegBuilder_error_mgr *)cinfo->err;

    (*error->output_message)(cinfo);

    /* Let the memory manager delete any temp files before we die */
    jpeg_destroy(cinfo);

    longjmp(error->fJmpBuf, -1);
}

static void jpegBuilder_init_destination(j_compress_ptr cinfo) {
    jpegBuilder_destination_mgr *dest =
        (jpegBuilder_destination_mgr *)cinfo->dest;

    dest->next_output_byte = dest->buf;
    dest->free_in_buffer   = dest->bufsize;
    dest->jpegsize         = 0;
}

static boolean jpegBuilder_empty_output_buffer(j_compress_ptr cinfo) {
    jpegBuilder_destination_mgr *dest =
        (jpegBuilder_destination_mgr *)cinfo->dest;

    dest->next_output_byte = dest->buf;
    dest->free_in_buffer   = dest->bufsize;
    return TRUE; // ?
}

static void jpegBuilder_term_destination(j_compress_ptr cinfo) {
    jpegBuilder_destination_mgr *dest =
        (jpegBuilder_destination_mgr *)cinfo->dest;

    dest->jpegsize = dest->bufsize - dest->free_in_buffer;
}

jpegBuilder_destination_mgr::jpegBuilder_destination_mgr(uint8_t *input,
                                                         int      size) {
    this->init_destination    = jpegBuilder_init_destination;
    this->empty_output_buffer = jpegBuilder_empty_output_buffer;
    this->term_destination    = jpegBuilder_term_destination;

    this->buf     = input;
    this->bufsize = size;

    jpegsize = 0;
}

