#!/system/bin/sh

# This script locates in vendor partition, it is used to do the OTA postinstall work
# if necessary. the work to do include:
# * update bootloader0 partition which is hardcoded in u-boot

# bootloader0 partition offset of various SoC based on boot device
imx8mm_emmc_bootloader0_offset=33
imx8mm_sd_bootloader0_offset=33
imx8mn_emmc_bootloader0_offset=0
imx8mn_sd_bootloader0_offset=32
imx8mp_emmc_bootloader0_offset=0
imx8mp_sd_bootloader0_offset=32
imx8mq_emmc_bootloader0_offset=33
imx8mq_sd_bootloader0_offset=33

imx8ulp_emmc_bootloader0_offset=0

imx8qm_reva_emmc_bootloader0_offset=33
imx8qm_reva_sd_bootloader0_offset=33
imx8qm_revb_emmc_bootloader0_offset=0
imx8qm_revb_sd_bootloader0_offset=32

imx8qxp_reva_emmc_bootloader0_offset=33
imx8qxp_reva_sd_bootloader0_offset=33
imx8qxp_revb_emmc_bootloader0_offset=32
imx8qxp_revb_sd_bootloader0_offset=32
imx8qxp_revc_emmc_bootloader0_offset=0
imx8qxp_revc_sd_bootloader0_offset=32

bootloader0_img=/postinstall/etc/bootloader0.img
bootloader0_img_size=`wc -c $bootloader0_img | cut -d ' ' -f1`
bootloader0_img_md5sum=`md5sum $bootloader0_img | cut -d ' ' -f1`

log -p i -t imx_ota_postinstall "bootloader0 image md5sum is $bootloader0_img_md5sum, size is $bootloader0_img_size"

soc_type=`getprop ro.boot.soc_type`
soc_rev=`getprop ro.boot.soc_rev`
log -p i -t imx_ota_postinstall "the soc is $soc_type:$soc_rev"

