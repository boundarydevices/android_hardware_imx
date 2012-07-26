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

#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/time.h>
#include <linux/videodev2.h>
#include <linux/mxcfb.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <utils/threads.h>
#include <dirent.h>

#include <linux/videodev2.h>


#include "V4l2UVCDevice.h"

#define MAX_DEV_NAME_LENGTH 10

namespace android {

V4l2UVCDevice::V4l2UVCDevice()
{
    mCameraType = CAMERA_TYPE_UVC;
    memset(mUvcBuffers, 0 , sizeof(mUvcBuffers));
    mCaptureConfigNum = 0;
    mCurrentConfig = NULL;
    memset(mCaptureConfig, 0, sizeof(mCaptureConfig));

    mEnableCSC = false;
    mSensorFmtCnt = 0;
    mCscFmtCnt = 0;
    mActualCscFmtCnt = 0;
    memset(mSensorSupportFmt, 0, sizeof(mSensorSupportFmt));
    memset(mActualCscFmt, 0 , sizeof(mActualCscFmt));

    memset(mCscGroup, 0, sizeof(mCscGroup));
    //related to format support in CSC.
    mCscGroup[0].srcFormat = v4l2_fourcc('Y','U','Y','V');
    mCscGroup[0].dstFormat = v4l2_fourcc('N','V','1','2');
    mCscGroup[0].cscConvert = convertYUYUToNV12;
    mCscGroup[0].isSensorSupport = false;
    mCscGroup[0].isOverlapWithSensor = false;
    mDoCsc = NULL;
}

CAPTURE_DEVICE_RET V4l2UVCDevice::V4l2Open(int cameraId)
{
        CAMERA_LOG_FUNC;
        int fd = 0, i, j, is_found = 0;
        const char *flags[] = {"uncompressed", "compressed"};

        char   dev_node[CAMAERA_FILENAME_LENGTH];
        DIR *v4l_dir = NULL;
        struct dirent *dir_entry;
        struct v4l2_capability v4l2_cap;
        struct v4l2_fmtdesc vid_fmtdesc;
        struct v4l2_frmsizeenum vid_frmsize;
        CAPTURE_DEVICE_RET ret = CAPTURE_DEVICE_ERR_NONE;

        if(mCameraDevice > 0)
            return CAPTURE_DEVICE_ERR_ALRADY_OPENED;
        else if (mCaptureDeviceName[0] != '#'){
            CAMERA_LOG_RUNTIME("already get the device name %s", mCaptureDeviceName);
            mCameraDevice = open(mCaptureDeviceName, O_RDWR | O_NONBLOCK, 0);
            if (mCameraDevice < 0)
                return CAPTURE_DEVICE_ERR_OPEN;
        }
        else{
            CAMERA_LOG_RUNTIME("deviceName is %s", mInitalDeviceName);
            v4l_dir = opendir("/sys/class/video4linux");
            if (v4l_dir){
                while((dir_entry = readdir(v4l_dir))) {
                    memset((void *)dev_node, 0, CAMAERA_FILENAME_LENGTH);
                    if(strncmp(dir_entry->d_name, "video", 5))
                        continue;
                    sprintf(dev_node, "/dev/%s", dir_entry->d_name);
                    if ((fd = open(dev_node, O_RDWR | O_NONBLOCK, 0)) < 0)
                        continue;
                    CAMERA_LOG_RUNTIME("dev_node is %s", dev_node);
                    if(ioctl(fd, VIDIOC_QUERYCAP, &v4l2_cap) < 0 ) {
                        close(fd);
                        fd = 0;
                        continue;
                    } else if (v4l2_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
                        CAMERA_LOG_RUNTIME("dev_node: %s, sensor name: %s",
                                dev_node, v4l2_cap.driver);
                        if(strstr((const char*)v4l2_cap.driver, mInitalDeviceName)){
                            is_found = 1;
                            CAMERA_LOG_RUNTIME("find the correct sensor %s, len=%d", v4l2_cap.driver, strlen((const char*)v4l2_cap.driver));
                            strcpy(mInitalDeviceName, (const char*)v4l2_cap.driver);
                            strcpy(mCaptureDeviceName, dev_node);
                            break;
                        }
                        else {
                            close(fd);
                            fd = 0;
                        }
                    } else {
                        close(fd);
                        fd = 0;
                    }
                }
                closedir(v4l_dir);
            }
            if (fd > 0){
                mCameraDevice = fd;
            }
            else{
                CAMERA_LOG_ERR("The device name is not correct or the device is error");
                return CAPTURE_DEVICE_ERR_OPEN;
            }
        }
        CAMERA_LOG_INFO("device name is %s", mCaptureDeviceName);
        CAMERA_LOG_INFO("sensor name is %s", mInitalDeviceName);
        return ret;
}

CAPTURE_DEVICE_RET V4l2UVCDevice::V4l2RegisterBufs(DMA_BUFFER *DevBufQue, unsigned int *pBufQueNum)
{
    unsigned int i;
    struct v4l2_buffer buf;
    enum v4l2_buf_type type;
    struct v4l2_requestbuffers req;
    int BufQueNum;

    CAMERA_LOG_FUNC;
    if (mCameraDevice <= 0 || DevBufQue == NULL || pBufQueNum == NULL || *pBufQueNum == 0){
        return CAPTURE_DEVICE_ERR_BAD_PARAM;
    }

    mBufQueNum = *pBufQueNum;

    memset(&req, 0, sizeof (req));
    req.count = mBufQueNum;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(mCameraDevice, VIDIOC_REQBUFS, &req) < 0) {
        CAMERA_LOG_ERR("v4l_capture_setup: VIDIOC_REQBUFS failed\n");
        return CAPTURE_DEVICE_ERR_SYS_CALL;
    }

