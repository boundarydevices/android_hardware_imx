/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2012-2014 Freescale Semiconductor, Inc.
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
#include "TVINDevice.h"
#include <poll.h>

#define DEFAULT_PREVIEW_FPS (15)


typedef unsigned int u32;
typedef unsigned char u8;

static void convertYUV444toNV12SP(u8 *inputBuffer,
                                         u8 *outputBuffer,
                                         u32      width,
                                         u32      height)
{
    u32 line, col;
    u8 *pbySrcY = inputBuffer + 2;
    u8 *pbySrcU = inputBuffer + 1;
    u8 *pbySrcV = inputBuffer;
    u8 *pbyDstY = outputBuffer;
    u8 *pbyDstU = outputBuffer + width * height;
    u8 *pbyDstV = pbyDstU + 1;

    for(line = 0; line < height; line++) {

        for(col = 0; col < width; col++) {
            *pbyDstY = *pbySrcY;

            pbySrcY += 4;
            pbyDstY++;

            if((line < height/2) && (col < width/2)) {
                *pbyDstU = *pbySrcU;
                *pbyDstV = *pbySrcV;

                pbySrcU += 8;
                pbySrcV += 8;

                pbyDstU += 2;
                pbyDstV += 2;
            }
        }

        pbySrcU += width * 4;
        pbySrcV += width * 4;
    }

    return;
}

PixelFormat TVINDevice::getMatchFormat(int *sfmt,
                                     int  slen,
                                     int *dfmt,
                                     int  dlen)
{
    if ((sfmt == NULL) || (slen == 0) || (dfmt == NULL) || (dlen == 0)) {
        FLOGE("setSupportedPreviewFormats invalid parameters");
        return 0;
    }

    PixelFormat matchFormat = 0;
    bool live               = true;
    for (int i = 0; i < slen && live; i++) {
        for (int j = 0; j < dlen; j++) {
            FLOG_RUNTIME("sfmt[%d]=%c%c%c%c, dfmt[%d]=%c%c%c%c",
                         i,
                         sfmt[i] & 0xFF,
                         (sfmt[i] >> 8) & 0xFF,
                         (sfmt[i] >> 16) & 0xFF,
                         (sfmt[i] >> 24) & 0xFF,
                         j,
                         dfmt[j] & 0xFF,
                         (dfmt[j] >> 8) & 0xFF,
                         (dfmt[j] >> 16) & 0xFF,
                         (dfmt[j] >> 24) & 0xFF);
            if (sfmt[i] == dfmt[j]) {
                matchFormat = convertV4L2FormatToPixelFormat(dfmt[j]);
                live        = false;
                break;
            }
        }
    }

    return matchFormat;
}

status_t TVINDevice::setSupportedPreviewFormats(int *sfmt,
                                              int  slen,
                                              int *dfmt,
                                              int  dlen)
{
    if ((sfmt == NULL) || (slen == 0) || (dfmt == NULL) || (dlen == 0)) {
        FLOGE("setSupportedPreviewFormats invalid parameters");
        return BAD_VALUE;
    }

    char fmtStr[FORMAT_STRING_LEN];
    memset(fmtStr, 0, FORMAT_STRING_LEN);
    for (int i = 0; i < slen; i++) {
        for (int j = 0; j < dlen; j++) {
            // should report VPU support format.
            if (sfmt[i] == dfmt[j]) {
                if (sfmt[i] == v4l2_fourcc('Y', 'U', '1', '2')) {
                    strcat(fmtStr, "yuv420p");
                    strcat(fmtStr, ",");
                }
                else if (sfmt[i] == v4l2_fourcc('N', 'V', '1', '2')) {
                    strcat(fmtStr, "yuv420sp");
                    strcat(fmtStr, ",");
                }
                else if (sfmt[i] == v4l2_fourcc('Y', 'U', 'Y', 'V')) {
                    strcat(fmtStr, "yuv422i-yuyv");
                    strcat(fmtStr, ",");
                }
            }
        }
    }
    mParams.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FORMATS, fmtStr);

    return NO_ERROR;
}

