#
# Copyright (c) 2025 HU Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(SB_CONF_FILE ${CMAKE_CURRENT_LIST_DIR}/\${BOARD}-sysbuild.conf)

get_filename_component(CURRENT_DIR ${CMAKE_CURRENT_LIST_DIR} ABSOLUTE)

# Get the parent directory
get_filename_component(PARENT_DIR ${CURRENT_DIR} DIRECTORY)
get_filename_component(APP_NAME ${PARENT_DIR} NAME)

set(BOARD_ROOT ${PARENT_DIR})
find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sysbuild LANGUAGES)