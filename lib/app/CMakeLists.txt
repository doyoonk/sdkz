#
# Copyright (c) 2025 HU Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()

include_directories(${ZEPHYR_BASE}/samples/subsys/usb/common)
include_directories(${ZEPHYR_BASE}/samples/net/common)
include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
include(${ZEPHYR_BASE}/samples/net/common/common.cmake)

zephyr_library_sources_ifdef(CONFIG_HU_APP
    bbram.c
    bootloader.c
    uart.c
    udp.c
    usb.c
)
