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

#define LOG_TAG "CameraUtils"

#include <log/log.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include "CameraUtils.h"

namespace android {

int32_t changeSensorFormats(int *src, int *dst, int len)
{
    if (src == NULL || dst == NULL || len == 0) {
        ALOGE("%s invalid parameters", __func__);
        return 0;
    }

    int32_t k = 0;
    for (int32_t i=0; i<len && i<MAX_SENSOR_FORMAT; i++) {
        switch (src[i]) {
            case v4l2_fourcc('N', 'V', '1', '2'):
                dst[k++] = HAL_PIXEL_FORMAT_YCbCr_420_SP;
                break;

            case v4l2_fourcc('N', 'V', '2', '1'):
                dst[k++] = HAL_PIXEL_FORMAT_YCrCb_420_SP;
                break;

            //camera service will use HAL_PIXEL_FORMAT_YV12 to match YV12 format.
            case v4l2_fourcc('Y', 'V', '1', '2'):
                dst[k++] = HAL_PIXEL_FORMAT_YV12;
                break;

            case v4l2_fourcc('Y', 'U', 'Y', 'V'):
                dst[k++] = HAL_PIXEL_FORMAT_YCbCr_422_I;
                break;

            case v4l2_fourcc('B', 'L', 'O', 'B'):
                dst[k++] = HAL_PIXEL_FORMAT_BLOB;
                break;

            case v4l2_fourcc('R', 'A', 'W', 'S'):
                dst[k++] = HAL_PIXEL_FORMAT_RAW16;
                break;
            case v4l2_fourcc('N', 'V', '1', '6'):
                dst[k++] = HAL_PIXEL_FORMAT_YCbCr_422_SP;
                break;
            case v4l2_fourcc('Y', 'U', 'V', '4'):
                dst[k++] = HAL_PIXEL_FORMAT_YCbCr_444_888;
                break;

            default:
                ALOGE("Error: format:%c%c%c%c not supported!", src[i]&0xFF,
                      (src[i]>>8)&0xFF, (src[i]>>16)&0xFF, (src[i]>>24)&0xFF);
                break;
        }
    }

    return k;
}



int getCaptureMode(int fd, int width, int height)
{
    int index = 0;
    int ret = 0;
    int capturemode = 0;
    struct v4l2_frmsizeenum cam_frmsize;

    if (fd < 0) {
        ALOGW("!!! %s, fd %d", __func__, fd);
        return 0;
    }

    while (ret == 0) {
        cam_frmsize.index = index++;
        cam_frmsize.pixel_format = v4l2_fourcc('Y', 'U', 'Y', 'V');
        ret = ioctl(fd, VIDIOC_ENUM_FRAMESIZES, &cam_frmsize);
        if ((cam_frmsize.discrete.width == (uint32_t)width) && (cam_frmsize.discrete.height == (uint32_t)height) && (ret == 0)) {
            capturemode = cam_frmsize.index;
            break;
        }
    }

    return capturemode;
}

int convertPixelFormatToV4L2Format(PixelFormat format, bool invert)
{
    int nFormat = 0;

    switch (format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCrCb_420_SP:
            // IPU doesn't support NV21, so treat this two format as the same.
            nFormat = v4l2_fourcc('N', 'V', '1', '2');
            break;

        case HAL_PIXEL_FORMAT_YCbCr_420_P:
            if (!invert) {
                nFormat = v4l2_fourcc('Y', 'U', '1', '2');
            } else {
                nFormat = v4l2_fourcc('Y', 'V', '1', '2');
            }
            break;

        case HAL_PIXEL_FORMAT_YCbCr_422_I:
            nFormat = v4l2_fourcc('Y', 'U', 'Y', 'V');
            break;
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
            nFormat = v4l2_fourcc('N', 'V', '1', '6');
            break;
        case HAL_PIXEL_FORMAT_YCbCr_444_888:
            nFormat = v4l2_fourcc('Y', 'U', 'V', '4');
            break;
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
            nFormat = v4l2_fourcc('N', 'V', '1', '2');
            break;
        case HAL_PIXEL_FORMAT_YV12:
            nFormat = v4l2_fourcc('Y', 'V', '1', '2');
            break;
        case HAL_PIXEL_FORMAT_RGBA_8888:
            nFormat = v4l2_fourcc('A', 'B', '2', '4');
            break;

        default:
            ALOGE("Error: format:0x%x not supported!", format);
            break;
    }

    ALOGV("v4l2 format: %c%c%c%c", nFormat & 0xFF, (nFormat >> 8) & 0xFF, (nFormat >> 16) & 0xFF, (nFormat >> 24) & 0xFF);
    return nFormat;
}

int getFps(int width, int height, int defValue)
{
    int fps = 0;
    if((width == 2592) && (height == 1944)) {
        fps = 8;
    } else if ((width > 1920) || (height > 1080)) {
        fps = 15;
    } else if ((width <= 1024) || (height <= 768)) {
        fps = 30;
    } else if ((defValue > 0) && (defValue <= 30)){
        fps = defValue;
    } else {
        fps = 30;
    }

    return fps;
}

int32_t getSizeByForamtRes(int32_t format, uint32_t width, uint32_t height, bool align)
{
    int32_t size = 0;
    int alignedw, alignedh, c_stride;

    if (align && (format == HAL_PIXEL_FORMAT_YCbCr_420_P)) {
        alignedw = ALIGN_PIXEL_32(width);
        alignedh = ALIGN_PIXEL_4(height);
        c_stride = (alignedw/2+15)/16*16;
        size = (alignedw + c_stride) * alignedh;
        return size;
    }

    alignedw = align ? ALIGN_PIXEL_16(width) : width;
    alignedh = align ? ALIGN_PIXEL_16(height) : height;

    switch (format) {
        case HAL_PIXEL_FORMAT_YCbCr_420_SP:
        case HAL_PIXEL_FORMAT_YCbCr_420_P:
        case HAL_PIXEL_FORMAT_YCbCr_420_888:
            size = alignedw * alignedh * 3 / 2;
            break;

        case HAL_PIXEL_FORMAT_YCbCr_422_I:
        case HAL_PIXEL_FORMAT_YCbCr_422_SP:
        case HAL_PIXEL_FORMAT_RAW16:
            size = alignedw * alignedh * 2;
            break;

        default:
            ALOGE("Error: %s format 0x%x not supported", __func__, format);
            break;
    }

    return size;
}

} // namespace android