status_t TVINDevice::setPreviewStringFormat(PixelFormat format)
{
    const char *pformat = NULL;

    if (format == HAL_PIXEL_FORMAT_YCbCr_420_P) {
        pformat = "yuv420p";
    }
    else if (format == HAL_PIXEL_FORMAT_YCbCr_420_SP) {
        pformat = "yuv420sp";
    }
    else if (format == HAL_PIXEL_FORMAT_YCbCr_422_I) {
        pformat = "yuv422i-yuyv";
    }
    else {
        FLOGE("format %d is not supported", format);
        return BAD_VALUE;
    }
    ALOGI("setPreviewFormat: %s", pformat);
    mParams.setPreviewFormat(pformat);
    mParams.set(CameraParameters::KEY_VIDEO_FRAME_FORMAT, pformat);
    return NO_ERROR;
}

status_t TVINDevice::setDeviceConfig(int         width,
                                        int         height,
                                        PixelFormat format,
                                        int         fps)
{
    if (mCameraHandle <= 0) {
        FLOGE("setDeviceConfig: DeviceAdapter uninitialized");
        return BAD_VALUE;
    }
    if ((width == 0) || (height == 0)) {
        FLOGE("setDeviceConfig: invalid parameters");
        return BAD_VALUE;
    }

    status_t ret = NO_ERROR;
    int input    = 1;
    ret = ioctl(mCameraHandle, VIDIOC_S_INPUT, &input);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_INPUT Failed: %s", strerror(errno));
        return ret;
    }

    int vformat;

#ifdef VADC_TVIN
    format; //avoid compile error
    vformat = v4l2_fourcc('Y', '4', '4', '4');
#else
    vformat = convertPixelFormatToV4L2Format(format);
#endif

    FLOGI("Width * Height %d x %d format 0x%x, fps: %d",
          width,
          height,
          vformat,
          fps);

    mVideoInfo->width       = width;
    mVideoInfo->height      = height;
    mVideoInfo->framesizeIn = (width * height << 1);
    mVideoInfo->formatIn    = vformat;

    mVideoInfo->param.type =
        V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->param.parm.capture.timeperframe.numerator   = 1;
    mVideoInfo->param.parm.capture.timeperframe.denominator = 0;
    mVideoInfo->param.parm.capture.capturemode = 0;
    ret = ioctl(mCameraHandle, VIDIOC_S_PARM, &mVideoInfo->param);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_PARM Failed: %s", strerror(errno));
        return ret;
    }


#ifdef VADC_TVIN
    ret = ioctl(mCameraHandle, VIDIOC_S_STD, &mSTD);
    if(ret < 0)
    {
		FLOGE("setDeviceConfig, VIDIOC_S_STD %d failed, V4L2_STD_PAL %d, V4L2_STD_NTSC %d\n", (int)mSTD, (int)V4L2_STD_PAL, (int)V4L2_STD_NTSC);
		return BAD_VALUE;
	}
    FLOGI("setDeviceConfig, VIDIOC_S_STD %d, V4L2_STD_PAL %d, V4L2_STD_NTSC %d\n", (int)mSTD, (int)V4L2_STD_PAL, (int)V4L2_STD_NTSC);
