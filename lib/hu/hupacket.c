/*
 * Copyright (c) 2025 HU Inc. All rights reserved.
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
 *  The stored body and CRC field must fit in RECV_BUFFER_SIZE-1 bytes.
 *  The last byte is reserved for NUL; oversized frames are discarded.
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
#include <stdio.h>

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

_Static_assert(RECV_BUFFER_SIZE >= 2, "HU receive buffer needs room for a terminator");

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

static bool valid_crc(struct hup_handle *h)
{
	size_t offset = h->crc16 - h->buffer;
	size_t length = h->state - offset;
	uint16_t received = 0;
	/* Wire CRCs are 1..4 hex digits, with no prefix, sign or trailing data. */
	if (length == 0 || length > 4) return false;
	for (size_t i = 0; i < length; i++) {
		unsigned char ch = h->crc16[i];
		unsigned digit;
		if (ch >= '0' && ch <= '9') digit = ch - '0';
		else if (ch >= 'a' && ch <= 'f') digit = ch - 'a' + 10;
		else if (ch >= 'A' && ch <= 'F') digit = ch - 'A' + 10;
		else return false;
		received = (received << 4) | digit;
	}
	return received == crc16(CRC16_CCITT_POLY, SEED_CRC16, h->buffer, offset - 1);
}

static void process_data(struct hup_handle* h)
{
	/* Validate the original record separators before splitting the header. */
	h->crc_match = h->crc16 != NULL && valid_crc(h);

	for (int i = 1; i < h->argc; i ++)
		*(h->argv[i] - 1) = '\0';

	seperate_header(h, &h->id, &h->argv[0], ID_MARK);
	seperate_header(h, &h->argv[0], &h->sequence, SEQUENCE_MARK);

	if (h->crc16 != NULL && !h->crc_match) {
		if (!h->response) {
			hupacket_nak_response(h, NULL, -ENMCRC16);
			hupacket_send_buffer(h, NULL);
		}
		reset_hupacket(h);
		return;
	}

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
		bool found = false;

		STRUCT_SECTION_FOREACH(hup_cmd, cmd)
		{
			if (strcmp(cmd->cmd, h->argv[0]) == 0)
			{
				found = true;
				cmd->func(h, h->argc, (const char**)h->argv);
				break;
			}
		}
		if (!found)
		{
			hupacket_nak_response(h, NULL, -ENOSYS);
			hupacket_send_buffer(h, NULL);
		}
	}
	reset_hupacket(h);
}

void reset_hupacket(void* handle)
{
	struct hup_handle* h = handle;
	h->state = HUP_STATE_NONE;
    h->argc = 0;
	h->argv[0] = NULL;
	h->crc16 = NULL;
	h->id = NULL;
    h->sequence = NULL;
	h->response = false;
	h->crc_match = false;
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
		/* CRC text cannot introduce new records or another CRC field. Keep
		 * these bytes in the CRC text so validation rejects the entire field.
		 */
		if (h->crc16 != NULL && ch != END_OF_PACKET &&
			ch != ENQ_OF_COMMAND && ch != ACK_OF_RESPONSE && ch != NAK_OF_RESPONSE) {
			if (h->state < sizeof(h->buffer) - 1)
				h->buffer[h->state++] = ch;
			else
				reset_hupacket(h);
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
			if ((size_t)h->state >= sizeof(h->buffer)) {
				reset_hupacket(h);
				break;
			}
			h->buffer[h->state] = '\0';
			process_data(h);
			break;
		case CRC_MARK:
			if (h->state < sizeof(h->buffer) - 1)
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
			if (h->argc >= ARRAY_SIZE(h->argv) ||
				h->state >= sizeof(h->buffer) - 1) {
				reset_hupacket(h);
				break;
			}
			h->argv[h->argc ++] = &h->buffer[h->state + 1];
			/* fall through */
		default:
			if (h->state < sizeof(h->buffer) - 1)
				h->buffer[h->state ++] = ch;
			else
				reset_hupacket(h);
			break;
		}
	}
}

