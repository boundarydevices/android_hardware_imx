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

#ifndef _NXP_EDID_H_
#define _NXP_EDID_H_

#include <aidl/android/hardware/graphics/composer3/HdrCapabilities.h>
#include <aidl/android/hardware/graphics/composer3/PerFrameMetadata.h>
#include <aidl/android/hardware/graphics/composer3/PerFrameMetadataKey.h>
#include <drm_mode.h>
#include <stdlib.h>

#include <vector>

#include "Common.h"

#define EDID_LENGTH 128
#define CEA_EXTENSION 0x02
#define DATA_BLOCK_EXTENDED_TAG 0x07
#define HDR_STATIC_METADATA_BLOCK 0x06
#define SDR_DEFAULT_LUMINANCE 100

#define EXTENSION_NUM 0x7e
#define LEN_MASK 0x1f
#define TAG_SHIFT 5

namespace aidl::android::hardware::graphics::composer3::impl {

struct HdrMetaData {
    uint8_t eotf;
    float max_cll;
    float max_fall;
    float min_cll;
};

class Edid {
public:
    Edid(std::vector<uint8_t> &edid);
    virtual ~Edid();

    // check HDR is supported or not
    bool isHdrSupported();
    uint32_t getHdrTypeCount() { return mNumTypes; }

    // get EdidMetaData
    int getHdrMetaData(HdrMetaData *hdrMetaData);

    // get edid raw data. return the actual size it get
    int getEdidRawData(uint8_t *buf, int size);

    // get supported HDR types
    int getHdrSupportTypes(uint32_t *numTypes, int32_t *hdrTypes);

    // check HDR eotf is supported or not
    bool isHdrEotfSupported(uint32_t eotf);

    // interfaces for hwcomposer3
    void getHdrCapabilities(HdrCapabilities *outCapabilities);
    void getPerFrameMetadataKeys(std::vector<PerFrameMetadataKey> *outKeys);
    void setPerFrameMetadata(const std::vector<std::optional<PerFrameMetadata>> &perFrameMetadata,
                             common::Dataspace &dataspace, hdr_output_metadata &hdrMetadata);

private:
    bool mIsHdrSupported;
    HdrMetaData mHdrMetaData;
    bool mIsValid;
    uint8_t mRawData[EDID_LENGTH];
    uint32_t mNumTypes;

    // get edid data from drm and parse it
    void getEdidDataFromDrm(int fd, uint32_t connectorID);

    // check whether the edid raw data is valid or not
    bool isEdidValid(unsigned char *edid);

    // get cea extension edid raw data
    unsigned char *getCeaExtensionData(unsigned char *edid, uint32_t size);

    // return cea data block length
    int getDataBlockLen(unsigned char *db);

    // check whether given data block is hdr metadata or not
    bool isHdrMetadataBlock(unsigned char *db);

    // parse the given hdr data block
    void parseHdrMetadataBlock(unsigned char *db);

    // parse cea extension edid raw data
    void parseCeaExtData(unsigned char *edid_ext);

    // dump EdidMetaData
    void dumpHdrMetaData();
};
} // namespace aidl::android::hardware::graphics::composer3::impl
#endif
