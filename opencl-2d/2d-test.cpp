/*
 * Copyright 2018-2023 NXP.
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
#include <CL/opencl.h>
#include <cutils/properties.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <libyuv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <utils/Timers.h>
#ifdef BUILD_FOR_ANDROID
#include <cutils/log.h>
#endif

#include <g2d.h>
#include <linux/videodev2.h>

#include "Allocator.h"
#include "opencl-2d.h"

typedef int (*hwc_func1)(void *handle);
typedef int (*hwc_func3)(void *handle, void *arg1, void *arg2);
typedef int (*hwc_func4)(void *handle, void *arg1, void *arg2, void *arg3);
hwc_func1 mOpenEngine;
hwc_func1 mCloseEngine;
hwc_func1 mFinishEngine;
hwc_func4 mCopyEngine;
hwc_func3 mBlitEngine;

hwc_func1 mCLOpen;
hwc_func1 mCLClose;
hwc_func4 mCLCopy;
hwc_func3 mCLBlit;
hwc_func1 mCLFlush;
hwc_func1 mCLFinish;

#if defined(__LP64__)
#define LIB_PATH1 "/system/lib64"
#define LIB_PATH2 "/vendor/lib64"
#else
#define LIB_PATH1 "/system/lib"
#define LIB_PATH2 "/vendor/lib"
#endif

#define CLENGINE "libg2d-opencl.so"
#define G2DENGINE "libg2d"

#define LOG_TAG "2d-test"
#define DEBUG 1
#define MAX_FILE_LEN 128
#define G2D_TEST_LOOP 10
#define TEST_BUFFER_NUM 3

static char input_file[MAX_FILE_LEN];
static char output_file[MAX_FILE_LEN];
static char output_2d_file[MAX_FILE_LEN];
static char output_cl_file[MAX_FILE_LEN];
static char output_benchmark_file[MAX_FILE_LEN];
static enum cl_g2d_format gInput_format = CL_G2D_YUYV;
static enum cl_g2d_format gOutput_format = CL_G2D_YUYV;
static int gWidth = 0;
static int gHeight = 0;
static int gStride = 0;
static int gOutWidth = 0;
static int gOutHeight = 0;
static int gOutStride = 0;
static int gInputMemory_type = 0;
static int gOutputMemory_type = 0;
static bool gMemTest = false;
static bool gCLBuildTest = false;
static int gCopyLen = 0;
static bool g_usePhyAddr = false;

static int get_buf_size(enum cl_g2d_format format, int width, int height, bool copyTest,
                        int copyLen) {
    if (!copyTest) {
        switch (format) {
            case CL_G2D_YUYV:
                return width * height * 2;
            case CL_G2D_NV12:
            case CL_G2D_NV21:
            case CL_G2D_I420:
                return width * height * 3 / 2;
            default:
                ALOGE("unsupported format\n");
        }
    } else {
        return copyLen;
    }
    return 0;
}

static int get_file_len(const char *filename) {
    int fd = 0;
    int filesize = 0;
    fd = open(filename, O_RDWR, 0666);
    if (fd < 0) {
        ALOGE("Unable to open file [%s]\n", filename);
        return -1;
    }
    filesize = lseek(fd, 0, SEEK_END);
    close(fd);
    return filesize;
}

static int read_from_file(char *buf, int count, const char *filename) {
    int fd = 0;
    int len = 0;
    fd = open(filename, O_RDWR, O_RDONLY);
    if (fd < 0) {
        ALOGE("Unable to open file [%s]\n", filename);
        return -1;
    }
    len = read(fd, buf, count);
    close(fd);
    return len;
}

static int write_from_file(char *buf, int count, const char *filename) {
    int fd = 0;
    int len = 0;
    fd = open(filename, O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        ALOGE("Unable to open file [%s]\n", filename);
        return -1;
    }
    len = write(fd, buf, count);
    close(fd);
    return len;
}

#ifdef DEBUG
static void dump_buffer(char *pbuf, int count, const char *title) {
    int i = 0, j = 0;
    char *buf = pbuf;
    char printbuf[256];
    memset(printbuf, 0, 256);

    if ((pbuf == NULL) || (title == NULL))
        return;

    ALOGI("Dump buffer %s, count 0x%x\n", title, count);
    for (i = 0; i < count; i += 16) {
        int pcount = count - i;
        if (pcount >= 16)
            ALOGI("0x%x: %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n", i, *(buf + 0),
                  *(buf + 1), *(buf + 2), *(buf + 3), *(buf + 4), *(buf + 5), *(buf + 6),
                  *(buf + 7), *(buf + 8), *(buf + 9), *(buf + 10), *(buf + 11), *(buf + 12),
                  *(buf + 13), *(buf + 14), *(buf + 15));
        else {
            // ALOGI("0x%x: ", i);
            sprintf(printbuf, "0x%x: ", i);
            for (j = 0; j < pcount; j++) {
                // ALOGI("\b\b %x ", *(buf + j));
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

static int g2d_get_planecount(unsigned int format) {
    switch (format) {
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

static int g2d_get_planebpp(unsigned int format, int plane) {
    if (plane >= g2d_get_planecount(format))
        return 0;
    switch (format) {
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
            if (plane == 0)
                return 8;
            else
                return 4;
        case CL_G2D_NV12_10BIT_TILED:
            if (plane == 0)
                return 10;
            else
                return 5;

        case CL_G2D_YV12:
        case CL_G2D_I420:
            if (plane == 0)
                return 8;
            else
                return 2;

        default:
            ALOGE("%s: unsupported format for getting bpp\n", __func__);
    }
    return 0;
}

static int g2d_get_planesize(enum cl_g2d_format format, int w_stride, int h_stride, int plane) {
    if (plane >= g2d_get_planecount(format))
        return 0;

    int bpp = g2d_get_planebpp(format, plane);

    if (format == CL_G2D_NV12_10BIT_TILED) {
        return (plane == 0) ? w_stride * h_stride : w_stride * h_stride / 2;
    }

    return w_stride * h_stride * bpp / 8;
}

static void YUYVCopyByLine(uint8_t *dst, uint32_t dstWidth, uint32_t dstHeight, uint8_t *src,
                           uint32_t srcWidth, uint32_t srcHeight) {
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

static void convertNV12toNV21(uint8_t *dst, uint32_t width, uint32_t height, uint8_t *src) {
    uint32_t i;
    uint8_t *pDstLine = dst;
    uint8_t *pUVDstLine = dst + width * height;
    uint8_t *pSrcLine = src;
    uint8_t *pUVSrcLine = src + width * height;
    uint32_t ystride = width;
    uint32_t uvstride = width / 2;

    for (i = 0; i < height; i++) {
        memcpy(pDstLine, pSrcLine, ystride);

        for (uint32_t j = 0; j < uvstride / 2; j++) {
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

static void convertYUYVtoNV12SP(uint8_t *inputBuffer, uint8_t *outputBuffer, int width,
                                int height) {
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
                // use bitwise operation to get data from src to improve performance.
                *pYDstOffset = ((value & 0x000000ff) >> 0) | ((value & 0x00ff0000) >> 8) |
                        ((value2 & 0x000000ff) << 16) | ((value2 & 0x00ff0000) << 8);
                pYDstOffset += 1;

#ifdef PLATFORM_VERSION_4
                *pUVDstOffset = ((value & 0xff000000) >> 24) | ((value & 0x0000ff00) >> 0) |
                        ((value2 & 0xff000000) >> 8) | ((value2 & 0x0000ff00) << 16);
#else
                *pUVDstOffset = ((value & 0x0000ff00) >> 8) | ((value & 0xff000000) >> 16) |
                        ((value2 & 0x0000ff00) << 8) | ((value2 & 0xff000000) << 0);
#endif
                pUVDstOffset += 1;
                pYSrcOffset += 2;
            }
        } else {
            for (w = 0; w < nWidthDiv4; w++) {
                value = (*pYSrcOffset);
                value2 = (*(pYSrcOffset + 1));
                *pYDstOffset = ((value & 0x000000ff) >> 0) | ((value & 0x00ff0000) >> 8) |
                        ((value2 & 0x000000ff) << 16) | ((value2 & 0x00ff0000) << 8);
                pYSrcOffset += 2;
                pYDstOffset += 1;
            }
        }
    }
}

void usage(char *app) {
    printf("%s test program.\n", app);
    printf("Usage: %s [-h] [-c] [-l len] [-w width] [-g height] [-i input_file] [-s input_format] "
           "[-o output_file] [-d output_format] [-m InputMemory_type] [-n Outputmemory_type]\n",
           app);
    printf("\t-h\t  Print this message\n");
    printf("\t-b\t  Generate CL Binary as output file from input file\n");
    printf("\t-c\t  Memory copy test\n");
    printf("\t-l\t  Copy length\n");
    printf("\t-i\t  Input file\n");
    printf("\t-s\t  input format\n");
    printf("\t\t\t  24:YUYV,20:NV12, 21:I420, 23:NV21, 28:NV16\n");
    printf("\t-d\t  output format\n");
    printf("\t\t\t  24:YUYV,20:NV12, 21:I420, 23:NV21, 28:NV16\n");
    printf("\t-o\t  output to output_file\n");
    printf("\t-w\t  input width\n");
    printf("\t-g\t  intput height\n");
    printf("\t-t\t  input stride\n");
    printf("\t-x\t  output width\n");
    printf("\t-y\t  output height\n");
    printf("\t-z\t  output stride\n");
    printf("\t-m\t  InputMemory_type\n");
    printf("\t-n\t  Outputmemory_type\n");
    printf("\t\t\t  0:un-cached memory,1:cached memory\n");
    printf("\t-p\t  use physical address, 0:use virt addr(default), 1: use phy addr\n");
    printf("\t-v\t  v4l2 device(such as /dev/video0, /dev/video1, ...)\n");
    printf("\tex\t  copy: 2d-test_64 -i 1080p.yuyv -o 1080p_cp.yuyv -c\n");
    printf("\t  \t  csc:  2d-test_64 -i 1080p.yuyv -s 24 -d 20 -o 1080p-out.nv12 -w 1920 -g 1080 "
           "-t 1920 -x 1920 -y 1080 -z 1920\n");
}

static int update_surface_parameters(struct cl_g2d_surface *src, char *input_buf,
                                     struct cl_g2d_surface *dst, char *output_buf,
                                     bool bUsePhyAddr) {
    if (gInputMemory_type == 0)
        src->usage = CL_G2D_UNCACHED_MEMORY;
    else
        src->usage = CL_G2D_CACHED_MEMORY;

    src->format = gInput_format;
    switch (src->format) {
        case CL_G2D_YUYV:
            src->planes[0] = (long)input_buf;
            break;
        case CL_G2D_NV12:
        case CL_G2D_NV21:
            src->planes[0] = (long)input_buf;
            src->planes[1] = (long)(input_buf + gWidth * gHeight);
            break;
        case CL_G2D_NV16:
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
    src->width = gWidth;
    src->height = gHeight;
    src->usePhyAddr = bUsePhyAddr;

    if (gOutputMemory_type == 0)
        dst->usage = CL_G2D_UNCACHED_MEMORY;
    else
        dst->usage = CL_G2D_CACHED_MEMORY;

    dst->format = gOutput_format;
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
    dst->width = gOutWidth;
    dst->height = gOutHeight;
    dst->usePhyAddr = bUsePhyAddr;
    return 0;
}

static int update_surface_parameters_2d(struct g2d_buf *s_buf, struct g2d_surface *s_surface,
                                        char *input_buf, uint64_t inputPhy_buf,
                                        struct g2d_buf *d_buf, struct g2d_surface *d_surface,
                                        char *output_buf, uint64_t outputPhy_buf) {
    // just scale or csc
    s_buf->buf_paddr = inputPhy_buf;
    s_buf->buf_vaddr = input_buf;
    d_buf->buf_paddr = outputPhy_buf;
    d_buf->buf_vaddr = output_buf;

    s_surface->format = (g2d_format)gInput_format;
    s_surface->planes[0] = (long)s_buf->buf_paddr;
    s_surface->left = 0;
    s_surface->top = 0;
    s_surface->right = gWidth;
    s_surface->bottom = gHeight;
    s_surface->stride = gWidth;
    s_surface->width = gWidth;
    s_surface->height = gHeight;
    s_surface->rot = G2D_ROTATION_0;

    d_surface->format = (g2d_format)gOutput_format;
    d_surface->planes[0] = (long)d_buf->buf_paddr;
    d_surface->planes[1] = (long)d_buf->buf_paddr + gOutWidth * gOutHeight;
    d_surface->left = 0;
    d_surface->top = 0;
    d_surface->right = gOutWidth;
    d_surface->bottom = gOutHeight;
    d_surface->stride = gOutWidth;
    d_surface->width = gOutWidth;
    d_surface->height = gOutHeight;
    d_surface->rot = G2D_ROTATION_0;

    return 0;
}

static bool getDefaultG2DLib(char *libName, int size) {
    char value[PROPERTY_VALUE_MAX];

    if ((libName == NULL) || (size < (int)strlen(G2DENGINE) + (int)strlen(".so")))
        return false;

    memset(libName, 0, size);
    property_get("vendor.imx.default-g2d", value, "");
    if (strcmp(value, "") == 0) {
        ALOGI("No g2d lib available to be used!");
        return false;
    } else {
        strncpy(libName, G2DENGINE, strlen(G2DENGINE));
        strcat(libName, "-");
        strcat(libName, value);
        strcat(libName, ".so");
    }
    ALOGI("Default g2d lib: %s", libName);
    return true;
}

static void getModule(char *path, const char *name) {
    snprintf(path, PATH_MAX, "%s/%s", LIB_PATH1, name);
    if (access(path, R_OK) == 0)
        return;
    snprintf(path, PATH_MAX, "%s/%s", LIB_PATH2, name);
    if (access(path, R_OK) == 0)
        return;
    return;
}

static void initializeModule(void **G2dHandle, void **CLHandle) {
    char path[PATH_MAX] = {0};
    char g2dlibName[PATH_MAX] = {0};

    // open g2d module
    if (getDefaultG2DLib(g2dlibName, PATH_MAX)) {
        getModule(path, g2dlibName);
        *G2dHandle = dlopen(path, RTLD_NOW);
    }
    if ((*G2dHandle) != NULL) {
        mOpenEngine = (hwc_func1)dlsym(*G2dHandle, "g2d_open");
        mCloseEngine = (hwc_func1)dlsym(*G2dHandle, "g2d_close");
        mFinishEngine = (hwc_func1)dlsym(*G2dHandle, "g2d_finish");
        mCopyEngine = (hwc_func4)dlsym(*G2dHandle, "g2d_copy");
        mBlitEngine = (hwc_func3)dlsym(*G2dHandle, "g2d_blit");
        if (mOpenEngine(G2dHandle) != 0 || (*G2dHandle) == NULL) {
            *G2dHandle = NULL;
            ALOGE("Fail to open %s device!\n", path);
        }
    }

    // open cl module
    memset(path, 0, sizeof(path));
    getModule(path, CLENGINE);
    *CLHandle = dlopen(path, RTLD_NOW);
    if ((*CLHandle) != NULL) {
        mCLOpen = (hwc_func1)dlsym(*CLHandle, "cl_g2d_open");
        mCLClose = (hwc_func1)dlsym(*CLHandle, "cl_g2d_close");
        mCLFlush = (hwc_func1)dlsym(*CLHandle, "cl_g2d_flush");
        mCLFinish = (hwc_func1)dlsym(*CLHandle, "cl_g2d_finish");
        mCLBlit = (hwc_func3)dlsym(*CLHandle, "cl_g2d_blit");
        mCLCopy = (hwc_func4)dlsym(*CLHandle, "cl_g2d_copy");
        if (mCLOpen(CLHandle) != 0 || (*CLHandle) == NULL) {
            *CLHandle = NULL;
            ALOGE("Fail to open %s device!\n", path);
        }
    }
}

static void dumpOutPutBuffer(char *output_buf, const char *title) {
    if (gMemTest) {
        if (strcmp(title, "g2d") == 0) {
            dump_buffer(output_buf, gCopyLen > 256 ? 256 : gCopyLen, "g2d_output_cp");
        } else if (strcmp(title, "cl") == 0) {
            dump_buffer(output_buf, gCopyLen > 256 ? 256 : gCopyLen, "cl_output_cp");
        } else if (strcmp(title, "benchmark") == 0) {
            dump_buffer(output_buf, gCopyLen > 256 ? 256 : gCopyLen, "benchmark_output_cp");
        }
        return;
    }

    switch (gOutput_format) {
        case CL_G2D_YUYV:
            if (strcmp(title, "g2d") == 0) {
                dump_buffer(output_buf, 128, "g2d_output_yuyv");
            } else if (strcmp(title, "cl") == 0) {
                dump_buffer(output_buf, 128, "cl_output_yuyv");
            } else if (strcmp(title, "benchmark") == 0) {
                dump_buffer(output_buf, 128, "output_benchmark_yuyv");
            }
            break;

        case CL_G2D_NV12:
        case CL_G2D_NV21:
            if (strcmp(title, "g2d") == 0) {
                dump_buffer(output_buf, 64, "g2d_output_y");
                dump_buffer(output_buf + gOutWidth * gOutHeight, 64, "g2d_output_uv");
            } else if (strcmp(title, "cl") == 0) {
                dump_buffer(output_buf, 64, "cl_output_y");
                dump_buffer(output_buf + gOutWidth * gOutHeight, 64, "cl_output_uv");
            } else if (strcmp(title, "benchmark") == 0) {
                dump_buffer(output_buf, 64, "output_benchmark_y");
                dump_buffer(output_buf + gOutWidth * gOutHeight, 64, "benchmark_output_uv");
            }
            break;
        default:
            ALOGE("No supported output format to dump buffer: 0x%x.\n", gOutput_format);
    }
}

int createCLProgram(const char *fileSrcName, const char *fileBinName) {
    cl_int errNum;
    cl_uint numPlatforms;
    cl_platform_id firstPlatformId;
    cl_context context = NULL;
    cl_uint numDevices = 0;
    cl_device_id *devices = NULL;
    cl_device_id device;
    cl_device_id *program_devices = NULL;
    size_t *programBinarySizes = NULL;
    size_t deviceBufferSize = -1;
    unsigned char **programBinaries = NULL;
    cl_program program;
    size_t program_length;
    FILE *pSrcFileStream = NULL;
    char *source = NULL;
    int ret = 0;
    long int res;

    errNum = clGetPlatformIDs(1, &firstPlatformId, &numPlatforms);
    if (errNum != CL_SUCCESS || numPlatforms <= 0) {
        ALOGE("Failed to find any OpenCL platforms.\n");
        return -1;
    }

    cl_context_properties contextProperties[] = {CL_CONTEXT_PLATFORM,
                                                 (cl_context_properties)firstPlatformId, 0};
    context = clCreateContextFromType(contextProperties, CL_DEVICE_TYPE_GPU, NULL, NULL, &errNum);
    if (errNum != CL_SUCCESS) {
        ALOGE("Could not create GPU context, trying CPU...\n");
        context =
                clCreateContextFromType(contextProperties, CL_DEVICE_TYPE_CPU, NULL, NULL, &errNum);
        if (errNum != CL_SUCCESS) {
            ALOGE("Failed to create an OpenCL GPU or CPU context.\n");
            return -1;
        }
    }
    // First get the size of the devices buffer
    errNum = clGetContextInfo(context, CL_CONTEXT_DEVICES, 0, NULL, &deviceBufferSize);
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

    errNum = clGetContextInfo(context, CL_CONTEXT_DEVICES, deviceBufferSize, devices, NULL);
    if (errNum != CL_SUCCESS) {
        ALOGE("Failed to get device IDs\n");
        return -1;
    }
    if (devices != NULL) {
        device = devices[0];
    }

    pSrcFileStream = fopen(fileSrcName, "rb");
    if (pSrcFileStream == 0) {
        ALOGE("Failed to open file %s for reading\n", fileSrcName);
        ret = -1;
        goto binary_out;
    }

    // get the length of the source code
    fseek(pSrcFileStream, 0, SEEK_END);
    res = ftell(pSrcFileStream);
    if (res == -1) {
        fclose(pSrcFileStream);
        ALOGE("Failed to return the file position for the file %s\n", fileSrcName);
        ret = -1;
        goto binary_out;
    }
    program_length = res;
    fseek(pSrcFileStream, 0, SEEK_SET);

    // allocate a buffer for the source code string and read it in
    source = (char *)malloc(program_length + 1);
    if (fread((source), program_length, 1, pSrcFileStream) != 1) {
        fclose(pSrcFileStream);
        free(source);
        ALOGE("Failed to open file %s for reading\n", fileSrcName);
        ret = -1;
        goto binary_out;
    }
    fclose(pSrcFileStream);
    source[program_length] = '\0';

    program = clCreateProgramWithSource(context, 1, (const char **)&source, NULL, NULL);
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
        clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, sizeof(buildLog), buildLog,
                              NULL);
        ALOGE("Error in build kernel:\n");
        ALOGE("%s", buildLog);
        clReleaseProgram(program);
        ret = -1;
        goto binary_out;
    }
    errNum = clGetProgramInfo(program, CL_PROGRAM_NUM_DEVICES, sizeof(cl_uint), &numDevices, NULL);
    if (errNum != CL_SUCCESS) {
        ALOGE("Error querying for number of devices.");
        ret = -1;
        goto binary_out;
    }

    // 2 - Get all of the Device IDs
    program_devices = new cl_device_id[numDevices];
    errNum = clGetProgramInfo(program, CL_PROGRAM_DEVICES, sizeof(cl_device_id) * numDevices,
                              program_devices, NULL);
    if (errNum != CL_SUCCESS) {
        ALOGE("Error querying for devices.");
        ret = -1;
        goto binary_out;
    }

    programBinarySizes = new size_t[numDevices];
    errNum = clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t) * numDevices,
                              programBinarySizes, NULL);
    if (errNum != CL_SUCCESS) {
        ALOGE("Error querying for program binary sizes.");
        ret = -1;
        goto binary_out;
    }

    programBinaries = new unsigned char *[numDevices];
    for (cl_uint i = 0; i < numDevices; i++) {
        programBinaries[i] = new unsigned char[programBinarySizes[i]];
    }
    errNum = clGetProgramInfo(program, CL_PROGRAM_BINARIES, sizeof(unsigned char *) * numDevices,
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
            if (fp == NULL) {
                ALOGE("%s: open %s fialed", __func__, fileBinName);
                ret = -1;
                goto binary_out;
            }
            fwrite(programBinaries[i], 1, programBinarySizes[i], fp);
            fclose(fp);
            break;
        }
    }

binary_out:
    if (devices != NULL)
        delete[] devices;
    if (program_devices != NULL)
        delete[] program_devices;
    if (programBinarySizes != NULL)
        delete[] programBinarySizes;
    for (cl_uint i = 0; i < numDevices; i++) {
        if (programBinaries != NULL)
            delete[] programBinaries[i];
    }
    if (programBinaries != NULL)
        delete[] programBinaries;

    if (pSrcFileStream == 0)
        fclose(pSrcFileStream);
    if (program != NULL)
        clReleaseProgram(program);
    if (context != NULL)
        clReleaseContext(context);
    return ret;
}

int yuv422iResize(uint8_t *srcBuf, int srcWidth, int srcHeight, uint8_t *dstBuf, int dstWidth,
                  int dstHeight) {
    int i, j;
    int h_offset;
    int v_offset;
    unsigned char *ptr, cc;
    int h_scale_ratio;
    int v_scale_ratio;

    int srcStride;
    int dstStride;

    if (!srcWidth || !srcHeight || !dstWidth || !dstHeight)
        return -1;

    h_scale_ratio = srcWidth / dstWidth;
    v_scale_ratio = srcHeight / dstHeight;

    if ((h_scale_ratio > 0) && (v_scale_ratio > 0))
        goto reduce;
    else if (h_scale_ratio + v_scale_ratio <= 1)
        goto enlarge;

    ALOGE("%s, not support resize %dx%d to %dx%d", __func__, srcWidth, srcHeight, dstWidth,
          dstHeight);

    return -1;

reduce:
    h_offset = (srcWidth - dstWidth * h_scale_ratio) / 2;
    v_offset = (srcHeight - dstHeight * v_scale_ratio) / 2;

    srcStride = srcWidth * 2;
    dstStride = dstWidth * 2;

    // for Y
    for (i = 0; i < dstHeight * v_scale_ratio; i += v_scale_ratio) {
        for (j = 0; j < dstStride * h_scale_ratio; j += 2 * h_scale_ratio) {
            ptr = srcBuf + i * srcStride + j + v_offset * srcStride + h_offset * 2;
            cc = ptr[0];

            ptr = dstBuf + (i / v_scale_ratio) * dstStride + (j / h_scale_ratio);
            ptr[0] = cc;
        }
    }

    // for U
    for (i = 0; i < dstHeight * v_scale_ratio; i += v_scale_ratio) {
        for (j = 0; j < dstStride * h_scale_ratio; j += 4 * h_scale_ratio) {
            ptr = srcBuf + 1 + i * srcStride + j + v_offset * srcStride + h_offset * 2;
            cc = ptr[0];

            ptr = dstBuf + 1 + (i / v_scale_ratio) * dstStride + (j / h_scale_ratio);
            ptr[0] = cc;
        }
    }

    // for V
    for (i = 0; i < dstHeight * v_scale_ratio; i += v_scale_ratio) {
        for (j = 0; j < dstStride * h_scale_ratio; j += 4 * h_scale_ratio) {
            ptr = srcBuf + 3 + i * srcStride + j + v_offset * srcStride + h_offset * 2;
            cc = ptr[0];

            ptr = dstBuf + 3 + (i / v_scale_ratio) * dstStride + (j / h_scale_ratio);
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

    ALOGV("h_scale_ratio %d, v_scale_ratio %d, h_offset %d, v_offset %d, h_offset_end %d, "
          "v_offset_end %d",
          h_scale_ratio, v_scale_ratio, h_offset, v_offset, h_offset_end, v_offset_end);

    // for Y
    for (i = 0; i < dstHeight; i++) {
        // top, bottom black margin
        if ((i < v_offset) || (i >= v_offset_end)) {
            for (j = 0; j < dstWidth; j++) {
                dstBuf[dstStride * i + j * 2] = 0;
            }
            continue;
        }

        for (j = 0; j < dstWidth; j++) {
            // left, right black margin
            if ((j < h_offset) || (j >= h_offset_end)) {
                dstBuf[dstStride * i + j * 2] = 0;
                continue;
            }

            srcRow = (i - v_offset) / v_scale_ratio;
            srcCol = (j - h_offset) / h_scale_ratio;
            dstBuf[dstStride * i + j * 2] = srcBuf[srcStride * srcRow + srcCol * 2];
        }
    }

    // for UV
    for (i = 0; i < dstHeight; i++) {
        // top, bottom black margin
        if ((i < v_offset) || (i >= v_offset_end)) {
            for (j = 0; j < dstWidth; j++) {
                dstBuf[dstStride * i + j * 2 + 1] = 128;
            }
            continue;
        }

        for (j = 0; j < dstWidth; j++) {
            // left, right black margin
            if ((j < h_offset) || (j >= h_offset_end)) {
                dstBuf[dstStride * i + j * 2 + 1] = 128;
                continue;
            }

            srcRow = (i - v_offset) / v_scale_ratio;
            srcCol = (j - h_offset) / h_scale_ratio;
            dstBuf[dstStride * i + j * 2 + 1] = srcBuf[srcStride * srcRow + srcCol * 2 + 1];
        }
    }

    return 0;
}

struct testPhyBuffer {
    void *mVirtAddr;
    uint64_t mPhyAddr;
    size_t mSize;
    int32_t mFd;
};

int AllocPhyBuffer(struct testPhyBuffer *phyBufs, bool bCached) {
    int sharedFd;
    uint64_t phyAddr;
    uint64_t outPtr;
    uint32_t ionSize;
    uint32_t flag;

    if (phyBufs == NULL)
        return -1;

    ionSize = phyBufs->mSize;
    fsl::Allocator *allocator = fsl::Allocator::getInstance();
    if (allocator == NULL) {
        printf("%s ion allocator invalid\n", __func__);
        return -1;
    }

    flag = fsl::MFLAGS_CONTIGUOUS;
    if (bCached) {
        flag |= fsl::MFLAGS_CACHEABLE;
    }
    sharedFd = allocator->allocMemory(ionSize, MEM_ALIGN, flag);
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
        munmap((void *)(uintptr_t)outPtr, ionSize);
        close(sharedFd);
        return -1;
    }

    printf("%s, outPtr:%p,  phy:%p, virt: %p, ionSize:%d\n", __func__, (void *)outPtr,
           (void *)phyAddr, (void *)outPtr, ionSize);

    phyBufs->mVirtAddr = (void *)outPtr;
    phyBufs->mPhyAddr = phyAddr;
    phyBufs->mFd = sharedFd;

    return 0;
}

int FreePhyBuffer(struct testPhyBuffer *phyBufs) {
    if (phyBufs == NULL)
        return -1;

    /* If already freed or never allocated, just return */
    if (phyBufs->mVirtAddr == NULL)
        return 0;

    munmap(phyBufs->mVirtAddr, phyBufs->mSize);

    if (phyBufs->mFd > 0)
        close(phyBufs->mFd);

    memset(phyBufs, 0, sizeof(struct testPhyBuffer));

    return 0;
}

