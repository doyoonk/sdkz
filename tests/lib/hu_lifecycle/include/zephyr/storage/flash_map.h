/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#define FIXED_PARTITION_EXISTS(name) 1
#define PARTITION_ID(name) name##_id
#define boot_partition_id 1
#define DT_NODELABEL(name) name
#define slot0_partition_id 2
#define slot1_partition_id 3
#define TEST_CAT_(a, b) a##b
#define TEST_CAT(a, b) TEST_CAT_(a, b)
#define TEST_UNWRAP(...) __VA_ARGS__
#define TEST_COND_1(yes, no) TEST_UNWRAP yes
#define TEST_COND_0(yes, no) TEST_UNWRAP no
#define COND_CODE_1(flag, yes, no) TEST_CAT(TEST_COND_, flag)(yes, no)
#define STRINGIFY_(x) #x
#define STRINGIFY(x) STRINGIFY_(x)
#define ZERO_OR_COMPILE_ERROR(cond) ((int)sizeof(char[1 - 2 * !(cond)]) - 1)
#define DT_PARTITION_ID(node) TEST_CAT(node, _id)
#define DT_PARENT(node) TEST_CAT(node, _parent)
#define DT_NODE_HAS_COMPAT(node, compat) TEST_CAT(node, _fixed)
#define fixed_table_fixed 1
#define sub_table_fixed 0
#define DT_NODE_HAS_STATUS_OKAY(node) TEST_CAT(node, _okay)
#define online_okay 1
#define offline_okay 0
#define DT_MTD_FROM_PARTITION(node) TEST_CAT(node, _mtd)
#define DT_MTD_FROM_FIXED_SUBPARTITION(node) TEST_CAT(node, _mtd)
#define DT_MTD_FROM_MAPPED_PARTITION(node) TEST_CAT(node, _mtd)
#define DT_PROP_OR(node, prop, default_value) TEST_CAT(node, _label)
#define DT_NODE_PATH(node) TEST_CAT(node, _path)
#define DT_FOREACH_NODELABEL_VARGS(node, fn, ...) TEST_CAT(node, _labels)(fn, __VA_ARGS__)
#define DT_FOREACH_STATUS_OKAY(compat, fn) TEST_CAT(TEST_FOREACH_, compat)(fn)
#define TEST_FOREACH_fixed_partitions(fn) fn(fixed_table)
#define TEST_FOREACH_fixed_subpartitions(fn) fn(sub_table)
#define TEST_FOREACH_zephyr_mapped_partition(fn) fn(mapped_partition) fn(disabled_mapped)
#define DT_FOREACH_CHILD(node, fn) TEST_CAT(node, _children)(fn)
#define fixed_table_children(fn) fn(boot_partition) fn(slot0_partition) fn(slot1_partition) \
    fn(storage_partition) fn(scratch_partition) fn(slot2_partition) fn(last_partition) \
    fn(duplicate_a) fn(duplicate_b) fn(offline_partition) fn(unlabeled_partition)
#define sub_table_children(fn) fn(sub_partition)
struct flash_area { uint8_t fa_id; off_t fa_off; uint32_t fa_size; const struct device *fa_dev; };
static inline const struct device *flash_area_get_device(const struct flash_area *fa) {
    return fa->fa_dev;
}
int flash_area_open(uint8_t id,const struct flash_area **fa);
void flash_area_close(const struct flash_area *fa);
int flash_area_write(const struct flash_area *fa,off_t off,const void *src,size_t len);
int flash_area_erase(const struct flash_area *fa,off_t off,size_t len);

int flash_area_read(const struct flash_area *fa,off_t off,void *dst,size_t len);

#define boot_partition_parent fixed_table
#define boot_partition_mtd online
#define boot_partition_okay 1
#define boot_partition_label "mcuboot"
#define boot_partition_path "/test/boot_partition"
#define boot_partition_labels(fn, ...) fn(boot_partition, __VA_ARGS__)

#define slot0_partition_parent fixed_table
#define slot0_partition_mtd online
#define slot0_partition_okay 1
#define slot0_partition_label "image-0"
#define slot0_partition_path "/test/slot0_partition"
#define slot0_partition_labels(fn, ...) fn(slot0_partition, __VA_ARGS__)

