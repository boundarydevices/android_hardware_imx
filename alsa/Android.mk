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

# This is the Freescale ALSA module for i.MX.

ifeq ($(strip $(BOARD_USES_ALSA_AUDIO)),true)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := tinyalsa_hal.c
LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	system/media/audio_utils/include \
	system/media/audio_effects/include
LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa libaudioutils libdl
LOCAL_MODULE_TAGS := optional

ifeq ($(BOARD_HAS_SGTL5000),true)
  $(warning board has SGTL5000)
  LOCAL_CFLAGS += -DBOARD_HAS_SGTL5000
endif

include $(BUILD_SHARED_LIBRARY)

endif


ifeq ($(findstring imx, $(soc_name)), imx)

LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := audio.primary.$(soc_name)
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_SRC_FILES := tinyalsa_hal.c control.c
LOCAL_C_INCLUDES += \
	external/tinyalsa/include \
	system/media/audio_utils/include \
	system/media/audio_effects/include
LOCAL_SHARED_LIBRARIES := liblog libcutils libtinyalsa libaudioutils libdl
LOCAL_MODULE_TAGS := optional
LOCAL_CFLAGS += -DBRILLO
include $(BUILD_SHARED_LIBRARY)

endif
