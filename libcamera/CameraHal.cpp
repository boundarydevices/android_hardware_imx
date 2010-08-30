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
int CameraHal::g_rotate=0;
int CameraHal::g_still_bpp = 16;
char CameraHal::dev_node[FILENAME_LENGTH];

#ifdef USE_FSL_JPEG_ENC 
JPEG_ENC_UINT32 CameraHal::g_JpegDataSize = 0;//Total size of g_JpegData
JPEG_ENC_UINT32 CameraHal::g_JpegDataLen = 0;//Valid data len of g_JpegData
JPEG_ENC_UINT8 *CameraHal::g_JpegData = NULL;//Buffer to hold jpeg data
#endif

wp<CameraHardwareInterface> CameraHal::singleton;

const char CameraHal::supportedPictureSizes [] = "2048x1536,1600x1200,1024x768,640x480";
const char CameraHal::supportedPreviewSizes [] = "1280x720,720x576,640x480,320x240";
const char CameraHal::supportedFPS [] = "30,15,10";
const char CameraHal::supprotedThumbnailSizes []= "80x60";
const char CameraHal::PARAMS_DELIMITER []= ",";

CameraHal::CameraHal()
                  : mParameters(),
                    mRecordHeight(0),
                    mRecordWidth(0),
                    mPictureHeight(0),
                    mPictureWidth(0),
                    fcount(6),
                    mOverlay(NULL),
                    mPreviewRunning(0),
                    mPreviewHeap(0),
                    mRecordFrameSize(0),
                    mRecordRunning(0),
                    mCurrentRecordFrame(0),
                    mVideoHeap(0),
                    mNotifyCb(0),
                    mDataCb(0),
                    mDataCbTimestamp(0),
                    mCallbackCookie(0),
                    mMsgEnabled(0),
                    nCameraBuffersQueued(0),
                    mCameraOpened(0)
{
    int i;

    for (i = 0; i < VIDEO_OUTPUT_BUFFER_NUM; i++) {
        mVideoBuffers[i] = 0;
        mVideoBufferUsing[i] = 0;
    }

    for (i = 0; i < CAPTURE_BUFFER_NUM; i++) {
	mPreviewBuffers[i] = 0;
	mCaptureBuffers[i].length = 0;
    }

    is_overlay_pushmode = 0;

#ifdef UVC_CAMERA
    mRecordFormat = mPictureFormat = V4L2_PIX_FMT_YUYV;
#else
    mRecordFormat = mPictureFormat = V4L2_PIX_FMT_YUV420;
#endif

    initDefaultParameters();
}

void CameraHal::initDefaultParameters()
{
    CameraParameters p;
    char tmpBuffer[PARAM_BUFFER], picture_sizes[PARAM_BUFFER];

    p.setPreviewSize(RECORDING_WIDTH_NORMAL, RECORDING_HEIGHT_NORMAL);
    p.setPreviewFrameRate(PREVIEW_FRAMERATE);
    p.setPreviewFormat("yuv420sp");

    p.setPictureSize(PICTURE_WIDTH, PICTURE_HEIGHT);
    p.setPictureFormat("jpeg");
    p.set(CameraParameters::KEY_JPEG_QUALITY, 100);

    //Eclair extended parameters
#ifdef UVC_CAMERA
    uvcGetDeviceAndCapability(tmpBuffer);
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, tmpBuffer);
    LOGD("supportedPictureSize=%s", tmpBuffer);
#else
    /* /dev/video0 is for on-board camera */
    strcpy(dev_node, "/dev/video0");
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES, CameraHal::supportedPictureSizes);
#endif
    p.set(CameraParameters::KEY_SUPPORTED_PICTURE_FORMATS, CameraParameters::PIXEL_FORMAT_JPEG);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES, CameraHal::supportedPreviewSizes);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, CameraParameters::PIXEL_FORMAT_YUV420SP);
    p.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES, CameraHal::supportedFPS);
    p.set(CameraParameters::KEY_SUPPORTED_JPEG_THUMBNAIL_SIZES, CameraHal::supprotedThumbnailSizes);

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

    cameraDestroy();

    LOGD("<<< Release");

    singleton.clear();
}

