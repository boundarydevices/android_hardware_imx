/*
 *   Copyright 2009-2010 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * Copyright (C) Texas Instruments - http://www.ti.com/
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
/**
* @file CameraHal.cpp
*
* This file maps the Camera Hardware Interface to V4L2.
*
*/
#define LOG_TAG "CameraHal"
#include "CameraHal.h"

namespace android {
int CameraHal::camera_device = -1;
int CameraHal::g_sensor_width = 640;
int CameraHal::g_sensor_height = 480;
int CameraHal::g_sensor_top = 0;
int CameraHal::g_sensor_left = 0;
int CameraHal::g_display_width = DISPLAY_WIDTH;
int CameraHal::g_display_height = DISPLAY_HEIGHT;
int CameraHal::g_display_top = 0;
int CameraHal::g_display_left = 0;
int CameraHal::g_rotate=0;
int CameraHal::g_display_lcd = 0;
int CameraHal::g_capture_mode = 0;	//0:low resolution, 1:high resolution
int CameraHal::g_preview_width = PREVIEW_WIDTH;
int CameraHal::g_preview_height = PREVIEW_HEIGHT;
int CameraHal::g_recording_width = RECORDING_WIDTH_NORMAL;
int CameraHal::g_recording_height = RECORDING_HEIGHT_NORMAL;
int CameraHal::g_recording_level = 1;	//1: CIF mode; 0: QCIF mode

//Camera Take Picture Parameter
int CameraHal::g_pic_width = PICTURE_WIDTH;
int CameraHal::g_pic_height = PICTURE_HEIGHT;
int CameraHal::g_still_bpp = 16;

struct picbuffer CameraHal::buffers[3];

#ifdef USE_FSL_JPEG_ENC 
JPEG_ENC_UINT32 CameraHal::g_JpegDataSize = 0;//Total size of g_JpegData
JPEG_ENC_UINT32 CameraHal::g_JpegDataLen = 0;//Valid data len of g_JpegData
JPEG_ENC_UINT8 *CameraHal::g_JpegData = NULL;//Buffer to hold jpeg data
#endif

#ifdef DUMP_CAPTURE_YUV
FILE * CameraHal::record_yuvFile = 0;
FILE * CameraHal::capture_yuvFile = 0;
#endif

wp<CameraHardwareInterface> CameraHal::singleton;

const char CameraHal::supportedPictureSizes [] = "3280x2464,2560x2048,2048x1536,1600x1200,1280x1024,1152x964,640x480,320x240";
const char CameraHal::supportedPreviewSizes [] = "1280x720,800x480,720x576,720x480,768x576,640x480,320x240,352x288,176x144,128x96";
const char CameraHal::supportedFPS [] = "33,30,25,24,20,15,10";
const char CameraHal::supprotedThumbnailSizes []= "80x60";
const char CameraHal::PARAMS_DELIMITER []= ",";

CameraHal::CameraHal()
                  : mParameters(),
		    mRawHeap(0),
                    mHeap(0),
                    mPictureHeap(0),
                    fcount(6),
                    previewStopped(true),
                    recordStopped(true),
                    doubledPreviewWidth(false),
                    doubledPreviewHeight(false),
                    mPreviewFrameSize(0),
		    mPreviewRunning(0),
		    mRecordFrameSize(0),
		    mCurrentRecordFrame(0),
		    mVideoHeap(0),
                    mNotifyCb(0),
                    mDataCb(0),
                    mDataCbTimestamp(0),
                    mCallbackCookie(0),
                    mMsgEnabled(0),
                    mCurrentPreviewFrame(0),
		    nCameraBuffersQueued(0),
		    mCameraOpened(0),
		    mIsTakingPic(0)
{
    for(int i = 0; i < videoBufferCount; i++)
    {
        mVideoBuffer[i] = 0;
        mVideoBufferUsing[i] = 0;
    }

    initDefaultParameters();
}

void CameraHal::initDefaultParameters()
{
    CameraParameters p;
    char tmpBuffer[PARAM_BUFFER];

    p.setPreviewSize(PREVIEW_WIDTH, PREVIEW_HEIGHT);
    p.setPreviewFrameRate(PREVIEW_FRAMERATE);
    p.setPreviewFormat(PREVIEW_FORMAT);

    p.setPictureSize(PICTURE_WIDTH, PICTURE_HEIGHT);
    p.setPictureFormat("jpeg");
    p.set(CameraParameters::KEY_JPEG_QUALITY, 100);

    //Eclair extended parameters
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, CameraHal::supportedPictureSizes);
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS, CameraParameters::PIXEL_FORMAT_JPEG);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, CameraHal::supportedPreviewSizes);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, CameraParameters::PIXEL_FORMAT_YUV422I);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, CameraHal::supportedFPS);
    p.set(CameraParameters::KEY_SUPPORTED_THUMBNAIL_SIZES, CameraHal::supprotedThumbnailSizes);

    memset(tmpBuffer, '\0', PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::WHITE_BALANCE_AUTO, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::WHITE_BALANCE_INCANDESCENT, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::WHITE_BALANCE_FLUORESCENT, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::WHITE_BALANCE_DAYLIGHT, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::WHITE_BALANCE_SHADE, PARAM_BUFFER);
    p.set(CameraParameters::KEY_SUPPORTED_WHITE_BALANCE, tmpBuffer);
    p.set(CameraParameters::KEY_WHITE_BALANCE, CameraParameters::WHITE_BALANCE_AUTO);

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    strncat((char*) tmpBuffer, (const char*) CameraParameters::EFFECT_NONE, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::EFFECT_MONO, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::EFFECT_NEGATIVE, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::EFFECT_SOLARIZE,  PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::EFFECT_SEPIA, PARAM_BUFFER);
    p.set(CameraParameters::KEY_SUPPORTED_EFFECTS, tmpBuffer);
    p.set(CameraParameters::KEY_EFFECT, CameraParameters::EFFECT_NONE);

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    strncat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_AUTO, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_PORTRAIT, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_LANDSCAPE, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_SPORTS, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_NIGHT_PORTRAIT, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_FIREWORKS, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::SCENE_MODE_NIGHT, PARAM_BUFFER);
    p.set(CameraParameters::KEY_SUPPORTED_SCENE_MODES, tmpBuffer);
    p.set(CameraParameters::KEY_SCENE_MODE, CameraParameters::SCENE_MODE_AUTO);

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    strncat((char*) tmpBuffer, (const char*) CameraParameters::FOCUS_MODE_AUTO, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::FOCUS_MODE_INFINITY, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::FOCUS_MODE_MACRO, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::FOCUS_MODE_FIXED, PARAM_BUFFER);
    p.set(CameraParameters::KEY_SUPPORTED_FOCUS_MODES, tmpBuffer);
    p.set(CameraParameters::KEY_FOCUS_MODE, CameraParameters::FOCUS_MODE_AUTO);

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    strncat((char*) tmpBuffer, (const char*) CameraParameters::ANTIBANDING_50HZ, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::ANTIBANDING_60HZ, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
    strncat((char*) tmpBuffer, (const char*) CameraParameters::ANTIBANDING_OFF, PARAM_BUFFER);
    p.set(CameraParameters::KEY_SUPPORTED_ANTIBANDING, tmpBuffer);
    p.set(CameraParameters::KEY_ANTIBANDING, CameraParameters::ANTIBANDING_OFF);

    memset(tmpBuffer, '\0', sizeof(*tmpBuffer));
    strncat( (char*) tmpBuffer, (const char*) CameraParameters::FLASH_MODE_OFF, PARAM_BUFFER);
    p.set(CameraParameters::KEY_SUPPORTED_FLASH_MODES, tmpBuffer);
    p.set(CameraParameters::KEY_FLASH_MODE, CameraParameters::FLASH_MODE_OFF);

    if (setParameters(p) != NO_ERROR) {
        LOGE("Failed to set default parameters?!");
    }

}

