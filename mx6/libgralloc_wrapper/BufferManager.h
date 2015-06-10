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

#ifndef _FSL_BUFFER_MANAGER_H
#define _FSL_BUFFER_MANAGER_H

#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include <cutils/ashmem.h>
#include <cutils/log.h>
#include <cutils/atomic.h>

#include <utils/threads.h>

#include <hardware/hardware.h>
#include <hardware/gralloc.h>

#include "gralloc_priv.h"

using namespace android;

struct fb_context_t;

class Display
{
public:
    Display()
      : mFramebuffer(NULL), mNumBuffers(0), mBufferMask(0),
        mLock(Mutex::PRIVATE), mCurrentBuffer(NULL)
    {
        memset(&mInfo, 0, sizeof(mInfo));
        memset(&mFinfo, 0, sizeof(mFinfo));
        mXdpi = 0;
        mYdpi = 0;
        mFps = 0;
        fb_num = -1;
    }

    int initialize(int fb);
    int allocFrameBuffer(size_t size, int usage, buffer_handle_t* pHandle);

    static int postBuffer(struct framebuffer_device_t* dev, buffer_handle_t buffer);
    static int setUpdateRect(struct framebuffer_device_t* dev,
        int l, int t, int w, int h);
    static int setSwapInterval(struct framebuffer_device_t* dev,
            int interval);
    static int compositionComplete(struct framebuffer_device_t* dev);
    static int closeDevice(struct hw_device_t *dev);
    void setContext(fb_context_t *dev);

private:
    int uninitialize();
    int checkFramebufferFormat(int fd, uint32_t &flags);

private:
/** do NOT change the elements below **/
    private_handle_t* mFramebuffer;
    uint32_t mNumBuffers;
    uint32_t mBufferMask;
    Mutex mLock;
    buffer_handle_t mCurrentBuffer;
    struct fb_var_screeninfo mInfo;
    struct fb_fix_screeninfo mFinfo;
/** do NOT change the elements above **/

    float mXdpi;
    float mYdpi;
    float mFps;
    int fb_num;
};

class BufferManager
{
public:
    virtual ~BufferManager() {}

    static BufferManager* getInstance();
    Display* getDisplay(int dispid);

    int alloc(int w, int h, int format, int usage,
            buffer_handle_t* handle, int* stride);
    int free(buffer_handle_t handle);

    virtual int registerBuffer(buffer_handle_t handle) = 0;
    virtual int unregisterBuffer(buffer_handle_t handle) = 0;
    virtual int lock(buffer_handle_t handle, int usage,
            int l, int t, int w, int h,
            void** vaddr) = 0;
    virtual int unlock(buffer_handle_t handle) = 0;

    // to alloc/free private handle.
    virtual private_handle_t* createPrivateHandle(int fd,
                             int size, int flags) = 0;
    virtual void destroyPrivateHandle(private_handle_t* handle) = 0;
    virtual int validateHandle(buffer_handle_t handle) = 0;

protected:
    virtual int allocBuffer(int w, int h, int format, int usage,
                            int alignW, int alignH, size_t size,
                            buffer_handle_t* handle, int* stride) = 0;
    virtual int freeBuffer(buffer_handle_t handle) = 0;

    int allocFramebuffer(size_t size, int usage, buffer_handle_t* pHandle);
    bool useFSLGralloc(int format, int usage);
    BufferManager();

public:
    // static function to be used in gralloc.
    static int gralloc_alloc(alloc_device_t* dev,
        int w, int h, int format, int usage,
        buffer_handle_t* pHandle, int* pStride);
    static int gralloc_free(alloc_device_t* dev,
        buffer_handle_t handle);
    static int gralloc_register_buffer(gralloc_module_t const* module,
                                buffer_handle_t handle);
    static int gralloc_unregister_buffer(gralloc_module_t const* module,
        buffer_handle_t handle);
    static int gralloc_lock(gralloc_module_t const* module,
        buffer_handle_t handle, int usage,
        int l, int t, int w, int h,
        void** vaddr);
    static int gralloc_unlock(gralloc_module_t const* module,
        buffer_handle_t handle);
    static int gralloc_device_close(struct hw_device_t *dev);
    static int gralloc_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device);
    static int fb_device_open(const hw_module_t* module, const char* name,
        hw_device_t** device);

private:
    static Mutex sLock;
    static BufferManager* sInstance;

    Display* mDisplays[MAX_DISPLAY_DEVICE];
};

class GPUBufferManager : public BufferManager
{
public:
    GPUBufferManager();
    virtual ~GPUBufferManager();

    virtual int registerBuffer(buffer_handle_t handle);
    virtual int unregisterBuffer(buffer_handle_t handle);
    virtual int lock(buffer_handle_t handle, int usage,
            int l, int t, int w, int h,
            void** vaddr);
    virtual int unlock(buffer_handle_t handle);

    virtual private_handle_t* createPrivateHandle(int fd,
                             int size, int flags);
    virtual void destroyPrivateHandle(private_handle_t* handle);
    virtual int validateHandle(buffer_handle_t handle);

protected:
    virtual int allocBuffer(int w, int h, int format, int usage,
                            int alignW, int alignH, size_t size,
                            buffer_handle_t* handle, int* stride);
    virtual int freeBuffer(buffer_handle_t handle);

private:
    int allocHandle(int w, int h, int format, int alignW, size_t size,
                       int usage, buffer_handle_t* handle, int* stride);
    int freeHandle(buffer_handle_t handle);

    int wrapHandle(private_handle_t* hnd,
                int width, int height, int format, int stride,
                unsigned long phys, void* vaddr);
    int unwrapHandle(private_handle_t* hnd);
    //int registerHandle(private_handle_t* hnd,
    //            unsigned long phys, void* vaddr);
    int registerHandle(private_handle_t* hnd);
    int unregisterHandle(private_handle_t* hnd);
    int lockHandle(private_handle_t* hnd, void** vaddr);
    int unlockHandle(private_handle_t* hnd);

private:
    alloc_device_t *gpu_device;
    gralloc_module_t* gralloc_viv;
};

class CPUBufferManager : public BufferManager
{
public:
    CPUBufferManager();
    virtual ~CPUBufferManager();

    virtual int registerBuffer(buffer_handle_t handle);
    virtual int unregisterBuffer(buffer_handle_t handle);
    virtual int lock(buffer_handle_t handle, int usage,
            int l, int t, int w, int h,
            void** vaddr);
    virtual int unlock(buffer_handle_t handle);

    virtual private_handle_t* createPrivateHandle(int fd,
                             int size, int flags);
    virtual void destroyPrivateHandle(private_handle_t* handle);
    virtual int validateHandle(buffer_handle_t handle);

protected:
    virtual int allocBuffer(int w, int h, int format, int usage,
                            int alignW, int alignH, size_t size,
                            buffer_handle_t* handle, int* stride);
    virtual int freeBuffer(buffer_handle_t handle);

private:
    int allocBuffer(size_t size, int usage,
                 buffer_handle_t* pHandle);
    int allocBufferByIon(size_t size, int usage,
                 buffer_handle_t* pHandle);
    int mapBuffer(buffer_handle_t handle);
    int unmapBuffer(buffer_handle_t handle);

private:
    int mIonFd;
};

#endif
