#
# Copyright (c) 2025 HU Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

add_subdirectory_ifdef(CONFIG_CUSTOM custom)
add_subdirectory_ifdef(CONFIG_ENET net)
add_subdirectory_ifdef(CONFIG_HU hu)

zephyr_linker_sources(ROM_SECTIONS iterables.ld)