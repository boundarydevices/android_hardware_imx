/*
 * Copyright 2024 NXP
 *
 */
#ifndef __ELE_MESSAGE_H__
#define __ELE_MESSAGE_H__

#include <stdint.h>

#define ELE_MU_MSG_SIZE (17)
#define NVM_HEADER_SIZE (sizeof(struct nvm_header))
/* Max number of words without the requirement of CRC */
#define STORAGE_NB_WORDS_MAX_NO_CRC (4u)
/* Max number of expected data size */
#define STORAGE_MAX_DATA_SIZE (16 * 1024)
/* Max length of nvm file name and path */
#define NVM_MAX_FILE_NAME_LEN (128)
#define AEAD_TAG_LENGTH (16)

typedef enum ErrorType {
    ELE_NO_ERROR = 0,
    ELE_NOT_INITED = 1,
    ELE_COMMUNICATION_ERROR = 2,
    ELE_GENERAL_ERROR = 3,
    ELE_INVALID_MU_TYPE = 4,
    ELE_INVALID_MESSAGE = 5,
    ELE_INVALID_ARGS = 6,
    ELE_ERROR_RETRY = 7,
    ELE_MEMORY_FAILURE = 8,

    /* HSM error code */
    ELE_COMMAND_SUCCEED = (0x00d6),
    ELE_COMMAND_ENCRYPTED_DATA = (0x05d6),
    ELE_COMMAND_KEYSTORE_NOT_UPDATE = (0x10d6),
    ELE_COMMAND_GENERAL_ERROR = (0x0029),
    ELE_COMMAND_INVALID_ADDRESS = (0x0229),
    ELE_COMMAND_INVALID_ID = (0x0329),
    ELE_COMMAND_INVALID_PARAM = (0x0429),
    ELE_COMMAND_NVM_ERROR = (0x0529),
    ELE_COMMAND_OOM = (0x0629),
    ELE_COMMAND_UNKNOWN_HANDLER = (0x0729),
    ELE_COMMAND_KEYSTORE_AUTH_FAIL = (0x0929),
    ELE_COMMAND_KEYSTORE_ERROR = (0x0a29),
    ELE_COMMAND_ID_CONFLICT = (0x0b29),
    ELE_COMMAND_RANDOM_FAIL = (0x0c29),
    ELE_COMMAND_NOT_SUPPORT = (0x0d29),
    ELE_COMMAND_INVALID_LC = (0x0e29),
    ELE_COMMAND_KEYSTORE_CONFLICT = (0x0f29),
    ELE_COMMAND_FEATURE_NOT_SUPPORT = (0x1129),
    ELE_COMMAND_SERVICE_NOT_INITED = (0x1329),
    ELE_COMMAND_FEATURE_DISABLED = (0x1429),
    ELE_COMMAND_INVALID_CONTENT = (0x1829),
    ELE_COMMAND_NO_SPACE = (0x1929),
    ELE_COMMAND_KEY_GROUP_ERROR = (0x1a29),
    ELE_COMMAND_KEY_NOT_SUPPORT = (0x1b29),
    ELE_COMMAND_ERR_DELETE_PERMANENT_KEY = (0x1c29),
    ELE_COMMAND_OUTPUT_TOO_SMALL = (0x1d29),
    ELE_COMMAND_WRONG_SIZE = (0x1e29),
    ELE_COMMAND_DATA_ALREADY_RETRIEVED = (0x1f29),
    ELE_COMMAND_WRONG_CRC = (0xb929),
    ELE_COMMAND_SIGNED_MESSAGE_ERROR = (0xf029),
    ELE_COMMAND_SERVICE_DISABLED = (0xf429),
} ErrorType;

enum MuType {
    MU_CHANNEL_INVALID = -1,
    MU_CHANNEL_PLAT_HSM,
    MU_CHANNEL_PLAT_HSM_NVM,
    MU_CHANNEL_PLAT_HSM_SECONDARY,
};