int CameraHal::uvcGetDeviceAndCapability(char *sizes_buf)
{
    int fd, i, j, is_found = 0;
    char *flags[] = {"uncompressed", "compressed"};
    char tmp[40];
    DIR *v4l_dir = NULL;
    struct dirent *dir_entry;
    struct v4l2_capability v4l2_cap;
    struct v4l2_fmtdesc vid_fmtdesc;
    struct v4l2_frmsizeenum vid_frmsize;

    /* Check avaiable UVC device */
    v4l_dir = opendir("/sys/class/video4linux");
    if (v4l_dir) {
        while((dir_entry = readdir(v4l_dir))) {
            memset((void *)dev_node, 0, FILENAME_LENGTH);
            if(strncmp(dir_entry->d_name, "video", 5)) /* Not video device */
                continue;
                sprintf(dev_node, "/dev/%s", dir_entry->d_name);
                if (fd = open(dev_node, O_RDWR, O_NONBLOCK) < 0)
                    continue;
                /* Query the capability of device */
                if(ioctl(fd, VIDIOC_QUERYCAP, &v4l2_cap) < 0 ) {
                    close(fd);
                continue;
            } else if ((strcmp((char *)v4l2_cap.driver, "uvcvideo") == 0) &&
                       (v4l2_cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
                is_found = 1;  /* No need to close device here */
                break;
            } else
                close(fd);
        }
    }

    if (is_found) {
        LOGD("Found one UVC camera\n");
    } else
        return -1;

    memset(sizes_buf, '\0', PARAM_BUFFER);
    /* Enum pixel format of UVC camera */
    for (i = 0;; i++) {
        memset(&vid_fmtdesc, 0, sizeof(vid_fmtdesc));
        vid_fmtdesc.index = i;
        vid_fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(fd, VIDIOC_ENUM_FMT, &vid_fmtdesc ) != 0)
            break;
        /* We got a video format/codec back */
        LOGD("VIDIOC_ENUM_FMT(%d, %s)\n", vid_fmtdesc.index, "VIDEO_CAPTURE");
        LOGD(" flags       :%s\n", flags[vid_fmtdesc.flags]);
        LOGD(" description :%s\n", vid_fmtdesc.description);
        /* Convert the pixelformat attributes from FourCC into 'human readable' format*/
        LOGD(" pixelformat :%c%c%c%c\n",
                    vid_fmtdesc.pixelformat & 0xFF, (vid_fmtdesc.pixelformat >> 8) & 0xFF,
                    (vid_fmtdesc.pixelformat >> 16) & 0xFF, (vid_fmtdesc.pixelformat >> 24) & 0xFF);
        if (vid_fmtdesc.pixelformat == V4L2_PIX_FMT_YUYV) {
        /* Only support YUYV format now */
        /* Enum YUYV frame size */
            for (j = 0;; j++) {
                vid_frmsize.index = j;
                vid_frmsize.pixel_format = vid_fmtdesc.pixelformat;
                if (ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &vid_frmsize) != 0)
                    break;
                sprintf(tmp, "%dx%d", vid_frmsize.discrete.width, vid_frmsize.discrete.height);
                if (vid_frmsize.index == 0)
                    strncpy((char*) sizes_buf, tmp, PARAM_BUFFER);
                else {
                    strncat((char*) sizes_buf, (const char*) PARAMS_DELIMITER, PARAM_BUFFER);
                    strncat((char*) sizes_buf, tmp, PARAM_BUFFER);
                }
                LOGD(" framze size: width %d, height %d,", vid_frmsize.discrete.width, vid_frmsize.discrete.height);
            }
            /* FIXME: will do enum YUYV frame intervals later*/
        }
    }
    close(fd);

    return 0;
}

int CameraHal::cameraOpen()
{
    int err;

    LOG_FUNCTION_NAME

    if(mCameraOpened == 0){
        camera_device = open(dev_node, O_RDWR, 0);
        LOGD("dev_node in open:%s\n", dev_node);
        if (camera_device < 0) {
            LOGE ("Could not open the camera device: %s",  strerror(errno) );
            return -1;
        }
        mCameraOpened = 1;
        LOGD("Camera Opened - Success");

    } else
        LOGE ("Camera device has been opened!");

    return 0;
}

int CameraHal::cameraClose()
{
    LOG_FUNCTION_NAME
    /* Free buffers firstly */
    for (int i = 0; i < CAPTURE_BUFFER_NUM; i++) {
        if (mCaptureBuffers[i].length && (mCaptureBuffers[i].virt_start > 0)) {
	    munmap(mCaptureBuffers[i].virt_start, mCaptureBuffers[i].length);
            mCaptureBuffers[i].length = 0;
	    LOGD("munmap buffers 0x%x\n", mCaptureBuffers[i].virt_start);
        }
    }
    if (camera_device != -1) {
        close(camera_device);
        camera_device = -1;
        mCameraOpened = 0;
    }

    return 0;
}

