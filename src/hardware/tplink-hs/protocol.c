/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Francois Gervais <francoisgervais@gmail.com>
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

/*
 * Done with help of the following sources:
 *
 * https://github.com/python-kasa/python-kasa
 * https://github.com/JustinZhou300/TP-Link-HS110-C
 * https://www.softscheck.com/en/reverse-engineering-tp-link-hs110/
 */

#include <config.h>
#ifdef _WIN32
#define _WIN32_WINNT 0x0501
#include <winsock2.h>
#include <ws2tcpip.h>
#endif
#include <glib.h>
#include <string.h>
#include <unistd.h>
#ifndef _WIN32
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#endif
#include <errno.h>
#include <stdlib.h>

#include "protocol.h"
#include "tplink-hs.h"

#define MESSAGE_PADDING_SIZE 4
#define MESSAGE_SIZE_OFFSET 3

#define CMD_SYSINFO_MSG "{\"system\":{\"get_sysinfo\":{}}}"
#define CMD_REALTIME_MSG "{\"emeter\":{\"get_realtime\":{}}}"

#define HS_POLL_PERIOD_MS 1000

static const struct channel_spec tplink_hs_channels[] = {
	{ "V",  SR_CHANNEL_ANALOG, SR_MQ_VOLTAGE, SR_UNIT_VOLT },
	{ "I",  SR_CHANNEL_ANALOG, SR_MQ_CURRENT, SR_UNIT_AMPERE },
	{ NULL, },
};

static int tplink_hs_tcp_encrypt(char *msg, int len)
{
	int i;
	char key = 171;

	for (i = 0; i < len; i++)
	{
		key ^= msg[i];
		msg[i] = key;
	}

	return SR_OK;
}

static int tplink_hs_tcp_decrypt(char *msg, int len)
{
	int i;
	char key = 171;
	char temp;

	for (i = 0; i < len; i++)
	{
		temp = key ^ msg[i];
		key = msg[i];
		msg[i] = temp;
	}

	return SR_OK;
}

static int tplink_hs_tcp_open(struct dev_context *devc)
{
	struct addrinfo hints;
	struct addrinfo *results, *res;
	int err;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	err = getaddrinfo(devc->address, devc->port, &hints, &results);

	if (err) {
		sr_err("Address lookup failed: %s:%s: %s", devc->address,
			devc->port, gai_strerror(err));
		return SR_ERR;
	}

	for (res = results; res; res = res->ai_next) {
		if ((devc->socket = socket(res->ai_family, res->ai_socktype,
						res->ai_protocol)) < 0)
			continue;
		if (connect(devc->socket, res->ai_addr, res->ai_addrlen) != 0) {
			close(devc->socket);
			devc->socket = -1;
			continue;
		}
		break;
	}

	freeaddrinfo(results);

	if (devc->socket < 0) {
		sr_err("Failed to connect to %s:%s: %s", devc->address,
			devc->port, g_strerror(errno));
		return SR_ERR;
	}

	return SR_OK;
}

static int tplink_hs_tcp_close(struct dev_context *devc)
{
	if (close(devc->socket) < 0)
		return SR_ERR;

	return SR_OK;
}

static int tplink_hs_tcp_send_cmd(struct dev_context *devc,
				    const char *msg)
{
	int len, out;
	char *buf;

	len = strlen(msg);

	buf = g_malloc0(len + MESSAGE_PADDING_SIZE);
	memcpy(buf + MESSAGE_PADDING_SIZE, msg, len);

	sr_spew("Unencrypted command: '%s'.", buf + MESSAGE_PADDING_SIZE);

	tplink_hs_tcp_encrypt(buf + MESSAGE_PADDING_SIZE, len);
	buf[MESSAGE_SIZE_OFFSET] = len;
	out = send(devc->socket, buf, len + MESSAGE_PADDING_SIZE, 0);

	if (out < 0) {
		sr_err("Send error: %s", g_strerror(errno));
		g_free(buf);
		return SR_ERR;
	}

	if (out < len + MESSAGE_PADDING_SIZE) {
		sr_dbg("Only sent %d/%zu bytes of command: '%s'.", out,
		       strlen(buf), buf);
	}

	sr_spew("Sent command: '%s'.", buf + MESSAGE_PADDING_SIZE);
	devc->cmd_sent_at = g_get_monotonic_time() / 1000;

	g_free(buf);

	return SR_OK;
}

