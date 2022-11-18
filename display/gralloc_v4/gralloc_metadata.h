/*
 * Copyright 2022 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GRALLOC_METADATA_H
#define GRALLOC_METADATA_H

#include <aidl/android/hardware/graphics/common/BlendMode.h>
#include <aidl/android/hardware/graphics/common/Cta861_3.h>
#include <aidl/android/hardware/graphics/common/Dataspace.h>
#include <aidl/android/hardware/graphics/common/Smpte2086.h>

#define GRALLOC_METADATA_MAX_NAME_SIZE 1024

struct gralloc_metadata {
        char name[GRALLOC_METADATA_MAX_NAME_SIZE];
        aidl::android::hardware::graphics::common::BlendMode blendMode;
        aidl::android::hardware::graphics::common::Dataspace dataspace;
        std::optional<aidl::android::hardware::graphics::common::Cta861_3> cta861_3;
        std::optional<aidl::android::hardware::graphics::common::Smpte2086> smpte2086;
};
#endif