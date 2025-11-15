#
# Copyright (c) 2025 HU Inc.
#
# SPDX-License-Identifier: Apache-2.0
#

set(ZEPHYR_MCUBOOT_CMAKE_DIR  ${CMAKE_CURRENT_LIST_DIR}/mcuboot)
set(ZEPHYR_MCUBOOT_KCONFIG    ${CMAKE_CURRENT_LIST_DIR}/mcuboot/Kconfig)

foreach(module_name ${CTM_KCONFIG_EXTENDED_MODULES})
  zephyr_string(SANITIZE TOUPPER MODULE_NAME_UPPER ${module_name})
  if(${module_name} IN_LIST ZEPHYR_MODULE_NAMES)
    set(CTM_${MODULE_NAME_UPPER}_KCONFIG
        "${CMAKE_CURRENT_LIST_DIR}/${module_name}/Kconfig"
    )
  else()
    set(CTM_${MODULE_NAME_UPPER}_KCONFIG
        "${CMAKE_CURRENT_LIST_DIR}/${module_name}/Kconfig-NOTFOUND"
    )
  endif()
  list(APPEND ZEPHYR_KCONFIG_MODULES_DIR
       "CTM_${MODULE_NAME_UPPER}_KCONFIG=${CTM_${MODULE_NAME_UPPER}_KCONFIG}"
  )
endforeach()
