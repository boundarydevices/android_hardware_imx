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
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <string.h>
#include <ion/ion.h>
#include <utils/Timers.h>
#include <linux/version.h>
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
#include <ion_4.12.h>
#endif
#include <linux/ion.h>
#include <libyuv.h>

#include <VX/vx.h>
#include <VX/vx_api.h>
#include <VX/vxu.h>

#include <CL/opencl.h>
#ifdef BUILD_FOR_ANDROID
#include <cutils/log.h>
#endif

#include "opencl-2d.h"
#include "Allocator.h"

#define LOG_TAG "opencl-2d-test"
#define DEBUG 1
#define MAX_FILE_LEN 128
#define G2D_TEST_LOOP 10

static char input_file[MAX_FILE_LEN];
static char output_file[MAX_FILE_LEN];
static char output_vx_file[MAX_FILE_LEN];
static char output_benchmark_file[MAX_FILE_LEN];
static enum cl_g2d_format gInput_format = CL_G2D_YUYV;
static enum cl_g2d_format gOutput_format = CL_G2D_YUYV;
static int gWidth = 0;
static int gHeight = 0;
static int gStride = 0;
static int gOutWidth = 0;
static int gOutHeight = 0;
static int gOutStride = 0;
static int gMemory_type = 0;
static bool gMemTest = false;
static bool gCLBuildTest = false;
static int gCopyLen = 0;
static int gIonFd;

static int get_buf_size(enum cl_g2d_format format, int width, int height, bool copyTest, int copyLen)
{
    if(!copyTest) {
        switch(format) {
        case CL_G2D_YUYV:
            return width*height*2;
        case CL_G2D_NV12:
        case CL_G2D_NV21:
        case CL_G2D_I420:
            return width*height*3/2;
        default:
            ALOGE("unsupported format\n");
        }
    } else {
        return copyLen;
    }
    return 0;

}

static int get_file_len(const char *filename)
{
    int fd = 0;
    int filesize = 0;
    fd = open(filename, O_RDWR, 0666);
    if (fd<0) {
        ALOGE("Unable to open file [%s]\n",
             filename);
        return -1;
    }
    filesize = lseek(fd, 0, SEEK_END);
    close(fd);
    return filesize;

}

static int read_from_file(char *buf, int count, const char *filename)
{
    int fd = 0;
    int len = 0;
    fd = open(filename, O_RDWR, O_RDONLY);
    if (fd<0) {
        ALOGE("Unable to open file [%s]\n",
             filename);
        return -1;
    }
    len = read(fd, buf, count);
    close(fd);
    return len;
}

static int write_from_file(char *buf, int count, const char *filename)
{
    int fd = 0;
    int len = 0;
    fd = open(filename, O_CREAT | O_RDWR, 0666);
    if (fd<0) {
        ALOGE("Unable to open file [%s]\n",
             filename);
        return -1;
    }
    len = write(fd, buf, count);
    close(fd);
    return len;
}

#ifdef DEBUG
static void dump_buffer(char *pbuf, int count, const char *title)
{
    int i = 0,j = 0;
    char *buf = pbuf;
    char printbuf[256];
    memset(printbuf, 0, 256);

    if((pbuf == NULL)||(title == NULL))
        return;

    ALOGI("Dump buffer %s, count 0x%x\n", title, count);
    for(i = 0; i < count; i += 16) {
       int pcount = count - i;
       if (pcount >= 16)
            ALOGI("0x%x: %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n",
                i, *(buf +0), *(buf +1), *(buf +2), *(buf +3),
                *(buf +4), *(buf +5), *(buf +6), *(buf +7),
                *(buf +8), *(buf +9), *(buf +10), *(buf +11),
                *(buf +12), *(buf +13), *(buf +14), *(buf +15));
       else {
           //ALOGI("0x%x: ", i);
           sprintf(printbuf, "0x%x: ", i);
           for(j = 0; j < pcount; j++) {
               //ALOGI("\b\b %x ", *(buf + j));
               sprintf(printbuf + strlen(printbuf), "0x%x ", *(buf + j));
           }
           ALOGI("%s", printbuf);
       }
       buf += 16;
    }
}
#else
#define dump_buffer(x, y, z)
#endif

static int g2d_get_planecount(unsigned int format)
{
    switch(format) {
        case CL_G2D_RGB565:
        case CL_G2D_BGRX8888:
        case CL_G2D_BGRA8888:
        case CL_G2D_RGBA8888:
        case CL_G2D_RGBX8888:
        case CL_G2D_ARGB8888:
        case CL_G2D_XRGB8888:
        case CL_G2D_ABGR8888:
        case CL_G2D_XBGR8888:
        case CL_G2D_UYVY:
        case CL_G2D_YUYV:
        case CL_G2D_VYUY:
        case CL_G2D_YVYU:
            return 1;
        /* for the multi-plane format,
         * only return the bits number
         * for Y plane
         */
        case CL_G2D_NV12:
        case CL_G2D_NV21:
        case CL_G2D_NV12_10BIT_TILED:
        case CL_G2D_NV12_TILED:
            return 2;
        case CL_G2D_YV12:
        case CL_G2D_I420:
            return 3;
        default:
           ALOGE("%s: unsupported format for getting plane count\n", __func__);
        }
        return 0;
}

static int g2d_get_planebpp(unsigned int format, int plane)
{
    if(plane >= g2d_get_planecount(format))
        return 0;
    switch(format) {
        case CL_G2D_RGB565:
            return 16;
        case CL_G2D_BGRX8888:
        case CL_G2D_BGRA8888:
        case CL_G2D_RGBA8888:
        case CL_G2D_RGBX8888:
        case CL_G2D_ARGB8888:
        case CL_G2D_XRGB8888:
        case CL_G2D_ABGR8888:
        case CL_G2D_XBGR8888:
            return 32;
        case CL_G2D_UYVY:
        case CL_G2D_YUYV:
        case CL_G2D_VYUY:
        case CL_G2D_YVYU:
            return 16;
        /* for the multi-plane format,
         * only return the bits number
         * for Y plane
         */
        case CL_G2D_NV12:
        case CL_G2D_NV21:
        case CL_G2D_NV12_TILED:
            if(plane == 0)
               return 8;
            else
               return 4;
        case CL_G2D_NV12_10BIT_TILED:
            if(plane == 0)
               return 10;
            else
               return 5;

        case CL_G2D_YV12:
        case CL_G2D_I420:
            if(plane == 0)
               return 8;
            else
               return 2;

        default:
           ALOGE("%s: unsupported format for getting bpp\n", __func__);
        }
        return 0;
}