CameraHal::~CameraHal()
{
    int err = 0;
    
    LOG_FUNCTION_NAME
    LOGD(">>> Release");

    LOGD("<<< Release");

    singleton.clear();
}

int CameraHal::CameraOpen()
{
    int err;

    LOG_FUNCTION_NAME

    if(mCameraOpened == 0){
    	camera_device = open(VIDEO_DEVICE, O_RDWR, 0);
    	if (camera_device < 0) {
        	LOGE ("Could not open the camera device: %s",  strerror(errno) );
        	return -1;
    	}
    	mCameraOpened = 1;
    	LOGD("Camera Created - Success");
    }else{
	LOGE ("Camera device has been opened!");
    }

    return 0;
}


int CameraHal::CameraClose()
{
    int err;

    if(mCameraOpened == 1){
    	close(camera_device);
    	camera_device = -1;
    	mCameraOpened = 0;
    	LOGD("Camera Destroyed - Success");
    }else{
	LOGE ("Camera device has been closed!");
    }

    return 0;
}

int CameraHal::CameraGetFBInfo()
{
        int fd_fb = 0;
        struct fb_fix_screeninfo fix;
        struct fb_var_screeninfo var;

        if ((fd_fb = open(FB_DEVICE, O_RDWR )) < 0)     {
                LOGD("Unable to open frame buffer\n");
                return -1;
        }

        if (ioctl(fd_fb, FBIOGET_VSCREENINFO, &var) < 0) {
                close(fd_fb);
                return -1;
        }
        if (ioctl(fd_fb, FBIOGET_FSCREENINFO, &fix) < 0) {
                close(fd_fb);
                return -1;
        }

        g_display_width = var.xres;
        g_display_height = var.yres;
        LOGD("var.xres = %d", var.xres);
        LOGD("var.yres = %d", var.yres);
        LOGD("var.bits_per_pixel = %d", var.bits_per_pixel);
        close(fd_fb);

        return 0;
}

int CameraHal::CameraPreviewConfig(int fd_v4l)
{
        LOG_FUNCTION_NAME
        
        struct v4l2_streamparm parm;
        v4l2_std_id id;
        struct v4l2_control ctl;
        struct v4l2_crop crop;
	struct v4l2_format fmt;

        if (ioctl(fd_v4l, VIDIOC_S_OUTPUT, &g_display_lcd) < 0)
        {
                LOGE("VIDIOC_S_OUTPUT failed\n");
                return -1;
        }

        ctl.id = V4L2_CID_PRIVATE_BASE;
	ctl.value = g_rotate;
        if (ioctl(fd_v4l, VIDIOC_S_CTRL, &ctl) < 0)
        {
                LOGE("set control failed\n");
                return -1;
        }

        crop.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
        crop.c.left = g_sensor_left;
        crop.c.top = g_sensor_top;
        crop.c.width = g_sensor_width;
        crop.c.height = g_sensor_height;
        if (ioctl(fd_v4l, VIDIOC_S_CROP, &crop) < 0)
        {
                LOGE("set cropping failed\n");
                return -1;
        }

        fmt.type = V4L2_BUF_TYPE_VIDEO_OVERLAY;
        fmt.fmt.win.w.top=  g_display_top ;
        fmt.fmt.win.w.left= g_display_left;
        fmt.fmt.win.w.width=g_preview_width;
        fmt.fmt.win.w.height=g_preview_height;

        if (ioctl(fd_v4l, VIDIOC_S_FMT, &fmt) < 0)
        {
                LOGE("set format failed\n");
                return -1;
        }

	//Camera Preview VIDIOC_S_PARM
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = PREVIEW_FRAMERATE;
	parm.parm.capture.capturemode = g_capture_mode;

        if (ioctl(fd_v4l, VIDIOC_S_PARM, &parm) < 0)
        {
                LOGE("VIDIOC_S_PARM failed\n");
                return -1;
        }

        return 0;
}