static struct testPhyBuffer InPhyBuffer[TEST_BUFFER_NUM];
static bool g_use_v4l2_buffer = false;
static int g_fd_v4l = -1;
static char *g_v4l_device = "/dev/video1";
int g_out_width = 1920;
int g_out_height = 1080;
int g_cap_fmt = V4L2_PIX_FMT_YUYV;
int g_capture_mode = 0;
int g_camera_framerate = 30;
enum v4l2_buf_type g_buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
int g_mem_type = V4L2_MEMORY_MMAP;

static int InitV4l2() {
    if ((g_fd_v4l = open(g_v4l_device, O_RDWR, 0)) < 0) {
        printf("unable to open %s for capture device.\n", g_v4l_device);
        return -1;
    }

    struct v4l2_format fmt;
    struct v4l2_streamparm parm;

    // set fps
    memset(&parm, 0, sizeof(parm));
    parm.type = g_buf_type;
    parm.parm.capture.capturemode = g_capture_mode;
    parm.parm.capture.timeperframe.denominator = g_camera_framerate;
    parm.parm.capture.timeperframe.numerator = 1;
    if (ioctl(g_fd_v4l, VIDIOC_S_PARM, &parm) < 0) {
        printf("VIDIOC_S_PARM failed\n");
        goto fail;
    }

    // set size, format
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = g_buf_type;

    if (g_buf_type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
        fmt.fmt.pix.pixelformat = g_cap_fmt;
        fmt.fmt.pix.width = g_out_width;
        fmt.fmt.pix.height = g_out_height;
    } else {
        fmt.fmt.pix_mp.pixelformat = g_cap_fmt;
        fmt.fmt.pix_mp.width = g_out_width;
        fmt.fmt.pix_mp.height = g_out_height;
        fmt.fmt.pix_mp.num_planes = 1;
    }

    if (ioctl(g_fd_v4l, VIDIOC_S_FMT, &fmt) < 0) {
        printf("set format failed\n");
        goto fail;
    }

    return 0;

fail:
    if (g_fd_v4l > 0)
        close(g_fd_v4l);

    return -1;
}