static int g2d_get_planesize(enum cl_g2d_format format, int w_stride, int h_stride, int plane)
{
    if(plane >= g2d_get_planecount(format))
        return 0;

    int bpp = g2d_get_planebpp(format, plane);

    if (format == CL_G2D_NV12_10BIT_TILED) {
        return (plane == 0) ? w_stride * h_stride :
                w_stride * h_stride / 2;
    }

    return w_stride * h_stride * bpp / 8;
}


static vx_df_image imagetype_to_openvx(enum cl_g2d_format format)
{
    switch(format) {
    case CL_G2D_YUYV:
        return VX_DF_IMAGE_YUYV;
        break;
    case CL_G2D_NV12:
        return VX_DF_IMAGE_NV12;
        break;
    case CL_G2D_NV21:
        return VX_DF_IMAGE_NV21;
        break;
    default:
        break;
    }
    return -1;
}

static bool imagepatch_to_openvx(enum cl_g2d_format format, int w_stride,
        int height, vx_imagepatch_addressing_t *frameFormats)
{
    if(frameFormats == NULL)
        return false;

    memset(frameFormats, 0, sizeof(frameFormats));
    switch(format) {
    case CL_G2D_YUYV:
        frameFormats[0].dim_x = gStride;
        frameFormats[0].dim_y = gHeight;
        frameFormats[0].stride_x = 2;
        frameFormats[0].stride_y = gStride * 2;
        break;
    case CL_G2D_NV12:
    case CL_G2D_NV21:
        frameFormats[0].dim_x = gStride;
        frameFormats[0].dim_y = gHeight;
        frameFormats[0].stride_x = 1;
        frameFormats[0].stride_y = gStride;
        frameFormats[1].dim_x = gStride;
        frameFormats[1].dim_y = gHeight;
        frameFormats[1].stride_x = 1;
        frameFormats[1].stride_y = gStride/2;
        break;
    default:
        break;
    }
    return true;
}

static bool planeptrs_fill_openvx(enum cl_g2d_format format, void *input_ptr, int w_stride,
                        int h_stride,void * plane_ptrs[])
{
    if((input_ptr == NULL) || (plane_ptrs == NULL))
        return false;
    int plane_count = g2d_get_planecount(format);
    char *in_ptr = (char *)input_ptr;
    for(int i = 0; i < plane_count; i ++) {
        plane_ptrs[i] = in_ptr;
        in_ptr += g2d_get_planesize(format, w_stride, h_stride, i);
    }
    return true;
}


//default ion heap
#define ION_BUFFER_HEAP 1
//64bit buffer alignment
#define ION_BUFFER_ALIGN 8

static void * allocate_memory(ion_user_handle_t *ion_hnd,
        int *ion_buf_fd,int size)
{
    int err;
    if (gMemory_type == 0) {
        *ion_hnd = -1;
        return malloc(size);
    }
    else {
        unsigned char *ptr = NULL;
        *ion_hnd = -1;
        *ion_buf_fd = -1;
        if (gIonFd <= 0) {
            gIonFd = ion_open();
        }

        if (gIonFd <= 0) {
            ALOGE("%s ion open failed", __func__);
            return NULL;
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 14, 0)
        int heap_cnt;
        int heap_mask = 0;
        struct ion_heap_data ihd[16];
        err = ion_query_heap_cnt(gIonFd, &heap_cnt);
        if (err != 0 || heap_cnt == 0) {
            ALOGE("can't query heap count");
            return NULL;
        }

        memset(&ihd, 0, sizeof(ihd));
        err = ion_query_get_heaps(gIonFd, heap_cnt, &ihd);
        if (err != 0) {
            ALOGE("can't get ion heaps");
            return NULL;
        }
        heap_mask = 0;
        // add heap ids from heap type.
        for (int i=0; i<heap_cnt; i++) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 10, 1)
            if (ihd[i].type == ION_HEAP_TYPE_DMA ||
                 ihd[i].type == ION_HEAP_TYPE_CARVEOUT) {
#else
            if (ihd[i].type == ION_HEAP_TYPE_DMA) {
#endif
                heap_mask |=  1 << ihd[i].heap_id;
                continue;
            }
        }
        err = ion_alloc_fd(gIonFd, size, ION_BUFFER_ALIGN, heap_mask, ION_FLAG_CACHED, ion_buf_fd);
        if (err) {
            ALOGE("ion allocation failed!\n");
            return NULL;
        }

        ptr = (unsigned char*)mmap(0, size, PROT_READ|PROT_WRITE,
                     MAP_SHARED, *ion_buf_fd, 0);
        if (ptr == MAP_FAILED) {
            ALOGE("mmap failed!\n");
            close(*ion_buf_fd);
            return NULL;
        }
#else
        err = ion_alloc(gIonFd,
            size,
            ION_BUFFER_ALIGN,
            ION_BUFFER_HEAP,
            0,
            ion_hnd);
        if (err) {
            ALOGE("ion_alloc failed");
            return NULL;
        }

        err = ion_map(gIonFd,
                *ion_hnd,
                size,
                PROT_READ | PROT_WRITE,
                MAP_SHARED,
                0,
                &ptr,
                ion_buf_fd);
        if (err) {
            ALOGE("ion_map failed.");
            ion_free(gIonFd, *ion_hnd);
            return NULL;
        }
#endif

        ALOGI("ion allocator: %p, size %d", ptr, size);
        return ptr;
    }
}

static void free_memory(void* pbuf, ion_user_handle_t ion_hnd,
        int ion_buf_fd, int size)
{
    if (gMemory_type == 0) {
        free(pbuf);
    }
    else {
        munmap(pbuf, size);
        if (ion_hnd > 0)
            ion_free(gIonFd, ion_hnd);
        if (ion_buf_fd > 0)
            close(ion_buf_fd);
    }
}

