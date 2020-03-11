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

#include <dirent.h>
#include <linux/v4l2-common.h>
#include <linux/v4l2-mediabus.h>
#include <linux/v4l2-subdev.h>
#include <cutils/properties.h>
#include <android-base/strings.h>
#include <android-base/file.h>
#include "CameraUtils.h"
#include "HwJpegEncoder.h"

#define NUM_BUFS 1
#define JPEG_ENC_NAME "mxc-jpeg-enc"


HwJpegEncoder::HwJpegEncoder(int format) : YuvToJpegEncoder(){
    // convert the camera hal format to v4l2 format.
    mFormat = convertPixelFormatToV4L2Format(format);
}

int HwJpegEncoder::encode(void *inYuv,
               void* inYuvPhy,
               int   inWidth,
               int   inHeight,
               int   quality,
               void *outBuf,
               int   outSize,
               int   outWidth,
               int   outHeight) {
    struct encoder_args encoder_parameter;

    struct v4l2_buffer bufferin;
    struct v4l2_buffer bufferout;
    int jpeg_size = 0;
    int err;

    uint8_t *resize_src = NULL;

    // need resize the width&height before do hw jpeg encoder.
    // the resolution for input and out need to been align when do jpeg encode.
    if ((inWidth != outWidth) || (inHeight != outHeight)) {
        resize_src = (uint8_t *)malloc(outSize);
        yuvResize((uint8_t *)inYuv,
            inWidth,
            inHeight,
            resize_src,
            outWidth,
            outHeight);
        inYuv = resize_src;
    }

    encoder_parameter.width = outWidth;
    encoder_parameter.height = outHeight;
    get_out_buffer_size(&encoder_parameter, mFormat);

    err = onEncoderConfig(&encoder_parameter, (char *)inYuv, &bufferin, &bufferout);
    if (err < 0) {
        ALOGE("encoder configure failed");
        goto failed;
    }

    jpeg_size = onEncoderStart(&bufferin, &bufferout, (char *)outBuf);

    onEncoderStop(&bufferin, &bufferout);

failed:
    if (mJpegFd > 0)
        close(mJpegFd);

    return jpeg_size;
}

void HwJpegEncoder::v4l2_mmap(int vdev_fd, struct v4l2_buffer *buf,
                              void *buf_start[])
{
    unsigned int i;

    /* multi-planar */
    for (i = 0; i < buf->length; i++) {
        buf_start[i] = mmap(NULL,
                            buf->m.planes[i].length, /* set by driver */
                            PROT_READ | PROT_WRITE,
                            MAP_SHARED,
                            vdev_fd,
                            buf->m.planes[i].m.mem_offset);
        if (buf_start[i] == MAP_FAILED) {
            ALOGE("mmap in, multi-planar");
            return;
        }

        /* empty capture buffer */
        memset(buf_start[i], 0, buf->m.planes[i].length);
    }
}

void HwJpegEncoder::v4l2_munmap(struct v4l2_buffer *buf,
                                void *buf_start[])
{
    unsigned int i;

    for (i = 0; i < buf->length; i++) {
        munmap(buf_start[i], buf->m.planes[i].length);
        ALOGI("munmap multi-planes %d\n", i);
    }
}

void HwJpegEncoder::get_out_buffer_size(struct encoder_args* ec_args, int fmt)
{
    switch(fmt) {
        case V4L2_PIX_FMT_YUYV:
            ec_args->size = ec_args->width * ec_args->height * 2;
            break;
        case V4L2_PIX_FMT_YUV24:
            ec_args->size = ec_args->width * ec_args->height * 3;
            break;
        case V4L2_PIX_FMT_NV12:
            ec_args->size = ec_args->width * ec_args->height * 3 / 2;
            break;
        default:
            ALOGE("%s unsupport format %d", __func__, fmt);
    }
}

