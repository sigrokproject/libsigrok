/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Eva Kissling <eva.kissling@bluewin.ch>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#ifdef _WIN32
#define _WIN32_WINNT 0x0501
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <string.h>
#include <unistd.h>

#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include <errno.h>
#include "protocol.h"

#include <sys/ioctl.h>

#define BUFFER_SIZE 4

/* Top-level command opcodes */
#define CMD_SET_TRIGGER            0x00
#define CMD_CFG_TRIGGER            0xF0
#define CMD_CFG_LA                 0x0F
#define CMD_START                  0xFE
#define CMD_RESET                  0xEE

#define CMD_GET_BUS_WIDTHS         0xAA
#define CMD_GET_LA_ID              0xBB
#define CMD_ESCAPE                 0x55

/* Trigger subfunction command opcodes */
#define CMD_TRIG_MASKS             0xF1
#define CMD_TRIG_MASK              0xF3
#define CMD_TRIG_VALUE             0xF7

#define CMD_TRIG_MASKS_LAST        0xF9
#define CMD_TRIG_MASK_LAST         0xFB
#define CMD_TRIG_VALUE_LAST        0xFF

#define CMD_TRIG_SELECT_EDGE_MASK  0xF5
#define CMD_TRIG_SET_EDGE_MASK     0xF6

/* LA subfunction command opcodes */
#define CMD_LA_DELAY               0x1F

SR_PRIV int data_available(struct ipdbg_la_tcp *tcp)
{
#ifdef __WIN32__
	ioctlsocket(tcp->socket, FIONREAD, &bytes_available);
#else
	int status;

	if (ioctl(tcp->socket, FIONREAD, &status) < 0) {	// TIOCMGET
		sr_err("FIONREAD failed: %s\n", strerror(errno));
		return 0;
	}

	return (status < 1) ? 0 : 1;
#endif  // __WIN32__
}

SR_PRIV struct ipdbg_la_tcp *ipdbg_la_tcp_new(void)
{
	struct ipdbg_la_tcp *tcp;

	tcp = g_malloc0(sizeof(struct ipdbg_la_tcp));

	tcp->address = NULL;
	tcp->port = NULL;
	tcp->socket = -1;

	return tcp;
}

SR_PRIV void ipdbg_la_tcp_free(struct ipdbg_la_tcp *tcp)
{
	g_free(tcp->address);
	g_free(tcp->port);
}