#endif

    memset(&mVideoInfo->format, 0, sizeof(mVideoInfo->format));
    mVideoInfo->format.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->format.fmt.pix.width        = width & 0xFFFFFFF8;
    mVideoInfo->format.fmt.pix.height       = height & 0xFFFFFFF8;
    mVideoInfo->format.fmt.pix.pixelformat  = vformat;
    // Special stride alignment for YU12
    if (vformat == v4l2_fourcc('Y', 'U', '1', '2')){
        // Goolge define the the stride and c_stride for YUV420 format
        // y_size = stride * height
        // c_stride = ALIGN(stride/2, 16)
        // c_size = c_stride * height/2
        // size = y_size + c_size * 2
        // cr_offset = y_size
        // cb_offset = y_size + c_size
        // int stride = (width+15)/16*16;
        // int c_stride = (stride/2+16)/16*16;
        // y_size = stride * height
        // c_stride = ALIGN(stride/2, 16)
        // c_size = c_stride * height/2
        // size = y_size + c_size * 2
        // cr_offset = y_size
        // cb_offset = y_size + c_size

        // GPU and IPU take below stride calculation
        // GPU has the Y stride to be 32 alignment, and UV stride to be
        // 16 alignment.
        // IPU have the Y stride to be 2x of the UV stride alignment
        int stride = (width+31)/32*32;
        int c_stride = (stride/2+15)/16*16;
        mVideoInfo->format.fmt.pix.bytesperline = stride;
        mVideoInfo->format.fmt.pix.sizeimage    = stride*height+c_stride * height;
        FLOGI("Special handling for YV12 on Stride %d, size %d",
            mVideoInfo->format.fmt.pix.bytesperline,
            mVideoInfo->format.fmt.pix.sizeimage);
    }

    ret = ioctl(mCameraHandle, VIDIOC_S_FMT, &mVideoInfo->format);
    if (ret < 0) {
        FLOGE("Open: VIDIOC_S_FMT Failed: %s", strerror(errno));
        return ret;
    }

    ALOGI("TVINDevice::setDeviceConfig, after VIDIOC_S_FMT, w %d, h %d, size %d\n",
        mVideoInfo->format.fmt.pix.width,
        mVideoInfo->format.fmt.pix.height,
        mVideoInfo->format.fmt.pix.sizeimage);

    return ret;
}

