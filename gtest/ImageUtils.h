
#ifndef __IMAGE_UTILS_H__
#define __IMAGE_UTILS_H__
#include <android/data_space.h>
#include <android/bitmap.h>
#include <android/imagedecoder.h>
bool getImageInfo(const char *input, uint32_t *width, uint32_t *height, uint32_t *stride);
bool decodeImage(void *outbuf, uint32_t len, const char *name,
        AndroidBitmapFormat format = ANDROID_BITMAP_FORMAT_RGBA_8888);

bool encoderImage(uint32_t width, uint32_t height,
                uint32_t stride, AndroidBitmapFormat format,
                char *image, const char *name);
#endif
