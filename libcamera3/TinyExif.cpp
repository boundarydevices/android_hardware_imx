/*
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#undef LOG_TAG
#define LOG_TAG "FslCameraHAL"
#include <cutils/log.h>
#include "TinyExif.h"

// inner used tag
#define TAG_EXIF_OFFSET 0x8769
#define TAG_GPS_OFFSET 0x8825
#define TAG_THUMBNAIL_OFFSET 0x0201
#define TAG_THUMBNAIL_LENGTH 0x0202

#define TYPE_BYTE 1
#define TYPE_ASCII 2
#define TYPE_SHORT 3
#define TYPE_LONG 4
#define TYPE_RATIONAL 5
#define TYPE_RATIONAL_SIGNED 10
#define TYPE_UNDEFINED 7

typedef struct st_IFDSpec {
    uint16_t tag;
    uint16_t type;
    uint32_t count;
    uint32_t value;
} __attribute__((packed)) IFDSpec;

#define MAX_ELE_NUM 32
typedef struct st_IFDGroup {
    uint32_t eleNum;
    IFDEle eleArray[MAX_ELE_NUM];
    uint32_t fixedLenIdx;
    uint32_t variedLenIdx;
    uint32_t fixedSize;
    uint32_t variedSize;
    uint32_t grpSize;
    uint32_t exifOffsetIdx;       // idx for TAG_EXIF_OFFSET in g_TiffGroup.eleArray
    uint32_t gpsOffsetIdx;        // idx for TAG_GPS_OFFSET in g_TiffGroup.eleArray
    uint32_t nextIFDOffset;       // offset of next IFD, now just used in g_TiffGroup to poit 1st IFD offset
} IFDGroup;

static IFDGroup g_TiffGroup;
static IFDGroup g_ExifGroup;
static IFDGroup g_GpsGroup;
static IFDGroup g_TiffGroup_1st;
static uint32_t g_thumbNailOffset;
static uint32_t g_mainJpgOffset;
uint32_t tag_length;

#define IFDELE_SIZE 12
#define ARRAYSIZE(a) (uint32_t)(sizeof(a) / sizeof(a[0]))

const static uint8_t SOIMark[] = {0xff, 0xd8};

/*  APP1 Marker, 0xffe1, 2 bytes
    APP1 Length, written later, 2 bytes
    Identifier "exif"00, 6 bytes
*/
const static uint8_t APP1Head[] = {
    0xff, 0xe1, 0x00, 0x00, 0x45, 0x78, 0x69, 0x66, 0x00, 0x00};

/*  Byte Order, 0x4949 little endian, 2 bytes
    Fxied as 0x2a, 0x00, 2 bytes
    0th IFD Offset, 8, 4 bytes
*/
const static uint8_t IFDHead[] = {
    0x49, 0x49, 0x2a, 0x00, 0x08, 0x00, 0x00, 0x00};

#define IFDHEAD_OFFSET \
    (uint32_t)(ARRAYSIZE(SOIMark) + ARRAYSIZE(APP1Head))  // 12
#define IFDGROUP_OFFSET                                   \
    (uint32_t)(ARRAYSIZE(SOIMark) + ARRAYSIZE(APP1Head) + \
               ARRAYSIZE(IFDHead))  // 20

static uint16_t GetTypeFromTag(uint16_t tag)
{
    uint16_t type = 0;

    switch (tag) {
        case TAG_MODEL:
        case TAG_MAKE:
        case TAG_DATETIME:
        case TAG_GPS_LATITUDE_REF:
        case TAG_GPS_LONGITUDE_REF:
        case TAG_GPS_DATESTAMP:
        case TAG_GPS_MAP_DATUM:
            type = TYPE_ASCII;
            break;

        case TAG_IMAGE_WIDTH:
        case TAG_IMAGE_LENGTH:
        case TAG_ORIENTATION:
            type = TYPE_SHORT;
            break;

        case TAG_GPS_ALTITUDE_REF:
        case TAG_GPS_VERSION_ID:
            type = TYPE_BYTE;
            break;

        case TAG_GPS_TIMESTAMP:
            type = TYPE_RATIONAL_SIGNED;
            break;

        case TAG_FOCALLENGTH:
        case TAG_GPS_ALTITUDE:
        case TAG_GPS_LATITUDE:
        case TAG_GPS_LONGITUDE:
            type = TYPE_RATIONAL;
            break;

        case TAG_THUMBNAIL_LENGTH:
        case TAG_THUMBNAIL_OFFSET:
        case TAG_EXIF_OFFSET:
        case TAG_GPS_OFFSET:
            type = TYPE_LONG;
            break;

        case TAG_GPS_PROCESSING_METHOD:
            type = TYPE_UNDEFINED;
            break;

        default:
            ALOGE("%s, unsupported tag 0x%x", __func__, tag);
            break;
    }

    return type;
}

