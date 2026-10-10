/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PALLOC_TEST_LOG_H
#define PALLOC_TEST_LOG_H
#include <stdio.h>
#define LOG_MODULE_REGISTER(...)
#define LOG_INF(...) do { printf(__VA_ARGS__); putchar('\n'); } while (0)
#define LOG_ERR(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#endif
