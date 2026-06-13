#
# Copyright (c) 2021 Nordic Semiconductor ASA
# Copyright (c) 2025 HU Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(app LANGUAGES C)

if (CONFIG_MICROPYTHON)

set(MICROPY_PORT_DIR    ${CMAKE_CURRENT_SOURCE_DIR}/micropython)
set(MICROPY_DIR         ${WEST_TOPDIR}/modules/lib/micropython)
set(MICROPY_TARGET      micropython)
string(TOUPPER          ZEPHYR_${BOARD} MICROPY_BOARD)

include(${MICROPY_DIR}/py/py.cmake)
include(${MICROPY_DIR}/extmod/extmod.cmake)
include(${MICROPY_DIR}/py/usermod.cmake)

list(APPEND DTS_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/dts)

# Define mpy-cross flags, for use with frozen code.
if(CONFIG_RISCV_ISA_RV32I AND CONFIG_RISCV_ISA_EXT_M AND CONFIG_RISCV_ISA_EXT_C)
    set(ARCH_FLAGS "")
    set(RV32_EXT "")
    if(CONFIG_RISCV_ISA_EXT_ZBA)
        list(APPEND RV32_EXT "zba")
    endif()
    if(CONFIG_RISCV_ISA_EXT_ZCMP)
        list(APPEND RV32_EXT "zcmp")
    endif()
    if(RV32_EXT)
        list(JOIN RV32_EXT "," RV32_EXT)
        set(ARCH_FLAGS "-march-flags=${RV32_EXT}")
    endif()
    set(MICROPY_CROSS_FLAGS "-march=rv32imc ${ARCH_FLAGS}")
elseif(CONFIG_RISCV_ISA_RV64I AND CONFIG_RISCV_ISA_EXT_M AND CONFIG_RISCV_ISA_EXT_C)
    set(MICROPY_CROSS_FLAGS -march=rv64imc)
endif()

if (CONFIG_MICROPY_FROZEN_MODULES)
    cmake_path(ABSOLUTE_PATH CONFIG_MICROPY_FROZEN_MANIFEST BASE_DIRECTORY ${CMAKE_CURRENT_LIST_DIR})
    set(MICROPY_FROZEN_MANIFEST ${CONFIG_MICROPY_FROZEN_MANIFEST})
endif()

dt_chosen(has_port PROPERTY "micropy,console")

set(MICROPY_SOURCE_PORT
    main.c
    help.c
    #machine_i2c.c
    machine_spi.c
    machine_pin.c
    machine_timer.c
    modbluetooth_zephyr.c
    modsocket.c
    modzephyr.c
    modzsensor.c
    mphalport.c
    zephyr_device.c
    zephyr_storage.c
    zephyr_filesystem.c
    mpthreadport.c
)
if (CONFIG_CONSOLE_SUBSYS AND (DEFINED has_port))
    list(APPEND MICROPY_SOURCE_PORT main.c uart_core.c)
else()
    list(APPEND MICROPY_SOURCE_PORT shellcmd.c)
endif()
list(TRANSFORM MICROPY_SOURCE_PORT PREPEND ${MICROPY_PORT_DIR}/)

set(MICROPY_SOURCE_SHARED
    libc/printf.c
    readline/readline.c
    runtime/gchelper_generic.c
    runtime/interrupt_char.c
    runtime/mpirq.c
    runtime/pyexec.c
    runtime/stdout_helpers.c
    runtime/sys_stdio_mphal.c
    timeutils/timeutils.c
)
list(TRANSFORM MICROPY_SOURCE_SHARED PREPEND ${MICROPY_DIR}/shared/)

set(MICROPY_SOURCE_DRIVERS
    bus/softspi.c
)
list(TRANSFORM MICROPY_SOURCE_DRIVERS PREPEND ${MICROPY_DIR}/drivers/)

set(MICROPY_QSTRDEFS_PORT
    ${MICROPY_PORT_DIR}/qstrdefsport.h
)

if (CONFIG_MICROPY_VFS_FAT)

list(APPEND MICROPY_SOURCE_LIB
    oofatfs/ff.c
    oofatfs/ffunicode.c
)

endif()

if (CONFIG_MICROPY_VFS_LFS1)

list(APPEND MICROPY_SOURCE_LIB
    littlefs/lfs1.c
    littlefs/lfs1_util.c
)

endif()

if (CONFIG_MICROPY_VFS_LFS2)

list(APPEND MICROPY_SOURCE_LIB
    littlefs/lfs2.c
    littlefs/lfs2_util.c
)

endif()

list(TRANSFORM MICROPY_SOURCE_LIB PREPEND ${MICROPY_DIR}/lib/)

set(MICROPY_SOURCE_QSTR
    ${MICROPY_SOURCE_PY}
    ${MICROPY_SOURCE_EXTMOD}
    ${MICROPY_SOURCE_USERMOD}
    ${MICROPY_SOURCE_SHARED}
    ${MICROPY_SOURCE_DRIVERS}
    ${MICROPY_SOURCE_LIB}
    ${MICROPY_SOURCE_PORT}
)

zephyr_get_include_directories_for_lang(C includes)
zephyr_get_system_include_directories_for_lang(C system_includes)
zephyr_get_compile_definitions_for_lang(C definitions)
zephyr_get_compile_options_for_lang(C options)

set(MICROPY_CPP_FLAGS_EXTRA ${includes} ${system_includes} ${definitions} ${options})

zephyr_library_named(${MICROPY_TARGET})

zephyr_library_include_directories(
    ${MICROPY_INC_CORE}
    ${MICROPY_PORT_DIR}
    ${MICROPY_INC_USERMOD}
    ${CMAKE_CURRENT_BINARY_DIR}
)

zephyr_library_compile_options(
    -std=gnu99 -fomit-frame-pointer
)

zephyr_library_compile_definitions(
    NDEBUG
    MP_CONFIGFILE=<${CONFIG_MICROPY_CONFIGFILE}>
    MICROPY_HEAP_SIZE=${CONFIG_MICROPY_HEAP_SIZE}
    FFCONF_H=\"${MICROPY_OOFATFS_DIR}/ffconf.h\"
    MICROPY_VFS_FAT=$<BOOL:${CONFIG_MICROPY_VFS_FAT}>
    MICROPY_VFS_LFS1=$<BOOL:${CONFIG_MICROPY_VFS_LFS1}>
    MICROPY_VFS_LFS2=$<BOOL:${CONFIG_MICROPY_VFS_LFS2}>
)

zephyr_library_sources(${MICROPY_SOURCE_QSTR})
zephyr_library_link_libraries(kernel)

include(${MICROPY_DIR}/py/mkrules.cmake)

add_dependencies(BUILD_VERSION_HEADER zephyr_generated_headers)
add_dependencies(${MICROPY_TARGET} zephyr_generated_headers)

target_sources(app PRIVATE
    ${MICROPY_PORT_DIR}/src/zephyr_start.c
)

if (NOT CONFIG_CONSOLE_SUBSYS)
target_sources(app PRIVATE
    ${MICROPY_PORT_DIR}/src/zephyr_getchar.c
)
endif()

endif()

target_sources(app PRIVATE
    src/hup_server.c
    src/main.c
    src/pwmleds.c
)

target_link_libraries(app PRIVATE ${MICROPY_TARGET})
