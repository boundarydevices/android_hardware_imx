/*
 * Copyright (C) 2015-2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2019 NXP.
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

#include <stdio.h>
#include <hardware/camera3.h>
#include <hardware/gralloc.h>
#include <system/graphics.h>
#include <graphics_ext.h>
#include <utils/Mutex.h>
#include <utils/StrongPointer.h>
#include <binder/MemoryBase.h>
#include <binder/MemoryHeapBase.h>
#include <sync/sync.h>

//#define LOG_NDEBUG 0

extern "C" {
#include "pxp_lib.h"
}

#include <cutils/log.h>
#include "Camera.h"
#include "Stream.h"
#include "CameraUtils.h"

static void YUYVCopyByLine(uint8_t *dst, uint32_t dstWidth, uint32_t dstHeight, uint8_t *src, uint32_t srcWidth, uint32_t srcHeight)
{
    uint32_t i;
    int BytesPerPixel = 2;
    uint8_t *pDstLine = dst;
    uint8_t *pSrcLine = src;
    uint32_t bytesPerSrcLine = BytesPerPixel * srcWidth;
    uint32_t bytesPerDstLine = BytesPerPixel * dstWidth;
    uint32_t marginWidh = dstWidth - srcWidth;
    uint16_t *pYUV;

    if ((srcWidth > dstWidth) || (srcHeight > dstHeight)) {
        ALOGW("%s, para error", __func__);
        return;
    }

    for (i = 0; i < srcHeight; i++) {
        memcpy(pDstLine, pSrcLine, bytesPerSrcLine);

        // black margin, Y:0, U:128, V:128
        for (uint32_t j = 0; j < marginWidh; j++) {
            pYUV = (uint16_t *)(pDstLine + bytesPerSrcLine + j * BytesPerPixel);
            *pYUV = 0x8000;
        }

        pSrcLine += bytesPerSrcLine;
        pDstLine += bytesPerDstLine;
    }

    return;
}

static void convertYUYVtoNV12SP(uint8_t *inputBuffer, uint8_t *outputBuffer, int width, int height)
{
#define u32 unsigned int
#define u8 unsigned char

    u32 h, w;
    u32 nHeight = height;
    u32 nWidthDiv4 = width / 4;

    u8 *pYSrcOffset = inputBuffer;
    u8 *pUSrcOffset = inputBuffer + 1;
    u8 *pVSrcOffset = inputBuffer + 3;

    u32 *pYDstOffset = (u32 *)outputBuffer;
    u32 *pUVDstOffset = (u32 *)(((u8 *)(outputBuffer)) + width * height);

    for (h = 0; h < nHeight; h++) {
        if (!(h & 0x1)) {
            for (w = 0; w < nWidthDiv4; w++) {
                *pYDstOffset = (((u32)(*(pYSrcOffset + 0))) << 0) +
                               (((u32)(*(pYSrcOffset + 2))) << 8) +
                               (((u32)(*(pYSrcOffset + 4))) << 16) +
                               (((u32)(*(pYSrcOffset + 6))) << 24);
                pYSrcOffset += 8;
                pYDstOffset += 1;

#ifdef PLATFORM_VERSION_4
                // seems th encoder use VUVU planner
                *pUVDstOffset = (((u32)(*(pVSrcOffset + 0))) << 0) +
                                (((u32)(*(pUSrcOffset + 0))) << 8) +
                                (((u32)(*(pVSrcOffset + 4))) << 16) +
                                (((u32)(*(pUSrcOffset + 4))) << 24);
#else
                *pUVDstOffset = (((u32)(*(pUSrcOffset + 0))) << 0) +
                                (((u32)(*(pVSrcOffset + 0))) << 8) +
                                (((u32)(*(pUSrcOffset + 4))) << 16) +
                                (((u32)(*(pVSrcOffset + 4))) << 24);
#endif
                pUSrcOffset += 8;
                pVSrcOffset += 8;
                pUVDstOffset += 1;
            }
        } else {
            pUSrcOffset += nWidthDiv4 * 8;
            pVSrcOffset += nWidthDiv4 * 8;
            for (w = 0; w < nWidthDiv4; w++) {
                *pYDstOffset = (((u32)(*(pYSrcOffset + 0))) << 0) +
                               (((u32)(*(pYSrcOffset + 2))) << 8) +
                               (((u32)(*(pYSrcOffset + 4))) << 16) +
                               (((u32)(*(pYSrcOffset + 6))) << 24);
                pYSrcOffset += 8;
                pYDstOffset += 1;
            }
        }
    }
}

Stream::Stream(int id, camera3_stream_t *s, Camera* camera)
  : mReuse(false),
    mPreview(false),
    mJpeg(false),
    mCallback(false),
    mId(id),
    mStream(s),
    mType(s->stream_type),
    mWidth(s->width),
    mHeight(s->height),
    mFormat(s->format),
    mUsage(0),
    mFps(30),
    mNumBuffers(0),
    mRegistered(false),
    mCamera(camera)
{
    if (s->format == HAL_PIXEL_FORMAT_BLOB) {
        ALOGI("%s create capture stream", __func__);
        mJpeg = true;
        mFormat = s->format;
        mUsage = CAMERA_GRALLOC_USAGE_JPEG;
        mNumBuffers = NUM_CAPTURE_BUFFER;
    }
    else if (s->format == HAL_PIXEL_FORMAT_IMPLEMENTATION_DEFINED) {
        ALOGI("%s create preview stream", __func__);
        mFormat = mCamera->getPreviewPixelFormat();
        s->format = mFormat;
        mUsage = CAMERA_GRALLOC_USAGE;
        mNumBuffers = NUM_PREVIEW_BUFFER;
        mPreview = true;
        if (s->usage & GRALLOC_USAGE_HW_VIDEO_ENCODER) {
            ALOGI("%s create video recording stream", __func__);
            s->format = HAL_PIXEL_FORMAT_YCBCR_420_888;
            mPreview = false;
        }
    }
    else {
        ALOGI("%s create callback stream", __func__);
        mCallback = true;
        mUsage = CAMERA_GRALLOC_USAGE;
        mNumBuffers = NUM_PREVIEW_BUFFER;
    }

    s->usage |= mUsage;
    s->max_buffers = mNumBuffers;
    mNumBuffers += 1;

    ALOGI("stream: w:%d, h:%d, format:0x%x, usage:0x%x, buffers:%d",
          s->width, s->height, s->format, s->usage, mNumBuffers);
    /*
     * imx6dl support IPU device and PXP device.
     * imx6q and imx6qp support IPU device.
     * imx6sx and imx6sl support PXP device.
     * IPU can't handle NV21 format, so open PXP on some platform to handle it.
     */
    mIpuFd = open("/dev/mxc_ipu", O_RDWR, 0);

    mPxpFd = open("/dev/pxp_device", O_RDWR, 0);

    //When open pxp device, need allocate a channel at the same time.
    int32_t ret = -1;
    if (mPxpFd > 0) {
        ret = ioctl(mPxpFd, PXP_IOC_GET_CHAN, &channel);
        if (ret < 0) {
            ALOGE("%s:%d, PXP_IOC_GET_CHAN failed %d", __FUNCTION__, __LINE__ ,ret);
        }
    }

    for (uint32_t i=0; i<MAX_STREAM_BUFFERS; i++) {
        mBuffers[i] = NULL;
    }

    mJpegBuilder = new JpegBuilder();
}

