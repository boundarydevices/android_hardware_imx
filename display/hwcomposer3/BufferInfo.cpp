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

#include "BufferInfo.h"

#include <core/buffer.h>    /* arm gralloc handle for imx9x*/
#include <core/drm_utils.h>
#include <gralloc_handle.h> /* gralloc handle for legacy imx */
#include <hardware/gralloc.h>
#include <ui/GraphicBufferMapper.h>

#include "Common.h"
#include "Drm.h"

namespace aidl::android::hardware::graphics::composer3::impl {

int getInfoFromHandle(buffer_handle_t handle, HandleInfo *info) {
    if ((static_cast<const private_handle_t *>(handle))->magic == private_handle_t::sMagic) {
        const imported_handle *memHandle = static_cast<const imported_handle *>(handle);
        info->fd = memHandle->share_fd;
        info->width = static_cast<uint32_t>(memHandle->width);
        info->height = static_cast<uint32_t>(memHandle->height);
        info->format = static_cast<uint32_t>(memHandle->alloc_format.get_base());
        info->stride = memHandle->plane_info[0].alloc_width;
        info->drm_format = drm_fourcc_from_handle(memHandle);
        info->modifier = drm_modifier_from_handle(memHandle);
        info->size = static_cast<uint32_t>(memHandle->size);
        info->usage = memHandle->producer_usage;
        info->num_planes = static_cast<uint32_t>(memHandle->get_num_planes());
        for (uint32_t i = 0; i < info->num_planes; i++) {
            info->strides[i] = memHandle->plane_info[i].byte_stride;
            info->offsets[i] = memHandle->plane_info[i].offset;
        }
        info->name = nullptr;
        info->phys = memHandle->phys;
        info->base = reinterpret_cast<uint64_t>(memHandle->base);
#if defined(DEBUG_NXP_HWC) || defined(DEBUG_NXP_HWC_G2D)
        if (memHandle->attr_base != MAP_FAILED) {
            ::android::GraphicBufferMapper::get().getName(handle, &info->sname);
            info->name = const_cast<char *>(info->sname.c_str());
        }
#endif
    } else if (gralloc_handle_t(handle)->magic == gralloc_handle::sMagic) {
        gralloc_handle_t memHandle = (gralloc_handle_t)handle;
        uint64_t modifier = 0;
        info->fd = memHandle->fds[0];
        info->width = memHandle->width;
        info->height = memHandle->height;
        info->format = static_cast<uint32_t>(memHandle->format);
        info->stride = memHandle->pixel_stride;
        info->drm_format = memHandle->drm_format;
        // TODO: some workaround for framebuffer of legacy imx
        if ((memHandle->format_modifier != DRM_FORMAT_MOD_LINEAR) &&
            (memHandle->usage & GRALLOC_USAGE_HW_FB)) {
            /* workaround GPU SUPER_TILED R/B swap issue, for no-resolve and tiled output
               GPU not distinguish A8B8G8R8 and A8R8G8B8, all regard as A8R8G8B8, need do
               R/B swap here for no-resolve and tiled buffer */
            if (info->drm_format == DRM_FORMAT_XBGR8888)
                info->drm_format = DRM_FORMAT_XRGB8888;
            if (info->drm_format == DRM_FORMAT_ABGR8888)
                info->drm_format = DRM_FORMAT_ARGB8888;
        }
        info->modifier = memHandle->format_modifier;
        info->size = static_cast<uint32_t>(memHandle->total_size);
        info->usage = memHandle->usage;
        info->num_planes = memHandle->num_planes;
        for (int i = 0; i < kBufferMaxPlanes; i++) {
            info->strides[i] = memHandle->strides[i];
            info->offsets[i] = memHandle->offsets[i];
        }
        info->name = const_cast<char *>(memHandle->name);
        info->phys = memHandle->phys; /* only for legacy imx */
        info->base = memHandle->base;
    } else {
        ALOGE("%s: Cannot recognize buffer handle", __FUNCTION__);
        return -1;
    }

    return 0;
}

} // namespace aidl::android::hardware::graphics::composer3::impl
