/*
 * Copyright (c) 2025-2026 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>

#include <hu/hupacket.h>
#include <hu/ascii85.h>
#include <hu/bootloader.h>
#include <huerrno.h>

#if defined(CONFIG_FLASH_STM32_WRITE_PROTECT) && defined(CONFIG_FLASH_EX_OP_ENABLED) && \
    defined(CONFIG_FLASH_PAGE_LAYOUT)
#include <zephyr/drivers/flash/stm32_flash_api_extensions.h>
#endif

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


struct fixed_partition
{
    const char* const partition_name;
    uint8_t id;
};

static struct fixed_partition fixed_partitions[] =
{
#if FIXED_PARTITION_EXISTS(slot0_partition)
    { "slot0", PARTITION_ID(slot0_partition) },
#endif
#if FIXED_PARTITION_EXISTS(slot1_partition)
    { "slot1", PARTITION_ID(slot1_partition) },
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
    int rc;

#if CONFIG_RETENTION_BOOTLOADER_INFO
    if (strcmp(partition_name, "inactive") == 0)
    {
        uint8_t partition_id;
        rc = bootloader_active_slot((uint8_t*)&partition_id);
        if (rc != 0)
            return rc;
        else if (partition_id == 0)
            rc = get_partition_id("slot1");
        else if (partition_id == 1)
            rc = get_partition_id("slot0");
        else
            rc = -EINVAL;
    }
    else
    {
        rc = get_partition_id(partition_name);
    }
#else
    rc = get_partition_id(partition_name);
#endif

    if (rc < 0)
        return rc;
    return flash_area_open(rc, fa);
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

static int protect_flash_partition(const struct flash_area *fa, bool enable)
{
#if defined(CONFIG_FLASH_STM32_WRITE_PROTECT) && defined(CONFIG_FLASH_EX_OP_ENABLED) && \
    defined(CONFIG_FLASH_PAGE_LAYOUT)
    const struct device *dev = flash_area_get_device(fa);
    struct flash_stm32_ex_op_sector_wp_out result;
    uint64_t mask = 0;

    /* Vendor operation numbers can mean something else on an external flash. */
    if (dev != DEVICE_DT_GET(DT_INST(0, st_stm32_flash_controller)))
        return -ENOTSUP;
#if defined(CONFIG_FLASH_STM32_WRITE_PROTECT_DISABLE_PREVENTION)
    if (!enable) return -EACCES;
#endif
    if (fa->fa_off < 0 || fa->fa_size == 0) return -EINVAL;
    /* Zephyr flash offsets are signed. Validate before computing the end. */
    if ((uint64_t)fa->fa_off > INT32_MAX ||
        (uint64_t)fa->fa_size > INT32_MAX - (uint64_t)fa->fa_off)
        return -EINVAL;

    off_t offset = fa->fa_off;
    size_t remaining = fa->fa_size;
    while (remaining != 0) {
        struct flash_pages_info page;
        int rc = flash_get_page_info_by_offs(dev, offset, &page);
        if (rc != 0) return rc;
        /* Never protect adjacent data by rounding a partition boundary. */
        if (page.start_offset != offset || page.size == 0 || page.size > remaining)
            return -EINVAL;
        if (page.index >= 64) return -ENOTSUP;
        mask |= UINT64_C(1) << page.index;
        offset += page.size;
        remaining -= page.size;
    }

    int rc = flash_ex_op(dev, FLASH_STM32_EX_OP_SECTOR_WP, 0, &result);
    if (rc != 0) return rc;
    uint64_t expected = enable ? mask : 0;
    if ((result.protected_mask & mask) == expected) return 0;

    const struct flash_stm32_ex_op_sector_wp_in request = {
        .enable_mask = enable ? mask : 0,
        .disable_mask = enable ? 0 : mask,
    };
    rc = flash_ex_op(dev, FLASH_STM32_EX_OP_SECTOR_WP, (uintptr_t)&request, &result);
    if (rc != 0) return rc;
    return (result.protected_mask & mask) == expected ? 0 : -EIO;