Stream::Stream(Camera* camera)
  : mReuse(false),
    mPreview(false),
    mJpeg(false),
    mId(-1),
    mStream(NULL),
    mType(-1),
    mWidth(0),
    mHeight(0),
    mFormat(0),
    mUsage(0),
    mFps(30),
    mNumBuffers(0),
    mRegistered(false),
    mCamera(camera)
{
    mIpuFd = open("/dev/mxc_ipu", O_RDWR, 0);

    mPxpFd = open("/dev/pxp_device", O_RDWR, 0);

    //When open pxp device, need allocate a channel at the same time.
    int32_t ret = -1;
    if (mPxpFd > 0) {
        ret = ioctl(mPxpFd, PXP_IOC_GET_CHAN, &channel);
        if(ret < 0) {
            ALOGE("%s:%d, PXP_IOC_GET_CHAN failed %d", __FUNCTION__, __LINE__ ,ret);
        }
    }

    for (uint32_t i=0; i<MAX_STREAM_BUFFERS; i++) {
        mBuffers[i] = NULL;
    }
}

Stream::~Stream()
{
    android::Mutex::Autolock al(mLock);
    if (mIpuFd > 0) {
        close(mIpuFd);
        mIpuFd = -1;
    }

    if (mPxpFd > 0) {
        close(mPxpFd);
        mPxpFd = -1;
    }
}