static int tplink_hs_tcp_read_data(struct dev_context *devc, char *buf,
				     int maxlen)
{
	int len;

	len = recv(devc->socket, buf, maxlen, 0);

	if (len < 0) {
		sr_err("Receive error: %s", g_strerror(errno));
		return SR_ERR;
	}

	if ((len - MESSAGE_PADDING_SIZE) < 0)
		return 0;

	len -= MESSAGE_PADDING_SIZE;
	memmove(buf, buf + MESSAGE_PADDING_SIZE, len);
	tplink_hs_tcp_decrypt(buf, len);

	sr_spew("Data received: '%s'.", buf);

	return len;
}

static int tplink_hs_tcp_get_json(struct dev_context *devc, const char *cmd,
				      char **tcp_resp)
{
	GString *response = g_string_sized_new(1024);
	int len;
	gint64 timeout;

	*tcp_resp = NULL;
	if (cmd) {
		if (tplink_hs_tcp_send_cmd(devc, cmd) != SR_OK)
			return SR_ERR;
	}

	timeout = g_get_monotonic_time() + devc->read_timeout;
	len = tplink_hs_tcp_read_data(devc, response->str,
					response->allocated_len);

	if (len < 0) {
		g_string_free(response, TRUE);
		return SR_ERR;
	}

	if (len > 0)
		g_string_set_size(response, len);

	if (g_get_monotonic_time() > timeout) {
		sr_err("Timed out waiting for response.");
		g_string_free(response, TRUE);
		return SR_ERR_TIMEOUT;
	}

	/* Remove trailing newline if present */
	if (response->len >= 1 && response->str[response->len - 1] == '\n')
		g_string_truncate(response, response->len - 1);

	/* Remove trailing carriage return if present */
	if (response->len >= 1 && response->str[response->len - 1] == '\r')
		g_string_truncate(response, response->len - 1);

	sr_spew("Got response: '%.70s', length %" G_GSIZE_FORMAT ".",
		response->str, response->len);

	*tcp_resp = g_string_free(response, FALSE);

	return SR_OK;
}

static int tplink_hs_get_node_value(char *string, char *node_name,
				      char **value)
{
	char *node_start;
	char *value_start;
	char *value_end;

	*value = NULL;

	node_start = strstr(string, node_name);
	if (node_start == NULL)
		return SR_ERR;

	value_start = node_start + strlen(node_name) + 2;

	if (*value_start == '\"')
		value_start += 1;

	value_end = strstr(value_start, ",");
	if (value_end == NULL)
		return SR_ERR;

	if (*(value_end - 1) == '\"')
		value_end -= 1;

	*value = g_strndup(value_start, value_end - value_start);

	return SR_OK;
}

