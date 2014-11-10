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

#include "CameraUtil.h"
#include "UvcMJPGDevice.h"
#include <cutils/properties.h>
#include <sys/times.h>
#include <poll.h>

typedef unsigned char u8;
typedef unsigned int u32;


static u8 g_hufTab[] = { \
0xff, 0xc4, 0x00, 0x1f,
0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00,
0xb5, 0x10, 0x00, 0x02, 0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00,
0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51,
0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52,
0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26,
0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47,
0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67,
0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87,
0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6,
0xf7, 0xf8, 0xf9, 0xfa, 0xff, 0xc4, 0x00, 0x1f, 0x01, 0x00, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01,
0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
0x07, 0x08, 0x09, 0x0a, 0x0b, 0xff, 0xc4, 0x00, 0xb5, 0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04,
0x03, 0x04, 0x07, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04,
0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14,
0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16,
0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56,
0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76,
0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94,
0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2,
0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9,
0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa };

#define MAX_SERCH_SIZE (u32)(64*1024)

typedef struct tagSecHead {
 u8 secStart;
 u8 secID;
 u8 secLenH;
 u8 secLenL;
}TSecHead;

#define SEC_START   0xff

#define ID_SOI  0xd8
#define ID_SOF0 0xc0
#define ID_DHT  0xc4
#define ID_SOS  0xda

static int SearchHufTabPos(u8 *pSrc, u32 nSrcSize, bool *pbExist, u32 *pdwExptDHTPos) {
    u32 dwSearchSize = (MAX_SERCH_SIZE < nSrcSize) ? MAX_SERCH_SIZE : nSrcSize;
    TSecHead tSecHead;
    u32 pos = 0;
    bool bSOF0Found = false;
    u32 dwSecSize = 0;
    u32 dwExpectedHufTabPos = 0;

    if((pSrc == NULL) || (nSrcSize < 4) || (pbExist == NULL) || (pdwExptDHTPos == NULL)) {
        ALOGE("SearchHufTabPos, para err");
        return -1;
    }
        
    *pbExist = false;
    
    if( ! ((pSrc[0] == SEC_START) && (pSrc[1] == ID_SOI)) ) {
        return -1;
    }
    pos = 2;
        
    while(pos + 4 <= dwSearchSize) {
        tSecHead.secStart = pSrc[pos++];
        tSecHead.secID = pSrc[pos++];
        tSecHead.secLenH = pSrc[pos++];
        tSecHead.secLenL = pSrc[pos++];
        
        if(tSecHead.secStart != SEC_START) {
            ALOGE("SearchHufTabPos, error format, no SEC_START 0x%x in pos 0x%x", SEC_START, pos - 4);
            return -1;
        }

        FLOGI("secID 0x%x at sec pos 0x%x", tSecHead.secID, pos-4);

        if(tSecHead.secID == ID_SOS) {
            FLOGI("SearchHufTabPos, ID_SOS 0x%x found at sec pos 0x%x, break", ID_SOS, pos-4);
            break;
        }
            
        if(tSecHead.secID == ID_SOF0) {
            FLOGI("SearchHufTabPos, ID_SOF0 0x%x found at sec pos 0x%x", ID_SOF0, pos-4);
            bSOF0Found = true;
        } 

        if(tSecHead.secID == ID_DHT) {
            FLOGI("SearchHufTabPos, huf tab found");
            *pbExist = true;
            return 0;
        }
        
        dwSecSize = (tSecHead.secLenH << 8) | tSecHead.secLenL;
        FLOGI("secID size 0x%x, h 0x%x, l 0x%x", dwSecSize, tSecHead.secLenH, tSecHead.secLenL);
        
        if(dwSecSize < 2) {
            ALOGE("SearchHufTabPos, err sec size %d < 2, at pos 0x%x", dwSecSize, pos-2);
            return -1;
        }
        pos += dwSecSize - 2;
        FLOGI("pos set to 0x%x", pos);
            
        if(bSOF0Found)
            dwExpectedHufTabPos = pos;
    }
    
    if(bSOF0Found == false) {
        FLOGI("SearchHufTabPos, err, no SOF0 found");
        return -1;
    }
    
    *pdwExptDHTPos = dwExpectedHufTabPos;
    FLOGI("SearchHufTabPos, huf not found, expected pos 0x%x", dwExpectedHufTabPos);

    return 0;
    
}
    
static int CheckAndInsertHufTab(u8 *pDst, u8 *pSrc, u32 nDstSize, u32 nSrcSize) {
    bool bExist = 0;
    u32 dwExptDHTPos = 0;
    int ret;
    
    ret = SearchHufTabPos(pSrc, nSrcSize, &bExist, &dwExptDHTPos);
    if(ret) {
        ALOGE("CheckAndInsertHufTab, SearchHufTabPos failed, ret %d", ret);
        return ret;
    }

    u32 nCopySize = (nDstSize < nSrcSize) ? nDstSize : nSrcSize;

    if(bExist) {        
        memcpy(pDst, pSrc, nCopySize);
        return 0;
    }

    //insert huf tab
    if(dwExptDHTPos + sizeof(g_hufTab) > nCopySize) {
        ALOGE("%s, dwExptDHTPos (%d) + sizeof(g_hufTab) (%d) > nCopySize (%d)",
            __FUNCTION__, dwExptDHTPos, sizeof(g_hufTab), nCopySize);
        return -1;
    }

    memcpy(pDst, pSrc, dwExptDHTPos);
    memcpy(pDst + dwExptDHTPos, g_hufTab, sizeof(g_hufTab));
    
    u32 dwLeftSize = nCopySize - (dwExptDHTPos + sizeof(g_hufTab));
    memcpy(pDst + dwExptDHTPos + sizeof(g_hufTab), pSrc + dwExptDHTPos, dwLeftSize);

    return 0;    
}


