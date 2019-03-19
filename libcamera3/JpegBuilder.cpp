/*
 * Copyright (C) 2012-2016 Freescale Semiconductor, Inc.
 * Copyright 2017-2019 NXP
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

#define LOG_TAG "CameraHAL"

#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>

#include "JpegBuilder.h"
#include "Metadata.h"
#include "Stream.h"

extern "C" {
    #include "jpeglib.h"
    #include "jerror.h"
}

namespace android {
struct string_pair {
    const char *string1;
    const char *string2;
};

static string_pair degress_to_exif_lut[] = {
    // degrees, exif_orientation
    { "0",   "1"     },
    { "90",  "6"     },
    { "180", "3"     },
    { "270", "8"     },
};

/* public static functions */
const char * JpegBuilder::degreesToExifOrientation(const char *degrees) {
    for (unsigned int i = 0; i < ARRAY_SIZE(degress_to_exif_lut); i++) {
        if (!strcmp(degrees, degress_to_exif_lut[i].string1)) {
            return degress_to_exif_lut[i].string2;
        }
    }
    return NULL;
}

void JpegBuilder::stringToRational(const char   *str,
                                   unsigned int *num,
                                   unsigned int *den) {
    int   len;
    char *tempVal = NULL;

    if (str != NULL) {
        len     = strlen(str);
        tempVal = (char *)malloc(sizeof(char) * (len + 1));
    }

    if (tempVal != NULL) {
        // convert the decimal string into a rational
        size_t den_len;
        char  *ctx;
        unsigned int numerator   = 0;
        unsigned int denominator = 0;
        char *temp               = NULL;

        memset(tempVal, '\0', len + 1);
        strncpy(tempVal, str, len);
        temp = strtok_r(tempVal, ".", &ctx);

        if (temp != NULL)
            numerator = atoi(temp);

        if (!numerator)
            numerator = 1;

        temp = strtok_r(NULL, ".", &ctx);
        if (temp != NULL) {
            den_len = strlen(temp);
            if (HUGE_VAL == den_len) {
                den_len = 0;
            }

            denominator = static_cast<unsigned int>(pow(10, den_len));
            numerator   = numerator * denominator + atoi(temp);
        }
        else {
            denominator = 1;
        }

        free(tempVal);

        *num = numerator;
        *den = denominator;
    }
}

status_t JpegBuilder::insertElement(uint16_t tag,
                                    uint32_t val1,
                                    uint32_t val2,
                                    uint32_t val3,
                                    uint32_t val4,
                                    uint32_t val5,
                                    uint32_t val6,
                                    char *strVal)
{
    int value_length = 0;
    status_t ret     = NO_ERROR;

    if (position >= MAX_EXIF_TAGS_SUPPORTED) {
        ALOGE("Max number of EXIF elements already inserted");
        return NO_MEMORY;
    }

    table[position].tag = tag;
    table[position].val1 = val1;
    table[position].val2 = val2;
    table[position].val3 = val3;
    table[position].val4 = val4;
    table[position].val5 = val5;
    table[position].val6 = val6;

    if (strVal) {
        if (table[position].tag == TAG_GPS_PROCESSING_METHOD) {
            value_length = sizeof(ExifAsciiPrefix) +
                           strlen(strVal + sizeof(ExifAsciiPrefix));
            table[position].strVal = (char *)malloc(sizeof(char) * (value_length + 1));
            memcpy(table[position].strVal, strVal, value_length + 1);
        } else {
            table[position].strVal = strdup(strVal);
        }
    }

    position++;

    return ret;
}

JpegBuilder::JpegBuilder()
    : position(0), has_datetime_tag(false)
{
    reset();
}

void JpegBuilder::reset()
{
    position         = 0;
    has_datetime_tag = false;
    mMainInput       = NULL;
    mThumbnailInput  = NULL;
    mCancelEncoding  = false;
    memset(&mEXIFData, 0, sizeof(mEXIFData));
    memset(table, 0, sizeof(table));
}

JpegBuilder::~JpegBuilder()
{
}