static int WriteOneIFD(IFDGroup* pGrp, uint32_t idx, uint8_t* pDst)
{
    IFDEle* pIFDEle = &pGrp->eleArray[idx];
    IFDSpec* pIFDSpec = (IFDSpec*)(pDst + pGrp->fixedLenIdx);

    pIFDSpec->tag = pIFDEle->tag;
    pIFDSpec->type = GetTypeFromTag(pIFDEle->tag);

    // set count
    if (pIFDSpec->type == TYPE_UNDEFINED) {
        pIFDSpec->count = tag_length;
    } else if (pIFDSpec->type != TYPE_ASCII) {
        pIFDSpec->count = 1;
    } else if (strlen(pIFDEle->strVal) < 4) {
        pIFDSpec->count = 4;
    } else {
        pIFDSpec->count = strlen(pIFDEle->strVal) + 1;
    }

    // set value
    if ((TYPE_BYTE == pIFDSpec->type) || (TYPE_SHORT == pIFDSpec->type) ||
        (TYPE_LONG == pIFDSpec->type)) {
        pIFDSpec->value = pIFDEle->val1;
    } else if ((TYPE_RATIONAL == pIFDSpec->type) || (TYPE_RATIONAL_SIGNED == pIFDSpec->type)) {
        if ((pIFDSpec->tag == TAG_GPS_LATITUDE) || (pIFDSpec->tag == TAG_GPS_LONGITUDE) || (pIFDSpec->tag == TAG_GPS_TIMESTAMP)) {
            pIFDSpec->count = 3;
            pIFDSpec->value = pGrp->variedLenIdx - IFDHEAD_OFFSET;
            *(uint32_t*)(pDst + pGrp->variedLenIdx) = pIFDEle->val1;
            *(uint32_t*)(pDst + pGrp->variedLenIdx + 4) = pIFDEle->val2;
            pGrp->variedLenIdx += 8;

            *(uint32_t*)(pDst + pGrp->variedLenIdx) = pIFDEle->val3;
            *(uint32_t*)(pDst + pGrp->variedLenIdx + 4) = pIFDEle->val4;
            pGrp->variedLenIdx += 8;

            *(uint32_t*)(pDst + pGrp->variedLenIdx) = pIFDEle->val5;
            *(uint32_t*)(pDst + pGrp->variedLenIdx + 4) = pIFDEle->val6;
            pGrp->variedLenIdx += 8;
        } else {
            pIFDSpec->value = pGrp->variedLenIdx - IFDHEAD_OFFSET;
            *(uint32_t*)(pDst + pGrp->variedLenIdx) = pIFDEle->val1;
            *(uint32_t*)(pDst + pGrp->variedLenIdx + 4) = pIFDEle->val2;
            pGrp->variedLenIdx += 8;
        }
    } else if ((TYPE_ASCII == pIFDSpec->type) || (TYPE_UNDEFINED == pIFDSpec->type)) {
        pIFDSpec->value = pGrp->variedLenIdx - IFDHEAD_OFFSET;
        if (strlen(pIFDEle->strVal) < 4) {
            strcpy((char*)(&pIFDSpec->value), pIFDEle->strVal);
        } else if (TYPE_UNDEFINED == pIFDSpec->type) {
            memcpy((char*)pDst + pGrp->variedLenIdx, pIFDEle->strVal, tag_length);
            pGrp->variedLenIdx += tag_length;
        } else {
            strcpy((char*)pDst + pGrp->variedLenIdx, pIFDEle->strVal);
            pGrp->variedLenIdx += strlen(pIFDEle->strVal) + 1;
        }
    } else {
        ALOGE("%s, unsupported type 0x%x", __func__, pIFDSpec->type);
        return -1;
    }

    pGrp->fixedLenIdx += IFDELE_SIZE;

    return 0;
}

