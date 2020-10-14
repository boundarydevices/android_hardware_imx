/*
 * Copyright 2019 NXP.
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
#include <stdlib.h>
#include <error.h>
#include <errno.h>
#include <memory.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <log/log.h>
#include <inttypes.h>
#include <png.h>

#include "assert.h"

#include "FakeCapture.h"

#define COLOR_HEIGHT 64
#define COLOR_VALUE 0x86
#define CAMERA_WIDTH 1920
#define CAMERA_HEIGHT 1024
//#define DEBUG_FAKE_CAMERA

#ifdef DEBUG_FAKE_CAMERA
static int nub;
#endif
FakeCapture::FakeCapture(const char *deviceName, const camera_metadata_t * metadata)
    : EvsCamera(deviceName, metadata)
{
    mWidth = CAMERA_WIDTH;
    mHeight = CAMERA_HEIGHT;
    mIslogicCamera = isLogicalCamera(metadata);
    // the logic camera have phsical camera inside, it need allocate buffer according phsical camera
    if (mIslogicCamera)
        mPhysicalCamera = getPhysicalCameraInLogic(metadata);
    else
        mPhysicalCamera.emplace(deviceName);
}

FakeCapture::~FakeCapture()
{
}

// open v4l2 device.
bool FakeCapture::onOpen(const char* /*deviceName*/)
{
    ALOGI("Current output format: fmt=0x%X, %dx%d", mFormat, mWidth, mHeight);
    // if logic camera have four cameras, the buffer fd will been 100-103
    // the type of mCamBuffers as below, buffer fd is the first parameter of unordered_map
    // std::unordered_map<int, std::vector<fsl::Memory*>>
    int fd_init = 100;

    std::unique_lock <std::mutex> lock(mLock);
    // Make sure we're initialized to the STOPPED state
    mRunMode = STOPPED;
    for (const auto& physical_cam : mPhysicalCamera) {
        mDeviceFd[physical_cam] = fd_init++;
    }

    // Ready to go!
    return true;
}

bool FakeCapture::isOpen()
{
    return true;
}

void FakeCapture::onClose()
{
    ALOGD("FakeCapture::close");

    {
        std::unique_lock <std::mutex> lock(mLock);
        // Stream should be stopped first!
        assert(mRunMode == STOPPED);
        for (const auto& physical_cam : mPhysicalCamera) {
            mDeviceFd[physical_cam] = 0;
        }
    }
}

bool FakeCapture::onStart()
{
    int fd = -1;
    fsl::Memory *buffer = nullptr;
    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            continue;
        {
            std::unique_lock <std::mutex> lock(mLock);
            fd = mDeviceFd[physical_cam];
        }
        if (fd < 0) {
            // The device is not opened.
            ALOGE("%s device not opened", __func__);
            continue;
        }

        for (int i = 0; i < CAMERA_BUFFER_NUM; i++) {
            {
                std::unique_lock <std::mutex> lock(mLock);
                buffer = mCamBuffers[mDeviceFd[physical_cam]].at(i);
            }
            if (buffer == nullptr) {
                 ALOGE("%s buffer not ready!", __func__);
                 continue;
            }
        }
    }
    ALOGD("Stream started.");
    return true;
}


void FakeCapture::onStop()
{
    fsl::Memory *buffer = nullptr;
    int fd = -1;
    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            continue;
        {
            std::unique_lock <std::mutex> lock(mLock);
            fd = mDeviceFd[physical_cam];
        }
        if (fd < 0) {
            // The device is not opened.
            ALOGE("%s device not opened", __func__);
            continue;
        }

        for (int i = 0; i < CAMERA_BUFFER_NUM; i++) {
            {
                std::unique_lock <std::mutex> lock(mLock);
                buffer = mCamBuffers[mDeviceFd[physical_cam]].at(i);
            }
            if (buffer == nullptr) {
                 ALOGE("%s buffer not ready!", __func__);
                 continue;
            }
        }
    }
    ALOGD("Stream stopped.");
}

bool FakeCapture::onFrameReturn(int index, __attribute__ ((unused))std::string deviceid)
{
    int fd = -1;
    // We're giving the frame back to the system, so clear the "ready" flag
    std::string devicename = deviceid;
    if (index < 0 || index >= CAMERA_BUFFER_NUM) {
        ALOGE("%s invalid index:%d", __func__, index);
        return false;
    }

    if (devicename == "" && mPhysicalCamera.size() == 1) {
        devicename = *mPhysicalCamera.begin();
    }

    fsl::Memory *buffer = nullptr;
    {
        std::unique_lock <std::mutex> lock(mLock);
        fd = mDeviceFd[devicename];
        buffer = mCamBuffers[mDeviceFd[devicename]].at(index);
    }

    if (fd < 0 || buffer == nullptr) {
        ALOGE("%s invalid buffer", __func__);
        return false;
    }

    return true;
}

