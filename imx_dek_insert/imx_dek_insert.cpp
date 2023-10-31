/*
 * Copyright (C) 2020 The Android Open Source Project
 * Copyright 2023 NXP
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>

#include <android/log.h>
#include <android-base/logging.h>

#include <aidl/android/hardware/imx_dek_extractor/IDek_Extractor.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "imx_dek_insert.h"

using namespace android;
using aidl::android::hardware::imx_dek_extractor::IDek_Extractor;

#define SECO_PADING 	(0x400)
#define CONTAINER_TAG 	0x87
#define SIG_BLK_TAG 	0x90
#define IMX_BL_PAYLOAD_SIZE   (96)

static std::shared_ptr<IDek_Extractor> dek_extractor_(nullptr);

static error_t insert_dek_blob_to_fit(image_type_t image_t, char *target_device,
		std::vector<uint8_t> dek_blob, off_t fit_length) {
	int file_fd = -1;
	error_t ret = NO_ERROR;

	file_fd = open(target_device, O_RDWR);
	if (file_fd < 0) {
		printf("file_fd: can not open %s!\r\n", target_device);
		ret = FILE_ERROR;
		goto fail;
	}

	if (image_t == SPL) {
		lseek(file_fd, fit_length, SEEK_SET);
		LOG(INFO) << "The dek blob offset is: " << fit_length;
	} else if (image_t == BOOTLOADER) {
		lseek(file_fd, fit_length - IMX_BL_PAYLOAD_SIZE, SEEK_SET);
		LOG(INFO) << "The dek blob offset is: " << fit_length - IMX_BL_PAYLOAD_SIZE;
	} else {
		printf("Wrong parameter: image type error!\r\n");
		ret = PARAMETER_ERROR;
		goto file_fail;
	}

	write(file_fd, &dek_blob[0], dek_blob.size());

file_fail:
	close(file_fd);
fail:
	return ret;
}

static error_t insert_dek_blob_to_container(image_type_t image_t, char *target_device,
		std::vector<uint8_t> dek_blob, int bootloader0_offset_byte) {
	int file_fd = -1;
	error_t ret = NO_ERROR;
	void *image_header = NULL;
	byte_str_t image_h = {0, 0, 0};
	off_t blob_offset = 0;
	uint16_t sig_blk_offset = 0, signature_blob_offset = 0;

	file_fd = open(target_device, O_RDWR);
	if (file_fd < 0) {
		printf("file_fd: can not open %s!\r\n", target_device);
		ret = FILE_ERROR;
		goto open_fail;
	}

	if (SPL == image_t) {
		pread(file_fd, &image_h, sizeof(byte_str_t), SECO_PADING + bootloader0_offset_byte);
		lseek(file_fd, SECO_PADING + bootloader0_offset_byte, SEEK_SET);
	} else if (BOOTLOADER == image_t) {
		pread(file_fd, &image_h, sizeof(byte_str_t), bootloader0_offset_byte);
	} else {
		ret = IMAGE_ERROR;
		goto image_fail;
	}

	/*
	 * Check the container tag
	 */
	if (CONTAINER_TAG != image_h.tag) {
		ret = IMAGE_ERROR;
		goto image_fail;
	}

	image_header = (void *)malloc(image_h.length);
	if (!image_header) {
		ret = MEMORY_ERROR;
		goto image_fail;
	}

	read(file_fd, image_header, image_h.length);

	sig_blk_offset = ((ahab_container_header_t *)image_header)->signature_block_offset;

	/*
	 * Check the signature block tag
	 */
	if (SIG_BLK_TAG != ((byte_str_t *)((uint8_t *)image_header + sig_blk_offset))->tag) {
		ret = IMAGE_ERROR;
		goto memory_fail;
	}

	signature_blob_offset = ((ahab_container_signature_block_t *)
			((uint8_t *)image_header + sig_blk_offset))->blob_offset;

	blob_offset = (SPL == image_t) ? (SECO_PADING + sig_blk_offset + signature_blob_offset) :
			(sig_blk_offset + signature_blob_offset);

	pwrite(file_fd, &dek_blob[0], dek_blob.size(), blob_offset);

	ret = NO_ERROR;

memory_fail:
	if (image_header)
		free(image_header);
image_fail:
	close(file_fd);
open_fail:
	return ret;
}

static int connectAIDL() {
	int ret = 0;

	auto dek_extractor_name = std::string() + IDek_Extractor::descriptor + "/default";
	LOG(INFO) << "connecting to AIDL" << dek_extractor_name.c_str() << "...";

	if (AServiceManager_isDeclared(dek_extractor_name.c_str())) {
		LOG(INFO) << "waiting for AIDL service" << dek_extractor_name.c_str() << "...";
		auto dek_extractor_Binder = ndk::SpAIBinder(AServiceManager_waitForService(dek_extractor_name.c_str()));
		dek_extractor_ = IDek_Extractor::fromBinder(dek_extractor_Binder);
		if (!dek_extractor_) {
			LOG(ERROR) << "Failed to connect to AIDL" << dek_extractor_name.c_str();
			ret = -1;
		}
	} else {
		LOG(ERROR) << "AIDL " << dek_extractor_name.c_str() << " is not declared!";
		ret = -1;
	}

	return ret;
}

