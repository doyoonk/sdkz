/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#define STRUCT_SECTION_ITERABLE(type, name) struct type name
#define STRUCT_SECTION_FOREACH(type, it) \
    for (struct type *it=type##_items; it<type##_items+type##_count; it++)