int CameraHal::CameraTakePicConfig(int fd_v4l)
{
        struct v4l2_streamparm parm;
        struct v4l2_format fmt;
        struct v4l2_crop crop;

	memset(&fmt, 0, sizeof(fmt));
        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        fmt.fmt.pix.pixelformat = PICTURE_FROMAT;
        fmt.fmt.pix.width = g_pic_width;
        fmt.fmt.pix.height = g_pic_height;
        fmt.fmt.pix.sizeimage = fmt.fmt.pix.width * fmt.fmt.pix.height * g_still_bpp / 8;
        fmt.fmt.pix.bytesperline = g_pic_width * g_still_bpp / 8;

        if (ioctl(fd_v4l, VIDIOC_S_FMT, &fmt) < 0)
        {
                LOGE("set format failed\n");
                return -1;
        }

        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c.left = 0;
        crop.c.top = 0;
        crop.c.width = g_pic_width;
        crop.c.height = g_pic_height;
        if (ioctl(fd_v4l, VIDIOC_S_CROP, &crop) < 0)
        {
                LOGE("set cropping failed\n");
                return -1;
        }

	//Camera Picture VIDIOC_S_PARM
	//avoid taking time to reconfig the sensor
/*
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1;
	parm.parm.capture.timeperframe.denominator = PREVIEW_FRAMERATE;
	parm.parm.capture.capturemode = g_capture_mode;

        if (ioctl(fd_v4l, VIDIOC_S_PARM, &parm) < 0)
        {
                LOGE("VIDIOC_S_PARM failed\n");
                return -1;
        }
*/

        return NO_ERROR;
}

int CameraHal::CameraRecordingConfig(int fd_v4l)
{
        struct v4l2_format fmt;
        struct v4l2_control ctrl;
        struct v4l2_streamparm parm;

        fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.pixelformat = RECORDING_FORMAT;
        fmt.fmt.pix.width = g_recording_width;
        fmt.fmt.pix.height = g_recording_height;

	fmt.fmt.pix.bytesperline = g_recording_width;
	fmt.fmt.pix.priv = 0;
       	fmt.fmt.pix.sizeimage = 0;

	if (ioctl(fd_v4l, VIDIOC_S_FMT, &fmt) < 0)
        {
                LOGE("set format failed\n");
                return -1;
        }

	//Camera Recording VIDIOC_S_PARM
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = PREVIEW_FRAMERATE;
	parm.parm.capture.capturemode = g_capture_mode;

        if (ioctl(fd_v4l, VIDIOC_S_PARM, &parm) < 0)
        {
                LOGE("VIDIOC_S_PARM failed\n");
                return -1;
        }

        // Set rotation
        ctrl.id = V4L2_CID_PRIVATE_BASE + 0;
        ctrl.value = g_rotate;
        if (ioctl(fd_v4l, VIDIOC_S_CTRL, &ctrl) < 0)
        {
                LOGE("set ctrl failed\n");
                return -1;
        }

        struct v4l2_requestbuffers req;
        memset(&req, 0, sizeof (req));
        req.count = CAPTURE_BUFFER_NUM;
        req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        req.memory = V4L2_MEMORY_MMAP;

        if (ioctl(fd_v4l, VIDIOC_REQBUFS, &req) < 0)
        {
                LOGE("v4l_capture_setup: VIDIOC_REQBUFS failed\n");
                return -1;
        }

        return 0;
}

int CameraHal::CameraStartRecording(int fd_v4l)
{
        unsigned int i;
        struct v4l2_buffer buf;
        enum v4l2_buf_type type;

        for (i = 0; i < CAPTURE_BUFFER_NUM; i++)
        {
                memset(&buf, 0, sizeof (buf));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.index = i;
                if (ioctl(fd_v4l, VIDIOC_QUERYBUF, &buf) < 0)
                {
                        LOGE("VIDIOC_QUERYBUF error\n");
                        return -1;
                }else{
			LOGE("VIDIOC_QUERYBUF ok\n");
		}

                buffers[i].length = buf.length;
                buffers[i].offset = (size_t) buf.m.offset;
                buffers[i].start = (unsigned char *)mmap (NULL, buffers[i].length,
                    PROT_READ | PROT_WRITE, MAP_SHARED,
                    fd_v4l, buffers[i].offset);
				memset(buffers[i].start, 0xFF, buffers[i].length);
		LOGE("buffers[%d].length = %d\n", i, buffers[i].length);
		LOGE("buffers[%d].offset = 0x%x\n", i, buffers[i].offset);
		LOGE("buffers[%d].start = 0x%x\n", i, buffers[i].start);
        }

        for (i = 0; i < CAPTURE_BUFFER_NUM; i++)
        {
                memset(&buf, 0, sizeof (buf));
                buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                buf.memory = V4L2_MEMORY_MMAP;
                buf.index = i;
		buf.m.offset = buffers[i].offset;

                if (ioctl (fd_v4l, VIDIOC_QBUF, &buf) < 0) {
                        LOGE("VIDIOC_QBUF error\n");
                        return -1;
                }else{
			LOGE("VIDIOC_QBUF ok\n");
		}
		nCameraBuffersQueued++;
        }

        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl (fd_v4l, VIDIOC_STREAMON, &type) < 0) {
                LOGE("VIDIOC_STREAMON error\n");
                return -1;
        }else{
		LOGE("VIDIOC_STREAMON ok\n");
	}
        return 0;
}