status_t JpegBuilder::prepareImage(const StreamBuffer *streamBuf)
{
    status_t ret = NO_ERROR;
    struct timeval sTv;
    struct tm     *pTime;

    if (streamBuf == NULL || streamBuf->mStream == NULL) {
        ALOGE("%s invalid stream buffer", __func__);
        return 0;
    }

    const sp<Stream>& stream = streamBuf->mStream;

    insertElement(TAG_MODEL, 0, 0, 0, 0, 0, 0, (char *)EXIF_MODEL);
    insertElement(TAG_MAKE, 0, 0, 0, 0, 0, 0, (char *)EXIF_MAKENOTE);

    float focalLength;
    ret = mMeta->getFocalLength(focalLength);
    if ((NO_ERROR == ret)) {
        char str[16];  // 14 should be enough. We overestimate to be safe.
        snprintf(str, sizeof(str), "%g", focalLength);
        unsigned int numerator = 0, denominator = 0;
        JpegBuilder::stringToRational(str, &numerator, &denominator);
        if (numerator || denominator) {
            insertElement(TAG_FOCALLENGTH, numerator, denominator, 0, 0, 0, 0, NULL);
        }
    }

    int status = gettimeofday(&sTv, NULL);
    pTime = localtime(&sTv.tv_sec);
    char temp_value[EXIF_DATE_TIME_SIZE + 1];
    if ((0 == status) && (NULL != pTime)) {
        snprintf(temp_value,
                 EXIF_DATE_TIME_SIZE,
                 "%04d:%02d:%02d %02d:%02d:%02d",
                 pTime->tm_year + 1900,
                 pTime->tm_mon + 1,
                 pTime->tm_mday,
                 pTime->tm_hour,
                 pTime->tm_min,
                 pTime->tm_sec);

        insertElement(TAG_DATETIME, 0, 0, 0, 0, 0, 0, temp_value);
    }

    int width, height;
    width = stream->width();
    height = stream->height();

    insertElement(TAG_IMAGE_WIDTH, width, 0, 0, 0, 0, 0, NULL);
    insertElement(TAG_IMAGE_LENGTH, height, 0, 0, 0, 0, 0, NULL);
    int32_t jpegRotation;
    ret = mMeta->getJpegRotation(jpegRotation);
    if (NO_ERROR == ret) {
        char str[16];
        snprintf(str, sizeof(str), "%d", jpegRotation);
        const char *exif_orient =
            JpegBuilder::degreesToExifOrientation(str);

        if (exif_orient) {
            ret = insertElement(TAG_ORIENTATION, atoi(exif_orient), 0, 0, 0, 0, 0, NULL);
        }
    }

    if ((NO_ERROR == ret) && (mEXIFData.mGPSData.mLatValid)) {
        char temp_value[256];  // arbitrarily long string
        snprintf(temp_value,
                 sizeof(temp_value) / sizeof(char) - 1,
                 "%d/%d,%d/%d,%d/%d",
                 abs(mEXIFData.mGPSData.mLatDeg),
                 1,
                 abs(mEXIFData.mGPSData.mLatMin),
                 1,
                 abs(mEXIFData.mGPSData.mLatSec),
                 abs(mEXIFData.mGPSData.mLatSecDiv));
        ret = insertElement(TAG_GPS_LATITUDE,
                            abs(mEXIFData.mGPSData.mLatDeg),
                            1,
                            abs(mEXIFData.mGPSData.mLatMin),
                            1,
                            abs(mEXIFData.mGPSData.mLatSec),
                            abs(mEXIFData.mGPSData.mLatSecDiv),
                            NULL);
    }

    if ((NO_ERROR == ret) && (mEXIFData.mGPSData.mLatValid)) {
        ret = insertElement(TAG_GPS_LATITUDE_REF, 0, 0, 0, 0, 0, 0, mEXIFData.mGPSData.mLatRef);
    }

    if ((NO_ERROR == ret) && (mEXIFData.mGPSData.mLongValid)) {
        char temp_value[256];  // arbitrarily long string
        snprintf(temp_value,
                 sizeof(temp_value) / sizeof(char) - 1,
                 "%d/%d,%d/%d,%d/%d",
                 abs(mEXIFData.mGPSData.mLongDeg),
                 1,
                 abs(mEXIFData.mGPSData.mLongMin),
                 1,
                 abs(mEXIFData.mGPSData.mLongSec),
                 abs(mEXIFData.mGPSData.mLongSecDiv));
        ret = insertElement(TAG_GPS_LONGITUDE,
                            abs(mEXIFData.mGPSData.mLongDeg),
                            1,
                            abs(mEXIFData.mGPSData.mLongMin),
                            1,
                            abs(mEXIFData.mGPSData.mLongSec),
                            abs(mEXIFData.mGPSData.mLongSecDiv),
                            NULL);
    }

    if ((NO_ERROR == ret) && (mEXIFData.mGPSData.mLongValid)) {
        ret = insertElement(TAG_GPS_LONGITUDE_REF, 0, 0, 0, 0, 0, 0, mEXIFData.mGPSData.mLongRef);
    }

    if ((NO_ERROR == ret) && (mEXIFData.mGPSData.mAltitudeValid)) {
        char temp_value[256];  // arbitrarily long string
        snprintf(temp_value,
                 sizeof(temp_value) / sizeof(char) - 1,
                 "%d/%d",
                 abs(mEXIFData.mGPSData.mAltitude),
                 1);
        unsigned int numerator = 0, denominator = 0;
        JpegBuilder::stringToRational(temp_value, &numerator, &denominator);
        ret = insertElement(TAG_GPS_ALTITUDE, numerator, denominator, 0, 0, 0, 0, NULL);
    }

    if ((NO_ERROR == ret) && (mEXIFData.mGPSData.mAltitudeValid)) {
        char temp_value[5];
        snprintf(temp_value,
                 sizeof(temp_value) / sizeof(char) - 1,
                 "%d",
                 mEXIFData.mGPSData.mAltitudeRef);
        ret = insertElement(TAG_GPS_ALTITUDE_REF, 0, 0, 0, 0, 0, 0, temp_value);
    }

    if ((NO_ERROR == ret) && (mEXIFData.mGPSData.mMapDatumValid)) {
        ret = insertElement(TAG_GPS_MAP_DATUM, 0, 0, 0, 0, 0, 0, mEXIFData.mGPSData.mMapDatum);
    }

    if ((NO_ERROR == ret) && (mEXIFData.mGPSData.mProcMethodValid)) {
        char temp_value[GPS_PROCESSING_SIZE];
        memcpy(temp_value, ExifAsciiPrefix, sizeof(ExifAsciiPrefix));
        memcpy(temp_value + sizeof(ExifAsciiPrefix),
               mEXIFData.mGPSData.mProcMethod,
               (GPS_PROCESSING_SIZE - sizeof(ExifAsciiPrefix)));
        ret = insertElement(TAG_GPS_PROCESSING_METHOD, 0, 0, 0, 0, 0, 0, temp_value);
    }

    if ((NO_ERROR == ret) && (mEXIFData.mGPSData.mVersionIdValid)) {
        char temp_value[256];  // arbitrarily long string
        snprintf(temp_value,
                 sizeof(temp_value) / sizeof(char) - 1,
                 "%d,%d,%d,%d",
                 mEXIFData.mGPSData.mVersionId[0],
                 mEXIFData.mGPSData.mVersionId[1],
                 mEXIFData.mGPSData.mVersionId[2],
                 mEXIFData.mGPSData.mVersionId[3]);
        ret = insertElement(TAG_GPS_VERSION_ID, 0, 0, 0, 0, 0, 0, temp_value);
    }

    if ((NO_ERROR == ret) && (mEXIFData.mGPSData.mTimeStampValid)) {
        char temp_value[256];  // arbitrarily long string
        snprintf(temp_value,
                 sizeof(temp_value) / sizeof(char) - 1,
                 "%d/%d,%d/%d,%d/%d",
                 mEXIFData.mGPSData.mTimeStampHour,
                 1,
                 mEXIFData.mGPSData.mTimeStampMin,
                 1,
                 mEXIFData.mGPSData.mTimeStampSec,
                 1);
        ret = insertElement(TAG_GPS_TIMESTAMP,
                            mEXIFData.mGPSData.mTimeStampHour,
                            1,
                            mEXIFData.mGPSData.mTimeStampMin,
                            1,
                            mEXIFData.mGPSData.mTimeStampSec,
                            1,
                            NULL);
    }

    if ((NO_ERROR == ret) && (mEXIFData.mGPSData.mDatestampValid)) {
        //add GPS date TAG.
        ret = insertElement(TAG_GPS_DATESTAMP, 0, 0, 0, 0, 0, 0, mEXIFData.mGPSData.mDatestamp);
    }

    return ret;
}

