#ifndef VENDOR_TAGS_H
#define VENDOR_TAGS_H

#include <hal_types.h>

namespace android {

using google_camera_hal::CameraMetadataType;
using google_camera_hal::VendorTagSection;

static const uint32_t kImxTagIdOffset = VENDOR_SECTION_START;

typedef enum enumImxTag {
    VSI_DEWARP = kImxTagIdOffset,
    VSI_HFLIP,
    VSI_VFLIP,
    VSI_LSC
} ImxTag;

static const std::vector<VendorTagSection> kImxTagSections = {
    {
        .section_name = "vsi",
        .tags =
            {
                {
                    .tag_id = VSI_DEWARP,
                    .tag_name = "dewarp.enable",
                    .tag_type = CameraMetadataType::kInt32,
                },
                {
                    .tag_id = VSI_HFLIP,
                    .tag_name = "hflip.enable",
                    .tag_type = CameraMetadataType::kInt32,
                },
                {
                    .tag_id = VSI_VFLIP,
                    .tag_name = "vflip.enable",
                    .tag_type = CameraMetadataType::kInt32,
                },
                {
                    .tag_id = VSI_LSC,
                    .tag_name = "lsc.enable",
                    .tag_type = CameraMetadataType::kInt32,
                },
            }
    }
};

}  // namespace android

#endif // VENDOR_TAGS_H
