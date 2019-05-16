/*
 * Copyright 2017 NXP.
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

#include <inttypes.h>
#include <stdlib.h>
#include <cutils/log.h>
#include <hardware/hardware.h>
#include <hardware/gralloc1.h>
#include <Memory.h>
#include <MemoryManager.h>
#include <sync/sync.h>
#include <graphics_ext.h>

using namespace fsl;

struct private_module_t {
    hw_module_t base;
};

struct gralloc_context {
    gralloc1_device_t device;
};

int convertAndroidFormat(int format)
{
    int fslFormat = 0;
    switch (format) {
        case HAL_PIXEL_FORMAT_RGBA_8888:
            fslFormat = FORMAT_RGBA8888;
            break;
        case HAL_PIXEL_FORMAT_RGBX_8888:
            fslFormat = FORMAT_RGBX8888;
            break;
        case HAL_PIXEL_FORMAT_BGRA_8888:
            fslFormat = FORMAT_BGRA8888;
            break;
        case HAL_PIXEL_FORMAT_RGB_888:
            fslFormat = FORMAT_RGB888;
            break;
        case HAL_PIXEL_FORMAT_RGB_565:
            fslFormat = FORMAT_RGB565;
            break;
        case HAL_PIXEL_FORMAT_YV12:
            fslFormat = FORMAT_YV12;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            fslFormat = FORMAT_NV16;
            break;
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            fslFormat = FORMAT_NV21;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            fslFormat = FORMAT_YUYV;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            fslFormat = FORMAT_I420;
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            fslFormat = FORMAT_NV12;
            break;
        case HAL_PIXEL_FORMAT_BLOB:
            fslFormat = FORMAT_BLOB;
            break;
        case HAL_PIXEL_FORMAT_NV12_TILED:
            fslFormat = FORMAT_NV12_TILED;
            break;
        case HAL_PIXEL_FORMAT_NV12_G1_TILED:
            fslFormat = FORMAT_NV12_G1_TILED;
            break;
        case HAL_PIXEL_FORMAT_NV12_G2_TILED:
            fslFormat = FORMAT_NV12_G2_TILED;
            break;
        case HAL_PIXEL_FORMAT_NV12_G2_TILED_COMPRESSED:
            fslFormat = FORMAT_NV12_G2_TILED_COMPRESSED;
            break;
        case HAL_PIXEL_FORMAT_P010:
            fslFormat = FORMAT_P010;
            break;
        case HAL_PIXEL_FORMAT_P010_TILED:
            fslFormat = FORMAT_P010_TILED;
            break;
        case HAL_PIXEL_FORMAT_P010_TILED_COMPRESSED:
            fslFormat = FORMAT_P010_TILED_COMPRESSED;
            break;
        case HAL_PIXEL_FORMAT_RGBA_1010102:
            fslFormat = FORMAT_RGBA1010102;
            break;
        case HAL_PIXEL_FORMAT_RGBA_FP16:
            fslFormat = FORMAT_RGBAFP16;
            break;
        default:
            ALOGE("%s invalid format:0x%x", __func__, format);
            break;
    }

    return fslFormat;
}

static int gralloc_unlock(gralloc1_device_t* device,
                   buffer_handle_t buffer,
                   int32_t* outReleaseFence)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    Memory* memory = (Memory*)buffer;
    if (!memory || !memory->isValid()) {
        ALOGE("%s invalid memory:0x%p", __func__, buffer);
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    MemoryManager* pManager = MemoryManager::getInstance();
    if (pManager == NULL) {
        ALOGE("%s can't get memory manager", __func__);
        return -EINVAL;
    }

    int ret = pManager->unlock(memory);
    if (ret != 0) {
        ALOGE("%s unlock memory failed", __func__);
        return GRALLOC1_ERROR_NO_RESOURCES;
    }

    if (outReleaseFence != NULL) {
        *outReleaseFence = -1;
    }

    return GRALLOC1_ERROR_NONE;
}

static int gralloc_lock(gralloc1_device_t* device,
                   buffer_handle_t buffer,
                   uint64_t /*produceUsage*/,
                   uint64_t /*consumeUsage*/,
                   const gralloc1_rect_t* /*accessRegion*/,
                   void** outData,
                   int32_t acquireFence)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    if (acquireFence != -1) {
        sync_wait(acquireFence, -1);
        close(acquireFence);
    }

    Memory* memory = (Memory*)buffer;
    if (!memory || !memory->isValid()) {
        ALOGE("%s invalid memory:0x%p", __func__, buffer);
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    MemoryManager* pManager = MemoryManager::getInstance();
    if (pManager == NULL) {
        ALOGE("%s can't get memory manager", __func__);
        return -EINVAL;
    }

    int ret = pManager->lock(memory, memory->usage, 0, 0, memory->width, memory->height, outData);
    if (ret != 0) {
        ALOGE("%s lock memory failed", __func__);
        return GRALLOC1_ERROR_NO_RESOURCES;
    }

    return GRALLOC1_ERROR_NONE;
}

