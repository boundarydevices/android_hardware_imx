/*
 * Copyright 2024 NXP
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

#include <cutils/native_handle.h>

#include "Common.h"

namespace aidl::android::hardware::graphics::composer3::impl {

constexpr int kBufferMaxPlanes = 4;

struct HandleInfo {
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t format; /* PixelFormat */
    uint32_t stride; /* pixel stride*/
    uint32_t size;
    uint32_t drm_format; /* DRM_FORMAT_* from drm_fourcc.h */
    uint64_t modifier;
    uint64_t usage;
    uint32_t num_planes;
    uint32_t strides[kBufferMaxPlanes];
    uint32_t offsets[kBufferMaxPlanes];
    uint32_t sizes[kBufferMaxPlanes];
    char* name;    /* only for debug log, pointer to handle->name */
    uint64_t phys; /* only for legacy imx */
    uint64_t base; /* only for legacy imx */
    std::string sname;
};

int getInfoFromHandle(buffer_handle_t handle, HandleInfo* info);

} // namespace aidl::android::hardware::graphics::composer3::impl
