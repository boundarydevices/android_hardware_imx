/*
 * Copyright 2018 NXP.
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
#include <cutils/log.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "Edid.h"

Edid::Edid(int fd,uint32_t connectorID)
{
    mIsHdrSupported = false;
    memset(&mHdrMetaData, 0, sizeof(mHdrMetaData));
    getEdidDataFromDrm(fd,connectorID);
}

Edid::~Edid()
{
}

bool Edid::isHdrSupported()
{
    return mIsHdrSupported;
}

int Edid::getHdrMetaData(HdrMetaData* hdrMetaData)
{
    if (hdrMetaData == NULL)
        return -EINVAL;
    *hdrMetaData = mHdrMetaData;
    return 0;
}

void Edid::getEdidDataFromDrm(int fd,uint32_t connectorID)
{
    ALOGI("Try to check fd %d connector:%d edid info",fd,connectorID);

    //Step1: get edid blobid for specified card and connector
    uint32_t edidBlobId = 0;
    drmModeObjectPropertiesPtr props;
    props = drmModeObjectGetProperties(fd,connectorID,DRM_MODE_OBJECT_CONNECTOR);
    if (!props) {
        ALOGE("%s No properties: %s", __FUNCTION__,strerror(errno));
        return;
    }
    for (uint32_t i=0; i<props->count_props; i++)
    {
        drmModePropertyPtr prop;
        prop = drmModeGetProperty(fd, props->props[i]);
        if (strcmp(prop->name,"EDID") == 0) {
            edidBlobId = props->prop_values[i];//find EDID blob
        }
        drmModeFreeProperty(prop);
    }
    drmModeFreeObjectProperties(props);

    //Step2: get edid data by edid blobid
    drmModePropertyBlobPtr blob;
    blob = drmModeGetPropertyBlob(fd, edidBlobId);
    if (!blob) {
        ALOGE("%s drmModeGetPropertyBlob failed %d",__FUNCTION__,errno);
        return;
    }
    unsigned char *edid;
    edid = (unsigned char *)blob->data;

    if (isEdidValid(edid)) {
        unsigned char *edidExt = getCeaExtensionData(edid);
        parseCeaExtData(edidExt);
    }
    drmModeFreePropertyBlob(blob);
}

bool Edid::isEdidValid(unsigned char *edid)
{
    if (edid == NULL)
        return false;
    unsigned char check = edid[EDID_LENGTH-1];
    unsigned char sum = 0;

    for (int i=0; i<EDID_LENGTH-1; i++)
        sum += edid[i];
    if ((unsigned char)(check + sum) != 0) {
        ALOGE("Checksum should be 0x%x", -sum & 0xff);
        return false;
    }
    return true;
}

unsigned char* Edid::getCeaExtensionData(unsigned char *edid)
{
    int extension_num = edid[EXTENSION_NUM];
    for (int i=1; i<=extension_num; i++) {
        unsigned char* edidExt = edid + EDID_LENGTH * i;
        if (!isEdidValid(edidExt))
            return NULL;

        if (edidExt[0] != CEA_EXTENSION)//CEA extension block
            continue;

        return edidExt;
    }
    return NULL;
}

int Edid::getDataBlockLen(unsigned char *db)
{
    return db[0] & LEN_MASK;
}

bool Edid::isHdrMetadataBlock(unsigned char *db)
{
    if (db[0]>>TAG_SHIFT != DATA_BLOCK_EXTENDED_TAG)
        return false;
    if (db[1] != HDR_STATIC_METADATA_BLOCK)
        return false;
    return true;
}

void Edid::parseHdrMetadataBlock(unsigned char *db)
{
    int len = getDataBlockLen(db);
    if (len == 6) {
        mHdrMetaData.max_cll  = db[4];
        mHdrMetaData.max_fall = db[5];
        mHdrMetaData.min_cll  = db[6];
    } else if (len == 5) {
        mHdrMetaData.max_cll  = db[4];
        mHdrMetaData.max_fall = db[5];
    } else if (len == 4) {
        mHdrMetaData.max_cll  = db[4];
    } else if (len == 3) {
        if (db[2]& 0x1) {
            mHdrMetaData.max_cll  = SDR_DEFAULT_LUMINANCE;
            mHdrMetaData.max_fall = SDR_DEFAULT_LUMINANCE;
            mHdrMetaData.min_cll  = SDR_DEFAULT_LUMINANCE;
        }
    }
}

void Edid::parseCeaExtData(unsigned char *edidExt)
{
    if (!isEdidValid(edidExt))
        return;

    int ceaDbStart = 4;
    int ceaDbLen = edidExt[2];
    int len = 0;
    for (int i = ceaDbStart; i < ceaDbLen; i += len +1) {
        unsigned char *ceaDb = &edidExt[i];
        len = getDataBlockLen(&edidExt[i]);
        if (isHdrMetadataBlock(ceaDb)) {
            mIsHdrSupported = true;
            parseHdrMetadataBlock(ceaDb);
        }
    }
}

void Edid::dumpHdrMetaData()
{
    ALOGI("hdrMetaData：isHdrSupported:%d maxcll:%f,max_fall:%f,min_cll:%f",
        mIsHdrSupported,mHdrMetaData.max_cll,mHdrMetaData.max_fall,mHdrMetaData.min_cll);
}
