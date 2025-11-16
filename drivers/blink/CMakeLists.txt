#
# Copyright (c) 2024 Nordic Semiconductor ASA
# Copyright (c) 2025 HU Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

zephyr_library()
zephyr_library_sources_ifdef(CONFIG_BLINK_GPIO_LED gpio_led.c)