UvcMJPGDevice::UvcMJPGDevice()
{
    mResCount = 0;
    mVPUHandle = 0;        
    memset(mVPUPhyAddr, 0, sizeof(mVPUPhyAddr));
    memset(mVPUVirtAddr, 0, sizeof(mVPUVirtAddr));
    memset(mResMap, 0, sizeof(mResMap));
    memset(&mDecMemInfo,0,sizeof(DecMemInfo));
    memset(&mDecContxt, 0, sizeof(mDecContxt)); 
}

UvcMJPGDevice::~UvcMJPGDevice()
{
}

status_t UvcMJPGDevice::setDeviceConfig(int         width,
                                    int         height,
                                    PixelFormat format,
                                    int         fps)
{
    int uvcWidth = 0;
    int uvcHeight = 0;
    status_t ret = NO_ERROR;
    
    FLOGI("UvcMJPGDevice::setDeviceConfig, width %d, height %d, format 0x%x, fps %d", width, height, format, fps);

    if (mCameraHandle <= 0) {
        if (pDevPath != NULL) {
            mCameraHandle = open(pDevPath, O_RDWR);
        }
        if (mCameraHandle <= 0) {
            FLOGE("setDeviceConfig: DeviceAdapter uninitialized");
            return BAD_VALUE;
        }
    }
    if ((width == 0) || (height == 0)) {
        FLOGE("setDeviceConfig: invalid parameters");
        return BAD_VALUE;
    }

    //map to v4l2 resolution
    ret = GetUVCResFromVPURes(width, height, &uvcWidth, &uvcHeight);
    if(ret != NO_ERROR) {
        return ret;
    }
        
    
    int vformat;
    //vformat = convertPixelFormatToV4L2Format(mDefaultFormat);
    //uvcWidth = 960;
    //uvcHeight = 544;
    vformat = 0x47504a4d; //MJPG

    
    FLOGI("Width * Height %d x %d format 0x%x, fps: %d",
          uvcWidth, uvcHeight, vformat, fps);

    mVideoInfo->width       = uvcWidth;
    mVideoInfo->height      = uvcHeight;
    mVideoInfo->framesizeIn = (uvcWidth * uvcHeight << 1);
    mVideoInfo->formatIn    = vformat;

    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    param.parm.capture.timeperframe.numerator   = 1;
    param.parm.capture.timeperframe.denominator = fps;
    ret = ioctl(mCameraHandle, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_PARM Failed: %s", strerror(errno));
        return ret;
    }

        
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width        = uvcWidth & 0xFFFFFFF8;
    fmt.fmt.pix.height       = uvcHeight & 0xFFFFFFF8;
    fmt.fmt.pix.pixelformat  = vformat;
    fmt.fmt.pix.priv         = 0;
    fmt.fmt.pix.sizeimage    = 0;
    fmt.fmt.pix.bytesperline = 0;

    ret = ioctl(mCameraHandle, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_FMT Failed: %s", strerror(errno));
        return ret;
    }

    return ret;
}

void UvcMJPGDevice::setPreviewPixelFormat()
{
    FLOGI("UvcMJPGDevice::setPreviewPixelFormat");

    mPreviewNeedCsc = true;
    if (mDefaultFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP) {
        mPreviewNeedCsc = false;
    }

    int n = 0;
    if (mPreviewNeedCsc) {
        mAvailableFormats[n++] = HAL_PIXEL_FORMAT_YCbCr_420_SP;
    }
    for (int i=0; i < MAX_SENSOR_FORMAT && (mSensorFormats[i] != 0) &&
                  n < MAX_SENSOR_FORMAT; i++) {
        mAvailableFormats[n++] = mSensorFormats[i];
    }
    mAvailableFormatCount = n;
  //  mPreviewPixelFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
    mPreviewPixelFormat = HAL_PIXEL_FORMAT_YCbCr_422_SP;
}

status_t UvcMJPGDevice::GetUVCResFromVPURes(int nVPUWidth, int nVPUHHeight, int *pnUVCWidth, int *pnUVCHeight) {
    int i;
    
    if((pnUVCWidth == NULL) || (pnUVCHeight == NULL)) {
        ALOGE("GetUVCResFromVPURes, para null");
        return BAD_VALUE;
    }

    for(i = 0; i < mResCount; i++) {
        if((nVPUWidth == mResMap[i].nVPUWidth) && (nVPUHHeight == mResMap[i].nVPUHeight)) {
            *pnUVCWidth = mResMap[i].nUVCWidth;
            *pnUVCHeight = mResMap[i].nUVCHeight;
            break;
        }
    }

    if(i >= mResCount) {
        ALOGE("GetUVCResFromVPURes, no UVC res found for VPU res %dx%d, mResCount %d", nVPUWidth, nVPUHHeight, mResCount);
        return BAD_VALUE;
    }

    return NO_ERROR;
}

