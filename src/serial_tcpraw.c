/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#include "config.h"

#if defined _WIN32
#include <winsock2.h>
#else
#include <sys/ioctl.h>
#endif

#include <libsigrok/libsigrok.h>
#include <string.h>

#include "libsigrok-internal.h"

#define LOG_PREFIX "serial-tcpraw"

#define SER_TCPRAW_CONN_PREFIX	"tcp-raw"

/* 100ms, high value for high latency IP connection (like WIFI). */
/* TODO: VERYLOW - make this connection param. */
static const int DRAIN_TIMEOUT = 100 * 1000;

/* 1ms */
/* TODO: VERYLOW - make this connection param. */
static const int FLUSH_TIMEOUT = 1 * 1000;


/**
 * @file
 *
 * Serial port handling, raw TCP support.
 */

/**
 * @defgroup grp_serial_tcpraw Serial port handling, raw TCP group
 *
 * Disguise raw byte sequences over TCP sockets as a serial transport.
 *
 * @{
 */

/* {{{ TCP specific helper routines */

/**
 * Parse conn= specs for serial over TCP communication.
 *
 * @param[in] serial The serial port that is about to get opened.
 * @param[in] spec The caller provided conn= specification.
 * @param[out] host_ref Pointer to host name or IP addr (text string).
 * @param[out] port_ref Pointer to a TCP port (text string).
 *
 * @return 0 upon success, non-zero upon failure. Fills the *_ref output
 * values.
 *
 * Summary of parsing rules as they are implemented:
 * - The 'spec' MUST start with "tcp-raw" followed by a separator. The
 *   prefix alone is not sufficient, host address and port number are
 *   mandatory.
 * - Host name follows. It's a DNS name or an IP address.
 * - TCP port follows. Can be a number or a "service" name.
 * - More than three fields are accepted, but currently don't take any
 *   effect. It's yet to be seen whether "options" or "variants" are
 *   needed or desired. For now any trailing fields are ignored. Cisco
 *   style serial-over-TCP as seen in ser2net(1) comes to mind (which
 *   includes configuration and control beyond data transmission). But
 *   its spec is rather involved, and ser2net can already derive COM
 *   port configurations from TCP port numbers, so it's not a blocker.
 *   That variant probably should go under a different name anyway.
 *
 * Supported format resulting from these rules:
 *   tcp-raw/<ipaddr>/<port>
 */
static int ser_tcpraw_parse_conn_spec(
	struct sr_serial_dev_inst *serial, const char *spec,
	char **host_ref, char **port_ref)
{
	char **fields;
	size_t count;
	gboolean valid;
	char *host, *port;

	if (host_ref)
		*host_ref = NULL;
	if (port_ref)
		*port_ref = NULL;

	host = NULL;
	port = NULL;
	if (!serial || !spec || !*spec)
		return SR_ERR_ARG;

	fields = g_strsplit(spec, "/", 0);
	if (!fields)
		return SR_ERR_ARG;
	count = g_strv_length(fields);

	valid = TRUE;
	if (count < 3)
		valid = FALSE;
	if (valid && strcmp(fields[0], SER_TCPRAW_CONN_PREFIX) != 0)
		valid = FALSE;
	if (valid) {
		host = fields[1];
		if (!host || !*host)
			valid = FALSE;
	}
	if (valid) {
		port = fields[2];
		if (!port || !*port)
			valid = FALSE;
	}
	/* Silently ignore trailing fields. Could be future options. */
	if (count > 3)
		sr_warn("Ignoring excess parameters in %s.", spec);

	if (valid) {
		if (host_ref && host)
			*host_ref = g_strdup(host);
		if (port_ref && port)
			*port_ref = g_strdup(port);
	}
	g_strfreev(fields);
	return valid ? SR_OK : SR_ERR_ARG;
}

/* }}} */
/* {{{ transport methods called by the common serial.c code */