static int ExitV4l2() {
    if (g_fd_v4l > 0)
        close(g_fd_v4l);

    return 0;
}

static int AllocV4l2Buffers() {
    unsigned int i;
    struct v4l2_buffer buf;
    enum v4l2_buf_type type;
    struct v4l2_requestbuffers req;
    int ret;
    struct v4l2_plane planes;
    memset(&planes, 0, sizeof(struct v4l2_plane));

    memset(&req, 0, sizeof(req));
    req.count = TEST_BUFFER_NUM;
    req.type = g_buf_type;
    req.memory = g_mem_type;

    if (ioctl(g_fd_v4l, VIDIOC_REQBUFS, &req) < 0) {
        printf("VIDIOC_REQBUFS failed\n");
        return -1;
    }

    for (i = 0; i < TEST_BUFFER_NUM; i++) {
        memset(&buf, 0, sizeof(buf));
        buf.type = g_buf_type;
        buf.memory = g_mem_type;
        buf.index = i;

        if (ioctl(g_fd_v4l, VIDIOC_QUERYBUF, &buf) < 0) {
            printf("VIDIOC_QUERYBUF error\n");
            return -1;
        }

        InPhyBuffer[i].mSize = buf.length;
        InPhyBuffer[i].mVirtAddr =
                mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd_v4l, buf.m.offset);
    }

    return 0;
}

