# Copyright (C) 2008 The Android Open Source Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

ifeq ($(findstring imx, $(TARGET_BOARD_PLATFORM)), imx)

LOCAL_PATH := $(call my-dir)

ifeq ($(TARGET_HWCOMPOSER_VERSION),v2.0)
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SHARED_LIBRARIES :=	\
	liblog			\
	libcutils		\
	libutils		\
	libui			\
	libhardware		\
	libhardware_legacy	\
	libbinder		\
	libsync                 \
	libfsldisplay

LOCAL_SRC_FILES := hwcomposer.cpp

LOCAL_VENDOR_MODULE := true
LOCAL_MODULE := hwcomposer.$(TARGET_BOARD_PLATFORM)
LOCAL_C_INCLUDES += $(IMX_PATH)/imx/display/display   \
                    $(FSL_PROPRIETARY_PATH)/fsl-proprietary/include \
                    $(IMX_PATH)/imx/libedid  \
                    system/core/include/

LOCAL_CFLAGS:= -DLOG_TAG=\"hwcomposer\"

LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
endif
endif
