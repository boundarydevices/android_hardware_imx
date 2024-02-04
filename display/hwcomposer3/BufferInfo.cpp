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
#include <gralloc_handle.h> /* gralloc handle for legacy imx */

namespace aidl::android::hardware::graphics::composer3::impl {

int getInfoFromHandle(buffer_handle_t handle, HandleInfo *info) {
    if ((static_cast<const private_handle_t *>(handle))->magic == private_handle_t::sMagic) {
        const private_handle_t *memHandle = static_cast<const private_handle_t *>(handle);
        info->fd = memHandle->share_fd;
        info->width = memHandle->width;
        info->height = memHandle->height;
        info->format = memHandle->alloc_format.get_base();
        info->stride = memHandle->stride;
        info->modifier = 0;
        info->size = memHandle->size;
        info->usage = memHandle->producer_usage;
        info->num_planes = memHandle->get_num_planes();
        for (uint32_t i = 0; i < info->num_planes; i++) {
            info->strides[i] = memHandle->plane_info[i].byte_stride;
            info->offsets[i] = memHandle->plane_info[i].offset;
        }
        info->name = nullptr;
        info->phys = 0;
        info->base = 0;
    } else if (gralloc_handle_t(handle)->magic == Memory::sMagic) {
        gralloc_handle_t memHandle = (gralloc_handle_t)handle;
        info->fd = memHandle->fd;
        info->width = memHandle->width;
        info->height = memHandle->height;
        info->format = memHandle->fslFormat;
        info->stride = memHandle->stride;
        info->modifier = memHandle->format_modifier;
        info->size = memHandle->size;
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
