/*
 * Copyright (C) 2015 Freescale Semiconductor, Inc.
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

#ifndef _STREAM_H_
#define _STREAM_H_

#include <hardware/camera3.h>
#include <hardware/gralloc.h>
#include <system/graphics.h>
#include <utils/Mutex.h>
#include <utils/RefBase.h>
#include <linux/ipu.h>
#include <ion/ion.h>
#include "JpegBuilder.h"
#include "g2d.h"

using namespace android;

class Camera;
// Stream represents a single input or output stream for a camera device.
class Stream : public LightRefBase<Stream>
{
public:
    Stream(Camera* camera);
    Stream(int id, camera3_stream_t *s, Camera* camera);
    virtual ~Stream();

    // validate that astream's parameters match this stream's parameters
    bool isValidReuseStream(int id, camera3_stream_t *s);

    int32_t processCaptureBuffer(StreamBuffer& buf,
                                 sp<Metadata> meta);

    void setCurrentBuffer(StreamBuffer* out) {mCurrent = out;}
    virtual void* getG2dHandle() {return NULL;}
    bool isPreview() {return mPreview;}
    bool isJpeg() {return mJpeg;}
    uint32_t width() {return mWidth;}
    uint32_t height() {return mHeight;}
    int32_t format() {return mFormat;}
    uint32_t bufferNum() {return mNumBuffers;}
    camera3_stream_t* stream() {return mStream;}
    void setReuse(bool reuse) {mReuse = mReuse;}
    void setFps(uint32_t fps) {mFps = fps;}
    uint32_t fps() {return mFps;};

    int getType();
    bool isInputType();
    bool isOutputType();
    bool isRegistered();
    void dump(int fd);

protected:
    int32_t processJpegBuffer(StreamBuffer& src,
                              sp<Metadata> meta);
    int32_t processFrameBuffer(StreamBuffer& src,
                               sp<Metadata> meta);
    int32_t convertNV12toNV21(StreamBuffer& src);
    int32_t processBufferWithPXP(StreamBuffer& src);
    int32_t processBufferWithIPU(StreamBuffer& src);
    int32_t processBufferWithGPU(StreamBuffer& src);

    int32_t processBufferWithCPU(StreamBuffer& src);

protected:
    // This stream is being reused. Used in stream configuration passes
    bool mReuse;
    bool mPreview;
    bool mJpeg;
    bool mCallback;
    // The camera device id this stream belongs to
    const int mId;
    // Handle to framework's stream, used as a cookie for buffers
    camera3_stream_t *mStream;
    // Stream type: CAMERA3_STREAM_* (see <hardware/camera3.h>)
    const int mType;
    // Width in pixels of the buffers in this stream
    uint32_t mWidth;
    // Height in pixels of the buffers in this stream
    uint32_t mHeight;
    // Gralloc format: HAL_PIXEL_FORMAT_* (see <system/graphics.h>)
    int32_t mFormat;
    // Gralloc usage mask : GRALLOC_USAGE_* (see <hardware/gralloc.h>)
    uint32_t mUsage;
    // frame rate.
    uint32_t mFps;
    // Max simultaneous in-flight buffers for this stream
    uint32_t mNumBuffers;
    // Buffers have been registered for this stream and are ready
    bool mRegistered;
    // Array of handles to buffers currently in use by the stream
    StreamBuffer* mBuffers[MAX_STREAM_BUFFERS];
    // Lock protecting the Stream object for modifications
    android::Mutex mLock;

    int32_t mIpuFd;
    int32_t mPxpFd;
    int32_t channel;
    StreamBuffer* mCurrent;
    Camera* mCamera;
    sp<JpegBuilder> mJpegBuilder;
};

#endif // STREAM_H_
