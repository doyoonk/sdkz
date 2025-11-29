#
# Copyright (c) 2025 HU Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()

include_directories(${ZEPHYR_BASE}/samples/subsys/usb/common)
include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)

zephyr_library_sources(
    settings.c
    udp.c
    usb.c
)