SR_PRIV int ipdbg_la_tcp_open(struct ipdbg_la_tcp *tcp)
{
	struct addrinfo hints;
	struct addrinfo *results, *res;
	int err;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	err = getaddrinfo(tcp->address, tcp->port, &hints, &results);

	if (err) {
		sr_err("Address lookup failed: %s:%s: %s", tcp->address,
			tcp->port, gai_strerror(err));
		return SR_ERR;
	}

	for (res = results; res; res = res->ai_next) {
		if ((tcp->socket = socket(res->ai_family, res->ai_socktype,
			res->ai_protocol)) < 0)
			continue;
		if (connect(tcp->socket, res->ai_addr, res->ai_addrlen) != 0) {
			close(tcp->socket);
			tcp->socket = -1;
			continue;
		}
		break;
	}

	freeaddrinfo(results);

	if (tcp->socket < 0) {
		sr_err("Failed to connect to %s:%s: %s", tcp->address, tcp->port,
			g_strerror(errno));
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int ipdbg_la_tcp_close(struct ipdbg_la_tcp *tcp)
{
	int ret = SR_OK;

	if (close(tcp->socket) < 0)
		ret = SR_ERR;

	tcp->socket = -1;

	return ret;
}

SR_PRIV int ipdbg_la_tcp_send(struct ipdbg_la_tcp *tcp,
	const uint8_t *buf, size_t len)
{
	int out;
	out = send(tcp->socket, (char*)buf, len, 0);

	if (out < 0) {
		sr_err("Send error: %s", g_strerror(errno));
		return SR_ERR;
	}

	if (out < (int)len)
		sr_dbg("Only sent %d/%d bytes of data.", out, (int)len);

	return SR_OK;
}

SR_PRIV int ipdbg_la_tcp_receive_blocking(struct ipdbg_la_tcp *tcp,
	uint8_t *buf, int bufsize)
{
	int received = 0;
	int error_count = 0;

	/* Timeout after 500ms of not receiving data */
	while ((received < bufsize) && (error_count < 500)) {
		if (ipdbg_la_tcp_receive(tcp, buf) > 0) {
			buf++;
			received++;
		} else {
			error_count++;
			g_usleep(1000);  /* Sleep for 1ms */
		}
	}

	return received;
}

SR_PRIV int ipdbg_la_tcp_receive(struct ipdbg_la_tcp *tcp,
	uint8_t *buf)
{
	int received = 0;

	if (data_available(tcp)) {
		while (received < 1) {
			int len = recv(tcp->socket, buf, 1, 0);

			if (len < 0) {
				sr_err("Receive error: %s", g_strerror(errno));
				return SR_ERR;
			} else
				received += len;
		}

		return received;
	} else
		return -1;
}

SR_PRIV int ipdbg_la_convert_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	const GSList *l, *m;

	devc = sdi->priv;

	devc->num_stages = 0;
	devc->num_transfers = 0;
	devc->raw_sample_buf = NULL;

	for (uint64_t i = 0; i < devc->DATA_WIDTH_BYTES; i++) {
		devc->trigger_mask[i] = 0;
		devc->trigger_value[i] = 0;
		devc->trigger_mask_last[i] = 0;
		devc->trigger_value_last[i] = 0;
		devc->trigger_edge_mask[i] = 0;
	}

	if (!(trigger = sr_session_trigger_get(sdi->session)))
		return SR_OK;

	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			int byte_idx = match->channel->index / 8;
			uint8_t match_bit = 1 << (match->channel->index % 8);

			if (!match->channel->enabled)
				/* Ignore disabled channels with a trigger. */
				continue;

			if (match->match == SR_TRIGGER_ONE) {
				devc->trigger_value[byte_idx] |= match_bit;
				devc->trigger_mask[byte_idx] |= match_bit;
				devc->trigger_mask_last[byte_idx] &= ~match_bit;
				devc->trigger_edge_mask[byte_idx] &= ~match_bit;
			} else if (match->match == SR_TRIGGER_ZERO) {
				devc->trigger_value[byte_idx] &= ~match_bit;
				devc->trigger_mask[byte_idx] |= match_bit;
				devc->trigger_mask_last[byte_idx] &= ~match_bit;
				devc->trigger_edge_mask[byte_idx] &= ~match_bit;
			} else if (match->match == SR_TRIGGER_RISING) {
				devc->trigger_value[byte_idx] |= match_bit;
				devc->trigger_value_last[byte_idx] &=
				    ~match_bit;
				devc->trigger_mask[byte_idx] |= match_bit;
				devc->trigger_mask_last[byte_idx] |= match_bit;
				devc->trigger_edge_mask[byte_idx] &= ~match_bit;
			} else if (match->match == SR_TRIGGER_FALLING) {
				devc->trigger_value[byte_idx] &= ~match_bit;
				devc->trigger_value_last[byte_idx] |= match_bit;
				devc->trigger_mask[byte_idx] |= match_bit;
				devc->trigger_mask_last[byte_idx] |= match_bit;
				devc->trigger_edge_mask[byte_idx] &= ~match_bit;
			} else if (match->match == SR_TRIGGER_EDGE) {
				devc->trigger_mask[byte_idx] &= ~match_bit;
				devc->trigger_mask_last[byte_idx] &= ~match_bit;
				devc->trigger_edge_mask[byte_idx] |= match_bit;
			}
		}
	}

	return SR_OK;
}

SR_PRIV int ipdbg_la_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;
	(void)revents;

	sdi = (const struct sr_dev_inst *)cb_data;
	if (!sdi)
		return FALSE;

	if (!(devc = sdi->priv))
		return FALSE;

	struct ipdbg_la_tcp *tcp = sdi->conn;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	if (!devc->raw_sample_buf) {
		devc->raw_sample_buf =
			g_try_malloc(devc->limit_samples * devc->DATA_WIDTH_BYTES);
		if (!devc->raw_sample_buf) {
			sr_warn("Sample buffer malloc failed.");
			return FALSE;
		}
	}

	if (devc->num_transfers <
		(devc->limit_samples_max * devc->DATA_WIDTH_BYTES)) {
		uint8_t byte;

		if (ipdbg_la_tcp_receive(tcp, &byte) == 1) {
			if (devc->num_transfers <
				(devc->limit_samples * devc->DATA_WIDTH_BYTES))
				devc->raw_sample_buf[devc->num_transfers] = byte;

			devc->num_transfers++;
		}
	} else {
		if (devc->delay_value > 0) {
			/* There are pre-trigger samples, send those first. */
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = devc->delay_value * devc->DATA_WIDTH_BYTES;
			logic.unitsize = devc->DATA_WIDTH_BYTES;
			logic.data = devc->raw_sample_buf;
			sr_session_send(cb_data, &packet);
		}

		/* Send the trigger. */
		packet.type = SR_DF_TRIGGER;
		sr_session_send(cb_data, &packet);

		/* Send post-trigger samples. */
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = (devc->limit_samples - devc->delay_value) *
			devc->DATA_WIDTH_BYTES;
		logic.unitsize = devc->DATA_WIDTH_BYTES;
		logic.data = devc->raw_sample_buf +
			(devc->delay_value * devc->DATA_WIDTH_BYTES);
		sr_session_send(cb_data, &packet);

		g_free(devc->raw_sample_buf);
		devc->raw_sample_buf = NULL;

		ipdbg_la_abort_acquisition(sdi);
	}

	return TRUE;
}

