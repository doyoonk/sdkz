# Copyright (c) 2025 HU Inc. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_MICROPYTHON AND CONFIG_MICROPYTHON_LIGHT)
  message(FATAL_ERROR "Enable only one of CONFIG_MICROPYTHON and CONFIG_MICROPYTHON_LIGHT")
endif()

if(CONFIG_MICROPYTHON)
  add_subdirectory(port)
elseif(CONFIG_MICROPYTHON_LIGHT)
  add_subdirectory(light)
endif()
