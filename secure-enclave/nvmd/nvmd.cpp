/*
 * Copyright 2024 NXP
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

#define LOG_TAG "nvmd"

#include "nvmd.h"

#include <EleOperation.h>
#include <signal.h>

static const char ele_nvmd_path[] = "/data/vendor/ele/";
static const char ele_nvm_master_name[] = "/data/vendor/ele/master";
static bool daemon_done = false;

void daemon_exit(int i) {
    (void)i;

    /* set the exit flag */
    ALOGI("Get NVM daemon termination signal.");
    daemon_done = true;
}

static int nvm_manager() {
    struct nvm_context nvm_ctx;
    int ret = GENERAL_SUCCESS;

    /* Open ELE and storage session */
    EleOperation ops(MU_CHANNEL_PLAT_HSM_NVM);
    if (ops.eleOpenDeviceNode() != ELE_NO_ERROR) {
        ALOGE("Failed to open ELE device node!");
        return GENERAL_FAILURE;
    }

    if (ops.eleOpenSession() != ELE_NO_ERROR) {
        ALOGE("Failed to open ELE session!");
        return GENERAL_FAILURE;
    }

    /* Init the context */
    nvm_ctx.prev_command = STORAGE_NVM_LAST_CMD;
    nvm_ctx.next_command = STORAGE_NVM_LAST_CMD;
    nvm_ctx.last_data = nullptr;
    nvm_ctx.nvm_master_name = ele_nvm_master_name;
    nvm_ctx.nvm_chunk_path = ele_nvmd_path;

    do {
        if (ops.eleOpenStorage(&nvm_ctx.nvm_handle) != ELE_NO_ERROR) {
            ALOGE("Failed to open ELE storage!");
            ret = GENERAL_FAILURE;
            break;
        }

        /* Import the storage master */
        if (ops.eleNvmMasterImport(&nvm_ctx) != ELE_NO_ERROR) {
            ret = GENERAL_FAILURE;
            break;
        }

        /* Loop to handle all requests from ELE */
        while (true && !daemon_done) {
            /* Main loop */
            /*TODO handle exit and retry */
            ops.eleHandleNVMRequest(&nvm_ctx);
        }
    } while (false);

    /* Clean up */
    ALOGI("NVM daemon exit...");
    if (nvm_ctx.nvm_handle != 0 && ops.eleCloseStorage(nvm_ctx.nvm_handle) != ELE_NO_ERROR) {
        ALOGE("Failed to close NVM storage!");
    }
    if (ops.eleCloseSession() != ELE_NO_ERROR) {
        ALOGE("Failed to close ELE session!");
    }

    return ret;
}

int main(/*int argc, char *argv[]*/) {
    struct sigaction action = {};

    /* Register exit handler */
    action.sa_handler = daemon_exit;
    if (sigaction(SIGTERM, &action, NULL)) {
        ALOGE("Failed to register kill signal handler!");
    }
    if (sigaction(SIGINT, &action, NULL)) {
        ALOGE("Failed to register ctrl-c signal handler!");
    }

    return nvm_manager();
}