int32_t Stream::getJpegBufferSize(StreamBuffer &src,
                                  sp<Metadata> meta)
{
    const ssize_t kMinJpegBufferSize = 256 * 1024 + sizeof(camera3_jpeg_blob);
    sp<Stream> &srcStream = src.mStream;
    int maxJpegResWidth, maxJpegResHeight;
    uint32_t v4l2Width = 0, v4l2Height = 0;
    ssize_t maxJpegBufferSize = 0;
    int ret;

    ret = mCamera->getV4l2Res(srcStream->mWidth, srcStream->mHeight, &v4l2Width, &v4l2Height);
    if (ret) {
        ALOGE("%s getV4l2Res failed, ret %d", __func__, ret);
        return BAD_VALUE;
    }

    maxJpegResWidth = mCamera->mMaxWidth;
    maxJpegResHeight = mCamera->mMaxHeight;

    // Get max jpeg buffer size
    maxJpegBufferSize = mCamera->mMaxJpegSize;
    assert(kMinJpegBufferSize < maxJpegBufferSize);

    // Calculate final jpeg buffer size for the given resolution.
    float scaleFactor = ((float) (v4l2Width * v4l2Height)) /
            (maxJpegResWidth * maxJpegResHeight);

    ssize_t jpegBufferSize = scaleFactor * (maxJpegBufferSize - kMinJpegBufferSize) +
            kMinJpegBufferSize;

    if (jpegBufferSize > maxJpegBufferSize) {
        jpegBufferSize = maxJpegBufferSize;
    }

    return jpegBufferSize;
}

