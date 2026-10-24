# Copyright (c) 2025 HU Inc. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

zephyr_library()
if(CONFIG_STM32N6_EXTRA_SRAM OR CONFIG_STM32N6_SYSTEM_SRAM)
  zephyr_library_sources(stm32n6_extra_sram.c)
endif()
