# Copyright (c) 2025 HU Inc. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_STM32N6_EXTRA_SRAM stm32n6_extra_sram.c)