int32_t Stream::processJpegBuffer(StreamBuffer& src,
                                  sp<Metadata> meta)
{
    int32_t ret = 0;
    int32_t encodeQuality = 100, thumbQuality = 100;
    int32_t thumbWidth, thumbHeight;
    JpegParams *mainJpeg = NULL, *thumbJpeg = NULL;
    void *rawBuf = NULL, *thumbBuf = NULL;
    uint32_t v4l2Width = 0, v4l2Height = 0;
    uint8_t *pDst = NULL;
    struct camera3_jpeg_blob *jpegBlob = NULL;
    uint32_t bufSize = 0;
    int32_t jpegBufferSize;

    StreamBuffer* dstBuf = mCurrent;
    sp<Stream>& srcStream = src.mStream;

    ret = mCamera->getV4l2Res(srcStream->mWidth, srcStream->mHeight, &v4l2Width, &v4l2Height);
    if (ret) {
        ALOGE("%s getV4l2Res failed, ret %d", __func__, ret);
        return BAD_VALUE;
    }

    ALOGI("%s srcStream->mWidth:%d, srcStream->mHeight:%d, v4l2Width:%d, v4l2Height:%d", __func__,
        srcStream->mWidth,
        srcStream->mHeight,
        v4l2Width,
        v4l2Height);
    // just set to actual v4l2 res
    srcStream->mWidth = v4l2Width;
    srcStream->mHeight = v4l2Height;

    if (dstBuf == NULL || srcStream == NULL) {
        ALOGE("%s invalid param", __FUNCTION__);
        return BAD_VALUE;
    }

    sp<Stream>& capture = dstBuf->mStream;

    ret = meta->getJpegQuality(encodeQuality);
    if (ret != NO_ERROR) {
        ALOGE("%s getJpegQuality failed", __FUNCTION__);
        return BAD_VALUE;
    }

    if ((encodeQuality < 0) || (encodeQuality > 100)) {
        encodeQuality = 100;
    }

    ret = meta->getJpegThumbQuality(thumbQuality);
    if (ret != NO_ERROR) {
        ALOGE("%s getJpegThumbQuality failed", __FUNCTION__);
        return BAD_VALUE;
    }

    if ((thumbQuality < 0) || (thumbQuality > 100)) {
        thumbQuality = 100;
    }

    int captureSize = 0;
    int alignedw, alignedh, c_stride;
    switch (srcStream->format()) {
        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            alignedw = ALIGN_PIXEL_32(capture->mWidth);
            alignedh = ALIGN_PIXEL_4(capture->mHeight);
            c_stride = (alignedw/2+15)/16*16;
            captureSize = alignedw * alignedh + c_stride * alignedh;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            alignedw = ALIGN_PIXEL_16(capture->mWidth);
            alignedh = ALIGN_PIXEL_16(capture->mHeight);
            captureSize = alignedw * alignedh * 3 / 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            alignedw = ALIGN_PIXEL_16(capture->mWidth);
            alignedh = ALIGN_PIXEL_16(capture->mHeight);
            captureSize = alignedw * alignedh * 2;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            alignedw = ALIGN_PIXEL_16(capture->mWidth);
            alignedh = ALIGN_PIXEL_16(capture->mHeight);
            captureSize = alignedw * alignedh * 2;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_444_888:
            alignedw = ALIGN_PIXEL_16(capture->mWidth);
            alignedh = ALIGN_PIXEL_16(capture->mHeight);
            captureSize = alignedw * alignedh * 3;
            break;

        default:
            ALOGE("Error: %s format not supported", __FUNCTION__);
    }

    sp<MemoryHeapBase> rawFrame(
        new MemoryHeapBase(captureSize, 0, "rawFrame"));
    rawBuf = rawFrame->getBase();
    if (rawBuf == MAP_FAILED) {
        ALOGE("%s new MemoryHeapBase failed", __FUNCTION__);
        return BAD_VALUE;
    }

    sp<MemoryHeapBase> thumbFrame(
        new MemoryHeapBase(captureSize, 0, "thumbFrame"));
    thumbBuf = thumbFrame->getBase();
    if (thumbBuf == MAP_FAILED) {
        ALOGE("%s new MemoryHeapBase failed", __FUNCTION__);
        return BAD_VALUE;
    }

    mainJpeg = new JpegParams((uint8_t *)src.mVirtAddr,
                              (uint8_t *)(uintptr_t)src.mPhyAddr,
                              src.mSize,
                              (uint8_t *)rawBuf,
                              captureSize,
                              encodeQuality,
                              srcStream->mWidth,
                              srcStream->mHeight,
                              capture->mWidth,
                              capture->mHeight,
                              srcStream->format());

    ret = meta->getJpegThumbSize(thumbWidth, thumbHeight);
    if (ret != NO_ERROR) {
        ALOGE("%s getJpegThumbSize failed", __FUNCTION__);
        goto err_out;
    }

    if ((thumbWidth > 0) && (thumbHeight > 0)) {
        int thumbSize = captureSize;
        thumbJpeg = new JpegParams((uint8_t *)src.mVirtAddr,
                           (uint8_t *)(uintptr_t)src.mPhyAddr,
                           src.mSize,
                           (uint8_t *)thumbBuf,
                           thumbSize,
                           thumbQuality,
                           srcStream->mWidth,
                           srcStream->mHeight,
                           thumbWidth,
                           thumbHeight,
                           srcStream->format());
    }

    mJpegBuilder->prepareImage(&src);
    ret = mJpegBuilder->encodeImage(mainJpeg, thumbJpeg);
    if (ret != NO_ERROR) {
        ALOGE("%s encodeImage failed", __FUNCTION__);
        goto err_out;
    }

    ret = mJpegBuilder->buildImage(dstBuf);
    if (ret != NO_ERROR) {
        ALOGE("%s buildImage failed", __FUNCTION__);
        goto err_out;
    }

    // write jpeg size
    pDst = (uint8_t *)dstBuf->mVirtAddr;
    jpegBufferSize = getJpegBufferSize(src, meta);
    bufSize = (mCamera->mMaxJpegSize <= jpegBufferSize) ? mCamera->mMaxJpegSize : jpegBufferSize;

    jpegBlob = (struct camera3_jpeg_blob *)(pDst + bufSize -
                                            sizeof(struct camera3_jpeg_blob));
    jpegBlob->jpeg_blob_id = CAMERA3_JPEG_BLOB_ID;
    jpegBlob->jpeg_size = mJpegBuilder->getImageSize();

    ALOGI("%s, dstbuf size %d, jpeg_size %d, max jpeg size %d",
           __func__, dstBuf->mSize, jpegBlob->jpeg_size, mCamera->mMaxJpegSize);

err_out:
    if (mainJpeg) {
        delete mainJpeg;
    }

    if (thumbJpeg) {
        delete thumbJpeg;
    }

    return ret;
}

