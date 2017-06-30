/*
 * Copyright 2017 NXP
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

enum {
    /* buffer will be used as an OpenGL ES render target */
    USAGE_HW_RENDER   = 0x00000200,
    /* buffer will be used by the 2D hardware blitter */
    USAGE_HW_2D       = 0x00000400,
    /* buffer will be used by the HWComposer HAL module */
    USAGE_HW_COMPOSER = 0x00000800,
    /* buffer will be used with the HW video encoder */
    USAGE_HW_VIDEO_ENCODER = 0x00010000,
};

enum {
    FLAGS_ALLOCATION_ION = 0x00000010,
    FLAGS_ALLOCATION_GPU = 0x00000020,
    FLAGS_WRAP_GPU       = 0x00000040,
    FLAGS_CAMERA         = 0x00100000,
    FLAGS_VIDEO          = 0x00200000,
    FLAGS_UI             = 0x00400000,
    FLAGS_CPU            = 0x00800000,
    FLAGS_FRAMEBUFFER    = 0x10000000,
};

struct MemoryDesc;

struct Memory : public native_handle
{
    static inline int sNumInts() {
        return (((sizeof(Memory) - sizeof(native_handle_t))/sizeof(int)) - sNumFds);
    }
    static const int sNumFds = 1;
    static const int sMagic = 0x3141592;

    Memory(MemoryDesc* desc, int fd);
    ~Memory();
    bool isValid();

    int  fd;
    int  magic;
    int  flags;
    int  size;
    int  offset;
    uint64_t base __attribute__((aligned(8)));
    uint64_t phys __attribute__((aligned(8)));
    int  format;
    int  width;
    int  height;
    int  pid;

    int  usage;
    int  stride;
    int  gemHandle;
    int  fbId;
    int  fslFormat;
    int  reserve;

    /* gpu private ints. */
    uint64_t gpu_priv[12];
    uint64_t shadow;
    uint64_t reserved[3];
};

}
#endif /* GRALLOC_PRIV_H_ */