static int tplink_hs_start(struct dev_context *devc)
{
	if (tplink_hs_tcp_send_cmd(devc, CMD_REALTIME_MSG) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int tplink_hs_stop(struct dev_context *devc)
{
	(void)devc;

	return SR_OK;
}

SR_PRIV int tplink_hs_probe(struct dev_context  *devc)
{
	char *resp = NULL;

	if (tplink_hs_tcp_open(devc) != SR_OK)
		return SR_ERR;
	if (tplink_hs_tcp_get_json(devc, CMD_SYSINFO_MSG, &resp) != SR_OK)
		goto err;
	if (tplink_hs_tcp_close(devc) != SR_OK)
		goto err;

	if (strstr(resp, "HS110") == NULL) {
		sr_err("Unrecognized HS device");
		goto err;
	}

	devc->dev_info.channels = tplink_hs_channels;

	if (tplink_hs_get_node_value(resp, "model",
				       &devc->dev_info.model) != SR_OK)
		goto err;
	if (tplink_hs_get_node_value(resp, "sw_ver",
				       &devc->dev_info.sw_ver) != SR_OK)
		goto err;
	if (tplink_hs_get_node_value(resp, "deviceId",
				       &devc->dev_info.device_id) != SR_OK)
		goto err;

	g_free(resp);

	sr_spew("Registered device: %s - %s - %s", devc->dev_info.model,
						   devc->dev_info.sw_ver,
						   devc->dev_info.device_id);

	return SR_OK;

err:
	g_free(devc->dev_info.model);
	g_free(devc->dev_info.sw_ver);
	g_free(devc->dev_info.device_id);
	g_free(resp);

	return SR_ERR;
}

static void handle_poll_data(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	int i;

	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.num_samples = 1;

	for (i = 0; devc->dev_info.channels[i].name; i++) {
		analog.meaning->mq = devc->dev_info.channels[i].mq;
		analog.meaning->unit = devc->dev_info.channels[i].unit;
		analog.meaning->mqflags = SR_MQFLAG_DC;
		analog.encoding->digits = 6;
		analog.spec->spec_digits = 6;

		if (devc->dev_info.channels[i].mq == SR_MQ_VOLTAGE) {
			analog.meaning->channels =
				g_slist_append(NULL, sdi->channels->data);
			analog.data = &devc->voltage;
		}
		else if (devc->dev_info.channels[i].mq == SR_MQ_CURRENT) {
			analog.meaning->channels =
				g_slist_append(NULL, sdi->channels->next->data);
			analog.data = &devc->current;
		}

		sr_session_send(sdi, &packet);
	}
	sr_sw_limits_update_samples_read(&devc->limits, 1);
}

static int recv_poll_data(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	char *response = g_malloc0(1024);
	char *node_current_value;
	char *node_voltage_value;
	int len;

	len = tplink_hs_tcp_read_data(devc, response, 1024);

	if (len < 0)
		goto err;

	if (tplink_hs_get_node_value(response, "current",
			       &node_current_value) != SR_OK)
		goto err;
	if (tplink_hs_get_node_value(response, "voltage",
			       &node_voltage_value) != SR_OK)
		goto err;

	sr_spew("volatage: %s, current: %s", node_voltage_value,
					     node_current_value);

	devc->voltage = strtof(node_voltage_value, NULL);
	devc->current = strtof(node_current_value, NULL);

	sr_spew("volatage(f): %f, current(f): %f", devc->voltage,
						    devc->current);

	handle_poll_data(sdi);

	g_free(response);
	return SR_OK;

err:
	g_free(response);
	return SR_ERR;

}

SR_PRIV int tplink_hs_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int64_t now, elapsed;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		sr_info("In callback G_IO_IN");
		recv_poll_data(sdi);
		tplink_hs_tcp_close(devc);
		sr_session_source_remove_pollfd(sdi->session, &devc->pollfd);
	}

	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	now = g_get_monotonic_time() / 1000;
	elapsed = now - devc->cmd_sent_at;

	if (elapsed > HS_POLL_PERIOD_MS) {
		tplink_hs_tcp_open(devc);
		devc->pollfd.fd = devc->socket;
		sr_session_source_add_pollfd(sdi->session, &devc->pollfd,
			0, tplink_hs_receive_data,
			(void *)sdi);

		tplink_hs_tcp_send_cmd(devc, CMD_REALTIME_MSG);
	}

	return TRUE;
}

SR_PRIV const struct tplink_hs_ops tplink_hs_dev_ops = {
	.open = tplink_hs_tcp_open,
	.close = tplink_hs_tcp_close,
	.start = tplink_hs_start,
	.stop = tplink_hs_stop,
};