static void YUYVCopyByLine(uint8_t *dst, uint32_t dstWidth, uint32_t dstHeight,
        uint8_t *src, uint32_t srcWidth, uint32_t srcHeight)
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

static void convertNV12toNV21(uint8_t *dst, uint32_t width, uint32_t height,
        uint8_t *src)
{
    uint32_t i;
    uint8_t *pDstLine = dst;
    uint8_t *pUVDstLine = dst + width * height;
    uint8_t *pSrcLine = src;
    uint8_t *pUVSrcLine = src + width * height;
    uint32_t ystride = width;
    uint32_t uvstride = width/2;

    for (i = 0; i < height; i++) {
        memcpy(pDstLine, pSrcLine, ystride);

        for (uint32_t j = 0; j < uvstride/2; j++) {
            *(pUVDstLine + 0) = *(pUVSrcLine + 1);
            *(pUVDstLine + 1) = *(pUVSrcLine + 0);
            pUVDstLine += 2;
            pUVSrcLine += 2;
        }

        pSrcLine += ystride;
        pDstLine += ystride;
    }

    return;
}

static void convertYUYVtoNV12SP(uint8_t *inputBuffer, uint8_t *outputBuffer,
        int width, int height)
{
#define u32 unsigned int
#define u8 unsigned char

    u32 h, w;
    u32 nHeight = height;
    u32 nWidthDiv4 = width / 4;

    u32 *pYSrcOffset = (u32 *)inputBuffer;
    u32 value = 0;
    u32 value2 = 0;

    u32 *pYDstOffset = (u32 *)outputBuffer;
    u32 *pUVDstOffset = (u32 *)(((u8 *)(outputBuffer)) + width * height);

    for (h = 0; h < nHeight; h++) {
        if (!(h & 0x1)) {
            for (w = 0; w < nWidthDiv4; w++) {
                value = (*pYSrcOffset);
                value2 = (*(pYSrcOffset + 1));
                //use bitwise operation to get data from src to improve performance.
                *pYDstOffset = ((value & 0x000000ff) >> 0) |
                               ((value & 0x00ff0000) >> 8) |
                               ((value2 & 0x000000ff) << 16) |
                               ((value2 & 0x00ff0000) << 8);
                pYDstOffset += 1;

#ifdef PLATFORM_VERSION_4
                *pUVDstOffset = ((value & 0xff000000) >> 24) |
                                ((value & 0x0000ff00) >> 0) |
                                ((value2 & 0xff000000) >> 8) |
                                ((value2 & 0x0000ff00) << 16);
#else
                *pUVDstOffset = ((value & 0x0000ff00) >> 8) |
                                ((value & 0xff000000) >> 16) |
                                ((value2 & 0x0000ff00) << 8) |
                                ((value2 & 0xff000000) << 0);
#endif
                pUVDstOffset += 1;
                pYSrcOffset += 2;
            }
        } else {
            for (w = 0; w < nWidthDiv4; w++) {
                value = (*pYSrcOffset);
                value2 = (*(pYSrcOffset + 1));
                *pYDstOffset = ((value & 0x000000ff) >> 0) |
                               ((value & 0x00ff0000) >> 8) |
                               ((value2 & 0x000000ff) << 16) |
                               ((value2 & 0x00ff0000) << 8);
                pYSrcOffset += 2;
                pYDstOffset += 1;
            }
        }
    }
}

void usage(char *app)
{
    printf("%s test program.\n", app);
    printf("Usage: %s [-h] [-c] [-l len] [-w width] [-g height] [-i input_file] [-s input_format] [-o output_file] [-d output_format] [-m memory_type]\n", app);
    printf("\t-h\t  Print this message\n");
    printf("\t-b\t  Generate CL Binary as output file from input file\n");
    printf("\t-c\t  Memory copy test\n");
    printf("\t-l\t  Copy length\n");
    printf("\t-i\t  Input file\n");
    printf("\t-s\t  input format\n");
    printf("\t\t\t  24:YUYV,20:NV12, 21:I420, 23:NV21\n");
    printf("\t-d\t  output format\n");
    printf("\t\t\t  24:YUYV,20:NV12, 21:I420, 23:NV21\n");
    printf("\t-o\t  output to output_file\n");
    printf("\t-w\t  input width\n");
    printf("\t-g\t  intput height\n");
    printf("\t-t\t  input stride\n");
    printf("\t-x\t  output width\n");
    printf("\t-y\t  output height\n");
    printf("\t-z\t  output stride\n");
    printf("\t-m\t  memory_type\n");
    printf("\t\t\t  0:Cached memory,1:Non-cached ION memory\n");

}

static int update_surface_parameters(struct cl_g2d_surface *src, char *input_buf,
        struct cl_g2d_surface *dst, char *output_buf)
{
    src->format = gInput_format;
    if (gMemory_type == 1)
        src->usage = CL_G2D_DEVICE_MEMORY;
    switch (src->format) {
    case CL_G2D_YUYV:
        src->planes[0] = (long)input_buf;
        break;
    case CL_G2D_NV12:
    case CL_G2D_NV21:
        src->planes[0] = (long)input_buf;
        src->planes[1] = (long)(input_buf + gWidth * gHeight);
        break;
    default:
        ALOGE("Unsupport input format %d\n", src->format);
        return 0;
    }

    src->left = 0;
    src->top = 0;
    src->right = gWidth;
    src->bottom = gHeight;
    src->stride = gStride;
    src->width  = gWidth;
    src->height = gHeight;

    dst->format = gOutput_format;
    if (gMemory_type == 1)
        dst->usage = CL_G2D_DEVICE_MEMORY;
    switch (dst->format) {
    case CL_G2D_NV12:
    case CL_G2D_NV21:
        dst->planes[0] = (long)output_buf;
        dst->planes[1] = (long)(output_buf + gOutWidth * gOutHeight);
        break;
    case CL_G2D_YUYV:
        dst->planes[0] = (long)output_buf;
        break;
    case CL_G2D_I420:
        dst->planes[0] = (long)output_buf;
        dst->planes[1] = (long)(output_buf + gOutWidth * gOutHeight);
        dst->planes[2] = (long)(output_buf + gOutWidth * gOutHeight * 5 / 4);
        break;
    default:
        ALOGE("Unsupport output format %d\n", dst->format);
        return -1;
    }

