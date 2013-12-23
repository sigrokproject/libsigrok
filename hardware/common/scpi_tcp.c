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

struct scpi_tcp {
	char *address;
	char *port;
	int socket;
};

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

SR_PRIV int scpi_tcp_receive(void *priv, char **scpi_response)
{
	struct scpi_tcp *tcp = priv;
	GString *response;
	char buf[256];
	int len;

	response = g_string_sized_new(1024);

	len = recv(tcp->socket, buf, sizeof(buf), 0);

	if (len < 0) {
		sr_err("Receive error: %s", strerror(errno));
		g_string_free(response, TRUE);
		return SR_ERR;
	}

	response = g_string_append_len(response, buf + 4, len - 4);

	*scpi_response = response->str;

	sr_dbg("SCPI response received (length %d): '%.50s'",
	       response->len, response->str);

	g_string_free(response, FALSE);

	return SR_OK;
}

SR_PRIV int scpi_tcp_read(void *priv, char *buf, int maxlen)
{
	struct scpi_tcp *tcp = priv;
	int len;

	len = recv(tcp->socket, buf, maxlen, 0);

	if (len < 0) {
		sr_err("Receive error: %s", strerror(errno));
		return SR_ERR;
	}

	return len;
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
	g_free(tcp);
}

SR_PRIV struct sr_scpi_dev_inst *scpi_tcp_dev_inst_new(const char *address,
		const char *port)
{
	struct sr_scpi_dev_inst *scpi;
	struct scpi_tcp *tcp;

	scpi = g_malloc(sizeof(struct sr_scpi_dev_inst));
	tcp = g_malloc0(sizeof(struct scpi_tcp));

	tcp->address = g_strdup(address);
	tcp->port = g_strdup(port);
	tcp->socket = -1;

	scpi->open = scpi_tcp_open;
	scpi->source_add = scpi_tcp_source_add;
	scpi->source_remove = scpi_tcp_source_remove;
	scpi->send = scpi_tcp_send;
	scpi->receive = scpi_tcp_receive;
	scpi->read = scpi_tcp_read;
	scpi->close = scpi_tcp_close;
	scpi->free = scpi_tcp_free;
	scpi->priv = tcp;

	return scpi;
}
