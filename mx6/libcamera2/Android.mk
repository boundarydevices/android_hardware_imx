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

ifeq ($(BOARD_SOC_CLASS),IMX6)
LOCAL_PATH:= $(call my-dir)

ifeq ($(BOARD_HAVE_IMX_CAMERA),true)

include $(CLEAR_VARS)

LOCAL_SRC_FILES:=    \
    CameraHal.cpp    \
    CameraModule.cpp \
    CameraUtil.cpp \
    DeviceAdapter.cpp \
    RequestManager.cpp \
    StreamAdapter.cpp \
    PreviewStream.cpp \
    CaptureStream.cpp \
    JpegBuilder.cpp \
    MetadaManager.cpp \
    messageQueue.cpp \
    OvDevice.cpp \
    Ov5640Mipi.cpp \
    Ov5642Csi.cpp \
    Ov5640Csi.cpp \
    TVINDevice.cpp \
    UvcDevice.cpp \
    PhysMemAdapter.cpp \
    YuvToJpegEncoder.cpp \
    NV12_resize.c

LOCAL_CPPFLAGS +=

LOCAL_SHARED_LIBRARIES:= \
    libcamera_client \
    libui \
    libutils \
    libcutils \
    libbinder \
    libmedia \
    libhardware_legacy \
    libdl \
    libc \
    libjpeg \
    libexif \
    libion \
    libcamera_metadata \
    libg2d

LOCAL_C_INCLUDES += \
	frameworks/base/include/binder \
	frameworks/base/include/ui \
	frameworks/base/camera/libcameraservice \
	hardware/imx/mx6/libgralloc_wrapper \
	system/media/camera/include \
	external/jpeg \
	external/jhead \
        device/fsl-proprietary/include

ifeq ($(HAVE_FSL_IMX_CODEC),true)
    #LOCAL_SHARED_LIBRARIES += libfsl_jpeg_enc_arm11_elinux
    #LOCAL_CPPFLAGS += -DUSE_FSL_JPEG_ENC
    #LOCAL_C_INCLUDES += device/fsl-proprietary/codec/ghdr
endif
ifeq ($(BOARD_CAMERA_NV12),true)
    LOCAL_CPPFLAGS += -DRECORDING_FORMAT_NV12
else
    LOCAL_CPPFLAGS += -DRECORDING_FORMAT_YUV420
endif

#Define this for switch the Camera through V4L2 MXC IOCTL
#LOCAL_CPPFLAGS += -DV4L2_CAMERA_SWITCH

LOCAL_CPPFLAGS += -Werror

LOCAL_MODULE_PATH := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE:= camera.$(TARGET_BOARD_PLATFORM)

LOCAL_CFLAGS += -fno-short-enums
LOCAL_PRELINK_MODULE := false
LOCAL_MODULE_TAGS := eng

include $(BUILD_SHARED_LIBRARY)
endif

endif