static bool IsTiffTags(uint16_t tag)
{
    bool flag = false;

    switch (tag) {
        case TAG_MODEL:
        case TAG_MAKE:
        case TAG_DATETIME:
        case TAG_IMAGE_WIDTH:
        case TAG_IMAGE_LENGTH:
        case TAG_ORIENTATION:
        case TAG_EXIF_OFFSET:
            flag = true;
            break;
        default:
            flag = false;
            break;
    }

    return flag;
}

static bool IsExifTags(uint16_t tag)
{
    bool flag = false;

    switch (tag) {
        case TAG_FOCALLENGTH:
            flag = true;
            break;
        default:
            flag = false;
            break;
    }

    return flag;
}

static bool IsGpsTags(uint16_t tag)
{
    bool flag = false;

    switch (tag) {
        case TAG_GPS_LATITUDE_REF:
        case TAG_GPS_LATITUDE:
        case TAG_GPS_LONGITUDE_REF:
        case TAG_GPS_LONGITUDE:
        case TAG_GPS_ALTITUDE_REF:
        case TAG_GPS_ALTITUDE:
        case TAG_GPS_MAP_DATUM:
        case TAG_GPS_PROCESSING_METHOD:
        case TAG_GPS_VERSION_ID:
        case TAG_GPS_TIMESTAMP:
        case TAG_GPS_DATESTAMP:
            flag = true;
            break;
        default:
            flag = false;
            break;
    }

    return flag;
}

static int InsertThumb(uint8_t* pThumb,
                       uint32_t thumbSize,
                       uint8_t* pDst,
                       uint32_t /*dstSize*/)
{
    if (pThumb == NULL)
        return 0;

    memcpy(pDst + g_thumbNailOffset, pThumb, thumbSize);

    return 0;
}

// Define Quantization Table
#define DQT_Mark_0 0xff
#define DQT_Mark_1 0xdb
#define DRI_Mark_0 0xff
#define DRI_Mark_1 0xdd

static int InsertMain(uint8_t* pMain,
                      uint32_t mainSize,
                      uint8_t* pDst,
                      uint32_t /*dstSize*/)
{
    uint32_t i = 0;
    uint32_t DRIOffset = 0;

    while ((((pMain[i] != DRI_Mark_0) || (pMain[i + 1] != DRI_Mark_1)) && ((pMain[i] != DQT_Mark_0) || (pMain[i + 1] != DQT_Mark_1))) &&
           (i + 2 <= mainSize))
        i++;

    if (i + 2 > mainSize) {
        ALOGE("InsertMain, can't find DRI Mark and DQT Mark, mainSize %d", mainSize);
        return -1;
    }

    DRIOffset = i;
    ALOGV("InsertMain, DQTOffset %d", DRIOffset);

    memcpy(pDst + g_mainJpgOffset, pMain + DRIOffset, mainSize - DRIOffset);

    return 0;
}

static uint32_t CalcGroupSize(IFDGroup* pIFDGrp)
{
    uint32_t i;
    uint32_t fixedSize = 0;
    uint32_t variedSize = 0;
    IFDEle* pIFDEle = NULL;
    uint16_t type;

    if (pIFDGrp == NULL)
        return -1;

    if(pIFDGrp->eleNum == 0)
        goto finished;

    // 2 bytes of ele number
    // 4 bytes of next IFD offset
    fixedSize = 2 + IFDELE_SIZE * pIFDGrp->eleNum + 4;

    for (i = 0; i < pIFDGrp->eleNum; i++) {
        pIFDEle = &pIFDGrp->eleArray[i];
        type = GetTypeFromTag(pIFDEle->tag);

        if (TYPE_ASCII == type) {
            if (strlen(pIFDEle->strVal) < 4) {
                ALOGI("the length of pIFDEle->strVal less than 4 bytes");
            } else {
                variedSize += strlen(pIFDEle->strVal) + 1;
            }
        } else if (TYPE_UNDEFINED == type) {
                tag_length = sizeof(ExifAsciiPrefix) +
                             strlen(pIFDEle->strVal + sizeof(ExifAsciiPrefix));
            variedSize += tag_length;
        } else if ((TYPE_RATIONAL == type) || (TYPE_RATIONAL_SIGNED == type)) {
            if (pIFDEle->val6 != 0) {
                variedSize += 24;
            } else {
                variedSize += 8;
            }
        }
    }

finished:
    pIFDGrp->fixedSize = fixedSize;
    pIFDGrp->variedSize = variedSize;
    pIFDGrp->grpSize = fixedSize + variedSize;

    return 0;
}

