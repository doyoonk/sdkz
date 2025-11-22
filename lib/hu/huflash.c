/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "zephyr/drivers/flash.h"
#include <hu/hupacket.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>

#include <hu/ascii85.h>
#include <huerrno.h>

#define ASCII85_ERROR_CODE_START 255

enum {
    ARG_FLASH_CMD,
    ARG_FLASH_PARTITION,
    ARG_ERASE_MAX,
    ARG_FLASH_WRITE_PROTECT = ARG_ERASE_MAX,
    ARG_WRPRT_MAX,
    ARG_FLASH_OFFSET = ARG_ERASE_MAX,
    ARG_FLASH_LENGTH,
    ARG_FLASH_ASCII85_DATA,
    ARG_FLASH_MAX
};


#if !defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD)

#define FIXED_PARTITION_IS_RUNNING_APP_PARTITION(label) \
    (FIXED_PARTITION_OFFSET(label) <= CONFIG_FLASH_LOAD_OFFSET && \
    FIXED_PARTITION_OFFSET(label) + FIXED_PARTITION_SIZE(label) > CONFIG_FLASH_LOAD_OFFSET)

#endif

struct fixed_partition
{
    const char* const partition_name;
    uint8_t id;
} fixed_partitions[] =
{
#if (defined(CONFIG_MCUBOOT_BOOTLOADER_MODE_RAM_LOAD) || ! defined(CONFIG_XIP))
 && FIXED_PARTITION_EXISTS(slot0_partition)
    { "slot0", FIXED_PARTITION_ID(slot0_partition) },
#endif
#if FIXED_PARTITION_EXISTS(slot1_partition)
    { "slot1", FIXED_PARTITION_ID(slot1_partition) },
#endif
};

static int get_partition_id(const char* partition_name)
{
    for (size_t i = 0; i < ARRAY_SIZE(fixed_partitions); i ++)
    {
        if (strcmp(partition_name, fixed_partitions[i].partition_name) == 0)
            return fixed_partitions[i].id;
    }
    return -EINVAL;
}

static int open_flash_partition(const char* partition_name, const struct flash_area** fa)
{
    int partition_id = get_partition_id(partition_name);
    if (partition_id < 0)
        return partition_id;

    return flash_area_open(partition_id, fa);
}
static void close_flash_partition(const struct flash_area* fa)
{
    if (fa != NULL)
        flash_area_close(fa);
}

static int get_errno(struct hup_handle* h, bool crc, int arg_max)
{
    if (crc)
        return ENMCRC16;
    else if (h->argc < arg_max)
        return EINVAL;
    else
        return errno;
}

static void append_record(struct hup_handle* h, int from, int to)
{
    for (int i = from; i <= to; i ++)
    {
        if (h->argc > i)
            hupacket_record_str(h, h->tx_buffer, h->argv[i]);
    }
}

struct fw_upgrade_data
{
    int param;
    int size;
    char* bin;
};
static void _fw_upgrade(struct hup_handle* h, bool crc, void* user_data
    , int argc_max, int argc_resp
    , int (*func)(const struct flash_area* fa, struct fw_upgrade_data* param)
    , struct fw_upgrade_data* param)
{
    if (crc && h->argc >= argc_max)
    {
        const struct flash_area* fa;
        int rc = open_flash_partition(h->argv[ARG_FLASH_PARTITION], &fa);
        if (rc != 0)
            goto fw_upgrade_error;

        rc = func(fa, param);
        if (rc != 0)
            goto fw_upgrade_error;
        close_flash_partition(fa);
        hupacket_record_int(h, h->tx_buffer, NOERROR);
        goto fw_upgrade_done;
    }

fw_upgrade_error:
    hupacket_record_int(h, h->tx_buffer, get_errno(h, crc, argc_max));

fw_upgrade_done:
    append_record(h, ARG_FLASH_PARTITION, argc_resp);
    h->send(h->tx_buffer, strlen(h->tx_buffer), h->user_data);
}

static int _f_wrprt(const struct flash_area* fa, struct fw_upgrade_data* param)
{
#if  CONFIG_FLASH_STM32_WRITE_PROTECT
    // TODO: write protect
#endif
    return 0;
}
static int _f_erase(const struct flash_area* fa, struct fw_upgrade_data* param)
{
    return flash_area_erase(fa, 0, fa->fa_size);
}

static int _f_flash(const struct flash_area* fa, struct fw_upgrade_data* param)
{
    int32_t len = strlen(param->bin);
    int olen = decode_ascii85(param->bin, len
        , param->bin, len);

    if (-ASCII85_ERROR_CODE_START != ascii85_err_out_buf_too_small)
        return -ECODEA85;
    if (olen < 0)
        return -(olen + ASCII85_ERROR_CODE_START + EASCII85);
    if (olen != param->size)
        return -EDESZA85;
    return flash_write(fa->fa_dev, param->param, param->bin, param->size);
}

static void _wrprt(struct hup_handle* h, bool crc, void* user_data)
{
#if CONFIG_FLASH_HAS_EX_OP
    struct fw_upgrade_data fw_data = {
        .param = strtol(h->argv[ARG_FLASH_WRITE_PROTECT], NULL, 10)
    };

    _fw_upgrade(h, crc, user_data, ARG_WRPRT_MAX, ARG_FLASH_PARTITION,
         _f_wrprt, &fw_data);
#endif
}
DEFINE_HUP_CMD(hup_cmd_wrprt, "wrprt", _wrprt);

static void _erase(struct hup_handle* h, bool crc, void* user_data)
{
    _fw_upgrade(h, crc, user_data, ARG_ERASE_MAX, ARG_FLASH_PARTITION
        , _f_erase, NULL);
}
DEFINE_HUP_CMD(hup_cmd_erase, "erase", _erase);

static void _flash(struct hup_handle* h, bool crc, void* user_data)
{
    struct fw_upgrade_data fw_data = {
        .param = strtol(h->argv[ARG_FLASH_OFFSET], NULL, 16),
        .size = strtol(h->argv[ARG_FLASH_LENGTH], NULL, 16),
        .bin = h->argv[ARG_FLASH_ASCII85_DATA]
    };
    _fw_upgrade(h, crc, user_data, ARG_FLASH_MAX, ARG_FLASH_LENGTH
        , _f_flash, &fw_data);
}
DEFINE_HUP_CMD(hup_cmd_flash, "flash", _flash);
