/*
 * Copyright (C) 2013 Freescale Semiconductor, Inc. All Rights Reserved.
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

#ifndef HWC_DISPLAY_H
#define HWC_DISPLAY_H

int hwc_get_display_fbid(struct hwc_context_t* ctx, int disp_type);
int hwc_get_framebuffer_info(displayInfo *pInfo);
int hwc_get_display_info(struct hwc_context_t* ctx);

#endif
