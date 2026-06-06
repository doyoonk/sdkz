/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/linker/iterable_sections.h>
ITERABLE_SECTION_ROM(hup_cmd, Z_LINK_ITERABLE_SUBALIGN)
ITERABLE_SECTION_ROM(hup_resp, Z_LINK_ITERABLE_SUBALIGN)
