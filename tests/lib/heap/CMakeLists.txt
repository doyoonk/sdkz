# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(app_heap_tests C)
target_sources(app PRIVATE main.c ../../../app/src/heap_test.c)
target_include_directories(app PRIVATE ../../../app/src)
