/*
 * Copyright (c) 2025 HU Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* 
 * ASCII85:
 *
 * Packet: STX record_data RS record_data RS ... [EOT CRC16] ETX
 * 	- CRC16: hex string(ex: A05A)
 *  - example packet: flash = offset, binary size, data(ascii85 encoded) for binary
 *    \x0219@flash:21345\x1eslot0\x1e10000\x1e200\x1e___ascii85 encoded data for 512 bytes___\x04CRC16\x03
 *    --->|******************************************************************************|<--- crc16
 *       write flash: partition=slot0, offset=0x10000, size=0x512
 *          size: binary size !!) NOT ascii85 encoded data size
 *       CRC16: for usart
 *       19: id for rs485/CAN
 *       21345: sequence for udp(optional)
 *
 *  !!) The packet length must be smaller than DATA_BUFFER_SIZE.
 *      Otherwise, the packet will be ignored.
 * 
 * Response: STX record_data RS record_data RS ... [EOT CRC16] ETX
 * 	- CRC16: hex string(ex: A05A), if the received packet contains crc16
 *  - example packet: flash = offset, binary size, 
 *    \x0219@flash:21345\x1eslot0\x1e10000\x1e200\x04CRC16\x03
 *    --->|***********************************************|<--- crc16
 *  - id, sequence: If the id/sequence is also included in the received packet
 */

#include <hu/hupacket.h>

#include <zephyr/sys/crc.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/iterable_sections.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(hup, CONFIG_LOG_DEFAULT_LEVEL);

#include <string.h>
#include <stdlib.h>
extern char* itoa (int value, char *str,int base);

#define HUP_STATE_NONE         -1

#define START_OF_PACKET	0x02
#define END_OF_PACKET	0x03
#define CRC_MARK		0x04
#define RECORD_MARK		0x1e

#define ID_MARK			'@'
#define SEQUENCE_MARK	':'

#define PRESET_VALUE	0x0000


void init_hupacket(struct hup_handle* h, send_func send, void* user_data)
{
	memset(h, 0, sizeof(struct hup_handle));
	reset_hupacket(h);
	h->user_data = user_data;
	h->send = send;
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
	bool crc_matched = true;
	if (h->crc16)
	{
		uint16_t crc_calculated = crc16(CRC16_CCITT_POLY, PRESET_VALUE, h->buffer, h->state);
		uint16_t crc_received = strtoul(h->argv[h->argc - 1], NULL, 16);
		if (crc_calculated != crc_received)
			crc_matched = false;
	}

	for (int i = 1; i < h->argc; i ++)
		*(h->argv[i] - 1) = '\0';

	seperate_header(h, &h->id, &h->argv[0], ID_MARK);
	seperate_header(h, &h->argv[0], &h->sequence, SEQUENCE_MARK);

	STRUCT_SECTION_FOREACH(hup_cmd, cmd)
	{
		if (strcmp(cmd->cmd, h->argv[0]) == 0)
		{
			hupacket_reset_txbuffer(h);
			cmd->func(h, crc_matched, h->user_data);
			break;
		}
	}
	reset_hupacket(h);
}

void reset_hupacket(struct hup_handle* h)
{
	h->state = HUP_STATE_NONE;
    h->argc = 0;
	h->crc16 = false;
	h->id = NULL;
    h->sequence = NULL;
}

void process_hupacket(struct hup_handle* h, uint8_t* data, size_t data_len)
{
	while (data_len > 0)
	{
		uint8_t ch = *data ++;
		data_len --;

		if (h->state == HUP_STATE_NONE)
		{
			if (ch != START_OF_PACKET)
				continue;
			h->state ++;
			h->argv[h->argc ++] =	&h->buffer[h->state];
		}
		else
		{
			switch(ch)
			{
			case CRC_MARK:
				h->argv[h->argc ++] =	&h->buffer[h->state];
				h->crc16 = true;
				break;
			case END_OF_PACKET:
				h->buffer[h->state] = '\0';
				process_data(h);
				break;
			case RECORD_MARK:
				h->argv[h->argc ++] =	&h->buffer[h->state + 1];
			default:
				if (h->state < sizeof(h->buffer))
					h->buffer[h->state ++] = ch;
				else
					reset_hupacket(h);
				break;
			}
		}
	}
}

void hupacket_reset_txbuffer(struct hup_handle* h)
{
	h->tx_buffer[0] = '\0';
	if (h->id)
	{
		hupacket_append_str(h, h->tx_buffer, h->id);
		hupacket_append_char(h, h->tx_buffer, ID_MARK);
	}
	hupacket_append_str(h, h->tx_buffer, h->argv[0]);
	if (h->sequence)
	{
		hupacket_append_char(h, h->tx_buffer, SEQUENCE_MARK);
		hupacket_append_str(h, h->tx_buffer, h->sequence);
	}
}

void hupacket_append_str(struct hup_handle* h, char* buffer, const char* str)
{
	strcat(buffer, str);
}
void hupacket_append_char(struct hup_handle* h, char* buffer, const char ch)
{
	if (ch != 0) {
		char sch[2] = { ch, '\0' };
		hupacket_append_str(h, buffer, sch);
	}
}

void hupacket_append_int(struct hup_handle* h, char* buffer, const int val)
{
	char buf[12];	// -2147483647\x00
	char* ptr = itoa(val, buf, 10);
	if (ptr != NULL)
		hupacket_append_str(h, buffer, buf);
}

void hupacket_append_hex(struct hup_handle* h, char* buffer, const int val)
{
	char buf[9];	// FFFFFFFF\x00
	char* ptr = itoa(val, buf, 16);
	if (ptr != NULL)
		hupacket_append_str(h, buffer, buf);
}


void hupacket_record_str(struct hup_handle* h, char* buffer, const char* const str)
{
	hupacket_append_char(h, buffer, RECORD_MARK);
	hupacket_append_str(h, buffer, str);
}

void hupacket_record_int(struct hup_handle* h, char* buffer, const int val)
{
	hupacket_append_char(h, buffer, RECORD_MARK);
	hupacket_append_int(h, buffer, val);
}

void hupacket_record_hex(struct hup_handle* h, char* buffer, const int val)
{
	hupacket_append_char(h, buffer, RECORD_MARK);
	hupacket_append_hex(h, buffer, val);
}