// This runs on a background thread to receive and dispatch video frames
void FakeCapture::onFrameCollect(std::vector<struct forwardframe> &frames)
{
    struct forwardframe frame;
    fsl::Memory *buffer = nullptr;
    usleep(33333);//30fps
#ifdef DEBUG_FAKE_CAMERA
    int index = 0;
#endif

    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            continue;
        {
            std::unique_lock <std::mutex> lock(mLock);
            buffer = mCamBuffers[mDeviceFd[physical_cam]].at(0);
        }
        if (buffer == nullptr || buffer->base == 0) {
            ALOGE("%s invalid buffer d ", __func__);
            return;
         }

#ifdef DEBUG_FAKE_CAMERA
         char filename[128];
         memset(filename, 0, 128);
         if (nub%20 ==0) {
             sprintf(filename, "/data/%s-frame_out-%d-%d.rgb",
                       "EVS", nub, index);
             int fd = 0;
             int len = 0;
             fd = open(filename, O_CREAT | O_RDWR, 0666);

             if (fd<0) {
                 ALOGE("failed to open");
             }

             len = write(fd, (void *)buffer->base, buffer->stride * buffer->height  * 4);
             close(fd);

         }
         index++;
#endif

        frame.buf = buffer;
        frame.index = 0;
        frame.deviceid = physical_cam;
        frames.push_back(frame);
    }

#ifdef DEBUG_FAKE_CAMERA
    nub++;
#endif
    return;
}

int FakeCapture::getParameter(__attribute__((unused))v4l2_control& control) {
    return 0;
}

int FakeCapture::setParameter(__attribute__((unused))v4l2_control& control) {
    return 0;
}

std::set<uint32_t> FakeCapture::enumerateCameraControls() {
    std::set<uint32_t> ctrlIDs;
    return std::move(ctrlIDs);
}

void FakeCapture::onMemoryCreate() {
    std::vector<fsl::Memory*> fsl_mem;
    fsl::Memory *buffer = nullptr;
    fsl::MemoryManager* allocator = fsl::MemoryManager::getInstance();
    fsl::MemoryDesc desc;
    desc.mWidth = mWidth;
    desc.mHeight = mHeight;
    desc.mFormat = fsl::FORMAT_RGBA8888;
    desc.mFslFormat = fsl::FORMAT_RGBA8888;
    desc.mProduceUsage |= fsl::USAGE_HW_TEXTURE
        | fsl::USAGE_HW_RENDER | fsl::USAGE_HW_VIDEO_ENCODER;
    desc.mFlag = 0;
    int ret = desc.checkFormat();
    if (ret != 0) {
        ALOGE("%s checkFormat failed", __func__);
        return;
    }
    // allocate CAMERA_BUFFER_NUM buffer for every physical camera
    int cam_nu = 0;
    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            return;
        fsl_mem.clear();
        for (int i = 0; i < CAMERA_BUFFER_NUM; i++) {
            buffer = nullptr;
            void *vaddr = NULL;
            char filename[128];
            memset(filename, 0, 128);
            sprintf(filename, "/vendor/etc/automotive/sv/cam%d.png", cam_nu);
            allocator->allocMemory(desc, &buffer);
            std::unique_lock <std::mutex> lock(mLock);
            allocator->lock(buffer,  buffer->usage |  fsl::USAGE_SW_READ_OFTEN
                           | fsl::USAGE_SW_WRITE_OFTEN, 0, 0,
                            buffer->width, buffer->height, &vaddr);
            readFromPng(filename, vaddr);
            fsl_mem.push_back(buffer);
        }
        cam_nu++;
        mCamBuffers[mDeviceFd[physical_cam]] = fsl_mem;
    }
}

