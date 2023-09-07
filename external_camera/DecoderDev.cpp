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

// #define LOG_NDEBUG 0
#define LOG_TAG "DecoderDev"

#include "DecoderDev.h"

#include <C2Config.h>
#include <fcntl.h>
#include <linux/imx_vpu.h>
#include <linux/videodev2.h>
#include <media/stagefright/MediaErrors.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "Imx_ext.h"
#include "graphics_ext.h"

namespace android {

const int kMaxDevicePathLen = 256;
const char *kDevicePath = "/dev/";
constexpr char kPrefix[] = "video";
constexpr int kPrefixLen = sizeof(kPrefix) - 1;

DecoderDev::DecoderDev() {
    memset((char *)mDevName, 0, MAX_DEV_NAME_LEN);
    mFd = -1;
    mEventFd = -1;
    mStreamType = V4L2_PIX_FMT_H264;
    mOutBufType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    mCapBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    mSocType = IMX8MQ;
}

int32_t DecoderDev::Open() {
    ALOGV("%s: DecoderDev Open BEGIN", __func__);

    if (OK != GetNode()) return -1;

    ALOGD("%s: open dev name %s", __func__, (char *)mDevName);

    mFd = open((char *)mDevName, O_RDWR | O_NONBLOCK);

    if (mFd > 0) {
        struct v4l2_event_subscription sub;
        memset(&sub, 0, sizeof(struct v4l2_event_subscription));

        sub.type = V4L2_EVENT_SOURCE_CHANGE;
        int32_t ret = ioctl(mFd, VIDIOC_SUBSCRIBE_EVENT, &sub);
        if (ret < 0) {
            ALOGE("%s: VIDIOC_SUBSCRIBE_EVENT Failed: %s", __func__, strerror(errno));
            return ret;
        }

        if (mSocType == IMX8QM) {
            // 8qm not support CODEC_ERROR SKIP
            return mFd;
        }

        sub.type = V4L2_EVENT_CODEC_ERROR;
        ret = ioctl(mFd, VIDIOC_SUBSCRIBE_EVENT, &sub);
        if (ret < 0) {
            ALOGE("%s: VIDIOC_SUBSCRIBE_EVENT Failed: %s", __func__, strerror(errno));
            return ret;
        }
        sub.type = V4L2_EVENT_SKIP;
        ret = ioctl(mFd, VIDIOC_SUBSCRIBE_EVENT, &sub);
        if (ret < 0) {
            ALOGE("%s: VIDIOC_SUBSCRIBE_EVENT Failed: %s", __func__, strerror(errno));
            return ret;
        }
    }

    return mFd;
}

status_t DecoderDev::Close() {
    if (mFd >= 0) {
        close(mFd);
        mFd = -1;
    }

    if (mEventFd >= 0) {
        close(mEventFd);
        mEventFd = -1;
    }
    return OK;
}

status_t DecoderDev::GetVideoBufferType(enum v4l2_buf_type *outType, enum v4l2_buf_type *capType) {
    struct v4l2_capability cap;

    if (ioctl(mFd, VIDIOC_QUERYCAP, &cap) != 0) {
        ALOGE("%s failed", __func__);
        return BAD_VALUE;
    }

    if (cap.capabilities & (V4L2_CAP_VIDEO_CAPTURE_MPLANE | V4L2_CAP_VIDEO_OUTPUT_MPLANE) &&
        cap.capabilities & V4L2_CAP_STREAMING) {
        mCapBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        mOutBufType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    } else if (cap.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE) {
        mCapBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        mOutBufType = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    } else {
        mCapBufType = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        mOutBufType = V4L2_BUF_TYPE_VIDEO_OUTPUT;
    }

    *outType = mOutBufType;
    *capType = mCapBufType;

    return OK;
}

bool DecoderDev::isDecoderDevice(const char *devName) {
    int32_t ret = -1;
    struct v4l2_capability vidCap;
    bool isDecNode = false;

    base::unique_fd fd(::open(devName, O_RDWR | O_NONBLOCK));
    if (fd.get() < 0) {
        ALOGE("%s open dev path:%s failed:%s", __func__, devName, strerror(errno));
        return false;
    }

    ret = ioctl(fd.get(), VIDIOC_QUERYCAP, &vidCap);
    if (ret < 0) {
        ALOGE("%s QUERYCAP dev path:%s failed", __func__, devName);
        return false;
    }
    ALOGI("%s: name=%s, card name=%s, bus info %s\n", __func__, (char *)vidCap.driver,
          (char *)vidCap.card, (char *)vidCap.bus_info);
    if (mSocType == IMX8QM) {
        isDecNode = (!strcmp((char *)vidCap.card, "mxc-jpeg codec") &&
                     (strstr((char *)vidCap.bus_info, "jpegdec") != NULL));
    } else {
        isDecNode = (!strcmp((char *)vidCap.card, "vsi_v4l2dec") ||
                     !strcmp((char *)vidCap.card, "vpu B0"));
    }
    if (isDecNode) return true;

    return false;
}

status_t DecoderDev::GetNode() {
    bool mDecoderGet = false;

    DIR *devdir = opendir(kDevicePath);
    if (devdir == 0) {
        ALOGE("%s: cannot open %s! Exiting", __func__, kDevicePath);
        return UNKNOWN_ERROR;
    }

    struct dirent *de;
    while ((de = readdir(devdir)) != 0) {
        if (!strncmp(kPrefix, de->d_name, kPrefixLen)) {
            ALOGV("%s: v4l device %s found", __func__, de->d_name);
            char DecoderDevicePath[kMaxDevicePathLen];
            snprintf(DecoderDevicePath, kMaxDevicePathLen, "%s%s", kDevicePath, de->d_name);
            if (isDecoderDevice(DecoderDevicePath)) {
                ALOGI("%s DecoderDevicePath:%s", __func__, DecoderDevicePath);
                strcpy((char *)mDevName, DecoderDevicePath);
                mDecoderGet = true;
                break;
            }
        }
    }
    closedir(devdir);

    if (mDecoderGet)
        return OK;
    else
        return UNKNOWN_ERROR;
}

status_t DecoderDev::QueryFormats(uint32_t format_type) {
    struct v4l2_fmtdesc fmt;
    int32_t i = 0;
    if (format_type == V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE ||
        format_type == V4L2_BUF_TYPE_VIDEO_OUTPUT) {
        output_formats.clear();
        while (true) {
            fmt.type = format_type;
            fmt.index = i;
            if (ioctl(mFd, VIDIOC_ENUM_FMT, &fmt) < 0) {
                ALOGE("%s: VIDIOC_ENUM_FMT fail", __func__);
                break;
            }

            output_formats.push_back(fmt.pixelformat);
            ALOGI("%s: add output format %x,  %s\n", __func__, fmt.pixelformat, fmt.description);
            i++;
        }
        if (output_formats.size() > 0)
            return OK;
        else
            return UNKNOWN_ERROR;
    } else if (format_type == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE ||
               format_type == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
        capture_formats.clear();
        while (true) {
            fmt.type = format_type;
            fmt.index = i;
            if (ioctl(mFd, VIDIOC_ENUM_FMT, &fmt) < 0) break;

            capture_formats.push_back(fmt.pixelformat);
            ALOGI("%s: add capture format %x,  %s\n", __func__, fmt.pixelformat, fmt.description);
            i++;
        }

        if (capture_formats.size() > 0)
            return OK;
        else
            return UNKNOWN_ERROR;
    }
    return BAD_TYPE;
}

bool DecoderDev::IsOutputFormatSupported(uint32_t format) {
    ALOGV("%s: format=%x", __func__, format);
    if (output_formats.empty()) {
        status_t ret = QueryFormats(mOutBufType);
        if (ret != OK) return false;
    }

    for (uint32_t i = 0; i < output_formats.size(); i++) {
        if (format == output_formats.at(i)) {
            return true;
        }
    }

    return false;
}

bool DecoderDev::IsCaptureFormatSupported(uint32_t format) {
    ALOGV("%s: format=%x", __func__, format);
    if (capture_formats.empty()) {
        status_t ret = QueryFormats(mCapBufType);
        if (ret != OK) return false;
    }

    for (uint32_t i = 0; i < capture_formats.size(); i++) {
        if (format == capture_formats.at(i)) {
            return true;
        }
    }
    return false;
}

typedef struct {
    uint32_t noncontiguous_format;
    uint32_t contiguous_format;
} CONTINUGUOUS_FORMAT_TABLE;

// TODO: add android pixel format
static const CONTINUGUOUS_FORMAT_TABLE contiguous_format_table[] = {
        {V4L2_PIX_FMT_NV12M, V4L2_PIX_FMT_NV12},
        {V4L2_PIX_FMT_YUV420M, V4L2_PIX_FMT_YUV420},
        {V4L2_PIX_FMT_YVU420M, V4L2_PIX_FMT_YVU420},
        {V4L2_PIX_FMT_NV12M_8L128, V4L2_PIX_FMT_NV12_8L128},
        {V4L2_PIX_FMT_NV12M_10BE_8L128, V4L2_PIX_FMT_NV12_10BE_8L128},
};

status_t DecoderDev::GetContiguousV4l2Format(uint32_t format, uint32_t *contiguous_format) {
    status_t ret = BAD_VALUE;
    for (size_t i = 0; i < sizeof(contiguous_format_table) / sizeof(CONTINUGUOUS_FORMAT_TABLE);
         i++) {
        if (format == contiguous_format_table[i].noncontiguous_format) {
            *contiguous_format = contiguous_format_table[i].contiguous_format;
            ret = OK;
            break;
        }
    }

    if (ret) ALOGE("unknown contiguous v4l2 format 0x%x", format);

    return ret;
}

status_t DecoderDev::GetCaptureFormat(uint32_t *format, uint32_t i) {
    status_t ret = OK;

    if (capture_formats.empty()) {
        ret = QueryFormats(mCapBufType);
        if (ret != OK) return ret;
    }

    if (i >= capture_formats.size()) return BAD_VALUE;

    *format = capture_formats.at(i);

    return ret;
}

status_t DecoderDev::GetColorFormatByV4l2(uint32_t v4l2_format, uint32_t *color_format,
                                          COLOR_FORMAT_TABLE *color_format_table,
                                          uint8_t tableSize) {
    for (size_t i = 0; i < tableSize; i++) {
        if (v4l2_format == color_format_table[i].v4l2_format) {
            *color_format = color_format_table[i].color_format;
            return OK;
        }
    }
    *color_format = 0;
    return ERROR_UNSUPPORTED;
}

status_t DecoderDev::GetV4l2FormatByColor(uint32_t color_format, uint32_t *v4l2_format,
                                          COLOR_FORMAT_TABLE *color_format_table,
                                          uint8_t tableSize) {
    for (size_t i = 0; i < tableSize; i++) {
        if (color_format == color_format_table[i].color_format) {
            *v4l2_format = color_format_table[i].v4l2_format;
            return OK;
        }
    }
    *v4l2_format = 0;
    return ERROR_UNSUPPORTED;
}

status_t DecoderDev::GetFormatFrameInfo(uint32_t format, struct v4l2_frmsizeenum *info) {
    if (info == NULL) return BAD_TYPE;

    info->index = 0;
    info->type = V4L2_FRMSIZE_TYPE_STEPWISE;
    info->pixel_format = format;

    if (0 == ioctl(mFd, VIDIOC_ENUM_FRAMESIZES, info)) {
        return OK;
    }

    return UNKNOWN_ERROR;
}

uint32_t DecoderDev::Poll() {
    uint32_t ret = V4L2_DEV_POLL_NONE;
    int result;
    struct pollfd pfd[2];
    struct timespec ts;
    ts.tv_sec = 0; // default timeout 1 seconds
    ts.tv_nsec = 400000000;

    pfd[0].fd = mFd;
    pfd[0].events = POLLERR | POLLNVAL | POLLHUP;
    pfd[0].revents = 0;

    pfd[0].events |= POLLOUT | POLLPRI | POLLWRNORM;
    pfd[0].events |= POLLIN | POLLRDNORM;

    pfd[1].fd = mEventFd;
    pfd[1].events = POLLIN | POLLERR;

    ALOGV("%s: BEGIN %p\n", __func__, this);
    result = ppoll(&pfd[0], 2, &ts, NULL);

    if (result <= 0) {
        ret = V4L2_DEV_POLL_NONE;
    } else {
        if (pfd[1].revents & POLLERR) {
            ret = V4L2_DEV_POLL_NONE;
            return ret;
        }

        if (pfd[0].revents & POLLPRI) {
            ALOGV("[%p]POLLPRI \n", this);
            ret |= V4L2_DEV_POLL_EVENT;
        }

        if ((pfd[0].revents & POLLIN) || (pfd[0].revents & POLLRDNORM)) {
            ret |= V4L2_DEV_POLL_CAPTURE;
        }
        if ((pfd[0].revents & POLLOUT) || (pfd[0].revents & POLLWRNORM)) {
            ret |= V4L2_DEV_POLL_OUTPUT;
        }

        if (pfd[0].revents & POLLERR) {
            if (V4L2_DEV_POLL_NONE == ret) {
                usleep(2000);
            } else
                ALOGE("%s: err has other flag 0x%x", __func__, pfd[0].revents);
        }
    }

    ALOGV("%s: END,ret=%x", __func__, ret);
    return ret;
}

status_t DecoderDev::SetPollInterrupt() {
    if (mEventFd > 0) {
        const uint64_t buf = EFD_CLOEXEC | EFD_NONBLOCK;
        eventfd_write(mEventFd, buf);
    }
    return OK;
}

status_t DecoderDev::ClearPollInterrupt() {
    if (mEventFd > 0) {
        uint64_t buf;
        eventfd_read(mEventFd, &buf);
    }
    return OK;
}

status_t DecoderDev::ResetDecoder() {
    int ret = 0;
    struct v4l2_decoder_cmd cmd;
    memset(&cmd, 0, sizeof(struct v4l2_decoder_cmd));

    cmd.cmd = V4L2_DEC_CMD_RESET;
    cmd.flags = V4L2_DEC_CMD_STOP_IMMEDIATELY;

    ret = ioctl(mFd, VIDIOC_DECODER_CMD, &cmd);
    if (ret < 0) {
        ALOGE("%s: ret=%x", __func__, ret);
        return UNKNOWN_ERROR;
    }

    ALOGV("%s: SUCCESS", __func__);
    return OK;
}

status_t DecoderDev::StopDecoder() {
    int ret = 0;
    struct v4l2_decoder_cmd cmd;
    memset(&cmd, 0, sizeof(struct v4l2_decoder_cmd));

    cmd.cmd = V4L2_DEC_CMD_STOP;
    cmd.flags = V4L2_DEC_CMD_STOP_IMMEDIATELY;

    ret = ioctl(mFd, VIDIOC_DECODER_CMD, &cmd);
    if (ret < 0) {
        ALOGE("%s: ret=%x", __func__, ret);
        return UNKNOWN_ERROR;
    }

    ALOGV("%s: SUCCESS", __func__);
    return OK;
}

} // namespace android
