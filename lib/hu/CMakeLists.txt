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
    palloc.c
)

zephyr_library_sources_ifdef(CONFIG_RETENTION_BOOTLOADER_INFO bootloader.c)

# Get MCUboot version from the VERSION file in the repository and create a local output header
# version file so that it can be compared against in the test
if(DEFINED CONFIG_RETENTION_BOOTLOADER_INFO)
  set(VERSION_FILE ${ZEPHYR_MCUBOOT_MODULE_DIR}/boot/zephyr/VERSION)
  set(VERSION_TYPE MCUBOOT)
  set(BUILD_VERSION_NAME MCUBOOT_BUILD_VERSION)
  include(${ZEPHYR_BASE}/cmake/modules/version.cmake)
  file(READ ${ZEPHYR_BASE}/version.h.in version_content)
  string(CONFIGURE "${version_content}" version_content)
  file(
    WRITE
    ${CMAKE_CURRENT_BINARY_DIR}/zephyr/include/generated/zephyr/mcuboot_version.h
    "${version_content}"
  )
endif()
