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

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false

LOCAL_SRC_FILES := opencl-2d.cpp

LOCAL_C_INCLUDES += \
                    $(FSL_PROPRIETARY_PATH)/fsl-proprietary/include \
                    system/core/include

LOCAL_SHARED_LIBRARIES := liblog \
                          libcutils \
                          libion \
                          libOpenCL

LOCAL_CFLAGS += -DBUILD_FOR_ANDROID -DUSE_CL_SOURCECODE

LOCAL_VENDOR_MODULE := true
LOCAL_MODULE := libg2d-opencl

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := 2d-test
LOCAL_MULTILIB := both
LOCAL_MODULE_STEM_32 := 2d-test_32
LOCAL_MODULE_STEM_64 := 2d-test_64
LOCAL_SRC_FILES := 2d-test.cpp

LOCAL_VENDOR_MODULE := true
LOCAL_C_INCLUDES += \
                    system/memory/libion/include \
                    system/memory/libion/kernel-headers/linux \
                    system/memory/libion \
                    $(FSL_PROPRIETARY_PATH)/fsl-proprietary/include \
                    vendor/nxp-opensource/imx/display/display \
                    external/libyuv/files/include

LOCAL_C_INCLUDES += \
                    $(FSL_PROPRIETARY_PATH)/fsl-proprietary/include/VX

LOCAL_SHARED_LIBRARIES := liblog \
                          libcutils \
                          libion \
                          libOpenCL \
                          libOpenVX \
                          libOpenVXU \
                          libg2d-opencl \
                          libutils \
                          libfsldisplay \
                          libyuv

LOCAL_CFLAGS += -DBUILD_FOR_ANDROID
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)
endif
