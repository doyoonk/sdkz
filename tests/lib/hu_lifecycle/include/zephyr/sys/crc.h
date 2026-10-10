/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once
#include <stdint.h>
#include <stddef.h>
#define CRC16_CCITT_POLY 0x1021
static inline uint16_t crc16(uint16_t poly,uint16_t crc,const uint8_t *s,size_t n) {
    while(n--) {
        crc^=(uint16_t)*s++<<8;
        for(int j=0;j<8;j++) crc=crc&0x8000 ? (crc<<1)^poly : crc<<1;
    }
    return crc;
}