bool CameraHal::initHeapLocked()
{
    LOG_FUNCTION_NAME

    int width, height;
    int nSizeBytes;
    struct v4l2_requestbuffers creqbuf;

    mParameters.getPreviewSize(&width, &height);

    LOGD("initHeapLocked: preview size=%dx%d", width, height);

    // Note that we enforce yuv422 in setParameters().
    int how_big = width * height * 2;
    nSizeBytes =  how_big;

    if (nSizeBytes & 0xfff)
    {
        how_big = (nSizeBytes & 0xfffff000) + 0x1000;
    }

    mPreviewFrameSize = how_big;
    LOGD("mPreviewFrameSize = 0x%x = %d", mPreviewFrameSize, mPreviewFrameSize);


    int buffer_count = mOverlay->getBufferCount();
    LOGD("mOverlay->getBufferCount=%d",buffer_count);

    buffer_count = CAPTURE_BUFFER_NUM;
    LOGD("number of buffers = %d\n", buffer_count);
    // Check that the camera can accept that many buffers */
    creqbuf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    creqbuf.memory = V4L2_MEMORY_MMAP;
    creqbuf.count  = buffer_count;

    return true;

}

sp<IMemoryHeap> CameraHal::getPreviewHeap() const
{
    LOG_FUNCTION_NAME

    return mSurfaceFlingerHeap;
}

sp<IMemoryHeap> CameraHal::getRawHeap() const
{
    return mRawHeap;
}

void CameraHal::setCallbacks(notify_callback notify_cb,
                                      data_callback data_cb,
                                      data_callback_timestamp data_cb_timestamp,
                                      void* user)
{
    Mutex::Autolock lock(mLock);
    mNotifyCb = notify_cb;
    mDataCb = data_cb;
    mDataCbTimestamp = data_cb_timestamp;
    mCallbackCookie = user;
}

void CameraHal::enableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled |= msgType;
}

void CameraHal::disableMsgType(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    mMsgEnabled &= ~msgType;
}

bool CameraHal::msgTypeEnabled(int32_t msgType)
{
    Mutex::Autolock lock(mLock);
    return (mMsgEnabled & msgType);
}

// ---------------------------------------------------------------------------

int CameraHal::previewThread()
{
    int w, h;
    unsigned long offset;
    void *croppedImage;
    overlay_buffer_t overlaybuffer;    
    struct v4l2_buffer cfilledbuffer;
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_MMAP; 
 
    return NO_ERROR;
}

int CameraHal::recordThread()
{
    int w, h;
    unsigned long offset;
    void *croppedImage;
    overlay_buffer_t overlaybuffer;    
    struct v4l2_buffer cfilledbuffer;
    memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_MMAP; 

    if (!recordStopped){

        /* De-queue the next avaliable buffer */
	nCameraBuffersQueued--;    
        while (ioctl(camera_device, VIDIOC_DQBUF, &cfilledbuffer) < 0) {
            LOGE("VIDIOC_DQBUF Failed");
        }

        mCurrentRecordFrame++;

        //LOGD("recordThread: CurrentRecordFrame %d", mCurrentRecordFrame);
       
	if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) {
        	int i;
		nsecs_t timeStamp = systemTime(SYSTEM_TIME_MONOTONIC);
        	for(i = 0 ; i < videoBufferCount; i ++)
        	{
            		if(0 == mVideoBufferUsing[i])
            		{
                		//LOGD("send buffer:%d pointer:0x%x", i,(unsigned int)(mVideoBuffer[i]->pointer()));
#ifdef DUMP_CAPTURE_YUV        
       		if(record_yuvFile)
       		{
            		int len = fwrite(buffers[cfilledbuffer.index].start, 1, mRecordFrameSize, record_yuvFile);
            		//LOGI("CameraHal:: WRITE FILE len %d",len);            
       		}        
#endif
                		memcpy(mVideoBuffer[i]->pointer(),(void*)buffers[cfilledbuffer.index].start,mRecordFrameSize);
                		mVideoBufferUsing[i] = 1;
				mDataCbTimestamp(timeStamp, CAMERA_MSG_VIDEO_FRAME, mVideoBuffer[i], mCallbackCookie);
                		break;
            		}else{
                		//LOGD("no Buffer can be used \n");
            		}
        	}
    	}
        
	nCameraBuffersQueued++;
	if (ioctl(camera_device, VIDIOC_QBUF, &cfilledbuffer) < 0) {
        	LOGE("nextPreview VIDIOC_QBUF Failed!!!");
    	}
    }
    return NO_ERROR;
}

status_t CameraHal::setOverlay(const sp<Overlay> &overlay)
{
    Mutex::Autolock lock(mLock);
    if (overlay == NULL)
        LOGE("Trying to set overlay, but overlay is null!");
    mOverlay = overlay;
    return NO_ERROR;
}

