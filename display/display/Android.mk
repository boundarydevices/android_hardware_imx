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

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false

LOCAL_SRC_FILES := Display.cpp \
                   DisplayManager.cpp \
                   FbDisplay.cpp \
                   KmsDisplay.cpp \
                   VirtualDisplay.cpp \
                   Layer.cpp \
                   Memory.cpp \
                   MemoryDesc.cpp \
                   MemoryManager.cpp \
                   IonManager.cpp \
                   Composer.cpp

LOCAL_C_INCLUDES += device/fsl-proprietary/include \
                    hardware/imx/include \
                    frameworks/native/libs/nativewindow/include  \
                    external/libdrm \
                    external/libdrm/include/drm

LOCAL_SHARED_LIBRARIES :=   \
    liblog                  \
    libcutils               \
    libutils                \
    libui                   \
    libhardware             \
    libhardware_legacy      \
    libsync                 \
    libion                  \
    libdrm

LOCAL_VENDOR_MODULE := true
LOCAL_MODULE := libfsldisplay
LOCAL_CFLAGS:= -DLOG_TAG=\"display\" -D_LINUX -Wunused-parameter

ifneq ($(NUM_FRAMEBUFFER_SURFACE_BUFFERS),)
  LOCAL_CFLAGS += -DNUM_FRAMEBUFFER_SURFACE_BUFFERS=$(NUM_FRAMEBUFFER_SURFACE_BUFFERS)
endif

LOCAL_MODULE_TAGS := optional

include $(BUILD_SHARED_LIBRARY)