static char *tx_destination(struct hup_handle *h, char *buffer)
{
	if (buffer != NULL && buffer != h->tx_buffer) {
		h->tx_error = -EINVAL;
		return NULL;
	}
	return h->tx_buffer;
}

static bool tx_space(struct hup_handle *h, char *buffer, size_t *length, size_t *available)
{
	if (tx_destination(h, buffer) == NULL || h->tx_error != 0) return false;
	if (h->tx_finalized) {
		h->tx_error = -EALREADY;
		return false;
	}
	/* Reserve the maximum CRC text, end marker and C-string terminator. */
	size_t tail = h->crc16 != NULL ? 7 : 2;
	*length = strnlen(h->tx_buffer, sizeof(h->tx_buffer));
	if (sizeof(h->tx_buffer) < tail || *length > sizeof(h->tx_buffer) - tail) {
		h->tx_error = -EMSGSIZE;
		return false;
	}
	*available = sizeof(h->tx_buffer) - tail - *length;
	return true;
}

void hupacket_append_str(void* handle, char* buffer, const char* str)
{
	struct hup_handle *h = handle;
	size_t length, available;
	if (!tx_space(h, buffer, &length, &available)) return;
	if (str == NULL) {
		h->tx_error = -EINVAL;
		return;
	}
	size_t count = strnlen(str, available + 1);
	if (count > available) {
		h->tx_error = -EMSGSIZE;
		return;
	}
	memmove(h->tx_buffer + length, str, count);
	h->tx_buffer[length + count] = '\0';
}

void hupacket_append_hex_bytes(void *handle, char *buffer, const uint8_t *data, size_t size)
{
	struct hup_handle *h = handle;
	static const char hex[] = "0123456789abcdef";
	size_t length, available;
	if (!tx_space(h, buffer, &length, &available)) return;
	if (data == NULL && size != 0) {
		h->tx_error = -EINVAL;
		return;
	}
	if (size > available / 2) {
		h->tx_error = -EMSGSIZE;
		return;
	}
	for (size_t i = 0; i < size; i++) {
		h->tx_buffer[length++] = hex[data[i] >> 4];
		h->tx_buffer[length++] = hex[data[i] & 15];
	}
	h->tx_buffer[length] = '\0';
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
{                   // 12,345,678,9012
	char buf[12];	// -2,147,483,647\x00
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
	h->tx_error = 0;
	h->tx_finalized = false;
	buffer = tx_destination(h, buffer);
	if (buffer == NULL) return;
	*buffer = '\0';
	if (h->argc < 1 || h->argv[0] == NULL) {
		h->tx_error = -EINVAL;
		return;
	}
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
	size_t length, available;
	int construction_error = 0;
	if (!tx_space(h, buffer, &length, &available)) {
		if (h->tx_error != -EMSGSIZE || h->response) return h->tx_error;
		construction_error = -EMSGSIZE;
		hupacket_nak_response(h, NULL, -EMSGSIZE);
		if (!tx_space(h, NULL, &length, &available)) return h->tx_error;
	}
	if (length == 0 || h->send == NULL) return -EINVAL;
	char trailer[7];
	int count;
	if (h->crc16 != NULL) {
		uint16_t crc = crc16(CRC16_CCITT_POLY, SEED_CRC16, h->tx_buffer + 1, length - 1);
		count = snprintf(trailer, sizeof(trailer), "%c%x%c", CRC_MARK, (unsigned)crc, END_OF_PACKET);
	} else {
		trailer[0] = END_OF_PACKET;
		trailer[1] = '\0';
		count = 1;
	}
	memcpy(h->tx_buffer + length, trailer, count + 1);
	h->tx_finalized = true;
	int rc = h->send(h->user_data, (const uint8_t *)h->tx_buffer, length + count);
	return rc < 0 ? rc : (construction_error != 0 ? construction_error : rc);
}
