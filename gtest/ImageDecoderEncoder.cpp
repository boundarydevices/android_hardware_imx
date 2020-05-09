#define LOG_TAG "sv-test"
#include <android/data_space.h>
#include <android/bitmap.h>
#include <android/imagedecoder.h>
#include <android/rect.h>
#include <cutils/log.h>

#include <fcntl.h>

#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdio.h>
#include <unistd.h>
#include <vector>

#include <gtest/gtest.h>


#define MAX_FILE_LEN 128

bool imageEncoderDecoder(const char *input_file,
                         const char *output_file,
                         int32_t raw_type) {
    int fd = 0;
    int result = -1;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    size_t size;
    void* pixels = nullptr;
    AImageDecoder* decoder;
    AndroidBitmapFormat format;
    const AImageDecoderHeaderInfo* info;

    fd = open(input_file, O_RDWR, O_RDONLY);
    if (fd < 0) {
        ALOGE("Unable to open file [%s]",
             input_file);
        return false;
    }
    result = AImageDecoder_createFromFd(fd, &decoder);
    if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
        // An error occurred, and the file could not be decoded.
        ALOGE("Not a valid image file [%s]",
             input_file);
        close(fd);
        return false;
    }
    info = AImageDecoder_getHeaderInfo(decoder);
    width = AImageDecoderHeaderInfo_getWidth(info);
    height = AImageDecoderHeaderInfo_getHeight(info);
    AImageDecoder_setAndroidBitmapFormat(decoder, raw_type);
    format =
           (AndroidBitmapFormat) AImageDecoderHeaderInfo_getAndroidBitmapFormat(info);
    stride = AImageDecoder_getMinimumStride(decoder);
    size = height * stride;
    pixels = malloc(size);
    ALOGI("Image: %d x %d, stride %u, format %d", width, height, stride, format);

    result = AImageDecoder_decodeImage(decoder, pixels, stride, size);
    if (result != ANDROID_IMAGE_DECODER_SUCCESS) {
        // An error occurred, and the file could not be decoded.
        ALOGE("file to decode the image [%s]",
             input_file);
        AImageDecoder_delete(decoder);
        close(fd);
        return false;
    }

    AndroidBitmapInfo bpinfo = {
        .flags = 0,
        .format = format,
        .height = height,
        .width = width,
        .stride = stride,
    };
    auto fn = [](void *userContext, const void *data, size_t size) -> bool {
        if((userContext == nullptr) || (data == nullptr) || (size == 0)) {
            ALOGE("Error on encoder!");
            return false;
        }
        int fd = *(int *)userContext;
        int len = 0;
        len = write(fd, data, size);
        return true;
    };

    int outfd = open(output_file, O_CREAT | O_RDWR, 0666);
    if (outfd < 0) {
        ALOGE("Unable to open out file [%s]",
                output_file);
        AImageDecoder_delete(decoder);
        close(fd);
        free(pixels);
        return false;
    }

    result = AndroidBitmap_compress(&bpinfo, ADATASPACE_SCRGB_LINEAR,
            pixels, ANDROID_BITMAP_COMPRESS_FORMAT_PNG, 0, &outfd, fn);
    if (result != ANDROID_BITMAP_RESULT_SUCCESS ) {
        ALOGE("Error on encoder return %d!", result);
    }

    // We’re done with the decoder, so now it’s safe to delete it.
    AImageDecoder_delete(decoder);

    // Free the pixels when done drawing with them
    free(pixels);

    if(fd >=0)
        close(fd);


    close(outfd);
    return true;
}

TEST(ImxTest, ImageEncoderDecoderRGBA8888) {
    char input_file[MAX_FILE_LEN];
    char output_file[MAX_FILE_LEN];
    int32_t raw_type = ANDROID_BITMAP_FORMAT_RGBA_8888;

    memset(input_file, 0, MAX_FILE_LEN);
    strncpy(input_file, "/sdcard/0.png", MAX_FILE_LEN);
    memset(output_file, 0, MAX_FILE_LEN);
    strncpy(output_file, "sdcard/0-output-rgba8888.png", MAX_FILE_LEN);
    ASSERT_TRUE(imageEncoderDecoder(input_file, output_file, raw_type));
}

TEST(ImxTest, ImageEncoderDecoderRGB565) {
    char input_file[MAX_FILE_LEN];
    char output_file[MAX_FILE_LEN];
    int32_t raw_type = ANDROID_BITMAP_FORMAT_RGB_565;

    memset(input_file, 0, MAX_FILE_LEN);
    strncpy(input_file, "/sdcard/0.png", MAX_FILE_LEN);
    memset(output_file, 0, MAX_FILE_LEN);
    strncpy(output_file, "sdcard/0-output-rgb565.png", MAX_FILE_LEN);

    ASSERT_TRUE(imageEncoderDecoder(input_file, output_file, raw_type));
}