#ifdef CAPTURE_ONLY_TEST
status_t CameraHal::startPreview(preview_callback cb, void* user)
{
    LOG_FUNCTION_NAME

    Mutex::Autolock lock(mLock);
    if (mPreviewThread != 0) {
        // already running
        return INVALID_OPERATION;
    }

    int width, height;
    mParameters.setPreviewSize(PICTURE_WIDTH, PICTURE_HEIGHT);
    mParameters.getPreviewSize(&width, &height);
    LOGI("w=%d h=%d", width, height);

    previewStopped = false;
    mPreviewRunning = true;
    mPreviewCallback = cb;
    mPreviewCallbackCookie = user;
    mPreviewThread = new PreviewThread(this);
    return NO_ERROR;
}
#else
status_t CameraHal::startPreview()
{
    LOG_FUNCTION_NAME

    int width;
    int height;

    Mutex::Autolock lock(mLock);
    if (mPreviewRunning != 0) {
        // already running
        return INVALID_OPERATION;
    }

if(mIsTakingPic == 0){
#ifdef DUMP_CAPTURE_YUV        
       capture_yuvFile = fopen("/sdcard/capture.yuv","wb");
       LOGD ("capture.yuv file opened!");
#endif

    CameraGetFBInfo();

    if (CameraOpen() < 0) {
        LOGE ("Could not open the camera device: %d, errno: %d", camera_device, errno);
        return -1;
    }else{
        LOGE ("Opened the camera device in preview!");
    }

    int width, height;
    mParameters.getPreviewSize(&width, &height);
    LOGI("Original Preview Width=%d Height=%d", width, height);

    g_recording_width = width;
    g_recording_height = height;

#ifdef IMX51_3STACK
    //mParameters.getPreviewSize(&g_preview_height, &g_preview_width);
    g_preview_height = PREVIEW_WIDTH;
    g_preview_width = PREVIEW_HEIGHT;
    g_display_top = 30;
    g_display_left = (g_display_width - g_preview_width) / 2;
#else
    //mParameters.getPreviewSize(&g_preview_width, &g_preview_height);
    g_preview_height = PREVIEW_HEIGHT;
    g_preview_width = PREVIEW_WIDTH;
    g_display_left = 45;
    g_display_top = 0;
#endif

    CameraPreviewConfig(camera_device);
}else{
    mIsTakingPic = 0;
    LOGD("Camera taking picture closed!");
}

    /* turn on streaming */
    int overlay = 1;

    if (ioctl(camera_device, VIDIOC_OVERLAY, &overlay) < 0)
    {
          LOGE("VIDIOC_OVERLAY start failed\n");
		return -1;
    }
    else LOGE("VIDIOC_OVERLAY started\n");
    LOGD("camera is in viewfinder mode...");

    previewStopped = false;
    mPreviewRunning = true;

    return NO_ERROR;
}
#endif

#ifdef CAPTURE_ONLY_TEST
void CameraHal::stopPreview()
{
    LOG_FUNCTION_NAME

    sp<PreviewThread> previewThread;
    struct v4l2_requestbuffers creqbuf;

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        previewStopped = true;
        mPreviewRunning = false;
    }

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        previewThread = mPreviewThread;
    }

    // don't hold the lock while waiting for the thread to quit
    if (previewThread != 0) {
        previewThread->requestExitAndWait();
    }

    Mutex::Autolock lock(mLock);
    mPreviewThread.clear();
}
#else
void CameraHal::stopPreview()
{
    LOG_FUNCTION_NAME

    sp<PreviewThread> previewThread;
    struct v4l2_requestbuffers creqbuf;

    { // scope for the lock
        Mutex::Autolock lock(mLock);
        previewStopped = true;
    }

    if (mPreviewRunning != 0) {

        /* Turn off streaming */
        int overlay = 0;
        if (ioctl(camera_device, VIDIOC_OVERLAY, &overlay) < 0)
        {
                LOGE("VIDIOC_OVERLAY stop failed\n");
        }
        LOGD("Turned off Viewfinder Mode\n");

        if(mIsTakingPic == 0){
                CameraClose();
#ifdef DUMP_CAPTURE_YUV        
               fclose(capture_yuvFile);
               LOGD ("capture.yuv file closed!");
#endif
        }

        mPreviewRunning = 0;
    }



    singleton.clear();
}
#endif

bool CameraHal::previewEnabled()
{
    return (!previewStopped);
}

status_t CameraHal::startRecording()
{
    LOG_FUNCTION_NAME 

    if (mRecordThread != 0) {
	LOGI("mRecordThread already exist!");
        return INVALID_OPERATION;
    }

#ifdef DUMP_CAPTURE_YUV
    record_yuvFile = fopen("/sdcard/recording.yuv","wb");
#endif

#ifdef CAPTURE_ONLY_TEST
    if (CameraOpen() < 0) {
        LOGE ("Could not open the camera device in recording: %d, errno: %d", camera_device, errno);
        return -1;
    }else{
        LOGE ("Opened the camera device in recording!");
    }
#endif

    CameraRecordingConfig(camera_device);

    struct v4l2_format fmt; 
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera_device, VIDIOC_G_FMT, &fmt) < 0)
    {
            LOGD("get format failed\n");
            return -1;
    }else{
            LOGD("\t Width = %d", fmt.fmt.pix.width);
            LOGD("\t Height = %d", fmt.fmt.pix.height);
            LOGD("\t Image size = %d\n", fmt.fmt.pix.sizeimage);
            LOGD("\t pixelformat = %d\n", fmt.fmt.pix.pixelformat);
    }

    // Just for the same size case
    //mRecordFrameSize = mPreviewFrameSize;
    mRecordFrameSize = fmt.fmt.pix.sizeimage;
    LOGD("mRecordFrameSize = %d\n", mRecordFrameSize);

    if (mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) {
        int i = 0;
        LOGD("Clear the old memory ");
        mVideoHeap.clear();
        for(i = 0; i < videoBufferCount; i++)
        {
            mVideoBuffer[i].clear();
        }
        LOGD("Init the video Memory %d", mRecordFrameSize);
        mVideoHeap = new MemoryHeapBase(mRecordFrameSize * videoBufferCount);
        for(i = 0; i < videoBufferCount; i++)
        {
            LOGD("Init Video Buffer:%d ",i);
            mVideoBuffer[i] = new MemoryBase(mVideoHeap, mRecordFrameSize*i, mRecordFrameSize);
        }
    }
    
    CameraStartRecording(camera_device);

    LOGD("Recording:camera is streaming...");

    recordStopped = false;
    mRecordThread = new RecordThread(this);
    return NO_ERROR;
}