void FakeCapture::readFromPng(const char *filename, void* buf) {
    // Open the PNG file
    FILE *inputFile = fopen(filename, "rb");
    unsigned char *buff = (unsigned char *)buf;
    if (inputFile == 0)
    {
        perror(filename);
        return;
    }

    // Read the file header and validate that it is a PNG
    static const int kSigSize = 8;
    png_byte header[kSigSize] = {0};
    fread(header, 1, kSigSize, inputFile);
    if (png_sig_cmp(header, 0, kSigSize)) {
        ALOGE("%s is not a PNG.\n", filename);
        fclose(inputFile);
        return;
    }

    // Set up our control structure
    png_structp pngControl = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!pngControl)
    {
        ALOGE("png_create_read_struct failed.\n");
        fclose(inputFile);
        return;
    }

    // Set up our image info structure
    png_infop pngInfo = png_create_info_struct(pngControl);
    if (!pngInfo)
    {
        ALOGE("error: png_create_info_struct returned 0.\n");
        png_destroy_read_struct(&pngControl, nullptr, nullptr);
        fclose(inputFile);
        return;
    }

    // Install an error handler
    if (setjmp(png_jmpbuf(pngControl))) {
        ALOGE("libpng reported an error\n");
        png_destroy_read_struct(&pngControl, &pngInfo, nullptr);
        fclose(inputFile);
        return;
    }

    // Set up the png reader and fetch the remaining bits of the header
    png_init_io(pngControl, inputFile);
    png_set_sig_bytes(pngControl, kSigSize);
    png_read_info(pngControl, pngInfo);

    // Get basic information about the PNG we're reading
    int bitDepth;
    int colorFormat;
    png_uint_32 width;
    png_uint_32 height;
    png_uint_32 number_of_passes;

    png_get_IHDR(pngControl, pngInfo,
                 &width, &height,
                 &bitDepth, &colorFormat,
                 NULL, NULL, NULL);

    if (colorFormat == PNG_COLOR_TYPE_RGB_ALPHA)
        png_set_swap_alpha(pngControl);
    if(colorFormat==PNG_COLOR_TYPE_PALETTE)
    {
        png_set_packing(pngControl);
        png_set_palette_to_rgb(pngControl); //Expand data to 24-bit RGB or 32-bit RGBA if alpha available.
    }

    if (colorFormat == PNG_COLOR_TYPE_GRAY && bitDepth < 8)
        png_set_expand_gray_1_2_4_to_8(pngControl);

    if (colorFormat == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(pngControl);

    if (bitDepth == 16)
        png_set_strip_16(pngControl);

    if ((colorFormat&PNG_COLOR_MASK_ALPHA) == 0)
        png_set_add_alpha(pngControl, 0xFF, PNG_FILLER_AFTER);

    if (colorFormat == PNG_COLOR_TYPE_GRAY || colorFormat == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(pngControl);

     if(png_get_valid(pngControl, pngInfo, PNG_INFO_tRNS))
         png_set_tRNS_to_alpha(pngControl);

    number_of_passes = png_set_interlace_handling(pngControl);
    // Refresh the values in the png info struct in case any transformation shave been applied.
    png_read_update_info(pngControl, pngInfo);
    if (setjmp(png_jmpbuf(pngControl))) {
        ALOGE("libpng reported an error\n");
        png_destroy_read_struct(&pngControl, &pngInfo, nullptr);
        fclose(inputFile);
        return;
    }

    int stride = png_get_rowbytes(pngControl, pngInfo);
    stride += 3 - ((stride-1) % 4);   // glTexImage2d requires rows to be 4-byte aligned

    // libpng needs an array of pointers into the image data for each row
    png_byte ** rowPointers = (png_byte**)malloc(height * sizeof(png_byte*));
    if (rowPointers == NULL)
    {
        ALOGE("Failed to allocate temporary row pointers\n");
        png_destroy_read_struct(&pngControl, &pngInfo, nullptr);
        fclose(inputFile);
        return;
    }
    for (unsigned int r = 0; r < height; r++)
    {
        rowPointers[r] = (png_bytep)malloc(png_get_rowbytes(pngControl, pngInfo));
    }


    // Read in the actual image bytes
    png_read_image(pngControl, rowPointers);
    png_read_end(pngControl, nullptr);

    int pos = (width * height * 4) - (4 * width);
    for(int row = 0; row < height; row++)
    {
        for(int col = 0; col < (4 * width); col += 4)
        {
            buff[pos++] = rowPointers[row][col];
            buff[pos++] = rowPointers[row][col+1];
            buff[pos++] = rowPointers[row][col+2];
            buff[pos++] = rowPointers[row][col+3];
        }
        pos=(pos - (width * 4)*2);
    }
    //png_read_end(pngControl, nullptr);

    png_destroy_read_struct(&pngControl, &pngInfo, nullptr);
    free(rowPointers);
    fclose(inputFile);
}

void FakeCapture::onMemoryDestroy() {
    fsl::Memory *buffer = nullptr;
    fsl::MemoryManager* allocator = fsl::MemoryManager::getInstance();
    for (const auto& physical_cam : mPhysicalCamera) {
        if (mDeviceFd[physical_cam] < 0)
            continue;

        std::vector<fsl::Memory*> fsl_mem;
        fsl_mem = mCamBuffers[mDeviceFd[physical_cam]];

        for (auto mem_singal : fsl_mem) {
            {
                std::unique_lock <std::mutex> lock(mLock);
                if (mem_singal == nullptr) {
                    continue;
                }

                buffer = mem_singal;
                mem_singal = nullptr;
            }
            allocator->releaseMemory(buffer);
        }
        fsl_mem.clear();
    }
}