status_t UvcMJPGDevice::initSensorInfo(const CameraInfo& info)
{
    FLOGI("UvcMJPGDevice::initSensorInfo");

    if (mCameraHandle < 0) {
        FLOGE("OvDevice: initParameters sensor has not been opened");
        return BAD_VALUE;
    }
    pDevPath = info.devPath;

    // first read sensor format.
    int ret = 0, index = 0;
    int sensorFormats[MAX_SENSOR_FORMAT];
    memset(mAvailableFormats, 0, sizeof(mAvailableFormats));
    memset(sensorFormats, 0, sizeof(sensorFormats));
    memset(mPreviewResolutions, 0, sizeof(mPreviewResolutions));
    memset(mPictureResolutions, 0, sizeof(mPictureResolutions));

    struct v4l2_fmtdesc vid_fmtdesc;
    bool supportMJPG = false;
    while (ret == 0) {
        vid_fmtdesc.index = index;
        vid_fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ret               = ioctl(mCameraHandle, VIDIOC_ENUM_FMT, &vid_fmtdesc);
        FLOGI("index:%d,ret:%d, format:%c%c%c%c", index, ret,
                     vid_fmtdesc.pixelformat & 0xFF,
                     (vid_fmtdesc.pixelformat >> 8) & 0xFF,
                     (vid_fmtdesc.pixelformat >> 16) & 0xFF,
                     (vid_fmtdesc.pixelformat >> 24) & 0xFF);
        if (ret == 0) {
            sensorFormats[index++] = vid_fmtdesc.pixelformat;
        }

        if(vid_fmtdesc.pixelformat == v4l2_fourcc('M', 'J', 'P', 'G')) {
            supportMJPG = true;
        }
    }
    sensorFormats[index++] = v4l2_fourcc('B', 'L', 'O', 'B');
    sensorFormats[index++] = v4l2_fourcc('R', 'A', 'W', 'S');

    if(!supportMJPG) {
        ALOGE("UvcMJPGDevice::initSensorInfo, not support MJPG, setprop %s to 0", UVC_USE_MJPG);
        return BAD_VALUE;
    }
    
    //mAvailableFormatCount = index;
    adjustSensorFormats(sensorFormats, index);
    if (mDefaultFormat == 0) {
        FLOGE("Error: invalid mDefaultFormat:%d", mDefaultFormat);
        return BAD_VALUE;
    }

    FLOGI("initSensorInfo, use MJPG as pixel format");
    
    ret = 0;
    index = 0;
    char TmpStr[20];
    int  previewCnt = 0, pictureCnt = 0;
    int  ResMapCnt = 0;
    struct v4l2_frmsizeenum vid_frmsize;
    struct v4l2_frmivalenum vid_frmval;
    while (ret == 0) {
        memset(TmpStr, 0, 20);
        memset(&vid_frmsize, 0, sizeof(struct v4l2_frmsizeenum));
        vid_frmsize.index        = index++;
        vid_frmsize.pixel_format = v4l2_fourcc('M', 'J', 'P', 'G');
            //convertPixelFormatToV4L2Format(mDefaultFormat);
        ret = ioctl(mCameraHandle,
                    VIDIOC_ENUM_FRAMESIZES, &vid_frmsize);
        if (vid_frmsize.discrete.width == 1600 &&
                     vid_frmsize.discrete.height == 1200) {
            continue;
        }
            
        if (ret == 0) {
            FLOGI("enum frame size w:%d, h:%d",
                       vid_frmsize.discrete.width, vid_frmsize.discrete.height);
            memset(&vid_frmval, 0, sizeof(struct v4l2_frmivalenum));
            vid_frmval.index        = 0;
            vid_frmval.pixel_format = vid_frmsize.pixel_format;
            vid_frmval.width        = vid_frmsize.discrete.width;
            vid_frmval.height       = vid_frmsize.discrete.height;

            ret = ioctl(mCameraHandle, VIDIOC_ENUM_FRAMEINTERVALS, &vid_frmval);
            if (ret == 0) {
                FLOGI("vid_frmval denominator:%d, numeraton:%d",
                             vid_frmval.discrete.denominator,
                             vid_frmval.discrete.numerator);

                //VPU decoder need 16 pixels align
                mResMap[ResMapCnt].nUVCWidth = vid_frmsize.discrete.width;
                mResMap[ResMapCnt].nUVCHeight = vid_frmsize.discrete.height;
                mResMap[ResMapCnt].nVPUWidth = Align(mResMap[ResMapCnt].nUVCWidth, FRAME_ALIGN);
                mResMap[ResMapCnt].nVPUHeight = Align(mResMap[ResMapCnt].nUVCHeight, FRAME_ALIGN);
                

                FLOGI("count %d, uvc res %dx%d map to vpu res %dx%d", ResMapCnt,
                    mResMap[ResMapCnt].nUVCWidth, mResMap[ResMapCnt].nUVCHeight,
                    mResMap[ResMapCnt].nVPUWidth, mResMap[ResMapCnt].nVPUHeight);
                    
                mPictureResolutions[pictureCnt++] = mResMap[ResMapCnt].nVPUWidth;
                mPictureResolutions[pictureCnt++] = mResMap[ResMapCnt].nVPUHeight;

                if (vid_frmval.discrete.denominator /
                    vid_frmval.discrete.numerator > 15) {
                    mPreviewResolutions[previewCnt++] = mResMap[ResMapCnt].nVPUWidth;
                    mPreviewResolutions[previewCnt++] = mResMap[ResMapCnt].nVPUHeight;
                }

                ResMapCnt++;                
            }
        }
    } // end while

    mResCount = ResMapCnt;
    mPreviewResolutionCount = previewCnt;
    mPictureResolutionCount = pictureCnt;

    mMinFrameDuration = 33331760L;
    mMaxFrameDuration = 30000000000L;
    int i;
    for (i=0; i<MAX_RESOLUTION_SIZE && i<pictureCnt; i+=2) {
        FLOGI("SupportedPictureSizes: %d x %d", mPictureResolutions[i], mPictureResolutions[i+1]);
    }

    adjustPreviewResolutions();
    for (i=0; i<MAX_RESOLUTION_SIZE && i<previewCnt; i+=2) {
        FLOGI("SupportedPreviewSizes: %d x %d", mPreviewResolutions[i], mPreviewResolutions[i+1]);
    }
    FLOGI("FrameDuration is %lld, %lld", mMinFrameDuration, mMaxFrameDuration);

    i = 0;
    mTargetFpsRange[i++] = 10;
    mTargetFpsRange[i++] = 15;
    mTargetFpsRange[i++] = 25;
    mTargetFpsRange[i++] = 30;

    setMaxPictureResolutions();
    FLOGI("mMaxWidth:%d, mMaxHeight:%d", mMaxWidth, mMaxHeight);
    mFocalLength = 3.42f;
    mPhysicalWidth = 3.673f;
    mPhysicalHeight = 2.738f;

    return NO_ERROR;
}

