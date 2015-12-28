/*
 * Copyright (C) 2013-2015 Freescale Semiconductor, Inc.
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


#include <BufferManager.h>
#include <gpuhelper.h>
#include <cutils/properties.h>

using namespace android;

GPUBufferManager::GPUBufferManager()
{
    gralloc_viv = NULL;
    gpu_device = NULL;
    ALOGI("open gpu gralloc module!");
    if (hw_get_module(GRALLOC_VIV_HARDWARE_MODULE_ID,
                (const hw_module_t**)&gralloc_viv) == 0) {
        int status = gralloc_open((const hw_module_t*)gralloc_viv, &gpu_device);
        if(status || !gpu_device){
            ALOGI("gpu gralloc device open failed!");
        }
    }
    else {
        ALOGI("gpu gralloc module open failed!");
    }
}

GPUBufferManager::~GPUBufferManager()
{
    if (gpu_device != NULL) {
        gpu_device->common.close((struct hw_device_t *)gpu_device);
    }
}

private_handle_t* GPUBufferManager::createPrivateHandle(int fd,
                             int size, int flags)
{
    return graphic_handle_create(fd, size, flags);
}

void GPUBufferManager::destroyPrivateHandle(private_handle_t* handle)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return;
    }

    return graphic_handle_destroy(handle);
}

int GPUBufferManager::validateHandle(buffer_handle_t handle)
{
    return graphic_handle_validate(handle);
}

int GPUBufferManager::allocBuffer(int w, int h, int format, int usage,
                                  int alignW, int /*alignH*/, size_t size,
                                  buffer_handle_t* handle, int* stride)
{
    if (!handle || !stride) {
        ALOGE("<%s,%d> invalide parameters", __FUNCTION__, __LINE__);
        return -EINVAL;
    }

    int err = 0;
    bool needWrap = true;
    private_handle_t* hnd = NULL;


    if (usage & GRALLOC_USAGE_HW_FB) {
        err = allocFramebuffer(size, usage, (buffer_handle_t*)&hnd);
        if (err != 0) {
            ALOGE("%s alloc framebuffer failed", __FUNCTION__);
            return err;
        }

        hnd->width = w;
        hnd->height = h;
        hnd->format = format;
        hnd->usage = usage;
        hnd->stride = alignW;
        //becaue private_handle_t doesn't contains stride.
        //hack it to set stride in flags high 16bit.
        hnd->pid = getpid();
        *handle = hnd;
        *stride = alignW;

        /*
         * XXX: Wrap it into Vivante driver so that Vivante HAL can
         * access the buffer.
         */
        return wrapHandle(hnd, w, h, format, alignW,
                          hnd->phys, (void*)hnd->base);
    }

    //YUV format
    if (useFSLGralloc(format, usage) || (gpu_device == NULL)) {
        return allocHandle(w, h, format, alignW, size,
                         usage, (buffer_handle_t*)handle, stride);
    }

    // RGB format
    return gpu_device->alloc(gpu_device, w, h, format, usage, handle, stride);
}

int GPUBufferManager::freeBuffer(buffer_handle_t handle)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t* hnd = (private_handle_t*)handle;
    // free framebuffer.
    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {
        /* XXX: Un-wrap the framebuffer. */
        unwrapHandle(hnd);

        if (hnd->base) {
            // unmap framebuffer here.
            if (munmap((void*)hnd->base, hnd->size) < 0) {
                ALOGE("Could not unmap %s", strerror(errno));
            }
        }

        destroyPrivateHandle(hnd);
        return 0;
    }

    //YUV format
    if (useFSLGralloc(hnd->format, hnd->usage) || (gpu_device == NULL)) {
        ALOGV("free handle");
        return freeHandle(handle);
    }

    // RGB format.
    ALOGV("viv free");
    return gpu_device->free(gpu_device, handle);
}

int GPUBufferManager::registerBuffer(buffer_handle_t handle)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t* hnd = (private_handle_t*)handle;

    /* Do not need to register for framebuffer. */
    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {
        return 0;
    }

    //YUV format
    if (useFSLGralloc(hnd->format, hnd->usage) || (gralloc_viv == NULL)) {
        return registerHandle((private_handle_t*)handle);
    }

    //RGB format.
    return gralloc_viv->registerBuffer(gralloc_viv, handle);
}

int GPUBufferManager::unregisterBuffer(buffer_handle_t handle)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t* hnd = (private_handle_t*)handle;

    /* Do not need to unregister for framebuffer. */
    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {
        return 0;
    }

    //YUV format
    if (useFSLGralloc(hnd->format, hnd->usage) || (gralloc_viv == NULL)) {
        return unregisterHandle((private_handle_t*)handle);
    }

    // RGB format buffer.
    return gralloc_viv->unregisterBuffer(gralloc_viv, handle);
}