status_t TVINDevice::initParameters(CameraParameters& params,
                                  int              *supportRecordingFormat,
                                  int               rfmtLen,
                                  int              *supportPictureFormat,
                                  int               pfmtLen)
{
    int ret = 0, index = 0;
    int maxWait = 6;
    int sensorFormat[MAX_SENSOR_FORMAT];

    if (mCameraHandle < 0) {
        FLOGE("TVINDevice: initParameters sensor has not been opened");
        return BAD_VALUE;
    }
    if ((supportRecordingFormat == NULL) || (rfmtLen == 0) ||
        (supportPictureFormat == NULL) || (pfmtLen == 0)) {
        FLOGE("TVINDevice: initParameters invalid parameters");
        return BAD_VALUE;
    }

    // Get the PAL/NTSC STD
    do {
        ret = ioctl(mCameraHandle, VIDIOC_G_STD, &mSTD);
        if (ret < 0)
        {
            FLOGE("VIDIOC_G_STD failed with more try %d\n",
                  maxWait - 1);
            sleep(1);
        }
        maxWait --;
    }while ((ret != 0) || (maxWait <= 0));

    if (mSTD == V4L2_STD_PAL)
        FLOGI("Get current mode: PAL");
    else if (mSTD == V4L2_STD_NTSC)
        FLOGI("Get current mode: NTSC");
    else {
        FLOGI("Error!Get invalid mode: %llu", mSTD);
		return BAD_VALUE;
    }

#ifndef VADC_TVIN
	if (ioctl(mCameraHandle, VIDIOC_S_STD, &mSTD) < 0)
	{
		FLOGE("VIDIOC_S_STD failed\n");
		return BAD_VALUE;
	}
#endif

    // first read sensor format.
#if 0
    struct v4l2_fmtdesc vid_fmtdesc;
    while (ret == 0) {
        vid_fmtdesc.index = index;
        vid_fmtdesc.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ret               = ioctl(mCameraHandle, VIDIOC_ENUM_FMT, &vid_fmtdesc);
        FLOG_RUNTIME("index:%d,ret:%d, format:%c%c%c%c", index, ret,
                     vid_fmtdesc.pixelformat & 0xFF,
                     (vid_fmtdesc.pixelformat >> 8) & 0xFF,
                     (vid_fmtdesc.pixelformat >> 16) & 0xFF,
                     (vid_fmtdesc.pixelformat >> 24) & 0xFF);
        if (ret == 0) {
            sensorFormat[index++] = vid_fmtdesc.pixelformat;
        }
    }
#endif // if 0

    // v4l2 does not support enum format, now hard code here.
    sensorFormat[0] = v4l2_fourcc('N', 'V', '1', '2');
    sensorFormat[1] = v4l2_fourcc('Y', 'U', '1', '2');
    sensorFormat[2] = v4l2_fourcc('Y', 'U', 'Y', 'V');
    index           = 3;

    // second check match sensor format with vpu support format and picture
    // format.
    mPreviewPixelFormat = getMatchFormat(supportRecordingFormat,
                                         rfmtLen,
                                         sensorFormat,
                                         index);
    mPicturePixelFormat = getMatchFormat(supportPictureFormat,
                                         pfmtLen,
                                         sensorFormat,
                                         index);
    setPreviewStringFormat(mPreviewPixelFormat);
    ret = setSupportedPreviewFormats(supportRecordingFormat,
                                     rfmtLen,
                                     sensorFormat,
                                     index);
    if (ret) {
        FLOGE("setSupportedPreviewFormats failed");
        return ret;
    }

    index = 0;
    char TmpStr[20];
    int  previewCnt = 0, pictureCnt = 0;
    struct v4l2_frmsizeenum vid_frmsize;
    struct v4l2_frmivalenum vid_frmval;
    while (ret == 0) {
        memset(TmpStr, 0, 20);
        memset(&vid_frmsize, 0, sizeof(struct v4l2_frmsizeenum));
        vid_frmsize.index        = index++;

#ifdef VADC_TVIN
        vid_frmsize.pixel_format = v4l2_fourcc('Y', '4', '4', '4');
#else
        vid_frmsize.pixel_format = v4l2_fourcc('N', 'V', '1', '2');
#endif
        ret                      = ioctl(mCameraHandle,
                                         VIDIOC_ENUM_FRAMESIZES,
                                         &vid_frmsize);
        if (ret == 0) {
            FLOG_RUNTIME("enum frame size w:%d, h:%d",
                         vid_frmsize.discrete.width, vid_frmsize.discrete.height);

#ifdef VADC_TVIN //omit large resolution
			if((vid_frmsize.discrete.width > 800) || (vid_frmsize.discrete.height > 600))
			{
				continue;
			}
#endif
            memset(&vid_frmval, 0, sizeof(struct v4l2_frmivalenum));
            vid_frmval.index        = 0;
            vid_frmval.pixel_format = vid_frmsize.pixel_format;
            vid_frmval.width        = vid_frmsize.discrete.width;
            vid_frmval.height       = vid_frmsize.discrete.height;

            // ret = ioctl(mCameraHandle, VIDIOC_ENUM_FRAMEINTERVALS,
            // &vid_frmval);
            // v4l2 does not support, now hard code here.
            if (ret == 0) {
                FLOG_RUNTIME("vid_frmval denominator:%d, numeraton:%d",
                             vid_frmval.discrete.denominator,
                             vid_frmval.discrete.numerator);
                if ((vid_frmsize.discrete.width > 1280) ||
                    (vid_frmsize.discrete.height > 720)) {
                    vid_frmval.discrete.denominator = 15;
                    vid_frmval.discrete.numerator   = 1;
                }
                else {
                    vid_frmval.discrete.denominator = 30;
                    vid_frmval.discrete.numerator   = 1;
                }

                sprintf(TmpStr,
                        "%dx%d",
                        vid_frmsize.discrete.width,
                        vid_frmsize.discrete.height);

                // Set default to be first enum w/h, since tvin may only
                // have one set
                if (pictureCnt == 0){
                    mParams.setPreviewSize(vid_frmsize.discrete.width,
                            vid_frmsize.discrete.height);
                    mParams.setPictureSize(vid_frmsize.discrete.width,
                            vid_frmsize.discrete.height);
                }

                if (pictureCnt == 0)
                    strncpy((char *)mSupportedPictureSizes,
                            TmpStr,
                            CAMER_PARAM_BUFFER_SIZE);
                else {
                    strncat(mSupportedPictureSizes,
                            PARAMS_DELIMITER,
                            CAMER_PARAM_BUFFER_SIZE);
                    strncat(mSupportedPictureSizes,
                            TmpStr,
                            CAMER_PARAM_BUFFER_SIZE);
                }
                pictureCnt++;

                if (vid_frmval.discrete.denominator /
                    vid_frmval.discrete.numerator >= 15) {
                    if (previewCnt == 0)
                        strncpy((char *)mSupportedPreviewSizes,
                                TmpStr,
                                CAMER_PARAM_BUFFER_SIZE);
                    else {
                        strncat(mSupportedPreviewSizes,
                                PARAMS_DELIMITER,
                                CAMER_PARAM_BUFFER_SIZE);
                        strncat(mSupportedPreviewSizes,
                                TmpStr,
                                CAMER_PARAM_BUFFER_SIZE);
                    }
                    previewCnt++;
                }
            }
        } // end if (ret == 0)
        else {
            FLOGI("enum frame size error %d", ret);
        }
    } // end while

    strcpy(mSupportedFPS, "15,30");
    FLOGI("SupportedPictureSizes is %s", mSupportedPictureSizes);
    FLOGI("SupportedPreviewSizes is %s", mSupportedPreviewSizes);
    FLOGI("SupportedFPS is %s", mSupportedFPS);

    mParams.set(CameraParameters::KEY_SUPPORTED_PICTURE_SIZES,
                mSupportedPictureSizes);
    mParams.set(CameraParameters::KEY_SUPPORTED_PREVIEW_SIZES,
                mSupportedPreviewSizes);
    mParams.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FRAME_RATES,
                mSupportedFPS);
    mParams.set(CameraParameters::KEY_SUPPORTED_PREVIEW_FPS_RANGE,
                "(12000,17000),(25000,33000)");
    // Align the default FPS RANGE to the DEFAULT_PREVIEW_FPS
    mParams.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "12000,17000");
    mParams.setPreviewFrameRate(DEFAULT_PREVIEW_FPS);

    params = mParams;
    return NO_ERROR;
}