void CameraHal::stopRecording()
{
    LOG_FUNCTION_NAME

    sp<RecordThread> recordThread;
    struct v4l2_requestbuffers creqbuf;

    {// scope for the lock
        Mutex::Autolock lock(mLock);
        recordStopped = true;
    }

    if (mRecordThread != 0) {

        struct v4l2_buffer cfilledbuffer;
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cfilledbuffer.memory = V4L2_MEMORY_MMAP;
        
   	while(nCameraBuffersQueued){
		LOGD("DQUEUING UNDQUEUED BUFFERS = %d",nCameraBuffersQueued);
        	nCameraBuffersQueued--;
        	if (ioctl(camera_device, VIDIOC_DQBUF, &cfilledbuffer) < 0) {
        		LOGE("VIDIOC_DQBUF Failed!!!");
        	}
    	}

       	/* Turn off streaming */
        creqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(camera_device, VIDIOC_STREAMOFF, &creqbuf.type) == -1) {
            LOGE("VIDIOC_STREAMOFF Failed");
        }
        LOGD("Recording:Turned off streaming\n");
    }

#ifdef DUMP_CAPTURE_YUV
    fclose(record_yuvFile);
    LOGD("Closed recorded_yuvfile captured\n");
#endif

    {// scope for the lock
        Mutex::Autolock lock(mLock);
        recordThread = mRecordThread;
    }

    // don't hold the lock while waiting for the thread to quit
    if (recordThread != 0) {
        recordThread->requestExitAndWait();
    }

#ifdef CAPTURE_ONLY_TEST
    CameraClose();
#endif

    Mutex::Autolock lock(mLock);
    mRecordThread.clear();
}

bool CameraHal::recordingEnabled()
{
    LOG_FUNCTION_NAME
    return (!recordStopped);
}

static void debugShowFPS()
{
     static int mFrameCount = 0;
     static int mLastFrameCount = 0;
     static nsecs_t mLastFpsTime = 0;
     static float mFps = 0;
     mFrameCount++;
     if (!(mFrameCount & 0x1F)) {
         nsecs_t now = systemTime();
         nsecs_t diff = now - mLastFpsTime;
         mFps =  ((mFrameCount - mLastFrameCount) * float(s2ns(1))) / diff;
         mLastFpsTime = now;
         mLastFrameCount = mFrameCount;
         LOGD("####### [%d] Frames, %f FPS", mFrameCount, mFps);
     }
}

void CameraHal::releaseRecordingFrame(const sp<IMemory>& mem)
{
    //LOG_FUNCTION_NAME
    ssize_t offset;
    size_t  size;
    int index;
    debugShowFPS();
    offset = mem->offset();
    size   = mem->size();
    index = offset / size;
    //LOGD("Index[%d] Buffer has been released,pointer = %p",index,mem->pointer());
    mVideoBufferUsing[index] = 0;
}


// ---------------------------------------------------------------------------

int CameraHal::beginAutoFocusThread(void *cookie)
{
    LOG_FUNCTION_NAME

    CameraHal *c = (CameraHal *)cookie;
    return c->autoFocusThread();
}

int CameraHal::autoFocusThread()
{
    LOG_FUNCTION_NAME

    if (mMsgEnabled & CAMERA_MSG_FOCUS)
        mNotifyCb(CAMERA_MSG_FOCUS, true, 0, mCallbackCookie);

    return UNKNOWN_ERROR;
}

status_t CameraHal::autoFocus()
{
    LOG_FUNCTION_NAME

    Mutex::Autolock lock(mLock);

    if (createThread(beginAutoFocusThread, this) == false)
        return UNKNOWN_ERROR;

    return NO_ERROR;
}

status_t CameraHal::cancelAutoFocus()
{
    return NO_ERROR;
}

/*static*/ int CameraHal::beginPictureThread(void *cookie)
{
    LOG_FUNCTION_NAME

    CameraHal *c = (CameraHal *)cookie;
    return c->pictureThread();
}

int CameraHal::pictureThread()
{

    int w, h;
    int pictureSize;
    unsigned long base, offset;
    struct v4l2_streamparm parm;
    struct v4l2_format fmt;
    sp<MemoryBase> mPictureBuffer;
    sp<MemoryBase> memBase;
    char *buf1;
    
    LOG_FUNCTION_NAME

    if (mMsgEnabled & CAMERA_MSG_SHUTTER) {
	LOGI("CAMERA_MSG_SHUTTER");
        mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);
    }

    mParameters.setPictureSize(g_pic_width, g_pic_height);
    mParameters.getPictureSize(&w, &h);
    LOGD("Picture Size: Width = %d \tHeight = %d", w, h);

    if(CameraTakePicConfig(camera_device) < 0){
	    return -1;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera_device, VIDIOC_G_FMT, &fmt) < 0) {
            LOGE("get format failed\n");
            return -1;
    }

    buf1 = (char *)malloc(fmt.fmt.pix.sizeimage);
    if (!buf1){
            LOGE("buffer malloc error!\n");
            free(buf1);
	    return -1;
    }

    memset(buf1, 0, fmt.fmt.pix.sizeimage);

    if (read(camera_device, buf1, fmt.fmt.pix.sizeimage) != fmt.fmt.pix.sizeimage) {
            LOGE("v4l2 read error.\n");
   	    free(buf1);
	    return -1;
    }

    LOGD("pictureThread: generated a picture");

#ifdef DUMP_CAPTURE_YUV        
       if(capture_yuvFile)
       {
            int len = fwrite((void*)buf1, 1, fmt.fmt.pix.sizeimage, capture_yuvFile);
            LOGI("CameraHal:: WRITE FILE len %d",len);            
       }
#endif

#ifdef USE_FSL_JPEG_ENC
        sp<MemoryBase> jpegMemBase = encodeImage((void*)buf1, fmt.fmt.pix.sizeimage);
        if (mMsgEnabled & CAMERA_MSG_COMPRESSED_IMAGE) {
		LOGI("==========CAMERA_MSG_COMPRESSED_IMAGE");
		mDataCb(CAMERA_MSG_COMPRESSED_IMAGE, jpegMemBase, mCallbackCookie);
	}

