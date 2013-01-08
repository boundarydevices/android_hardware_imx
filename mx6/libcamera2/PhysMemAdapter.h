/*
 * Copyright (C) 2012-2013 Freescale Semiconductor, Inc.
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

#ifndef _PHYS_MEM_ADAPTER_H_
#define _PHYS_MEM_ADAPTER_H_

#include "CameraUtil.h"

using namespace android;

class PhysMemAdapter : public CameraBufferProvider {
public:
    PhysMemAdapter();
    virtual ~PhysMemAdapter();

//    virtual int allocatePreviewBuffer(int width,
//                                      int height,
//                                      int format,
//                                      int numBufs);
    virtual int allocateBuffers(int width, int height,
                               int format, int numBufs);
    virtual int freeBuffers();
    //virtual int maxQueueableBuffers();

    // void setErrorListener(CameraErrorListener* listener);

protected:
    int mIonFd;
    CameraErrorListener *mErrorListener;

    CameraFrame mCameraBuffer[MAX_CAPTURE_BUFFER];

    uint32_t mFrameWidth;
    uint32_t mFrameHeight;
    int mBufferCount;
    int mBufferSize;
    PixelFormat mFormat;
    //int mQueueableCount;
};

#endif // ifndef _PHYS_MEM_ADAPTER_H_