status_t TVINDevice::setParameters(CameraParameters& params)
{
    int  w, h;
    int  framerate, local_framerate;
    int  max_zoom, zoom, max_fps, min_fps;
    char tmp[128];

    Mutex::Autolock lock(mLock);

    max_zoom = params.getInt(CameraParameters::KEY_MAX_ZOOM);
    zoom     = params.getInt(CameraParameters::KEY_ZOOM);
    if (zoom > max_zoom) {
        FLOGE("Invalid zoom setting, zoom %d, max zoom %d", zoom, max_zoom);
        return BAD_VALUE;
    }
    if (!((strcmp(params.getPreviewFormat(), "yuv420sp") == 0) ||
          (strcmp(params.getPreviewFormat(), "yuv420p") == 0) ||
          (strcmp(params.getPreviewFormat(), "yuv422i-yuyv") == 0))) {
        FLOGE("Only yuv420sp or yuv420pis supported, but input format is %s",
              params.getPreviewFormat());
        return BAD_VALUE;
    }

    if (strcmp(params.getPictureFormat(), "jpeg") != 0) {
        FLOGE("Only jpeg still pictures are supported");
        return BAD_VALUE;
    }

    params.getPreviewSize(&w, &h);
    sprintf(tmp, "%dx%d", w, h);
    FLOGI("Set preview size: %s", tmp);
    if (strstr(mSupportedPreviewSizes, tmp) == NULL) {
        FLOGE("The preview size w %d, h %d is not corrected", w, h);
        return BAD_VALUE;
    }

    params.getPictureSize(&w, &h);
    sprintf(tmp, "%dx%d", w, h);
    FLOGI("Set picture size: %s", tmp);
    if (strstr(mSupportedPictureSizes, tmp) == NULL) {
        FLOGE("The picture size w %d, h %d is not corrected", w, h);
        return BAD_VALUE;
    }

    local_framerate = mParams.getPreviewFrameRate();
    FLOGI("get local frame rate:%d FPS", local_framerate);
    if ((local_framerate > 30) || (local_framerate < 0)) {
        FLOGE("The framerate is not corrected");
        local_framerate = 15;
    }

    framerate = params.getPreviewFrameRate();
    FLOGI("Set frame rate:%d FPS", framerate);
    if ((framerate > 30) || (framerate < 0)) {
        FLOGE("The framerate is not corrected");
        return BAD_VALUE;
    }
    else if (local_framerate != framerate) {
        if (framerate == 15) {
            params.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "12000,17000");
        }
        else if (framerate == 30) {
            params.set(CameraParameters::KEY_PREVIEW_FPS_RANGE, "25000,33000");
        }
    }

    int actual_fps = 15;
    params.getPreviewFpsRange(&min_fps, &max_fps);
    FLOGI("FPS range: %d - %d", min_fps, max_fps);
    if ((max_fps < 1000) || (min_fps < 1000) || (max_fps > 33000) ||
        (min_fps > 33000)) {
        FLOGE("The fps range from %d to %d is error", min_fps, max_fps);
        return BAD_VALUE;
    }
    actual_fps = min_fps > 15000 ? 30 : 15;
    FLOGI("setParameters: actual_fps=%d", actual_fps);
    params.setPreviewFrameRate(actual_fps);

    mParams = params;
    return NO_ERROR;
}