static int gralloc_get_num_flex_planes(gralloc1_device_t* /*device*/,
                   buffer_handle_t buffer, uint32_t* outNumPlanes)
{
    if (!outNumPlanes) {
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    Memory* memory = (Memory*)buffer;
    if (!memory || !memory->isValid()) {
        ALOGE("%s invalid memory:0x%p", __func__, buffer);
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    bool isYuv = false;
    switch (memory->format) {
        case HAL_PIXEL_FORMAT_YV12:
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_422_I:
        case HAL_PIXEL_FORMAT_YCbCr_422_P:
        case HAL_PIXEL_FORMAT_YCbCr_420_P:
        case HAL_PIXEL_FORMAT_CbYCrY_422_I:
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            isYuv = true;
            break;
        default:
            break;
    }

    if (!isYuv) {
        return GRALLOC1_ERROR_UNSUPPORTED;
    }

    *outNumPlanes = 3;

    return GRALLOC1_ERROR_NONE;
}

static int gralloc_lock_flex(gralloc1_device_t* /*device*/,
                   buffer_handle_t buffer,
                   uint64_t /*produceUsage*/,
                   uint64_t /*consumeUsage*/,
                   const gralloc1_rect_t* /*accessRegion*/,
                   struct android_flex_layout* layout,
                   int32_t acquireFence)
{
    if (acquireFence != -1) {
        sync_wait(acquireFence, -1);
        close(acquireFence);
    }

    if (!layout) {
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    Memory* memory = (Memory*)buffer;
    if (!memory || !memory->isValid()) {
        ALOGE("%s invalid memory:0x%p", __func__, buffer);
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    MemoryManager* pManager = MemoryManager::getInstance();
    if (pManager == NULL) {
        ALOGE("%s can't get memory manager", __func__);
        return -EINVAL;
    }

    android_ycbcr ycbcr;
    int ret = pManager->lockYCbCr(memory, memory->usage, 0, 0, memory->width, memory->height, &ycbcr);
    if (ret != 0) {
        ALOGE("%s lock memory failed", __func__);
        return GRALLOC1_ERROR_NO_RESOURCES;
    }

    layout->format = FLEX_FORMAT_YCbCr;
    layout->num_planes = 3;

    for (uint32_t i = 0; i < layout->num_planes; i++) {
        layout->planes[i].bits_per_component = 8;
        layout->planes[i].bits_used = 8;
        layout->planes[i].h_increment = 1;
        layout->planes[i].v_increment = 1;
        layout->planes[i].h_subsampling = 2;
        layout->planes[i].v_subsampling = 2;
    }

    layout->planes[0].top_left = static_cast<uint8_t *>(ycbcr.y);
    layout->planes[0].component = FLEX_COMPONENT_Y;
    layout->planes[0].v_increment = static_cast<int32_t>(ycbcr.ystride);

    layout->planes[1].top_left = static_cast<uint8_t *>(ycbcr.cb);
    layout->planes[1].component = FLEX_COMPONENT_Cb;
    layout->planes[1].h_increment = static_cast<int32_t>(ycbcr.chroma_step);
    layout->planes[1].v_increment = static_cast<int32_t>(ycbcr.cstride);

    layout->planes[2].top_left = static_cast<uint8_t *>(ycbcr.cr);
    layout->planes[2].component = FLEX_COMPONENT_Cr;
    layout->planes[2].h_increment = static_cast<int32_t>(ycbcr.chroma_step);
    layout->planes[2].v_increment = static_cast<int32_t>(ycbcr.cstride);
    return GRALLOC1_ERROR_NONE;
}

static int gralloc_release(gralloc1_device_t* device,
                   buffer_handle_t buffer)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    Memory* memory = (Memory*)buffer;
    if (!memory || !memory->isValid()) {
        ALOGE("%s invalid memory:0x%p", __func__, buffer);
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    MemoryManager* pManager = MemoryManager::getInstance();
    if (pManager == NULL) {
        ALOGE("%s can't get memory manager", __func__);
        return -EINVAL;
    }

    int ret = pManager->releaseMemory(memory);
    if (ret != 0) {
        ALOGE("%s retain memory failed", __func__);
        return GRALLOC1_ERROR_NO_RESOURCES;
    }

    return GRALLOC1_ERROR_NONE;
}

static int gralloc_retain(gralloc1_device_t* device,
                   buffer_handle_t buffer)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    Memory* memory = (Memory*)buffer;
    if (!memory || !memory->isValid()) {
        ALOGE("%s invalid memory:0x%p", __func__, buffer);
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    MemoryManager* pManager = MemoryManager::getInstance();
    if (pManager == NULL) {
        ALOGE("%s can't get memory manager", __func__);
        return -EINVAL;
    }

    int ret = pManager->retainMemory(memory);
    if (ret != 0) {
        ALOGE("%s retain memory failed", __func__);
        return GRALLOC1_ERROR_NO_RESOURCES;
    }

    return GRALLOC1_ERROR_NONE;
}

static int gralloc_allocate(gralloc1_device_t* device,
                   uint32_t numDescriptors,
                   const gralloc1_buffer_descriptor_t* descriptors,
                   buffer_handle_t* outBuffers)
{
    if (!device || !outBuffers) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    if (!numDescriptors || !descriptors) {
        ALOGE("%s invalid descriptor", __func__);
        return GRALLOC1_ERROR_BAD_DESCRIPTOR;
    }

    MemoryManager* pManager = MemoryManager::getInstance();
    if (pManager == NULL) {
        ALOGE("%s can't get memory manager", __func__);
        return -EINVAL;
    }

    for (uint32_t i=0; i<numDescriptors; i++) {
        Memory* memory = NULL;
        MemoryDesc* desc = (MemoryDesc*)descriptors[i];
        if (!desc || !desc->isValid() || desc->mWidth < 0 || desc->mHeight < 0) {
            ALOGE("%s invalid descriptor:%" PRId64, __func__, descriptors[i]);
            return GRALLOC1_ERROR_BAD_DESCRIPTOR;
        }

        desc->mFslFormat = convertAndroidFormat(desc->mFormat);
        if (desc->mFslFormat == FORMAT_BLOB) {
            // Below trick is based on the convention that the height is 1, width is size
            // when BLOB format. Show the info, so that once the framework change the convention,
            // We can get some clue.
            ALOGI("%s, FORMAT_BLOB, %dx%d", __func__, desc->mWidth, desc->mHeight);

            // GPU can't recognize BLOB format, fake format to YUYV.
            // size = width * height * 2;
            // avoid height alignment issue.
            desc->mFormat = HAL_PIXEL_FORMAT_YCbCr_422_I;
            desc->mFslFormat = FORMAT_YUYV;
            desc->mWidth = (desc->mWidth + 1) / 2;

            // For FORMAT_YUYV, the height is aligned by 4 in MemoryDesc::checkFormat().
            // Do some trick here to keep  w*h unchanged. Or will allocate 4 times of the needed size.
            desc->mWidth = (desc->mWidth+3)/4;
            desc->mHeight *= 4;
        }
        if (desc->mFslFormat == FORMAT_NV12_TILED ||
            desc->mFslFormat == FORMAT_NV12_G1_TILED ||
            desc->mFslFormat == FORMAT_NV12_G2_TILED ||
            desc->mFslFormat == FORMAT_NV12_G2_TILED_COMPRESSED ||
            desc->mFslFormat == FORMAT_P010 ||
            desc->mFslFormat == FORMAT_P010_TILED ||
            desc->mFslFormat == FORMAT_P010_TILED_COMPRESSED) {
            desc->mFormat = HAL_PIXEL_FORMAT_YCbCr_420_SP;
        }

        desc->checkFormat();

        int ret = pManager->allocMemory(*desc, &memory);
        if (ret != 0) {
            ALOGE("%s alloc memory failed", __func__);
            return GRALLOC1_ERROR_NO_RESOURCES;
        }

        *(&outBuffers[i]) = memory;
    }

    return GRALLOC1_ERROR_NONE;
}

static int gralloc_get_stride(gralloc1_device_t* device,
                   buffer_handle_t buffer,
                   uint32_t* outStride)
{
    if (!device || !outStride) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    Memory* memory = (Memory*)buffer;
    if (!memory || !memory->isValid()) {
        ALOGE("%s invalid memory:0x%p", __func__, buffer);
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    *outStride = memory->stride;
    return GRALLOC1_ERROR_NONE;
}

static int gralloc_get_produce_usage(gralloc1_device_t* device,
                   buffer_handle_t buffer,
                   uint64_t* outUsage)
{
    if (!device || !outUsage) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    Memory* memory = (Memory*)buffer;
    if (!memory || !memory->isValid()) {
        ALOGE("%s invalid memory:0x%p", __func__, buffer);
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    *outUsage = memory->usage;
    return GRALLOC1_ERROR_NONE;
}

static int gralloc_get_format(gralloc1_device_t* device,
                   buffer_handle_t buffer,
                   int32_t* outFormat)
{
    if (!device || !outFormat) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    Memory* memory = (Memory*)buffer;
    if (!memory || !memory->isValid()) {
        ALOGE("%s invalid memory:0x%p", __func__, buffer);
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    *outFormat = memory->format;
    return GRALLOC1_ERROR_NONE;
}

static int gralloc_get_dimension(gralloc1_device_t* device,
                   buffer_handle_t buffer,
                   uint32_t* outWidth, uint32_t* outHeight)
{
    if (!device || !outWidth || !outHeight) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    Memory* memory = (Memory*)buffer;
    if (!memory || !memory->isValid()) {
        ALOGE("%s invalid memory:0x%p", __func__, buffer);
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    *outWidth = memory->width;
    *outHeight = memory->height;
    return GRALLOC1_ERROR_NONE;
}

static int gralloc_get_consume_usage(gralloc1_device_t* device,
                   buffer_handle_t buffer,
                   uint64_t* outUsage)
{
    if (!device || !outUsage) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    Memory* memory = (Memory*)buffer;
    if (!memory || !memory->isValid()) {
        ALOGE("%s invalid memory:0x%p", __func__, buffer);
        return GRALLOC1_ERROR_BAD_HANDLE;
    }

    *outUsage = memory->usage;
    return GRALLOC1_ERROR_NONE;
}

static int gralloc_get_backing_store(gralloc1_device_t* /*device*/,
                   buffer_handle_t /*buffer*/,
                   gralloc1_backing_store_t* /*outStore*/)
{
    return GRALLOC1_ERROR_UNSUPPORTED;
}

static int gralloc_set_produce_usage(gralloc1_device_t *device,
                   gralloc1_buffer_descriptor_t descriptor,
                   uint64_t usage)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    MemoryDesc* desc = (MemoryDesc*)descriptor;
    if (!desc || !desc->isValid()) {
        ALOGE("%s invalid descriptor:%" PRId64, __func__, descriptor);
        return GRALLOC1_ERROR_BAD_DESCRIPTOR;
    }

    int flags = 0;
    if (usage & GRALLOC1_CONSUMER_USAGE_CLIENT_TARGET) {
        flags |= FLAGS_FRAMEBUFFER;
        usage |= GRALLOC1_CONSUMER_USAGE_HWCOMPOSER
              | GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET;
    }

    if ((usage & GRALLOC1_PRODUCER_USAGE_CPU_READ_OFTEN) != 0 ||
        (usage & GRALLOC1_PRODUCER_USAGE_CPU_WRITE_OFTEN) != 0) {
        flags |= FLAGS_CPU;
    }

    desc->mFlag |= flags;
    desc->mProduceUsage = usage;
    return GRALLOC1_ERROR_NONE;
}

static int gralloc_set_format(gralloc1_device_t *device,
                   gralloc1_buffer_descriptor_t descriptor,
                   int32_t format)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    MemoryDesc* desc = (MemoryDesc*)descriptor;
    if (!desc || !desc->isValid()) {
        ALOGE("%s invalid descriptor:%" PRId64, __func__, descriptor);
        return GRALLOC1_ERROR_BAD_DESCRIPTOR;
    }

    if (format == HAL_PIXEL_FORMAT_YCbCr_420_888) {
        // now, take the flex format as NV12.
        // this format should only be known in framework.
        // private_handle in hal should not record this format.
        format = HAL_PIXEL_FORMAT_YCbCr_420_SP;
    }

    desc->mFormat = format;
    return GRALLOC1_ERROR_NONE;
}

static int gralloc_set_dimension(gralloc1_device_t *device,
                   gralloc1_buffer_descriptor_t descriptor,
                   uint32_t width, uint32_t height)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    MemoryDesc* desc = (MemoryDesc*)descriptor;
    if (!desc || !desc->isValid()) {
        ALOGE("%s invalid descriptor:%" PRId64, __func__, descriptor);
        return GRALLOC1_ERROR_BAD_DESCRIPTOR;
    }

    desc->mWidth = width;
    desc->mHeight = height;
    return GRALLOC1_ERROR_NONE;
}

static int gralloc_set_consume_usage(gralloc1_device_t *device,
                   gralloc1_buffer_descriptor_t descriptor,
                   uint64_t usage)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    MemoryDesc* desc = (MemoryDesc*)descriptor;
    if (!desc || !desc->isValid()) {
        ALOGE("%s invalid descriptor:%" PRId64, __func__, descriptor);
        return GRALLOC1_ERROR_BAD_DESCRIPTOR;
    }

    int flags = 0;
    if (usage & GRALLOC1_CONSUMER_USAGE_CLIENT_TARGET) {
        flags |= FLAGS_FRAMEBUFFER;
        usage |= GRALLOC1_CONSUMER_USAGE_HWCOMPOSER
              | GRALLOC1_PRODUCER_USAGE_GPU_RENDER_TARGET;
    }

    if ((usage & GRALLOC1_CONSUMER_USAGE_CPU_READ_OFTEN) != 0) {
        flags |= FLAGS_CPU;
    }

    desc->mFlag |= flags;
    desc->mConsumeUsage = usage;
    return GRALLOC1_ERROR_NONE;
}

static int gralloc_destroy_descriptor(gralloc1_device_t *device,
                   gralloc1_buffer_descriptor_t descriptor)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    MemoryDesc* desc = (MemoryDesc*)descriptor;
    if (!desc || !desc->isValid()) {
        ALOGE("%s invalid descriptor:%" PRId64, __func__, descriptor);
        return GRALLOC1_ERROR_BAD_DESCRIPTOR;
    }

    delete desc;
    return GRALLOC1_ERROR_NONE;
}

static int gralloc_create_descriptor(gralloc1_device_t* device,
                   gralloc1_buffer_descriptor_t* outDescriptor)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    if (!outDescriptor) {
        ALOGE("%s invalid outDesc", __func__);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    MemoryDesc* desc = new MemoryDesc();
    *outDescriptor = (gralloc1_buffer_descriptor_t)desc;

    return GRALLOC1_ERROR_NONE;
}

static void gralloc_dump(gralloc1_device_t* /*device*/,
                         uint32_t* /*outSize*/, char* /*outBuffer*/)
{
}

static gralloc1_function_pointer_t gralloc_get_function(
                    struct gralloc1_device* device,
                    int32_t descriptor)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return NULL;
    }

    gralloc1_function_pointer_t func = NULL;
    switch (descriptor) {
        case GRALLOC1_FUNCTION_CREATE_DESCRIPTOR:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_create_descriptor);
            break;
        case GRALLOC1_FUNCTION_DESTROY_DESCRIPTOR:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_destroy_descriptor);
            break;
        case GRALLOC1_FUNCTION_SET_CONSUMER_USAGE:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_set_consume_usage);
            break;
        case GRALLOC1_FUNCTION_SET_DIMENSIONS:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_set_dimension);
            break;
        case GRALLOC1_FUNCTION_SET_FORMAT:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_set_format);
            break;
        case GRALLOC1_FUNCTION_SET_PRODUCER_USAGE:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_set_produce_usage);
            break;
        case GRALLOC1_FUNCTION_GET_BACKING_STORE:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_get_backing_store);
            break;
        case GRALLOC1_FUNCTION_GET_CONSUMER_USAGE:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_get_consume_usage);
            break;
        case GRALLOC1_FUNCTION_GET_DIMENSIONS:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_get_dimension);
            break;
        case GRALLOC1_FUNCTION_GET_FORMAT:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_get_format);
            break;
        case GRALLOC1_FUNCTION_GET_PRODUCER_USAGE:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_get_produce_usage);
            break;
        case GRALLOC1_FUNCTION_GET_STRIDE:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_get_stride);
            break;
        case GRALLOC1_FUNCTION_ALLOCATE:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_allocate);
            break;
        case GRALLOC1_FUNCTION_RETAIN:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_retain);
            break;
        case GRALLOC1_FUNCTION_RELEASE:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_release);
            break;
        case GRALLOC1_FUNCTION_GET_NUM_FLEX_PLANES:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_get_num_flex_planes);
            break;
        case GRALLOC1_FUNCTION_LOCK:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_lock);
            break;
        case GRALLOC1_FUNCTION_LOCK_FLEX:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_lock_flex);
            break;
        case GRALLOC1_FUNCTION_UNLOCK:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_unlock);
            break;
        case GRALLOC1_FUNCTION_DUMP:
            func = reinterpret_cast<gralloc1_function_pointer_t>(gralloc_dump);
            break;
        default:
            ALOGE("invalid descriptor:%d", descriptor);
            break;
    }

    return func;
}