#else
        LOGI("CameraHal::pictureThread get default image");     
        sp<MemoryHeapBase> heap = new MemoryHeapBase(kCannedJpegSize);
        sp<MemoryBase> mem = new MemoryBase(heap, 0, kCannedJpegSize);
        memcpy(heap->base(), kCannedJpeg, kCannedJpegSize);
        if (mJpegPictureCallback)
            mJpegPictureCallback(mem, mPictureCallbackCookie);
#endif

exit0:
    if (buf1)
            free(buf1);

    return NO_ERROR;
}

status_t CameraHal::takePicture()
{
    LOG_FUNCTION_NAME

    mIsTakingPic = 1;
    LOGD("Camera taking picture opened!");

    stopPreview();

    LOGD("Creating Picture Thread");
    //##############################TODO use  thread
    if (createThread(beginPictureThread, this) == false)
        return -1;

    return NO_ERROR;
}

status_t CameraHal::cancelPicture()
{
    LOG_FUNCTION_NAME

    return NO_ERROR;
}

status_t CameraHal::dump(int fd, const Vector<String16>& args) const
{
    return NO_ERROR;
}

int CameraHal::validateSize(int w, int h)
{
    LOG_FUNCTION_NAME

    if ((w < MIN_WIDTH) || (h < MIN_HEIGHT)){
        return false;
    }

    return true;
}


status_t CameraHal::setParameters(const CameraParameters& params)
{
    LOG_FUNCTION_NAME

    int w, h;
    int framerate;

    Mutex::Autolock lock(mLock);

    if (strcmp(params.getPreviewFormat(), PREVIEW_FORMAT) != 0) {
        LOGE("Only yuv422i preview is supported");
        return -1;
    }

    if (strcmp(params.getPictureFormat(), "jpeg") != 0) {
        LOGE("Only jpeg still pictures are supported");
        return -1;
    }

    params.getPreviewSize(&w, &h);
    if (!validateSize(w, h)) {
        LOGE("Preview size not supported");
        return -1;
    }

    params.getPictureSize(&w, &h);
    if (!validateSize(w, h)) {
        LOGE("Picture size not supported");
        return -1;
    }

    framerate = params.getPreviewFrameRate();

    mParameters = params;

    mParameters.getPreviewSize(&w, &h);
    doubledPreviewWidth = false;
    doubledPreviewHeight = false;
    if (w < MIN_WIDTH){
        LOGE("Preview Width < MIN");
        return -1;
    }
    if (h < MIN_HEIGHT){
        LOGE("Preview Height < MIN");
        return -1;
    }
    mParameters.setPreviewSize(w, h);

    /* This is a hack. MMS APP is not setting the resolution correctly. So hardcoding it. */
    mParameters.setPictureSize(PICTURE_WIDTH, PICTURE_HEIGHT);

    return NO_ERROR;
}

CameraParameters CameraHal::getParameters() const
{
    LOG_FUNCTION_NAME
    CameraParameters params;

    {
        Mutex::Autolock lock(mLock);
        params = mParameters;
    }

    int w, h;
    params.getPreviewSize(&w, &h);
    if (doubledPreviewWidth){
        w = w >> 1;
    }
    if (doubledPreviewHeight){
        h= h >> 1;
    }
    params.setPreviewSize(w, h);
    return params;
}

status_t CameraHal::sendCommand(int32_t command, int32_t arg1,
                                         int32_t arg2)
{
    return BAD_VALUE;
}

void CameraHal::release()
{
    LOG_FUNCTION_NAME

    close(camera_device);
}


sp<CameraHardwareInterface> CameraHal::createInstance()
{
    LOG_FUNCTION_NAME

    if (singleton != 0) {
        sp<CameraHardwareInterface> hardware = singleton.promote();
        if (hardware != 0) {
            return hardware;
        }
    }

    sp<CameraHardwareInterface> hardware(new CameraHal());

    singleton = hardware;
    return hardware;
}

#ifdef USE_FSL_JPEG_ENC
JPEG_ENC_UINT8 CameraHal::pushJpegOutput(JPEG_ENC_UINT8 ** out_buf_ptrptr,JPEG_ENC_UINT32 *out_buf_len_ptr,
    JPEG_ENC_UINT8 flush, void * context, JPEG_ENC_MODE enc_mode)
{
    JPEG_ENC_UINT32 i;
    if(*out_buf_ptrptr == NULL)
    {
        /* This function is called for the 1'st time from the
         * codec */
        *out_buf_ptrptr = g_JpegData;
        *out_buf_len_ptr = g_JpegDataSize;
    }

    else if(flush == 1)
    {
        /* Flush the buffer*/
        g_JpegDataLen += *out_buf_len_ptr;
        LOGI("jpeg output data len %d",g_JpegDataLen);
    }
    else
    {
        LOGI("Not enough buffer for encoding");
        return 0;
    }

    return(1); /* Success */
}


