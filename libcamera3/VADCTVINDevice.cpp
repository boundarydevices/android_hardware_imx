/*
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

#include "VADCTVINDevice.h"

VADCTVINDevice::VADCTVINDevice(int32_t id, int32_t facing, int32_t orientation, char* path)
    : Camera(id, facing, orientation, path)
{
    mVideoStream = new VADCTVinStream(this);
}

VADCTVINDevice::~VADCTVINDevice()
{
}

static int32_t convertYUV444toNV12(u8 *inputBuffer,
        u8 *outputBuffer,
        int32_t      width,
        int32_t      height)
{
    int32_t line, col;
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

                //jump 2 pixels each step
                pbySrcU += 8;
                pbySrcV += 8;

                pbyDstU += 2;
                pbyDstV += 2;
            }
        }

        // Since already moved 8*width/2 bytes,
        // so total move width*8 bytes.
        // It means jump 2 lines each cycle.
        pbySrcU += width * 4;
        pbySrcV += width * 4;
    }

    return 0;
}

status_t VADCTVINDevice::initSensorStaticData()
{
    int32_t fd = open(mDevPath, O_RDWR);
    if (fd < 0) {
        ALOGE("OvDevice: initParameters sensor has not been opened");
        return BAD_VALUE;
    }

    int ret = 0, index = 0;
    int maxWait = 6;
    // Get the PAL/NTSC STD
    do {
        ret = ioctl(fd, VIDIOC_G_STD, &mSTD);
        if (ret < 0) {
            ALOGW("%s VIDIOC_G_STD failed with try %d", __func__, maxWait - 1);
            sleep(1);
        }
        maxWait --;
    } while ((ret != 0) && (maxWait > 0));

    if (mSTD == V4L2_STD_PAL)
        ALOGI("%s Get current mode: PAL", __func__);
    else if (mSTD == V4L2_STD_NTSC)
        ALOGI("%s Get current mode: NTSC", __func__);
    else {
        ALOGE("%s Error!Get invalid mode: %llu", __func__, mSTD);
        close(fd);
        return BAD_VALUE;
    }

    // read sensor format.
    int sensorFormats[MAX_SENSOR_FORMAT];
    int availFormats[MAX_SENSOR_FORMAT];
    memset(sensorFormats, 0, sizeof(sensorFormats));
    memset(availFormats, 0, sizeof(availFormats));

    // v4l2 does not support VIDIOC_ENUM_FMT, now hard code here.
    sensorFormats[index] = v4l2_fourcc('N', 'V', '1', '2');
    availFormats[index++] = v4l2_fourcc('N', 'V', '1', '2');
    mSensorFormatCount = changeSensorFormats(sensorFormats, mSensorFormats, index);
    if (mSensorFormatCount == 0) {
        ALOGE("%s no sensor format enum", __func__);
        close(fd);
        return BAD_VALUE;
    }

    mAvailableFormatCount = changeSensorFormats(availFormats, mAvailableFormats, index);

    ret = 0;
    index = 0;
    char TmpStr[20];
    int  previewCnt = 0, pictureCnt = 0;
    struct v4l2_frmsizeenum vid_frmsize;
    struct v4l2_frmivalenum vid_frmval;

    while (ret == 0) {
        memset(TmpStr, 0, 20);
        memset(&vid_frmsize, 0, sizeof(struct v4l2_frmsizeenum));
        vid_frmsize.index        = index++;
        vid_frmsize.pixel_format = v4l2_fourcc('Y', 'U', 'V', '4');
        ret = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &vid_frmsize);
        if (ret != 0) {
            continue;
        }

        ALOGV("enum frame size w:%d, h:%d",
                vid_frmsize.discrete.width, vid_frmsize.discrete.height);
        //memset(&vid_frmval, 0, sizeof(struct v4l2_frmivalenum));
        //vid_frmval.index        = 0;
        //vid_frmval.pixel_format = vid_frmsize.pixel_format;
        //vid_frmval.width        = vid_frmsize.discrete.width;
        //vid_frmval.height       = vid_frmsize.discrete.height;

        // ret = ioctl(fd, VIDIOC_ENUM_FRAMEINTERVALS, &vid_frmval);
        //if (ret != 0) {
        //    continue;
        //}
        //ALOGV("vid_frmval denominator:%d, numeraton:%d",
        //        vid_frmval.discrete.denominator,
        //        vid_frmval.discrete.numerator);
        if((vid_frmsize.discrete.width > 800) || (vid_frmsize.discrete.height > 600))
        {
            continue;
        }

        // v4l2 does not support VIDIOC_ENUM_FRAMEINTERVALS, now hard code here.
        if ((vid_frmsize.discrete.width > 1280) ||
                (vid_frmsize.discrete.height > 800)) {
            vid_frmval.discrete.denominator = 15;
            vid_frmval.discrete.numerator   = 1;
        }
        else {
            vid_frmval.discrete.denominator = 30;
            vid_frmval.discrete.numerator   = 1;
        }

        //If w/h ratio is not same with senserW/sensorH, framework assume that
        //first crop little width or little height, then scale.
        //But 1920x1080, 176x144 not work in this mode.
        //For 1M pixel, 720p sometimes may take green picture(5%), so not report it,
        //use 1024x768 for 1M pixel
        // 1920x1080 1280x720 is required by CTS.
        if(!(vid_frmsize.discrete.width == 176 && vid_frmsize.discrete.height == 144)){
            mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.width;
            mPictureResolutions[pictureCnt++] = vid_frmsize.discrete.height;
        }

        if (vid_frmval.discrete.denominator / vid_frmval.discrete.numerator > 15) {
            mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.width;
            mPreviewResolutions[previewCnt++] = vid_frmsize.discrete.height;;
        }
    } // end while

    mPreviewResolutionCount = previewCnt;
    mPictureResolutionCount = pictureCnt;

    mMinFrameDuration = 33331760L;
    mMaxFrameDuration = 30000000000L;
    int i;
    for (i=0; i<MAX_RESOLUTION_SIZE && i<pictureCnt; i+=2) {
        ALOGI("SupportedPictureSizes: %d x %d", mPictureResolutions[i], mPictureResolutions[i+1]);
    }

    adjustPreviewResolutions();
    for (i=0; i<MAX_RESOLUTION_SIZE && i<previewCnt; i+=2) {
        ALOGI("SupportedPreviewSizes: %d x %d", mPreviewResolutions[i], mPreviewResolutions[i+1]);
    }
    ALOGI("FrameDuration is %lld, %lld", mMinFrameDuration, mMaxFrameDuration);

    i = 0;
    mTargetFpsRange[i++] = 10;
    mTargetFpsRange[i++] = 30;
    mTargetFpsRange[i++] = 30;
    mTargetFpsRange[i++] = 30;

    setMaxPictureResolutions();
    ALOGI("mMaxWidth:%d, mMaxHeight:%d", mMaxWidth, mMaxHeight);

    mFocalLength = 3.37f;
    mPhysicalWidth = 3.6288f;   //2592 x 1.4u
    mPhysicalHeight = 2.7216f;  //1944 x 1.4u

    ALOGI("tvin device, mFocalLength:%f, mPhysicalWidth:%f, mPhysicalHeight %f",
        mFocalLength, mPhysicalWidth, mPhysicalHeight);

    close(fd);
    return NO_ERROR;
}

// configure device.
int32_t VADCTVINDevice::VADCTVinStream::onDeviceConfigureLocked()
{
    ALOGI("%s", __func__);
    int32_t ret = 0;
    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    int maxWait = 6;
    v4l2_std_id mSTD;
    // Get the PAL/NTSC STD
    do {
        ret = ioctl(mDev, VIDIOC_G_STD, &mSTD);
        if (ret < 0) {
            ALOGW("%s VIDIOC_G_STD failed with try %d", __func__, maxWait - 1);
            sleep(1);
        }
        maxWait --;
    } while ((ret != 0) && (maxWait > 0));

    if (mSTD == V4L2_STD_PAL)
        ALOGI("%s Get current mode: PAL", __func__);
    else if (mSTD == V4L2_STD_NTSC)
        ALOGI("%s Get current mode: NTSC", __func__);
    else {
        ALOGE("%s Error!Get invalid mode: %llu", __func__, mSTD);
        return BAD_VALUE;
    }

    int32_t fps = mFps;
    int32_t vformat;
    vformat = v4l2_fourcc('Y', 'U', 'V', '4');

    if ((mWidth > 1920) || (mHeight > 1080)) {
        fps = 15;
    }

    ALOGI("Width * Height %d x %d format %c%c%c%c, fps: %d",
            mWidth, mHeight, vformat&0xFF, (vformat>>8)&0xFF,
            (vformat>>16)&0xFF, (vformat>>24)&0xFF, fps);

    struct v4l2_streamparm param;
    memset(&param, 0, sizeof(param));
    param.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    param.parm.capture.timeperframe.numerator   = 1;
    param.parm.capture.timeperframe.denominator = fps;
    param.parm.capture.capturemode = mCamera->getCaptureMode(mWidth, mHeight);
    ret = ioctl(mDev, VIDIOC_S_PARM, &param);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_PARM Failed: %s", __func__, strerror(errno));
        return ret;
    }

    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type                 = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width        = mWidth & 0xFFFFFFF8;
    fmt.fmt.pix.height       = mHeight & 0xFFFFFFF8;
    fmt.fmt.pix.pixelformat  = vformat;
    fmt.fmt.pix.priv         = 0;
    fmt.fmt.pix.sizeimage    = 0;
    fmt.fmt.pix.bytesperline = 0;

    ret = ioctl(mDev, VIDIOC_S_FMT, &fmt);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_S_FMT Failed: %s", __func__, strerror(errno));
        return ret;
    }

    return 0;
}

int32_t VADCTVINDevice::VADCTVinStream::allocateFrameBuffersLocked()
{
    ALOGV("%s", __func__);
    if (mIonFd <= 0) {
        ALOGE("%s ion invalid", __func__);
        return BAD_VALUE;
    }

    int32_t mFrameBufferSize = getFormatSize();
    unsigned char *SXptr = NULL;
    int32_t sharedFd;
    int32_t phyAddr;
    ion_user_handle_t ionHandle;
    int32_t ionSize = mFrameBufferSize;
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        ionHandle = -1;
        int32_t err = ion_alloc(mIonFd, ionSize, 8, 1, 0, &ionHandle);
        if (err) {
            ALOGI("ion_alloc failed.");
            return BAD_VALUE;
        }

        err = ion_map(mIonFd,
                ionHandle,
                ionSize,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                0,
                &SXptr,
                &sharedFd);

        if (err) {
            ALOGI("ion_map failed.");
            ion_free(mIonFd, ionHandle);
            if (SXptr != MAP_FAILED) {
                munmap(SXptr, ionSize);
            }
            if (sharedFd > 0) {
                close(sharedFd);
            }
            goto err;
        }
        phyAddr = ion_phys(mIonFd, ionHandle);
        if (phyAddr == 0) {
            ALOGI("ion_phys failed.");
            ion_free(mIonFd, ionHandle);
            if (SXptr != MAP_FAILED) {
                munmap(SXptr, ionSize);
            }
            close(sharedFd);
            goto err;
        }
        mBuffers[i] = new StreamBuffer();
        mBuffers[i]->mVirtAddr  = SXptr;
        mBuffers[i]->mPhyAddr   = phyAddr;
        mBuffers[i]->mSize      = ionSize;
        mBuffers[i]->mBufHandle = (buffer_handle_t*)(uintptr_t)ionHandle;
        mBuffers[i]->mFd = sharedFd;
        mBuffers[i]->mStream = this;
        mBuffers[i]->mpFrameBuf  = NULL;
    }

    return 0;
err:
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        if (mBuffers[i] == NULL) {
            continue;
        }

        ion_user_handle_t ionHandle =
            (ion_user_handle_t)(uintptr_t)mBuffers[i]->mBufHandle;
        munmap(mBuffers[i]->mVirtAddr, mBuffers[i]->mSize);
        close(mBuffers[i]->mFd);
        ion_free(mIonFd, ionHandle);
        delete mBuffers[i];
        mBuffers[i] = NULL;
    }

    return BAD_VALUE;
}

int32_t VADCTVINDevice::VADCTVinStream::freeFrameBuffersLocked()
{
    ALOGV("%s", __func__);
    if (mIonFd <= 0) {
        ALOGE("%s ion invalid", __func__);
        return BAD_VALUE;
    }

    ALOGI("freeCSCBufferToIon buffer");
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        ion_user_handle_t ionHandle =
            (ion_user_handle_t)(uintptr_t)mBuffers[i]->mBufHandle;
        munmap(mBuffers[i]->mVirtAddr, mBuffers[i]->mSize);
        close(mBuffers[i]->mFd);
        ion_free(mIonFd, ionHandle);
        delete mBuffers[i];
        mBuffers[i] = NULL;
    }

    return 0;
}

int32_t VADCTVINDevice::VADCTVinStream::onDeviceStartLocked()
{
    ALOGI("%s", __func__);
    if (mDev <= 0) {
        ALOGE("%s invalid dev node", __func__);
        return BAD_VALUE;
    }

    mIonFd = ion_open();
    int32_t cscbuffer = allocateFrameBuffersLocked();

    //-------register buffers----------
    struct v4l2_buffer buf;
    struct v4l2_requestbuffers req;

    memset(&req, 0, sizeof (req));
    req.count = mNumBuffers;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    if (ioctl(mDev, VIDIOC_REQBUFS, &req) < 0) {
        ALOGE("%s VIDIOC_REQBUFS failed", __func__);
        return BAD_VALUE;
    }

    for (uint32_t i = 0; i < mNumBuffers; i++) {
        memset(&buf, 0, sizeof (buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.index = i;
        buf.memory = V4L2_MEMORY_MMAP;
        if (ioctl(mDev, VIDIOC_QUERYBUF, &buf) < 0) {
            ALOGE("%s VIDIOC_QUERYBUF error", __func__);
            return BAD_VALUE;
        }

        mV4L2Buffers[i] = new StreamBuffer();
        mV4L2Buffers[i]->mPhyAddr = buf.m.offset;
        mV4L2Buffers[i]->mSize = buf.length;
        mV4L2Buffers[i]->mVirtAddr = (void *)mmap(NULL, mV4L2Buffers[i]->mSize,
                PROT_READ | PROT_WRITE, MAP_SHARED, mDev,
                mV4L2Buffers[i]->mPhyAddr);
        mV4L2Buffers[i]->mStream = this;
        memset(mV4L2Buffers[i]->mVirtAddr, 0xFF, mV4L2Buffers[i]->mSize);
    }

    int32_t ret = 0;
    //----------qbuf----------
    struct v4l2_buffer cfilledbuffer;
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        memset(&cfilledbuffer, 0, sizeof (struct v4l2_buffer));
        cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        cfilledbuffer.memory = V4L2_MEMORY_MMAP;
        cfilledbuffer.index = i;
        cfilledbuffer.m.offset = mV4L2Buffers[i]->mPhyAddr;
        cfilledbuffer.length = mV4L2Buffers[i]->mSize;
        ALOGI("%s VIDIOC_QBUF phy:0x%x", __func__, mV4L2Buffers[i]->mPhyAddr);
        ret = ioctl(mDev, VIDIOC_QBUF, &cfilledbuffer);
        if (ret < 0) {
            ALOGE("%s VIDIOC_QBUF Failed", __func__);
            return BAD_VALUE;
        }
    }

    //-------stream on-------
    enum v4l2_buf_type bufType;
    bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(mDev, VIDIOC_STREAMON, &bufType);
    if (ret < 0) {
        ALOGE("%s VIDIOC_STREAMON failed:%s", __func__, strerror(errno));
        return ret;
    }

    return 0;
}

int32_t VADCTVINDevice::VADCTVinStream::onDeviceStopLocked()
{
    ALOGI("%s", __func__);
    int32_t ret = 0;

    if (mDev <= 0) {
        ALOGE("%s invalid fd handle", __func__);
        return BAD_VALUE;
    }

    for (uint32_t i = 0; i < MAX_STREAM_BUFFERS; i++) {
        if (mV4L2Buffers[i] != NULL && mV4L2Buffers[i]->mVirtAddr != NULL
                && mV4L2Buffers[i]->mSize > 0) {
            munmap(mV4L2Buffers[i]->mVirtAddr, mV4L2Buffers[i]->mSize);
            delete mV4L2Buffers[i];
            mV4L2Buffers[i] = NULL;
        }
    }

    enum v4l2_buf_type bufType;
    bufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(mDev, VIDIOC_STREAMOFF, &bufType);
    if (ret < 0) {
        ALOGE("%s VIDIOC_STREAMOFF failed: %s", __func__, strerror(errno));
        return ret;
    }

    ret = freeFrameBuffersLocked();
    if (ret != 0) {
        ALOGE("%s freeCSCBuffersLocked failed", __func__);
        return -1;
    }

    return 0;
}

int32_t VADCTVINDevice::VADCTVinStream::onFrameAcquireLocked()
{
    ALOGV("%s", __func__);
    int32_t ret = 0;
    struct v4l2_buffer cfilledbuffer;
    memset(&cfilledbuffer, 0, sizeof (cfilledbuffer));
    cfilledbuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    cfilledbuffer.memory = V4L2_MEMORY_MMAP;

    ret = ioctl(mDev, VIDIOC_DQBUF, &cfilledbuffer);
    if (ret < 0) {
        ALOGE("%s: VIDIOC_DQBUF Failed", __func__);
        return -1;
    }

    // Do format convert and transfer converted data to stream.
    int32_t rets = convertYUV444toNV12((u8 *)mV4L2Buffers[cfilledbuffer.index]->mVirtAddr, (u8 *)mBuffers[cfilledbuffer.index]->mVirtAddr, mWidth, mHeight);

    return cfilledbuffer.index;
}

int32_t VADCTVINDevice::VADCTVinStream::getFormatSize()
{
    int32_t size = 0;
    int alignedw, alignedh, c_stride;
    switch (mFormat) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            alignedw = ALIGN_PIXEL_16(mWidth);
            alignedh = ALIGN_PIXEL_16(mHeight);
            size = alignedw * alignedh * 3 / 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_420_P: {
            alignedw = ALIGN_PIXEL_32(mWidth);
            alignedh = ALIGN_PIXEL_4(mHeight);
            c_stride = (alignedw/2+15)/16*16;
            size = (alignedw + c_stride) * alignedh;
            break;
            }
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            alignedw = ALIGN_PIXEL_16(mWidth);
            alignedh = ALIGN_PIXEL_16(mHeight);
            size = alignedw * alignedh * 2;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            alignedw = ALIGN_PIXEL_16(mWidth);
            alignedh = ALIGN_PIXEL_16(mHeight);
            size = alignedw * alignedh * 2;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_444_888:
            alignedw = ALIGN_PIXEL_16(mWidth);
            alignedh = ALIGN_PIXEL_16(mHeight);
            size = (alignedw * alignedh * 3)/2;
            break;

        default:
            ALOGE("Error: %s format not supported", __func__);
            break;
    }

    return size;
}