/* See if a serial port's name refers to a raw TCP connection. */
SR_PRIV int ser_name_is_tcpraw(struct sr_serial_dev_inst *serial)
{
	char *p;

	if (!serial)
		return 0;
	if (!serial->port || !*serial->port)
		return 0;

	p = serial->port;
	if (!g_str_has_prefix(p, SER_TCPRAW_CONN_PREFIX))
		return 0;
	p += strlen(SER_TCPRAW_CONN_PREFIX);
	if (*p != '/')
		return 0;

	return 1;
}

static int ser_tcpraw_open(struct sr_serial_dev_inst *serial, int flags)
{
	char *host, *port;
	int ret;

	(void)flags;

	ret = ser_tcpraw_parse_conn_spec(serial, serial->port,
			&host, &port);
	if (ret != SR_OK) {
		g_free(host);
		g_free(port);
		return SR_ERR_ARG;
	}
	serial->tcp_dev = sr_tcp_dev_inst_new(host, port);
	g_free(host);
	g_free(port);
	if (!serial->tcp_dev)
		return SR_ERR_MALLOC;

	/*
	 * Open the TCP socket. Only keep caller's parameters (and the
	 * resulting socket fd) when open completes successfully.
	 */
	ret = sr_tcp_connect(serial->tcp_dev);
	if (ret != SR_OK) {
		sr_err("Failed to establish TCP connection.");
		sr_tcp_dev_inst_free(serial->tcp_dev);
		serial->tcp_dev = NULL;
		return SR_ERR_IO;
	}

	return SR_OK;
}

static int ser_tcpraw_close(struct sr_serial_dev_inst *serial)
{
	if (!serial)
		return SR_ERR_ARG;

	if (!serial->tcp_dev)
		return SR_OK;

	(void)sr_tcp_disconnect(serial->tcp_dev);
	return SR_OK;
}

#ifdef SER_TCPRAW_TRY_AUTO_RECONNECT
static gboolean sr_tcpraw_reconnect_internal(int ret,
	struct sr_serial_dev_inst *serial)
{
	struct sr_tcp_dev_inst *tcp = serial->tcp_dev;

	if (ret != SR_ERR_IO || (errno != ENOTCONN && errno != EBADF))
		return FALSE;
	if (sr_tcp_disconnect(tcp) != SR_OK)
		return FALSE;

	sr_info("Trying reconnect to %s:%s", tcp->host_addr, tcp->tcp_port);
	if (sr_tcp_connect(serial->tcp_dev) != SR_OK) {
		sr_err("Failed reconnected to %s:%s. Error: %d", tcp->host_addr,
			tcp->tcp_port, errno);
		return FALSE;
	}
	sr_info("Successfully reconnected to %s:%s", tcp->host_addr, tcp->tcp_port);
	return TRUE;
}
#endif