SR_PRIV int ipdbg_la_send_delay(struct dev_context *devc,
	struct ipdbg_la_tcp *tcp)
{
	devc->delay_value = (devc->limit_samples / 100.0) * devc->capture_ratio;

	uint8_t buf;
	buf = CMD_CFG_LA;
	ipdbg_la_tcp_send(tcp, &buf, 1);
	buf = CMD_LA_DELAY;
	ipdbg_la_tcp_send(tcp, &buf, 1);

	uint8_t delay_buf[4] = { devc->delay_value & 0x000000ff,
		(devc->delay_value >> 8) & 0x000000ff,
		(devc->delay_value >> 16) & 0x000000ff,
		(devc->delay_value >> 24) & 0x000000ff
	};

	for (uint64_t i = 0; i < devc->ADDR_WIDTH_BYTES; i++)
		send_escaping(tcp, &(delay_buf[devc->ADDR_WIDTH_BYTES - 1 - i]), 1);

	return SR_OK;
}

SR_PRIV int ipdbg_la_send_trigger(struct dev_context *devc,
	struct ipdbg_la_tcp *tcp)
{
	uint8_t buf;

	/* Mask */
	buf = CMD_CFG_TRIGGER;
	ipdbg_la_tcp_send(tcp, &buf, 1);
	buf = CMD_TRIG_MASKS;
	ipdbg_la_tcp_send(tcp, &buf, 1);
	buf = CMD_TRIG_MASK;
	ipdbg_la_tcp_send(tcp, &buf, 1);

	for (size_t i = 0; i < devc->DATA_WIDTH_BYTES; i++)
		send_escaping(tcp,
			devc->trigger_mask + devc->DATA_WIDTH_BYTES - 1 - i, 1);

	/* Value */
	buf = CMD_CFG_TRIGGER;
	ipdbg_la_tcp_send(tcp, &buf, 1);
	buf = CMD_TRIG_MASKS;
	ipdbg_la_tcp_send(tcp, &buf, 1);
	buf = CMD_TRIG_VALUE;
	ipdbg_la_tcp_send(tcp, &buf, 1);

	for (size_t i = 0; i < devc->DATA_WIDTH_BYTES; i++)
		send_escaping(tcp,
			devc->trigger_value + devc->DATA_WIDTH_BYTES - 1 - i, 1);

	/* Mask_last */
	buf = CMD_CFG_TRIGGER;
	ipdbg_la_tcp_send(tcp, &buf, 1);
	buf = CMD_TRIG_MASKS_LAST;
	ipdbg_la_tcp_send(tcp, &buf, 1);
	buf = CMD_TRIG_MASK_LAST;
	ipdbg_la_tcp_send(tcp, &buf, 1);

	for (size_t i = 0; i < devc->DATA_WIDTH_BYTES; i++)
		send_escaping(tcp,
			devc->trigger_mask_last + devc->DATA_WIDTH_BYTES - 1 - i, 1);

	/* Value_last */
	buf = CMD_CFG_TRIGGER;
	ipdbg_la_tcp_send(tcp, &buf, 1);
	buf = CMD_TRIG_MASKS_LAST;
	ipdbg_la_tcp_send(tcp, &buf, 1);
	buf = CMD_TRIG_VALUE_LAST;
	ipdbg_la_tcp_send(tcp, &buf, 1);

	for (size_t i = 0; i < devc->DATA_WIDTH_BYTES; i++)
		send_escaping(tcp,
			devc->trigger_value_last + devc->DATA_WIDTH_BYTES - 1 - i, 1);

	/* Edge_mask */
	buf = CMD_CFG_TRIGGER;
	ipdbg_la_tcp_send(tcp, &buf, 1);
	buf = CMD_TRIG_SELECT_EDGE_MASK;
	ipdbg_la_tcp_send(tcp, &buf, 1);
	buf = CMD_TRIG_SET_EDGE_MASK;
	ipdbg_la_tcp_send(tcp, &buf, 1);

	for (size_t i = 0; i < devc->DATA_WIDTH_BYTES; i++)
		send_escaping(tcp,
			devc->trigger_edge_mask + devc->DATA_WIDTH_BYTES - 1 - i, 1);

	return SR_OK;
}

