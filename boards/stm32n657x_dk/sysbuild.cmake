#
# Copyright (c) 2025 STMicroelectronics
# Copyright (c) 2025 HU Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

if(SB_CONFIG_BOOTLOADER_MCUBOOT)
  set_target_properties(mcuboot PROPERTIES BOARD stm32n657x_dk/stm32n657xx/fsbl)
  set(SB_CONFIG_BOOT_SIGNATURE_KEY_FILE "etc/ssl/certs/mcuboot_sign-rsa-2048.pem")
endif()