status_t UvcMJPGDevice::fillCameraFrame(CameraFrame *frame)
{   
    status_t ret = NO_ERROR;

    if (!mVideoInfo->isStreamOn) {
        return NO_ERROR;
    }  

    int i = frame->mBindUVCBufIdx;   

    // It's frame reserved in server side at begin.
    // It's not registered in VPU, just igore it.
    if (i < 0) { 
        return NO_ERROR;
    } 

    struct v4l2_buffer cfilledbuffer; 
    memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer)); 
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_MMAP;
    cfilledbuffer.index    = i;
    cfilledbuffer.m.offset = mUvcBuffers[i].mPhyAddr; 

    ret = ioctl(mCameraHandle, VIDIOC_QBUF, &cfilledbuffer); 
    if (ret < 0) {
        FLOGE("fillCameraFrame: VIDIOC_QBUF Failed, i %d", i);
        return BAD_VALUE; 
    }
    mQueued++; 

    Mutex::Autolock lock(mVPULock);   

    if(frame->mpFrameBuf) { 
        VpuDecRetCode retCode;
        retCode = VPU_DecOutFrameDisplayed(mVPUHandle, (VpuFrameBuffer *)frame->mpFrameBuf);     
        if(VPU_DEC_RET_SUCCESS != retCode) {       
            FLOGE("%s: vpu clear frame display failure: ret=%d \r\n",__FUNCTION__,ret); 
            ret = BAD_VALUE;  
            }
        }
    return ret;
}