#define BOUND_CHECK(idx, bound)                                      \
    do {                                                             \
        if (idx >= bound) {                                          \
            ALOGI("%s, idx(%d) >= bound(%d)", __func__, idx, bound); \
            return -1;                                               \
        }                                                            \
    } while (0)

static int ScanIFD(IFDEle* pIFDEle,
                   uint32_t eleNum,
                   uint32_t thumbSize,
                   uint32_t mainSize,
                   uint32_t* pRequestSize)
{
    uint32_t i;
    uint32_t requestSize = 0;
    bool bTagExifOffsetAdded = false;
    bool bTagGpsOffsetAdded = false;
    uint32_t totalHeadSize =
        ARRAYSIZE(SOIMark) + ARRAYSIZE(APP1Head) + ARRAYSIZE(IFDHead);

    memset(&g_TiffGroup, 0, sizeof(g_TiffGroup));
    memset(&g_ExifGroup, 0, sizeof(g_ExifGroup));
    memset(&g_GpsGroup, 0, sizeof(g_GpsGroup));
    memset(&g_TiffGroup_1st, 0, sizeof(g_TiffGroup_1st));

    for (i = 0; i < eleNum; i++) {
        if (IsTiffTags(pIFDEle[i].tag)) {
            BOUND_CHECK(g_TiffGroup.eleNum, MAX_ELE_NUM);
            g_TiffGroup.eleArray[g_TiffGroup.eleNum] = pIFDEle[i];
            g_TiffGroup.eleNum++;
        } else if (IsGpsTags(pIFDEle[i].tag)) {
            BOUND_CHECK(g_GpsGroup.eleNum, MAX_ELE_NUM);
            g_GpsGroup.eleArray[g_GpsGroup.eleNum] = pIFDEle[i];
            g_GpsGroup.eleNum++;

            // TAG_GPS_OFFSET
            if (bTagGpsOffsetAdded == false) {
                BOUND_CHECK(g_TiffGroup.eleNum, MAX_ELE_NUM);
                g_TiffGroup.eleArray[g_TiffGroup.eleNum].tag = TAG_GPS_OFFSET;
                g_TiffGroup.gpsOffsetIdx = g_TiffGroup.eleNum;
                g_TiffGroup.eleNum++;
                bTagGpsOffsetAdded = true;
            }
        } else if (IsExifTags(pIFDEle[i].tag)) {
            BOUND_CHECK(g_ExifGroup.eleNum, MAX_ELE_NUM);
            g_ExifGroup.eleArray[g_ExifGroup.eleNum] = pIFDEle[i];
            g_ExifGroup.eleNum++;

            // TAG_EXIF_OFFSET
            if (bTagExifOffsetAdded == false) {
                BOUND_CHECK(g_TiffGroup.eleNum, MAX_ELE_NUM);
                g_TiffGroup.eleArray[g_TiffGroup.eleNum].tag = TAG_EXIF_OFFSET;
                g_TiffGroup.exifOffsetIdx = g_TiffGroup.eleNum;
                g_TiffGroup.eleNum++;
                bTagExifOffsetAdded = true;
            }
        }
    }

    CalcGroupSize(&g_TiffGroup);
    CalcGroupSize(&g_ExifGroup);
    CalcGroupSize(&g_GpsGroup);

    g_TiffGroup.eleArray[g_TiffGroup.gpsOffsetIdx].val1 =
        ARRAYSIZE(IFDHead) + g_TiffGroup.grpSize;

    g_TiffGroup.eleArray[g_TiffGroup.exifOffsetIdx].val1 =
        ARRAYSIZE(IFDHead) + g_TiffGroup.grpSize + g_GpsGroup.grpSize;

    if(thumbSize > 0) {
        g_TiffGroup.nextIFDOffset =
            ARRAYSIZE(IFDHead) + g_TiffGroup.grpSize + g_GpsGroup.grpSize + g_ExifGroup.grpSize;

        BOUND_CHECK(g_TiffGroup_1st.eleNum, MAX_ELE_NUM);
        g_TiffGroup_1st.eleArray[g_TiffGroup_1st.eleNum].tag = TAG_THUMBNAIL_OFFSET;
        g_TiffGroup_1st.eleNum++;

        BOUND_CHECK(g_TiffGroup_1st.eleNum, MAX_ELE_NUM);
        g_TiffGroup_1st.eleArray[g_TiffGroup_1st.eleNum].tag = TAG_THUMBNAIL_LENGTH;
        g_TiffGroup_1st.eleArray[g_TiffGroup_1st.eleNum].val1 = thumbSize;
        g_TiffGroup_1st.eleNum++;

        CalcGroupSize(&g_TiffGroup_1st);
        g_TiffGroup_1st.eleArray[0].val1 =
           ARRAYSIZE(IFDHead) + g_TiffGroup.grpSize + g_GpsGroup.grpSize + g_ExifGroup.grpSize + g_TiffGroup_1st.grpSize;
    }


    g_thumbNailOffset = totalHeadSize + g_TiffGroup.grpSize + g_GpsGroup.grpSize + g_ExifGroup.grpSize + g_TiffGroup_1st.grpSize;
    g_mainJpgOffset = g_thumbNailOffset + thumbSize;
    requestSize = g_mainJpgOffset + mainSize;

    if (pRequestSize)
        *pRequestSize = requestSize;

    // calulate fixed (IFDs, each 12 bytes) and varied (such as string, ratio data) area offset for each group
    g_TiffGroup.fixedLenIdx = IFDGROUP_OFFSET;
    g_TiffGroup.variedLenIdx = g_TiffGroup.fixedLenIdx + g_TiffGroup.fixedSize;

    g_GpsGroup.fixedLenIdx = IFDGROUP_OFFSET + g_TiffGroup.grpSize;
    g_GpsGroup.variedLenIdx = g_GpsGroup.fixedLenIdx + g_GpsGroup.fixedSize;

    g_ExifGroup.fixedLenIdx = IFDGROUP_OFFSET + g_TiffGroup.grpSize + g_GpsGroup.grpSize;
    g_ExifGroup.variedLenIdx = g_ExifGroup.fixedLenIdx + g_ExifGroup.fixedSize;

    g_TiffGroup_1st.fixedLenIdx = IFDGROUP_OFFSET + g_TiffGroup.grpSize + g_GpsGroup.grpSize + g_ExifGroup.grpSize;
    g_TiffGroup_1st.variedLenIdx = g_TiffGroup_1st.fixedLenIdx + g_TiffGroup_1st.fixedSize;

    ALOGI("requestSize %d, thumb offset 0x%x, main offset 0x%x",
          requestSize,
          g_thumbNailOffset,
          g_mainJpgOffset);

    ALOGI(
        "TiffGrp elenum %d, fixedSzie %d, Size %d, fixedLenIdx 0x%x, "
        "variedLenIdx 0x%x",
        g_TiffGroup.eleNum,
        g_TiffGroup.fixedSize,
        g_TiffGroup.grpSize,
        g_TiffGroup.fixedLenIdx,
        g_TiffGroup.variedLenIdx);

    ALOGI(
        "GpsGroup elenum %d, fixedSzie %d, Size %d, fixedLenIdx 0x%x, "
        "variedLenIdx 0x%x",
        g_GpsGroup.eleNum,
        g_GpsGroup.fixedSize,
        g_GpsGroup.grpSize,
        g_GpsGroup.fixedLenIdx,
        g_GpsGroup.variedLenIdx);

    ALOGI(
        "ExifGroup elenum %d, fixedSzie %d, Size %d, fixedLenIdx 0x%x, "
        "variedLenIdx 0x%x",
        g_ExifGroup.eleNum,
        g_ExifGroup.fixedSize,
        g_ExifGroup.grpSize,
        g_ExifGroup.fixedLenIdx,
        g_ExifGroup.variedLenIdx);

    ALOGI(
        "TiffGrp_1st elenum %d, fixedSzie %d, Size %d, fixedLenIdx 0x%x, "
        "variedLenIdx 0x%x",
        g_TiffGroup_1st.eleNum,
        g_TiffGroup_1st.fixedSize,
        g_TiffGroup_1st.grpSize,
        g_TiffGroup_1st.fixedLenIdx,
        g_TiffGroup_1st.variedLenIdx);

    return 0;
}

