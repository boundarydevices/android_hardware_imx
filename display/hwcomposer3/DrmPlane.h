/*
 * Copyright 2022 The Android Open Source Project
 * Copyright 2023 NXP
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

#pragma once

#include <android-base/logging.h>
#include <android-base/unique_fd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include "Common.h"
#include "DrmCrtc.h"
#include "DrmProperty.h"

namespace aidl::android::hardware::graphics::composer3::impl {

enum {
    PLANE_STATE_NONE,
    PLANE_STATE_ACTIVE,
    PLANE_STATE_DISABLED,
};

class DrmPlane {
public:
    static std::unique_ptr<DrmPlane> create(::android::base::borrowed_fd drmFd, uint32_t planeId);
    ~DrmPlane(){};

    uint32_t getId() const { return mId; }

    bool isPrimary() const;
    bool isOverlay() const;

    bool isCompatibleWith(const DrmCrtc& crtc);
    bool checkFormat(uint32_t format, uint64_t modifier);
    int getState() const { return mState; }
    void setState(int state) { mState = state; }
    bool checkFormatSupported(uint32_t format);

    const DrmProperty& getCrtcProperty() const { return mCrtc; }
    const DrmProperty& getInFenceProperty() const { return mInFenceFd; }
    const DrmProperty& getFbProperty() const { return mFb; }
    const DrmProperty& getCrtcXProperty() const { return mCrtcX; }
    const DrmProperty& getCrtcYProperty() const { return mCrtcY; }
    const DrmProperty& getCrtcWProperty() const { return mCrtcW; }
    const DrmProperty& getCrtcHProperty() const { return mCrtcH; }
    const DrmProperty& getSrcXProperty() const { return mSrcX; }
    const DrmProperty& getSrcYProperty() const { return mSrcY; }
    const DrmProperty& getSrcWProperty() const { return mSrcW; }
    const DrmProperty& getSrcHProperty() const { return mSrcH; }
    const DrmProperty& getZposProperty() const { return mZpos; }
    const DrmProperty& getDtrcTableOffestProperty() const { return mDtrcTableOffest; }

private:
    DrmPlane(uint32_t id) : mId(id){};

    const uint32_t mId;

    uint32_t mPossibleCrtcsMask = 0;
    std::vector<uint32_t> mDrmFormats;
    std::unordered_map<uint32_t, std::vector<uint64_t>> mFormatModifiers;
    int mState = PLANE_STATE_NONE;

    DrmProperty mCrtc;
    DrmProperty mInFenceFd;
    DrmProperty mFb;
    DrmProperty mCrtcX;
    DrmProperty mCrtcY;
    DrmProperty mCrtcW;
    DrmProperty mCrtcH;
    DrmProperty mSrcX;
    DrmProperty mSrcY;
    DrmProperty mSrcW;
    DrmProperty mSrcH;
    DrmProperty mType;
    DrmProperty mAlpha;
    DrmProperty mZpos;
    DrmProperty mInFormats;
    DrmProperty mDtrcTableOffest;

    static const auto& GetPropertiesMap() {
        static const auto* sMap = []() {
            return new DrmPropertyMemberMap<DrmPlane>{
                    {"CRTC_ID", &DrmPlane::mCrtc},
                    {"CRTC_X", &DrmPlane::mCrtcX},
                    {"CRTC_Y", &DrmPlane::mCrtcY},
                    {"CRTC_W", &DrmPlane::mCrtcW},
                    {"CRTC_H", &DrmPlane::mCrtcH},
                    {"FB_ID", &DrmPlane::mFb},
                    {"IN_FENCE_FD", &DrmPlane::mInFenceFd},
                    {"SRC_X", &DrmPlane::mSrcX},
                    {"SRC_Y", &DrmPlane::mSrcY},
                    {"SRC_W", &DrmPlane::mSrcW},
                    {"SRC_H", &DrmPlane::mSrcH},
                    {"type", &DrmPlane::mType},
                    {"alpha", &DrmPlane::mAlpha},
                    {"zpos", &DrmPlane::mZpos},
                    {"IN_FORMATS", &DrmPlane::mInFormats},
                    {"dtrc_table_ofs", &DrmPlane::mDtrcTableOffest}, // for overlay in 8mq only
            };
        }();
        return *sMap;
    }
};

} // namespace aidl::android::hardware::graphics::composer3::impl