static int ser_tcpraw_setup_source_add(struct sr_session *session,
	struct sr_serial_dev_inst *serial, int events, int timeout,
	sr_receive_data_callback cb, void *cb_data)
{
#ifndef _WIN32
	if (!serial || !serial->tcp_dev)
		return SR_ERR_ARG;
	return sr_tcp_source_add(session, serial->tcp_dev,
		events, timeout, cb, cb_data);
#else
	struct sr_tcp_dev_inst *tcp;
	HANDLE wsa_evt;
	int ret;

	if (!serial || !serial->tcp_dev || serial->tcp_dev->sock_fd < 0)
	 	return SR_ERR_ARG;

	/* Initializing serial->sp_data */
	if (serial->sp_data) 
		sr_warn("sp_data not NULL (%p) in tcpraw source add", serial->sp_data);
	serial->sp_data = NULL;	

	/* Creating WinSock2 event for G_IO_IN & G_IO_OUT events in glib. */
	/* TODO: LOW - Move that code to tcp.c (need field for save event handle). */
	/*
	 * TODO: VERYLOW - Implement something like "sr_session_source_add_socket" 
	 * based on g_socket_create_source and can be handled by glib 
	 * transparently (need testing).
	 */
	wsa_evt = WSACreateEvent();
	if (wsa_evt == WSA_INVALID_EVENT)
		return SR_ERR_BUG;
	sr_spew("Created WS2 pollfd event %p", wsa_evt);

	tcp = serial->tcp_dev;
	if (WSAEventSelect(tcp->sock_fd, wsa_evt, FD_READ) != 0) {
		sr_err("Cant select WS2 socket %x for poolfd event %p", tcp->sock_fd,
			wsa_evt);
		WSACloseEvent(wsa_evt);
		return SR_ERR_IO;
	}

	ret = sr_session_fd_source_add(session, GINT_TO_POINTER(tcp->sock_fd),
		(gintptr)wsa_evt, events, timeout, cb, cb_data);
	if (ret != SR_OK) {
		WSACloseEvent(wsa_evt);
		wsa_evt = NULL;
	}

	/* Using serial sp_data field for store WS2 event handle. */
	serial->sp_data = (struct sp_port *)wsa_evt;
	return ret;
#endif
}

static int ser_tcpraw_setup_source_remove(struct sr_session *session,
	struct sr_serial_dev_inst *serial)
{
	if (!serial || !serial->tcp_dev)
		return SR_ERR_ARG;

#ifdef _WIN32
	if (serial->sp_data) {
		/* Closing WS2 event handle stored in serial sp_data field. */
		if (WSACloseEvent(serial->sp_data))
			sr_spew("Closed WS2 poolfd event %p", serial->sp_data);
		else
			sr_warn("Cant close WS2 poolfd event %p", serial->sp_data);
		serial->sp_data = NULL;
	}
#endif

	(void)sr_tcp_source_remove(session, serial->tcp_dev);
	return SR_OK;
}

static int ser_tcpraw_write(struct sr_serial_dev_inst *serial,
	const void *buf, size_t count,
	int nonblocking, unsigned int timeout_ms)
{
	size_t total, written;
	ssize_t ret;

	/* Non-blocking writes, and write timeouts, are not supported. */
	(void)nonblocking;
	(void)timeout_ms;

	if (!serial || !serial->tcp_dev)
		return SR_ERR_ARG;

	total = 0;
	while (count) {
		ret = sr_tcp_write_bytes(serial->tcp_dev, buf, count);

#ifdef SER_TCPRAW_TRY_AUTO_RECONNECT
		/*
		 * All device driver send commands for start acquisition.
		 * So we can do reconnect there with "free" check,
		 * and socket fd used for source key still be valid.
		 */
		if (ret < 0 && !total && sr_tcpraw_reconnect_internal(ret, serial))
			ret = sr_tcp_write_bytes(serial->tcp_dev, buf, count);
#endif

		if (ret < 0 && !total) {
			sr_err("Error sending TCP transmit data.");
			return total;
		}
		if (ret <= 0) {
			count += total;
			sr_warn("Short transmission of TCP data (%zu/%zu).",
				total, count);
			return total;
		}
		written = (size_t)ret;
		buf += written;
		count -= written;
		total += written;
	}

	return total;
}