CameraFrame * UvcMJPGDevice::acquireCameraFrame()
{
    int capLoop = 0;
    
CaptureFrame:

    capLoop++;

    if(capLoop > 100) {
        FLOGE("UvcMJPGDevice::acquireCameraFrame, too much capLoop %d", capLoop);
        return NULL;
    }   

    int n;
    fd_set rfds;
    struct timeval tv;
    struct v4l2_buffer cfilledbuffer;
    CameraFrame *camBuf = NULL;

    FD_ZERO(&rfds);
    FD_SET(mCameraHandle, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = MAX_DEQUEUE_WAIT_TIME*1000;

    n = select(mCameraHandle+1, &rfds, NULL, NULL, &tv);
    if(n < 0) {
        FLOGE("Error!Query the V4L2 Handler state error.");
    }
    else if(n == 0) {
        FLOGI("Warning!Time out wait for V4L2 capture reading operation!");
    }
    else if(FD_ISSET(mCameraHandle, &rfds)) {
        memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cfilledbuffer.memory = V4L2_MEMORY_MMAP;
        int rtval;
        rtval = ioctl(mCameraHandle, VIDIOC_DQBUF, &cfilledbuffer);
        if (rtval < 0) {
            FLOGE("Camera VIDIOC_DQBUF failure, ret=%d", rtval);
            return camBuf;
        }	    
        mQueued --;
        
        int index = cfilledbuffer.index;
        fAssert(index >= 0 && index < mBufferCount);       

        if(mImageCapture) {
            camBuf = mDeviceBufs[index];
            camBuf->mTimeStamp = systemTime();
            camBuf->mBindUVCBufIdx = index;
            camBuf->mpFrameBuf = NULL;            
            CheckAndInsertHufTab((u8 *)camBuf->mVirtAddr, (u8 *)mUvcBuffers[index].mVirtAddr, camBuf->mSize, mUvcBuffers[index].mSize);            

            return camBuf;
        }        
        
        int JPGLen;
        JPGLen = cfilledbuffer.length;
        camBuf = VPUDec(mDeviceBufs, mBufferCount, (u8 *)mUvcBuffers[index].mVirtAddr, JPGLen, index, mUvcBuffers[index].mWidth, mUvcBuffers[index].mHeight);
        if(camBuf) {
            //need yuv422 sp
            if((meOutColorFmt != DEC_OUT_422H) && (meOutColorFmt != DEC_OUT_422V)) {
                ALOGE("%s, meOutColorFmt %d not supported", __FUNCTION__, meOutColorFmt);
                return NULL;
            }
            
            camBuf->mTimeStamp = systemTime(); //capturedStamp;
        }
        else { //VPU can't output one frame after feed stream
            FLOGI("VPU can't output one frame after feed stream, cap and feed again, capLoop %d", capLoop);
            ioctl(mCameraHandle, VIDIOC_QBUF, &cfilledbuffer);
            if (rtval < 0) {
                FLOGE("Camera VIDIOC_DQBUF failure, ret=%d", rtval);
                return NULL;
            }
            mQueued++;

            goto CaptureFrame;
        }    
       
    }
    else {
        FLOGE("Error!Query the V4L2 Handler state, no known error.");
    }

    return camBuf;
}


status_t UvcMJPGDevice::startDeviceLocked()
{
    FLOGI("UvcMJPGDevice::startDeviceLocked");

    status_t ret = NO_ERROR;
    struct v4l2_buffer cfilledbuffer;

    fAssert(mBufferProvider != NULL);

    if(!mImageCapture) {        
        int vpuRet;
        vpuRet = VPUInit();
        if(vpuRet) {
            FLOGE("VPUInit failed, vpuRet %d", vpuRet);
            return BAD_VALUE;
        }
    }

    int state;
    for (int i = 0; i < mBufferCount; i++) {
        CameraFrame* frame = mDeviceBufs[i];
        state = frame->getState();
        if (state != CameraFrame::BUFS_FREE) {
            continue;
        }
        frame->setState(CameraFrame::BUFS_IN_DRIVER);

        memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cfilledbuffer.memory = V4L2_MEMORY_MMAP;
        cfilledbuffer.index    = i;
        cfilledbuffer.m.offset = mUvcBuffers[i].mPhyAddr;
        ret = ioctl(mCameraHandle, VIDIOC_QBUF, &cfilledbuffer);
        if (ret < 0) {
            FLOGE("VIDIOC_QBUF Failed");
            return BAD_VALUE;
        }
        FLOGI("startDeviceLocked, buf %d queued", i);

        mQueued++;        
    }

    FLOGI("startDeviceLocked, que v4l2 buffer num %d, mQueued %d", mBufferCount, mQueued);
    
    enum v4l2_buf_type bufType;
    if (!mVideoInfo->isStreamOn) {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        ret = ioctl(mCameraHandle, VIDIOC_STREAMON, &bufType);
        if (ret < 0) {
            FLOGE("VIDIOC_STREAMON failed: %s", strerror(errno));
            return ret;
        }

        mVideoInfo->isStreamOn = true;
    }

    mDeviceThread = new DeviceThread(this);

    FLOGI("Created device thread");
    return ret;

}

status_t UvcMJPGDevice::stopDeviceLocked()
{
    FLOGI("UvcMJPGDevice::stopDeviceLocked");    

    int ret = 0;
    ret = DeviceAdapter::stopDeviceLocked();
    if (ret != 0) {
        FLOGE("call %s failed", __FUNCTION__);
        return ret;
    }

    for (int i = 0; i < mBufferCount; i++) {
        if (mUvcBuffers[i].mVirtAddr != NULL && mUvcBuffers[i].mSize > 0) {
            munmap(mUvcBuffers[i].mVirtAddr, mUvcBuffers[i].mSize);
        }
    }
    
    if (mCameraHandle > 0) {
        close(mCameraHandle);
        mCameraHandle = -1;
    }
    
    if(!mImageCapture) {
        VPUExit();
    }

    return ret;
}

int UvcMJPGDevice::VPUInit()
{
	VpuVersionInfo ver;
	VpuDecRetCode ret;
	VpuWrapperVersionInfo w_ver;
   	VpuMemInfo memInfo;
    VpuDecOpenParam decOpenParam;
	int capability=0;    

	//Initial decode context
	mDecContxt.fin = NULL;
	mDecContxt.fout = NULL;
	mDecContxt.nMaxNum = 0x7FFFFFFF;
	mDecContxt.nDisplay = 0; // -d
	mDecContxt.nFbNo = 0; // -d
	mDecContxt.nCodec = 11; // -f (Fixed as JPG)
	//decContxt.nInWidth=ioParams.width;
	//decContxt.nInHeight=ioParams.height;	
	mDecContxt.nSkipMode = 0; // -s
	mDecContxt.nDelayBufSize = DEFAULT_DELAY_BUFSIZE; 
	mDecContxt.nRepeatNum = 0; // -r
	mDecContxt.nOffset = 0; // -r
	mDecContxt.nUnitDataSize = DEFAULT_FILL_DATA_UNIT;
	mDecContxt.nUintDataNum = 0x7FFFFFFF;
	mDecContxt.nChromaInterleave = 1;
	mDecContxt.nMapType = 0;
	mDecContxt.nTile2LinearEnable = 0;

	int nUnitDataSize=mDecContxt.nUnitDataSize;

	//clear 0
	memset(&memInfo,0,sizeof(memInfo));
	memset(&mDecMemInfo,0,sizeof(mDecMemInfo));

    FLOGI("UvcMJPGDevice::VPUInit");
    
	//load vpu
	ret = VPU_DecLoad();
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		FLOGE("%s: vpu load failure: ret=%d \r\n",__FUNCTION__,ret);	
		return 1;
	}

	ret = VPU_DecGetVersionInfo(&ver);
	if (ret != VPU_DEC_RET_SUCCESS){
		FLOGE("%s: vpu get version failure: ret=%d \r\n",  __FUNCTION__, ret);
		goto bail;
	}	

	FLOGI("vpu lib version : major.minor.rel=%d.%d.%d \r\n",ver.nLibMajor,ver.nLibMinor,ver.nLibRelease);
	FLOGI("vpu fw version : major.minor.rel_rcode=%d.%d.%d_r%d \r\n",ver.nFwMajor,ver.nFwMinor,ver.nFwRelease,ver.nFwCode);

	//wrapper version info
	ret = VPU_DecGetWrapperVersionInfo(&w_ver);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		FLOGE("%s: vpu get wrapper version failure: ret=%d \r\n",__FUNCTION__,ret);
		goto bail;
	}
	FLOGI("vpu wrapper version : major.minor.rel=%d.%d.%d: %s \r\n",w_ver.nMajor,w_ver.nMinor,w_ver.nRelease,w_ver.pBinary);

	//query memory
	ret = VPU_DecQueryMem(&memInfo);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		FLOGE("%s: vpu query memory failure: ret=%d \r\n",__FUNCTION__,ret);
		goto bail;		
	}

	//malloc memory for vpu wrapper
	if(MallocMemBlock(&memInfo,&mDecMemInfo) == 0)
	{
		FLOGE("%s: malloc memory failure: \r\n",__FUNCTION__);
		goto bail;		
	}

    memset(&decOpenParam, 0, sizeof(decOpenParam));
	//set open params
	if(ConvertCodecFormat(mDecContxt.nCodec, &decOpenParam.CodecFormat) == 0)
	{
		FLOGE("%s: unsupported codec format: id=%d \r\n",__FUNCTION__, mDecContxt.nCodec);
		goto bail;		
	}	
	
	decOpenParam.nReorderEnable=1;	//for H264
	decOpenParam.nEnableFileMode=0;	//unit test: using stream mode

	//check capabilities
	VPU_DecGetCapability((VpuDecHandle)NULL, VPU_DEC_CAP_FILEMODE, &capability);
	FLOGI("capability: file mode supported: %d \r\n",capability);
	VPU_DecGetCapability((VpuDecHandle)NULL, VPU_DEC_CAP_TILE, &capability);
	FLOGI("capability: tile format supported: %d \r\n",capability);
	if((capability==0)&&(mDecContxt.nMapType!=0))
	{
		FLOGW("WARNING: tile format is not supported \r\n");
	}

	decOpenParam.nChromaInterleave=mDecContxt.nChromaInterleave;
	decOpenParam.nMapType=mDecContxt.nMapType;
	decOpenParam.nTiled2LinearEnable=mDecContxt.nTile2LinearEnable;

	// open vpu
	ret = VPU_DecOpen(&mVPUHandle, &decOpenParam, &memInfo);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		FLOGE("%s: vpu open failure: ret=%d \r\n", __FUNCTION__, ret);
		return 1;
	}

	return 0;