    dst->left = 0;
    dst->top = 0;
    dst->right = gOutWidth;
    dst->bottom = gOutHeight;
    dst->stride = gOutStride;
    dst->width  = gOutWidth;
    dst->height = gOutHeight;
    return 0;
}

int createCLProgram(const char* fileSrcName, const char*fileBinName)
{
    cl_int errNum;
    cl_uint numPlatforms;
    cl_platform_id firstPlatformId;
    cl_context context = NULL;
    cl_uint numDevices = 0;
    cl_device_id *devices = NULL;
    cl_device_id device;
    cl_device_id *program_devices = NULL;
    size_t *programBinarySizes =  NULL;
    size_t deviceBufferSize = -1;
    unsigned char **programBinaries = NULL;
    cl_program program;
    size_t program_length;
    FILE* pSrcFileStream = NULL;
    char* source = NULL;
    int ret = 0;

    errNum = clGetPlatformIDs(1, &firstPlatformId, &numPlatforms);
    if (errNum != CL_SUCCESS || numPlatforms <= 0) {
        ALOGE("Failed to find any OpenCL platforms.\n");
        return -1;
    }

    cl_context_properties contextProperties[] =
    {
        CL_CONTEXT_PLATFORM,
        (cl_context_properties)firstPlatformId,
        0
    };
    context = clCreateContextFromType(contextProperties,
            CL_DEVICE_TYPE_GPU,
            NULL, NULL, &errNum);
    if (errNum != CL_SUCCESS) {
        ALOGE("Could not create GPU context, trying CPU...\n");
        context = clCreateContextFromType(contextProperties,
                CL_DEVICE_TYPE_CPU,
                NULL, NULL, &errNum);
        if (errNum != CL_SUCCESS) {
            ALOGE("Failed to create an OpenCL GPU or CPU context.\n");
            return -1;
        }
    }
    // First get the size of the devices buffer
    errNum = clGetContextInfo(context, CL_CONTEXT_DEVICES, 0, NULL,
            &deviceBufferSize);
    if (errNum != CL_SUCCESS) {
        ALOGE("Failed call to clGetContextInfo(...,GL_CONTEXT_DEVICES,...)\n");
        return -1;
    }
    if (deviceBufferSize <= 0) {
        ALOGE("No devices available.\n");
        return -1;
    }

    // Allocate memory for the devices buffer
    devices = new cl_device_id[numDevices];
    errNum = clGetContextInfo(context, CL_CONTEXT_DEVICES,
            deviceBufferSize, devices, NULL);
    if (errNum != CL_SUCCESS) {
        ALOGE("Failed to get device IDs\n");
        return -1;
    }
    device = devices[0];

    pSrcFileStream = fopen(fileSrcName, "rb");
    if(pSrcFileStream == 0) {
        ALOGE("Failed to open file %s for reading\n" ,fileSrcName);
        ret = -1;
        goto binary_out;
    }

    // get the length of the source code
    fseek(pSrcFileStream, 0, SEEK_END);
    program_length = ftell(pSrcFileStream);
    fseek(pSrcFileStream, 0, SEEK_SET);

    // allocate a buffer for the source code string and read it in
    source = (char *)malloc(program_length + 1);
    if (fread((source), program_length, 1, pSrcFileStream) != 1) {
        fclose(pSrcFileStream);
        free(source);
        ALOGE("Failed to open file %s for reading\n" ,fileSrcName);
        ret = -1;
        goto binary_out;
    }
    fclose(pSrcFileStream);
    source[program_length] = '\0';

    program = clCreateProgramWithSource(context, 1,
            (const char**)&source,
            NULL, NULL);
    free(source);
    if (program == NULL) {
        ALOGE("Failed to create CL program from source.\n");
        ret = -1;
        goto binary_out;
    }
    errNum = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);
    if (errNum != CL_SUCCESS) {
        // Determine the reason for the error
        char buildLog[16384];
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG,
                sizeof(buildLog), buildLog, NULL);
        ALOGE("Error in build kernel:\n");
        ALOGE("%s", buildLog);
        clReleaseProgram(program);
        ret = -1;
        goto binary_out;
    }
    errNum = clGetProgramInfo(program, CL_PROGRAM_NUM_DEVICES, sizeof(cl_uint),
                                          &numDevices, NULL);
    if (errNum != CL_SUCCESS) {
        ALOGE("Error querying for number of devices.");
        ret = -1;
        goto binary_out;
    }

    // 2 - Get all of the Device IDs
    program_devices = new cl_device_id[numDevices];
    errNum = clGetProgramInfo(program, CL_PROGRAM_DEVICES,
                sizeof(cl_device_id) * numDevices,
                program_devices, NULL);
    if (errNum != CL_SUCCESS) {
        ALOGE("Error querying for devices.");
        ret = -1;
        goto binary_out;
    }

    programBinarySizes = new size_t [numDevices];
    errNum = clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES,
            sizeof(size_t) * numDevices,
            programBinarySizes, NULL);
    if (errNum != CL_SUCCESS) {
        ALOGE("Error querying for program binary sizes.");
        ret = -1;
        goto binary_out;
    }

    programBinaries = new unsigned char*[numDevices];
    for (cl_uint i = 0; i < numDevices; i++) {
        programBinaries[i] =
            new unsigned char[programBinarySizes[i]];
    }
    errNum = clGetProgramInfo(program, CL_PROGRAM_BINARIES,
            sizeof(unsigned char*) * numDevices,
            programBinaries, NULL);
    if (errNum != CL_SUCCESS) {
        ALOGE("Error querying for program binaries");
        ret = -1;
        goto binary_out;
    }

    for (cl_uint i = 0; i < numDevices; i++) {
        // Store the binary just for the device requested.
        // In a scenario where multiple devices were being used
        // you would save all of the binaries out here.
        if (program_devices[i] == device) {
            FILE *fp = fopen(fileBinName, "wb");
            fwrite(programBinaries[i], 1,
                    programBinarySizes[i], fp);
            fclose(fp);
            break;
        }
    }

