/*
 * Copyright (C) 2012-2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2020 NXP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "JpegBuilder"

#include "JpegBuilder.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "CameraConfigurationParser.h"
#include "CameraMetadata.h"
#include "CameraUtils.h"
#include "ISPWrapper.h"
#include "log/log.h"

extern "C" {
#include "jerror.h"
#include "jpeglib.h"
}

#define IMX_JPEG_ENC "mxc-jpeg-enc"

namespace android {
struct string_pair {
    const char *string1;
    const char *string2;
};

static string_pair degress_to_exif_lut[] = {
        // degrees, exif_orientation
        {"0", "1"},
        {"90", "6"},
        {"180", "3"},
        {"270", "8"},
};

/* public static functions */
const char *JpegBuilder::degreesToExifOrientation(const char *degrees) {
    for (unsigned int i = 0; i < ARRAY_SIZE(degress_to_exif_lut); i++) {
        if (!strcmp(degrees, degress_to_exif_lut[i].string1)) {
            return degress_to_exif_lut[i].string2;
        }
    }
    return NULL;
}

void JpegBuilder::stringToRational(const char *str, unsigned int *num, unsigned int *den) {
    int len;
    char *tempVal = NULL;

    if (str != NULL) {
        len = strlen(str);
        tempVal = (char *)malloc(sizeof(char) * (len + 1));
    }

    if (tempVal != NULL) {
        // convert the decimal string into a rational
        size_t den_len;
        char *ctx;
        unsigned int numerator = 0;
        unsigned int denominator = 0;
        char *temp = NULL;

        memset(tempVal, '\0', len + 1);
        strncpy(tempVal, str, len);
        temp = strtok_r(tempVal, ".", &ctx);

        if (temp != NULL) numerator = atoi(temp);

        if (!numerator) numerator = 1;

        temp = strtok_r(NULL, ".", &ctx);
        if (temp != NULL) {
            den_len = strlen(temp);
            if (HUGE_VAL == den_len) {
                den_len = 0;
            }

            denominator = static_cast<unsigned int>(pow(10, den_len));
            numerator = numerator * denominator + atoi(temp);
        } else {
            denominator = 1;
        }

        free(tempVal);

        *num = numerator;
        *den = denominator;
    }
}

JpegBuilder::JpegBuilder() : has_datetime_tag(false) {
    reset();
}

void JpegBuilder::reset() {
    has_datetime_tag = false;
    mMainInput = NULL;
    mThumbnailInput = NULL;
    mCancelEncoding = false;
    memset(&mEXIFData, 0, sizeof(mEXIFData));
}

JpegBuilder::~JpegBuilder() {}

void JpegBuilder::setMetadata(CameraMetadata *meta) {
    mMeta = meta;
    if (mMeta == NULL) {
        ALOGV("%s invalid meta param", __func__);
        return;
    }

    status_t ret = NO_ERROR;

    double gpsCoordinates[3];
    ret = mMeta->getGpsCoordinates(gpsCoordinates, 3);
    if (ret == 0) {
        double gpsPos = gpsCoordinates[0];
        if (convertGPSCoord(gpsPos, mEXIFData.mGPSData.mLatDeg, mEXIFData.mGPSData.mLatMin,
                            mEXIFData.mGPSData.mLatSec,
                            mEXIFData.mGPSData.mLatSecDiv) == NO_ERROR) {
            if (0 < gpsPos) {
                strncpy(mEXIFData.mGPSData.mLatRef, GPS_NORTH_REF, GPS_REF_SIZE);
            } else {
                strncpy(mEXIFData.mGPSData.mLatRef, GPS_SOUTH_REF, GPS_REF_SIZE);
            }

            mEXIFData.mGPSData.mLatValid = true;
        } else {
            mEXIFData.mGPSData.mLatValid = false;
        }

        gpsPos = gpsCoordinates[1];
        if (convertGPSCoord(gpsPos, mEXIFData.mGPSData.mLongDeg, mEXIFData.mGPSData.mLongMin,
                            mEXIFData.mGPSData.mLongSec,
                            mEXIFData.mGPSData.mLongSecDiv) == NO_ERROR) {
            if (0 < gpsPos) {
                strncpy(mEXIFData.mGPSData.mLongRef, GPS_EAST_REF, GPS_REF_SIZE);
            } else {
                strncpy(mEXIFData.mGPSData.mLongRef, GPS_WEST_REF, GPS_REF_SIZE);
            }

            mEXIFData.mGPSData.mLongValid = true;
        } else {
            mEXIFData.mGPSData.mLongValid = false;
        }

        gpsPos = gpsCoordinates[2];
        mEXIFData.mGPSData.mAltitude = floor(fabs(gpsPos));
        if (gpsPos < 0) {
            mEXIFData.mGPSData.mAltitudeRef = 1;
        } else {
            mEXIFData.mGPSData.mAltitudeRef = 0;
        }
        mEXIFData.mGPSData.mAltitudeValid = true;
    } else {
        mEXIFData.mGPSData.mLatValid = false;
        mEXIFData.mGPSData.mLongValid = false;
        mEXIFData.mGPSData.mAltitudeValid = false;
    }

    int64_t gpsTimestamp;
    mEXIFData.mGPSData.mTimeStampValid = false;
    mEXIFData.mGPSData.mDatestampValid = false;
    ret = mMeta->getGpsTimeStamp(gpsTimestamp);
    if (ret == 0) {
        time_t time = (time_t)gpsTimestamp;
        struct tm *timeinfo = gmtime(&time);
        if (NULL != timeinfo) {
            mEXIFData.mGPSData.mTimeStampHour = timeinfo->tm_hour;
            mEXIFData.mGPSData.mTimeStampMin = timeinfo->tm_min;
            mEXIFData.mGPSData.mTimeStampSec = timeinfo->tm_sec;
            mEXIFData.mGPSData.mTimeStampValid = true;

            strftime(mEXIFData.mGPSData.mDatestamp, GPS_DATESTAMP_SIZE, "%Y:%m:%d", timeinfo);
            mEXIFData.mGPSData.mDatestampValid = true;
        }
    }

    uint8_t gpsProcessingMethod[GPS_PROCESSING_SIZE];
    ret = mMeta->getGpsProcessingMethod(gpsProcessingMethod, GPS_PROCESSING_SIZE);
    if (ret == 0) {
        memset(mEXIFData.mGPSData.mProcMethod, 0, GPS_PROCESSING_SIZE);
        strcpy(mEXIFData.mGPSData.mProcMethod, (const char *)gpsProcessingMethod);
        mEXIFData.mGPSData.mProcMethodValid = true;
    } else {
        mEXIFData.mGPSData.mProcMethodValid = false;
    }

    mEXIFData.mGPSData.mMapDatumValid = false;
    mEXIFData.mGPSData.mVersionIdValid = false;
    mEXIFData.mModelValid = true;
    mEXIFData.mMakeValid = true;
}