bail:
	//release mem
	if(0==FreeMemBlock(&mDecMemInfo))
	{
		FLOGE("%s: mmfree memory failure:  \r\n",__FUNCTION__);
	}


	//unload
	ret = VPU_DecUnLoad();
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		FLOGE("%s: vpu unload failure: ret=%d \r\n",__FUNCTION__,ret);
	}

	return 1;
}

int UvcMJPGDevice::VPUExit()
{
	VpuDecRetCode ret;

    FLOGI("UvcMJPGDevice::VPUExit");
    
	// close vpu
	ret = VPU_DecClose(mVPUHandle);
	if (ret != VPU_DEC_RET_SUCCESS)
	{
		FLOGE("%s: vpu close failure: ret=%d \r\n", __FUNCTION__, ret);
		return 1;
	}

    //release mem
    FLOGI("FreeMemBlock");
	if(0==FreeMemBlock(&mDecMemInfo))
	{
		FLOGE("%s: mmfree memory failure:  \r\n",__FUNCTION__);
        return 1;
	}
    

	return 0;
}

CameraFrame *UvcMJPGDevice::VPUDec(CameraFrame **ppDeviceBuf, unsigned int BufNum, u8 *InVirAddr, u32 inLen, unsigned int nUVCBufIdx, unsigned int /*w*/, unsigned int /*h*/)
{
DecLogic:
	VpuDecRetCode ret; 
	int bufRetCode = 0;
	int err = 0;

	//DecContxt * decContxt;
	DecMemInfo pDecMemInfo;

	VpuBufferNode InData;

	CameraFrame *pCamFrame = NULL;
	unsigned int outIdx = 0;

	Mutex::Autolock lock(mVPULock);    
    
	InData.nSize = inLen;
	InData.pPhyAddr = NULL;
	InData.pVirAddr = InVirAddr;
	InData.sCodecData.pData = NULL;
	InData.sCodecData.nSize = 0;

	ret = VPU_DecDecodeBuf(mVPUHandle, &InData, &bufRetCode);
	//FLOGI("%s: VPU_DecDecodeBuf: ret = %d, bufRetCode = %d \r\n", __FUNCTION__, ret, bufRetCode);

	// check init info
    if(bufRetCode & VPU_DEC_INIT_OK) {
		FLOGI("%s: vpu & VPU_DEC_INIT_OK \r\n", __FUNCTION__);
        int nFrmNum;
        VpuDecInitInfo InitInfo;
        
		//process init info
		if(ProcessInitInfo(&InitInfo, &pDecMemInfo, &nFrmNum, mVPUPhyAddr, mVPUVirtAddr, NUM_PREVIEW_BUFFER+2) == 0)
		{
			FLOGE("%s: vpu process init info failure: \r\n", __FUNCTION__);
			return NULL;
		}
        
        goto DecLogic;
	}       

//    FLOGI("bufRetCode 0x%x, DIS 0x%x, MOSAIC_DIS 0x%x", bufRetCode, VPU_DEC_OUTPUT_DIS, VPU_DEC_OUTPUT_MOSAIC_DIS);
	//check output buff
    if((bufRetCode & VPU_DEC_OUTPUT_DIS) ||(bufRetCode & VPU_DEC_OUTPUT_MOSAIC_DIS))
	{
	    VpuDecOutFrameInfo frameInfo;

		// get output frame
        ret = VPU_DecGetOutputFrame(mVPUHandle, &frameInfo);
		if(ret != VPU_DEC_RET_SUCCESS)
		{
			FLOGE("%s: vpu get output frame failure: ret=%d \r\n",__FUNCTION__,ret);	
			return NULL;
		}


        unsigned int i;
    	for(i = 0; i < BufNum; i++) {
      		if(frameInfo.pDisplayFrameBuf->pbufY == (unsigned char* )ppDeviceBuf[i]->mPhyAddr) {                
    			pCamFrame = ppDeviceBuf[i];                
                break;
            }
    	}

    	//shoud never happened
    	fAssert(i < BufNum);
        
        //remember the uvc buffer index
    	pCamFrame->mBindUVCBufIdx = nUVCBufIdx;
    	pCamFrame->mpFrameBuf = (void *)frameInfo.pDisplayFrameBuf;
           
    } else if(bufRetCode & VPU_DEC_NO_ENOUGH_BUF) {
        mVPULock.unlock();            
        usleep(10000);
        FLOGI("VPU_DEC_NO_ENOUGH_BUF, wait 10ms, goto DecLogic");
        goto DecLogic;
    }	
	
	return pCamFrame;

	//return ret;
}