typedef enum key_type {
    KEY_TYPE_HMAC = 0x1100,
    KEY_TYPE_DERIVE = 0x1200,
    KEY_TYPE_AES = 0x2400,
    KEY_TYPE_SM4 = 0x2405,
    PUBKEY_TYPE_RSA = 0x4001,
    PUBKEY_TYPE_ECC_BP_R1 = 0x4130,
    PUBKEY_TYPE_ECC_NIST = 0x4112,
    KEY_TYPE_RSA = 0x7001,
    KEY_TYPE_ECC_BP_R1 = 0x7130,
    KEY_TYPE_ECC_NIST = 0x7112,
    KEY_TYPE_OEM_IMPORT_MK_SK = 0x9200,
    PUBKEY_TYPE_ECC_BP_T1 = 0xC180,
} key_type;

typedef enum key_sz_bit {
    KEY_SIZE_HMAC_224 = 224,
    KEY_SIZE_HMAC_256 = 256,
    KEY_SIZE_HMAC_384 = 384,
    KEY_SIZE_HMAC_512 = 512,
    KEY_SIZE_AES_128 = 128,
    KEY_SIZE_AES_192 = 192,
    KEY_SIZE_AES_256 = 256,
    KEY_SIZE_SM4_128 = 128,
    KEY_SIZE_RSA_2048 = 2048,
    KEY_SIZE_RSA_3072 = 3072,
    KEY_SIZE_RSA_4096 = 4096,
    KEY_SIZE_ECC_BP_R1_224 = 224,
    KEY_SIZE_ECC_BP_R1_256 = 256,
    KEY_SIZE_ECC_BP_R1_320 = 320,
    KEY_SIZE_ECC_BP_R1_384 = 384,
    KEY_SIZE_ECC_BP_R1_512 = 512,
    KEY_SIZE_ECC_NIST_224 = 224,
    KEY_SIZE_ECC_NIST_256 = 256,
    KEY_SIZE_ECC_NIST_384 = 384,
    KEY_SIZE_ECC_NIST_521 = 521,
    KEY_SIZE_ECC_BP_T1_224 = 224,
    KEY_SIZE_ECC_BP_T1_256 = 256,
    KEY_SIZE_ECC_BP_T1_320 = 320,
    KEY_SIZE_ECC_BP_T1_384 = 384,
    KEY_SIZE_OEM_IMPORT_MK_SK_128 = 128,
    KEY_SIZE_OEM_IMPORT_MK_SK_192 = 192,
    KEY_SIZE_OEM_IMPORT_MK_SK_256 = 256,
} key_sz_bit;

typedef enum {
    STD_VOLATILE = 0x0,
    STD_PERSISTENT = 0x1,
    STD_PERMANENT = 0xff,
    ELE_IMPORT_VOLATILE = 0xc0020000,
    ELE_IMPORT_PERSISTENT = 0xc0020001,
    ELE_IMPORT_PERMANENT = 0xc00200ff,
} key_lifetime;

typedef enum key_usage {
    KEY_USAGE_ENCRYPT = (0x1u << 8),
    KEY_USAGE_DECRYPT = (0x1u << 9),
    KEY_USAGE_SIGN_MSG = (0x1u << 10),
    KEY_USAGE_VERIFY_MSG = (0x1u << 11),
    KEY_USAGE_SIGN_HASH = (0x1u << 12),
    KEY_USAGE_VERIFY_HASH = (0x1u << 13),
    KEY_USAGE_DERIVE = (0x1u << 14),
} key_usage;