#define slot1_partition_parent fixed_table
#define slot1_partition_mtd online
#define slot1_partition_okay 1
#define slot1_partition_label "image-1"
#define slot1_partition_path "/test/slot1_partition"
#define slot1_partition_labels(fn, ...) fn(slot1_partition, __VA_ARGS__)

#define storage_partition_id 4
#define storage_partition_parent fixed_table
#define storage_partition_mtd online
#define storage_partition_okay 1
#define storage_partition_label "storage"
#define storage_partition_path "/test/storage_partition"
#define storage_partition_labels(fn, ...) fn(storage_partition, __VA_ARGS__)

#define scratch_partition_id 5
#define scratch_partition_parent fixed_table
#define scratch_partition_mtd online
#define scratch_partition_okay 1
#define scratch_partition_label "scratch"
#define scratch_partition_path "/test/scratch_partition"
#define scratch_partition_labels(fn, ...) fn(scratch_partition, __VA_ARGS__)

#define slot2_partition_id 6
#define slot2_partition_parent fixed_table
#define slot2_partition_mtd online
#define slot2_partition_okay 1
#define slot2_partition_label "image-2"
#define slot2_partition_path "/test/slot2_partition"
#define slot2_partition_labels(fn, ...) fn(slot2_partition, __VA_ARGS__)

#define last_partition_id 255
#define last_partition_parent fixed_table
#define last_partition_mtd online
#define last_partition_okay 1
#define last_partition_label "last"
#define last_partition_path "/test/last_partition"
#define last_partition_labels(fn, ...) fn(last_partition, __VA_ARGS__)

#define sub_partition_id 7
#define sub_partition_parent sub_table
#define sub_partition_mtd online
#define sub_partition_okay 1
#define sub_partition_label "sub"
#define sub_partition_path "/test/sub_partition"
#define sub_partition_labels(fn, ...) fn(sub_partition, __VA_ARGS__)

#define mapped_partition_id 8
#define mapped_partition_parent fixed_table
#define mapped_partition_mtd online
#define mapped_partition_okay 1
#define mapped_partition_label "mapped"
#define mapped_partition_path "/test/mapped_partition"
#define mapped_partition_labels(fn, ...) fn(mapped_partition, __VA_ARGS__)

#define duplicate_a_id 9
#define duplicate_a_parent fixed_table
#define duplicate_a_mtd online
#define duplicate_a_okay 1
#define duplicate_a_label "duplicate"
#define duplicate_a_path "/test/duplicate_a"
#define duplicate_a_labels(fn, ...) fn(duplicate_a, __VA_ARGS__)

#define duplicate_b_id 10
#define duplicate_b_parent fixed_table
#define duplicate_b_mtd online
#define duplicate_b_okay 1
#define duplicate_b_label "duplicate"
#define duplicate_b_path "/test/duplicate_b"
#define duplicate_b_labels(fn, ...) fn(duplicate_b, __VA_ARGS__)

#define offline_partition_id 11
#define offline_partition_parent fixed_table
#define offline_partition_mtd offline
#define offline_partition_okay 1
#define offline_partition_label "offline"
#define offline_partition_path "/test/offline_partition"
#define offline_partition_labels(fn, ...) fn(offline_partition, __VA_ARGS__)

#define disabled_mapped_id 12
#define disabled_mapped_parent fixed_table
#define disabled_mapped_mtd online
#define disabled_mapped_okay 0
#define disabled_mapped_label "disabled"
#define disabled_mapped_path "/test/disabled_mapped"
#define disabled_mapped_labels(fn, ...) fn(disabled_mapped, __VA_ARGS__)

#define unlabeled_partition_id 13
#define unlabeled_partition_parent fixed_table
#define unlabeled_partition_mtd online
#define unlabeled_partition_okay 1
#define unlabeled_partition_label NULL
#define unlabeled_partition_path "/test/unlabeled_partition"
#define unlabeled_partition_labels(fn, ...)
