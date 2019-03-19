LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_SRC_FILES := evs_service.c

LOCAL_SHARED_LIBRARIES := libcutils liblog
LOCAL_INIT_RC := android.automotive.evs.vehicle_state.rc

LOCAL_VENDOR_MODULE := true
LOCAL_MODULE := evs_service
LOCAL_MODULE_TAGS := optional
include $(BUILD_EXECUTABLE)
