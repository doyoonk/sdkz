/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __HUPACKET_H__
#define __HUPACKET_H__

#include "common.h"
#include <zephyr/sys/iterable_sections.h>
#include <zephyr/posix/unistd.h>

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RECV_BUFFER_SIZE    CONFIG_HU_PACKET_SIZE

#ifdef __cplusplus
extern "C"
{
#endif

typedef ssize_t (*send_func)(void* h, const uint8_t* buffer, size_t size);
struct hup_handle
{
    int argc;
    char* argv[64];
    
    int state;
    char buffer[RECV_BUFFER_SIZE];
    bool response;
    bool crc_match;

    char* id;
    char* sequence;
    char* crc16;
    void* enduser_data;

    char tx_buffer[CONFIG_HU_PACKET_SIZE];
    int tx_error;
    bool tx_finalized;
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
void deinit_hupacket(void* h);
void reset_hupacket(void* h);
/* At most RECV_BUFFER_SIZE-1 stored bytes (body plus CRC field), reserving
 * the final byte for NUL. At most 64 records, including the command header.
 * Oversized frames are discarded; a new ENQ/ACK/NAK starts reception again.
 */
void process_hupacket(void* h, uint8_t* data, size_t data_len);


/* Builders accept NULL or this handle's tx_buffer only; other buffers have
 * no known capacity and are rejected. Start each response with reset/ACK/NAK.
 * On error, further appends do nothing until a new response is started.
 */
void hupacket_append_str(void* h, char* buffer, const char* str);
void hupacket_append_char(void* h, char* buffer, const char ch);
void hupacket_append_int(void* h, char* buffer, const int val);
void hupacket_append_hex(void* h, char* buffer, const uint32_t val);
void hupacket_append_hex_bytes(void* h, char* buffer, const uint8_t* data, size_t size);


void hupacket_record_str(void* h, char* buffer, const char* const str);
void hupacket_record_int(void* h, char* buffer, const int val);
void hupacket_record_hex(void* h, char* buffer, const uint32_t val);


void hupacket_reset_buffer(void* h, char* buffer, char res);
void hupacket_ack_response(void* h, char* buffer);
void hupacket_nak_response(void* h, char* buffer, int rc);

/* Returns the transport result, or a negative construction/transport error.
 * On overflow, sends an EMSGSIZE NAK if the complete original header fits;
 * otherwise sends nothing. Never sends a truncated response. A transmitted
 * buffer cannot be appended to or sent again until reset/ACK/NAK is called.
 */
int hupacket_send_buffer(void* h, char* buffer);

#ifdef __cplusplus
}
#endif

#endif // __HUPACKET_H__
