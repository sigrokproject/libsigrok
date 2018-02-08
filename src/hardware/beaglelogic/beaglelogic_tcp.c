/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Kumar Abhishek <abhishek@theembeddedkitchen.net>
 * Portions of the code are adapted from scpi_tcp.c and scpi.c, their
 * copyright notices are listed below:
 *
 * Copyright (C) 2013 Martin Ling <martin-sigrok@earth.li>
 * Copyright (C) 2013 poljar (Damir Jelić) <poljarinho@gmail.com>
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

#include "protocol.h"
#include "beaglelogic.h"

static int beaglelogic_tcp_open(struct dev_context *devc)
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

static int beaglelogic_tcp_send_cmd(struct dev_context *devc,
				    const char *format, ...)
{
	int len, out;
	va_list args, args_copy;
	char *buf;

	va_start(args, format);
	va_copy(args_copy, args);
	len = vsnprintf(NULL, 0, format, args_copy);
	va_end(args_copy);

	buf = g_malloc0(len + 2);
	vsprintf(buf, format, args);
	va_end(args);

	if (buf[len - 1] != '\n')
		buf[len] = '\n';

	out = send(devc->socket, buf, strlen(buf), 0);

	if (out < 0) {
		sr_err("Send error: %s", g_strerror(errno));
		g_free(buf);
		return SR_ERR;
	}

	if (out < (int)strlen(buf)) {
		sr_dbg("Only sent %d/%lu bytes of command: '%s'.", out,
		       strlen(buf), buf);
	}

	sr_spew("Sent command: '%s'.", buf);

	g_free(buf);

	return SR_OK;
}

static int beaglelogic_tcp_read_data(struct dev_context *devc, char *buf,
				     int maxlen)
{
	int len;

	len = recv(devc->socket, buf, maxlen, 0);

	if (len < 0) {
		sr_err("Receive error: %s", g_strerror(errno));
		return SR_ERR;
	}

	return len;
}

SR_PRIV int beaglelogic_tcp_drain(struct dev_context *devc)
{
	char *buf = g_malloc(1024);
	fd_set rset;
	int ret, len = 0;
	struct timeval tv;

	FD_ZERO(&rset);
	FD_SET(devc->socket, &rset);

	/* 25ms timeout */
	tv.tv_sec = 0;
	tv.tv_usec = 25 * 1000;

	do {
		ret = select(devc->socket + 1, &rset, NULL, NULL, &tv);
		if (ret > 0)
			len += beaglelogic_tcp_read_data(devc, buf, 1024);
	} while (ret > 0);

	sr_spew("Drained %d bytes of data.", len);

	g_free(buf);

	return SR_OK;
}

