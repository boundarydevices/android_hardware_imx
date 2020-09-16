/*
 *  Copyright 2020 NXP.
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
#ifndef HwJpegEncoder_DEFINED
#define HwJpegEncoder_DEFINED

#include "YuvToJpegEncoder.h"

using namespace android;

struct encoder_args {
    int width;
    int height;
    int fourcc;
    int size;
};

class HwJpegEncoder : public YuvToJpegEncoder {
public:
    HwJpegEncoder(int format);

    int encode(void *inYuv,
               void* inYuvPhy,
               int   inWidth,
               int   inHeight,
               int   quality,
               void *outBuf,
               int   outSize,
               int   outWidth,
               int   outHeight);


    int mFormat;

    struct encoder_args mEncArgs;

    // The data direction is jpeg hw -> RAM, the output of jpeg hw
    void *mBufferOutStart[2] = {0};

    // The data direction is RAM -> jpeg hw, the input of jpeg hw
    void *mBufferInStart[2] = {0};

    char mJpegDevPath[64];
    int mJpegFd;

    virtual ~HwJpegEncoder() {};

private:
    int v4l2_mmap(int vdev_fd, struct v4l2_buffer *buf,
                   void *buf_start[]);
    void v4l2_munmap(struct v4l2_buffer *buf,
                   void *buf_start[]);
    void get_out_buffer_size(struct encoder_args* ec_args, int fmt);
    int onEncoderConfig(struct encoder_args *ea, char *srcbuf, struct v4l2_buffer *bufferin,
                                          struct v4l2_buffer *bufferout);

    int onEncoderStart(struct v4l2_buffer *buf_cap, struct v4l2_buffer *buf_out,
                              char *dstbuf);
    void onEncoderStop(struct v4l2_buffer *buf_cap, struct v4l2_buffer *buf_out);
    void enumJpegEnc();

};
#endif
