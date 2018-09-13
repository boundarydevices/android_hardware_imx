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
#include <linux/ion.h>

#include <CL/opencl.h>
#ifdef BUILD_FOR_ANDROID
#include <cutils/log.h>
#endif

#include "opencl-2d.h"

#define LOG_TAG "opencl-2d-test"
#define DEBUG 1
#define MAX_FILE_LEN 128
#define G2D_TEST_LOOP 10

static char input_file[MAX_FILE_LEN];
static char output_file[MAX_FILE_LEN];
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
    fd = open(filename, O_RDWR, 0666);
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
static void dump_buffer(char *pbuf, int count, char *title)
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

//default ion heap
#define ION_BUFFER_HEAP 1
//64bit buffer alignment
#define ION_BUFFER_ALIGN 8

static void * allocate_memory(ion_user_handle_t *ion_hnd,
        int *ion_buf_fd,int size)
{
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
        int err = ion_alloc(gIonFd,
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
        ALOGE("Unsupport input format\n");
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
        break;
    case CL_G2D_YUYV:
        dst->planes[0] = (long)output_buf;
        break;
    default:
        ALOGE("Unsupport input format\n");
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

int main(int argc, char** argv)
{
    int fd, err, rt;
    int inputlen = 0;
    int outputlen = 0;
    int read_len = 0;

    char *input_buf = NULL;
    ion_user_handle_t input_ion_hnd = 0;
    int input_ion_buf_fd = 0;

    char *output_buf = NULL;
    ion_user_handle_t output_ion_hnd = 0;
    int output_ion_buf_fd = 0;

    char *output_benchmark_buf = NULL;
    ion_user_handle_t benchmark_ion_hnd = 0;
    int benchmark_ion_buf_fd = 0;;

    struct cl_g2d_surface src,dst;
    void *g2dHandle = NULL;

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
    input_buf  = (char *)allocate_memory(&input_ion_hnd, &input_ion_buf_fd,
            inputlen);
    if(input_buf  == NULL) {
        ALOGE("Cannot allocate input buffer");
        goto clean;
    }
    read_len = read_from_file(input_buf, inputlen, input_file);
    dump_buffer(input_buf, 64, "input");

    outputlen = get_buf_size(gOutput_format, gOutWidth, gOutHeight, gMemTest, gCopyLen);
    output_buf  = (char *)allocate_memory(&output_ion_hnd, &output_ion_buf_fd,
            outputlen);
    output_benchmark_buf  = (char *)allocate_memory(&benchmark_ion_hnd, &benchmark_ion_buf_fd,
            outputlen);
    if((output_buf  == NULL)||(output_benchmark_buf == NULL)) {
        ALOGE("Cannot allocate output buffer");
        goto clean;
    }
    memset(output_buf, 0, outputlen);
    memset(output_benchmark_buf, 0, outputlen);

    if(cl_g2d_open(&g2dHandle) == -1 || g2dHandle == NULL) {
        ALOGE("Fail to open g2d device!\n");
        goto clean;
    }

    for(int loop = 0; loop < G2D_TEST_LOOP; loop ++) {
        ALOGI("Start openCL 2d blit at loop %d", loop);
        if (!gMemTest) {
            update_surface_parameters(&src, input_buf,
                &dst, output_buf);

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
            cl_g2d_copy(g2dHandle, &g2d_output_buf,
                    &g2d_input_buf, (unsigned int)gCopyLen);
        }
        cl_g2d_flush(g2dHandle);
        cl_g2d_finish(g2dHandle);
        ALOGI("End openCL 2d blit at loop %d", loop);
    }

    ALOGI("Start CPU 2d blit");
    if (!gMemTest) {
        if ((src.format == CL_G2D_YUYV) && (dst.format == CL_G2D_NV12)) {
            convertYUYVtoNV12SP((uint8_t *)input_buf, (uint8_t *)output_benchmark_buf,
                    gOutWidth, gOutHeight);
        }
        else if ((src.format == CL_G2D_YUYV) && (dst.format == CL_G2D_YUYV)) {
            YUYVCopyByLine((uint8_t *)output_benchmark_buf, gOutWidth, gOutHeight,
                (uint8_t *)input_buf, gWidth, gHeight);
        }
        else if ((src.format == CL_G2D_NV12) && (dst.format == CL_G2D_NV21)) {
            convertNV12toNV21((uint8_t *)output_benchmark_buf, gOutWidth, gOutHeight,
                (uint8_t *)input_buf);
        }
    } else {
        memcpy(output_benchmark_buf, input_buf, gCopyLen);
    }
    ALOGI("End CPU 2d blit");

    if (!gMemTest) {
        if (dst.format == CL_G2D_YUYV) {
            dump_buffer(output_buf, 128, "output_yuyv");
            dump_buffer(output_benchmark_buf, 128, "output_benchmark_yuyv");
        }
        else if (dst.format == CL_G2D_NV12) {
            dump_buffer(output_buf, 64, "output_y");
            dump_buffer(output_buf + gOutWidth*gOutHeight, 64, "output_uv");
            dump_buffer(output_benchmark_buf, 64, "output_benchmark_y");
            dump_buffer(output_benchmark_buf + gOutWidth*gOutHeight, 64, "output_benchmark_uv");
        }
        else if (dst.format == CL_G2D_NV21) {
            dump_buffer(output_buf, 64, "output_y");
            dump_buffer(output_buf + gOutWidth*gOutHeight, 64, "output_uv");
            dump_buffer(output_benchmark_buf, 64, "output_benchmark_y");
            dump_buffer(output_benchmark_buf + gOutWidth*gOutHeight, 64, "output_benchmark_uv");
        }
    } else {
        dump_buffer(output_buf, gCopyLen>256?256:gCopyLen, "output");
        dump_buffer(output_benchmark_buf, gCopyLen>256?256:gCopyLen, "output_benchmark");
    }

    write_from_file(output_buf, outputlen, output_file);
    strncpy(output_benchmark_file, output_file, strlen(output_file));
    strcat(output_benchmark_file, "_benchmark");
    write_from_file(output_benchmark_buf, outputlen, output_benchmark_file);

clean:
    if(input_buf  == NULL)
        free_memory(input_buf, input_ion_hnd,
                input_ion_buf_fd, inputlen);
    if(output_buf  == NULL)
        free_memory(output_buf, output_ion_hnd,
                output_ion_buf_fd, outputlen);
    if(output_benchmark_buf  == NULL)
        free_memory(output_benchmark_buf, benchmark_ion_hnd,
                benchmark_ion_buf_fd, outputlen);
    if(g2dHandle  == NULL)
        cl_g2d_close(g2dHandle);
    if(gIonFd == 0)
        ion_close(gIonFd);

    return 0;
}