static int beaglelogic_tcp_get_string(struct dev_context *devc, const char *cmd,
				      char **tcp_resp)
{
	GString *response = g_string_sized_new(1024);
	int len;
	gint64 timeout;

	*tcp_resp = NULL;
	if (cmd) {
		if (beaglelogic_tcp_send_cmd(devc, cmd) != SR_OK)
			return SR_ERR;
	}

	timeout = g_get_monotonic_time() + devc->read_timeout;
	len = beaglelogic_tcp_read_data(devc, response->str,
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

static int beaglelogic_tcp_get_int(struct dev_context *devc,
				   const char *cmd, int *response)
{
	int ret;
	char *resp = NULL;

	ret = beaglelogic_tcp_get_string(devc, cmd, &resp);
	if (!resp && ret != SR_OK)
		return ret;

	if (sr_atoi(resp, response) == SR_OK)
		ret = SR_OK;
	else
		ret = SR_ERR_DATA;

	g_free(resp);

	return ret;
}

SR_PRIV int beaglelogic_tcp_detect(struct dev_context *devc)
{
	char *resp = NULL;
	int ret;

	ret = beaglelogic_tcp_get_string(devc, "version", &resp);
	if (ret == SR_OK && !g_ascii_strncasecmp(resp, "BeagleLogic", 11))
		ret = SR_OK;
	else
		ret = SR_ERR;

	g_free(resp);

	return ret;
}

static int beaglelogic_open(struct dev_context *devc)
{
	return beaglelogic_tcp_open(devc);
}

static int beaglelogic_close(struct dev_context *devc)
{
	if (close(devc->socket) < 0)
		return SR_ERR;

	return SR_OK;
}

static int beaglelogic_get_buffersize(struct dev_context *devc)
{
	return beaglelogic_tcp_get_int(devc, "memalloc",
		(int *)&devc->buffersize);
}

static int beaglelogic_set_buffersize(struct dev_context *devc)
{
	int ret;
	char *resp;

	beaglelogic_tcp_send_cmd(devc, "memalloc %lu", devc->buffersize);
	ret = beaglelogic_tcp_get_string(devc, NULL, &resp);
	if (ret == SR_OK && !g_ascii_strncasecmp(resp, "ok", 2))
		ret = SR_OK;
	else
		ret = SR_ERR;

	g_free(resp);

	return ret;
}

static int beaglelogic_get_samplerate(struct dev_context *devc)
{
	int arg, err;

	err = beaglelogic_tcp_get_int(devc, "samplerate", &arg);
	if (err)
		return err;

	devc->cur_samplerate = arg;
	return SR_OK;
}

static int beaglelogic_set_samplerate(struct dev_context *devc)
{
	int ret;
	char *resp;

	beaglelogic_tcp_send_cmd(devc, "samplerate %lu",
		(uint32_t)devc->cur_samplerate);
	ret = beaglelogic_tcp_get_string(devc, NULL, &resp);
	if (ret == SR_OK && !g_ascii_strncasecmp(resp, "ok", 2))
		ret = SR_OK;
	else
		ret = SR_ERR;

	g_free(resp);

	return ret;
}

static int beaglelogic_get_sampleunit(struct dev_context *devc)
{
	return beaglelogic_tcp_get_int(devc, "sampleunit",
		(int *)&devc->sampleunit);
}

static int beaglelogic_set_sampleunit(struct dev_context *devc)
{
	int ret;
	char *resp;

	beaglelogic_tcp_send_cmd(devc, "sampleunit %lu", devc->sampleunit);
	ret = beaglelogic_tcp_get_string(devc, NULL, &resp);
	if (ret == SR_OK && !g_ascii_strncasecmp(resp, "ok", 2))
		ret = SR_OK;
	else
		ret = SR_ERR;

	g_free(resp);

	return ret;
}

static int beaglelogic_get_triggerflags(struct dev_context *devc)
{
	return beaglelogic_tcp_get_int(devc, "triggerflags",
		(int *)&devc->triggerflags);
}

static int beaglelogic_set_triggerflags(struct dev_context *devc)
{
	int ret;
	char *resp;

	beaglelogic_tcp_send_cmd(devc, "triggerflags %lu", devc->triggerflags);
	ret = beaglelogic_tcp_get_string(devc, NULL, &resp);
	if (ret == SR_OK && !g_ascii_strncasecmp(resp, "ok", 2))
		ret = SR_OK;
	else
		ret = SR_ERR;

	g_free(resp);

	return ret;
}

static int beaglelogic_get_lasterror(struct dev_context *devc)
{
	devc->last_error = 0;

	return SR_OK;
}

static int beaglelogic_start(struct dev_context *devc)
{
	beaglelogic_tcp_drain(devc);

	return beaglelogic_tcp_send_cmd(devc, "get");
}

static int beaglelogic_stop(struct dev_context *devc)
{
	return beaglelogic_tcp_send_cmd(devc, "close");
}

static int beaglelogic_get_bufunitsize(struct dev_context *devc)
{
	return beaglelogic_tcp_get_int(devc, "bufunitsize",
		(int *)&devc->bufunitsize);
}

static int beaglelogic_set_bufunitsize(struct dev_context *devc)
{
	int ret;
	char *resp;

	beaglelogic_tcp_send_cmd(devc, "bufunitsize %ld", devc->bufunitsize);
	ret = beaglelogic_tcp_get_string(devc, NULL, &resp);
	if (ret == SR_OK && !g_ascii_strncasecmp(resp, "ok", 2))
		ret = SR_OK;
	else
		ret = SR_ERR;

	g_free(resp);

	return ret;
}

static int dummy(struct dev_context *devc)
{
	(void)devc;

	return SR_ERR_NA;
}

SR_PRIV const struct beaglelogic_ops beaglelogic_tcp_ops = {
	.open = beaglelogic_open,
	.close = beaglelogic_close,
	.get_buffersize = beaglelogic_get_buffersize,
	.set_buffersize = beaglelogic_set_buffersize,
	.get_samplerate = beaglelogic_get_samplerate,
	.set_samplerate = beaglelogic_set_samplerate,
	.get_sampleunit = beaglelogic_get_sampleunit,
	.set_sampleunit = beaglelogic_set_sampleunit,
	.get_triggerflags = beaglelogic_get_triggerflags,
	.set_triggerflags = beaglelogic_set_triggerflags,
	.start = beaglelogic_start,
	.stop = beaglelogic_stop,
	.get_lasterror = beaglelogic_get_lasterror,
	.get_bufunitsize = beaglelogic_get_bufunitsize,
	.set_bufunitsize = beaglelogic_set_bufunitsize,
	.mmap = dummy,
	.munmap = dummy,
};
