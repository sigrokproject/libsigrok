/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Martin Ling <martin-sigrok@earth.li>
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

#ifdef _WIN32
#define _WIN32_WINNT 0x0501
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include "libsigrok.h"
#include "libsigrok-internal.h"

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

#define LOG_PREFIX "scpi_tcp"

#define LENGTH_BYTES 4

struct scpi_tcp {
	char *address;
	char *port;
	int socket;
	char length_buf[LENGTH_BYTES];
	int length_bytes_read;
	int response_length;
	int response_bytes_read;
};

static int scpi_tcp_dev_inst_new(void *priv, const char *resource,
		char **params, const char *serialcomm)
{
	struct scpi_tcp *tcp = priv;

	(void)resource;
	(void)serialcomm;

	if (!params || !params[1] || !params[2]) {
		sr_err("Invalid parameters.");
		return SR_ERR;
	}

	tcp->address = g_strdup(params[1]);
	tcp->port    = g_strdup(params[2]);
	tcp->socket  = -1;

	return SR_OK;
}

SR_PRIV int scpi_tcp_open(void *priv)
{
	struct scpi_tcp *tcp = priv;
	struct addrinfo hints;
	struct addrinfo *results, *res;
	int err;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	err = getaddrinfo(tcp->address, tcp->port, &hints, &results);

	if (err) {
		sr_err("Address lookup failed: %s:%d: %s", tcp->address, tcp->port,
			gai_strerror(err));
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
				strerror(errno));
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int scpi_tcp_source_add(void *priv, int events, int timeout,
			sr_receive_data_callback_t cb, void *cb_data)
{
	struct scpi_tcp *tcp = priv;

	return sr_source_add(tcp->socket, events, timeout, cb, cb_data);
}

SR_PRIV int scpi_tcp_source_remove(void *priv)
{
	struct scpi_tcp *tcp = priv;

	return sr_source_remove(tcp->socket);
}

SR_PRIV int scpi_tcp_send(void *priv, const char *command)
{
	struct scpi_tcp *tcp = priv;
	int len, out;
	char *terminated_command;

	terminated_command = g_strdup_printf("%s\r\n", command);
	len = strlen(terminated_command);
	out = send(tcp->socket, terminated_command, len, 0);
	g_free(terminated_command);

	if (out < 0) {
		sr_err("Send error: %s", strerror(errno));
		return SR_ERR;
	}

	if (out < len) {
		sr_dbg("Only sent %d/%d bytes of SCPI command: '%s'.", out,
		       len, command);
	}

	sr_spew("Successfully sent SCPI command: '%s'.", command);

	return SR_OK;
}

SR_PRIV int scpi_tcp_read_begin(void *priv)
{
	struct scpi_tcp *tcp = priv;

	tcp->response_bytes_read = 0;
	tcp->length_bytes_read = 0;

	return SR_OK;
}

SR_PRIV int scpi_tcp_read_data(void *priv, char *buf, int maxlen)
{
	struct scpi_tcp *tcp = priv;
	int len;

	if (tcp->length_bytes_read < LENGTH_BYTES) {
		len = recv(tcp->socket, tcp->length_buf + tcp->length_bytes_read,
				LENGTH_BYTES - tcp->length_bytes_read, 0);
		if (len < 0) {
			sr_err("Receive error: %s", strerror(errno));
			return SR_ERR;
		}

		tcp->length_bytes_read += len;

		if (tcp->length_bytes_read < LENGTH_BYTES)
			return 0;
		else
			tcp->response_length = RL32(tcp->length_buf);
	}

	if (tcp->response_bytes_read >= tcp->response_length)
		return SR_ERR;

	len = recv(tcp->socket, buf, maxlen, 0);

	if (len < 0) {
		sr_err("Receive error: %s", strerror(errno));
		return SR_ERR;
	}

	tcp->response_bytes_read += len;

	return len;
}

SR_PRIV int scpi_tcp_read_complete(void *priv)
{
	struct scpi_tcp *tcp = priv;

	return (tcp->length_bytes_read == LENGTH_BYTES &&
			tcp->response_bytes_read >= tcp->response_length);
}

SR_PRIV int scpi_tcp_close(void *priv)
{
	struct scpi_tcp *tcp = priv;

	if (close(tcp->socket) < 0)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV void scpi_tcp_free(void *priv)
{
	struct scpi_tcp *tcp = priv;

	g_free(tcp->address);
	g_free(tcp->port);
}

SR_PRIV const struct sr_scpi_dev_inst scpi_tcp_dev = {
	.name          = "TCP",
	.prefix        = "tcp",
	.priv_size     = sizeof(struct scpi_tcp),
	.dev_inst_new  = scpi_tcp_dev_inst_new,
	.open          = scpi_tcp_open,
	.source_add    = scpi_tcp_source_add,
	.source_remove = scpi_tcp_source_remove,
	.send          = scpi_tcp_send,
	.read_begin    = scpi_tcp_read_begin,
	.read_data     = scpi_tcp_read_data,
	.read_complete = scpi_tcp_read_complete,
	.close         = scpi_tcp_close,
	.free          = scpi_tcp_free,
};