status_t TVINDevice::registerCameraFrames(CameraFrame *pBuffer,
                                             int        & num)
{
    status_t ret = NO_ERROR;

    if ((pBuffer == NULL) || (num <= 0)) {
        FLOGE("requestCameraBuffers invalid pBuffer");
        return BAD_VALUE;
    }

    mVideoInfo->rb.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    mVideoInfo->rb.memory = V4L2_MEMORY_MMAP;
    mVideoInfo->rb.count  = num;

    ret = ioctl(mCameraHandle, VIDIOC_REQBUFS, &mVideoInfo->rb);
    if (ret < 0) {
        FLOGE("VIDIOC_REQBUFS failed: %s", strerror(errno));
        return ret;
    }

    for (int i = 0; i < num; i++) {
        memset(&mVideoInfo->buf, 0, sizeof(struct v4l2_buffer));
        mVideoInfo->buf.index    = i;
        mVideoInfo->buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mVideoInfo->buf.memory   = V4L2_MEMORY_MMAP;

	    ret = ioctl(mCameraHandle, VIDIOC_QUERYBUF, &mVideoInfo->buf);
	    if (ret < 0) {
	        FLOGE("Unable to query buffer (%s)", strerror(errno));
	        return ret;
	    }

		mMapedBuf[i].length = mVideoInfo->buf.length;
		mMapedBuf[i].offset = (size_t)mVideoInfo->buf.m.offset;
		mMapedBuf[i].start = (unsigned char*)mmap(NULL, mMapedBuf[i].length,
				 PROT_READ | PROT_WRITE, MAP_SHARED,
				 mCameraHandle, mMapedBuf[i].offset);

		FLOGI("maped idx %d, len %d, phy 0x%x, vir %p",
		i, mMapedBuf[i].length, mMapedBuf[i].offset, mMapedBuf[i].start);
    }


	for (int i = 0; i < num; i++) {         // Associate each Camera buffer
        CameraFrame *buffer = pBuffer + i;
        buffer->setObserver(this);
        mPreviewBufs.add((int)buffer, i);
		mMapedBufVector.add((int)&mMapedBuf[i], i);
	}

    mPreviewBufferSize  = pBuffer->mSize;
    mPreviewBufferCount = num;

    return ret;
}