static void gralloc_get_capabilities(struct gralloc1_device* device,
                          uint32_t* outCount, int32_t* outCapabilities)
{
    if (!device) {
        ALOGE("%s invalid device", __func__);
        return;
    }

    if (outCapabilities) {
        if (outCount != NULL && *outCount >= 1)
        *outCapabilities = GRALLOC1_CAPABILITY_RELEASE_IMPLY_DELETE;
    }
    else if (outCount) {
        *outCount = 1;
    }
}

static int gralloc_device_close(struct hw_device_t *device)
{
    gralloc_context* ctx = reinterpret_cast<gralloc_context*>(device);
    if (ctx) {
        free(ctx);
    }

    return 0;
}

static int gralloc1_device_open(const struct hw_module_t *module,
                         const char *name, hw_device_t **device)
{
    if (!module || strcmp(name, GRALLOC_HARDWARE_MODULE_ID)) {
        ALOGE("%s invalid name:%s", __func__, name);
        return GRALLOC1_ERROR_BAD_VALUE;
    }

    gralloc_context* dev = (gralloc_context*)malloc(sizeof(*dev));
    /* initialize our state here */
    memset(dev, 0, sizeof(*dev));

    /* initialize the procs */
    dev->device.common.tag = HARDWARE_DEVICE_TAG;
    dev->device.common.version = 1;
    dev->device.common.module = const_cast<hw_module_t*>(module);
    dev->device.common.close = gralloc_device_close;

    dev->device.getCapabilities = gralloc_get_capabilities;
    dev->device.getFunction = gralloc_get_function;

    *device = &dev->device.common;

    return 0;
}

static struct hw_module_methods_t gralloc_module_methods = {
    .open = gralloc1_device_open
};

struct private_module_t HAL_MODULE_INFO_SYM = {
    .base = {
        .tag = HARDWARE_MODULE_TAG,
        .version_major = GRALLOC_MODULE_API_VERSION_1_0,
        .version_minor = 0,
        .id = GRALLOC_HARDWARE_MODULE_ID,
        .name = "Graphics Memory Module",
        .author = "Freescale Semiconductor, Inc.",
        .methods = &gralloc_module_methods,
        .dso = 0,
        .reserved = {0},
    },
};