int GPUBufferManager::lock(buffer_handle_t handle, int usage,
        int l, int t, int w, int h,
        void** vaddr)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t* hnd = (private_handle_t*)handle;

    /* Do not need to lock for framebuffer. */
    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {
        *vaddr = (void*)hnd->base;
        return 0;
    }

    // yuv format buffer.
    if (useFSLGralloc(hnd->format, hnd->usage) ||  (gralloc_viv == NULL)) {
        return lockHandle(hnd, vaddr);
    }

    // RGB format buffer.
    return gralloc_viv->lock(gralloc_viv, handle, usage,
                             l, t, w, h, vaddr);
}


int GPUBufferManager::lockYCbCr(buffer_handle_t handle, int /*usage*/,
        int /*l*/, int /*t*/, int /*w*/, int /*h*/,
        android_ycbcr* ycbcr)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t* hnd = (private_handle_t*)handle;
    return lockYUVHandle(hnd, ycbcr);
}

int GPUBufferManager::unlock(buffer_handle_t handle)
{
    if (validateHandle(handle) < 0) {
        ALOGE("%s invalid handle", __FUNCTION__);
        return -EINVAL;
    }

    private_handle_t* hnd = (private_handle_t*)handle;

    /* Do not need to unlock for framebuffer. */
    if (hnd->flags & private_handle_t::PRIV_FLAGS_FRAMEBUFFER) {
        return 0;
    }

    // yuv format buffer.
    if (useFSLGralloc(hnd->format, hnd->usage) || (gralloc_viv == NULL)) {
        return unlockHandle(hnd);
    }

    // RGB format buffer.
    return gralloc_viv->unlock(gralloc_viv, handle);
}

////////////////////////////////////////////////////
int GPUBufferManager::allocHandle(int w, int h, int format, int alignW,
                size_t size, int usage, buffer_handle_t* handle, int* stride)
{
    *stride = alignW;
    return graphic_buffer_alloc(w, h, format, usage, alignW,
                             size, (private_handle_t**)handle);
}

int GPUBufferManager::freeHandle(buffer_handle_t handle)
{
    return graphic_buffer_free((private_handle_t*)handle);
}

int GPUBufferManager::wrapHandle(private_handle_t* hnd,
            int width, int height, int format, int stride,
            unsigned long phys, void* vaddr)
{
    return graphic_buffer_wrap(hnd, width, height, format,
                                stride, phys, vaddr);
}

int GPUBufferManager::unwrapHandle(private_handle_t* hnd)
{
    return graphic_buffer_unwrap(hnd);
}

int GPUBufferManager::registerHandle(private_handle_t* hnd)
{
    return graphic_buffer_register(hnd);
}

int GPUBufferManager::unregisterHandle(private_handle_t* hnd)
{
    return graphic_buffer_unregister(hnd);
}

int GPUBufferManager::lockHandle(private_handle_t* hnd, void** vaddr)
{
    int ret = graphic_buffer_lock(hnd);

    if (vaddr != NULL) {
        *vaddr = (void*)hnd->base;
    }

    return ret;
}

int GPUBufferManager::lockYUVHandle(private_handle_t* hnd, android_ycbcr* ycbcr)
{
    int ret = graphic_buffer_lock(hnd);

    if (ycbcr == NULL) {
        return ret;
    }

    switch (hnd->format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
            ycbcr->ystride = hnd->stride;
            ycbcr->cstride = ycbcr->ystride;
            ycbcr->y = (void*)hnd->base;
            ycbcr->cb = (void*)(hnd->base + hnd->stride*hnd->height);
            ycbcr->cr = (int*)ycbcr->cb + 1;
            ycbcr->chroma_step = 2;
            break;

        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            ycbcr->ystride = hnd->stride;
            ycbcr->cstride = ycbcr->ystride;
            ycbcr->y = (void*)hnd->base;
            ycbcr->cr = (void*)(hnd->base + hnd->stride*hnd->height);
            ycbcr->cb = (int*)ycbcr->cr + 1;
            ycbcr->chroma_step = 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            ycbcr->ystride = hnd->stride;
            ycbcr->cstride = ycbcr->ystride / 2;
            ycbcr->y = (void*)hnd->base;
            ycbcr->cb = (void*)(hnd->base + hnd->stride*hnd->height);
            ycbcr->cr = (void*)((int)ycbcr->cb + ycbcr->cstride*hnd->height/2);
            ycbcr->chroma_step = 1;
            break;

        case HAL_PIXEL_FORMAT_YV12:
            ycbcr->ystride = hnd->stride;
            ycbcr->cstride = ycbcr->ystride / 2;
            ycbcr->y = (void*)hnd->base;
            ycbcr->cr = (void*)(hnd->base + hnd->stride*hnd->height);
            ycbcr->cb = (void*)((int)ycbcr->cr + ycbcr->cstride*hnd->height/2);
            ycbcr->chroma_step = 1;
            break;

        default:
            ALOGE("%s not support format:0x%x", __func__, hnd->format);
            return -EINVAL;
    }

    return ret;
}

int GPUBufferManager::unlockHandle(private_handle_t* hnd)
{
    return graphic_buffer_unlock(hnd);
}

