/*
 * Copyright 2021 NXP
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

#ifndef _NXP_DISPLAY_UTIL_H_
#define _NXP_DISPLAY_UTIL_H_

#include <string>

namespace fsl {

int convert_gralloc_format_to_nxp_format(int format);
std::string getNxpFormatString(int format);
std::string getGrallocFormatString(int format);
std::string getUsageString(uint64_t bufferUsage);
}
#endif //_NXP_DISPLAY_UTIL_H_
