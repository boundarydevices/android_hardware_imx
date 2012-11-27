/*
 * Copyright (C) 2008 The Android Open Source Project
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
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

#ifndef _SURFACE_ADAPTER_H_
#define _SURFACE_ADAPTER_H_

#include "CameraUtil.h"

using namespace android;

class SurfaceAdapter : public CameraBufferProvider, public LightRefBase<SurfaceAdapter>
{
public:
    SurfaceAdapter();
    virtual ~SurfaceAdapter();

    virtual int allocatePreviewBuffer(int width, int height, int format, int numBufs);
    virtual int allocatePictureBuffer(int width, int height, int format, int numBufs);
    virtual int freeBuffer();
    virtual int maxQueueableBuffers();
    virtual int setPreviewWindow(struct preview_stream_ops *window);

    void setErrorListener(CameraErrorListener* listener);

protected:
    void destroy();
    void renderBuffer(buffer_handle_t* bufHandle);
    void cancelBuffer(buffer_handle_t* bufHandle);
    CameraFrame* requestBuffer();

private:
    int setNativeWindowAttribute(int width, int height, int format, int numBufs);
    int allocateBuffer(int width, int height, int format, int numBufs, int maxQCount);

protected:
    CameraErrorListener* mErrorListener;
    preview_stream_ops_t* mNativeWindow;

    CameraFrame mCameraBuffer[MAX_PREVIEW_BUFFER];

    uint32_t mFrameWidth;
    uint32_t mFrameHeight;
    int mBufferCount;
    int mBufferSize;
    PixelFormat mFormat;
    int mQueueableCount;
};

#endif