typedef enum permit_algorithms {
    PERMITTED_ALGO_SHA224 = 0x02000008,
    PERMITTED_ALGO_SHA256 = 0x02000009,
    PERMITTED_ALGO_SHA384 = 0x0200000a,
    PERMITTED_ALGO_SHA512 = 0x0200000b,
    PERMITTED_ALGO_SM3 = 0x02000014,
    PERMITTED_ALGO_HMAC_SHA256 = 0x03800009,
    PERMITTED_ALGO_HMAC_SHA384 = 0x0380000a,
    PERMITTED_ALGO_CMAC = 0x03c00200,
    PERMITTED_ALGO_CTR = 0x04c01000,
    PERMITTED_ALGO_CFB = 0x04c01100,
    PERMITTED_ALGO_OFB = 0x04c01200,
    PERMITTED_ALGO_ECB_NO_PADDING = 0x04404400,
    PERMITTED_ALGO_CBC_NO_PADDING = 0x04404000,
    PERMITTED_ALGO_CCM = 0x05500100,
    PERMITTED_ALGO_GCM = 0x05500200,
    PERMITTED_ALGO_CHACHA20_POLY1305 = 0x05100500,
    PERMITTED_ALGO_RSA_PKCS1_V15_SHA224 = 0x06000208,
    PERMITTED_ALGO_RSA_PKCS1_V15_SHA256 = 0x06000209,
    PERMITTED_ALGO_RSA_PKCS1_V15_SHA384 = 0x0600020a,
    PERMITTED_ALGO_RSA_PKCS1_V15_SHA512 = 0x0600020b,
    PERMITTED_ALGO_RSA_PKCS1_PSS_MGF1_SHA224 = 0x06000308,
    PERMITTED_ALGO_RSA_PKCS1_PSS_MGF1_SHA256 = 0x06000309,
    PERMITTED_ALGO_RSA_PKCS1_PSS_MGF1_SHA384 = 0x0600030a,
    PERMITTED_ALGO_RSA_PKCS1_PSS_MGF1_SHA512 = 0x0600030b,
    PERMITTED_ALGO_ECDSA_SHA224 = 0x06000608,
    PERMITTED_ALGO_ECDSA_SHA256 = 0x06000609,
    PERMITTED_ALGO_ECDSA_SHA384 = 0x0600060a,
    PERMITTED_ALGO_ECDSA_SHA512 = 0x0600060b,
    PERMITTED_ALGO_HMAC_KDF_SHA256 = 0x08000109,
    PERMITTED_ALGO_ALL_CIPHER = 0x84C0FF00,
    PERMITTED_ALGO_ALL_AEAD = 0x8550FF00,
    PERMITTED_ALGO_OTH_KEK_CBC = 0x84404000,
    PERMITTED_ALGO_ECDH_HKDF_SHA256 = 0x09020109,
    PERMITTED_ALGO_ECDH_HKDF_SHA384 = 0x0902010A,
    PERMITTED_ALGO_HKDF_EXTRACT_SHA256 = 0x08000409,
    PERMITTED_ALGO_HKDF_EXTRACT_SHA384 = 0x0800040A,
    PERMITTED_ALGO_HKDF_EXTRACT_SHA_ANY = 0x080004FF,
    PERMITTED_ALGO_HKDF_EXPAND_SHA256 = 0x08000509,
    PERMITTED_ALGO_HKDF_EXPAND_SHA384 = 0x0800050A,
    PERMITTED_ALGO_TLS1_3_EARLY_SECRET_SHA256 = 0x8800D009,
    PERMITTED_ALGO_TLS1_3_EARLY_SECRET_SHA384 = 0x8800D00A,
    PERMITTED_ALGO_TLS1_3_HANDSHAKE_SECRET_SHA256 = 0x8800D109,
    PERMITTED_ALGO_TLS1_3_HANDSHAKE_SECRET_SHA384 = 0x8800D10A,
    PERMITTED_ALGO_TLS1_3_MASTER_SECRET_SHA256 = 0x8800D209,
    PERMITTED_ALGO_TLS1_3_MASTER_SECRET_SHA384 = 0x8800D20A,
    PERMITTED_ALGO_ATTEST_CMAC = 0x83C00200,
    PERMITTED_ALGO_ATTEST_ECDSA_SHA224 = 0x86000608,
    PERMITTED_ALGO_ATTEST_ECDSA_SHA256 = 0x86000609,
    PERMITTED_ALGO_ATTEST_ECDSA_SHA384 = 0x8600060A,
    PERMITTED_ALGO_ATTEST_ECDSA_SHA512 = 0x8600060B,
} permit_algorithms;