void JpegBuilder::setMetadata(sp<Metadata> meta)
{
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
        if (convertGPSCoord(gpsPos,
                            mEXIFData.mGPSData.mLatDeg,
                            mEXIFData.mGPSData.mLatMin,
                            mEXIFData.mGPSData.mLatSec,
                            mEXIFData.mGPSData.mLatSecDiv) == NO_ERROR) {
            if (0 < gpsPos) {
                strncpy(mEXIFData.mGPSData.mLatRef, GPS_NORTH_REF, GPS_REF_SIZE);
            }
            else {
                strncpy(mEXIFData.mGPSData.mLatRef, GPS_SOUTH_REF, GPS_REF_SIZE);
            }

            mEXIFData.mGPSData.mLatValid = true;
        }
        else {
            mEXIFData.mGPSData.mLatValid = false;
        }

        gpsPos = gpsCoordinates[1];
        if (convertGPSCoord(gpsPos,
                            mEXIFData.mGPSData.mLongDeg,
                            mEXIFData.mGPSData.mLongMin,
                            mEXIFData.mGPSData.mLongSec,
                            mEXIFData.mGPSData.mLongSecDiv) == NO_ERROR) {
            if (0 < gpsPos) {
                strncpy(mEXIFData.mGPSData.mLongRef, GPS_EAST_REF, GPS_REF_SIZE);
            }
            else {
                strncpy(mEXIFData.mGPSData.mLongRef, GPS_WEST_REF, GPS_REF_SIZE);
            }

            mEXIFData.mGPSData.mLongValid = true;
        }
        else {
            mEXIFData.mGPSData.mLongValid = false;
        }

        gpsPos = gpsCoordinates[2];
        mEXIFData.mGPSData.mAltitude = floor(fabs(gpsPos));
        if (gpsPos < 0) {
            mEXIFData.mGPSData.mAltitudeRef = 1;
        }
        else {
            mEXIFData.mGPSData.mAltitudeRef = 0;
        }
        mEXIFData.mGPSData.mAltitudeValid = true;
    }
    else {
        mEXIFData.mGPSData.mLatValid = false;
        mEXIFData.mGPSData.mLongValid = false;
        mEXIFData.mGPSData.mAltitudeValid = false;
    }

    int64_t gpsTimestamp;
    ret = mMeta->getGpsTimeStamp(gpsTimestamp);
    if (ret == 0) {
        struct tm *timeinfo = gmtime((time_t *)&(gpsTimestamp));
        if (NULL != timeinfo) {
            mEXIFData.mGPSData.mTimeStampHour  = timeinfo->tm_hour;
            mEXIFData.mGPSData.mTimeStampMin   = timeinfo->tm_min;
            mEXIFData.mGPSData.mTimeStampSec   = timeinfo->tm_sec;
            mEXIFData.mGPSData.mTimeStampValid = true;
        }
        else {
            mEXIFData.mGPSData.mTimeStampValid = false;
        }

        long gpsDatestamp = gpsTimestamp;
        timeinfo = gmtime((time_t *)&(gpsDatestamp));
        if (NULL != timeinfo) {
            strftime(mEXIFData.mGPSData.mDatestamp,
                     GPS_DATESTAMP_SIZE,
                     "%Y:%m:%d",
                     timeinfo);
            mEXIFData.mGPSData.mDatestampValid = true;
        }
        else {
            mEXIFData.mGPSData.mDatestampValid = false;
        }
    }
    else {
        mEXIFData.mGPSData.mTimeStampValid = false;
        mEXIFData.mGPSData.mDatestampValid = false;
    }

    uint8_t gpsProcessingMethod[GPS_PROCESSING_SIZE];
    ret = mMeta->getGpsProcessingMethod(gpsProcessingMethod, GPS_PROCESSING_SIZE);
    if (ret == 0) {
        memset(mEXIFData.mGPSData.mProcMethod, 0, GPS_PROCESSING_SIZE);
        strcpy(mEXIFData.mGPSData.mProcMethod, (const char*)gpsProcessingMethod);
        mEXIFData.mGPSData.mProcMethodValid = true;
    }
    else {
        mEXIFData.mGPSData.mProcMethodValid = false;
    }

    mEXIFData.mGPSData.mMapDatumValid  = false;
    mEXIFData.mGPSData.mVersionIdValid = false;
    mEXIFData.mModelValid              = true;
    mEXIFData.mMakeValid               = true;
}

