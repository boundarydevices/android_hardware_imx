/*
 * Copyright 2018-2023 NXP
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
#include "Edid.h"

#include <drm_mode.h>
#include <errno.h>
#include <hardware/hwcomposer2.h>
#include <log/log.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <system/graphics-base-v1.0.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

namespace aidl::android::hardware::graphics::composer3::impl {

Edid::Edid(std::vector<uint8_t> &edid) {
    mIsValid = false;
    mIsHdrSupported = false;
    memset(&mHdrMetaData, 0, sizeof(mHdrMetaData));
    mNumTypes = 0;

    uint8_t *data = edid.data();

    if (isEdidValid(data)) {
        unsigned char *edidExt = getCeaExtensionData(data, uint32_t(edid.size()));
        if (edidExt)
            parseCeaExtData(edidExt);
        memcpy(mRawData, data, EDID_LENGTH);
        mIsValid = true;
    }
}

Edid::~Edid() {}

bool Edid::isHdrSupported() {
    return mIsHdrSupported;
}

int Edid::getHdrMetaData(HdrMetaData *hdrMetaData) {
    if (hdrMetaData == NULL)
        return -EINVAL;
    *hdrMetaData = mHdrMetaData;
    return 0;
}

int Edid::getEdidRawData(uint8_t *buf, int size) {
    if ((buf == NULL) || (size < EDID_LENGTH) || (!mIsValid))
        return -1;

    memcpy(buf, mRawData, EDID_LENGTH);

    return EDID_LENGTH;
}

int Edid::getHdrSupportTypes(uint32_t *numTypes, int32_t *hdrTypes) {
    if (!numTypes)
        return -1;

    typedef struct {
        int32_t eotf;
        int32_t hdr;
    } EotfHdrTable;

    EotfHdrTable hdrTable[2] = {
            {SMPTE_ST2084, HAL_HDR_HDR10},
            {BT_2100_HLG, HAL_HDR_HLG},
    };

    *numTypes = 0;
    for (int i = 0; i < int(sizeof(hdrTable) / sizeof(hdrTable[0])); i++) {
        if (mHdrMetaData.eotf & (1 << hdrTable[i].eotf)) {
            if (hdrTypes) {
                hdrTypes[*numTypes] = hdrTable[i].hdr;
            }
            (*numTypes)++;
        }
    }

    return 0;
}

bool Edid::isHdrEotfSupported(uint32_t eotf) {
    return (mHdrMetaData.eotf & (1 << eotf));
}

bool Edid::isEdidValid(unsigned char *edid) {
    if (edid == NULL)
        return false;
    unsigned char check = edid[EDID_LENGTH - 1];
    unsigned char sum = 0;

    for (int i = 0; i < EDID_LENGTH - 1; i++) sum += edid[i];
    if ((unsigned char)(check + sum) != 0) {
        ALOGE("Checksum should be 0x%x", -sum & 0xff);
        return false;
    }
    return true;
}

unsigned char *Edid::getCeaExtensionData(unsigned char *edid, uint32_t size) {
    uint32_t extension_num = edid[EXTENSION_NUM];
    for (uint32_t i = 1; i <= extension_num; i++) {
        if (EDID_LENGTH * (i + 1) >= size)
            break;
        unsigned char *edidExt = edid + EDID_LENGTH * i;
        if (!isEdidValid(edidExt))
            return NULL;

        if (edidExt[0] != CEA_EXTENSION) // CEA extension block
            continue;

        return edidExt;
    }
    return NULL;
}

int Edid::getDataBlockLen(unsigned char *db) {
    return db[0] & LEN_MASK;
}

bool Edid::isHdrMetadataBlock(unsigned char *db) {
    if (db[0] >> TAG_SHIFT != DATA_BLOCK_EXTENDED_TAG)
        return false;
    if (db[1] != HDR_STATIC_METADATA_BLOCK)
        return false;
    return true;
}

void Edid::parseHdrMetadataBlock(unsigned char *db) {
    int len = getDataBlockLen(db);
    mHdrMetaData.eotf = db[2];
    if (len == 6) {
        mHdrMetaData.max_cll = db[4];
        mHdrMetaData.max_fall = db[5];
        mHdrMetaData.min_cll = db[6];
    } else if (len == 5) {
        mHdrMetaData.max_cll = db[4];
        mHdrMetaData.max_fall = db[5];
    } else if (len == 4) {
        mHdrMetaData.max_cll = db[4];
    } else if (len == 3) {
        if (db[2] & 0x1) {
            mHdrMetaData.max_cll = SDR_DEFAULT_LUMINANCE;
            mHdrMetaData.max_fall = SDR_DEFAULT_LUMINANCE;
            mHdrMetaData.min_cll = SDR_DEFAULT_LUMINANCE;
        }
    }
}

void Edid::parseCeaExtData(unsigned char *edidExt) {
    if (!isEdidValid(edidExt))
        return;

    int ceaDbStart = 4;
    int ceaDbLen = edidExt[2];
    int len = 0;
    for (int i = ceaDbStart; i < ceaDbLen; i += len + 1) {
        unsigned char *ceaDb = &edidExt[i];
        len = getDataBlockLen(&edidExt[i]);
        if (isHdrMetadataBlock(ceaDb)) {
            mIsHdrSupported = true;
            parseHdrMetadataBlock(ceaDb);
            getHdrSupportTypes(&mNumTypes, NULL);
        }
    }
}

void Edid::getHdrCapabilities(HdrCapabilities *outCapabilities) {
    uint32_t count;
    getHdrSupportTypes(&count, NULL);

    std::vector<int32_t> hwcHdrTypes(count);
    getHdrSupportTypes(&count, hwcHdrTypes.data());

    outCapabilities->maxLuminance = mHdrMetaData.max_cll;
    outCapabilities->maxAverageLuminance = mHdrMetaData.max_fall;
    outCapabilities->minLuminance = mHdrMetaData.min_cll;

    for (auto const &type : hwcHdrTypes) {
        common::Hdr atype = static_cast<common::Hdr>(type);
        outCapabilities->types.emplace_back(std::move(atype));
    }
}

void Edid::getPerFrameMetadataKeys(std::vector<PerFrameMetadataKey> *outKeys) {
    outKeys->push_back(PerFrameMetadataKey::DISPLAY_RED_PRIMARY_X);
    outKeys->push_back(PerFrameMetadataKey::DISPLAY_RED_PRIMARY_Y);
    outKeys->push_back(PerFrameMetadataKey::DISPLAY_GREEN_PRIMARY_X);
    outKeys->push_back(PerFrameMetadataKey::DISPLAY_GREEN_PRIMARY_Y);
    outKeys->push_back(PerFrameMetadataKey::DISPLAY_BLUE_PRIMARY_X);
    outKeys->push_back(PerFrameMetadataKey::DISPLAY_BLUE_PRIMARY_Y);
    outKeys->push_back(PerFrameMetadataKey::WHITE_POINT_X);
    outKeys->push_back(PerFrameMetadataKey::WHITE_POINT_Y);
    outKeys->push_back(PerFrameMetadataKey::MAX_LUMINANCE);
    outKeys->push_back(PerFrameMetadataKey::MIN_LUMINANCE);
    outKeys->push_back(PerFrameMetadataKey::MAX_CONTENT_LIGHT_LEVEL);
    outKeys->push_back(PerFrameMetadataKey::MAX_FRAME_AVERAGE_LIGHT_LEVEL);
}

void Edid::setPerFrameMetadata(const std::vector<std::optional<PerFrameMetadata>> &perFrameMetadata,
                               common::Dataspace &dataspace, hdr_output_metadata &hdrMetadata) {
    hdrMetadata.metadata_type = 0;
    hdrMetadata.hdmi_metadata_type1.eotf = SMPTE_ST2084;
    hdrMetadata.hdmi_metadata_type1.metadata_type = 1;

    if (HAL_DATASPACE_TRANSFER_HLG == ((int)dataspace & HAL_DATASPACE_TRANSFER_MASK)) {
        hdrMetadata.hdmi_metadata_type1.eotf = BT_2100_HLG;
    }

    for (auto &frameMetadata : perFrameMetadata) {
        auto key = frameMetadata->key;
        auto metadata = frameMetadata->value;
        switch (key) {
            case PerFrameMetadataKey::DISPLAY_RED_PRIMARY_X:
                hdrMetadata.hdmi_metadata_type1.display_primaries[0].x =
                        (uint16_t)(metadata * 50000);
                break;
            case PerFrameMetadataKey::DISPLAY_RED_PRIMARY_Y:
                hdrMetadata.hdmi_metadata_type1.display_primaries[0].y =
                        (uint16_t)(metadata * 50000);
                break;
            case PerFrameMetadataKey::DISPLAY_GREEN_PRIMARY_X:
                hdrMetadata.hdmi_metadata_type1.display_primaries[1].x =
                        (uint16_t)(metadata * 50000);
                break;
            case PerFrameMetadataKey::DISPLAY_GREEN_PRIMARY_Y:
                hdrMetadata.hdmi_metadata_type1.display_primaries[1].y =
                        (uint16_t)(metadata * 50000);
                break;
            case PerFrameMetadataKey::DISPLAY_BLUE_PRIMARY_X:
                hdrMetadata.hdmi_metadata_type1.display_primaries[2].x =
                        (uint16_t)(metadata * 50000);
                break;
            case PerFrameMetadataKey::DISPLAY_BLUE_PRIMARY_Y:
                hdrMetadata.hdmi_metadata_type1.display_primaries[2].y =
                        (uint16_t)(metadata * 50000);
                break;
            case PerFrameMetadataKey::WHITE_POINT_X:
                hdrMetadata.hdmi_metadata_type1.white_point.x = (uint16_t)(metadata * 50000);
                break;
            case PerFrameMetadataKey::WHITE_POINT_Y:
                hdrMetadata.hdmi_metadata_type1.white_point.y = (uint16_t)(metadata * 50000);
                break;
            case PerFrameMetadataKey::MAX_LUMINANCE:
                hdrMetadata.hdmi_metadata_type1.max_display_mastering_luminance =
                        (uint16_t)(metadata);
                break;
            case PerFrameMetadataKey::MIN_LUMINANCE:
                hdrMetadata.hdmi_metadata_type1.min_display_mastering_luminance =
                        (uint16_t)(metadata * 10000);
                break;
            case PerFrameMetadataKey::MAX_CONTENT_LIGHT_LEVEL:
                hdrMetadata.hdmi_metadata_type1.max_cll = (uint16_t)(metadata);
                break;
            case PerFrameMetadataKey::MAX_FRAME_AVERAGE_LIGHT_LEVEL:
                hdrMetadata.hdmi_metadata_type1.max_fall = (uint16_t)(metadata);
                break;
            case PerFrameMetadataKey::HDR10_PLUS_SEI:
                break; // Not supprot yet
        }
    }
}

void Edid::dumpHdrMetaData() {
    ALOGI("hdrMetaData：isHdrSupported:%d maxcll:%f,max_fall:%f,min_cll:%f", mIsHdrSupported,
          mHdrMetaData.max_cll, mHdrMetaData.max_fall, mHdrMetaData.min_cll);
}

} // namespace aidl::android::hardware::graphics::composer3::impl
