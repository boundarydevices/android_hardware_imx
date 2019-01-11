LOCAL_PATH:= $(call my-dir)

ifeq ($(BOARD_HAVE_IMX_EVS),true)
##################################
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
    service.cpp \
    EvsEnumerator.cpp \
    EvsCamera.cpp \
    EvsDisplay.cpp \
    V4l2Capture.cpp \
    FakeCapture.cpp


LOCAL_SHARED_LIBRARIES := \
    android.hardware.automotive.evs@1.0 \
    nxp.hardware.display@1.0 \
    libbase \
    libbinder \
    libcutils \
    libhardware \
    libhidlbase \
    libhidltransport \
    liblog \
    libutils \
    libfsldisplay \
    libsync

LOCAL_C_INCLUDES +=  \
    frameworks/native/include \
    $(IMX_PATH)/imx/display/display

LOCAL_MODULE := android.hardware.automotive.evs@1.0-EvsEnumeratorHw

LOCAL_VENDOR_MODULE := true
LOCAL_MODULE_TAGS := optional
LOCAL_STRIP_MODULE := keep_symbols

LOCAL_CFLAGS += -DLOG_TAG=\"EvsDriver\"
LOCAL_CFLAGS += -DGL_GLEXT_PROTOTYPES -DEGL_EGLEXT_PROTOTYPES
LOCAL_CFLAGS += -Wall -Werror -Wunused -Wunreachable-code
LOCAL_CFLAGS += \
        -DANDROID_SDK_VERSION=$(PLATFORM_SDK_VERSION)

# NOTE:  It can be helpful, while debugging, to disable optimizations
#LOCAL_CFLAGS += -O0 -g

include $(BUILD_EXECUTABLE)
endif
