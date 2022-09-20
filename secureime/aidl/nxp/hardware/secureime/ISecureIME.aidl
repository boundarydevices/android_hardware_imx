/*
 * Copyright (C) 2020 The Android Open Source Project
 *
 * Copyright 2022 NXP
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

package nxp.hardware.secureime;

import android.os.ParcelFileDescriptor;
/**
 */
@VintfStability
interface ISecureIME {
    int SecureIMEInit(in ParcelFileDescriptor fd, in int buffer_size, in int stride, in int width, in int height);
    int SecureIMEHandleTouch(in int x, in int y);
    int SecureIMEExit();
}
