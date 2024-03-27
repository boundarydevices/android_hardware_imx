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

cameraconfigparser::PhysicalMetaMapPtr ClonePhysicalDeviceMap(
        const cameraconfigparser::PhysicalMetaMapPtr &src) {
    auto ret = std::make_unique<cameraconfigparser::PhysicalMetaMap>();
    for (const auto &it : *src) {
        ret->emplace(it.first, HalCameraMetadata::Clone(it.second.get()));
    }
    return ret;
}

int AllocPhyBuffer(ImxStreamBuffer &imxBuf) {
    ALOGE("%s: not supported on evk_95");
    return -1;
}

int FreePhyBuffer(ImxStreamBuffer &imxBuf) {
    ALOGE("%s: not supported on evk_95");
    return -1;
}

void SwitchImxBuf(ImxStreamBuffer &imxBufA, ImxStreamBuffer &imxBufB) {
    ImxStreamBuffer tmpBuf = imxBufA;
    imxBufA = imxBufB;
    imxBufB = tmpBuf;

    return;
}

int32_t ImageBufferToStreamBuffer(ImxImageBuffer &imageBuffer, ImxStreamBuffer &streamBuffer) {
    ImxStream *stream = streamBuffer.mStream;
    if (stream == NULL) {
        ALOGE("%s: stream is NULL", __func__);
        return -EINVAL;
    }

    stream->mFormat = imageBuffer.mFormat;
    stream->mWidth = imageBuffer.mWidth;
    stream->mHeight = imageBuffer.mHeight;
    stream->mUsage = imageBuffer.mUsage;
    streamBuffer.mVirtAddr = imageBuffer.mVirtAddr;
    streamBuffer.mPhyAddr = imageBuffer.mPhyAddr;
    streamBuffer.mFd = imageBuffer.mFd;
    streamBuffer.mSize = imageBuffer.mSize;
    streamBuffer.mFormatSize = imageBuffer.mFormatSize;
    streamBuffer.buffer = imageBuffer.buffer;
    stream->mZoomRatio = imageBuffer.mZoomRatio;

    return 0;
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

unique_private_handle MaliAllocBuffer(uint32_t width, uint32_t height, uint64_t format, uint64_t usage) {
    buffer_descriptor_t descriptor = {0};
    descriptor.width = width;
    descriptor.height = height;
    descriptor.producer_usage = usage |  GRALLOC_USAGE_PRIVATE_3;
    descriptor.consumer_usage = descriptor.producer_usage;
    descriptor.hal_format = format;
    descriptor.layer_count = 1;

    unique_private_handle uniq_hnd = mali_gralloc_buffer_allocate(&descriptor);
    if (uniq_hnd == nullptr) {
      ALOGE("%s: mali_gralloc_buffer_allocate failed, width %d, height %d, format 0x%lx, usage 0x%lx", __func__, width, height, format, usage);
      return nullptr;
    }

    ALOGI("%s: width %d, height %d, format 0x%lx, usage 0x%lx", __func__, width, height, format, usage); 

    return std::move(uniq_hnd);
}

void MaliFreeBuffer(unique_private_handle uniq_hnd) {
    if (uniq_hnd == NULL)
        return;

    buffer_handle_t handle = uniq_hnd.get();
    int numFds = handle->numFds;
    for (int i = 0; i < numFds; i++) {
        close(handle->data[i]);
    }

    uniq_hnd.reset();
    return;
}

int GetDMAAddr(int fd, uint32_t size, uint32_t offset, uint64_t& addr, void **virt)
{
    uint64_t phyAddr = -1;

    if (fd < 0) {
        ALOGE("%s invalid parameters", __func__);
        return -EINVAL;
    }

    struct dmabuf_imx_phys_data data;
    int fd_;

    fd_ = open("/dev/dmabuf_imx", O_RDONLY | O_CLOEXEC);
    if (fd_ < 0) {
        ALOGE("open /dev/dmabuf_imx failed: %s", strerror(errno));
        return -EINVAL;
    }

    data.dmafd = fd;
    if (ioctl(fd_, DMABUF_GET_PHYS, &data) < 0) {
        ALOGE("%s DMABUF_GET_PHYS  failed",__func__);
        close(fd_);
        return -EINVAL;
    } else
        phyAddr = data.phys;

    if (virt)
        *virt = (void *)mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, offset);

    close(fd_);

    addr = phyAddr;

    return 0;
}

ImxStreamBuffer *CreateImxStreamBufferFromStreamBuffer(
        buffer_handle_t buffer, uint32_t size, uint32_t width, uint32_t height, int32_t format, uint32_t usage) {
    bool bPreview = false;

    if (buffer == NULL)
        return NULL;

    ImxStreamBuffer *imxBuf = new ImxStreamBuffer();
    if (imxBuf == NULL)
        return NULL;

    uint64_t addr = 0;
    void *virt = NULL;
    int fd = buffer->data[0];

    /* only for YUYV, just 1 fd */
    int ret = GetDMAAddr(fd, size, 0, addr, &virt);
    if (ret) {
        ALOGE("%s: GetDMAAddr failed, ret %d, fd %d, size %d", __func__, ret, fd, size);
        goto error;
    }

    ALOGI("%s: GetDMAAddr ret %d, fd %d, size %d", __func__, ret, fd, size);

    imxBuf->buffer = buffer;
    imxBuf->mVirtAddr = virt;
    imxBuf->mPhyAddr = addr;
    imxBuf->mSize = size;
    imxBuf->mFormatSize = imxBuf->mSize;

    imxBuf->mStream = new ImxStream(width, height, format, usage, 0, false);

    if (imxBuf->mStream == NULL)
        goto error;

    goto finish;

error:
    if (imxBuf)
        free(imxBuf);

    return NULL;

finish:
    return imxBuf;
}

void ReleaseImxStreamBuffer(ImxStreamBuffer *imxBuf) {
    if (imxBuf == NULL)
        return;

    if (imxBuf->mStream)
        delete (imxBuf->mStream);

    delete imxBuf;
}


} // namespace android
