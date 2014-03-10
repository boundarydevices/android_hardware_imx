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

ifneq ($(PREBUILT_FSL_HWCOMPOSER),true)
LOCAL_PATH := $(call my-dir)

# HAL module implemenation, not prelinked and stored in
# hw/<OVERLAY_HARDWARE_MODULE_ID>.<ro.product.board>.so
include $(CLEAR_VARS)
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SHARED_LIBRARIES :=			\
	liblog					\
	libEGL					\
	libcutils				\
	libutils				\
	libui					\
	libhardware				\
	libhardware_legacy			\
	libbinder \
	libg2d \
	libsync

LOCAL_SRC_FILES :=				\
	hwcomposer.cpp				\
	hwc_composite.cpp

LOCAL_MODULE := hwcomposer_fsl.$(TARGET_BOARD_PLATFORM)
LOCAL_C_INCLUDES += hardware/imx/mx6/libgralloc_wrapper \
                    hardware/imx/mx6/hwcomposer \
                    device/fsl-proprietary/include \
                    system/core/include/

LOCAL_CFLAGS:= -DLOG_TAG=\"hwcomposer\"
LOCAL_CFLAGS += -DENABLE_VSYNC

ifneq ($(NUM_FRAMEBUFFER_SURFACE_BUFFERS),)
  LOCAL_CFLAGS += -DNUM_FRAMEBUFFER_SURFACE_BUFFERS=$(NUM_FRAMEBUFFER_SURFACE_BUFFERS)
endif

LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
endif