int CameraHal::cameraDestroy()
{
    int err, i;

    cameraClose();

    if (mOverlay != NULL) {
        mOverlay->destroy();
        mOverlay = NULL;
    }

    return 0;
}

int CameraHal::cameraTakePicConfig()
{
    struct v4l2_streamparm parm;
    struct v4l2_format fmt;
    struct v4l2_crop crop;
    int fd_v4l;

    if (cameraOpen() < 0)
         return -1;
    fd_v4l = camera_device;

    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.pixelformat = mPictureFormat;
    fmt.fmt.pix.width = mPictureWidth;
    fmt.fmt.pix.height = mPictureHeight;
    fmt.fmt.pix.sizeimage = fmt.fmt.pix.width * fmt.fmt.pix.height * g_still_bpp / 8;
    if (mPictureFormat == V4L2_PIX_FMT_YUV420)
        fmt.fmt.pix.bytesperline = mPictureWidth;
    else if (mPictureFormat == V4L2_PIX_FMT_YUYV)
        fmt.fmt.pix.bytesperline = mPictureWidth * 2;
    else
        LOGE("Not supported format");

    if (ioctl(fd_v4l, VIDIOC_S_FMT, &fmt) < 0) {
        LOGE("set format failed\n");
        return -1;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_v4l, VIDIOC_G_FMT, &fmt) < 0)
        return -1;
    else {
        LOGD(" Width = %d\n", fmt.fmt.pix.width);
        LOGD(" Height = %d \n", fmt.fmt.pix.height);
        LOGD(" Image size = %d\n", fmt.fmt.pix.sizeimage);
        LOGD(" pixelformat = %d\n", fmt.fmt.pix.pixelformat);
    }

    if ((mPictureWidth != fmt.fmt.pix.width) ||
        (mPictureHeight != fmt.fmt.pix.height)) {
        LOGD("Hardware not support the width or height");
        mPictureWidth = fmt.fmt.pix.width;
        mPictureHeight = fmt.fmt.pix.height;
        mParameters.setPictureSize(mPictureWidth, mPictureHeight);
    }

    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = 15;
#ifndef UVC_CAMERA
    /* This capturemode value is related to ov3640 driver */
    if (mPictureWidth > 640 || mPictureHeight > 480)
        parm.parm.capture.capturemode = 3;  /* QXGA mode */
    else
        parm.parm.capture.capturemode = 0;  /* VGA mode */
#endif
    if (ioctl(fd_v4l, VIDIOC_S_PARM, &parm) < 0) {
        LOGE("VIDIOC_S_PARM failed\n");
        return -1;
    }

#ifdef UVC_CAMERA
    /* streaming is used from uvc picture since .read function
       isn't realized in driver */
    cameraPreviewStart();
#else
    crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    crop.c.left = 0;
    crop.c.top = 0;
    crop.c.width = mPictureWidth;
    crop.c.height = mPictureHeight;
    if (ioctl(fd_v4l, VIDIOC_S_CROP, &crop) < 0) {
        LOGE("set cropping failed\n");
        return -1;
    }
#endif
    return NO_ERROR;
}

int CameraHal::cameraPreviewConfig()
{
    struct v4l2_format fmt;
    struct v4l2_control ctrl;
    struct v4l2_streamparm parm;
    int fd_v4l;
    LOG_FUNCTION_NAME

    if (cameraOpen() < 0)
        return -1;
    fd_v4l = camera_device;

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.pixelformat = mRecordFormat;
    fmt.fmt.pix.width = mRecordWidth;
    fmt.fmt.pix.height = mRecordHeight;
    if (mRecordFormat == V4L2_PIX_FMT_YUV420)
        fmt.fmt.pix.bytesperline = mRecordWidth;
    else if (mRecordFormat == V4L2_PIX_FMT_YUYV)
        fmt.fmt.pix.bytesperline = mRecordWidth * 2;
    fmt.fmt.pix.priv = 0;
    fmt.fmt.pix.sizeimage = 0;

    if (ioctl(fd_v4l, VIDIOC_S_FMT, &fmt) < 0) {
        LOGE("set format failed, format 0x%x\n", mRecordFormat);
        return -1;
    }

    //Camera Recording VIDIOC_S_PARM
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = PREVIEW_FRAMERATE;
    parm.parm.capture.capturemode = 0;  /* VGA */

    if (ioctl(fd_v4l, VIDIOC_S_PARM, &parm) < 0) {
        LOGE("VIDIOC_S_PARM failed\n");
        return -1;
    }

#ifndef UVC_CAMERA
    // Set rotation
    ctrl.id = V4L2_CID_PRIVATE_BASE + 0;
    ctrl.value = g_rotate;
    if (ioctl(fd_v4l, VIDIOC_S_CTRL, &ctrl) < 0) {
        LOGE("set ctrl failed\n");
        return -1;
    }
#endif

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_v4l, VIDIOC_G_FMT, &fmt) < 0)
        return -1;
    else {
        LOGD(" Width = %d\n", fmt.fmt.pix.width);
        LOGD(" Height = %d \n", fmt.fmt.pix.height);
        LOGD(" Image size = %d\n", fmt.fmt.pix.sizeimage);
        LOGD(" pixelformat = %d\n", fmt.fmt.pix.pixelformat);
    }

    /* mRecordFrameSize is the size of frame size to upper layer,
       only yuv420sp is supported. */
    mRecordFrameSize = mRecordWidth * mRecordHeight * 3 / 2;
    return 0;
}

