/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __HUPACKET_H__
#define __HUPACKET_H__

#include "common.h"
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/posix/unistd.h>


#define RECV_BUFFER_SIZE    CONFIG_HU_PACKET_SIZE

#ifdef __cplusplus
extern "C"
{
#endif

typedef ssize_t (*send_func)(const void* buffer, size_t size, void* user_data);
struct hup_handle
{
    int argc;
    char* argv[64];
    
    int state;
    char buffer[RECV_BUFFER_SIZE];
    bool response;

    char* id;
    char* sequence;
    char* crc16;
    void* enduser_data;

    char tx_buffer[CONFIG_HU_PACKET_SIZE];
    void* user_data;
    send_func send;
};

struct hup_cmd
{
	const char* cmd;
	void (*func)(void* h, int argc, const char* argv[]);
};
#define DEFINE_HUP_CMD(name, command, function) STRUCT_SECTION_ITERABLE(hup_cmd, name) = \
{ \
    .cmd = command, \
    .func = function \
}

struct hup_resp
{
	const char* cmd;
	void (*func)(void* h, int argc, const char* argv[], void* enduser_data);
};
#define DEFINE_HUP_RESP(name, command, function) STRUCT_SECTION_ITERABLE(hup_resp, name) = \
{ \
    .cmd = command, \
    .func = function \
}

void* init_hupacket(void* h, send_func send, void* user_data);
void reset_hupacket(void* h);
void process_hupacket(void* h, uint8_t* data, size_t data_len);

void hupacket_reset_buffer(void* h, char* buffer, char res);

void hupacket_append_str(void* h, char* buffer, const char* str);
void hupacket_append_char(void* h, char* buffer, const char ch);
void hupacket_append_int(void* h, char* buffer, const int val);
void hupacket_append_hex(void* h, char* buffer, const uint32_t val);

void hupacket_record_str(void* h, char* buffer, const char* const str);
void hupacket_record_int(void* h, char* buffer, const int val);
void hupacket_record_hex(void* h, char* buffer, const uint32_t val);


void hupacket_ack_response(void* h, char* buffer);
void hupacket_nak_response(void* h, char* buffer);

int hupacket_send_buffer(void* h, char* buffer);

#ifdef __cplusplus
}
#endif

#endif // __HUPACKET_H__
