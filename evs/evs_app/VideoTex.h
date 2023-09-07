/*
 * Copyright (C) 2017 The Android Open Source Project
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
#ifndef VIDEOTEX_H
#define VIDEOTEX_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3ext.h>
#include <android/hardware/automotive/evs/1.1/IEvsEnumerator.h>

#include "StreamHandler.h"
#include "TexWrapper.h"

using namespace ::android::hardware::automotive::evs::V1_1;
using ::android::hardware::camera::device::V3_2::Stream;

typedef struct {
    int32_t id;
    int32_t width;
    int32_t height;
    int32_t format;
    int32_t direction;
    int32_t framerate;
} RawStreamConfig;

class VideoTex : public TexWrapper {
    friend VideoTex* createVideoTexture(sp<IEvsEnumerator> pEnum, const char* evsCameraId,
                                        std::unique_ptr<Stream> streamCfg, EGLDisplay glDisplay);

public:
    VideoTex() = delete;
    virtual ~VideoTex();

    bool refresh(); // returns true if the texture contents were updated

private:
    VideoTex(sp<IEvsEnumerator> pEnum, sp<IEvsCamera> pCamera, sp<StreamHandler> pStreamHandler,
             EGLDisplay glDisplay);

    sp<IEvsEnumerator> mEnumerator;
    sp<IEvsCamera> mCamera;
    sp<StreamHandler> mStreamHandler;
    BufferDesc mImageBuffer;

    EGLDisplay mDisplay;
    EGLImageKHR mKHRimage = EGL_NO_IMAGE_KHR;
};

VideoTex* createVideoTexture(sp<IEvsEnumerator> pEnum, const char* deviceName,
                             std::unique_ptr<Stream> streamCfg, EGLDisplay glDisplay);

#endif // VIDEOTEX_H