int UvcMJPGDevice::ProcessInitInfo(VpuDecInitInfo* pInitInfo, DecMemInfo* /*pDecMemInfo*/, int*pOutFrmNum,
    unsigned char **pPhyAddr, unsigned char **pVirtAddr, unsigned int registerNum)
{
	VpuDecRetCode ret;
	VpuFrameBuffer frameBuf[MAX_FRAME_NUM];
	VpuMemDesc vpuMem;
	int requestedBufNum;
	int i;
	int totalSize=0;
	int mvSize=0;
	int ySize=0;
	int uSize=0;
	int vSize=0;
	int yStride=0;
	int uStride=0;
	int vStride=0;
	unsigned char* ptr;
	unsigned char* ptrVirt;
	int nAlign;
	int multifactor=1;

    FLOGI("enter ProcessInitInfo, registerNum %d", registerNum);
    
	//get init info	
	ret=VPU_DecGetInitialInfo(mVPUHandle, pInitInfo);
	if(VPU_DEC_RET_SUCCESS!=ret)
	{
		FLOGE("%s: vpu get init info failure: ret=%d \r\n",__FUNCTION__,ret);	
		return 0;
	}
    
	//malloc frame buffs
	requestedBufNum=pInitInfo->nMinFrameBufferCount+FRAME_SURPLUS;
    FLOGI("VPU requested requestedBufNum %d, minCount %d", requestedBufNum, pInitInfo->nMinFrameBufferCount);
    
	if(requestedBufNum>MAX_FRAME_NUM)
	{
		FLOGE("%s: vpu request too many frames : num=0x%X \r\n",__FUNCTION__,pInitInfo->nMinFrameBufferCount);	
		return 0;		
	}

    if((unsigned int)requestedBufNum > registerNum)
    {
        FLOGE("vpu requested requestedBufNum(%d) > registerNum(%d)", requestedBufNum, registerNum);
        return 0;
    }

    requestedBufNum = registerNum;
        
	yStride=Align(pInitInfo->nPicWidth,FRAME_ALIGN);
    
	if(pInitInfo->nInterlace)
	{
		ySize = Align(pInitInfo->nPicWidth,FRAME_ALIGN)*Align(pInitInfo->nPicHeight,(2*FRAME_ALIGN));
	}
	else
	{
		ySize = Align(pInitInfo->nPicWidth,FRAME_ALIGN)*Align(pInitInfo->nPicHeight,FRAME_ALIGN);
	}

    FLOGI("nInterlace %d, ySize %d", pInitInfo->nInterlace, ySize);
    

	//for MJPG: we need to check 4:4:4/4:2:2/4:2:0/4:0:0
	VpuCodStd vpuCodec = VPU_V_MPEG4;
    
	ConvertCodecFormat(mDecContxt.nCodec, &vpuCodec);        
	if(VPU_V_MJPG==vpuCodec)
	{
		switch(pInitInfo->nMjpgSourceFormat)
		{
			case 0:	//4:2:0
				FLOGI("MJPG: 4:2:0 \r\n");
				uStride=yStride/2;
				vStride=uStride;
				uSize=ySize/4;
				vSize=uSize;	
				mvSize=uSize;
				mDecContxt.eOutColorFmt=DEC_OUT_420;
				break;
			case 1:	//4:2:2 hor
				FLOGI("MJPG: 4:2:2 hor \r\n");
				uStride=yStride/2;
				vStride=uStride;
				uSize=ySize/2;
				vSize=uSize;	
				mvSize=uSize;
				mDecContxt.eOutColorFmt=DEC_OUT_422H;
				break;
			case 2:	//4:2:2 ver
				FLOGI("MJPG: 4:2:2 ver \r\n");				
				uStride=yStride;
				vStride=uStride;
				uSize=ySize/2;
				vSize=uSize;	
				mvSize=uSize;
				mDecContxt.eOutColorFmt=DEC_OUT_422V;
				break;
			case 3:	//4:4:4
				FLOGI("MJPG: 4:4:4 \r\n");				
				uStride=yStride;
				vStride=uStride;
				uSize=ySize;
				vSize=uSize;	
				mvSize=uSize;
				mDecContxt.eOutColorFmt=DEC_OUT_444;
				break;
			case 4:	//4:0:0
				FLOGI("MJPG: 4:0:0 \r\n");				
				uStride=0;
				vStride=uStride;
				uSize=0;
				vSize=uSize;	
				mvSize=uSize;
				mDecContxt.eOutColorFmt=DEC_OUT_400;
				break;
			default:	//4:2:0
				FLOGI("unknown color format: %d \r\n",vpuCodec);
				uStride=yStride/2;
				vStride=uStride;
				uSize=ySize/4;
				vSize=uSize;	
				mvSize=uSize;
				mDecContxt.eOutColorFmt=DEC_OUT_420;
				break;			
		}
	}
	else
	{
		//4:2:0 for all video
		uStride=yStride/2;
		vStride=uStride;
		uSize=ySize/4;
		vSize=uSize;	
		mvSize=uSize;
		mDecContxt.eOutColorFmt=DEC_OUT_420;
	}


    meOutColorFmt = mDecContxt.eOutColorFmt;

   
	nAlign = pInitInfo->nAddressAlignment;
	if(mDecContxt.nMapType == 2)
	{
		//only consider Y since interleave must be enabled
		multifactor = 2;	//for field, we need to consider alignment for top and bot
	}
	if(nAlign > 1)
	{
		ySize=Align(ySize,multifactor*nAlign);
		uSize=Align(uSize,nAlign);
		vSize=Align(vSize,nAlign);
	}

    FLOGI("ySize %d", ySize);
    
	for(i=0;i<requestedBufNum;i++)
	{
		totalSize=(ySize+uSize+vSize+mvSize+nAlign)*1;
        
        //fill frameBuf from user buffer
        ptr=pPhyAddr[i];
        ptrVirt=pVirtAddr[i];

		/*align the base address*/
		if(nAlign>1)
		{
			ptr=(unsigned char*)Align(ptr,nAlign);
			ptrVirt=(unsigned char*)Align(ptrVirt,nAlign);
		}

        FLOGI("VPU reg buf, idx %d, ptr phy %p, vir %p", i, ptr, ptrVirt);

		/* fill stride info */
		frameBuf[i].nStrideY=yStride;
		frameBuf[i].nStrideC=uStride;	

		/* fill phy addr*/
		frameBuf[i].pbufY=ptr;
		frameBuf[i].pbufCb=ptr+ySize;
		frameBuf[i].pbufCr=ptr+ySize+uSize;
		frameBuf[i].pbufMvCol=ptr+ySize+uSize+vSize;
		//ptr+=ySize+uSize+vSize+mvSize;
		/* fill virt addr */
		frameBuf[i].pbufVirtY=ptrVirt;
		frameBuf[i].pbufVirtCb=ptrVirt+ySize;
		frameBuf[i].pbufVirtCr=ptrVirt+ySize+uSize;
		frameBuf[i].pbufVirtMvCol=ptrVirt+ySize+uSize+vSize;
		//ptrVirt+=ySize+uSize+vSize+mvSize;


		/* fill bottom address for field tile*/
		if(mDecContxt.nMapType==2)
		{
			frameBuf[i].pbufY_tilebot=frameBuf[i].pbufY+ySize/2;
			frameBuf[i].pbufCb_tilebot=frameBuf[i].pbufCr;
			frameBuf[i].pbufVirtY_tilebot=frameBuf[i].pbufVirtY+ySize/2;
			frameBuf[i].pbufVirtCb_tilebot=frameBuf[i].pbufVirtCr;			
		}
		else
		{
			frameBuf[i].pbufY_tilebot=0;
			frameBuf[i].pbufCb_tilebot=0;
			frameBuf[i].pbufVirtY_tilebot=0;
			frameBuf[i].pbufVirtCb_tilebot=0;
		}
	}

	//register frame buffs
	ret=VPU_DecRegisterFrameBuffer(mVPUHandle, frameBuf, requestedBufNum);
	if(VPU_DEC_RET_SUCCESS!=ret)
	{
		FLOGE("%s: vpu register frame failure: ret=%d \r\n",__FUNCTION__,ret);	
		return 0;
	}	

	*pOutFrmNum=requestedBufNum;
	return 1;
}