#else
    ARG_UNUSED(fa);
    ARG_UNUSED(enable);
    return -ENOTSUP;
#endif
}

static void _wrprt(void* h, int argc, const char** argv)
{
    const struct flash_area* fa = NULL;
    int rc = -EINVAL;

    last_size = -1;
    if (argc == ARG_WRPRT_MAX &&
        (strcmp(argv[ARG_FLASH_WRITE_PROTECT], "0") == 0 ||
         strcmp(argv[ARG_FLASH_WRITE_PROTECT], "1") == 0))
    {
        rc = open_flash_partition(argv[ARG_FLASH_PARTITION], &fa);
        if (rc == 0)
            rc = protect_flash_partition(fa, argv[ARG_FLASH_WRITE_PROTECT][0] == '1');
    }
    _set_status(h, rc);

    close_flash_partition(fa);
    _set_done(h, argc, argv, ARG_FLASH_PARTITION, ARG_FLASH_WRITE_PROTECT);
}
DEFINE_HUP_CMD(hup_cmd_wrprt, "wrprt", _wrprt);

static void _erase(void* h, int argc, const char** argv)
{
    const struct flash_area* fa = NULL;
    int rc = -EINVAL;

    last_size = -1;
    if (argc == ARG_ERASE_MAX)
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

static bool parse_hex_u32(const char *text, uint32_t *value)
{
    uint32_t result = 0;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) text += 2;
    if (*text == '\0') return false;
    for (; *text != '\0'; text++) {
        unsigned digit;
        if (*text >= '0' && *text <= '9') digit = *text - '0';
        else if (*text >= 'a' && *text <= 'f') digit = *text - 'a' + 10;
        else if (*text >= 'A' && *text <= 'F') digit = *text - 'A' + 10;
        else return false;
        if (result > (UINT32_MAX - digit) / 16) return false;
        result = result * 16 + digit;
    }
    *value = result;
    return true;
}

static int decode_flash_data(const char *text, uint8_t *output, size_t size)
{
    size_t remaining = strlen(text);
    size_t written = 0;
    while (remaining != 0) {
        /* The decoder reserves four bytes per encoded character, even for
         * ordinary five-character groups. Bound each call with a small
         * scratch buffer rather than trusting a worst-case whole-packet size.
         */
        uint8_t chunk[20];
        size_t count = *text == 'z' ? 1 : MIN(remaining, 5);
        if (count == 1 && *text != 'z') return -ECODEA85;
        int decoded = decode_ascii85((const uint8_t *)text, count, chunk, sizeof(chunk));
        if (decoded < 0) return -(decoded + ASCII85_ERROR_CODE_START + EASCII85);
        if ((size_t)decoded > size - written) return -EDESZA85;
        memcpy(output + written, chunk, decoded);
        written += decoded;
        text += count;
        remaining -= count;
    }
    return written == size ? 0 : -EDESZA85;
}

static void _flash(void* h, int argc, const char** argv)
{
    uint32_t size, offset;
    struct hup_handle* handle = h;
    const struct flash_area* fa = NULL;
    int rc = -EINVAL;

    if (argc != ARG_FLASH_MAX ||
        !parse_hex_u32(argv[ARG_FLASH_LENGTH], &size) ||
        !parse_hex_u32(argv[ARG_FLASH_OFFSET], &offset) ||
        size == 0 || size > ASCII85_MAX_CHUNK_SIZE ||
        size > sizeof(handle->tx_buffer) || offset > INT32_MAX)
        goto fw_flash_error;

    rc = open_flash_partition(argv[ARG_FLASH_PARTITION], &fa);
    if (rc != 0)
        goto fw_flash_error;

    if (offset > fa->fa_size || size > fa->fa_size - offset)
    {
        rc = -EINVAL;
        goto fw_flash_error;
    }
    rc = decode_flash_data(argv[ARG_FLASH_ASCII85_DATA],
        (uint8_t *)handle->tx_buffer, size);
    if (rc == 0)
    {
        rc = flash_area_write(fa, offset, handle->tx_buffer, size);
        if (rc == 0 && last_size != size) {
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
