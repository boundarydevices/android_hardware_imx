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

#include <errno.h>
#include <hardware/memtrack.h>

//#define LOG_NDEBUG 0
#include <utils/Log.h>

int memtrack_init(const struct memtrack_module *module)
{
    if(!module)
        return -1;
    return 0;
}

int memtrack_get_memory(const struct memtrack_module *module,
                              pid_t pid,
                              int type,
                              struct memtrack_record *records,
                              size_t *num_records)
{
    if(!module)
        return -1;

    ALOGV("memtrack_get_memory: pid(%d), type(%d) records (%x), &num_records(%x)",
          pid, type, (unsigned int)records, (unsigned int)num_records);

    return -EINVAL;
}

static struct hw_module_methods_t memtrack_module_methods = {
    .open = NULL,
};

struct memtrack_module HAL_MODULE_INFO_SYM = {
    .common = {
        .tag = HARDWARE_MODULE_TAG,
        .module_api_version = MEMTRACK_MODULE_API_VERSION_0_1,
        .hal_api_version = HARDWARE_HAL_API_VERSION,
        .id = MEMTRACK_HARDWARE_MODULE_ID,
        .name = "Freescale i.MX memtrack HAL",
        .author = "Freescale Semiconductor, Inc.",
        .methods = &memtrack_module_methods,
    },

    .init = memtrack_init,
    .getMemory = memtrack_get_memory,
};
