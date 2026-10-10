/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <stdio.h>
#define LOG_MODULE_REGISTER(...)
#define LOG_ERR(...) do { fprintf(stderr,__VA_ARGS__);fputc('\n',stderr); } while(0)
#define LOG_DBG(...)
#define LOG_INF(...)