status_t JpegBuilder::encodeImage(JpegParams *mainJpeg,
                                  JpegParams *thumbNail)
{
    status_t ret = NO_ERROR;

    mMainInput      = mainJpeg;
    mThumbnailInput = thumbNail;
    if (thumbNail) {
        ret = encodeJpeg(thumbNail);
    }

    if (ret != NO_ERROR) {
        ALOGE("%s encodeJpeg failed", __FUNCTION__);
        return ret;
    }

    return encodeJpeg(mainJpeg);
}

status_t JpegBuilder::encodeJpeg(JpegParams *input)
{
    PixelFormat format = input->format;
    YuvToJpegEncoder *encoder = YuvToJpegEncoder::create(format);

    if (encoder == NULL) {
        ALOGE("%s YuvToJpegEncoder::create failed", __FUNCTION__);
        return BAD_VALUE;
    }

    int res = 0;
    res = encoder->encode(input->src,
                          input->srcPhy,
                          input->in_width,
                          input->in_height,
                          input->quality,
                          input->dst,
                          input->dst_size,
                          input->out_width,
                          input->out_height);

    delete encoder;
    if (res) {
        input->jpeg_size = res;
        return NO_ERROR;
    }
    else {
        return BAD_VALUE;
    }
}

status_t JpegBuilder::buildImage(StreamBuffer *streamBuf)
{
    int ret = 0;

    uint8_t *pThumb = NULL;
    uint32_t dwThumbSize = 0;
    bool bMapVirt = false;

    if (!streamBuf || !mMainInput) {
        ALOGE("%s invalid param", __FUNCTION__);
        return BAD_VALUE;
    }

    // When run with some APKs, the virt address is not mapped. Cause jpeg encode
    // failed on no vpu device, say pico_imx7d. Map the virt address in HAL.
    if(!streamBuf->mVirtAddr) {
        streamBuf->MapVirtAddr();
        if(!streamBuf->mVirtAddr) {
            return BAD_VALUE;
        }

        bMapVirt = true;
    }

    if (mThumbnailInput) {
        pThumb = mThumbnailInput->dst;
        dwThumbSize = mThumbnailInput->jpeg_size;
    }

    mRequestSize = 0;
    ret = InsertEXIFAndThumbnail(table,
                                 position,
                                 pThumb,
                                 dwThumbSize,
                                 mMainInput->dst,
                                 mMainInput->jpeg_size,
                                 (uint8_t *)streamBuf->mVirtAddr,
                                 streamBuf->mSize,
                                 &mRequestSize);

    // clean IDF table
    unsigned int i;
    for (i = 0; i < position; i++) {
        if (table[i].strVal) {
            free(table[i].strVal);
        }
    }

    memset(table, 0, sizeof(table));
    position = 0;

    if(bMapVirt) {
        streamBuf->UnMapVirtAddr();
    }

    return ret;
}

status_t JpegBuilder::convertGPSCoord(double coord,
                                      int  & deg,
                                      int  & min,
                                      int  & sec,
                                      int  & secDivisor)
{
    double tmp;

    if (coord == 0) {
        ALOGE("Invalid GPS coordinate");

        return -EINVAL;
    }

    deg        = (int)floor(fabs(coord));
    tmp        = (fabs(coord) - floor(fabs(coord))) * GPS_MIN_DIV;
    min        = (int)floor(tmp);
    tmp        = (tmp - floor(tmp)) * (GPS_SEC_DIV * GPS_SEC_ACCURACY);
    sec        = (int)floor(tmp);
    secDivisor = GPS_SEC_ACCURACY;

    if (sec >= (GPS_SEC_DIV * GPS_SEC_ACCURACY)) {
        sec  = 0;
        min += 1;
    }

    if (min >= 60) {
        min  = 0;
        deg += 1;
    }

    return NO_ERROR;
}
};
