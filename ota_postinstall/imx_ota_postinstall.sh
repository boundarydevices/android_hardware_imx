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


loop_index=0
flash_flag=0
while [ loop_index -lt 2 ]; do
	let loop_index=loop_index+1
	log -p i -t imx_ota_postinstall "to write to $target_device with offset $target_device_offset"
	dd if=$bootloader0_img of=$target_device bs=1k seek=$target_device_offset conv=fsync,notrunc
	if [ $? != 0 ]; then
		log -p e -t imx_ota_postinstall "dd command exit with error"
		continue
	fi
	sync
	echo 3 > /proc/sys/vm/drop_caches
	sync
	read_back_md5sum=`dd if=$target_device bs=1k skip=$target_device_offset iflag=count_bytes count=$bootloader0_img_size 2>/dev/null | md5sum | cut -d ' ' -f1`
	log -p i -t imx_ota_postinstall "read_back_md5sum is $read_back_md5sum"
	if [ "$read_back_md5sum" = "$bootloader0_img_md5sum" ]; then
		flash_flag=1
		break
	fi
done

if [ ${boot_device_type} = "emmc" ]; then
	echo 1 > /sys/block/${boot_device}boot0/force_ro
fi

if [ $flash_flag != 1 ]; then
	log -p e -t imx_ota_postinstall "fail to update bootloader0 partition, the system may fail to reboot"
	exit 1
else
	log -p i -t imx_ota_postinstall "finished to update bootloader0 partition"
	exit 0
fi
