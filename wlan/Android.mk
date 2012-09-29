atheros_dirs := compat-wireless
ifeq ($(BOARD_HAS_ATH_WLAN),true)
    include $(call all-subdir-makefiles,$(atheros_dirs))
endif
