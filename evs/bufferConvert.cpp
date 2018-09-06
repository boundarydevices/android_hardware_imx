
/*
 * Copyright (C) 2016 The Android Open Source Project
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

#include <stdint.h>
#include "opencl-2d.h"

void cl_YUYVCopyByLine(void *g2dHandle,
         uint8_t *output, int dstWidth,
         int dstHeight, uint8_t *input,
         int srcWidth, int srcHeight, bool bInputCached)
{
    struct cl_g2d_surface src,dst;

    src.format = CL_G2D_YUYV;
    if(bInputCached){
        //Input is buffer from usb v4l2 driver
        //cachable buffer
        src.usage = CL_G2D_CPU_MEMORY;
    }
    else
        src.usage = CL_G2D_DEVICE_MEMORY;

    src.planes[0] = (long)input;
    src.left = 0;
    src.top = 0;
    src.right  = srcWidth;
    src.bottom = srcHeight;
    src.stride = srcWidth;
    src.width  = srcWidth;
    src.height = srcHeight;

    dst.format = CL_G2D_YUYV;
    dst.usage = CL_G2D_DEVICE_MEMORY;
    dst.planes[0] = (long)output;
    dst.left = 0;
    dst.top = 0;
    dst.right  = dstWidth;
    dst.bottom = dstHeight;
    dst.stride = dstWidth;
    dst.width  = dstWidth;
    dst.height = dstHeight;

    cl_g2d_blit(g2dHandle, &src, &dst);
    cl_g2d_flush(g2dHandle);
    cl_g2d_finish(g2dHandle);
}