status_t JpegBuilder::encodeImage(JpegParams *mainJpeg, JpegParams *thumbNail, char *hw_jpeg_enc,
                                  CameraMetadata &meta) {
    status_t ret = NO_ERROR;

    mMainInput = mainJpeg;
    mThumbnailInput = thumbNail;

    /* Generate EXIF object */
    mExifUtils = ExifUtils::Create();
    if (mExifUtils == NULL) {
        ALOGE("%s: ExifUtils::Create() failed", __func__);
        return BAD_VALUE;
    }

    mExifUtils->Initialize();

    mExifUtils->SetFromMetadata(meta, mainJpeg->out_width, mainJpeg->out_height);

    char value[PROPERTY_VALUE_MAX];
    property_get("ro.product.manufacturer", value, "");
    mExifUtils->SetMake(value);

    property_get("ro.product.model", value, "");
    mExifUtils->SetModel(value);

    mExifUtils->SetExposureTime(EXP_TIME_DFT);

    // When flash is not available, the last 2 parameters are not cared
    mExifUtils->SetFlash(ANDROID_FLASH_INFO_AVAILABLE_FALSE, ANDROID_FLASH_STATE_UNAVAILABLE,
                         ANDROID_CONTROL_AE_MODE_ON);

    size_t thumbCodeSize = 0;
    if (thumbNail) {
        ret = encodeJpeg(thumbNail, hw_jpeg_enc, 0, 0);
        if (ret != NO_ERROR) {
            ALOGE("%s encodeJpeg failed", __func__);
            goto error;
        }
        thumbCodeSize = mThumbnailInput->jpeg_size;
    }

    ret = mExifUtils->GenerateApp1(thumbNail ? mThumbnailInput->dst : 0, thumbCodeSize);
    if (!ret) {
        ALOGE("%s: generating APP1 failed.", __FUNCTION__);
        ret = BAD_VALUE;
        goto error;
    }

    /* Get internal buffer */
    exifDataSize = mExifUtils->GetApp1Length();
    exifData = mExifUtils->GetApp1Buffer();

    ret = encodeJpeg(mainJpeg, hw_jpeg_enc, exifData, exifDataSize);
    if (ret) goto error;

    return 0;

error:
    delete mExifUtils;
    mExifUtils = NULL;
    return ret;
}

status_t JpegBuilder::encodeJpeg(JpegParams *input, char *hw_jpeg_enc, const void *app1Buffer,
                                 size_t app1Size) {
    PixelFormat format = input->format;

    YuvToJpegEncoder *encoder;
    if (strstr(hw_jpeg_enc, IMX_JPEG_ENC))
        encoder = new HwJpegEncoder(format);
    else
        encoder = YuvToJpegEncoder::create(format);

    if (encoder == NULL) {
        ALOGE("%s failed to create jpeg encoder", __func__);
        return BAD_VALUE;
    }

    int res = 0;

    res = encoder->encode(input->src, input->srcPhy, input->src_size, input->src_fd,
                          input->src_handle, input->in_width, input->in_height, input->quality,
                          input->dst, input->dst_size, input->out_width, input->out_height,
                          app1Buffer, app1Size);

    delete encoder;
    if (res) {
        input->jpeg_size = res;
        return NO_ERROR;
    } else {
        return BAD_VALUE;
    }
}

