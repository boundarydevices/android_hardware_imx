
#include <android/bitmap.h>
#include <android/data_space.h>
#include <android/imagedecoder.h>
#include <cutils/log.h>

bool getImageInfo(const char *input, uint32_t *width, uint32_t *height, uint32_t *stride) {
    int fd = -1;
    int result = -1;
    AImageDecoder *decoder;
    const AImageDecoderHeaderInfo *info;

    fd = open(input, O_RDWR, O_RDONLY);
    if (fd < 0) {
        ALOGE("Unable to open file [%s]", input);
        return false;
    }

    result = AImageDecoder_createFromFd(fd, &decoder);
    if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
        // An error occurred, and the file could not be decoded.
        ALOGE("Not a valid image file [%s]", input);
        close(fd);
        return false;
    }
    info = AImageDecoder_getHeaderInfo(decoder);
    *width = AImageDecoderHeaderInfo_getWidth(info);
    *height = AImageDecoderHeaderInfo_getHeight(info);
    *stride = AImageDecoder_getMinimumStride(decoder);

    AImageDecoder_delete(decoder);

    if (fd >= 0) close(fd);
    return true;
}

bool encoderImage(uint32_t width, uint32_t height, uint32_t stride, AndroidBitmapFormat format,
                  char *image, const char *name) {
    // Encoder the flat_output
    AndroidBitmapInfo bpinfo = {
            .flags = 0,
            .format = format,
            .height = height,
            .width = width,
            .stride = stride,
    };

    int outfd = open(name, O_CREAT | O_RDWR, 0666);
    if (outfd < 0) {
        ALOGE("Unable to open out file [%s]", name);
        return false;
    }

    auto fn = [](void *userContext, const void *data, size_t size) -> bool {
        if ((userContext == nullptr) || (data == nullptr) || (size == 0)) {
            ALOGE("Error on encoder!");
            return false;
        }
        int fd = *(int *)userContext;
        int len = 0;
        len = write(fd, data, size);
        return true;
    };

    int result = -1;
    result = AndroidBitmap_compress(&bpinfo, ADATASPACE_SCRGB_LINEAR, image,
                                    ANDROID_BITMAP_COMPRESS_FORMAT_JPEG, 100, &outfd, fn);
    if (result != ANDROID_BITMAP_RESULT_SUCCESS) {
        ALOGE("Error on encoder return %d!", result);
        return false;
    }
    if (outfd >= 0) close(outfd);
    return true;
}

// Force it to RGBA8888
bool decodeImage(void *outbuf, uint32_t len, const char *name, AndroidBitmapFormat format) {
    if (outbuf == nullptr) {
        ALOGE("Invalid buf for image decoding");
        return false;
    }

    uint32_t width = 0, height = 0, stride = 0;
    AImageDecoder *decoder;
    const AImageDecoderHeaderInfo *info;
    auto fd = open(name, O_RDWR, O_RDONLY);
    if (fd < 0) {
        ALOGE("Unable to open file [%s]", name);
        return false;
    }

    auto result = AImageDecoder_createFromFd(fd, &decoder);
    if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
        // An error occurred, and the file could not be decoded.
        ALOGE("Not a valid image file [%s]", name);
        close(fd);
        return false;
    }
    info = AImageDecoder_getHeaderInfo(decoder);
    width = AImageDecoderHeaderInfo_getWidth(info);
    height = AImageDecoderHeaderInfo_getHeight(info);
    AImageDecoder_setAndroidBitmapFormat(decoder, format);
    format = (AndroidBitmapFormat)AImageDecoderHeaderInfo_getAndroidBitmapFormat(info);
    stride = AImageDecoder_getMinimumStride(decoder);
    auto size = height * stride;
    if (size > len) {
        ALOGE("Out buffer size is too small!!");
        close(fd);
        AImageDecoder_delete(decoder);
        return false;
    }

    ALOGI("decodeImage: %d x %d, stride %u, format %d", width, height, stride, format);

    result = AImageDecoder_decodeImage(decoder, outbuf, stride, size);
    if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
        // An error occurred, and the file could not be decoded.
        ALOGE("file to decode the image [%s]", name);
        AImageDecoder_delete(decoder);
        close(fd);
        return false;
    }

    close(fd);
    return true;
}