static int ser_tcpraw_read(struct sr_serial_dev_inst *serial,
	void *buf, size_t count,
	int nonblocking, unsigned int timeout_ms)
{
	guint64 deadline_us, now_us;
	size_t total, chunk;
	ssize_t ret;

	if (!serial || !serial->tcp_dev)
		return SR_ERR_ARG;
	if (!count)
		return 0;

	/*
	 * Timeouts are only useful in blocking mode, non-blocking read
	 * will return as soon as an iteration sees no more data.
	 * Silence a (false) compiler warning, always assign to 'now_us'.
	 */
	if (nonblocking)
		timeout_ms = 0;
	deadline_us = now_us = 0;
	if (timeout_ms) {
		now_us = g_get_monotonic_time();
		deadline_us = now_us + timeout_ms * 1000;
	}

	/*
	 * Keep reading until the caller's requested length is reached,
	 * or fatal errors are seen, or specified timeouts have expired.
	 */
	total = 0;
	while (count) {
		ret = sr_tcp_read_bytes(serial->tcp_dev,
			buf, count, nonblocking);
		if (ret < 0 && !total) {
			sr_err("Failed to receive TCP data.");
			break;
		}
		if (ret < 0) {
			/* Short read, not worth warning about. */
			break;
		}
		if (ret == 0 && nonblocking)
			break;
		if (ret == 0 && deadline_us) {
			now_us = g_get_monotonic_time();
			if (now_us >= deadline_us)
				break;
			g_usleep(10 * 1000);
			continue;
		}
		chunk = (size_t)ret;
		buf += chunk;
		count -= chunk;
		total += chunk;
	}

	return total;
}

static int tcpraw_drain_internal(struct sr_tcp_dev_inst *tcp,
	int timeout, gboolean clear)
{
	unsigned char *buf = g_malloc(1024);
	fd_set rset;
	int ret, len = 0;
	struct timeval tv;

	FD_ZERO(&rset);
	FD_SET(tcp->sock_fd, &rset);

	tv.tv_sec = 0;
	tv.tv_usec = timeout;

	do {
		ret = select(tcp->sock_fd+1, &rset, NULL, NULL, &tv);
		if (ret > 0) {
			if (clear)
				len += sr_tcp_read_bytes(tcp, buf, 1024, TRUE);
			else
				break;
		}
	} while (ret > 0);

	g_free(buf);

	if (clear)
		sr_spew("Drained %d bytes of data.", len);

	return len;
}

static int ser_tcpraw_drain(struct sr_serial_dev_inst *serial)
{
	if (!serial || !serial->tcp_dev)
		return SR_ERR_ARG;

	tcpraw_drain_internal(serial->tcp_dev, DRAIN_TIMEOUT, FALSE);
	return SR_OK;
}

static size_t ser_tcpraw_get_rx_avail(struct sr_serial_dev_inst *serial)
{
	struct sr_tcp_dev_inst *tcp;

	if (!serial || !serial->tcp_dev)
		return 0;
	tcp = serial->tcp_dev;

#ifdef _WIN32
	u_long bytes_available;
	if (ioctlsocket(tcp->sock_fd, FIONREAD, &bytes_available) != 0) {
#else
	int bytes_available;
	if (ioctl(tcp->sock_fd, FIONREAD, &bytes_available) < 0) {
#endif
		sr_err("FIONREAD failed: %s\n", g_strerror(errno));
		return 0;
	}
	return bytes_available;
}

/* Just clean incoming buffers. */
static int ser_tcpraw_flush(struct sr_serial_dev_inst *serial)
{
	if (!serial || !serial->tcp_dev)
		return SR_ERR;
	tcpraw_drain_internal(serial->tcp_dev, FLUSH_TIMEOUT, TRUE);
	return SR_OK;
}


static struct ser_lib_functions serlib_tcpraw = {
	.open = ser_tcpraw_open,
	.close = ser_tcpraw_close,
	.write = ser_tcpraw_write,
	.read = ser_tcpraw_read,
	.drain = ser_tcpraw_drain,
	.flush = ser_tcpraw_flush,
	.get_rx_avail = ser_tcpraw_get_rx_avail,
	.set_params = std_dummy_set_params,
	.set_handshake = std_dummy_set_handshake,
	.setup_source_add = ser_tcpraw_setup_source_add,
	.setup_source_remove = ser_tcpraw_setup_source_remove,
	.get_frame_format = NULL,
};
SR_PRIV struct ser_lib_functions *ser_lib_funcs_tcpraw = &serlib_tcpraw;

/** @} */