typedef enum key_life_cycle {
    LIFE_CYCLE_CURRENT = 0x0,
    LIFE_CYCLE_OPEN = 0x1,
    LIFE_CYCLE_CLOSED = 0x2,
    LIFE_CYCLE_CLOSED_LOCKED = 0x4,
} key_life_cycle;

typedef enum key_operation_flag {
    OPERATION_MONOTONIC = (0x1u << 5),
    OPERATION_SYNC = (0x1u << 7),
} key_operation_flag;

typedef enum cipher_operation {
    CIPHER_ONE_GO_FLAGS_DECRYPT = (0u << 0),
    CIPHER_ONE_GO_FLAGS_ENCRYPT = (1u << 0),
    CIPHER_ONE_GO_FLAGS_FULL_IV = (1u << 1),
    CIPHER_ONE_GO_FLAGS_COUNTER_IV = (1u << 2)
} cipher_operation;

typedef enum ele_signature_message_flags {
    ELE_SIGN_FLAGS_DIGEST = (0),
    ELE_SIGN_FLAGS_MESSAGE = (1),
} ele_gen_sign_flags;

typedef struct ele_mu_info {
    uint8_t ele_mu_id;
    uint8_t interrupt_idx;
    uint8_t tz;
    uint8_t did;
    uint8_t cmd_tag;
    uint8_t rsp_tag;
    uint8_t success_tag;
    uint8_t base_api_ver;
    uint8_t fw_api_ver;
} ele_mu_info;

typedef struct ele_mu_ioctl_iobuf {
    uint8_t *user_buf;
    uint32_t length;
    uint32_t flags;
    uint64_t ele_addr;
} ele_mu_ioctl_iobuf;

typedef struct ele_mu_ioctl_shared_mem_cfg {
    uint32_t base_offset;
    uint32_t size;
} ele_mu_ioctl_shared_mem_cfg;

struct msg_hdr {
    uint8_t ver;
    uint8_t size;
    uint8_t cmd;
    uint8_t tag;
};

struct mu_msg {
    struct msg_hdr header;
    union {
        uint32_t u32[ELE_MU_MSG_SIZE];
        uint16_t u16[ELE_MU_MSG_SIZE * 2];
        uint8_t u8[ELE_MU_MSG_SIZE * 4];
    } data;
};

struct mu_rsp {
    uint8_t status;
    uint8_t rating;
    uint16_t rating_extension;
};

struct nvm_blob_id {
    uint32_t metadata;
    uint32_t id;
    uint32_t ext;
};

struct nvm_header {
    uint32_t size;
    uint32_t crc;
    struct nvm_blob_id blob_id;
};

struct nvm_context {
    uint32_t nvm_handle;
    uint32_t prev_command;
    uint32_t next_command;
    uint8_t *last_data;
    const char *nvm_master_name;
    const char *nvm_chunk_path;
};

typedef struct gen_key_attribute {
    uint16_t pub_key_size;
    uint16_t key_group;
    uint16_t type;
    uint16_t size_bits;
    uint32_t lifetime;
    uint32_t usage;
    uint32_t permit_algo;
    uint32_t lifecycle;
    uint8_t flags;
    uint8_t *pub_key_lsb_addr;
} gen_key_attribute;

typedef struct open_session_msg_cmd {
    uint8_t rsv1;
    uint8_t interrupt_num;
    uint16_t rsv2;
    uint8_t priority;
    uint8_t op_mode;
    uint16_t rsv3;
} open_session_msg_cmd;

typedef struct open_session_msg_rsp {
    uint32_t rsp_code;
    uint32_t session_handle;
} open_session_msg_rsp;

typedef struct close_session_msg_cmd {
    uint32_t session_handle;
} close_session_msg_cmd;

typedef struct close_session_msg_rsp {
    uint32_t rsp_code;
} close_session_msg_rsp;

typedef struct key_store_open_msg_cmd {
    uint32_t session_handle;
    uint32_t id;
    uint32_t nonce;
    uint16_t rsv1;
    uint8_t flags;
    uint8_t rsv2;
    uint32_t crc;
} key_store_open_msg_cmd;

