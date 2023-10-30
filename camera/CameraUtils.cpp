/*
 *  Copyright 2020-2023 NXP.
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

#include "CameraUtils.h"

#include <linux/videodev2.h>
#include <log/log.h>
#include <sys/ioctl.h>

#include "Allocator.h"
#include "Memory.h"
#include "MemoryDesc.h"
#include "NV12_resize.h"

namespace android {

int32_t changeSensorFormats(int *src, int *dst, int len) {
    if (src == NULL || dst == NULL || len == 0) {
        ALOGE("%s invalid parameters", __func__);
        return 0;
    }

    int32_t k = 0;
    for (int32_t i = 0; i < len && i < MAX_SENSOR_FORMAT; i++) {
        switch (src[i]) {
            case v4l2_fourcc('N', 'V', '1', '2'):
                dst[k++] = HAL_PIXEL_FORMAT_YCbCr_420_SP;
                break;

            case v4l2_fourcc('N', 'V', '2', '1'):
                dst[k++] = HAL_PIXEL_FORMAT_YCrCb_420_SP;
                break;

            // camera service will use HAL_PIXEL_FORMAT_YV12 to match YV12 format.
            case v4l2_fourcc('Y', 'V', '1', '2'):
                dst[k++] = HAL_PIXEL_FORMAT_YV12;
                break;

            case v4l2_fourcc('Y', 'U', 'Y', 'V'):
                dst[k++] = HAL_PIXEL_FORMAT_YCbCr_422_I;
                break;

            case v4l2_fourcc('B', 'L', 'O', 'B'):
                dst[k++] = HAL_PIXEL_FORMAT_BLOB;
                break;

            case v4l2_fourcc('N', 'V', '1', '6'):
                dst[k++] = HAL_PIXEL_FORMAT_YCbCr_422_SP;
                break;
            case v4l2_fourcc('Y', 'U', 'V', '4'):
                dst[k++] = HAL_PIXEL_FORMAT_YCbCr_444_888;
                break;

            default:
                ALOGE("Error: format:%c%c%c%c not supported!", src[i] & 0xFF, (src[i] >> 8) & 0xFF,
                      (src[i] >> 16) & 0xFF, (src[i] >> 24) & 0xFF);
                break;
        }
    }

    return k;
}

int getCaptureMode(int fd, int width, int height) {
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
        if ((cam_frmsize.discrete.width == (uint32_t)width) &&
            (cam_frmsize.discrete.height == (uint32_t)height) && (ret == 0)) {
            capturemode = cam_frmsize.index;
            break;
        }
    }

    return capturemode;
}

cameraconfigparser::PhysicalMetaMapPtr ClonePhysicalDeviceMap(
        const cameraconfigparser::PhysicalMetaMapPtr &src) {
    auto ret = std::make_unique<cameraconfigparser::PhysicalMetaMap>();
    for (const auto &it : *src) {
        ret->emplace(it.first, HalCameraMetadata::Clone(it.second.get()));
    }
    return ret;
}

int AllocPhyBuffer(ImxStreamBuffer &imxBuf) {
    int sharedFd;
    uint64_t phyAddr;
    uint64_t outPtr;
    uint32_t ionSize = imxBuf.mSize;

    fsl::Allocator *allocator = fsl::Allocator::getInstance();
    if (allocator == NULL) {
        ALOGE("%s ion allocator invalid", __func__);
        return -1;
    }

    sharedFd = allocator->allocMemory(ionSize, MEM_ALIGN, fsl::MFLAGS_CONTIGUOUS);
    if (sharedFd < 0) {
        ALOGE("%s: allocMemory failed.", __func__);
        return -1;
    }

    int err = allocator->getVaddrs(sharedFd, ionSize, outPtr);
    if (err != 0) {
        ALOGE("%s: getVaddrs failed.", __func__);
        close(sharedFd);
        return -1;
    }

    err = allocator->getPhys(sharedFd, ionSize, phyAddr);
    if (err != 0) {
        ALOGE("%s: getPhys failed.", __func__);
        munmap((void *)(uintptr_t)outPtr, ionSize);
        close(sharedFd);
        return -1;
    }

    ALOGV("%s, outPtr:%p,  phy:%p, ionSize:%d, req:%zu\n", __func__, (void *)outPtr,
          (void *)phyAddr, ionSize, imxBuf.mFormatSize);

    imxBuf.mVirtAddr = (void *)outPtr;
    imxBuf.mPhyAddr = phyAddr;
    imxBuf.mFd = sharedFd;
    SetBufferHandle(imxBuf);

    return 0;
}

int FreePhyBuffer(ImxStreamBuffer &imxBuf) {
    if (imxBuf.mVirtAddr)
        munmap(imxBuf.mVirtAddr, imxBuf.mSize);

    if (imxBuf.mFd > 0)
        close(imxBuf.mFd);

    fsl::Memory *handle = (fsl::Memory *)imxBuf.buffer;
    if (handle)
        delete handle;

    return 0;
}

void SetBufferHandle(ImxStreamBuffer &imxBuf) {
    fsl::MemoryDesc desc;
    fsl::Memory *handle = NULL;

    desc.mFlag = 0;
    desc.mWidth = desc.mStride = imxBuf.mSize / 4;
    desc.mHeight = 1;
    desc.mFormat = HAL_PIXEL_FORMAT_RGBA_8888;
    desc.mFslFormat = fsl::FORMAT_RGBA8888;
    desc.mSize = imxBuf.mSize;
    desc.mProduceUsage = 0;

    handle = new fsl::Memory(&desc, imxBuf.mFd, -1);
    imxBuf.buffer = (buffer_handle_t)handle;
}

void SwitchImxBuf(ImxStreamBuffer &imxBufA, ImxStreamBuffer &imxBufB) {
    ImxStreamBuffer tmpBuf = imxBufA;
    imxBufA = imxBufB;
    imxBufB = tmpBuf;

    return;
}

static int32_t StreamBufferToImageBuffer(ImxStreamBuffer &streamBuffer, ImxImageBuffer &imageBuffer) {
    ImxStream *stream = streamBuffer.mStream;
    if (stream == NULL) {
        ALOGE("%s: stream is NULL", __func__);
        return -EINVAL;
    }

    imageBuffer.mFormat = stream->format();
    imageBuffer.mWidth = stream->width();
    imageBuffer.mHeight = stream->height();
    imageBuffer.mStride = stream->width();
    imageBuffer.mHeightSpan = stream->height();
    imageBuffer.mVirtAddr = streamBuffer.mVirtAddr;
    imageBuffer.mPhyAddr = streamBuffer.mPhyAddr;
    imageBuffer.mFd = streamBuffer.mFd;
    imageBuffer.mSize = streamBuffer.mSize;
    imageBuffer.mFormatSize = streamBuffer.mFormatSize;
    imageBuffer.buffer = streamBuffer.buffer;
    imageBuffer.mZoomRatio = stream->mZoomRatio;
    imageBuffer.mUsage = stream->usage();
    imageBuffer.mPrivate = NULL;

    return 0;
}

int32_t handleFrame(ImxStreamBuffer &dstBuf, ImxStreamBuffer &srcBuf, ImxEngine engine) {
    fsl::ImageProcess *imageProcess = fsl::ImageProcess::getInstance();

    ImxImageBuffer imageBufferSrc;
    ImxImageBuffer imageBufferDst;

    StreamBufferToImageBuffer(srcBuf, imageBufferSrc);
    StreamBufferToImageBuffer(dstBuf, imageBufferDst);

    return imageProcess->ConvertImage(imageBufferDst, imageBufferSrc, engine);
}

} // namespace android
