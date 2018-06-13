#/*
# * Copyright (C) 2017 The Android Open Source Project
# * Copyright 2017 NXP
# *
# * Licensed under the Apache License, Version 2.0 (the "License");
# * you may not use this file except in compliance with the License.
# * You may obtain a copy of the License at
# *
# *      http://www.apache.org/licenses/LICENSE-2.0
# *
# * Unless required by applicable law or agreed to in writing, software
# * distributed under the License is distributed on an "AS IS" BASIS,
# * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# * See the License for the specific language governing permissions and
# * limitations under the License.
# */
ifeq ($(findstring imx, $(TARGET_BOARD_PLATFORM)), imx)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE := android.hardware.usb@1.1-service.imx
LOCAL_INIT_RC := android.hardware.usb@1.1-service.imx.rc
LOCAL_SRC_FILES := \
    service.cpp \
    Usb.cpp

LOCAL_SHARED_LIBRARIES := \
    libcutils \
    libhidlbase \
    libhidltransport \
    liblog \
    libutils \
    libhardware \
    android.hardware.usb@1.0 \
    android.hardware.usb@1.1

include $(BUILD_EXECUTABLE)
endif