typedef struct key_store_open_msg_rsp {
    uint32_t rsp_code;
    uint32_t key_store_handle;
} key_store_open_msg_rsp;

typedef struct key_store_close_msg_cmd {
    uint32_t key_store_handle;
} key_store_close_msg_cmd;

typedef struct key_store_close_msg_rsp {
    uint32_t rsp_code;
} key_store_close_msg_rsp;

typedef struct storage_open_msg_cmd {
    uint32_t session_handle;
    uint32_t msbi;
    uint32_t msbo;
    uint8_t flags;
    uint8_t rsv[3];
    uint32_t crc;
} storage_open_msg_cmd;

typedef struct storage_open_msg_rsp {
    uint32_t rsp_code;
    uint32_t nvm_storage_hdl;
} storage_open_msg_rsp;

typedef struct storage_close_msg_cmd {
    uint32_t nvm_storage_handle;
} storage_close_msg_cmd;

typedef struct storage_close_msg_rsp {
    uint32_t rsp_code;
} storage_close_msg_rsp;

typedef struct storage_master_import_msg_cmd {
    uint32_t nvm_storage_handle;
    uint32_t master_data_lsb_addr;
    uint32_t master_data_size;
} storage_master_import_msg_cmd;

typedef struct storage_master_import_msg_rsp {
    uint32_t rsp_code;
} storage_master_import_msg_rsp;

typedef struct storage_master_exp_msg_cmd {
    uint32_t nvm_storage_handle;
    uint32_t master_data_size;
} storage_master_exp_msg_cmd;

typedef struct storage_master_exp_msg_rsp {
    uint32_t nvm_storage_handle;
    uint32_t rsp_code;
    uint32_t master_data_addr;
} storage_master_exp_msg_rsp;

typedef struct storage_exp_finish_msg_cmd {
    uint32_t nvm_storage_handle;
    uint32_t export_status;
} storage_exp_finish_msg_cmd;

typedef struct storage_exp_finish_msg_rsp {
    uint32_t nvm_storage_handle;
    uint32_t rsp_code;
} storage_exp_finish_msg_rsp;

typedef struct storage_get_chunk_msg_cmd {
    uint32_t nvm_storage_handle;
    struct nvm_blob_id blob_id;
    uint32_t crc;
} storage_get_chunk_msg_cmd;

typedef struct storage_get_chunk_msg_rsp {
    uint32_t chunk_size;
    uint32_t chunk_addr;
    uint32_t rsp_code;
} storage_get_chunk_msg_rsp;

typedef struct storage_get_chunk_done_msg_cmd {
    uint32_t nvm_storage_handle;
    uint32_t status;
} storage_get_chunk_done_msg_cmd;

typedef struct storage_get_chunk_done_msg_rsp {
    uint32_t rsp_code;
} storage_get_chunk_done_msg_rsp;

typedef struct storage_chunk_exp_msg_cmd {
    uint32_t nvm_storage_handle;
    uint32_t chunk_size;
    struct nvm_blob_id blob_id;
    uint32_t crc;
} storage_chunk_exp_msg_cmd;

typedef struct storage_chunk_exp_msg_rsp {
    uint32_t rsp_code;
    uint32_t chunk_blob_addr;
} storage_chunk_exp_msg_rsp;

typedef struct key_mgt_open_msg_cmd {
    uint32_t key_store_handle;
    uint32_t msbi;
    uint32_t msbo;
    uint8_t flags;
    uint8_t rsv[3];
    uint32_t crc;
} key_mgt_open_msg_cmd;

typedef struct key_mgt_open_msg_rsp {
    uint32_t rsp_code;
    uint32_t key_mgt_handle;
} key_mgt_open_msg_rsp;

typedef struct key_mgt_close_msg_cmd {
    uint32_t key_mgt_handle;
} key_mgt_close_msg_cmd;

typedef struct key_mgt_close_msg_rsp {
    uint32_t rsp_code;
} key_mgt_close_msg_rsp;