status_t JpegBuilder::buildImage(ImxStreamBuffer *streamBuf, char *hw_jpeg_enc) {
    int ret = 0;

    if (!streamBuf || !mMainInput) {
        ALOGE("%s invalid param", __func__);
        ret = BAD_VALUE;
        goto finish;
    }

    if (strstr(hw_jpeg_enc, IMX_JPEG_ENC)) {
        ret = InsertEXIFAndJpeg(mMainInput->dst, mMainInput->jpeg_size,
                                (uint8_t *)streamBuf->mVirtAddr, streamBuf->mSize);
    } else {
        uint8_t *pDst = (uint8_t *)streamBuf->mVirtAddr;
        mRequestSize = mMainInput->jpeg_size;
        memcpy(pDst, mMainInput->dst, mRequestSize);
    }

finish:
    if (mExifUtils) {
        delete mExifUtils;
        mExifUtils = NULL;
    }

    return ret;
}

// Define Quantization Table
#define DQT_Mark_0 0xff
#define DQT_Mark_1 0xdb
#define DRI_Mark_0 0xff
#define DRI_Mark_1 0xdd
#define ARRAYSIZE(a) (uint32_t)(sizeof(a) / sizeof(a[0]))

int JpegBuilder::InsertEXIFAndJpeg(uint8_t *pMain, uint32_t mainSize, uint8_t *pDst,
                                   uint32_t dstSize) {
    int ret = 0;
    uint32_t mapp1Size;

    static uint8_t SOIMark[] = {0xff, 0xd8};
    static uint8_t EOIMark[] = {0xff, 0xd9};

    /*
     *  APP1 Marker, 0xffe1, 2 bytes
     *  APP1 Length, written later, 2 bytes
     */
    const static uint8_t APP1Head[] = {0xff, 0xe1, 0x00, 0x00};

    uint32_t totalHeadSize = ARRAYSIZE(SOIMark) + ARRAYSIZE(APP1Head);
    uint32_t totalEndSize = ARRAYSIZE(EOIMark);
    static uint32_t g_mainJpgOffset = totalHeadSize + exifDataSize;
    mRequestSize = g_mainJpgOffset + mainSize + totalEndSize;

    if (dstSize < mRequestSize) {
        ALOGE("%s, dstSize(%d) < mRequestSize(%d)", __func__, dstSize, mRequestSize);
        return -1;
    }

    // write head
    memcpy(pDst, SOIMark, ARRAYSIZE(SOIMark));
    memcpy(pDst + ARRAYSIZE(SOIMark), APP1Head, ARRAYSIZE(APP1Head));

    mapp1Size = (uint16_t)g_mainJpgOffset - 4;
    pDst[4] = (uint8_t)(mapp1Size >> 8);
    pDst[5] = (uint8_t)(mapp1Size & 0xff);

    // InsertApp1Data
    memcpy(pDst + totalHeadSize, exifData, exifDataSize);

    // InsertMainJpeg
    uint32_t i = 0;
    uint32_t DRIOffset = 0;

    while ((((pMain[i] != DRI_Mark_0) || (pMain[i + 1] != DRI_Mark_1)) &&
            ((pMain[i] != DQT_Mark_0) || (pMain[i + 1] != DQT_Mark_1))) &&
           (i + 2 <= mainSize))
        i++;

    if (i + 2 > mainSize) {
        ALOGE("InsertMainJpeg, can't find DRI Mark and DQT Mark, mainSize %d", mainSize);
        return -1;
    }

    DRIOffset = i;
    ALOGV("InsertMainJpeg, DQTOffset %d", DRIOffset);
    memcpy(pDst + g_mainJpgOffset, pMain + DRIOffset, mainSize - DRIOffset);

    // write EOI
    memcpy(pDst + mRequestSize - ARRAYSIZE(EOIMark), EOIMark, ARRAYSIZE(EOIMark));

    return ret;
}

status_t JpegBuilder::convertGPSCoord(double coord, int &deg, int &min, int &sec, int &secDivisor) {
    double tmp;

    if (coord == 0) {
        ALOGE("Invalid GPS coordinate");

        return -EINVAL;
    }

    deg = (int)floor(fabs(coord));
    tmp = (fabs(coord) - floor(fabs(coord))) * GPS_MIN_DIV;
    min = (int)floor(tmp);
    tmp = (tmp - floor(tmp)) * (GPS_SEC_DIV * GPS_SEC_ACCURACY);
    sec = (int)floor(tmp);
    secDivisor = GPS_SEC_ACCURACY;

    if (sec >= (GPS_SEC_DIV * GPS_SEC_ACCURACY)) {
        sec = 0;
        min += 1;
    }

    if (min >= 60) {
        min = 0;
        deg += 1;
    }

    return NO_ERROR;
}
}; // namespace android
