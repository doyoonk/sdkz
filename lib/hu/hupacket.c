/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 
 * Packet: ENQ record_data RS record_data RS ... [EOT CRC16] ETX
 * 	- CRC16: hex string
 *  - example packet: flash = offset, binary size, data(ascii85 encoded) for binary
 *    \x0519@flash:21345\x1eslot0\x1e10000\x1e100\x1e_ascii85 encoded_\x1aCRC16\x03
 *    --->|***********************************************************|<--- crc16
 *       write flash: partition=slot0, offset=0x10000, size=256
 *          size: binary size !!) NOT ascii85 encoded data size
 *       CRC16: for usart
 *       19: id for rs485
 *       21345: sequence for udp(optional)
 *
 *  !!) The packet length must be smaller than DATA_BUFFER_SIZE.
 *      Otherwise, the packet will be ignored.
 * 
 * Response: NAK/ACK record_status RS data RS record_data RS ... [EOT CRC16] ETX
 * 	- CRC16: hex string(ex: A05A), if the received packet contains crc16
 *  - example packet: flash = offset, binary size, 
 *    \x1519@flash:21345\x1e-5\x1eslot0\x1e10000\x1e100\x1aCRC16\x03
 *    --->|********************************************|<--- crc16
 *  - ACK/NAK: status of response packet
 *  - id, sequence: If the id/sequence is also included in the received packet
 *  - status: -5(-EIO)
 */

#include <hu/hupacket.h>
#include <hu/palloc.h>

#include <zephyr/sys/crc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/iterable_sections.h>

#include <zephyr/logging/log.h>

#include <huerrno.h>
#include <string.h>
#include <stdlib.h>

extern char* itoa (int value, char *str,int base);

LOG_MODULE_REGISTER(hup, CONFIG_LOG_DEFAULT_LEVEL);

#define HUP_STATE_NONE  -1

// command
#define ENQ_OF_COMMAND	0x05
// response
#define ACK_OF_RESPONSE	0x06
#define NAK_OF_RESPONSE	0x15
// common
#define END_OF_PACKET	0x04
#define CRC_MARK		0x1a
#define RECORD_MARK		0x1e

#define ID_MARK			'@'
#define SEQUENCE_MARK	':'

#define SEED_CRC16	0x0000


void* init_hupacket(void* h, send_func send, void* user_data)
{
	struct hup_handle* hup = (struct hup_handle*)h;
	if (hup == NULL)
		hup = (struct hup_handle*)palloc(sizeof(struct hup_handle));
	if (hup != NULL)
	{
		memset(hup, 0, sizeof(struct hup_handle));
		reset_hupacket(hup);
		hup->user_data = user_data;
		hup->send = send;
	}
	return hup;
}

void deinit_hupacket(void* h)
{
	if (h != NULL)
		pfree(h);
}

static void seperate_header(struct hup_handle* h, char** ptr, char** dst, char ch)
{
	char* tmp;
	if ((tmp = strchr(h->argv[0], ch)) != NULL)
	{
		*ptr = h->argv[0];
		*tmp = '\0';

		*dst = tmp + 1;
	}
}

static void process_data(struct hup_handle* h)
{
	if (h->crc16 != NULL)
	{
		uint16_t crc_calculated = crc16(CRC16_CCITT_POLY, SEED_CRC16
			, h->buffer, (size_t)h->crc16 - (size_t)h->buffer - 1);
		uint16_t crc_received = strtoul(h->crc16, NULL, 16);
		h->crc_match = crc_calculated == crc_received;
		if (!h->crc_match)
		{
			if (!h->response)
			{
				hupacket_nak_response(h, h->tx_buffer, -ENMCRC16);
				hupacket_send_buffer(h, h->tx_buffer);
				return;
			}
		}
	}

	for (int i = 1; i < h->argc; i ++)
		*(h->argv[i] - 1) = '\0';

	seperate_header(h, &h->id, &h->argv[0], ID_MARK);
	seperate_header(h, &h->argv[0], &h->sequence, SEQUENCE_MARK);

	if (h->response) {
		STRUCT_SECTION_FOREACH(hup_resp, cmd)
		{
			if (strcmp(cmd->cmd, h->argv[0]) == 0)
			{
				cmd->func(h, h->argc, (const char**)h->argv, h->enduser_data);
				break;
			}
		}
	}
	else
	{
		STRUCT_SECTION_FOREACH(hup_cmd, cmd)
		{
			if (strcmp(cmd->cmd, h->argv[0]) == 0)
			{
				cmd->func(h, h->argc, (const char**)h->argv);
				break;
			}
		}
	}
	reset_hupacket(h);
}

void reset_hupacket(void* handle)
{
	struct hup_handle* h = handle;
	h->state = HUP_STATE_NONE;
    h->argc = 0;
	h->crc16 = NULL;
	h->id = NULL;
    h->sequence = NULL;
}