SR_PRIV int send_escaping(struct ipdbg_la_tcp *tcp, uint8_t *dataToSend,
	uint32_t length)
{
	uint8_t escape = CMD_ESCAPE;

	while (length--) {
		uint8_t payload = *dataToSend++;

		if (payload == (uint8_t) CMD_RESET)
			if (ipdbg_la_tcp_send(tcp, &escape, 1) != SR_OK)
				sr_warn("Couldn't send escape");

		if (payload == (uint8_t) CMD_ESCAPE)
			if (ipdbg_la_tcp_send(tcp, &escape, 1) != SR_OK)
				sr_warn("Couldn't send escape");

		if (ipdbg_la_tcp_send(tcp, &payload, 1) != SR_OK)
			sr_warn("Couldn't send data");
	}

	return SR_OK;
}

SR_PRIV void ipdbg_la_get_addrwidth_and_datawidth(
	struct ipdbg_la_tcp *tcp, struct dev_context *devc)
{
	uint8_t buf[8];
	uint8_t read_cmd = CMD_GET_BUS_WIDTHS;

	if (ipdbg_la_tcp_send(tcp, &read_cmd, 1) != SR_OK)
		sr_warn("Can't send read command");

	if (ipdbg_la_tcp_receive_blocking(tcp, buf, 8) != 8)
		sr_warn("Can't get address and data width from device");

	devc->DATA_WIDTH = buf[0] & 0x000000FF;
	devc->DATA_WIDTH |= (buf[1] << 8) & 0x0000FF00;
	devc->DATA_WIDTH |= (buf[2] << 16) & 0x00FF0000;
	devc->DATA_WIDTH |= (buf[3] << 24) & 0xFF000000;

	devc->ADDR_WIDTH = buf[4] & 0x000000FF;
	devc->ADDR_WIDTH |= (buf[5] << 8) & 0x0000FF00;
	devc->ADDR_WIDTH |= (buf[6] << 16) & 0x00FF0000;
	devc->ADDR_WIDTH |= (buf[7] << 24) & 0xFF000000;

	uint8_t HOST_WORD_SIZE = 8;

	devc->DATA_WIDTH_BYTES =
		(devc->DATA_WIDTH + HOST_WORD_SIZE - 1) / HOST_WORD_SIZE;
	devc->ADDR_WIDTH_BYTES =
		(devc->ADDR_WIDTH + HOST_WORD_SIZE - 1) / HOST_WORD_SIZE;

	devc->limit_samples_max = (0x01 << devc->ADDR_WIDTH);
	devc->limit_samples = devc->limit_samples_max;

	devc->trigger_mask = g_malloc0(devc->DATA_WIDTH_BYTES);
	devc->trigger_value = g_malloc0(devc->DATA_WIDTH_BYTES);
	devc->trigger_mask_last = g_malloc0(devc->DATA_WIDTH_BYTES);
	devc->trigger_value_last = g_malloc0(devc->DATA_WIDTH_BYTES);
	devc->trigger_edge_mask = g_malloc0(devc->DATA_WIDTH_BYTES);
}

SR_PRIV struct dev_context *ipdbg_la_dev_new(void)
{
	struct dev_context *devc;

	devc = g_malloc0(sizeof(struct dev_context));
	devc->capture_ratio = 50;

	return devc;
}

SR_PRIV int ipdbg_la_send_reset(struct ipdbg_la_tcp *tcp)
{
	uint8_t buf = CMD_RESET;
	if (ipdbg_la_tcp_send(tcp, &buf, 1) != SR_OK)
		sr_warn("Couldn't send reset");

	return SR_OK;
}

SR_PRIV int ipdbg_la_request_id(struct ipdbg_la_tcp *tcp)
{
	uint8_t buf = CMD_GET_LA_ID;
	if (ipdbg_la_tcp_send(tcp, &buf, 1) != SR_OK)
		sr_warn("Couldn't send ID request");

	char id[4];
	if (ipdbg_la_tcp_receive_blocking(tcp, (uint8_t*)id, 4) != 4) {
		sr_err("Couldn't read device ID");
		return SR_ERR;
	}

	if (strncmp(id, "IDBG", 4)) {
		sr_err("Invalid device ID: expected 'IDBG', got '%c%c%c%c'.",
			id[0], id[1], id[2], id[3]);
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV void ipdbg_la_abort_acquisition(const struct sr_dev_inst *sdi)
{
	struct ipdbg_la_tcp *tcp = sdi->conn;

	sr_session_source_remove(sdi->session, tcp->socket);

	std_session_send_df_end(sdi);
}

SR_PRIV int ipdbg_la_send_start(struct ipdbg_la_tcp *tcp)
{
	uint8_t buf = CMD_START;

	if (ipdbg_la_tcp_send(tcp, &buf, 1) != SR_OK)
		sr_warn("Couldn't send start");

	return SR_OK;
}