boot_device=`getprop ro.boot.boot_device_root`
# the property ro.boot.boot_device_root may not exist
# search for other properties related to boot device
if [ -z ${boot_device} ]; then
	boot_device_uevent_sub_path=`getprop ro.boot.boot_devices`

	all_boot_device_props=`getprop | grep ro.boot.boot_devices_`
	for boot_device_prop in ${all_boot_device_props}
	do
		if [ "$(echo $boot_device_prop | grep "ro.boot.boot_devices_")" != "" ]; then
			boot_device_prop=${boot_device_prop#*[}
			boot_device_prop=${boot_device_prop%]*}
			boot_device_prop_value=`getprop ${boot_device_prop}`
			if [ "${boot_device_uevent_sub_path}" = "${boot_device_prop_value}" ]; then
				boot_device=${boot_device_prop##*_}
				log -p i -t imx_ota_postinstall "found the boot device: ${boot_device}"
				break
			fi
		fi
	done
fi

# check whether the boot device is eMMC based on whether the device file named
# "/dev/block/$(device_root}boot0" can be accessed or not.
ls /dev/block/${boot_device}boot0 1>/dev/null 2>/dev/null
if [ $? != 0 ]; then
	boot_device_type=sd
	target_device=/dev/block/${boot_device}
else
	boot_device_type=emmc
	target_device=/dev/block/${boot_device}boot0
fi
log -p i -t imx_ota_postinstall "the boot device is $boot_device_type:$boot_device"

if [ -z $soc_rev ]; then
	log -p i -t imx_ota_postinstall "soc_rev info is not provided"
	target_device_offset=(`eval echo \$\{${soc_type}_${boot_device_type}_bootloader0_offset\}`)
else
	target_device_offset=(`eval echo \$\{${soc_type}_${soc_rev}_${boot_device_type}_bootloader0_offset\}`)
fi

if [ ${boot_device_type} = "emmc" ]; then
	emmc_boot0_rw=`cat /sys/block/${boot_device}boot0/force_ro`
	if [ -z $emmc_boot0_rw ]; then
		log -p e -t imx_ota_postinstall "fail to read emmc boot0 partition read-only property"
		exit 1
	fi
	if [ ${emmc_boot0_rw} != 0 ]; then
		echo 0 > /sys/block/${boot_device}boot0/force_ro
		if [ $? != 0 ]; then
			log -p e -t imx_ota_postinstall "fail to set emmc boot0 partition to be writtable"
			exit 1
		fi
	else
		log -p i -t imx_ota_postinstall "emmc boot0 partition is already writtable"
	fi
fi

encryted_boot=`getprop ro.boot.encrypted_boot_ota`

loop_index=0
flash_flag=0
while [ loop_index -lt 2 ]; do
	let loop_index=loop_index+1
	log -p i -t imx_ota_postinstall "to write to $target_device with offset $target_device_offset"
	dd if=$bootloader0_img of=$target_device bs=1k seek=$target_device_offset conv=fsync,notrunc
	if [ $? != 0 ]; then
		log -p e -t imx_ota_postinstall "spl: dd command exit with error"
		continue
	fi
	sync
	echo 3 > /proc/sys/vm/drop_caches
	sync
	read_back_md5sum_spl=`dd if=$target_device bs=1k skip=$target_device_offset iflag=count_bytes count=$bootloader0_img_size 2>/dev/null | md5sum | cut -d ' ' -f1`
	log -p i -t imx_ota_postinstall "spl: read_back_md5sum is $read_back_md5sum_spl"
	if [ "$read_back_md5sum_spl" = "$bootloader0_img_md5sum" ]; then
		flash_flag=1
		break
	fi
done

if [ $encryted_boot = "true" ]; then
	if [ -f "/postinstall/bin/imx_dek_inserter" ]; then
		/postinstall/bin/imx_dek_inserter -s $soc_type -S $target_device_offset -t $target_device -l $bootloader0_img_size
	else
		log -p e -t imx_ota_postinstall "/postinstall/bin/imx_dek_inserter not exist, exit!"
		exit 1
	fi
	if [ $? != 0 ]; then
		log -p e -t imx_ota_postinstall "spl: imx_dek_inserter exit with error"
		flash_flag=0
	fi
fi

if [ ${boot_device_type} = "emmc" ]; then
	echo 1 > /sys/block/${boot_device}boot0/force_ro
fi

if [ $flash_flag != 1 ]; then
	log -p e -t imx_ota_postinstall "fail to update bootloader0 partition, the system may fail to reboot"
	exit 1
else
	log -p i -t imx_ota_postinstall "finished to update bootloader0 partition"
fi

if [ $encryted_boot = "true" ]; then
	bootloader_slot=`getprop ro.boot.slot_suffix`
	if [ ${bootloader_slot} != "_a" && ${bootloader_slot} != "_b" ]; then
		log -p e -t imx_ota_postinstall "fail to get boot slot"
		exit 1
	fi
	[ ${bootloader_slot} = "_a" ] && bootloader_slot="_b" || bootloader_slot="_a"
	bootloader_ab_device=/dev/block/by-name/bootloader${bootloader_slot}
	log -p e -t imx_ota_postinstall "Start to update bootloader, the target path is $bootloader_ab_device"

	bootloader_ab_img=/postinstall/etc/bootloader_ab.img
	bootloader_ab_img_size=`wc -c $bootloader_ab_img | cut -d ' ' -f1`
	bootloader_ab_img_md5sum=`md5sum $bootloader_ab_img | cut -d ' ' -f1`

	log -p i -t imx_ota_postinstall "bootloader_ab image md5sum is $bootloader_ab_img_md5sum, size is $bootloader_ab_img_size"

	log -p i -t imx_ota_postinstall "to write to $bootloader_ab_device with offset 0"
	dd if=$bootloader_ab_img of=$bootloader_ab_device bs=1k seek=0 conv=fsync,notrunc
	if [ $? != 0 ]; then
		log -p e -t imx_ota_postinstall "bootloader: dd command exit with error"
		exit 1
	fi
	sync
	echo 3 > /proc/sys/vm/drop_caches
	sync

	read_back_md5sum_bl=`dd if=$bootloader_ab_device bs=1k skip=0 iflag=count_bytes count=$bootloader_ab_img_size 2>/dev/null | md5sum | cut -d ' ' -f1`
	log -p i -t imx_ota_postinstall "bootloader: read_back_md5sum is $read_back_md5sum_bl"

	if [ "$read_back_md5sum_bl" != "$bootloader_ab_img_md5sum" ]; then
		log -p e -t imx_ota_postinstall "fail to update bootloader_a/b partition, the system may fail to reboot"
		exit 1
	fi

	if [ -f "/postinstall/bin/imx_dek_inserter" ]; then
		/postinstall/bin/imx_dek_inserter -s $soc_type -B -t $bootloader_ab_device -l $bootloader_ab_img_size
		sync
		echo 3 > /proc/sys/vm/drop_caches
		sync
	else
		log -p e -t imx_ota_postinstall "/postinstall/bin/imx_dek_inserter not exist, exit!"
		exit 1
	fi

	if [ $? != 0 ]; then
		log -p e -t imx_ota_postinstall "bootloader: imx_dek_inserter exit with error"
		exit 1
	fi
	log -p i -t imx_ota_postinstall "finished to update bootloader_a/b partition"
fi

exit 0
