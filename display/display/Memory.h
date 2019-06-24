/*
 * Copyright 2017-2019 NXP
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

#ifndef FSL_MEMORY_H_
#define FSL_MEMORY_H_

#include <stdint.h>
#include <cutils/native_handle.h>

namespace fsl {

#define  ALIGN_PIXEL_2(x)  ((x+ 1) & ~1)
#define  ALIGN_PIXEL_4(x)  ((x+ 3) & ~3)
#define  ALIGN_PIXEL_8(x)  ((x+ 7) & ~7)
#define  ALIGN_PIXEL_16(x)  ((x+ 15) & ~15)
#define  ALIGN_PIXEL_32(x)  ((x+ 31) & ~31)
#define  ALIGN_PIXEL_64(x)  ((x+ 63) & ~63)
#define  ALIGN_PIXEL_256(x)  ((x+ 255) & ~255)

enum {
    /* below usage come from gralloc.h*/
    /* buffer is often read in software */
    USAGE_SW_READ_OFTEN = 0x00000003,
    /* buffer is often written in software */
    USAGE_SW_WRITE_OFTEN = 0x00000030,
    /* buffer will be used as an OpenGL ES texture */
    USAGE_HW_TEXTURE = 0x00000100,
    /* buffer will be used as an OpenGL ES render target */
    USAGE_HW_RENDER   = 0x00000200,
    /* buffer will be used by the 2D hardware blitter */
    USAGE_HW_2D       = 0x00000400,
    /* buffer will be used by the HWComposer HAL module */
    USAGE_HW_COMPOSER = 0x00000800,
    /* secure buffer flag.
     * reference to hardware/libhardware/include/hardware/gralloc.h*/
    USAGE_PROTECTED   = 0x00004000,
    /* buffer will be used with the HW video encoder */
    USAGE_HW_VIDEO_ENCODER = 0x00010000,
    USAGE_GPU_TILED_VIV = 0x10000000,
    USAGE_GPU_TS_VIV = 0x20000000,

    /* buffer size of hantro decoder is not to yuv pixel size, it need to
    * pad some bytes for vpu usage, so add this flag */
    USAGE_PADDING_BUFFER = 0x40000000,
};

enum {
    FLAGS_FRAMEBUFFER    = 0x00000001,
    FLAGS_DIMBUFFER      = 0x00000002,
    FLAGS_ALLOCATION_ION = 0x00000010,
    FLAGS_ALLOCATION_GPU = 0x00000020,
    FLAGS_WRAP_GPU       = 0x00000040,
    FLAGS_CAMERA         = 0x00100000,
    FLAGS_VIDEO          = 0x00200000,
    FLAGS_UI             = 0x00400000,
    FLAGS_CPU            = 0x00800000,
    FLAGS_META_CHANGED   = 0x01000000,
    FLAGS_HDR10_VIDEO    = 0x02000000,
    FLAGS_DOLBY_VIDEO    = 0x04000000,
    FLAGS_COMPRESSED_OFFSET = 0x08000000,
    FLAGS_SECURE         = 0x10000000,
};

struct MemoryDesc;

struct Memory : public native_handle
{
    static inline int sNumInts() {
        return (((sizeof(Memory) - sizeof(native_handle_t))/sizeof(int)) - sNumFds);
    }
    static const int sNumFds = 2;
    static const int sMagic = 0x3141592;

    Memory(MemoryDesc* desc, int fd, int fd2);
    ~Memory();
    bool isValid();

    int  fd;
    int  fd_meta;
    int  magic;
    int  flags;
    int  size;
    int  offset;
    uint64_t base __attribute__((aligned(8)));
    uint64_t phys __attribute__((aligned(8)));
    int width;
    int height;
    int format;
    int stride;
    int usage;
    int pid;

    int fslFormat;
    int kmsFd;
    uint32_t fbHandle;
    uint32_t fbId;
    uint64_t fsl_reserved[1] __attribute__((aligned(8)));
    uint64_t surface;

    /* pointer to viv private. */
    uint64_t viv_reserved[4] __attribute__((aligned(8)));
};

}
#endif /* GRALLOC_PRIV_H_ */
