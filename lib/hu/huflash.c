/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>

#include <hu/hupacket.h>
#include <hu/ascii85.h>
#include <huerrno.h>

LOG_MODULE_REGISTER(huflash, CONFIG_LOG_DEFAULT_LEVEL);

#define ASCII85_ERROR_CODE_START    255
#define ASCII85_MAX_CHUNK_SIZE      256

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

static struct fixed_partition
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
static int last_size;

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

static inline void _set_status(void*h, int rc)
{
    if (rc == 0)
        hupacket_ack_response(h, NULL);
    else
        hupacket_nak_response(h, NULL, rc);
}

static inline void _set_done(void*h, int argc, const char** argv, int from, int to)
{
    if (to >= argc)
        to = argc - 1;
    for (int i = from; i <= to; i ++)
        hupacket_record_str(h, NULL, argv[i]);
    hupacket_send_buffer(h, NULL);
}

static void _wrprt(void* h, int argc, const char** argv)
{
#if CONFIG_FLASH_HAS_EX_OP
    const struct flash_area* fa = NULL;
    int rc = -EINVAL;

    last_size = -1;
    if (argc >= ARG_WRPRT_MAX)
    {
        int wrptr = strtol(argv[ARG_FLASH_WRITE_PROTECT], NULL, 10);
        rc = open_flash_partition(argv[ARG_FLASH_PARTITION], &fa);
        if (rc != 0)
        {
            LOG_ERR("Write protect: Failed to open partition %s", argv[ARG_FLASH_PARTITION]);
            goto fw_wrprt_error;
        }
#if  CONFIG_FLASH_STM32_WRITE_PROTECT
        // TODO: write protect
#endif
        LOG_INF("Succeed wrprt partition %s, id %d, %s", argv[ARG_FLASH_PARTITION], fa->fa_id, wrptr ? "locked" : "unlocked");
    }
fw_wrprt_error:
    _set_status(h, rc);

    close_flash_partition(fa);
    _set_done(h, argc, argv, ARG_FLASH_PARTITION, ARG_FLASH_WRITE_PROTECT);
#endif
}
DEFINE_HUP_CMD(hup_cmd_wrprt, "wrprt", _wrprt);

static void _erase(void* h, int argc, const char** argv)
{
    const struct flash_area* fa = NULL;
    int rc = -EINVAL;

    last_size = -1;
    if (argc >= ARG_ERASE_MAX)
    {
        rc = open_flash_partition(argv[ARG_FLASH_PARTITION], &fa);
        if (rc != 0)
        {
            LOG_ERR("Erase: Failed to open partition %s", argv[ARG_FLASH_PARTITION]);
            goto fw_erase_error;
        }

        rc = flash_area_erase(fa, 0, fa->fa_size);
        if (rc < 0)
        {
            LOG_ERR("Failed erase partition %s, id %d, offset 0x%08lx, size 0x%08x", argv[ARG_FLASH_PARTITION], fa->fa_id, fa->fa_off, fa->fa_size);
            goto fw_erase_error;
        }
        LOG_INF("Succeed erase partition %s, id %d, offset 0x%08lx, size 0x%08x", argv[ARG_FLASH_PARTITION], fa->fa_id, fa->fa_off, fa->fa_size);
    }
fw_erase_error:
    _set_status(h, rc);

    close_flash_partition(fa);
    _set_done(h, argc, argv, ARG_FLASH_PARTITION, ARG_FLASH_PARTITION);
}
DEFINE_HUP_CMD(hup_cmd_erase, "erase", _erase);

static void _flash(void* h, int argc, const char** argv)
{
    const struct flash_area* fa = NULL;
    int rc = -EINVAL;

    if (argc >= ARG_FLASH_MAX)
    {
        rc = open_flash_partition(argv[ARG_FLASH_PARTITION], &fa);
        if (rc != 0)
            goto fw_flash_error;

        int size = strtol(argv[ARG_FLASH_LENGTH], NULL, 16);
        if (size > ASCII85_MAX_CHUNK_SIZE)
        {
            rc = -EINVAL;
            goto fw_flash_error;
        }

        struct hup_handle* handle = (struct hup_handle*)h;
        int offset = strtol(argv[ARG_FLASH_OFFSET], NULL, 16);
        uint8_t* ptr = (uint8_t*)argv[ARG_FLASH_ASCII85_DATA];
        int32_t len = strlen(ptr);
        int bin_len = decode_ascii85(ptr, len, handle->tx_buffer, (ASCII85_MAX_CHUNK_SIZE * 5));

        if (bin_len < 0)
            rc = -(bin_len + ASCII85_ERROR_CODE_START + EASCII85);
        if (rc == 0 && bin_len != size)
            rc = -EDESZA85;
        if (rc != 0)
        {
            LOG_INF("Flash: Failed decode error code %d", rc);
            goto fw_flash_error;
        }
        rc = flash_area_write(fa, offset, handle->tx_buffer, size);
        if (rc != 0)
        {
            LOG_INF("Flash: Failed write error code %d", rc);
            goto fw_flash_error;
        }
        if (last_size != size)
        {
            last_size = size;
            LOG_INF("Flash: Succeed offset 0x%08x, len 0x%x", offset, size);
        }
    }

fw_flash_error:
    _set_status(h, rc);

    close_flash_partition(fa);
    _set_done(h, argc, argv, ARG_FLASH_PARTITION, ARG_FLASH_LENGTH);
}
DEFINE_HUP_CMD(hup_cmd_flash, "flash", _flash);
