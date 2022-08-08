/*
 *  Copyright 2021-2022 NXP.
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

#ifndef V4L2_DEV_H
#define V4L2_DEV_H
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <linux/videodev2.h>
#include <utils/Errors.h>
#include <fcntl.h>
#include <log/log.h>
#include <vector>
#include <android-base/file.h>
#include <android-base/file.h>
#include <android-base/strings.h>

namespace android {

#define V4L2_DEV_POLL_NONE 0
#define V4L2_DEV_POLL_EVENT 1
#define V4L2_DEV_POLL_OUTPUT 2
#define V4L2_DEV_POLL_CAPTURE 4

#define MAX_DEV_NAME_LEN (256)
#define V4L2_FORMAT_IS_NON_CONTIGUOUS_PLANES(fmt) \
	((fmt) == V4L2_PIX_FMT_NV12M        \
	 || (fmt) == V4L2_PIX_FMT_YUV420M   \
     || (fmt) == V4L2_PIX_FMT_YVU420M   \
	 || (fmt) == V4L2_PIX_FMT_NV12M_8L128   \
     || (fmt) == V4L2_PIX_FMT_NV12M_10BE_8L128)

typedef struct {
    uint32_t color_format;
    uint32_t v4l2_format;
} COLOR_FORMAT_TABLE;

enum socType {
    IMX8MQ = 0,
    IMX8QM = 1,
};

class DecoderDev {
public:
    explicit DecoderDev();
    int32_t mSocType;
    int32_t Open();
    status_t Close();
    status_t GetVideoBufferType(enum v4l2_buf_type *outType, enum v4l2_buf_type *capType);
    bool IsOutputFormatSupported(uint32_t format);
    bool IsCaptureFormatSupported(uint32_t format);

    status_t GetContiguousV4l2Format(uint32_t format, uint32_t *contiguous_format);
    status_t GetCaptureFormat(uint32_t *format, uint32_t i);
    status_t GetFormatFrameInfo(uint32_t format, struct v4l2_frmsizeenum * info);
    status_t GetColorFormatByV4l2(uint32_t v4l2_format, uint32_t * color_format,
        COLOR_FORMAT_TABLE *color_format_table, uint8_t tableSize);
    status_t GetV4l2FormatByColor(uint32_t color_format, uint32_t * v4l2_format,
        COLOR_FORMAT_TABLE *color_format_table, uint8_t tableSize);

    uint32_t Poll();
    status_t SetPollInterrupt();
    status_t ClearPollInterrupt();
    status_t ResetDecoder();
    status_t StopDecoder();

private:
    char mDevName[MAX_DEV_NAME_LEN];
    int32_t mFd;
    int32_t mEventFd;
    int32_t mStreamType;
    enum v4l2_buf_type mOutBufType;
    enum v4l2_buf_type mCapBufType;
    std::vector<uint32_t> output_formats;
    std::vector<uint32_t> capture_formats;

    status_t GetNode();
    bool isDecoderDevice(const char* devName);
    status_t QueryFormats(uint32_t format_type);
};

}
#endif