int HwJpegEncoder::onEncoderConfig(struct encoder_args *ea, char *srcbuf, struct v4l2_buffer *bufferin,
                                          struct v4l2_buffer *bufferout){
    struct v4l2_capability capabilities;
    struct v4l2_format out_fmt;
    struct v4l2_format cap_fmt;
    struct v4l2_requestbuffers bufreq_cap;
    struct v4l2_requestbuffers bufreq_out;
    bool support_m2m;
    bool support_mp;

    enumJpegEnc();

    mJpegFd = open(mJpegDevPath, O_RDWR);
    if (mJpegFd < 0) {
        ALOGI("Could not open video device \n");
        goto failed;
    }

    if (ioctl(mJpegFd, VIDIOC_QUERYCAP, &capabilities) < 0) {
        ALOGE("VIDIOC_QUERYCAP failed ");
        goto failed;
    }

    support_m2m = capabilities.capabilities & V4L2_CAP_VIDEO_M2M;
    support_mp = capabilities.capabilities & V4L2_CAP_VIDEO_M2M_MPLANE;
    if (!support_m2m && !support_mp) {
        ALOGE(
            "Device doesn't handle M2M video capture\n");
        goto failed;
    }

    // out_fmt need to set 0, otherwise the request buffer will failed.
    memset(&out_fmt, 0, sizeof(out_fmt));

    out_fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
    out_fmt.fmt.pix_mp.pixelformat = mFormat;
    out_fmt.fmt.pix_mp.num_planes = 1;
    out_fmt.fmt.pix_mp.width = ea->width;
    out_fmt.fmt.pix_mp.height = ea->height;

    if (ioctl(mJpegFd, VIDIOC_S_FMT, &out_fmt) < 0) {
        ALOGE("VIDIOC_S_FMT failed for out fmt");
        goto failed;
    }

    // cap_fmt need to set 0, otherwise the request buffer will failed.
    memset(&cap_fmt, 0, sizeof(cap_fmt));

    cap_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    cap_fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_JPEG;
    cap_fmt.fmt.pix_mp.num_planes = 1;
    cap_fmt.fmt.pix_mp.width = ea->width;
    cap_fmt.fmt.pix_mp.height = ea->height;


    if (ioctl(mJpegFd, VIDIOC_S_FMT, &cap_fmt) < 0) {
        ALOGE("VIDIOC_S_FMT failed for cap fmt");
        goto failed;
    }

    /* The reserved array must be zeroed */
    memset(&bufreq_cap, 0, sizeof(bufreq_cap));
    memset(&bufreq_out, 0, sizeof(bufreq_out));

    /* the capture buffer is filled by the driver with data from device */
    bufreq_cap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    bufreq_cap.memory = V4L2_MEMORY_MMAP;
    // current NUM_BUFS value is 1
    // This may been set as 2 or 3 in camera preview/recording case.
    bufreq_cap.count = NUM_BUFS;

    if (ioctl(mJpegFd, VIDIOC_REQBUFS, &bufreq_cap) < 0) {
        ALOGE("VIDIOC_REQBUFS failed for cap stream");
        goto failed;
    }

    /*
     * the output buffer is filled by the application
     * and the driver sends it to the device, for processing
     */
    bufreq_out.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    bufreq_out.memory = V4L2_MEMORY_MMAP;
    // current NUM_BUFS value is 1
    // This may been set as 2 or 3 in camera preview/recording case.
    bufreq_out.count = NUM_BUFS;

    if (ioctl(mJpegFd, VIDIOC_REQBUFS, &bufreq_out) < 0) {
        ALOGE("VIDIOC_REQBUFS failed for out stream");
        goto failed;
    }

    /* the capture buffer is filled by the driver */
    memset(bufferin, 0, sizeof(*bufferin));
    bufferin->type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    /* bytesused set by the driver for capture stream */
    bufferin->memory = V4L2_MEMORY_MMAP;
    bufferin->bytesused = 0;
    bufferin->index = 0;
    bufferin->length = 1;
    bufferin->m.planes = (struct v4l2_plane *)calloc(bufferin->length, sizeof(struct v4l2_plane));

    if (ioctl(mJpegFd, VIDIOC_QUERYBUF, bufferin) < 0) {
        ALOGE("VIDIOC_QUERYBUF failed for ");
        goto failed;
    }

    ALOGI("Plane 0 bytesused=%d, length=%d, data_offset=%d",
               bufferin->m.planes[0].bytesused,
               bufferin->m.planes[0].length,
               bufferin->m.planes[0].data_offset);

    memset(bufferout, 0, sizeof(*bufferout));
    bufferout->type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    bufferout->memory = V4L2_MEMORY_MMAP;
    bufferout->index = 0;
    bufferout->length = 1;
    bufferout->m.planes = (struct v4l2_plane *)calloc(bufferout->length,
                                 sizeof(struct v4l2_plane));

    if (ioctl(mJpegFd, VIDIOC_QUERYBUF, bufferout) < 0) {
        ALOGE("VIDIOC_QUERYBUF failed for out buffer");
        goto failed;
    }

    ALOGI("Plane 0 bytesused=%d, length=%d, data_offset=%d\n",
               bufferout->m.planes[0].bytesused,
               bufferout->m.planes[0].length,
               bufferout->m.planes[0].data_offset);

    v4l2_mmap(mJpegFd, bufferout, mBufferOutStart);
    v4l2_mmap(mJpegFd, bufferin, mBufferInStart);

    /*
     * fill output buffer with the contents of the input raw file
     * the output buffer is given to the device for processing,
     * typically for display, hence the name "output", encoding in this case
     */
    memcpy((char *)mBufferInStart[0], (char *)srcbuf, ea->size);

    return 0;
failed:
   return -1;
}