binary_out:
    if(devices != NULL)
        delete [] devices;
    if(program_devices != NULL)
        delete [] program_devices;
    if(programBinarySizes != NULL)
        delete [] programBinarySizes;
    for(cl_uint i = 0; i < numDevices; i++) {
        if( programBinaries != NULL)
            delete [] programBinaries[i];
    }
    if(programBinaries != NULL)
        delete [] programBinaries;

    if(pSrcFileStream == 0)
        fclose(pSrcFileStream);
    if(program != NULL)
        clReleaseProgram(program);
    if(context != NULL)
        clReleaseContext(context);
    return ret;
}

int yuv422iResize(uint8_t *srcBuf,
                                    int      srcWidth,
                                    int      srcHeight,
                                    uint8_t *dstBuf,
                                    int      dstWidth,
                                    int      dstHeight)
{
    int i, j;
    int h_offset;
    int v_offset;
    unsigned char *ptr, cc;
    int h_scale_ratio;
    int v_scale_ratio;

    int srcStride;
    int dstStride;

    if (!srcWidth || !srcHeight || !dstWidth || !dstHeight) return -1;

    h_scale_ratio = srcWidth / dstWidth;
    v_scale_ratio = srcHeight / dstHeight;

    if((h_scale_ratio > 0) && (v_scale_ratio > 0))
        goto reduce;
    else if(h_scale_ratio + v_scale_ratio <= 1)
        goto enlarge;

    ALOGE("%s, not support resize %dx%d to %dx%d",
        __func__, srcWidth, srcHeight, dstWidth, dstHeight);

    return -1;

reduce:
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

enlarge:
    int h_offset_end;
    int v_offset_end;
    int srcRow;
    int srcCol;

    h_scale_ratio = dstWidth / srcWidth;
    v_scale_ratio = dstHeight / srcHeight;

    h_offset = (dstWidth - srcWidth * h_scale_ratio) / 2;
    v_offset = (dstHeight - srcHeight * v_scale_ratio) / 2;

    h_offset_end = h_offset + srcWidth * h_scale_ratio;
    v_offset_end = v_offset + srcHeight * v_scale_ratio;

    srcStride = srcWidth * 2;
    v_offset = (dstHeight - srcHeight * v_scale_ratio) / 2;

    h_offset_end = h_offset + srcWidth * h_scale_ratio;
    v_offset_end = v_offset + srcHeight * v_scale_ratio;

    srcStride = srcWidth * 2;
    dstStride = dstWidth * 2;

    ALOGV("h_scale_ratio %d, v_scale_ratio %d, h_offset %d, v_offset %d, h_offset_end %d, v_offset_end %d",
            h_scale_ratio, v_scale_ratio, h_offset, v_offset, h_offset_end, v_offset_end);

    // for Y
    for (i = 0; i < dstHeight; i++)
    {
        // top, bottom black margin
        if((i < v_offset) || (i >= v_offset_end)) {
            for (j = 0; j < dstWidth; j++)
            {
                dstBuf[dstStride*i + j*2] = 0;
            }
            continue;
        }

        for (j = 0; j < dstWidth; j++)
        {
            // left, right black margin
            if((j < h_offset) || (j >= h_offset_end)) {
                dstBuf[dstStride*i + j*2] = 0;
                continue;
            }

            srcRow = (i - v_offset)/v_scale_ratio;
            srcCol = (j - h_offset)/h_scale_ratio;
            dstBuf[dstStride*i + j*2] = srcBuf[srcStride * srcRow + srcCol*2];
        }
    }

    // for UV
    for (i = 0; i < dstHeight; i++)
    {
        // top, bottom black margin
        if((i < v_offset) || (i >= v_offset_end)) {
            for (j = 0; j < dstWidth; j++)
            {
                dstBuf[dstStride*i + j*2+1] = 128;
            }
            continue;
        }

        for (j = 0; j < dstWidth; j++)
        {
            // left, right black margin
            if((j < h_offset) || (j >= h_offset_end)) {
                dstBuf[dstStride*i + j*2+1] = 128;
                continue;
            }

            srcRow = (i - v_offset)/v_scale_ratio;
            srcCol = (j - h_offset)/h_scale_ratio;
            dstBuf[dstStride*i + j*2+1] = srcBuf[srcStride * srcRow + srcCol*2+1];
        }
    }

    return 0;
}

struct testPhyBuffer {
    void* mVirtAddr;
    uint64_t mPhyAddr;
    size_t mSize;
    int32_t mFd;
};

int AllocPhyBuffer(struct testPhyBuffer *phyBufs)
{
    int sharedFd;
    uint64_t phyAddr;
    uint64_t outPtr;
    uint32_t ionSize = phyBufs->mSize;

    if (phyBufs == NULL)
        return -1;

    fsl::Allocator *allocator = fsl::Allocator::getInstance();
    if (allocator == NULL) {
        printf("%s ion allocator invalid\n", __func__);
        return -1;
    }

    sharedFd = allocator->allocMemory(ionSize,
                    MEM_ALIGN, fsl::MFLAGS_CONTIGUOUS);
    if (sharedFd < 0) {
        printf("%s: allocMemory failed.\n", __func__);
        return -1;
    }

    int err = allocator->getVaddrs(sharedFd, ionSize, outPtr);
    if (err != 0) {
        printf("%s: getVaddrs failed.\n", __func__);
        close(sharedFd);
        return -1;
    }

    err = allocator->getPhys(sharedFd, ionSize, phyAddr);
    if (err != 0) {
        printf("%s: getPhys failed.\n", __func__);
        munmap((void*)(uintptr_t)outPtr, ionSize);
        close(sharedFd);
        return -1;
    }

    printf("%s, outPtr:%p,  phy:%p, virt: %p, ionSize:%d\n", __func__, (void *)outPtr, (void *)phyAddr, (void *)outPtr, ionSize);

    phyBufs->mVirtAddr = (void *)outPtr;
    phyBufs->mPhyAddr = phyAddr;
    phyBufs->mFd = sharedFd;

    return 0;
}