int UvcMJPGDevice::FreeMemBlock(DecMemInfo* pDecMem)
{
	int i;
	VpuMemDesc vpuMem;
	VpuDecRetCode vpuRet;
	int retOk=1;

	//free virtual mem
	for(i=0;i<pDecMem->nVirtNum;i++)
	{
		if((void*)pDecMem->virtMem[i]) free((void*)pDecMem->virtMem[i]);
	}
	pDecMem->nVirtNum=0;

	//free physical mem
	for(i=0;i<pDecMem->nPhyNum;i++)
	{
		vpuMem.nPhyAddr=pDecMem->phyMem_phyAddr[i];
		vpuMem.nVirtAddr=pDecMem->phyMem_virtAddr[i];
		vpuMem.nCpuAddr=pDecMem->phyMem_cpuAddr[i];
		vpuMem.nSize=pDecMem->phyMem_size[i];
		vpuRet=VPU_DecFreeMem(&vpuMem);
		if(vpuRet!=VPU_DEC_RET_SUCCESS)
		{
			FLOGE("%s: free vpu memory failure : ret=%d \r\n",__FUNCTION__,vpuRet);
			retOk=0;
		}
	}
	pDecMem->nPhyNum	=0;
	
	return retOk;
}

int UvcMJPGDevice::MallocMemBlock(VpuMemInfo* pMemBlock,DecMemInfo* pDecMem)
{
	int i;
	unsigned char * ptr=NULL;
	int size;
	
	for(i=0;i<pMemBlock->nSubBlockNum;i++)
	{
		size=pMemBlock->MemSubBlock[i].nAlignment+pMemBlock->MemSubBlock[i].nSize;
		if(pMemBlock->MemSubBlock[i].MemType==VPU_MEM_VIRT)
		{
			ptr=(unsigned char *)malloc(size);
			if(ptr==NULL)
			{
				FLOGE("%s: get virtual memory failure, size=%d \r\n",__FUNCTION__,size);
				goto failure;
			}		
			pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char*)Align(ptr,pMemBlock->MemSubBlock[i].nAlignment);

			//record virtual base addr
			pDecMem->virtMem[pDecMem->nVirtNum]=(unsigned int)ptr;
			pDecMem->nVirtNum++;
		}
		else// if(memInfo.MemSubBlock[i].MemType==VPU_MEM_PHY)
		{
			VpuMemDesc vpuMem;
			VpuDecRetCode ret;
			vpuMem.nSize=size;
			ret=VPU_DecGetMem(&vpuMem);
			if(ret!=VPU_DEC_RET_SUCCESS)
			{
				FLOGE("%s: get vpu memory failure, size=%d, ret=%d \r\n",__FUNCTION__,size,ret);
				goto failure;
			}		
			pMemBlock->MemSubBlock[i].pVirtAddr=(unsigned char*)Align(vpuMem.nVirtAddr,pMemBlock->MemSubBlock[i].nAlignment);
			pMemBlock->MemSubBlock[i].pPhyAddr=(unsigned char*)Align(vpuMem.nPhyAddr,pMemBlock->MemSubBlock[i].nAlignment);

			//record physical base addr
			pDecMem->phyMem_phyAddr[pDecMem->nPhyNum]=(unsigned int)vpuMem.nPhyAddr;
			pDecMem->phyMem_virtAddr[pDecMem->nPhyNum]=(unsigned int)vpuMem.nVirtAddr;
			pDecMem->phyMem_cpuAddr[pDecMem->nPhyNum]=(unsigned int)vpuMem.nCpuAddr;
			pDecMem->phyMem_size[pDecMem->nPhyNum]=size;
			pDecMem->nPhyNum++;			
		}
	}	

	return 1;
	
failure:
	FreeMemBlock(pDecMem);
	return 0;
	
}

int UvcMJPGDevice::ConvertCodecFormat(int codec, VpuCodStd* pCodec)
{
	switch (codec)
	{
		case 1:
			*pCodec=VPU_V_MPEG2;
			break;
		case 2:
			*pCodec=VPU_V_MPEG4;
			break;			
		case 3:
			*pCodec=VPU_V_DIVX3;
			break;
		case 4:
			*pCodec=VPU_V_DIVX4;
			break;
		case 5:
			*pCodec=VPU_V_DIVX56;
			break;
		case 6:
			*pCodec=VPU_V_XVID;
			break;			
		case 7:
			*pCodec=VPU_V_H263;
			break;
		case 8:
			*pCodec=VPU_V_AVC;
			break;
		case 9:
			*pCodec=VPU_V_VC1; //VPU_V_VC1_AP
			break;
		case 10:
			*pCodec=VPU_V_RV;
			break;			
		case 11:
			*pCodec=VPU_V_MJPG;
			break;
		case 12:
			*pCodec=VPU_V_AVS;
			break;
		case 13:
			*pCodec=VPU_V_VP8;
			break;
		case 14:
			*pCodec=VPU_V_AVC_MVC;
			break;
		default:
			return 0;			
	}
	return 1;
}
 