status_t TVINDevice::startDeviceLocked()
{

    status_t ret = NO_ERROR;
	unsigned int phyAddr = 0;

    FSL_ASSERT(!mPreviewBufs.isEmpty());
    FSL_ASSERT(mBufferProvider != NULL);
	FSL_ASSERT(!mMapedBufVector.isEmpty());

    int queueableBufs = mBufferProvider->maxQueueableBuffers();
    FSL_ASSERT(queueableBufs > 0);

    for (int i = 0; i < queueableBufs; i++) {
		//FLOGI("i %d, num %d", i, queueableBufs);
		MemmapBuf *pMapdBuf = (MemmapBuf *)mMapedBufVector.keyAt(i);
		phyAddr = (unsigned int)pMapdBuf->offset;
		mVideoInfo->buf.index    = i;
        mVideoInfo->buf.type     = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mVideoInfo->buf.memory   = V4L2_MEMORY_MMAP;
        mVideoInfo->buf.m.offset = phyAddr;

		ALOGE("VIDIOC_QBUF, idx %d, phyAddr 0x%x", i, phyAddr);
        ret = ioctl(mCameraHandle, VIDIOC_QBUF, &mVideoInfo->buf);
	    if (ret < 0) {
            FLOGE("VIDIOC_QBUF Failed, %s, mCameraHandle %d", strerror(errno), mCameraHandle);
            return BAD_VALUE;
        }

        mQueued++;
    }


    enum v4l2_buf_type bufType;
    if (!mVideoInfo->isStreamOn) {
        bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;

        ret = ioctl(mCameraHandle, VIDIOC_STREAMON, &bufType);
		ALOGE("VIDIOC_STREAMON, ret %d", ret);
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


status_t TVINDevice::stopDeviceLocked()
{
    status_t ret = NO_ERROR;

	ret = DeviceAdapter::stopDeviceLocked();
	if (ret != 0) {
        FLOGE("call %s failed", __FUNCTION__);
        return ret;
    }

	for (int i = 0; i < mPreviewBufferCount; i++) {
        if (mMapedBuf[i].start!= NULL && mMapedBuf[i].length > 0) {
            munmap(mMapedBuf[i].start, mMapedBuf[i].length);
        }
    }


    return ret;
}

status_t TVINDevice::fillCameraFrame(CameraFrame *frame)
{
    status_t ret = NO_ERROR;
	unsigned int phyAddr = 0;

    if (!mVideoInfo->isStreamOn) {
        return NO_ERROR;
    }

    int i = mPreviewBufs.valueFor((unsigned int)frame);
    if (i < 0) {
        return BAD_VALUE;
    }


	MemmapBuf *pMemmapBuf = (MemmapBuf *) mMapedBufVector.keyAt(i);
	phyAddr = (unsigned int)pMemmapBuf->offset;
//	FLOGI("==== VIDIOC_QBUF idx %d, phy 0x%x", i, phyAddr);


    struct v4l2_buffer cfilledbuffer;
    memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_MMAP;
    cfilledbuffer.index    = i;
    cfilledbuffer.m.offset = phyAddr;

    ret = ioctl(mCameraHandle, VIDIOC_QBUF, &cfilledbuffer);
    if (ret < 0) {
        FLOGE("fillCameraFrame: VIDIOC_QBUF Failed");
        return BAD_VALUE;
    }
    mQueued++;

    return ret;
}

CameraFrame * TVINDevice::acquireCameraFrame()
{
    int ret;
	int n;
	struct v4l2_buffer cfilledbuffer;
	struct pollfd fdListen;
	int pollCount = 0;

dopoll:
	pollCount++;

	memset(&fdListen, 0, sizeof(fdListen));
	fdListen.fd = mCameraHandle;
	fdListen.events = POLLIN;

    n = poll(&fdListen, 1, MAX_DEQUEUE_WAIT_TIME);
    if(n < 0) {
        FLOGE("Error!Query the V4L2 Handler state error.");
    }
    else if(n == 0) {
        FLOGI("Warning!Time out wait for V4L2 capture reading operation!");
    }
    else if(fdListen.revents & POLLIN) {
		memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
	    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	    cfilledbuffer.memory = V4L2_MEMORY_MMAP;

	    /* DQ */
	    ret = ioctl(mCameraHandle, VIDIOC_DQBUF, &cfilledbuffer);
	    if (ret < 0) {
	        FLOGE("GetFrame: VIDIOC_DQBUF Failed, fd  %d, erro %s", mCameraHandle, strerror(errno));
	        return NULL;
	    }
	    mDequeued++;

	    int index = cfilledbuffer.index;
	    FSL_ASSERT(!mPreviewBufs.isEmpty(), "mPreviewBufs is empty");
		FSL_ASSERT(!mMapedBufVector.isEmpty(), "mMapedBufVector is empty");

		CameraFrame *camFrame = (CameraFrame *)mPreviewBufs.keyAt(index);
		MemmapBuf *pMapedBuf = (MemmapBuf *)mMapedBufVector.keyAt(index);

		convertYUV444toNV12SP(pMapedBuf->start, (u8 *)camFrame->mVirtAddr, mVideoInfo->width, mVideoInfo->height);

	    return camFrame;

    }
	else {
        FLOGW("Poll the V4L2 Handler, revent 0x%x, pollCount %d", fdListen.revents, pollCount);
		usleep(10000);

	if(pollCount <= 3)
		goto dopoll;
    }

	return NULL;

}


void TVINDevice::onBufferDestroy()
{
    mPreviewBufs.clear();
	mMapedBufVector.clear();
}


