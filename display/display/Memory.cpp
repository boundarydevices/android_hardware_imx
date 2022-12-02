/*
 * Copyright 2017-2022 NXP.
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

#include <string.h>
#include "Memory.h"
#include "MemoryDesc.h"

namespace fsl {

Memory::Memory(MemoryDesc* desc, int fd, int fd2)
  : fd(dup(fd)), fd_meta(fd2), magic(sMagic), flags(desc->mFlag),
    size(desc->mSize), offset(0), base(0),  phys(0),
    width(desc->mWidth), height(desc->mHeight),
    format(desc->mFormat), stride(desc->mStride),
    usage(desc->mProduceUsage), pid(getpid()),
    fslFormat(desc->mFslFormat), kmsFd(-1),
    fbHandle(0), fbId(0), surface(0)
{
    version = sizeof(native_handle);
    numInts = sNumInts();
    numFds = sNumFds;
#if GRALLOC_VERSION == 4
    fd_region = -1;
    id = 0;
    num_planes = 0;
    format_modifier = 0;
    reserved_region_size = 0;
    total_size = 0;
    memset(fd_reserved, 0, sizeof(fd_reserved));
    memset(strides, 0, sizeof(strides));
    memset(offsets, 0, sizeof(offsets));
    memset(sizes, 0, sizeof(sizes));
    memset(name, 0, sizeof(name));
    memset(nxp_reserved, 0, sizeof(nxp_reserved));
#endif
    memset(viv_reserved, 0, sizeof(viv_reserved));
}

Memory::~Memory()
{
    magic = 0;
    if (fd_meta > 0) {
        close(fd_meta);
    }

    if (fd > 0) {
        close(fd);
    }
#if GRALLOC_VERSION == 4
    if (fd_region > 0) {
        close(fd_region);
    }
#endif
}

bool Memory::isValid()
{
    return (magic == sMagic);
}

}
