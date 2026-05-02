# Copyright (c) 2025 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MICROPYTHON)
  zephyr_library_named(micropython)

  zephyr_library_include_directories(include)
  zephyr_library_include_directories(${ZEPHYR_MICROPYTHON_MODULE_DIR})
  zephyr_library_include_directories(${CMAKE_BINARY_DIR})

  set(MICROPY_DIR "${ZEPHYR_MICROPYTHON_MODULE_DIR}")
  set(MICROPY_PY_DIR "${MICROPY_DIR}/py")
  set(MICROPY_TARGET micropython)

  include(${MICROPY_PY_DIR}/py.cmake)
  include(${MICROPY_DIR}/extmod/extmod.cmake)

  set(MICROPY_SOURCES
    ${MICROPY_SOURCE_PY}
    ${MICROPY_SOURCE_EXTMOD}
    ${MICROPY_DIR}/shared/runtime/pyexec.c
    ${MICROPY_DIR}/shared/runtime/gchelper_generic.c
    ${MICROPY_DIR}/shared/runtime/stdout_helpers.c
    ${MICROPY_DIR}/shared/readline/readline.c
  )

  zephyr_library_sources(${MICROPY_SOURCES})
  set(MICROPY_SOURCE_QSTR ${MICROPY_SOURCES})
  include(${MICROPY_PY_DIR}/mkrules.cmake)
  zephyr_library_sources(src/mphalport.c)
endif()
