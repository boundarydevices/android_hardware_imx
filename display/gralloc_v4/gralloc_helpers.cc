/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gralloc_helpers.h"

#include <cutils/log.h>
#include <sync/sync.h>
#include <errno.h>
#include <unistd.h>

#include <MemoryDesc.h>

using namespace fsl;

gralloc_handle_t gralloc_convert_handle(buffer_handle_t handle)
{
	auto hnd = reinterpret_cast<const gralloc_handle_t>(handle);
	if (!hnd || hnd->magic != fsl::Memory::sMagic)
		return nullptr;

	return hnd;
}

int32_t gralloc_sync_wait(int32_t fence, bool close_fence)
{
	if (fence < 0)
		return 0;

	/*
	 * Wait initially for 1000 ms, and then wait indefinitely. The SYNC_IOC_WAIT
	 * documentation states the caller waits indefinitely on the fence if timeout < 0.
	 */
	int err = sync_wait(fence, 1000);
	if (err < 0) {
		ALOGE("%s Timed out on sync wait, err = %s", __func__, strerror(errno));
		err = sync_wait(fence, -1);
		if (err < 0) {
			ALOGE("%s sync wait error = %s", __func__, strerror(errno));
			return -errno;
		}
	}

	if (close_fence) {
		err = close(fence);
		if (err) {
			ALOGE("%s Unable to close fence fd, err = %s", __func__, strerror(errno));
			return -errno;
		}
	}

	return 0;
}

uint32_t drv_convert_nxp_format_to_drm_format(int format)
{
        switch (format) {
        case FORMAT_BLOB: //DRM_FORMAT_R8
                return DRM_FORMAT_R8;
        case FORMAT_YV12://DRM_FORMAT_YVU420_ANDROID
                return DRM_FORMAT_YVU420_ANDROID;
        case FORMAT_NV21://DRM_FORMAT_NV21    ????
                return DRM_FORMAT_NV21;
        case FORMAT_YCBCR_P010:
        case FORMAT_P010://DRM_FORMAT_P010
                return DRM_FORMAT_P010;
        case FORMAT_RGB565://DRM_FORMAT_RGB565
		return DRM_FORMAT_RGB565;
        case FORMAT_YUYV://DRM_FORMAT_YUYV   ????
                return DRM_FORMAT_YUYV;
        case FORMAT_RGB888://DRM_FORMAT_BGR888
                return DRM_FORMAT_BGR888;
        case FORMAT_RGBA8888://DRM_FORMAT_ABGR8888
		return DRM_FORMAT_ABGR8888;
        case FORMAT_RGBX8888://DRM_FORMAT_XBGR8888
		return DRM_FORMAT_XBGR8888;
        case FORMAT_BGRA8888://DRM_FORMAT_ARGB8888
		return DRM_FORMAT_ARGB8888;
        case FORMAT_RGBA1010102://DRM_FORMAT_ABGR2101010
                return DRM_FORMAT_ABGR2101010;
        case FORMAT_RGBAFP16://DRM_FORMAT_ABGR16161616F
                return DRM_FORMAT_ABGR16161616F;
	case FORMAT_NV12:
		return DRM_FORMAT_NV12;
        case FORMAT_NV16:
		return DRM_FORMAT_NV16;//YCBCR_422_SP
        case FORMAT_I420://HAL_PIXEL_FORMAT_YCbCr_420_P
		return DRM_FORMAT_YUV420;
        case FORMAT_RAW16:
            return DRM_FORMAT_RAW16;
        case FORMAT_NV12_TILED:
        case FORMAT_NV12_G1_TILED:
        case FORMAT_NV12_G2_TILED:
        case FORMAT_NV12_G2_TILED_COMPRESSED:
        case FORMAT_P010_TILED:
        case FORMAT_P010_TILED_COMPRESSED:
        default:
                ALOGE("%s UNKNOWN FORMAT %d, cannot convert to DRM fourcc format", __func__, format);
                return 0;
	}
}

int convert_pixel_format_to_gralloc_format(int32_t format)
{
	return static_cast<int>(format);
}

int convert_drm_format_to_nxp_format(uint32_t drm_format)
{
	return static_cast<int>(drm_format);
}

int convert_buffer_usage_to_nxp_usage(int32_t droid_usage)
{
	return static_cast<int>(droid_usage);
}

int convert_bo_use_flages_to_nxp_flags(uint64_t use_flags)
{
	return static_cast<int>(use_flags);
}

uint32_t drv_resolve_format(uint32_t format, uint64_t use_flags)
{
	return format;
}