int32_t Stream::processBufferWithPXP(StreamBuffer& src)
{
    ALOGV("%s", __func__);
    sp<Stream>& device = src.mStream;
    if (device == NULL) {
        ALOGE("%s invalid device stream", __func__);
        return 0;
    }

    StreamBuffer* out = mCurrent;
    if (out == NULL || out->mBufHandle == NULL) {
        ALOGE("%s invalid buffer handle", __func__);
        return 0;
    }

    struct pxp_config_data pxp_conf;
    struct pxp_layer_param *src_param = NULL, *out_param = NULL;
    int32_t ret = -1;

    memset(&pxp_conf, 0, sizeof(struct pxp_config_data));

    src_param = &(pxp_conf.s0_param);
    out_param = &(pxp_conf.out_param);

    /*
    * Initialize src parameters
    */
    src_param->paddr = src.mPhyAddr;
    src_param->width = device->mWidth;
    src_param->height = device->mHeight;
    src_param->color_key = -1;
    src_param->color_key_enable = 0;
    src_param->pixel_fmt = convertPixelFormatToV4L2Format(device->mFormat);
    pxp_conf.proc_data.srect.top = 0;
    pxp_conf.proc_data.srect.left = 0;
    pxp_conf.proc_data.srect.width = device->mWidth;
    pxp_conf.proc_data.srect.height = device->mHeight;

    /*
    * Initialize out parameters
    */
    out_param->paddr = out->mPhyAddr;
    out_param->width = mWidth;
    out_param->height = mHeight;
    out_param->stride = mWidth;
    out_param->pixel_fmt = convertPixelFormatToV4L2Format(mFormat);
    pxp_conf.handle = channel;
    pxp_conf.proc_data.drect.top = 0;
    pxp_conf.proc_data.drect.left = 0;
    pxp_conf.proc_data.drect.width = mWidth;
    pxp_conf.proc_data.drect.height = mHeight;

    ret = ioctl(mPxpFd, PXP_IOC_CONFIG_CHAN, &pxp_conf);
    if(ret < 0) {
        ALOGE("%s:%d, PXP_IOC_CONFIG_CHAN failed %d", __FUNCTION__, __LINE__ ,ret);
        return ret;
    }

    ret = ioctl(mPxpFd, PXP_IOC_START_CHAN, &(pxp_conf.handle));
    if(ret < 0) {
        ALOGE("%s:%d, PXP_IOC_START_CHAN failed %d", __FUNCTION__, __LINE__ ,ret);
        return ret;
    }

    ret = ioctl(mPxpFd, PXP_IOC_WAIT4CMPLT, &pxp_conf);
    if(ret < 0) {
        ALOGE("%s:%d, PXP_IOC_WAIT4CMPLT failed %d", __FUNCTION__, __LINE__ ,ret);
        return ret;
    }

    return ret;

}

int32_t Stream::processBufferWithIPU(StreamBuffer& src)
{
    ALOGV("%s", __func__);
    sp<Stream>& device = src.mStream;
    if (device == NULL) {
        ALOGE("%s invalid device stream", __func__);
        return 0;
    }

    StreamBuffer* out = mCurrent;
    if (out == NULL || out->mBufHandle == NULL) {
        ALOGE("%s invalid buffer handle", __func__);
        return 0;
    }

    struct ipu_task mTask;
    memset(&mTask, 0, sizeof(mTask));

    mTask.input.width = device->mWidth;
    mTask.input.height = device->mHeight;
    mTask.input.crop.pos.x = 0;
    mTask.input.crop.pos.y = 0;
    mTask.input.crop.w = device->mWidth;
    mTask.input.crop.h = device->mHeight;
    mTask.input.format = convertPixelFormatToV4L2Format(device->mFormat);
    mTask.input.paddr = src.mPhyAddr;

    if (!mCallback) {
        mTask.output.format = convertPixelFormatToV4L2Format(mFormat);
    }
    else {
        mTask.output.format = convertPixelFormatToV4L2Format(mFormat, true);
    }
    mTask.output.width = mWidth;
    mTask.output.height = mHeight;
    mTask.output.crop.pos.x = 0;
    mTask.output.crop.pos.y = 0;
    mTask.output.crop.w = mWidth;
    mTask.output.crop.h = mHeight;
    mTask.output.rotate = 0;
    mTask.output.paddr = out->mPhyAddr;

    // If after convert, src/dst has same format and resolution, then process with GPU.
    // For exmaple, HAL_PIXEL_FORMAT_YCBCR_420_888, HAL_PIXEL_FORMAT_YCbCr_420_SP
    // both convert to v4l2_fourcc('N', 'V', '1', '2').
    if( (mTask.output.format == mTask.input.format) &&
        (mTask.output.width == mTask.input.width) &&
        (mTask.output.height == mTask.input.height) ) {
        return processBufferWithGPU(src);
    }

    int32_t ret = IPU_CHECK_ERR_INPUT_CROP;
    while(ret != IPU_CHECK_OK && ret > IPU_CHECK_ERR_MIN) {
        ret = ioctl(mIpuFd, IPU_CHECK_TASK, &mTask);
        ALOGV("%s:%d, IPU_CHECK_TASK ret=%d", __FUNCTION__, __LINE__, ret);
        switch(ret) {
            case IPU_CHECK_OK:
                break;
            case IPU_CHECK_ERR_SPLIT_INPUTW_OVER:
                mTask.input.crop.w -= 8;
                break;
            case IPU_CHECK_ERR_SPLIT_INPUTH_OVER:
                mTask.input.crop.h -= 8;
                break;
            case IPU_CHECK_ERR_SPLIT_OUTPUTW_OVER:
                mTask.output.crop.w -= 8;
                break;
            case IPU_CHECK_ERR_SPLIT_OUTPUTH_OVER:
                mTask.output.crop.h -= 8;;
                break;
            default:
                ALOGE("%s:%d, IPU_CHECK_TASK ret=%d", __FUNCTION__, __LINE__, ret);
                return ret;
        }
    }

    ret = ioctl(mIpuFd, IPU_QUEUE_TASK, &mTask);
    if(ret < 0) {
        ALOGE("%s:%d, IPU_QUEUE_TASK failed %d", __FUNCTION__, __LINE__ ,ret);
        return ret;
    }

    return ret;
}

