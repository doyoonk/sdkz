#
# Copyright (c) 2021 Nordic Semiconductor ASA
# Copyright (c) 2025 HU Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
# This CMake file is picked by the Zephyr build system because it is defined
# as the module CMake entry point (see zephyr/module.yml).

# Point to SDK root directory.
set(SDK_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE PATH "SDK root directory")

# We need to cache the build type so that it is not out of sync when CONF_FILE
# is cached.
set(CONF_FILE_BUILD_TYPE ${CONF_FILE_BUILD_TYPE} CACHE INTERNAL "The build type")

foreach(runner_ext BIN HEX ELF)
  zephyr_get(SDK_RUNNER_${runner_ext} SYSBUILD)
  if(DEFINED SDK_RUNNER_${runner_ext})
    string(TOLOWER "${runner_ext}" type)
    set_target_properties(runners_yaml_props_target PROPERTIES
                          "${type}_file" "${SDK_RUNNER_${runner_ext}}"
    )
  endif()
endforeach()

# This is needed so that custom driver classes using system calls are taken into
# account
zephyr_syscall_include_directories(include)

zephyr_include_directories(include)

add_subdirectory(drivers)
add_subdirectory(lib)

# The SoC's post-build command has already created the STM32 FSBL header.
# Keep a HEX equivalent at the boot partition's flash address, not its RAM VMA.
if(CONFIG_MCUBOOT AND CONFIG_SOC_SERIES_STM32N6X AND
   CONFIG_TRUSTED_EXECUTION_SECURE AND NOT CONFIG_BOOTLOADER_MCUBOOT)
  if(signing_tool_find AND EXISTS "${signing_tool_find}")
    dt_nodelabel(sdk_boot_partition NODELABEL "boot_partition" REQUIRED)
    dt_reg_addr(sdk_boot_address PATH "${sdk_boot_partition}" REQUIRED)
    set(sdk_signed_image "${PROJECT_BINARY_DIR}/${CONFIG_KERNEL_BIN_NAME}.signed")
    set_property(GLOBAL APPEND PROPERTY extra_post_build_commands
      COMMAND ${CMAKE_OBJCOPY} -I binary -O ihex --change-addresses=${sdk_boot_address}
              ${sdk_signed_image}.bin ${sdk_signed_image}.hex
    )
    set_property(GLOBAL APPEND PROPERTY extra_post_build_byproducts ${sdk_signed_image}.hex)
    set_property(TARGET runners_yaml_props_target PROPERTY hex_file
                 ${CONFIG_KERNEL_BIN_NAME}.signed.hex)
  endif()
endif()
