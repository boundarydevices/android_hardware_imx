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

LOCAL_SRC_FILES := IonAllocator.cpp

LOCAL_C_INCLUDES += $(IMX_PATH)/imx/include \
                    system/core/libion/include \
                    system/core/libion/kernel-headers \
                    system/core/libion \
                    system/core/include

LOCAL_CFLAGS:= -DLOG_TAG=\"ionalloc\"

ifeq ($(CFG_SECURE_DATA_PATH), y)
    LOCAL_CPPFLAGS += -DCFG_SECURE_DATA_PATH
endif

LOCAL_VENDOR_MODULE := true
LOCAL_MODULE := libionallocator
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false

LOCAL_SRC_FILES := Display.cpp \
                   DisplayHal.cpp \
                   DisplayManager.cpp \
                   FbDisplay.cpp \
                   KmsDisplay.cpp \
                   VirtualDisplay.cpp \
                   Layer.cpp \
                   Memory.cpp \
                   MemoryDesc.cpp \
                   MemoryManager.cpp \
                   IonManager.cpp \
                   Composer.cpp \
                   android/Rect.cpp \
                   android/Region.cpp \
                   android/uevent.cpp

LOCAL_C_INCLUDES += $(FSL_PROPRIETARY_PATH)/fsl-proprietary/include \
                    $(IMX_PATH)/imx/include \
                    $(IMX_PATH)/imx/libedid \
                    $(IMX_PATH)/libdrm-imx \
                    $(IMX_PATH)/libdrm-imx/include/drm \
                    system/core/libion \
                    frameworks/native/include \
                    hardware/libhardware_legacy/include

LOCAL_SHARED_LIBRARIES :=   \
    liblog                  \
    libcutils               \
    libutils                \
    libhardware             \
    libsync                 \
    libion                  \
    libedid                 \
    nxp.hardware.display@1.0 \
    libhidlbase \
    libhidltransport \
    libdrm_android

LOCAL_WHOLE_STATIC_LIBRARIES := libionallocator
LOCAL_VENDOR_MODULE := true
LOCAL_MODULE := libfsldisplay
LOCAL_CFLAGS:= -DLOG_TAG=\"display\" -D_LINUX -Wunused-parameter

ifneq ($(NUM_FRAMEBUFFER_SURFACE_BUFFERS),)
  LOCAL_CFLAGS += -DNUM_FRAMEBUFFER_SURFACE_BUFFERS=$(NUM_FRAMEBUFFER_SURFACE_BUFFERS)
endif

# DRM DPU not support tile buffer downscale, Cactus player video play is not full screen
# need do downscale, workaround by set source width and height the same as DPU Crtc then
# it will work as crop instead of downscale, and source width and height need 2 pixel
# align in DPU plane check.
ifeq ($(BOARD_SOC_TYPE), IMX8Q)
    LOCAL_CPPFLAGS += -DWORKAROUND_DOWNSCALE_LIMITATION
endif

ifeq ($(BOARD_SOC_CLASS), IMX8)
    LOCAL_CFLAGS += -DIMX8
endif

ifeq ($(BOARD_SOC_TYPE), IMX8MQ)
    LOCAL_CFLAGS += -DFRAMEBUFFER_COMPRESSION
endif

ifneq ($(HAVE_FSL_IMX_GPU3D),true)
LOCAL_CFLAGS += -DUSE_SW_OPENGL
endif

ifeq ($(CFG_SECURE_DATA_PATH), y)
    LOCAL_CPPFLAGS += -DCFG_SECURE_DATA_PATH
endif

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
endif