static void bufferDump(StreamBuffer *frame, bool in)
{
    // for test code
    char value[100];
    char name[100];
    memset(value, 0, sizeof(value));
    bool vflg = false;
    static int dump_num = 1;
    property_get("rw.camera.test", value, "");
    if (strcmp(value, "true") == 0)
        vflg = true;

    if (vflg) {
        FILE *pf = NULL;
        memset(name, 0, sizeof(name));
        snprintf(name, 100, "/data/dump/camera_dump_%s_%d.data",
                   in ? "in" : "out", dump_num++);
        pf = fopen(name, "wb");
        if (pf == NULL) {
            ALOGI("open %s failed", name);
        }
        else {
            ALOGV("write yuv data");
            fwrite(frame->mVirtAddr, frame->mSize, 1, pf);
            fclose(pf);
        }
    }
}

int32_t Stream::processBufferWithGPU(StreamBuffer& src)
{
    sp<Stream>& device = src.mStream;
    if (device == NULL) {
        ALOGE("%s invalid device stream", __func__);
        return 0;
    }

    StreamBuffer* out = mCurrent;
    if (out == NULL || out->mBufHandle == NULL) {
        ALOGE("%s invalid buffer handle", __func__);
        return 0;
    }

    void* g2dHandle = device->getG2dHandle();
    int size = (src.mSize > out->mSize) ? out->mSize : src.mSize;

    if (g2dHandle == NULL) {
        ALOGV("%s if board don't support g2d_copy, use memcpy", __func__);
        memcpy(out->mVirtAddr, src.mVirtAddr, size);
        return 0;
    }

#ifdef TARGET_FSL_IMX_2D
    struct g2d_buf s_buf, d_buf;
    s_buf.buf_paddr = src.mPhyAddr;
    s_buf.buf_vaddr = src.mVirtAddr;
    d_buf.buf_paddr = out->mPhyAddr;
    d_buf.buf_vaddr = out->mVirtAddr;
    g2d_copy(g2dHandle, &d_buf, &s_buf, size);
    g2d_finish(g2dHandle);
#endif

    bufferDump(&src, true);
    bufferDump(out, false);

    return 0;
}