typedef struct gen_key_msg_cmd {
    uint32_t key_mgt_handle;
    uint32_t key_id;
    uint16_t pub_key_size;
    uint16_t key_group;
    uint16_t type;
    uint16_t size_bits;
    uint32_t lifetime;
    uint32_t usage;
    uint32_t permit_algo;
    uint32_t lifecycle;
    uint8_t flags;
    uint8_t rsv[3];
    uint32_t pub_key_lsb_addr;
    uint32_t crc;
} gen_key_msg_cmd;

typedef struct gen_key_msg_rsp {
    uint32_t rsp_code;
    uint32_t key_id;
    uint16_t pub_key_size;
    uint16_t rsv;
} gen_key_msg_rsp;

typedef struct del_key_msg_cmd {
    uint32_t key_mgt_handle;
    uint32_t key_id;
    uint16_t rsv1;
    uint8_t flags;
    uint8_t rsv2;
} del_key_msg_cmd;

typedef struct del_key_msg_rsp {
    uint32_t rsp_code;
} del_key_msg_rsp;

typedef struct key_attribute {
    uint16_t size_bits;
    uint16_t type;
    uint32_t lifetime;
    uint32_t usage;
    uint32_t permit_algo;
    uint32_t lifecycle;
} key_attribute;

typedef struct get_key_attr_msg_cmd {
    uint32_t key_mgt_handle;
    uint32_t key_id;
    uint32_t rsv;
} get_key_attr_msg_cmd;

typedef struct get_key_attr_rsp {
    uint32_t rsp_code;
    uint16_t size_bits;
    uint16_t type;
    uint32_t lifetime;
    uint32_t usage;
    uint32_t permit_algo;
    uint32_t lifecycle;
    uint32_t rsv;
    uint32_t crc;
} get_key_attr_rsp;

typedef struct cipher_open_msg_cmd {
    uint32_t key_store_handle;
    uint32_t msbi;
    uint32_t msbo;
    uint8_t flags;
    uint8_t rsv[3];
    uint32_t crc;
} cipher_open_msg_cmd;

typedef struct cipher_open_msg_rsp {
    uint32_t rsp_code;
    uint32_t cipher_hdl;
} cipher_open_msg_rsp;

typedef struct cipher_close_msg_cmd {
    uint32_t cipher_hdl;
} cipher_close_msg_cmd;

typedef struct cipher_close_msg_rsp {
    uint32_t rsp_code;
} cipher_close_msg_rsp;

typedef struct cipher_msg_cmd {
    uint32_t cipher_hdl;
    uint32_t key_id;
    uint32_t iv_addr;
    uint16_t iv_size;
    uint8_t flags;
    uint8_t rsv;
    uint32_t algo;
    uint32_t input_addr;
    uint32_t output_addr;
    uint32_t input_size;
    uint32_t output_size;
    uint32_t crc;
} cipher_msg_cmd;

typedef struct cipher_msg_rsp {
    uint32_t rsp_code;
    uint32_t output_size;
} cipher_msg_rsp;

typedef struct cipher_operation_attr {
    uint32_t key_id;
    uint8_t *iv_addr;
    uint16_t iv_size;
    uint8_t flags;
    uint32_t algo;
    uint8_t *input_addr;
    uint8_t *output_addr;
    uint32_t input_size;
    uint32_t output_size;
} cipher_operation_attr;

typedef struct cipher_ae_msg_cmd {
    uint32_t cipher_hdl;
    uint32_t key_id;
    uint32_t iv_addr;
    uint16_t iv_size;
    uint8_t flags;
    uint8_t rsv;
    uint32_t algo;
    uint32_t aad_addr;
    uint16_t aad_size;
    uint16_t rsv2;
    uint32_t input_addr;
    uint32_t output_addr;
    uint32_t input_size;
    uint32_t output_size;
    uint32_t crc;
} cipher_ae_msg_cmd;

typedef struct cipher_ae_msg_rsp {
    uint32_t rsp_code;
    uint32_t output_size;
} cipher_ae_msg_rsp;

