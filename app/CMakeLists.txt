#
# Copyright (c) 2021 Nordic Semiconductor ASA
# Copyright (c) 2025 HU Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

target_sources(app PRIVATE
    src/camera.c
    src/hup_server.c
    src/main.c
    src/pwmleds.c
)

if(CONFIG_APP_CAMERA_BARCODE OR (CONFIG_ZXC_USE_HEAP AND
   CONFIG_ZXC_ALLOCATOR_CUSTOM AND CONFIG_VIDEO_BUFFER_USE_SHARED_MULTI_HEAP))
    target_sources(app PRIVATE src/scanner_zxc.c)
endif()

target_sources_ifdef(CONFIG_APP_HEAP_SELF_TEST app PRIVATE src/heap_test.c)
