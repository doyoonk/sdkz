/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <hu/hupacket.h>
#include <zephyr/kernel.h>
#include <zephyr/storage/flash_map.h>

#include <huerrno.h>

enum {
    ARG_FLASH_CMD,
    ARG_FLASH_PARTITION,
    ARG_ERASE_MAX,
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
#if FIXED_PARTITION_EXISTS(slot2_partition)
    { "slot2", FIXED_PARTITION_ID(slot2_partition) },
#endif
#if FIXED_PARTITION_EXISTS(slot3_partition)
    { "slot3", FIXED_PARTITION_ID(slot3_partition) },
#endif
#if FIXED_PARTITION_EXISTS(slot4_partition)
    { "slot4", FIXED_PARTITION_ID(slot4_partition) },
#endif
#if FIXED_PARTITION_EXISTS(slot5_partition)
    { "slot5", FIXED_PARTITION_ID(slot5_partition) },
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

static void _erase(struct hup_handle* h, bool crc, void* user_data)
{
    if (crc && h->argc >= ARG_ERASE_MAX)
    {
        const struct flash_area* fa;
        int rc = open_flash_partition(h->argv[ARG_FLASH_PARTITION], &fa);
        if (rc != 0)
            goto erase_error;

        // TODO: erase flash partition
        close_flash_partition(fa);
        hupacket_record_int(h, h->tx_buffer, NOERROR);
        goto erase_done;
    }

erase_error:
    hupacket_record_int(h, h->tx_buffer, get_errno(h, crc, ARG_ERASE_MAX));

erase_done:
    append_record(h, ARG_FLASH_PARTITION, ARG_FLASH_PARTITION);
    h->send(h->tx_buffer, strlen(h->tx_buffer), h->user_data);
}

DEFINE_HUP_CMD(hup_cmd_erase, "erase", _erase);


static void _flash(struct hup_handle* h, bool crc, void* user_data)
{
    if (crc && h->argc >= ARG_FLASH_MAX)
    {
        const struct flash_area* fa;
        int rc = open_flash_partition(h->argv[ARG_FLASH_PARTITION], &fa);
        if (rc != 0)
            goto flash_error;

        // TODO: write data
        close_flash_partition(fa);
        hupacket_record_int(h, h->tx_buffer, NOERROR);
        goto flash_done;
    }

flash_error:
    hupacket_record_int(h, h->tx_buffer, get_errno(h, crc, ARG_FLASH_MAX));

flash_done:
    append_record(h, ARG_FLASH_PARTITION, ARG_FLASH_LENGTH);
    h->send(h->tx_buffer, strlen(h->tx_buffer), h->user_data);
}

DEFINE_HUP_CMD(hup_cmd_flash, "flash", _flash);
