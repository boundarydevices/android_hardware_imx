atheros_dirs := compat-wireless
ifeq ($(BOARD_WLAN_DEVICE),$(filter $(BOARD_WLAN_DEVICE),ar6003 UNITE))
    include $(call all-subdir-makefiles,$(atheros_dirs))
endif
