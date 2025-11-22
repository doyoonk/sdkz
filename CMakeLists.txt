#
# Copyright (c) 2021 Nordic Semiconductor ASA
# Copyright (c) 2025 HU Inc.
#
# SPDX-License-Identifier: Apache-2.0
#
# This CMake file is picked by the Zephyr build system because it is defined
# as the module CMake entry point (see zephyr/module.yml).

# Point to NCS root directory.
set(SDK_DIR ${CMAKE_CURRENT_LIST_DIR} CACHE PATH "SDK root directory")

# We need to cache the build type so that it is not out of sync when CONF_FILE
# is cached.
set(CONF_FILE_BUILD_TYPE ${CONF_FILE_BUILD_TYPE} CACHE INTERNAL "The build type")

foreach(runner_ext BIN HEX ELF)
  zephyr_get(NCS_RUNNER_${runner_ext} SYSBUILD)
  if(DEFINED NCS_RUNNER_${runner_ext})
    string(TOLOWER "${runner_ext}" type)
    set_target_properties(runners_yaml_props_target PROPERTIES
                          "${type}_file" "${NCS_RUNNER_${runner_ext}}"
    )
  endif()
endforeach()

# This is needed so that custom driver classes using system calls are taken into
# account
zephyr_syscall_include_directories(include)

zephyr_include_directories(include)

add_subdirectory(drivers)
add_subdirectory(lib)
