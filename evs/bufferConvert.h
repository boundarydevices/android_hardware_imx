
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

#ifndef ANDROID_HARDWARE_AUTOMOTIVE_EVS_V1_0_BUFFERCONVERT_H
#define ANDROID_HARDWARE_AUTOMOTIVE_EVS_V1_0_BUFFERCONVERT_H

void cl_YUYVCopyByLine(void *g2dHandle,
         uint8_t *output, int dstWidth,
         int dstHeight, uint8_t *input,
         int srcWidth, int srcHeight, bool bInputCached);
#endif  // ANDROID_HARDWARE_AUTOMOTIVE_EVS_V1_0_BUFFERCONVERT_H