sp<MemoryBase> CameraHal::encodeImage(void *buffer, uint32_t bufflen)
{
    int width, height, size,index;
    JPEG_ENC_UINT8 * i_buff = NULL;
    JPEG_ENC_UINT8 * y_buff = NULL;
    JPEG_ENC_UINT8 * u_buff = NULL;
    JPEG_ENC_UINT8 * v_buff = NULL;
    JPEG_ENC_RET_TYPE return_val;
    jpeg_enc_parameters * params = NULL;
    jpeg_enc_object * obj_ptr = NULL;
    JPEG_ENC_UINT8 number_mem_info;
    jpeg_enc_memory_info * mem_info = NULL;
    sp<MemoryBase> jpegPtr = NULL;
    
    mParameters.getPictureSize(&width, &height);    

    if((width== 0)||(height == 0)|!buffer||(bufflen == 0))
    {
        LOGI("Error!Invalid parameters");
        return NULL;
    }
    g_JpegDataSize = 0;//Total size of g_JpegData
    g_JpegDataLen = 0;//Valid data len of g_JpegData
    g_JpegData = NULL;//Buffer to hold jpeg data
    size = width*height*3/2;
    LOGI("CameraHal::encodeImage:buffer 0x%x, bufflen %d,width %d,height %d",buffer,bufflen,width,height);
    sp<MemoryHeapBase> mJpegImageHeap = new MemoryHeapBase(size);

    g_JpegData = (JPEG_ENC_UINT8 *)(mJpegImageHeap->getBase());
    g_JpegDataSize = size;
    if(!g_JpegData)
    {
        LOGI("Cannot allocate jpeg data");
        return NULL;
    }
        
    /* --------------------------------------------
     * Allocate memory for Encoder Object
     * -------------------------------------------*/
    obj_ptr = (jpeg_enc_object *) malloc(sizeof(jpeg_enc_object));
    if(!obj_ptr)
    {
        LOGE("Error!Cannot allocate enc obj");
        return NULL;
    }
    memset(obj_ptr, 0, sizeof(jpeg_enc_object));
    
    /* Assign the function for streaming output */
    obj_ptr->jpeg_enc_push_output = pushJpegOutput;
    obj_ptr->context=NULL;   //user can put private variables into it
    /* --------------------------------------------
     * Fill up the parameter structure of JPEG Encoder
     * -------------------------------------------*/
    params = &(obj_ptr->parameters);

    params->compression_method = JPEG_ENC_SEQUENTIAL;
    params->mode = JPEG_ENC_MAIN_ONLY;
    params->quality = 75;
    params->restart_markers = 0;
    params->y_width = width;
    params->y_height = height;
    params->u_width = params->y_width/2;
    params->u_height = params->y_height/2;
    params->v_width = params->y_width/2;
    params->v_height = params->y_height/2;
    params->primary_image_height = height;
    params->primary_image_width = width;
    params->yuv_format = JPEG_ENC_YUV_420_NONINTERLEAVED;
    params->exif_flag = 0;

    params->y_left = 0;
    params->y_top = 0;
    params->y_total_width = 0;
    params->y_total_height = 0;
    params->raw_dat_flag= 0;	

    if(params->y_total_width==0)
    {
         params->y_left=0;
	  params->u_left=0;
	  params->v_left=0;
    	  params->y_total_width=params->y_width;  // no cropping
    	  params->u_total_width=params->u_width;  // no cropping
    	  params->v_total_width=params->v_width;  // no cropping
    }

    if(params->y_total_height==0)
    {
        params->y_top=0;
	  params->u_top=0;
	  params->v_top=0;		
    	  params->y_total_height=params->y_height; // no cropping
    	  params->u_total_height=params->u_height; // no cropping
    	  params->v_total_height=params->v_height; // no cropping
    }

     /* Pixel size is unknown by default */
    params->jfif_params.density_unit = 0;
    /* Pixel aspect ratio is square by default */
    params->jfif_params.X_density = 1;
    params->jfif_params.Y_density = 1;

    y_buff = (JPEG_ENC_UINT8 *)buffer;
    u_buff = y_buff+width*height;
    v_buff = u_buff+width*height/4;
    i_buff = NULL;
    LOGI("version: %s\n", jpege_CodecVersionInfo());	
    
    /* --------------------------------------------
     * QUERY MEMORY REQUIREMENTS
     * -------------------------------------------*/
    return_val = jpeg_enc_query_mem_req(obj_ptr);

    if(return_val != JPEG_ENC_ERR_NO_ERROR)
    {
        LOGI("JPEG encoder returned an error when jpeg_enc_query_mem_req was called \n");
        LOGI("Return Val %d\n",return_val);
        goto done;
    }
    LOGI("jpeg_enc_query_mem_req success");
    /* --------------------------------------------
     * ALLOCATE MEMORY REQUESTED BY CODEC
     * -------------------------------------------*/
    number_mem_info = obj_ptr->mem_infos.no_entries;
    for(index = 0; index < number_mem_info; index++)
    {
        /* This example code ignores the 'alignment' and
         * 'memory_type', but some other applications might want
         * to allocate memory based on them */
        mem_info = &(obj_ptr->mem_infos.mem_info[index]);
        mem_info->memptr = (void *) malloc(mem_info->size);
	if(mem_info->memptr==NULL)
	{
		LOGI("Malloc error after query\n");
		goto done;
	}
    }


    return_val = jpeg_enc_init(obj_ptr);
    if(return_val != JPEG_ENC_ERR_NO_ERROR)
    {
        LOGI("JPEG encoder returned an error when jpeg_enc_init was called \n");
        LOGI("Return Val %d\n",return_val);
        goto done;
    }
    LOGI("jpeg_enc_init success");
    return_val = jpeg_enc_encodeframe(obj_ptr, i_buff,
                                      y_buff, u_buff, v_buff);

    if(return_val != JPEG_ENC_ERR_ENCODINGCOMPLETE)
    {
        LOGI("JPEG encoder returned an error in jpeg_enc_encodeframe \n");
        LOGI("Return Val %d\n",return_val);
        goto done;
    }
    LOGI("jpeg_enc_encodeframe success");    
    // Make an IMemory for each frame
    jpegPtr = new MemoryBase(mJpegImageHeap, 0, g_JpegDataLen);    
    
done:
    /* --------------------------------------------
     * FREE MEMORY REQUESTED BY CODEC
     * -------------------------------------------*/
    if(obj_ptr)
    {
        number_mem_info = obj_ptr->mem_infos.no_entries;
        for(index = 0; index < number_mem_info; index++)
        {
            mem_info = &(obj_ptr->mem_infos.mem_info[index]);
            if(mem_info)
                free(mem_info->memptr);
        }
        free(obj_ptr);
    }
    return jpegPtr;
}

#endif

extern "C" sp<CameraHardwareInterface> openCameraHardware()
{
    LOGD("Opening Freescale Camera HAL\n");

    return CameraHal::createInstance();
}

}; // namespace android

