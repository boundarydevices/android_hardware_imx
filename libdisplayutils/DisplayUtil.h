/*
 * Copyright 2020 The Chromium OS Authors. All rights reserved.
 * Copyright 2021-2022 NXP.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
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