int CameraHal::cameraPreviewStart()
{
    int i;
    struct v4l2_buffer buf;
    enum v4l2_buf_type type;
    int fd_v4l = camera_device;

    LOG_FUNCTION_NAME

    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof (req));

    req.count = CAPTURE_BUFFER_NUM;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(fd_v4l, VIDIOC_REQBUFS, &req) < 0) {
        LOGE("v4l_capture_setup: VIDIOC_REQBUFS failed\n");
        return -1;
    }

    for (i = 0; i < CAPTURE_BUFFER_NUM; i++) {
        memset(&buf, 0, sizeof (buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.index = i;
        if (ioctl(fd_v4l, VIDIOC_QUERYBUF, &buf) < 0) {
            LOGE("VIDIOC_QUERYBUF error\n");
            return -1;
        } else {
            LOGE("VIDIOC_QUERYBUF ok\n");
        }

        mCaptureBuffers[i].length = buf.length;
        mCaptureBuffers[i].phy_offset = (size_t) buf.m.offset;
        mCaptureBuffers[i].virt_start = (unsigned char *)mmap (NULL, mCaptureBuffers[i].length,
                    PROT_READ | PROT_WRITE, MAP_SHARED, fd_v4l, mCaptureBuffers[i].phy_offset);
        memset(mCaptureBuffers[i].virt_start, 0xFF, mCaptureBuffers[i].length);
        LOGE("capture buffers[%d].length = %d\n", i, mCaptureBuffers[i].length);
        LOGE("capture buffers[%d].phy_offset = 0x%x\n", i, mCaptureBuffers[i].phy_offset);
        LOGE("capture buffers[%d].virt_start = 0x%x\n", i, mCaptureBuffers[i].virt_start);
    }

    nCameraBuffersQueued = 0;
    for (i = 0; i < CAPTURE_BUFFER_NUM; i++) {
        memset(&buf, 0, sizeof (buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        buf.m.offset = mCaptureBuffers[i].phy_offset;

        if (ioctl (fd_v4l, VIDIOC_QBUF, &buf) < 0) {
            LOGE("VIDIOC_QBUF error\n");
            return -1;
        } else {
            LOGE("VIDIOC_QBUF ok\n");
        }
        nCameraBuffersQueued++;
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl (fd_v4l, VIDIOC_STREAMON, &type) < 0) {
        LOGE("VIDIOC_STREAMON error\n");
        return -1;
    } else
        LOGE("VIDIOC_STREAMON ok\n");

    return 0;
}

sp<IMemoryHeap> CameraHal::getPreviewHeap() const
{
    LOG_FUNCTION_NAME

    return mPreviewHeap;
}

sp<IMemoryHeap> CameraHal::getRawHeap() const
{
    return NULL;
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

void CameraHal::previewOneFrame()
{
    struct v4l2_buffer cfilledbuffer;
    overlay_buffer_t overlaybuffer;
    int ret, display_index, i, image_size, count = 0;
    struct timespec ts;

    /* Use timed wait since captureFrame thread may be exited when waiting for the semaphore */
    do {
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_nsec +=100000; // 100ms
    } while ((sem_timedwait(&avaiable_show_frame, &ts) != 0) && !error_status &&  mPreviewRunning);

    if ((mPreviewRunning == 0) || error_status)
	return;

    image_size = mRecordFrameSize;
    display_index = buffer_index_maps[queue_head];

    if ((mMsgEnabled & CAMERA_MSG_VIDEO_FRAME) && mRecordRunning) {
        nsecs_t timeStamp = systemTime(SYSTEM_TIME_MONOTONIC);
        for(i = 0 ; i < VIDEO_OUTPUT_BUFFER_NUM; i ++) {
            if(mVideoBufferUsing[i] == 0) {
                if (mRecordFormat == V4L2_PIX_FMT_YUYV)
                    memcpy(mVideoBuffers[i]->pointer(), mPreviewBuffers[display_index]->pointer(), image_size);
                else
                    memcpy(mVideoBuffers[i]->pointer(),
		                       (void*)mCaptureBuffers[display_index].virt_start, image_size);
                 mVideoBufferUsing[i] = 1;
                 mDataCbTimestamp(timeStamp, CAMERA_MSG_VIDEO_FRAME, mVideoBuffers[i], mCallbackCookie);
                 break;
            }
        }
        if (i == VIDEO_OUTPUT_BUFFER_NUM)
                 LOGD("no Buffer can be used for record\n");
    }

    if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
        if (mRecordFormat != V4L2_PIX_FMT_YUYV)
            memcpy(mPreviewBuffers[display_index]->pointer(),
                     (void*)mCaptureBuffers[display_index].virt_start, image_size);
        mDataCb(CAMERA_MSG_PREVIEW_FRAME, mPreviewBuffers[display_index], mCallbackCookie);
    }

    /* Notify overlay of a new frame. */
    if (mOverlay != 0) {
	if (is_overlay_pushmode) {
	    if (mOverlay->queueBuffer((overlay_buffer_t)mCaptureBuffers[display_index].phy_offset))
		LOGD("qbuf failed. May be bcos stream was not turned on yet.");
	    /* For overlay push mode, the second queueBuffer return means last buffer can
	       be used for capturing next frame */
	    if (is_first_buffer) {
		is_first_buffer = 0;
		goto out;
	    }
	} else {
            mOverlay->dequeueBuffer(&overlaybuffer);
            void* address = mOverlay->getBufferAddress(overlaybuffer);
            if (mRecordFormat == V4L2_PIX_FMT_YUYV)
                memcpy(address, mPreviewBuffers[display_index]->pointer(), image_size);
            else
                memcpy(address, (void*)mCaptureBuffers[display_index].virt_start, image_size);
            if (mOverlay->queueBuffer(overlaybuffer))
		LOGD("qbuf failed. May be bcos stream was not turned on yet.");
	}
    }

    /* Queue the buffer to camera for coming usage */
    memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_MMAP;
    cfilledbuffer.index = display_index;

#ifdef UVC_CAMERA
    ret = ioctl(camera_device, VIDIOC_QBUF, &cfilledbuffer);
    if (ret < 0) {
	LOGE("uvc camera device VIDIOC_QBUF failure, ret=%d", ret);
	error_status = -1;
	return;
    }
#else
    ret = ioctl(camera_device, VIDIOC_QUERYBUF, &cfilledbuffer);
    if (ret < 0) {
        error_status = -1;
        LOGE("VIDIOC_QUERYBUF camera device failure, ret=%d", ret);
        return;
    }

    while ((ret = ioctl(camera_device, VIDIOC_QBUF, &cfilledbuffer) < 0) && (count < 10)) {
	count ++;
    }
    if (ret < 0) {
	LOGE("VIDIOC_QBUF Failure, ret=%d", ret);
	error_status = -1;
	return;
    }

#endif
    nCameraBuffersQueued++;
    queue_head ++;
    queue_head %= CAPTURE_BUFFER_NUM;

    sem_post(&avaible_dequeue_frame);
out:
    return;
}

int CameraHal::previewShowFrameThread()
{
    if (mPreviewRunning && !error_status) {
        previewOneFrame();
    }

    return NO_ERROR;
}

status_t CameraHal::setOverlay(const sp<Overlay> &overlay)
{
    LOG_FUNCTION_NAME

    Mutex::Autolock lock(mLock);
    if (overlay == NULL)
        LOGE("Trying to set overlay, but overlay is null!");
    mOverlay = overlay;

    if (mOverlay != 0) {
	/* Not enable push mode for UVC camera case yet*/
#ifndef UVC_CAMERA
	mOverlay->setParameter(OVERLAY_MODE, OVERLAY_PUSH_MODE);
	is_overlay_pushmode = 1;
#endif
    }

    return NO_ERROR;
}

int CameraHal::previewCaptureFrameThread()
{
    struct v4l2_buffer cfilledbuffer;
    int ret, index, count = 0;
    struct timespec ts;

    if (mPreviewRunning && !error_status) {

        /* Use timed wait since another thread may be failure when waiting for the semaphore */
        do {
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_nsec +=100000; // 100ms
        } while ((sem_timedwait(&avaible_dequeue_frame, &ts) != 0) && !error_status);

	memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
	cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	cfilledbuffer.memory = V4L2_MEMORY_MMAP;

#ifdef UVC_CAMERA
	/* De-queue the next avaiable buffer */
	ret = ioctl(camera_device, VIDIOC_DQBUF, &cfilledbuffer);
	if (ret < 0) {
	    LOGE("uvc camera device VIDIOC_DQBUF failure, ret=%d", ret);
	    error_status = -1;
	    return -1;
	}
#else
	/* De-queue the next avaliable buffer in loop since timout is used in driver */
	while ((ret = ioctl(camera_device, VIDIOC_DQBUF, &cfilledbuffer) < 0) &&
                (count < 10) && mPreviewRunning) {
	    count ++;
	}

	if (ret < 0) {
	    LOGE("Camera VIDIOC_DQBUF failure, ret=%d", ret);
	    error_status = -1;
	    return -1;
	}

	if (mPreviewRunning == 0)
	    return -1;
#endif
	nCameraBuffersQueued--;

	/* Convert YUYV to YUV420SP and put to mPreviewBuffer */
	if (mRecordFormat == V4L2_PIX_FMT_YUYV) {
	    convertYUYVtoYUV420SP(mCaptureBuffers[cfilledbuffer.index].virt_start,
				  (uint8_t *)mPreviewBuffers[cfilledbuffer.index]->pointer(), mRecordWidth, mRecordHeight);
	}

	buffer_index_maps[dequeue_head] = cfilledbuffer.index;
	dequeue_head ++;
	dequeue_head %= CAPTURE_BUFFER_NUM;
	sem_post(&avaiable_show_frame);
    }

    return UNKNOWN_ERROR;
}

status_t CameraHal::startPreview()
{
    LOG_FUNCTION_NAME

    int i, width, height;

    Mutex::Autolock lock(mLock);
    if (mPreviewRunning != 0) {
        // already running
        return INVALID_OPERATION;
    }

    dequeue_head = 0;
    queue_head = 0;
    error_status = 0;
    is_first_buffer = 1;

    for (i = 0; i < CAPTURE_BUFFER_NUM; i++) {
        mCaptureBuffers[i].length = 0;
    }

    if (cameraPreviewConfig() < 0)
        return INVALID_OPERATION;

    LOGD("Clear the old preview memory and Init new memory");
    mPreviewHeap.clear();
    for (i = 0; i< 3; i++)
        mPreviewBuffers[i].clear();
    mPreviewHeap = new MemoryHeapBase(mRecordFrameSize * VIDEO_OUTPUT_BUFFER_NUM);
    for (i = 0; i < 3; i++)
       mPreviewBuffers[i] = new MemoryBase(mPreviewHeap, mRecordFrameSize * i, mRecordFrameSize);

    cameraPreviewStart();

    sem_init(&avaiable_show_frame, 0, 0);
    /* Init sem to CAPTURE_BUFFER_NUM -1 since buffer can be dequeued at least
       two buffers are queued */
    sem_init(&avaible_dequeue_frame, 0, CAPTURE_BUFFER_NUM - 1);

    mPreviewCaptureFrameThread = new PreviewCaptureFrameThread(this);
    mPreviewShowFrameThread = new PreviewShowFrameThread(this);

    mPreviewRunning = true;

    return NO_ERROR;
}

void CameraHal::stopPreview()
{
    LOG_FUNCTION_NAME

    Mutex::Autolock lock(mLock);
    /* Cannot stop preview in recording */
    if(mMsgEnabled & CAMERA_MSG_VIDEO_FRAME)
	    return;
    cameraPreviewStop();
}

void CameraHal::cameraPreviewStop()
{
    sp<PreviewShowFrameThread> previewShowFrameThread;
    sp<PreviewCaptureFrameThread> previewCaptureFrameThread;
    struct v4l2_requestbuffers creqbuf;

    LOG_FUNCTION_NAME

    if (mPreviewRunning != 0) {
        int ret;
        struct v4l2_requestbuffers creqbuf;
        struct v4l2_buffer cfilledbuffer;

        mPreviewRunning = 0;

	{// scope for the lock
//	Mutex::Autolock lock(mLock);
        previewShowFrameThread = mPreviewShowFrameThread;
	previewCaptureFrameThread = mPreviewCaptureFrameThread;
	}

	if (previewCaptureFrameThread != 0)
	    previewCaptureFrameThread->requestExitAndWait();

        if (previewShowFrameThread != 0)
	    previewShowFrameThread->requestExitAndWait();

	sem_destroy(&avaiable_show_frame);
	sem_destroy(&avaible_dequeue_frame);

        /* Flush to release buffer used in overlay */
        if (mOverlay != 0) {
            if (is_overlay_pushmode) {
                //if (mOverlay->queueBuffer(NULL))
                //    LOGD("overlay queueBuffer NULL failure");
		/* FIXME: Cannot use queueBuffer here since overlay may be streamed off,
		   delay some time here now*/
		usleep(30000);
            }
        }

/* no need to DQBUF before STREAMOFF for UVC camera to improve performance */
#if 0
    if (!error_status) {
        memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cfilledbuffer.memory = V4L2_MEMORY_MMAP;

        while(nCameraBuffersQueued){
            LOGD("DQUEUING UNDQUEUED BUFFERS enter = %d",nCameraBuffersQueued);
            if (ioctl(camera_device, VIDIOC_DQBUF, &cfilledbuffer) < 0) {
                LOGE("VIDIOC_DQBUF Failed!!!");
            }
            nCameraBuffersQueued--;
            LOGD("DQUEUING UNDQUEUED BUFFERS exit = %d",nCameraBuffersQueued);
        }
    }
#endif

        creqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (ioctl(camera_device, VIDIOC_STREAMOFF, &creqbuf.type) == -1) {
            LOGE("VIDIOC_STREAMOFF Failed");
        }

    /* Close device */
	cameraClose();
    }
//    Mutex::Autolock lock(mLock);
    mPreviewShowFrameThread.clear();
    mPreviewCaptureFrameThread.clear();
}

bool CameraHal::previewEnabled()
{
    return mPreviewRunning;
}

status_t CameraHal::startRecording()
{
    LOG_FUNCTION_NAME 
    int w,h;
    int i = 0, mRecordingFrameSize= 0;

    if (mRecordRunning !=0 ) {
        LOGI("Recording is already existed\n");
        return INVALID_OPERATION;
    }

    LOGD("Clear the old memory ");
    mVideoHeap.clear();
    for(i = 0; i < VIDEO_OUTPUT_BUFFER_NUM; i++) {
        mVideoBuffers[i].clear();
        mVideoBufferUsing[i] = 0;
    }
    LOGD("Init the video Memory %d", mRecordFrameSize);
    mVideoHeap = new MemoryHeapBase(mRecordFrameSize * VIDEO_OUTPUT_BUFFER_NUM);
    for(i = 0; i < VIDEO_OUTPUT_BUFFER_NUM; i++) {
        LOGD("Init Video Buffer:%d ",i);
        mVideoBuffers[i] = new MemoryBase(mVideoHeap,
	                       mRecordFrameSize * i, mRecordFrameSize);
    }
    mRecordRunning = true;

    return NO_ERROR;
}

void CameraHal::stopRecording()
{
    LOG_FUNCTION_NAME

    mRecordRunning = false;
    if(mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
        LOGD("Preview is still in progress\n");
    }
}

bool CameraHal::recordingEnabled()
{
    LOG_FUNCTION_NAME
    return (mPreviewRunning && mRecordRunning);
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
    ssize_t offset;
    size_t  size;
    int index;

    debugShowFPS();
    offset = mem->offset();
    size   = mem->size();
    index = offset / size;

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

void CameraHal::convertYUYVtoYUV420SP(uint8_t *inputBuffer, uint8_t *outputBuffer, int width, int height)
{
    /* Color space conversion from YUYV to YUV420SP */
    int src_size = 0, out_size = 0;
    uint8_t *p;
    uint8_t *Y, *U, *V;

    src_size = (width * height) << 1 ;//YUY2 4:2:2
    p = inputBuffer;

    out_size = width * height * 3 / 2;//YUV 4:2:0
    Y = outputBuffer;
    U = Y + width * height;
    V = U + (width *  height >> 2);

    memset(outputBuffer, 0, out_size);
    for(int k = 0; k < height; ++k) {
        for(int j = 0; j < (width >> 1); ++j) {
            Y[j*2] = p[4*j];
            Y[j*2+1] = p[4*j+2];
            if (k %2 == 0) {
                U[j] = p[4*j+1];
                V[j] = p[4*j+3];
            }
        }
        p = p + width * 2;

        Y = Y + width;
        U = U + (width >> 2);
        V = V + (width >> 2);
    }
}

int CameraHal::cameraTakePicture()
{

    int w, h, ret;
    int pictureSize;
    unsigned long base, offset;
    struct v4l2_streamparm parm;
    struct v4l2_format fmt;
    sp<MemoryBase> mPictureBuffer;
    sp<MemoryBase> memBase;
    int target_size, count = 0;
    uint8_t *buf1;
    struct v4l2_requestbuffers creqbuf;
    struct v4l2_buffer cfilledbuffer;

    LOG_FUNCTION_NAME

    mParameters.getPictureSize(&w, &h);
    LOGD("Picture Size: Width = %d \tHeight = %d", w, h);
    mPictureWidth = w;
    mPictureHeight = h;

    if(cameraTakePicConfig() < 0){
        return -1;
    }

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera_device, VIDIOC_G_FMT, &fmt) < 0) {
        LOGE("get format failed\n");
        return -1;
    }

    LOGD(" Image size = %d\n", fmt.fmt.pix.sizeimage);
    LOGD(" pixelformat = %d\n", fmt.fmt.pix.pixelformat);

    target_size = fmt.fmt.pix.sizeimage;

    buf1 = (uint8_t *)malloc(target_size);
    if (!buf1){
        LOGE("buffer malloc error!\n");
        free(buf1);
        return -1;
    }
    memset(buf1, 0 ,target_size);

    if (mMsgEnabled & CAMERA_MSG_SHUTTER) {
        LOGI("CAMERA_MSG_SHUTTER");
        mNotifyCb(CAMERA_MSG_SHUTTER, 0, 0, mCallbackCookie);
    }

#ifdef UVC_CAMERA
    memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_MMAP;

    /* De-queue the next avaliable buffer */
    ret = ioctl(camera_device, VIDIOC_DQBUF, &cfilledbuffer);
    if (ret < 0){
        LOGE("VIDIOC_DQBUF Failed!!!");
        error_status = -1;
    }

    convertYUYVtoYUV420SP(mCaptureBuffers[cfilledbuffer.index].virt_start,
                          buf1, mPictureWidth, mPictureHeight);

    target_size = mPictureWidth * mPictureHeight * 3 / 2;
#else
    /* Use read to get one picture for on-board camera */
    if (read(camera_device, buf1, target_size) != target_size) {
        LOGE("v4l2 read error.\n");
        free(buf1);
        return -1;
    }

#endif
    LOGD("Generated a picture");

#ifdef UVC_CAMERA
    creqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(camera_device, VIDIOC_STREAMOFF, &creqbuf.type) == -1) {
        LOGE("VIDIOC_STREAMOFF Failed");
    }
#endif
    /* Close device */
    cameraClose();

#ifdef USE_FSL_JPEG_ENC
    sp<MemoryBase> jpegMemBase = encodeImage((void*)buf1, target_size);
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

int CameraHal::beginPictureThread(void *cookie)
{
    CameraHal *c = (CameraHal *)cookie;
    return c->pictureThread();
}

int CameraHal::pictureThread()
{
    LOG_FUNCTION_NAME

    /* Camera video capture is used for UVC camera since read function
       ins't realized on uvc_v4l2 driver, but, read function is called
       for CSI camera */
    LOGD("Camera is taking picture!");
    /* Stop preview, start picture capture, and then restart preview again for CSI camera*/

    cameraPreviewStop();
    cameraTakePicture();

    return NO_ERROR;

}
status_t CameraHal::takePicture()
{
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

    if (!(strcmp(params.getPreviewFormat(), "yuv420sp") == 0) ||
	(strcmp(params.getPreviewFormat(), "yuv422i") == 0)) {
        LOGE("Only yuv420 or yuv420i is supported");
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

    mParameters.getPreviewSize(&mRecordWidth, &mRecordHeight);
    LOGD("mRecordWidth %d, mRecordHeight %d\n", mRecordWidth, mRecordHeight);

    mParameters.getPictureSize(&mPictureWidth, &mPictureHeight);
    LOGD("mPictureWidth %d, mPictureHeight %d\n", mPictureWidth, mPictureHeight);

    if (!mPictureWidth || !mPictureHeight) {
        /* This is a hack. MMS APP is not setting the resolution correctly. So hardcoding it. */
        mParameters.setPictureSize(PICTURE_WIDTH, PICTURE_HEIGHT);
        mParameters.getPictureSize(&mPictureWidth, &mPictureHeight);
    }

    return NO_ERROR;
}

CameraParameters CameraHal::getParameters() const
{
    LOG_FUNCTION_NAME

    Mutex::Autolock lock(mLock);
    return mParameters;
}

status_t CameraHal::sendCommand(int32_t command, int32_t arg1,
                                         int32_t arg2)
{
    return BAD_VALUE;
}

void CameraHal::release()
{
    LOG_FUNCTION_NAME

    cameraClose();
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
    size = width * height * 3 / 2;
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
        if(mem_info->memptr==NULL) {
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

