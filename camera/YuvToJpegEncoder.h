/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2012-2015 Freescale Semiconductor, Inc.
 * Copyright 2017-2018 NXP
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

#ifndef YuvToJpegEncoder_DEFINED
#define YuvToJpegEncoder_DEFINED

#include <dlfcn.h>
#include <fcntl.h>
#include <linux/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "CameraUtils.h"

extern "C" {
#include "jerror.h"
#include "jpeglib.h"
}
#include <setjmp.h>

#define DEINTERLEAVE_LINES_ONE_TIME 16

using namespace android;

class YuvToJpegEncoder {
public:
    /** Create an encoder based on the YUV format.
     */
    static YuvToJpegEncoder *create(int pixelFormat);

    YuvToJpegEncoder(int format);

    /** Encode YUV data to jpeg,  which is output to a stream.
     */
    virtual int encode(void *inYuv, void *inYuvPhy, int inSize, int inFd, buffer_handle_t inHandle,
                       int inWidth, int inHeight, int quality, void *outBuf, int outSize,
                       int outWidth, int outHeight, const void *app1Buffer, size_t app1Size);

    virtual ~YuvToJpegEncoder() {}
    int getColorFormat() { return mColorFormat; }

protected:
    int fNumPlanes;
    int color;
    int mColorFormat;
    int mPixelFormat;

    void setJpegCompressStruct(jpeg_compress_struct *cinfo, int width, int height, int quality);
    virtual void configSamplingFactors(__attribute__((unused)) jpeg_compress_struct *cinfo) {
        return;
    };
    virtual void compress(__attribute__((unused)) jpeg_compress_struct *cinfo,
                          __attribute__((unused)) uint8_t *yuv) {
        return;
    };
    virtual int yuvResize(__attribute__((unused)) uint8_t *srcBuf,
                          __attribute__((unused)) int srcWidth,
                          __attribute__((unused)) int srcHeight,
                          __attribute__((unused)) uint8_t *dstBuf,
                          __attribute__((unused)) int dstWidth,
                          __attribute__((unused)) int dstHeight);
    bool supportVpu;
};

class Yuv420SpToJpegEncoder : public YuvToJpegEncoder {
public:
    Yuv420SpToJpegEncoder(int pixelFormat);
    virtual ~Yuv420SpToJpegEncoder() {}

private:
    void configSamplingFactors(jpeg_compress_struct *cinfo);
    void deinterleaveYuv(uint8_t *yuv, int width, int height, uint8_t *&yPlanar, uint8_t *&uPlanar,
                         uint8_t *&vPlanar);
    void deinterleave(uint8_t *vuPlanar, uint8_t *uRows, uint8_t *vRows, int rowIndex, int width,
                      int height, int processLines);
    void compress(jpeg_compress_struct *cinfo, uint8_t *yuv);
    int yuvResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth,
                  int dstHeight);
};

class Yuv422IToJpegEncoder : public YuvToJpegEncoder {
public:
    Yuv422IToJpegEncoder(int pixelFormat);
    virtual ~Yuv422IToJpegEncoder() {}

private:
    void configSamplingFactors(jpeg_compress_struct *cinfo);
    void compress(jpeg_compress_struct *cinfo, uint8_t *yuv);
    void deinterleave(uint8_t *yuv, uint8_t *yRows, uint8_t *uRows, uint8_t *vRows, int rowIndex,
                      int width, int height, int processLines);
};

class Yuv422SpToJpegEncoder : public YuvToJpegEncoder {
public:
    Yuv422SpToJpegEncoder(int pixelFormat);
    virtual ~Yuv422SpToJpegEncoder() {}

private:
    void configSamplingFactors(jpeg_compress_struct *cinfo);
    void compress(jpeg_compress_struct *cinfo, uint8_t *yuv);
    void deinterleave(uint8_t *yuv, uint8_t *yRows, uint8_t *uRows, uint8_t *vRows, int rowIndex,
                      int width, int height, int processLines);
    int yuvResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth,
                  int dstHeight);
};

struct jpegBuilder_destination_mgr : jpeg_destination_mgr {
    jpegBuilder_destination_mgr(uint8_t *input, int size);

    uint8_t *buf;
    int bufsize;
    size_t jpegsize;
};

struct jpegBuilder_error_mgr : jpeg_error_mgr {
    jmp_buf fJmpBuf;
};

void jpegBuilder_error_exit(j_common_ptr cinfo);

#endif // ifndef YuvToJpegEncoder_DEFINED
