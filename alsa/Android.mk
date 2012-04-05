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

# This is the Freescale ALSA module for i.MX

ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)

  LOCAL_PATH := $(call my-dir)

  include $(CLEAR_VARS)

  LOCAL_PRELINK_MODULE := true

  LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw

  LOCAL_CFLAGS := -D_POSIX_SOURCE -Wno-multichar

  LOCAL_C_INCLUDES += hardware/alsa_sound external/alsa-lib/include

  LOCAL_SRC_FILES:= alsa_imx.cpp

  LOCAL_SHARED_LIBRARIES := \
	libaudio \
  	libasound \
  	liblog   \
    libcutils

  LOCAL_MODULE:= alsa.freescale

  LOCAL_MODULE_TAGS := eng

ifeq ($(TARGET_BOOTLOADER_BOARD_NAME),SABRELITE)
  LOCAL_CFLAGS += -DBOARD_IS_SABRELITE
endif
  include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)
LOCAL_MODULE := audio.primary.freescale
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SRC_FILES := imx_audio_hal.cpp
LOCAL_SHARED_LIBRARIES := liblog libcutils libutils libhardware
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := audio.sabresd_reva.freescale
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SRC_FILES := tinyalsa_sabresd_reva.c
LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	system/media/audio_utils/include \
	system/media/audio_effects/include
LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa libaudioutils libdl
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)
LOCAL_MODULE := audio.sabresd_revb.freescale
LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_SRC_FILES := tinyalsa_sabresd_revb.c
LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	system/media/audio_utils/include \
	system/media/audio_effects/include
LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa libaudioutils libdl
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

endif
