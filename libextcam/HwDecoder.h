/*
 *  Copyright 2021-2023 NXP.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef V4L2_DECODER_H
#define V4L2_DECODER_H

#include <utils/Mutex.h>
#include <condition_variable>
#include <cutils/properties.h>

#include "ExternalCameraUtils.h"
#include "DecoderDev.h"

namespace android {

#define DEFAULT_FRM_WIDTH   176
#define DEFAULT_FRM_HEIGHT  144
#define DEFAULT_INPUT_BUFFER_SIZE_4K (4 * 1024 * 1024)
#define DEFAULT_INPUT_BUFFER_SIZE_1080P (2 * 1024 * 1024)
#define DEFAULT_INPUT_BUFFER_COUNT 4
#define DEFAULT_INPUT_BUFFER_PLANE 1
#define DEFAULT_OUTPUT_BUFFER_PLANE 2

enum {
    UNINITIALIZED,
    STOPPED,
    RUNNING,
    STOPPING,
    FLUSHING,
    RES_CHANGING,
};

struct DecoderBufferInfo {
    int32_t mDBInfoId = -1;
    bool bInUse = false;
    int mDMABufFd;
    unsigned long mPhysAddr;
    unsigned long mVirtAddr;
    uint32_t mCapacity;
};

struct VideoRect {
	uint32_t left = 0;
	uint32_t top = 0;
	uint32_t right = 0;
	uint32_t bottom = 0;
};

struct VideoFormat {
    int pixelFormat = 0;
    uint32_t width = DEFAULT_FRM_WIDTH;
    uint32_t height = DEFAULT_FRM_HEIGHT;
    uint32_t stride = DEFAULT_FRM_WIDTH;
    uint32_t bufferNum = 0;
    uint32_t bufferSize = 0;
    VideoRect rect;
};

struct VideoFramePlane {
    int32_t fd = 0;
    uint64_t vaddr = 0;
    uint64_t paddr = 0;
    uint32_t size = 0;
    uint32_t length = 0;
    uint32_t offset = 0;
};

struct InputBufferMap {
    int32_t input_id = -1;
    bool used = false;
    VideoFramePlane plane;
};

struct OutputBufferMap {
    int32_t buf_id = 0;
    bool used = false ;
    VideoFramePlane planes[DEFAULT_OUTPUT_BUFFER_PLANE];
};

typedef struct {
        uint8_t* data = nullptr;
        int fd = -1;
        int width = 0;
        int height = 0;
        uint32_t format = 0x103; // HAL_PIXEL_FORMAT_YCbCr_420_SP
        int32_t bufId;
} DecodedData;

struct DecoderInputBuffer {
        void* pInBuffer;
        int id;
        uint32_t size;
};

class HwDecoder {
public:
    HwDecoder(const char* mime);
    virtual ~HwDecoder();

    status_t Init(const char* socType);
    status_t Start();
    status_t Flush();
    status_t Stop();
    status_t Destroy();

    status_t queueInputBuffer(std::unique_ptr<DecoderInputBuffer> input);

    status_t freeOutputBuffers();

    void notifyDecodeReady(int32_t mOutbufId);
    int exportDecodedBuf(DecodedData &data, int32_t timeoutMs);
    void returnOutputBufferToDecoder(int32_t bufId);

    DecodedData mData;
    mutable std::mutex mFramesSignalLock;
    std::condition_variable mFramesSignal;

private:
    //const char* mMime;
    pthread_t mPollThread;
    pthread_t mFetchThread;

    DecoderDev* pDev;
    int32_t mFd;

    VideoFormat mInputFormat;
    VideoFormat mOutputFormat;

    enum v4l2_buf_type mOutBufType;
    enum v4l2_buf_type mCapBufType;

    enum v4l2_memory mInMemType;
    enum v4l2_memory mOutMemType;

    uint32_t mInFormat;  //v4l2 input format
    uint32_t mOutFormat; //v4l2 output format
    uint32_t mOutputPlaneSize[DEFAULT_OUTPUT_BUFFER_PLANE];

    std::vector<InputBufferMap> mInputBufferMap;
    std::vector<OutputBufferMap> mOutputBufferMap;

    Mutex mLock;
    Mutex mThreadLock;

    int mDecState;
    int mPollState;
    int mFetchState;

    bool bInputStreamOn;
    bool bOutputStreamOn;

    uint32_t mOutputBufferUsed;

    uint32_t mFrameAlignW;
    uint32_t mFrameAlignH;

    std::vector<DecoderBufferInfo> mDecoderBuffers;

    bool bNeedPostProcess;
    uint8_t mTableSize;
    COLOR_FORMAT_TABLE *color_format_table;

    status_t SetInputFormats();
    status_t allocateInputBuffers();
    status_t destroyInputBuffers();

    status_t SetOutputFormats();
    status_t allocateOutputBuffers();
    status_t allocateOutputBuffer(int bufId);
    status_t destroyOutputBuffers();

    status_t createPollThread();
    status_t destroyPollThread();

    status_t createFetchThread();
    status_t destroyFetchThread();

    status_t startInputStream();
    status_t stopInputStream();

    status_t startOutputStream();
    status_t stopOutputStream();

    status_t dequeueInputBuffer();

    status_t queueOutputBuffer(DecoderBufferInfo* pInfo);
    status_t dequeueOutputBuffer();

    status_t handleDequeueEvent();
    status_t handleFormatChanged();
    status_t onOutputFormatChanged();

    void SetDecoderBufferState(int32_t bufId, bool state);
    DecoderBufferInfo* getDecoderBufferById(int32_t bufId);
    DecoderBufferInfo* getFreeDecoderBuffer();

    static void *PollThreadWrapper(void *);
    status_t HandlePollThread();
    static void *FetchThreadWrapper(void *);
    status_t HandleFetchThread();

    void dumpStream(void *src, size_t srcSize, int32_t id);
};

}
#endif