    /*the driver may can't meet the request, and return the buf num it can handle*/
    *pBufQueNum = mBufQueNum = req.count;

    for (i = 0; i < mBufQueNum; i++) {
        memset(&buf, 0, sizeof (buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.index = i;
        if (ioctl(mCameraDevice, VIDIOC_QUERYBUF, &buf) < 0) {
            CAMERA_LOG_ERR("VIDIOC_QUERYBUF error\n");
            return CAPTURE_DEVICE_ERR_SYS_CALL;
        } else {
            CAMERA_LOG_RUNTIME("VIDIOC_QUERYBUF ok\n");
        }

        mCaptureBuffers[i].length = DevBufQue[i].length = mUvcBuffers[i].length = buf.length;

        mCaptureBuffers[i].phy_offset = DevBufQue[i].phy_offset;
        mUvcBuffers[i].phy_offset = (size_t) buf.m.offset;

        mCaptureBuffers[i].virt_start = DevBufQue[i].virt_start;
        mUvcBuffers[i].virt_start = (unsigned char *)mmap (NULL, mUvcBuffers[i].length,
                    PROT_READ | PROT_WRITE, MAP_SHARED, mCameraDevice, mUvcBuffers[i].phy_offset);

        memset(mUvcBuffers[i].virt_start, 0xFF, mUvcBuffers[i].length);
        CAMERA_LOG_RUNTIME("user space buffers[%d].length = %d\n", i, mCaptureBuffers[i].length);
        CAMERA_LOG_RUNTIME("user space buffers[%d].phy_offset = 0x%x\n", i, mCaptureBuffers[i].phy_offset);
        CAMERA_LOG_RUNTIME("user space buffers[%d].virt_start = 0x%x\n", i, (unsigned int)(mCaptureBuffers[i].virt_start));
        CAMERA_LOG_RUNTIME("uvc driver buffers[%d].length = %d\n", i, mUvcBuffers[i].length);
        CAMERA_LOG_RUNTIME("uvc driver buffers[%d].phy_offset = 0x%x\n", i, mUvcBuffers[i].phy_offset);
        CAMERA_LOG_RUNTIME("uvc driver buffers[%d].virt_start = 0x%x\n", i, (unsigned int)(mUvcBuffers[i].virt_start));
    }

    return CAPTURE_DEVICE_ERR_NONE;
}

CAPTURE_DEVICE_RET V4l2UVCDevice::V4l2Prepare()
{
    CAMERA_LOG_FUNC;
    struct v4l2_buffer buf;
    mQueuedBufNum = 0;
    for (unsigned int i = 0; i < mBufQueNum; i++) {
        memset(&buf, 0, sizeof (struct v4l2_buffer));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.offset = mUvcBuffers[i].phy_offset;

        if (ioctl (mCameraDevice, VIDIOC_QBUF, &buf) < 0) {
            CAMERA_LOG_ERR("VIDIOC_QBUF error\n");
            return CAPTURE_DEVICE_ERR_SYS_CALL;
        }
        mQueuedBufNum ++;
    }

    return CAPTURE_DEVICE_ERR_NONE;
}

CAPTURE_DEVICE_RET V4l2UVCDevice::V4l2Dequeue(unsigned int *pBufQueIdx)
{
    CAPTURE_DEVICE_RET ret = CAPTURE_DEVICE_ERR_NONE;
    struct v4l2_buffer cfilledbuffer;
    int n;
    fd_set rfds;
    struct timeval tv;
    //CAMERA_LOG_FUNC;
    if (mCameraDevice <= 0 || mBufQueNum == 0 || mCaptureBuffers == NULL){
        return CAPTURE_DEVICE_ERR_OPEN;
    }

    FD_ZERO(&rfds);
    FD_SET(mCameraDevice, &rfds);
    tv.tv_sec = 0;
    tv.tv_usec = MAX_DEQUEUE_WAIT_TIME*1000;
    n = select(mCameraDevice+1, &rfds, NULL, NULL, &tv);
    if(n < 0) {
        CAMERA_LOG_ERR("Error!Query the V4L2 Handler state error.");
        ret = CAPTURE_DEVICE_ERR_SYS_CALL;
    }
    else if(n == 0) {
        CAMERA_LOG_INFO("Warning!Time out wait for V4L2 capture reading operation!");
        ret = CAPTURE_DEVICE_ERR_OPT_TIMEOUT;
    }
    else if(FD_ISSET(mCameraDevice, &rfds)) {
        memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cfilledbuffer.memory = V4L2_MEMORY_MMAP;
        int rtval;
        rtval = ioctl(mCameraDevice, VIDIOC_DQBUF, &cfilledbuffer);
        if (rtval < 0) {
            CAMERA_LOG_ERR("Camera VIDIOC_DQBUF failure, ret=%d", rtval);
            return CAPTURE_DEVICE_ERR_SYS_CALL;
        }
        *pBufQueIdx = cfilledbuffer.index;

        //should do hardware accelerate.
        if(mEnableCSC && mDoCsc) {
            mDoCsc->width = mCurrentConfig->width;
            mDoCsc->height = mCurrentConfig->height;
            mDoCsc->srcStride = mDoCsc->width;
            mDoCsc->dstStride = mDoCsc->width;
            mDoCsc->srcVirt = mUvcBuffers[*pBufQueIdx].virt_start;
            mDoCsc->dstVirt = mCaptureBuffers[*pBufQueIdx].virt_start;
            mDoCsc->srcPhy = mUvcBuffers[*pBufQueIdx].phy_offset;
            mDoCsc->dstPhy = mCaptureBuffers[*pBufQueIdx].phy_offset;
            mDoCsc->cscConvert(mDoCsc);
        }
        else
            memcpy(mCaptureBuffers[*pBufQueIdx].virt_start, mUvcBuffers[*pBufQueIdx].virt_start, mCaptureBuffers[*pBufQueIdx].length);

        mQueuedBufNum --;

        ret =  CAPTURE_DEVICE_ERR_NONE;
    }
    else {
        CAMERA_LOG_ERR("Error!Query the V4L2 Handler state, no known error.");
        ret = CAPTURE_DEVICE_ERR_UNKNOWN;
    }

    return ret;
}

CAPTURE_DEVICE_RET V4l2UVCDevice::V4l2Queue(unsigned int BufQueIdx)
{
    int ret;
    struct v4l2_buffer cfilledbuffer;
    //CAMERA_LOG_FUNC;
    if (mCameraDevice <= 0 || mBufQueNum == 0 || mCaptureBuffers == NULL){
        return CAPTURE_DEVICE_ERR_OPEN;
    }
    memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_MMAP;
    cfilledbuffer.index = BufQueIdx;
    ret = ioctl(mCameraDevice, VIDIOC_QBUF, &cfilledbuffer);
    if (ret < 0) {
        CAMERA_LOG_ERR("Camera VIDIOC_QBUF failure, ret=%d", ret);
        return CAPTURE_DEVICE_ERR_SYS_CALL;
    }

    mQueuedBufNum ++;

    return CAPTURE_DEVICE_ERR_NONE;
}

CAPTURE_DEVICE_RET V4l2UVCDevice::V4l2DeAlloc()
{

    CAMERA_LOG_FUNC;
    if (mCameraDevice <= 0 ){
        return CAPTURE_DEVICE_ERR_BAD_PARAM;
    }

    for (unsigned int i = 0; i < mBufQueNum; i++) {
        if (mUvcBuffers[i].length && (mUvcBuffers[i].virt_start > 0)) {
            munmap(mUvcBuffers[i].virt_start, mUvcBuffers[i].length);
            mUvcBuffers[i].length = 0;
            CAMERA_LOG_RUNTIME("munmap buffers 0x%x\n", (unsigned int)(mUvcBuffers[i].virt_start));
        }
    }
    return CAPTURE_DEVICE_ERR_NONE;
}

void V4l2UVCDevice::selectCscFunction(unsigned int format)
{
    CAMERA_LOG_FUNC;
    mDoCsc = NULL;
    for(int i=0; i<MAX_CSC_SUPPORT_FMT; i++) {
        if(mCscGroup[i].isSensorSupport == true && mCscGroup[i].isOverlapWithSensor == false &&
                     mCscGroup[i].dstFormat == format) {
            mDoCsc = &mCscGroup[i];
            CAMERA_LOG_RUNTIME("find the match mCscGroup[%d] CSC function", i);
        }
    }
}

unsigned int V4l2UVCDevice::queryCscSourceFormat(unsigned int format)
{
    CAMERA_LOG_FUNC;
    for(int i=0; i<MAX_CSC_SUPPORT_FMT; i++) {
        if(mCscGroup[i].isSensorSupport == true && mCscGroup[i].isOverlapWithSensor == false &&
                     mCscGroup[i].dstFormat == format) {
            CAMERA_LOG_RUNTIME("find the CSC source format=0x%x convert to dest format=0x%x",
                         mCscGroup[i].srcFormat, mCscGroup[i].dstFormat);
            return mCscGroup[i].srcFormat;
        }
    }

    CAMERA_LOG_ERR("invalidate format 0x%x in query", format);
    return 0;
}

bool V4l2UVCDevice::needDoCsc(unsigned int format)
{
    CAMERA_LOG_FUNC;
    unsigned int i;
    for(i=0; i < mActualCscFmtCnt; i++) {
        if(mActualCscFmt[i] == format)
            return true;
    }

    return false;
}

unsigned int V4l2UVCDevice::countActualCscFmt()
{
    CAMERA_LOG_FUNC;
    if(mSensorFmtCnt <= 0) {
        return 0;
    }

    unsigned int i, k;
    unsigned int n = 0;

    for(i=0; i < MAX_CSC_SUPPORT_FMT; i++) {
        for(k=0; k < mSensorFmtCnt; k++) {
            if(mCscGroup[i].srcFormat == mSensorSupportFmt[k]) {
                mCscGroup[i].isSensorSupport = true;
                break;
            }
        }
    }

    for(i=0; i < MAX_CSC_SUPPORT_FMT; i++) {
        for(k=0; k < mSensorFmtCnt; k++) {
            if(mCscGroup[i].isSensorSupport == true && mCscGroup[i].dstFormat == mSensorSupportFmt[k]) {
                mCscGroup[i].isOverlapWithSensor = true;
                break;
            }
        }
        if(mCscGroup[i].isSensorSupport == true && mCscGroup[i].isOverlapWithSensor == false) {
            mActualCscFmt[n++] = mCscGroup[i].dstFormat;
        }
    }

    return n;
}

CAPTURE_DEVICE_RET V4l2UVCDevice::V4l2EnumFmt(void *retParam)
{
    CAMERA_LOG_FUNC;

    CAPTURE_DEVICE_RET ret = CAPTURE_DEVICE_ERR_NONE;
    struct v4l2_fmtdesc vid_fmtdesc;
    unsigned int *pParamVal = (unsigned int *)retParam;

    vid_fmtdesc.index = mFmtParamIdx;
    vid_fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(mCameraDevice, VIDIOC_ENUM_FMT, &vid_fmtdesc ) != 0){
        if(mSensorFmtCnt > 0) {
            mCscFmtCnt = countActualCscFmt();
            mActualCscFmtCnt = mCscFmtCnt;
            mSensorFmtCnt = 0;
        }
        if(mCscFmtCnt > 0) {
            *pParamVal = mActualCscFmt[mCscFmtCnt-1];
            mFmtParamIdx ++;
            mCscFmtCnt --;
            return CAPTURE_DEVICE_ERR_ENUM_CONTINUE;
        }
        mFmtParamIdx = 0;
        ret = CAPTURE_DEVICE_ERR_GET_PARAM;
    }else{
        CAMERA_LOG_RUNTIME("vid_fmtdesc.pixelformat is %x", vid_fmtdesc.pixelformat);
        *pParamVal = vid_fmtdesc.pixelformat;
        if(mFmtParamIdx < MAX_SUPPORTED_FMT) {
            mSensorSupportFmt[mFmtParamIdx] = vid_fmtdesc.pixelformat;
            mSensorFmtCnt ++;
        }
        mFmtParamIdx ++;
        ret = CAPTURE_DEVICE_ERR_ENUM_CONTINUE;
    }
    return ret;
}

CAPTURE_DEVICE_RET V4l2UVCDevice::V4l2EnumSizeFps(void *retParam)
{
    CAMERA_LOG_FUNC;
    CAPTURE_DEVICE_RET ret = CAPTURE_DEVICE_ERR_NONE;
    struct v4l2_frmsizeenum vid_frmsize;
    struct v4l2_frmivalenum vid_frmval;

    struct capture_config_t *pCapCfg =(struct capture_config_t *) retParam;
    memset(&vid_frmsize, 0, sizeof(struct v4l2_frmsizeenum));
    mCaptureConfigNum = mSizeFPSParamIdx;
    vid_frmsize.index = mSizeFPSParamIdx;
    CAMERA_LOG_RUNTIME("the query for size fps fmt is %x",pCapCfg->fmt);

    if(needDoCsc(pCapCfg->fmt)) {
        vid_frmsize.pixel_format = queryCscSourceFormat(pCapCfg->fmt);
        if(vid_frmsize.pixel_format == 0) {
            CAMERA_LOG_ERR("EnumSizeFps: queryCscSourceFormat return failed");
            return CAPTURE_DEVICE_ERR_BAD_PARAM;
        }
    }
    else {
        vid_frmsize.pixel_format = pCapCfg->fmt;
    }
    if (ioctl(mCameraDevice, VIDIOC_ENUM_FRAMESIZES, &vid_frmsize) != 0){
        mSizeFPSParamIdx = 0;
        ret = CAPTURE_DEVICE_ERR_SET_PARAM;
    }else{
        //uvc handle 1600x1200 may have some problem. so, skip it.
        if(vid_frmsize.discrete.width == 1600 && vid_frmsize.discrete.height == 1200) {
            CAMERA_LOG_ERR("EnumSizeFps: now skip %d x %d resolution", vid_frmsize.discrete.width, vid_frmsize.discrete.height);
            mSizeFPSParamIdx = 0;
            return CAPTURE_DEVICE_ERR_SET_PARAM;
        }

        memset(&vid_frmval, 0, sizeof(struct v4l2_frmivalenum));
        CAMERA_LOG_RUNTIME("in %s the w %d, h %d", __FUNCTION__,vid_frmsize.discrete.width, vid_frmsize.discrete.height);
        vid_frmval.index = 0; //get the first, that is the min frame interval, but the biggest fps
        if(needDoCsc(pCapCfg->fmt)) {
            vid_frmval.pixel_format = queryCscSourceFormat(pCapCfg->fmt);
            if(vid_frmsize.pixel_format == 0) {
                CAMERA_LOG_ERR("EnumSizeFps2: queryCscSourceFormat return failed");
                return CAPTURE_DEVICE_ERR_BAD_PARAM;
            }
        }
        else {
            vid_frmval.pixel_format = pCapCfg->fmt;
        }
        vid_frmval.width = vid_frmsize.discrete.width;
        vid_frmval.height= vid_frmsize.discrete.height;
        if (ioctl(mCameraDevice, VIDIOC_ENUM_FRAMEINTERVALS, &vid_frmval) != 0){
            CAMERA_LOG_ERR("VIDIOC_ENUM_FRAMEINTERVALS error");
            mSizeFPSParamIdx = 0;
            ret = CAPTURE_DEVICE_ERR_SET_PARAM;
        }else{
            pCapCfg->width	= vid_frmsize.discrete.width;
            pCapCfg->height = vid_frmsize.discrete.height;
            pCapCfg->tv.numerator = vid_frmval.discrete.numerator;
            pCapCfg->tv.denominator = vid_frmval.discrete.denominator;
            mSizeFPSParamIdx ++;

            //store all configuration here.
            mCaptureConfig[mCaptureConfigNum].fmt = vid_frmsize.pixel_format;//pCapCfg->fmt;
            mCaptureConfig[mCaptureConfigNum].width = pCapCfg->width;
            mCaptureConfig[mCaptureConfigNum].height = pCapCfg->height;
            mCaptureConfig[mCaptureConfigNum].picture_waite_number = 0;
            mCaptureConfig[mCaptureConfigNum].tv.numerator = pCapCfg->tv.numerator;
            mCaptureConfig[mCaptureConfigNum].tv.denominator = pCapCfg->tv.denominator;
            ret = CAPTURE_DEVICE_ERR_ENUM_CONTINUE;
        }
    }
    return ret;
}

CAPTURE_DEVICE_RET V4l2UVCDevice::V4l2SetConfig(struct capture_config_t *pCapcfg)
{
    CAMERA_LOG_FUNC;
    if (mCameraDevice <= 0 || pCapcfg == NULL){
        return CAPTURE_DEVICE_ERR_BAD_PARAM;
    }

    CAPTURE_DEVICE_RET ret = CAPTURE_DEVICE_ERR_NONE;
    struct v4l2_format fmt;
    struct v4l2_control ctrl;
    struct v4l2_streamparm parm;
    struct capture_config_t *matchConfig = NULL;
    struct capture_config_t *betterMatchConfig = NULL;
    struct capture_config_t *bestMatchConfig = NULL;

    //find the best match configuration.
    for(unsigned int i=0; i < mCaptureConfigNum; i++) {
        if(/*mCaptureConfig[i].fmt == pCapcfg->fmt &&*/
                 mCaptureConfig[i].width == pCapcfg->width &&
                 mCaptureConfig[i].height == pCapcfg->height) {
            matchConfig = &mCaptureConfig[i];
            if(mCaptureConfig[i].tv.numerator == pCapcfg->tv.numerator &&
                    mCaptureConfig[i].tv.denominator == pCapcfg->tv.denominator) {
                bestMatchConfig = &mCaptureConfig[i];
            }
            else if(mCaptureConfig[i].tv.denominator/mCaptureConfig[i].tv.numerator >
                   pCapcfg->tv.denominator/pCapcfg->tv.numerator){
                betterMatchConfig = &mCaptureConfig[i];
            }//else
        }
    }//for

    if(bestMatchConfig != NULL) {
        matchConfig = bestMatchConfig;
    }
    else if(betterMatchConfig != NULL) {
        matchConfig = betterMatchConfig;
    }

    if(matchConfig == NULL) {
        CAMERA_LOG_ERR("Error: not support format=0x%x, Width=%d, Height=%d",
                       pCapcfg->fmt, pCapcfg->width, pCapcfg->height);
        return CAPTURE_DEVICE_ERR_BAD_PARAM;
    }

    mCurrentConfig = matchConfig;

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if(needDoCsc(pCapcfg->fmt)) {
        mEnableCSC = true;
        //set mDoCsc 
        selectCscFunction(pCapcfg->fmt);
        fmt.fmt.pix.pixelformat = queryCscSourceFormat(pCapcfg->fmt);
        if(fmt.fmt.pix.pixelformat == 0) {
            CAMERA_LOG_ERR("SetConfig: queryCscSourceFormat return failed");
            return CAPTURE_DEVICE_ERR_BAD_PARAM;
        }
    }
    else {
        mEnableCSC = false;
        fmt.fmt.pix.pixelformat = matchConfig->fmt;
    }

    fmt.fmt.pix.width = matchConfig->width;
    fmt.fmt.pix.height = matchConfig->height;
    if (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV)
        fmt.fmt.pix.bytesperline = fmt.fmt.pix.width * 2;
    else
        fmt.fmt.pix.bytesperline = fmt.fmt.pix.width;
    fmt.fmt.pix.priv = 0;
    fmt.fmt.pix.sizeimage = 0;

    int err = 0;
    if ((err = ioctl(mCameraDevice, VIDIOC_S_FMT, &fmt)) < 0) {
        CAMERA_LOG_ERR("set format failed err=%d\n", err);
        CAMERA_LOG_ERR("matchConfig->width is %d, matchConfig->height is %d", matchConfig->width, matchConfig->height);
        CAMERA_LOG_ERR(" Set the Format %x :%c%c%c%c\n", matchConfig->fmt,
                matchConfig->fmt & 0xFF, (matchConfig->fmt >> 8) & 0xFF,
                (matchConfig->fmt >> 16) & 0xFF, (matchConfig->fmt >> 24) & 0xFF);
        return CAPTURE_DEVICE_ERR_SYS_CALL;
    }

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = matchConfig->tv.numerator;
    parm.parm.capture.timeperframe.denominator = matchConfig->tv.denominator;
    CAMERA_LOG_RUNTIME("frame timeval is numerator %d, denominator %d",parm.parm.capture.timeperframe.numerator,
                parm.parm.capture.timeperframe.denominator);
    if ( (err = ioctl(mCameraDevice, VIDIOC_S_PARM, &parm)) < 0) {
        CAMERA_LOG_ERR("%s:%d  VIDIOC_S_PARM failed err=%d\n", __FUNCTION__,__LINE__, err);
        CAMERA_LOG_ERR("frame timeval is numerator %d, denominator %d",parm.parm.capture.timeperframe.numerator,
                parm.parm.capture.timeperframe.denominator);
        return CAPTURE_DEVICE_ERR_SYS_CALL;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(mCameraDevice, VIDIOC_G_FMT, &parm) < 0) {
        CAMERA_LOG_ERR("VIDIOC_S_PARM failed\n");
        return CAPTURE_DEVICE_ERR_SYS_CALL;
    }else{

        CAMERA_LOG_RUNTIME(" Width = %d\n", fmt.fmt.pix.width);
        CAMERA_LOG_RUNTIME(" Height = %d \n", fmt.fmt.pix.height);
        CAMERA_LOG_RUNTIME(" Image size = %d\n", fmt.fmt.pix.sizeimage);
        CAMERA_LOG_RUNTIME(" pixelformat = %x\n", fmt.fmt.pix.pixelformat);
    }
    matchConfig->framesize = fmt.fmt.pix.sizeimage;
    pCapcfg->framesize = fmt.fmt.pix.sizeimage;
    //For uvc, the first frame is ok.
    matchConfig->picture_waite_number = pCapcfg->picture_waite_number = 1;

    return CAPTURE_DEVICE_ERR_NONE;
}

void V4l2UVCDevice::convertYUYUToNV12(struct CscConversion* param)
{
    unsigned char *pSrcBufs = param->srcVirt;
    unsigned char *pDstBufs = param->dstVirt;
    unsigned int bufWidth = param->width;
    unsigned int bufHeight = param->height;

    unsigned char *pSrcY1Offset = pSrcBufs;
    unsigned char *pSrcY2Offset = pSrcBufs + (bufWidth << 1);
    unsigned char *pSrcY3Offset = pSrcBufs + (bufWidth << 1) * 2;
    unsigned char *pSrcY4Offset = pSrcBufs + (bufWidth << 1) * 3;
    unsigned char *pSrcU1Offset = pSrcY1Offset + 1;
    unsigned char *pSrcU2Offset = pSrcY2Offset + 1;
    unsigned char *pSrcU3Offset = pSrcY3Offset + 1;
    unsigned char *pSrcU4Offset = pSrcY4Offset + 1;
    unsigned char *pSrcV1Offset = pSrcY1Offset + 3;
    unsigned char *pSrcV2Offset = pSrcY2Offset + 3;
    unsigned char *pSrcV3Offset = pSrcY3Offset + 3;
    unsigned char *pSrcV4Offset = pSrcY4Offset + 3;
    unsigned int srcYStride = (bufWidth << 1) * 3;
    unsigned int srcUVStride = srcYStride;

    unsigned char *pDstY1Offset = pDstBufs;
    unsigned char *pDstY2Offset = pDstBufs + bufWidth;
    unsigned char *pDstY3Offset = pDstBufs + bufWidth * 2;
    unsigned char *pDstY4Offset = pDstBufs + bufWidth * 3;
    unsigned char *pDstU1Offset = pDstBufs + bufWidth * bufHeight;
    unsigned char *pDstU2Offset = pDstBufs + bufWidth * (bufHeight + 1);
    unsigned char *pDstV1Offset = pDstU1Offset + 1;
    unsigned char *pDstV2Offset = pDstU2Offset + 1;
    unsigned int dstYStride = bufWidth * 3;
    unsigned int dstUVStride = bufWidth;

    unsigned int nw, nh;
    for(nh = 0; nh < (bufHeight >> 2); nh++) {
        for(nw=0; nw < (bufWidth >> 1); nw++) {
            *pDstY1Offset++ = *pSrcY1Offset;
            *pDstY2Offset++ = *pSrcY2Offset;
            *pDstY3Offset++ = *pSrcY3Offset;
            *pDstY4Offset++ = *pSrcY4Offset;

            pSrcY1Offset += 2;
            pSrcY2Offset += 2;
            pSrcY3Offset += 2;
            pSrcY4Offset += 2;

            *pDstY1Offset++ = *pSrcY1Offset;
            *pDstY2Offset++ = *pSrcY2Offset;
            *pDstY3Offset++ = *pSrcY3Offset;
            *pDstY4Offset++ = *pSrcY4Offset;

            pSrcY1Offset += 2;
            pSrcY2Offset += 2;
            pSrcY3Offset += 2;
            pSrcY4Offset += 2;

            *pDstU1Offset = *pSrcU1Offset;
            *pDstU2Offset = *pSrcU3Offset;
            pDstU1Offset += 2;
            pDstU2Offset += 2;
            pSrcU1Offset += 4;
            pSrcU3Offset += 4;

            *pDstV1Offset = *pSrcV1Offset;
            *pDstV2Offset = *pSrcV3Offset;
            pDstV1Offset += 2;
            pDstV2Offset += 2;
            pSrcV1Offset += 4;
            pSrcV3Offset += 4;
        }

        pSrcY1Offset += srcYStride;
        pSrcY2Offset += srcYStride;
        pSrcY3Offset += srcYStride;
        pSrcY4Offset += srcYStride;

        pSrcU1Offset += srcUVStride;
        pSrcU3Offset += srcUVStride;
        pSrcV1Offset += srcUVStride;
        pSrcV3Offset += srcUVStride;

        pDstY1Offset += dstYStride;
        pDstY2Offset += dstYStride;
        pDstY3Offset += dstYStride;
        pDstY4Offset += dstYStride;

        pDstU1Offset += dstUVStride;
        pDstU2Offset += dstUVStride;
        pDstV1Offset += dstUVStride;
        pDstV2Offset += dstUVStride;
    }
}

};