static int FreeV4l2Buffers() {
    struct v4l2_requestbuffers req;
    int ret;

    if (g_fd_v4l < 0)
        return -1;

    /* unmap memory */
    printf("unmap memory, g_mem_type %d, V4L2_MEMORY_MMAP %d, TEST_BUFFER_NUM %d\n", g_mem_type,
           V4L2_MEMORY_MMAP, TEST_BUFFER_NUM);
    for (int i = 0; i < TEST_BUFFER_NUM; i++) {
        if (InPhyBuffer[i].mVirtAddr) {
            printf("unmap %p, size %lu\n", InPhyBuffer[i].mVirtAddr, InPhyBuffer[i].mSize);
            munmap(InPhyBuffer[i].mVirtAddr, InPhyBuffer[i].mSize);
        }
    }

    memset(&req, 0, sizeof(req));
    req.count = 0;
    req.type = g_buf_type;
    req.memory = g_mem_type;

    ret = ioctl(g_fd_v4l, VIDIOC_REQBUFS, &req);

    return ret;
}

int main(int argc, char **argv) {
    int rt;
    int ret = 0;
    int inputlen = 0;
    int outputlen = 0;
    int read_len = 0;
    bool bInputMemCached = false;
    bool bOutputMemCached = false;

    void *input_buf = NULL;
    uint64_t inputPhy_buf = 0;
    void *output_buf = NULL;
    uint64_t outputPhy_buf = 0;

    void *output_benchmark_buf = NULL;

    struct cl_g2d_surface src, dst;
    void *CLHandle = NULL;

    struct g2d_buf s_buf, d_buf;
    struct g2d_surface s_surface, d_surface;
    void *G2dHandle = NULL;

    if (argc < 3) {
        usage(argv[0]);
        return 0;
    }

    while ((rt = getopt(argc, argv, "hbcl:i:s:o:d:w:g:t:m:n:x:y:z:v:p:")) >= 0) {
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
            case 'p':
                g_usePhyAddr = atoi(optarg);
                break;
            case 'l':
                gCopyLen = atoi(optarg);
                break;
            case 'i':
                memset(input_file, 0, MAX_FILE_LEN);
                if (strlen(optarg) >= MAX_FILE_LEN) {
                    ALOGE("input file name too long to process: %s", optarg);
                    return 0;
                }
                strncpy(input_file, optarg, strlen(optarg));
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
                gInputMemory_type = atoi(optarg);
                break;
            case 'n':
                gOutputMemory_type = atoi(optarg);
                break;
            case 'o':
                memset(output_file, 0, MAX_FILE_LEN);
                if (strlen(optarg) >= MAX_FILE_LEN) {
                    ALOGE("output file name too long to process: %s", optarg);
                    return 0;
                }
                strncpy(output_file, optarg, strlen(optarg));
                break;
            case 'v':
                g_v4l_device = optarg;
                g_use_v4l2_buffer = true;
                break;
            default:
                usage(argv[0]);
                return 0;
        }
    }

    if (g_use_v4l2_buffer)
        printf("g_v4l_device %s, gMemTest %d\n", g_v4l_device, gMemTest);

    printf("g_usePhyAddr %d\n", g_usePhyAddr);

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
        if (createCLProgram(input_file, output_file) == 0)
            ALOGI("Success on opencl 2d binary build!");
        else
            ALOGI("Fail on opencl 2d binary build!");
        return 0;
    }

    // Either copy or blit
    if (gMemTest && (gWidth != 0) && (gHeight != 0)) {
        usage(argv[0]);
        return 0;
    }

    if (!gMemTest && (gWidth == 0) && (gHeight == 0)) {
        usage(argv[0]);
        return 0;
    }

    ALOGI("Start 2d test with:");
    ALOGI("input file: %s", input_file);
    ALOGI("output file: %s", output_file);

    inputlen = get_file_len(input_file);
    if (inputlen <= 0 ||
        inputlen < get_buf_size(gInput_format, gWidth, gHeight, gMemTest, gCopyLen)) {
        ALOGE("No valid file %s for this test", input_file);
        goto clean;
    }
    // if no '-l' parameter, the default length is input file size
    gCopyLen = gCopyLen ? gCopyLen : inputlen;

    outputlen = get_buf_size(gOutput_format, gOutWidth, gOutHeight, gMemTest, gCopyLen);

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

    struct testPhyBuffer OutPhyBuffer[TEST_BUFFER_NUM];
    struct testPhyBuffer OutBenchMarkPhyBuffer;
    memset(&InPhyBuffer, 0, sizeof(InPhyBuffer));
    memset(&OutPhyBuffer, 0, sizeof(OutPhyBuffer));
    memset(&OutBenchMarkPhyBuffer, 0, sizeof(OutBenchMarkPhyBuffer));

    bInputMemCached = gInputMemory_type ? true : false;
    bOutputMemCached = gOutputMemory_type ? true : false;

    OutBenchMarkPhyBuffer.mSize = outputlen;
    AllocPhyBuffer(&OutBenchMarkPhyBuffer, bOutputMemCached);
    output_benchmark_buf = OutBenchMarkPhyBuffer.mVirtAddr;
    if (output_benchmark_buf == NULL) {
        ALOGE("Cannot allocate output buffer");
        goto clean;
    }
    memset(output_benchmark_buf, 0, outputlen);
    ALOGI("Get CPU output ptr %p", output_benchmark_buf);

    if (g_use_v4l2_buffer) {
        ret = InitV4l2();
        if (ret) {
            ALOGI("%s: InitV4l2 failed, ret %d", __func__, ret);
            goto clean;
        }

        ret = AllocV4l2Buffers();
        if (ret) {
            ALOGI("%s: AllocV4l2Buffers failed, ret %d", __func__, ret);
            goto clean;
        }
    } else {
        for (int i = 0; i < TEST_BUFFER_NUM; i++) {
            InPhyBuffer[i].mSize = inputlen;
            AllocPhyBuffer(&InPhyBuffer[i], bInputMemCached);
            if (InPhyBuffer[i].mVirtAddr == NULL) {
                ALOGE("Cannot allocate input buffer, i %d", i);
                goto clean;
            }
        }
    }

    for (int i = 0; i < TEST_BUFFER_NUM; i++) {
        OutPhyBuffer[i].mSize = outputlen;
        AllocPhyBuffer(&OutPhyBuffer[i], bOutputMemCached);
        if (OutPhyBuffer[i].mVirtAddr == NULL) {
            ALOGE("Cannot allocate output buffer, i %d", i);
            goto clean;
        }
    }

    initializeModule(&G2dHandle, &CLHandle);
    if (G2dHandle == NULL && CLHandle == NULL) {
        ALOGE("G2dHandle and CLHandle both NULL");
        goto clean;
    }

    uint64_t t1, t2;
    // g2d engine
    if (G2dHandle != NULL) {
        ALOGI("Start g2d engine blit, in size %d, out size %d", inputlen, outputlen);
        t1 = systemTime();
        for (int loop = 0; loop < G2D_TEST_LOOP; loop++) {
            int test_buffer_index = loop % TEST_BUFFER_NUM;
            ALOGV("loop %d, test_buffer_index %d", loop, test_buffer_index);
            input_buf = InPhyBuffer[test_buffer_index].mVirtAddr;
            inputPhy_buf = InPhyBuffer[test_buffer_index].mPhyAddr;
            ;
            output_buf = OutPhyBuffer[test_buffer_index].mVirtAddr;
            outputPhy_buf = OutPhyBuffer[test_buffer_index].mPhyAddr;

            read_len = read_from_file((char *)input_buf, inputlen, input_file);

            if (!gMemTest) {
                update_surface_parameters_2d(&s_buf, &s_surface, (char *)input_buf, inputPhy_buf,
                                             &d_buf, &d_surface, (char *)output_buf, outputPhy_buf);
                ALOGV("call g2d_blit");
                mBlitEngine(G2dHandle, &s_surface, &d_surface);
            } else {
                s_buf.buf_paddr = inputPhy_buf;
                s_buf.buf_vaddr = input_buf;
                d_buf.buf_paddr = outputPhy_buf;
                d_buf.buf_vaddr = output_buf;

                ALOGV("call g2d_copy");
                mCopyEngine(G2dHandle, &d_buf, &s_buf, (void *)(intptr_t)gCopyLen);
            }
            mFinishEngine(G2dHandle);
        }
        t2 = systemTime();
        ALOGI("End g2d engine blit, %d loops use %lld ns, average %lld ns per loop", G2D_TEST_LOOP,
              t2 - t1, (t2 - t1) / G2D_TEST_LOOP);

        dump_buffer((char *)input_buf, gCopyLen > 256 ? 256 : gCopyLen, "g2d_input");
        dumpOutPutBuffer((char *)output_buf, "g2d");

        memset(output_2d_file, 0, MAX_FILE_LEN);
        strncpy(output_2d_file, output_file, strlen(output_file));
        strcat(output_2d_file, "_2d");
        write_from_file((char *)output_buf, outputlen, output_2d_file);
    }

    // cl engine
    if (CLHandle != NULL) {
        ALOGI("Start CL engine blit, in size %d, out size %d", inputlen, outputlen);
        memset(&src, 0, sizeof(src));
        memset(&dst, 0, sizeof(dst));

        t1 = systemTime();
        for (int loop = 0; loop < G2D_TEST_LOOP; loop++) {
            int test_buffer_index = loop % TEST_BUFFER_NUM;
            input_buf = InPhyBuffer[test_buffer_index].mVirtAddr;
            inputPhy_buf = InPhyBuffer[test_buffer_index].mPhyAddr;

            output_buf = OutPhyBuffer[test_buffer_index].mVirtAddr;
            outputPhy_buf = OutPhyBuffer[test_buffer_index].mPhyAddr;

            ALOGV("loop %d, test_buffer_index %d, inVirt %p, inPhy 0x%llx, outVirt %p, outPhy 0x%llx, inputlen %d",
                  loop, test_buffer_index, input_buf, inputPhy_buf, output_buf, outputPhy_buf,
                  inputlen);

            read_len = read_from_file((char *)input_buf, inputlen, input_file);

            if (!gMemTest) {
                if (g_usePhyAddr)
                    update_surface_parameters(&src, (char *)inputPhy_buf, &dst,
                                              (char *)outputPhy_buf, true);
                else
                    update_surface_parameters(&src, (char *)input_buf, &dst, (char *)output_buf,
                                              false);

                ALOGV("call cl_g2d_blit");
                mCLBlit(CLHandle, &src, &dst);

            } else {
                struct cl_g2d_buf cl_output_buf;
                struct cl_g2d_buf cl_input_buf;

                cl_output_buf.buf_vaddr = output_buf;
                cl_output_buf.buf_paddr = outputPhy_buf;
                cl_output_buf.buf_size = gCopyLen;
                cl_output_buf.use_phy = g_usePhyAddr;

                cl_input_buf.buf_vaddr = input_buf;
                cl_input_buf.buf_paddr = inputPhy_buf;
                cl_input_buf.buf_size = gCopyLen;
                cl_input_buf.use_phy = g_usePhyAddr;

                if (gInputMemory_type == 0) {
                    cl_input_buf.usage = CL_G2D_UNCACHED_MEMORY;
                } else {
                    cl_input_buf.usage = CL_G2D_CACHED_MEMORY;
                }
                if (gOutputMemory_type == 0) {
                    cl_output_buf.usage = CL_G2D_UNCACHED_MEMORY;
                } else {
                    cl_output_buf.usage = CL_G2D_CACHED_MEMORY;
                }
                ALOGV("call cl_g2d_copy");
                mCLCopy(CLHandle, &cl_output_buf, &cl_input_buf, (void *)(intptr_t)gCopyLen);
            }
            mCLFlush(CLHandle);
            mCLFinish(CLHandle);
        }
        t2 = systemTime();
        ALOGI("End CL engine blit, %d loops use %lld ns, average %lld ns per loop, g_usePhyAddr "
              "%d, input cached %d, output cached %d",
              G2D_TEST_LOOP, t2 - t1, (t2 - t1) / G2D_TEST_LOOP, g_usePhyAddr, gInputMemory_type,
              gOutputMemory_type);

        dump_buffer((char *)input_buf, gCopyLen > 256 ? 256 : gCopyLen, "cl_input");
        dumpOutPutBuffer((char *)output_buf, "cl");

        memset(output_cl_file, 0, MAX_FILE_LEN);
        strncpy(output_cl_file, output_file, strlen(output_file));
        strcat(output_cl_file, "_cl");
        write_from_file((char *)output_buf, outputlen, output_cl_file);
    }

    // cpu engine
    ALOGI("Start CPU 2d blit, in size %d, out size %d", inputlen, outputlen);
    t1 = systemTime();
    for (int loop = 0; loop < G2D_TEST_LOOP; loop++) {
        if (!gMemTest) {
            if ((gInput_format == CL_G2D_YUYV) && (gOutput_format == CL_G2D_NV12)) {
                convertYUYVtoNV12SP((uint8_t *)input_buf, (uint8_t *)output_benchmark_buf,
                                    gOutWidth, gOutHeight);
            } else if ((gInput_format == CL_G2D_YUYV) && (gOutput_format == CL_G2D_YUYV)) {
                if ((gWidth == gOutWidth) && (gHeight == gOutHeight))
                    YUYVCopyByLine((uint8_t *)output_benchmark_buf, gOutWidth, gOutHeight,
                                   (uint8_t *)input_buf, gWidth, gHeight);
                else
                    yuv422iResize((uint8_t *)input_buf, gWidth, gHeight,
                                  (uint8_t *)output_benchmark_buf, gOutWidth, gOutHeight);
            } else if ((gInput_format == CL_G2D_NV12) && (gOutput_format == CL_G2D_NV21)) {
                convertNV12toNV21((uint8_t *)output_benchmark_buf, gOutWidth, gOutHeight,
                                  (uint8_t *)input_buf);
            } else if ((gInput_format == CL_G2D_NV12) && (gOutput_format == CL_G2D_I420) &&
                       (gWidth == gOutWidth) && (gHeight == gOutHeight)) {
                uint8_t *nv12_y = (uint8_t *)input_buf;
                uint8_t *nv12_uv = (uint8_t *)input_buf + gWidth * gHeight;
                int nv12_y_stride = gWidth;
                int nv12_uv_stride = gWidth;

                libyuv::NV12ToI420(nv12_y, nv12_y_stride, nv12_uv, nv12_uv_stride,
                                   (uint8_t *)output_benchmark_buf, gWidth,
                                   (uint8_t *)output_benchmark_buf + gWidth * gHeight, gWidth / 2,
                                   (uint8_t *)output_benchmark_buf + gWidth * gHeight * 5 / 4,
                                   gWidth / 2, gWidth, gHeight);
            } else {
                ALOGW("unsupported by CPU blit, gInput_format %d, gOutput_format %d", gInput_format,
                      gOutput_format);
            }
        } else {
            memcpy(output_benchmark_buf, input_buf, gCopyLen);
        }
    }
    t2 = systemTime();
    ALOGI("End CPU 2d blit, %d loops use %lld ns, average %lld ns per loop", G2D_TEST_LOOP, t2 - t1,
          (t2 - t1) / G2D_TEST_LOOP);
    dumpOutPutBuffer((char *)output_benchmark_buf, "benchmark");

    memset(output_benchmark_file, 0, MAX_FILE_LEN);
    strncpy(output_benchmark_file, output_file, strlen(output_file));
    strcat(output_benchmark_file, "_benchmark");
    write_from_file((char *)output_benchmark_buf, outputlen, output_benchmark_file);

clean:
    if (g_use_v4l2_buffer) {
        FreeV4l2Buffers();
        ExitV4l2();
    } else {
        for (int i = 0; i < TEST_BUFFER_NUM; i++) {
            FreePhyBuffer(&InPhyBuffer[i]);
        }
    }

    for (int i = 0; i < TEST_BUFFER_NUM; i++) {
        FreePhyBuffer(&OutPhyBuffer[i]);
    }

    FreePhyBuffer(&OutBenchMarkPhyBuffer);

    if (G2dHandle != NULL) {
        mCloseEngine(G2dHandle);
    }
    if (CLHandle != NULL) {
        mCLClose(CLHandle);
    }

    return 0;
}