static int InsertGrp(IFDGroup* pGrp, uint8_t* pDst)
{
    uint32_t i;
    int ret;

    if ((pGrp == NULL) || (pDst == NULL))
        return -1;

    if ((pGrp->eleNum) == 0)
        return 0;

    // write IFD element number
    *(uint16_t*)(pDst + pGrp->fixedLenIdx) = (uint16_t)pGrp->eleNum;
    pGrp->fixedLenIdx += 2;

    for (i = 0; i < pGrp->eleNum; i++) {
        ret = WriteOneIFD(pGrp, i, pDst);
        if (ret) {
            return ret;
        }
    }

    // next IFD offset, 4 bytes
    *(uint32_t*)(pDst + pGrp->fixedLenIdx) = pGrp->nextIFDOffset;
    pGrp->fixedLenIdx += 4;

    return 0;
}

int InsertEXIFAndThumbnail(IFDEle* pIFDEle,
                           uint32_t eleNum,
                           uint8_t* pThumb,
                           uint32_t thumbSize,
                           uint8_t* pMain,
                           uint32_t mainSize,
                           uint8_t* pDst,
                           uint32_t dstSize,
                           uint32_t *pRequestSize)
{
    int ret;
    uint32_t requestSize;
    uint32_t app1Size;

    if ((pIFDEle == NULL) || (eleNum == 0) || (pDst == NULL) || (dstSize == 0) ||
        (pMain == NULL) || (mainSize == 0)) {
        ALOGE(
            "%s, para err, pIFDEle %p, eleNum %d, pDst %p, dstSize %d, pMain %p, "
            "mainSize %d",
            __func__,
            pIFDEle,
            eleNum,
            pDst,
            dstSize,
            pMain,
            mainSize);
        return -1;
    }

    if ((thumbSize == 0) || (pThumb == NULL)) {
        thumbSize = 0;
        pThumb = NULL;
    }

    ret = ScanIFD(pIFDEle, eleNum, thumbSize, mainSize, &requestSize);
    if (ret) {
        ALOGE("%s, ScanIFD failed, ret %d", __func__, ret);
        return ret;
    }

    if (pRequestSize)
        *pRequestSize = requestSize;

    if (dstSize < requestSize) {
        ALOGE("%s, dstSize(%d) < requestSize(%d)", __func__, dstSize, requestSize);
        return -1;
    }

    // write head
    memcpy(pDst, SOIMark, ARRAYSIZE(SOIMark));
    memcpy(pDst + ARRAYSIZE(SOIMark), APP1Head, ARRAYSIZE(APP1Head));
    memcpy(pDst + IFDHEAD_OFFSET, IFDHead, ARRAYSIZE(IFDHead));

    app1Size = (uint16_t)g_mainJpgOffset - 4;
    pDst[4] = (uint8_t)(app1Size >> 8);
    pDst[5] = (uint8_t)(app1Size & 0xff);

    ret = InsertGrp(&g_TiffGroup, pDst);
    if (ret) {
        ALOGE("%s, InsertGrp g_TiffGroup failed, ret %d", __func__, ret);
        return ret;
    }

    ret = InsertGrp(&g_GpsGroup, pDst);
    if (ret) {
        ALOGE("%s, InsertGrp g_GpsGroup failed, ret %d", __func__, ret);
        return ret;
    }

    ret = InsertGrp(&g_ExifGroup, pDst);
    if (ret) {
        ALOGE("%s, InsertGrp g_ExifGroup failed, ret %d", __func__, ret);
        return ret;
    }

    ret = InsertGrp(&g_TiffGroup_1st, pDst);
    if (ret) {
        ALOGE("%s, InsertGrp g_TiffGroup_1st failed, ret %d", __func__, ret);
        return ret;
    }


    ALOGV("should equal, g_ExifGroup.variedLenIdx %d, g_thumbNailOffset %d",
          g_ExifGroup.variedLenIdx,
          g_thumbNailOffset);

    ret = InsertThumb(pThumb, thumbSize, pDst, dstSize);
    if (ret) {
        ALOGE("%s, InsertThumb failed, ret %d", __func__, ret);
        return ret;
    }

    ret = InsertMain(pMain, mainSize, pDst, dstSize);
    if (ret) {
        ALOGE("%s, InsertMain failed, ret %d", __func__, ret);
        return ret;
    }

    return ret;
}
