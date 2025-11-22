#
# Copyright (c) 2025 HU Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_HU_PACKET
    hupacket.c
    huflash.c
    ascii85.c
)
