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

ifeq ($(BOARD_SOC_CLASS),IMX5X)
LOCAL_PATH:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=               \
    CameraHal.cpp

LOCAL_CPPFLAGS += -DUSE_FSL_JPEG_ENC -DDUMP_CAPTURE_YUVxx -DCAPTURE_ONLY_TESTxx

LOCAL_SHARED_LIBRARIES:= \
    libui \
    libutils \
    libcutils \
    libbinder \
    libmedia \
    libdl \
    libc

LOCAL_C_INCLUDES += \
	kernel_imx/include \
	frameworks/base/include/binder \
	frameworks/base/include/ui \
	frameworks/base/camera/libcameraservice

ifeq ($(BOARD_CAMERA_NV12),true)
    LOCAL_CPPFLAGS += -DRECORDING_FORMAT_NV12
else
    LOCAL_CPPFLAGS += -DRECORDING_FORMAT_YUV420
endif

ifeq ($(TARGET_BOARD_PLATFORM), imx51_3stack)
    LOCAL_CPPFLAGS += -DIMX51_3STACK
endif

ifeq ($(HAVE_FSL_IMX_CODEC),true)
LOCAL_SHARED_LIBRARIES += libfsl_jpeg_enc_arm11_elinux
LOCAL_CPPFLAGS += -DUSE_FSL_JPEG_ENC -DDUMP_CAPTURE_YUVxx
ifeq ($(PREBUILT_FSL_IMX_CODEC),true)	
    LOCAL_C_INCLUDES += prebuilt/android-arm/fsl_imx_codec/ghdr	
else
    LOCAL_C_INCLUDES += external/fsl_imx_codec/fsl_mad_multimedia_codec/ghdr	
endif
endif	
	
LOCAL_MODULE:= libcamera

LOCAL_CFLAGS += -fno-short-enums

include $(BUILD_SHARED_LIBRARY)
endif
