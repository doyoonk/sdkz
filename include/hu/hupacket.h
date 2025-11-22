/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __HUPACKET_H__
#define __HUPACKET_H__

#include "common.h"
#include <zephyr/sys/iterable_sections.h>

#define RECV_BUFFER_SIZE    CONFIG_HU_PACKET_SIZE

#ifdef __cplusplus
extern "C"
{
#endif

typedef int (*send_func)(char* buffer, int size, void* user_data);

struct hup_handle
{
    int argc;
    char* argv[64];
    
    int state;
    char buffer[RECV_BUFFER_SIZE];

    char* id;
    char* sequence;
    bool crc16;

    char tx_buffer[CONFIG_HU_PACKET_SIZE];
    void* user_data;
    send_func send;
};

struct hup_cmd
{
	const char* cmd;
	void (*func)(struct hup_handle* h, bool crc, void* user_data);
};
#define DEFINE_HUP_CMD(name, command, function) STRUCT_SECTION_ITERABLE(hup_cmd, name) = \
{ \
    .cmd = command, \
    .func = function \
}

void init_hupacket(struct hup_handle* h, send_func send, void* user_data);
void reset_hupacket(struct hup_handle* h);
void process_hupacket(struct hup_handle* h, uint8_t* data, size_t data_len);

void hupacket_reset_txbuffer(struct hup_handle* h);
void hupacket_append_str(struct hup_handle* h, char* buffer, const char* str);
void hupacket_append_char(struct hup_handle* h, char* buffer, const char ch);
void hupacket_append_int(struct hup_handle* h, char* buffer, const int val);

void hupacket_record_str(struct hup_handle* h, char* buffer, const char* const str);
void hupacket_record_int(struct hup_handle* h, char* buffer, const int val);
void hupacket_record_hex(struct hup_handle* h, char* buffer, const int val);


#ifdef __cplusplus
}
#endif

#endif // __HUPACKET_H__