typedef struct cipher_ae_operation_attr {
    uint32_t key_id;
    uint8_t *iv_addr;
    uint16_t iv_size;
    uint8_t flags;
    uint32_t algo;
    uint8_t *aad_addr;
    uint16_t aad_size;
    uint8_t *input_addr;
    uint8_t *output_addr;
    uint32_t input_size;
    uint32_t output_size;
} cipher_ae_operation_attr;

typedef struct sign_gen_open_msg_cmd {
    uint32_t key_store_handle;
    uint32_t msbi;
    uint32_t msbo;
    uint8_t flags;
    uint8_t rsv[3];
    uint32_t crc;
} sign_gen_open_msg_cmd;

typedef struct sign_gen_open_msg_rsp {
    uint32_t rsp_code;
    uint32_t sign_gen_hdl;
} sign_gen_open_msg_rsp;

typedef struct sign_gen_close_msg_cmd {
    uint32_t sign_gen_hdl;
} sign_gen_close_msg_cmd;

typedef struct sign_gen_close_msg_rsp {
    uint32_t rsp_code;
} sign_gen_close_msg_rsp;

typedef struct gen_sign_msg_cmd {
    uint32_t sign_gen_hdl;
    uint32_t key_id;
    uint32_t msg_lsb_addr;
    uint32_t sign_lsb_addr;
    uint32_t msg_size;
    uint16_t sign_size;
    uint8_t flags;
    uint8_t rsv1;
    uint32_t sign_scheme;
    uint16_t salt_len;
    uint16_t rsv2;
    uint32_t crc;
} gen_sign_msg_cmd;

typedef struct gen_sign_msg_rsp {
    uint32_t rsp_code;
    uint16_t signature_size;
    uint16_t rsv;
} gen_sign_msg_rsp;

typedef struct gen_sign_attr {
    uint32_t key_id;
    uint8_t *msg_lsb_addr;
    uint8_t *sign_lsb_addr;
    uint32_t msg_size;
    uint16_t sign_size;
    uint8_t flags;
    uint32_t sign_scheme;
    uint16_t salt_len;
} gen_sign_attr;

typedef struct sign_verify_open_msg_cmd {
    uint32_t session_hdl;
    uint32_t msbi;
    uint32_t msbo;
    uint8_t flags;
    uint8_t rsv[3];
    uint32_t crc;
} sign_verify_open_msg_cmd;

typedef struct sign_verify_open_msg_rsp {
    uint32_t rsp_code;
    uint32_t sign_verify_hdl;
} sign_verify_open_msg_rsp;

typedef struct sign_verify_close_msg_cmd {
    uint32_t sign_verify_hdl;
} sign_verify_close_msg_cmd;

typedef struct sign_verify_close_msg_rsp {
    uint32_t rsp_code;
} sign_verify_close_msg_rsp;

typedef struct verify_sign_msg_cmd {
    uint32_t sign_verify_hdl;
    uint32_t key_lsb_addr;
    uint32_t msg_lsb_addr;
    uint32_t sign_lsb_addr;
    uint32_t msg_size;
    uint16_t sign_size;
    uint16_t key_size;
    uint16_t key_security_size;
    uint16_t key_type;
    uint8_t flags;
    uint8_t rsv[3];
    uint32_t sign_scheme;
    uint16_t salt_len;
    uint16_t rsv2;
    uint32_t crc;
} verify_sign_msg_cmd;

typedef struct verify_sign_msg_rsp {
    uint32_t rsp_code;
    uint32_t verify_status;
} verify_sign_msg_rsp;

typedef struct verify_sign_attr {
    uint8_t *key_lsb_addr;
    uint8_t *msg_lsb_addr;
    uint8_t *sign_lsb_addr;
    uint32_t msg_size;
    uint16_t sign_size;
    uint16_t key_size;
    uint16_t key_security_size;
    uint16_t key_type;
    uint8_t flags;
    uint32_t sign_scheme;
    uint16_t salt_len;
} verify_sign_attr;

#endif //__ELE_MESSAGE_H__