int HwJpegEncoder::onEncoderStart(struct v4l2_buffer *buf_in, struct v4l2_buffer *buf_out,
                              char *dstbuf){
    int type_cap, type_out;
    unsigned int plane;
    FILE *fout;
    int return_bytes = 0;
    char value[PROPERTY_VALUE_MAX];
    bool vflg = false;

    type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    ALOGV("streamon in stream \n");
    if (ioctl(mJpegFd, VIDIOC_STREAMON, &type_cap) < 0) {
        ALOGE("VIDIOC_STREAMON cap stream failed");
        return 0;
    }

    ALOGV("streamon out stream\n");
    if (ioctl(mJpegFd, VIDIOC_STREAMON, &type_out) < 0) {
        ALOGE("VIDIOC_STREAMON out stream failed");
        return 0;
    }

    /*
    * repeatedly enqueue/dequeue 1 output buffer and 1 capture buffer,
    * the output buffer is filled once by the application and the result
    * is expected to be filled by the device in the capture buffer,
    * this is just to enable a stress test for the driver & the device
    */
    if (ioctl(mJpegFd, VIDIOC_QBUF, buf_out) < 0) {
        ALOGE("VIDIOC_QBUF failed for cap stream");
        return 0;
    }

    if (ioctl(mJpegFd, VIDIOC_QBUF, buf_in) < 0) {
        ALOGE("VIDIOC_QBUF failed for out stream");
        return 0;
    }

    if (ioctl(mJpegFd, VIDIOC_DQBUF, buf_in) < 0) {
        ALOGE("VIDIOC_DQBUF failed for out stream");
        return 0;
    }

    if (ioctl(mJpegFd, VIDIOC_DQBUF, buf_out) < 0) {
        ALOGE("VIDIOC_DQBUF failed for cap stream");
        return 0;
    }

    // dump the jpeg data into /data/dump.jpeg when set rw.camera.test
    // it need disable selinux when open dump option.
    property_get("rw.camera.test", value, "");
    if (strcmp(value, "true") == 0)
        vflg = true;

    if (vflg) {
        fout = fopen("/data/dump.jpeg", "wb");
        if (fout == NULL) {
            return 0;
        }
    }

    for (plane = 0; plane < buf_out->length; plane++) {
        ALOGI("Plane %d payload: %d bytes buf_start[plane] %p\n", plane,
            buf_out->m.planes[plane].bytesused, mBufferOutStart[plane]);
        if (vflg) {
            fwrite(mBufferOutStart[plane],
                 buf_out->m.planes[plane].bytesused, 1, fout);
        }

        if (buf_out->m.planes[plane].bytesused > 0)
            memcpy((char *)(dstbuf + return_bytes), (char *)mBufferOutStart[plane], buf_out->m.planes[plane].bytesused);
        return_bytes += buf_out->m.planes[plane].bytesused;
    }
    if (vflg)
        fclose(fout);

    return return_bytes;
}

void HwJpegEncoder::onEncoderStop(struct v4l2_buffer *buf_in, struct v4l2_buffer *buf_out){
    int type_cap, type_out;

    type_cap = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    type_out = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

    if (ioctl(mJpegFd, VIDIOC_STREAMOFF, &type_cap) < 0) {
        ALOGE("VIDIOC_STREAMOFF failed for cap stream");
        return;
    }

    if (ioctl(mJpegFd, VIDIOC_STREAMOFF, &type_out) < 0) {
        ALOGE("VIDIOC_STREAMOFF failed for out stream");
        return;
    }

    v4l2_munmap(buf_out, mBufferOutStart);
    v4l2_munmap(buf_in, mBufferInStart);
}


void HwJpegEncoder::enumJpegEnc()
{
    DIR *vidDir = NULL;
    char jpegEncName[64];
    struct dirent *dirEntry;
    std::string buffer;

    vidDir = opendir("/sys/class/video4linux");
    if (vidDir == NULL) {
        return;
    }

    while ((dirEntry = readdir(vidDir)) != NULL) {
        if (strncmp(dirEntry->d_name, "video", 5)) {
            continue;
        }

        sprintf(jpegEncName, "/sys/class/video4linux/%s/name", dirEntry->d_name);
        if (!android::base::ReadFileToString(std::string(jpegEncName), &buffer)) {
            ALOGE("can't read video device name");
            break;
        }

        // the string read through ReadFileToString have '\n' in last byte
        // the last byte is \0 in mJpegEnc.mJpegHwName, so we just need compare length (buffer.length() - 1)
        if (!strncmp(JPEG_ENC_NAME, buffer.c_str(), (buffer.length() - 1) )) {
            sprintf(mJpegDevPath, "/dev/%s", dirEntry->d_name);
            break;
        }
    }
}
