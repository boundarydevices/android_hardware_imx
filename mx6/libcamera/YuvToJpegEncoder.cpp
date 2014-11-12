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

#include "YuvToJpegEncoder.h"
#include <ui/PixelFormat.h>
#include <hardware/hardware.h>
#include "NV12_resize.h"

YuvToJpegEncoder * YuvToJpegEncoder::create(int format) {
    // Only ImageFormat.NV21 and ImageFormat.YUY2 are supported
    // for now.
    if (format == HAL_PIXEL_FORMAT_YCbCr_420_SP) {
        return new Yuv420SpToJpegEncoder();
    } else if (format == HAL_PIXEL_FORMAT_YCbCr_422_I) {
        return new Yuv422IToJpegEncoder();
    } else {
        FLOGE("YuvToJpegEncoder:create format:%d not support", format);
        return NULL;
    }
}

int YuvToJpegEncoder::getSupportedPictureFormat(int *pFormat,
                                                int  len)
{
    if ((pFormat != NULL) && (len > 0)) {
        pFormat[0] = v4l2_fourcc('N', 'V', '1', '2');
        if (len > 1) {
            pFormat[1] = v4l2_fourcc('Y', 'U', 'Y', 'V');
        }

        return NO_ERROR;
    }
    return BAD_VALUE;
}

YuvToJpegEncoder::YuvToJpegEncoder()
{}

int YuvToJpegEncoder::encode(void *inYuv,
                             int   inWidth,
                             int   inHeight,
                             int   quality,
                             void *outBuf,
                             int   outSize,
                             int   outWidth,
                             int   outHeight) {
    jpeg_compress_struct  cinfo;
    jpegBuilder_error_mgr sk_err;
    uint8_t *resize_src = NULL;
    jpegBuilder_destination_mgr dest_mgr((uint8_t *)outBuf, outSize);

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
        deinterleave(vuPlanar, uRows, vRows, cinfo->next_scanline, width);

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
                                         int      width) {
    for (int row = 0; row < 8; ++row) {
        int offset  = ((rowIndex >> 1) + row) * width;
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


//interleaved
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
	int srcStride;
	int dstStride;

	if (!dstWidth) return -1;

    if (!dstHeight) return -1;

    h_scale_ratio = srcWidth / dstWidth;
    if (!h_scale_ratio) return -1;

    v_scale_ratio = srcHeight / dstHeight;
    if (!v_scale_ratio) return -1;

    h_offset = (srcWidth - dstWidth * h_scale_ratio) / 2;
    v_offset = (srcHeight - dstHeight * v_scale_ratio) / 2;

	srcStride = srcWidth * 2;
	dstStride = dstWidth * 2;


	//for Y
	for (i = 0; i < dstHeight * v_scale_ratio; i += v_scale_ratio)
    {
        for (j = 0; j < dstStride * h_scale_ratio; j += 2 * h_scale_ratio)
        {
            ptr = srcBuf + i * srcStride + j + v_offset * srcStride + h_offset * 2;
            cc  = ptr[0];

            ptr    = dstBuf + (i / v_scale_ratio) * dstStride + (j / h_scale_ratio);
            ptr[0] = cc;
        }
    }

	//for U
	for (i = 0; i < dstHeight * v_scale_ratio; i += v_scale_ratio)
    {
        for (j = 0; j < dstStride * h_scale_ratio; j += 4 * h_scale_ratio)
        {
            ptr = srcBuf + 1 + i * srcStride + j + v_offset * srcStride + h_offset * 2;
            cc  = ptr[0];

            ptr    = dstBuf + 1 + (i / v_scale_ratio) * dstStride + (j / h_scale_ratio);
            ptr[0] = cc;
        }
    }

	//for V
	for (i = 0; i < dstHeight * v_scale_ratio; i += v_scale_ratio)
    {
        for (j = 0; j < dstStride * h_scale_ratio; j += 4 * h_scale_ratio)
        {
            ptr = srcBuf + 3 + i * srcStride + j + v_offset * srcStride + h_offset * 2;
            cc  = ptr[0];

            ptr    = dstBuf + 3 + (i / v_scale_ratio) * dstStride + (j / h_scale_ratio);
            ptr[0] = cc;
        }
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