int32_t Stream::convertNV12toNV21(StreamBuffer& src)
{
    sp<Stream>& device = src.mStream;
    if (device == NULL) {
        ALOGE("%s invalid device stream", __func__);
        return 0;
    }

    StreamBuffer* out = mCurrent;
    if (out == NULL || out->mBufHandle == NULL) {
        ALOGE("%s invalid buffer handle", __func__);
        return 0;
    }
    int Ysize = 0, UVsize = 0;
    uint8_t *srcIn, *dstOut;
    uint32_t *UVout;
    int size = (src.mSize > out->mSize) ? out->mSize : src.mSize;

    Ysize  = device->mWidth * device->mHeight;
    UVsize = device->mWidth * device->mHeight >> 2;
    srcIn = (uint8_t *)src.mVirtAddr;
    dstOut = (uint8_t *)out->mVirtAddr;
    UVout = (uint32_t *)(dstOut + Ysize);

    void* g2dHandle = device->getG2dHandle();

    if (g2dHandle != NULL) {
#ifdef TARGET_FSL_IMX_2D
        struct g2d_buf s_buf, d_buf;
        s_buf.buf_paddr = src.mPhyAddr;
        s_buf.buf_vaddr = src.mVirtAddr;
        d_buf.buf_paddr = out->mPhyAddr;
        d_buf.buf_vaddr = out->mVirtAddr;
        g2d_copy(g2dHandle, &d_buf, &s_buf, size);
        g2d_finish(g2dHandle);
#endif
    }
    else {
        memcpy(dstOut, srcIn, size);
    }

    for (int k = 0; k < UVsize/2; k++) {
        __asm volatile ("rev16 %0, %0" : "+r"(*UVout));
        UVout += 1;
    }

    return 0;
}

int32_t Stream::processBufferWithCPU(StreamBuffer &src)
{
    int ret;
    uint32_t v4l2Width;
    uint32_t v4l2Height;

    sp<Stream> &device = src.mStream;
    if (device == NULL) {
        ALOGE("%s invalid device stream", __func__);
        return 0;
    }

    StreamBuffer *out = mCurrent;
    if (out == NULL || out->mBufHandle == NULL) {
        ALOGE("%s invalid buffer handle", __func__);
        return 0;
    }

    ret = mCamera->getV4l2Res(mWidth, mHeight, &v4l2Width, &v4l2Height);
    if (ret) {
        ALOGE("%s getV4l2Res failed, ret %d", __func__, ret);
        return 0;
    }

    ALOGV("res, stream %dx%d, v4l2 %dx%d", mWidth, mHeight, v4l2Width, v4l2Height);

    if ((device->mWidth != mWidth) || (device->mHeight != mHeight)) {
        ALOGE("%s:%d, Software don't support resize, device->mFormat:0x%d, mFormat:0x%d", __FUNCTION__, __LINE__, device->mFormat, mFormat);
        return 0;
    }

    if ((mFormat == HAL_PIXEL_FORMAT_YCbCr_420_888) &&
        (device->mFormat == HAL_PIXEL_FORMAT_YCbCr_422_I)) {

        uint8_t *pTmpBuf = (uint8_t *)src.mVirtAddr;
        if ((v4l2Width != mWidth) || (v4l2Height != mHeight)) {
            pTmpBuf = mCamera->getTmpBuf();
            if (pTmpBuf == NULL) {
                ALOGE("this %p, %s pTmpBuf null", this, __func__);
                return 0;
            }
            YUYVCopyByLine(pTmpBuf, mWidth, mHeight, (uint8_t *)src.mVirtAddr, v4l2Width, v4l2Height);
        }
        convertYUYVtoNV12SP(pTmpBuf, (uint8_t *)out->mVirtAddr, mWidth, mHeight);

    } else if ((mFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP) &&
               (device->mFormat == HAL_PIXEL_FORMAT_YCbCr_422_I)) {
        convertYUYVtoNV12SP((uint8_t *)src.mVirtAddr, (uint8_t *)out->mVirtAddr, mWidth, mHeight);
    } else if ((device->mFormat == HAL_PIXEL_FORMAT_YCbCr_420_SP) &&
               (mFormat == HAL_PIXEL_FORMAT_YCrCb_420_SP)) {
        ret = convertNV12toNV21(src);
    } else if (device->mFormat == mFormat) {
        YUYVCopyByLine((uint8_t *)out->mVirtAddr, mWidth, mHeight, (uint8_t *)src.mVirtAddr, v4l2Width, v4l2Height);
    } else {
        ALOGE("%s:%d, Software don't support format convert from 0x%x to 0x%x", __FUNCTION__, __LINE__, device->mFormat, mFormat);
        return 0;
    }

    return 0;
}

