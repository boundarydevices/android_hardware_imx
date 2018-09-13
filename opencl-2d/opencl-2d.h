/*
 * Copyright 2018 NXP.
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
#ifndef __OPENCL_2D_H__
#define __OPENCL_2D_H__

#ifdef __cplusplus
extern "C"  {
#endif

#define CL_G2D_VERSION_MAJOR   0
#define CL_G2D_VERSION_MINOR   1
#define CL_G2D_VERSION_PATCH   0

enum cl_buffer_usage
{
    /*cached and non-continuous buffer*/
     CL_G2D_CPU_MEMORY       = 0,
     /* Uncached physical continuous buffer*/
     CL_G2D_DEVICE_MEMORY    = 1,
};

//rgb formats
enum cl_g2d_format
{
//rgb formats
     CL_G2D_RGB565               = 0,
     CL_G2D_RGBA8888             = 1,
     CL_G2D_RGBX8888             = 2,
     CL_G2D_BGRA8888             = 3,
     CL_G2D_BGRX8888             = 4,
     CL_G2D_BGR565               = 5,

     CL_G2D_ARGB8888             = 6,
     CL_G2D_ABGR8888             = 7,
     CL_G2D_XRGB8888             = 8,
     CL_G2D_XBGR8888             = 9,
     CL_G2D_RGB888               = 10,

//yuv formats
     CL_G2D_NV12                 = 20,
     CL_G2D_I420                 = 21,
     CL_G2D_YV12                 = 22,
     CL_G2D_NV21                 = 23,
     CL_G2D_YUYV                 = 24,
     CL_G2D_YVYU                 = 25,
     CL_G2D_UYVY                 = 26,
     CL_G2D_VYUY                 = 27,
     CL_G2D_NV16                 = 28,
     CL_G2D_NV61                 = 29,
     CL_G2D_NV12_TILED           = 30,
     CL_G2D_NV12_10BIT_TILED     = 31,
};

struct cl_g2d_buf
{
    void *buf_vaddr;
    enum cl_buffer_usage usage;
    unsigned int  buf_size;
};


struct cl_g2d_surface
{
    enum cl_g2d_format format;
    enum cl_buffer_usage usage;

    long planes[3];//surface buffer addresses are set in physical planes separately
                  //RGB:  planes[0] - RGB565/RGBA8888/RGBX8888/BGRA8888/BRGX8888
                  //NV12: planes[0] - Y, planes[1] - packed UV
                  //I420: planes[0] - Y, planes[1] - U, planes[2] - V
                  //YV12: planes[0] - Y, planes[1] - V, planes[2] - U
                  //NV21: planes[0] - Y, planes[1] - packed VU
                  //YUYV: planes[0] - packed YUYV
                  //YVYU: planes[0] - packed YVYU
                  //UYVY: planes[0] - packed UYVY
                  //VYUY: planes[0] - packed VYUY
                  //NV16: planes[0] - Y, planes[1] - packed UV
                  //NV61: planes[0] - Y, planes[1] - packed VU

    //blit rectangle in surface
    int left;
    int top;
    int right;
    int bottom;

    int stride;//surface buffer stride

    int width;//surface width
    int height;//surface height
};

int cl_g2d_open(void **handle);
int cl_g2d_close(void *handle);
int cl_g2d_copy(void *handle, struct cl_g2d_buf *output_buf,
        struct cl_g2d_buf* input_buf, unsigned int size);
int cl_g2d_blit(void *handle, struct cl_g2d_surface *src,
        struct cl_g2d_surface *dst);
int cl_g2d_flush(void *handle);
int cl_g2d_finish(void *handle);

#ifdef __cplusplus
}
#endif
#endif
