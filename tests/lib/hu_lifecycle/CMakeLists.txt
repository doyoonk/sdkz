# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
project(hu_lifecycle_tests C)
find_package(Threads REQUIRED)
enable_testing()
foreach(name server uart udp udp_sessions protocol protocol_no_ex bounds
    protocol_wp protocol_wp_no_disable protocol_wp_no_layout protocol_wp_no_ex)
  if(name MATCHES "^protocol")
    add_executable(${name}_test protocol.c)
  else()
    add_executable(${name}_test ${name}.c)
  endif()
  target_include_directories(${name}_test PRIVATE include ../../../include)
  target_compile_definitions(${name}_test PRIVATE CONFIG_HU_APP=1 CONFIG_HU_PACKET_SIZE=1536
    CONFIG_UART_INTERRUPT_DRIVEN=1 CONFIG_UART_ASYNC_API=1 CONFIG_NET_L2_ETHERNET=1 __ELASTERROR=2000)
  target_compile_definitions(${name}_test PRIVATE CONFIG_HU_UDP_MAX_SESSIONS=4
    CONFIG_HU_UDP_SESSION_TIMEOUT_MS=30000)
  target_compile_options(${name}_test PRIVATE -Wall -Wextra -Wno-unused-parameter
    -Wno-sign-compare -Wno-pointer-to-int-cast -Wno-pointer-sign -Werror -UNDEBUG
    -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(${name}_test PRIVATE -fsanitize=address,undefined)
  target_link_libraries(${name}_test PRIVATE Threads::Threads)
  add_test(NAME ${name}_test COMMAND ${name}_test)
  set_tests_properties(${name}_test PROPERTIES TIMEOUT 15)
endforeach()
# This integration test uses the real protocol declarations/implementation.
target_include_directories(udp_sessions_test BEFORE PRIVATE ../../../include)
target_include_directories(bounds_test BEFORE PRIVATE ../../../include)
foreach(name protocol protocol_no_ex protocol_wp protocol_wp_no_disable
    protocol_wp_no_layout protocol_wp_no_ex)
  target_include_directories(${name}_test BEFORE PRIVATE ../../../include)
  target_compile_definitions(${name}_test PRIVATE CONFIG_RETENTION_BOOTLOADER_INFO=1)
endforeach()

foreach(name protocol_wp protocol_wp_no_disable protocol_wp_no_layout protocol_wp_no_ex)
  # Use Zephyr's real STM32 extension declarations with the mock base flash API.
  target_include_directories(${name}_test PRIVATE ../../../../zephyr/include)
  target_compile_definitions(${name}_test PRIVATE CONFIG_FLASH_STM32_WRITE_PROTECT=1)
  if(NOT name STREQUAL "protocol_wp_no_layout")
    target_compile_definitions(${name}_test PRIVATE CONFIG_FLASH_PAGE_LAYOUT=1)
  endif()
  if(NOT name STREQUAL "protocol_wp_no_ex")
    target_compile_definitions(${name}_test PRIVATE CONFIG_FLASH_EX_OP_ENABLED=1)
  endif()
endforeach()
target_compile_definitions(protocol_wp_no_disable_test PRIVATE
  CONFIG_FLASH_STM32_WRITE_PROTECT_DISABLE_PREVENTION=1)
target_compile_definitions(protocol_test PRIVATE CONFIG_FLASH_HAS_EX_OP=1)
target_compile_definitions(protocol_no_ex_test PRIVATE CONFIG_FLASH_HAS_EX_OP=0)