int32_t Stream::processFrameBuffer(StreamBuffer& src,
                                   sp<Metadata> meta __unused)
{
    ALOGV("%s", __func__);
    sp<Stream>& device = src.mStream;
    if (device == NULL) {
        ALOGE("%s invalid device stream", __func__);
        return 0;
    }

    int32_t ret = 0;
    uint32_t v4l2Width;
    uint32_t v4l2Height;

    ret = mCamera->getV4l2Res(device->mWidth, device->mHeight, &v4l2Width, &v4l2Height);
    if (ret) {
        ALOGE("%s getV4l2Res failed, ret %d", __func__, ret);
    }

    if ((mWidth != v4l2Width) || (mHeight != v4l2Height) ||
            (mFormat != device->mFormat)) {
        if ((mIpuFd > 0) && (mFormat != HAL_PIXEL_FORMAT_YCrCb_420_SP)) {
            ret = processBufferWithIPU(src);
        } else if (mPxpFd > 0){
            ret = processBufferWithPXP(src);
        } else {
            ret = processBufferWithCPU(src);
        }
    } else {
        ret = processBufferWithGPU(src);
    }

    return ret;
}

int32_t Stream::processCaptureBuffer(StreamBuffer& src,
        sp<Metadata> meta)
{
    int32_t res = 0;

    ALOGV("%s", __func__);
    StreamBuffer* out = mCurrent;
    if (out == NULL || out->mBufHandle == NULL) {
        ALOGE("%s invalid buffer handle", __func__);
        return 0;
    }

    if (out->mAcquireFence != -1) {
        res = sync_wait(out->mAcquireFence, CAMERA_SYNC_TIMEOUT);
        if (res == -ETIME) {
            ALOGE("%s: Timeout waiting on buffer acquire fence",
                    __func__);
            return res;
        } else if (res) {
            ALOGE("%s: Error waiting on buffer acquire fence: %s(%d)",
                    __func__, strerror(-res), res);
            ALOGV("fence id:%d", out->mAcquireFence);
        }
        close(out->mAcquireFence);
    }

    if (mJpeg) {
        mJpegBuilder->reset();
        mJpegBuilder->setMetadata(meta);

        res = processJpegBuffer(src, meta);
        mJpegBuilder->setMetadata(NULL);
    }
    else {
        res = processFrameBuffer(src, meta);
    }

    return res;
}

int Stream::getType()
{
    return mType;
}

bool Stream::isInputType()
{
    return mType == CAMERA3_STREAM_INPUT ||
        mType == CAMERA3_STREAM_BIDIRECTIONAL;
}

bool Stream::isOutputType()
{
    return mType == CAMERA3_STREAM_OUTPUT ||
        mType == CAMERA3_STREAM_BIDIRECTIONAL;
}

bool Stream::isRegistered()
{
    return mRegistered;
}

bool Stream::isValidReuseStream(int id, camera3_stream_t *s)
{
    if (id != mId) {
        ALOGE("%s:%d: Invalid camera id for reuse. Got %d expect %d",
                __func__, mId, id, mId);
        return false;
    }

    if (s != mStream || s->stream_type != mType) {
        ALOGE("%s:%d: Invalid stream handle for reuse. Got %p expect %p",
                __func__, mId, s, mStream);
        return false;
    }

    if (s->width != mWidth || s->height != mHeight || s->format != mFormat) {
        ALOGE("%s:%d: Mismatched reused stream."
              "Got w:%d, h:%d, f:%d expect w:%d, h:%d, f:%d",
                __func__, mId, s->width, s->height, s->format,
                mWidth, mHeight, mFormat);
        return false;
    }

    ALOGV("%s:%d: Mismatched reused stream. usage got:0x%x expect:0x%x",
            __func__, mId, s->usage, mUsage);
    s->usage |= mUsage;
    //max_buffers is mNumBuffers-1 which is set in Stream constructor.
    s->max_buffers = mNumBuffers -1;

    return true;
}

void Stream::dump(int fd)
{
    android::Mutex::Autolock al(mLock);

    dprintf(fd, "Stream ID: %d (%p)\n", mId, mStream);
    dprintf(fd, "Stream Type: (%d)\n", mType);
    dprintf(fd, "Width: %d Height: %d\n", mWidth, mHeight);
    dprintf(fd, "Stream Format: (%d)", mFormat);
    // ToDo: prettyprint usage mask flags
    dprintf(fd, "Gralloc Usage Mask: %d\n", mUsage);
    dprintf(fd, "Buffers Registered: %s\n", mRegistered ? "true" : "false");
    dprintf(fd, "Number of Buffers: %d\n", mNumBuffers);
    for (uint32_t i = 0; i < mNumBuffers; i++) {
        if(mBuffers[i] == NULL)
            continue;
        dprintf(fd, "Buffer %d %d : %p\n", i, mNumBuffers,
                mBuffers[i]->mBufHandle);
    }
}