void process_hupacket(void* handle, uint8_t* data, size_t data_len)
{
	struct hup_handle* h = handle;
	while (data_len > 0)
	{
		uint8_t ch = *data ++;
		data_len --;

		if (h->state == HUP_STATE_NONE)
		{
			if (ch != ENQ_OF_COMMAND &&
				ch != ACK_OF_RESPONSE &&
				ch != NAK_OF_RESPONSE)
				continue;
		}
		switch(ch)
		{
		case ENQ_OF_COMMAND:
		case ACK_OF_RESPONSE:
		case NAK_OF_RESPONSE:
			reset_hupacket(h);	
			h->state ++;
			h->response = ch != ENQ_OF_COMMAND;
			h->argv[h->argc ++] = &h->buffer[h->state];
			break;
		case END_OF_PACKET:
			h->buffer[h->state] = '\0';
			process_data(h);
			break;
		case CRC_MARK:
			if (h->state < sizeof(h->buffer))
			{
				h->buffer[h->state ++] = '\0';
				h->crc16 = &h->buffer[h->state];
			}
			else
			{
				reset_hupacket(h);
			}
			break;
		case RECORD_MARK:
			h->argv[h->argc ++] = &h->buffer[h->state + 1];
		default:
			if (h->state < sizeof(h->buffer))
				h->buffer[h->state ++] = ch;
			else
				reset_hupacket(h);
			break;
		}
	}
}

void hupacket_append_str(void* handle, char* buffer, const char* str)
{
	struct hup_handle* h = handle;
	if (buffer == NULL)
		buffer = &h->tx_buffer[0];
	strcat(buffer, str);
}

void hupacket_append_char(void* h, char* buffer, const char ch)
{
	if (ch != 0)
	{
		char sch[2] = { ch, '\0' };
		hupacket_append_str(h, buffer, sch);
	}
}
void hupacket_append_int(void* h, char* buffer, const int val)
{                   // 123456789012
	char buf[12];	// -2147483647\x00
	char* ptr = itoa(val, buf, 10);
	if (ptr != NULL)
		hupacket_append_str(h, buffer, buf);
}
void hupacket_append_hex(void* h, char* buffer, const uint32_t val)
{                   // 123456789
	char buf[9];	// FFFFFFFF\x00
	char* ptr = itoa(val, buf, 16);
	if (ptr != NULL)
		hupacket_append_str(h, buffer, buf);
}


void hupacket_record_str(void* h, char* buffer, const char* const str)
{
	hupacket_append_char(h, buffer, RECORD_MARK);
	hupacket_append_str(h, buffer, str);
}
void hupacket_record_int(void* h, char* buffer, const int val)
{
	hupacket_append_char(h, buffer, RECORD_MARK);
	hupacket_append_int(h, buffer, val);
}
void hupacket_record_hex(void* h, char* buffer, const uint32_t val)
{
	hupacket_append_char(h, buffer, RECORD_MARK);
	hupacket_append_hex(h, buffer, val);
}


void hupacket_reset_buffer(void* handle, char* buffer, char stx)
{
	struct hup_handle* h = handle;
	if (buffer == NULL)
		buffer = &h->tx_buffer[0];
	*buffer = '\0';
	hupacket_append_char(h, buffer, stx);
	if (h->id)
	{
		hupacket_append_str(h, buffer, h->id);
		hupacket_append_char(h, buffer, ID_MARK);
	}
	hupacket_append_str(h, buffer, h->argv[0]);
	if (h->sequence)
	{
		hupacket_append_char(h, buffer, SEQUENCE_MARK);
		hupacket_append_str(h, buffer, h->sequence);
	}
}
void hupacket_ack_response(void* h, char* buffer)
{
	hupacket_reset_buffer(h, buffer, ACK_OF_RESPONSE);
	hupacket_record_int(h, buffer, NOERROR);
}
void hupacket_nak_response(void* h, char* buffer, int rc)
{
	hupacket_reset_buffer(h, buffer, NAK_OF_RESPONSE);
	hupacket_record_int(h, buffer, rc);
}

int hupacket_send_buffer(void* handle, char* buffer)
{
	struct hup_handle* h = handle;
	if (buffer == NULL)
		buffer = &h->tx_buffer[0];

	if (h->crc16 != NULL)
	{
		char* ptr = buffer + 1;
		uint16_t crc_calculated = crc16(CRC16_CCITT_POLY, SEED_CRC16, ptr, strlen(ptr));
		hupacket_append_char(h, buffer, CRC_MARK);
		hupacket_append_hex(h, buffer, crc_calculated);
	}
	hupacket_append_char(h, buffer, END_OF_PACKET);
	return h->send(h->user_data, buffer, strlen(buffer));
}