static error_t read_dek_blob(std::vector<uint8_t>* dek_blob, image_type_t image_t) {
	int32_t err = 0;


	if (connectAIDL()){
		LOG(ERROR) << "Failed to connect service";
		return CONNECT_ERROR;
	}
	if (!dek_extractor_) {
		LOG(ERROR) << "dek_extractor AIDL service is not initialized!";
		return CONNECT_ERROR;
	} else {
		auto status = dek_extractor_->Dek_ExtractorInit(dek_blob, (int)image_t, &err);
		if ((status.getExceptionCode() != EX_NONE) || (err < 0)) {
			LOG(ERROR) << "AIDL call Dek_ExtractorInit failed!";
			return CONNECT_ERROR;
		}
	}

	return NO_ERROR;
}

static error_t insert_dek_blob(soc_type_t soc, std::vector<uint8_t> dek_blob, image_type_t image_t,
		char *target_device, int bootloader0_offset_byte, off_t file_length) {
	int ret = NO_ERROR;

	switch(soc)
	{
		case MM:
		case MN:
		case MP:
		case MQ:
			ret = insert_dek_blob_to_fit(image_t, target_device, dek_blob, file_length);
			if (ret) {
				printf("Failed to insert dek_blob to fit, exit! %d\r\n", ret);
				return OTHER_ERROR;
			}
			break;
		case QM:
		case QX:
		case ULP:
		case IMX9:
			ret = insert_dek_blob_to_container(image_t, target_device, dek_blob, bootloader0_offset_byte);
			if (ret) {
				printf("Failed to insert dek_blob to container, exit! %d\r\n", ret);
				return OTHER_ERROR;
			}
			break;
		default:
			printf("soc not found\n");
			return PARAMETER_ERROR;
	}

	return NO_ERROR;
}

int main(int argc, char** argv) {
	std::vector<uint8_t> dek_blob;

	static struct option long_options[] =
	{
		{"soc", required_argument, NULL, 's'},
		{"target", required_argument, NULL, 't'},
		{"bootloader", no_argument, NULL, 'B'},
		{"spl", required_argument, NULL, 'S'},
		{"length", required_argument, NULL, 'l'},
		{NULL, 0, NULL, 0}
	};

	const char *optstring = "s:t:BS:l:";
	int option_index = 0;
	int bootloader0_offset_k = 0, bootloader0_offset_b = 0;
	soc_type_t soc = SOC_NONE;
	image_type_t image_t = IMAGE_NONE;
	int c;
	int ret = NO_ERROR;
	char target_device[50] = {'\0'};
	off_t file_length = 0;

	while ((c = getopt_long(argc, argv, optstring, long_options, &option_index)) != -1) {
		if (c == -1)
			break;
		switch(c)
		{
			case 's':
				if(!strncmp(optarg, "imx8mm", 6))
					soc = MM;
				else if(!strncmp(optarg, "imx8mn", 6))
					soc = MN;
				else if(!strncmp(optarg, "imx8mp", 6))
					soc = MP;
				else if(!strncmp(optarg, "imx8mq", 6))
					soc = MQ;
				else if(!strncmp(optarg, "imx8qm", 6))
					soc = QM;
				else if(!strncmp(optarg, "imx8qxp", 7))
					soc = QX;
				else if(!strncmp(optarg, "imx8ulp", 7))
					soc = ULP;
				else if(!strncmp(optarg, "imx93", 5))
					soc = IMX9;
				else {
					printf("unrecognized SOC: %s \n",optarg);
				}
				break;
			case 't':
				memcpy(target_device, optarg, strlen(optarg));
				break;
			case 'S':
				image_t = SPL;
				bootloader0_offset_k= atoi(optarg);
				bootloader0_offset_b = bootloader0_offset_k * 1024;
				break;
			case 'B':
				image_t = BOOTLOADER;
				break;
			case 'l':
				file_length = (off_t)atoi(optarg);
				break;
		}
	}

	ret = read_dek_blob(&dek_blob, image_t);
	if (ret != NO_ERROR)
		return EXIT_FAILURE;

	ret = insert_dek_blob(soc, dek_blob, image_t, target_device, bootloader0_offset_b, file_length);
	if (ret != NO_ERROR)
		return EXIT_FAILURE;

	printf("Done.\n");

	return ret == NO_ERROR ? EXIT_SUCCESS : EXIT_FAILURE;
}