int FreePhyBuffer(struct testPhyBuffer *phyBufs)
{
    if (phyBufs == NULL)
        return -1;

    if (phyBufs->mVirtAddr)
        munmap(phyBufs->mVirtAddr, phyBufs->mSize);

    if (phyBufs->mFd > 0)
        close(phyBufs->mFd);

    return 0;
}


int main(int argc, char** argv)
{
    int rt;
    int inputlen = 0;
    int outputlen = 0;
    int read_len = 0;

    void *input_buf = NULL;
    ion_user_handle_t input_ion_hnd = 0;
    int input_ion_buf_fd = 0;

    void *output_buf = NULL;
    ion_user_handle_t output_ion_hnd = 0;
    int output_ion_buf_fd = 0;

    void *output_benchmark_buf = NULL;
    ion_user_handle_t benchmark_ion_hnd = 0;
    int benchmark_ion_buf_fd = 0;

    void *output_vx_buf = NULL;
    ion_user_handle_t output_vx_ion_hnd = 0;
    int output_vx_ion_buf_fd = 0;

    struct cl_g2d_surface src,dst;
    void *g2dHandle = NULL;

    vx_context context = NULL;

    if (argc < 3) {
        usage(argv[0]);
        return 0;
    }

    while ((rt = getopt(argc, argv, "hbcl:i:s:o:d:w:g:t:m:x:y:z:")) >= 0) {
        switch (rt) {
        case 'h':
            usage(argv[0]);
            return 0;
        case 'b':
            gCLBuildTest = true;
            break;
        case 'c':
            gMemTest = true;
            break;
        case 'l':
            gCopyLen = atoi(optarg);
            break;
        case 'i':
            memset(input_file, 0, MAX_FILE_LEN);
            strncpy(input_file, optarg, MAX_FILE_LEN);
            break;
        case 's':
            gInput_format = (enum cl_g2d_format)atoi(optarg);
            break;
        case 'd':
            gOutput_format = (enum cl_g2d_format)atoi(optarg);
            break;
        case 'w':
            gWidth = atoi(optarg);
            break;
        case 'g':
             gHeight = atoi(optarg);
            break;
        case 't':
             gStride = atoi(optarg);
            break;
        case 'x':
             gOutWidth = atoi(optarg);
            break;
        case 'y':
             gOutHeight = atoi(optarg);
            break;
        case 'z':
             gOutStride = atoi(optarg);
            break;
        case 'm':
            gMemory_type = atoi(optarg);
            break;
        case 'o':
            memset(output_file, 0, MAX_FILE_LEN);
            strncpy(output_file, optarg, MAX_FILE_LEN);
            break;
        default:
            usage(argv[0]);
            return 0;
        }
    }

    if (gOutWidth == 0)
        gOutWidth = gWidth;
    if (gOutHeight == 0)
        gOutHeight = gHeight;
    if (gStride == 0)
        gStride = gWidth;
    if (gOutStride == 0)
        gOutStride = gOutWidth;

    if (gCLBuildTest) {
        ALOGI("Start opencl 2d binary build:");
        ALOGI("input file: %s", input_file);
        ALOGI("output file: %s", output_file);
        if(createCLProgram(input_file, output_file) == 0)
            ALOGI("Success on opencl 2d binary build!");
        else
            ALOGI("Fail on opencl 2d binary build!");
        return 0;
    }

    //Either copy or blit
    if (gMemTest && (gWidth != 0) && (gHeight != 0) ) {
        usage(argv[0]);
        return 0;
    }

    if (!gMemTest && (gWidth == 0) && (gHeight == 0) ) {
        usage(argv[0]);
        return 0;
    }

    if (gMemTest && (gCopyLen == 0)) {
        usage(argv[0]);
        return 0;
    }

    ALOGI("Start opencl 2d test with:");
    ALOGI("input file: %s", input_file);
    ALOGI("output file: %s", output_file);
    if (!gMemTest) {
        ALOGI("src width: %d", gWidth);
        ALOGI("src height: %d", gHeight);
        ALOGI("src stride: %d", gStride);
        ALOGI("out width: %d", gOutWidth);
        ALOGI("out height: %d", gOutHeight);
        ALOGI("out stride: %d", gOutStride);
        ALOGI("input format: %d", gInput_format);
        ALOGI("output format: %d", gOutput_format);
    } else {
        ALOGI("copy len: %d", gCopyLen);
    }

    inputlen = get_file_len(input_file);
    if (inputlen <= 0 ||
        inputlen < get_buf_size(gInput_format, gWidth, gHeight, gMemTest, gCopyLen)) {
        ALOGE("No valid file %s for this test", input_file);
        goto clean;
    }

/*
    input_buf  = allocate_memory(&input_ion_hnd, &input_ion_buf_fd,
            inputlen);
    if(input_buf  == NULL) {
        ALOGE("Cannot allocate input buffer");
        goto clean;
    }
*/
    struct testPhyBuffer InPhyBuffer;
    struct testPhyBuffer OutPhyBuffer;
    struct testPhyBuffer OutVXPhyBuffer;
    struct testPhyBuffer OutBenchMarkPhyBuffer;
    memset(&InPhyBuffer, 0, sizeof(InPhyBuffer));
    memset(&OutPhyBuffer, 0, sizeof(OutPhyBuffer));
    memset(&OutVXPhyBuffer, 0, sizeof(OutVXPhyBuffer));
    memset(&OutBenchMarkPhyBuffer, 0, sizeof(OutBenchMarkPhyBuffer));

    InPhyBuffer.mSize = inputlen;
    AllocPhyBuffer(&InPhyBuffer);
    input_buf = InPhyBuffer.mVirtAddr;
    if(input_buf == NULL) {
        ALOGE("Cannot allocate input buffer");
        goto clean;
    }

    read_len = read_from_file((char *)input_buf, inputlen, input_file);
    dump_buffer((char *)input_buf, 64, "input");


    outputlen = get_buf_size(gOutput_format, gOutWidth, gOutHeight, gMemTest, gCopyLen);
/*    output_buf  = allocate_memory(&output_ion_hnd, &output_ion_buf_fd,
            outputlen);
    output_vx_buf  = allocate_memory(&output_vx_ion_hnd, &output_vx_ion_buf_fd,
            outputlen);
    output_benchmark_buf  = allocate_memory(&benchmark_ion_hnd, &benchmark_ion_buf_fd,
            outputlen);
*/

    OutPhyBuffer.mSize = outputlen;
    AllocPhyBuffer(&OutPhyBuffer);
    output_buf = OutPhyBuffer.mVirtAddr;

    OutVXPhyBuffer.mSize = outputlen;
    AllocPhyBuffer(&OutVXPhyBuffer);
    output_vx_buf = OutVXPhyBuffer.mVirtAddr;

    OutBenchMarkPhyBuffer.mSize = outputlen;
    AllocPhyBuffer(&OutBenchMarkPhyBuffer);
    output_benchmark_buf = OutBenchMarkPhyBuffer.mVirtAddr;

    if((output_buf  == NULL)||(output_benchmark_buf == NULL)||(output_vx_buf == NULL)) {
        ALOGE("Cannot allocate output buffer");
        goto clean;
    }
    memset(output_buf, 0, outputlen);
    ALOGI("Get openCL output ptr %p", output_buf);
    memset(output_vx_buf, 0, outputlen);
    ALOGI("Get openVX output ptr %p", output_vx_buf);
    memset(output_benchmark_buf, 0, outputlen);
    ALOGI("Get CPU output ptr %p", output_benchmark_buf);

    if(cl_g2d_open(&g2dHandle) == -1 || g2dHandle == NULL) {
        ALOGE("Fail to open g2d device!\n");
        goto clean;
    }

    uint64_t t1, t2;
    ALOGI("Start openCL 2d blit, in size %d, out size %d", inputlen, outputlen);
    t1 = systemTime();
    for(int loop = 0; loop < G2D_TEST_LOOP; loop ++) {
        if (!gMemTest) {
            update_surface_parameters(&src, (char *)input_buf,
                &dst, (char *)output_buf);

            ALOGI("call cl_g2d_blit");
            cl_g2d_blit(g2dHandle, &src, &dst);
        }
        else {
            struct cl_g2d_buf g2d_output_buf;
            struct cl_g2d_buf g2d_input_buf;
            g2d_output_buf.buf_vaddr = output_buf;
            g2d_output_buf.buf_size = gCopyLen;
            g2d_input_buf.buf_vaddr = input_buf;
            g2d_input_buf.buf_size = gCopyLen;
            if (gMemory_type == 1) {
                g2d_output_buf.usage = CL_G2D_DEVICE_MEMORY;
                g2d_input_buf.usage = CL_G2D_DEVICE_MEMORY;
            } else {
                g2d_output_buf.usage = CL_G2D_CPU_MEMORY;
                g2d_input_buf.usage = CL_G2D_CPU_MEMORY;
            }
            ALOGI("call cl_g2d_copy");
            cl_g2d_copy(g2dHandle, &g2d_output_buf,
                    &g2d_input_buf, (unsigned int)gCopyLen);
        }
        cl_g2d_flush(g2dHandle);
        cl_g2d_finish(g2dHandle);
    }
    t2 = systemTime();
    ALOGI("End openCL 2d blit, %d loops use %lld ns, average %lld ns per loop", G2D_TEST_LOOP, t2-t1, (t2-t1)/G2D_TEST_LOOP);

    ALOGI("Start CPU 2d blit");
    t1 = systemTime();
    if (!gMemTest) {
        if ((src.format == CL_G2D_YUYV) && (dst.format == CL_G2D_NV12)) {
            convertYUYVtoNV12SP((uint8_t *)input_buf, (uint8_t *)output_benchmark_buf,
                    gOutWidth, gOutHeight);
        }
        else if ((src.format == CL_G2D_YUYV) && (dst.format == CL_G2D_YUYV)) {
            if ((gWidth == gOutWidth) && (gHeight == gOutHeight))
                YUYVCopyByLine((uint8_t *)output_benchmark_buf, gOutWidth, gOutHeight,
                  (uint8_t *)input_buf, gWidth, gHeight);
            else
                yuv422iResize((uint8_t *)input_buf, gWidth, gHeight, (uint8_t *)output_benchmark_buf, gOutWidth, gOutHeight);
        }
        else if ((src.format == CL_G2D_NV12) && (dst.format == CL_G2D_NV21)) {
            convertNV12toNV21((uint8_t *)output_benchmark_buf, gOutWidth, gOutHeight,
                (uint8_t *)input_buf);
        } else if((src.format == CL_G2D_NV12) && (dst.format == CL_G2D_I420) && (gWidth == gOutWidth) && (gHeight == gOutHeight)) {
            uint8_t *nv12_y = (uint8_t *)input_buf;
            uint8_t *nv12_uv = (uint8_t *)input_buf + gWidth*gHeight;
            int nv12_y_stride = gWidth;
            int nv12_uv_stride = gWidth;

            libyuv::NV12ToI420(
                nv12_y, nv12_y_stride, nv12_uv, nv12_uv_stride,
                (uint8_t *)output_benchmark_buf, gWidth,
                (uint8_t *)output_benchmark_buf + gWidth*gHeight, gWidth/2,
                (uint8_t *)output_benchmark_buf + gWidth*gHeight*5/4, gWidth/2,
                gWidth, gHeight);
        } else {
            ALOGW("unsupported by CPU blit, src.format %d, dst.format %d", src.format, dst.format);
        }
    } else {
        memcpy(output_benchmark_buf, input_buf, gCopyLen);
    }
    t2 = systemTime();
    ALOGI("End CPU 2d blit, use %lld ns", t2-t1);

#if 0
    ALOGI("Start openVX blit");
    {
        vx_status status;
        void *input_planes[3] = { NULL, NULL, NULL};
        void *output_planes[3] = { NULL, NULL, NULL};
        vx_df_image src_type = imagetype_to_openvx(src.format);
        vx_df_image dst_type = imagetype_to_openvx(dst.format);
        vx_imagepatch_addressing_t frameFormats[3];

        planeptrs_fill_openvx(src.format, input_buf, gStride,
                        gHeight, input_planes);
        planeptrs_fill_openvx(dst.format, output_vx_buf, gStride,
                        gHeight, output_planes);

        context = vxCreateContext();
        ALOGI("Create openVX inputput Image based on plan 0: 0x%p, plane 1: 0x%p",
                input_planes[0], input_planes[1]);
        imagepatch_to_openvx(src.format, gStride, gHeight, frameFormats);
        vx_image im = vxCreateImageFromHandle(context, src_type,
                                            frameFormats, input_planes, VX_MEMORY_TYPE_HOST);
        status = vxGetStatus((vx_reference)im);
        if (status != VX_SUCCESS) {
            ALOGI("Falied to create input vx image with error %d", status);
            goto clean;
        }

        ALOGI("Create openVX output Image based on plan 0: 0x%p, plane 1: 0x%p",
                output_planes[0], output_planes[1]);
        imagepatch_to_openvx(dst.format, gStride, gHeight, frameFormats);
        vx_image om = vxCreateImageFromHandle(context, dst_type,
                                            frameFormats, output_planes, VX_MEMORY_TYPE_HOST);
        status = vxGetStatus((vx_reference)om);
        if (status != VX_SUCCESS) {
            ALOGI("Falied to create output vx image");
            goto clean;
        }

        status = vxuColorConvert(context, im, om);
        if (status != VX_SUCCESS) {
            ALOGI("Falied to convert vx image");
            goto clean;
        }
        vx_imagepatch_addressing_t addr;
        vx_map_id map_id;
        void* ptr = 0;
        vx_rectangle_t rect;
        rect.start_x = 0;
        rect.start_y =0;
        rect.end_x = gOutWidth;
        rect.end_y = gOutHeight;
        status = vxMapImagePatch(om, &rect, 0, &map_id, &addr, &ptr, VX_READ_ONLY, VX_MEMORY_TYPE_HOST, 0);
        if (status != VX_SUCCESS) {
            ALOGI("Falied to map output vx image");
            goto clean;
        }
        ALOGI("Get openVX output ptr %p", ptr);
        status = vxUnmapImagePatch(om, map_id);
        if (status != VX_SUCCESS) {
            ALOGI("Falied to unmap output vx image");
            goto clean;
        }
    }
    ALOGI("End openVX blit");
#endif

    if (!gMemTest) {
        if (dst.format == CL_G2D_YUYV) {
            dump_buffer((char *)output_buf, 128, "cl_output_yuyv");
            dump_buffer((char *)output_benchmark_buf, 128, "output_benchmark_yuyv");
            dump_buffer((char *)output_vx_buf, 128, "vx_output_yuyv");
        }
        else if (dst.format == CL_G2D_NV12) {
            dump_buffer((char *)output_buf, 64, "cl_output_y");
            dump_buffer((char *)output_buf + gOutWidth*gOutHeight, 64, "cl_output_uv");
            dump_buffer((char *)output_vx_buf, 64, "vx_output_y");
            dump_buffer((char *)output_vx_buf + gOutWidth*gOutHeight, 64, "vx_output_uv");
            dump_buffer((char *)output_benchmark_buf, 64, "output_benchmark_y");
            dump_buffer((char *)output_benchmark_buf + gOutWidth*gOutHeight, 64, "output_benchmark_uv");
        }
        else if (dst.format == CL_G2D_NV21) {
            dump_buffer((char *)output_buf, 64, "cl_output_y");
            dump_buffer((char *)output_buf + gOutWidth*gOutHeight, 64, "output_uv");
            dump_buffer((char *)output_vx_buf, 64, "vx_output_y");
            dump_buffer((char *)output_vx_buf + gOutWidth*gOutHeight, 64, "vx_output_uv");
            dump_buffer((char *)output_benchmark_buf, 64, "output_benchmark_y");
            dump_buffer((char *)output_benchmark_buf + gOutWidth*gOutHeight, 64, "output_benchmark_uv");
        }
    } else {
        dump_buffer((char *)output_buf, gCopyLen>256?256:gCopyLen, "cl_output");
        dump_buffer((char *)output_vx_buf, gCopyLen>256?256:gCopyLen, "vx_output");
        dump_buffer((char *)output_benchmark_buf, gCopyLen>256?256:gCopyLen, "output_benchmark");
    }

    write_from_file((char *)output_buf, outputlen, output_file);
    strncpy(output_vx_file, output_file, strlen(output_file));
    strcat(output_vx_file, "_vx");
    write_from_file((char *)output_vx_buf, outputlen, output_vx_file);
    strncpy(output_benchmark_file, output_file, strlen(output_file));
    strcat(output_benchmark_file, "_benchmark");
    write_from_file((char *)output_benchmark_buf, outputlen, output_benchmark_file);

clean:
/*
    if(input_buf  == NULL)
        free_memory(input_buf, input_ion_hnd,
                input_ion_buf_fd, inputlen);
    if(output_buf  == NULL)
        free_memory(output_buf, output_ion_hnd,
                output_ion_buf_fd, outputlen);
    if(output_vx_buf  == NULL)
        free_memory(output_vx_buf, output_vx_ion_hnd,
                output_vx_ion_buf_fd, outputlen);
    if(output_benchmark_buf  == NULL)
        free_memory(output_benchmark_buf, benchmark_ion_hnd,
                benchmark_ion_buf_fd, outputlen);
*/

    FreePhyBuffer(&InPhyBuffer);
    FreePhyBuffer(&OutPhyBuffer);
    FreePhyBuffer(&OutVXPhyBuffer);
    FreePhyBuffer(&OutBenchMarkPhyBuffer);

    if(g2dHandle  == NULL)
        cl_g2d_close(g2dHandle);
    if(gIonFd == 0)
        ion_close(gIonFd);
    if (context != nullptr)
        vxReleaseContext(&context);

    return 0;
}
