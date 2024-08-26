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

/* TODO
 * Can we sort these include directives? Or do the platform specific
 * headers depend on a specific order? Experience from VXI maintenance
 * suggests that some systems can be picky and it's hard to notice ...
 * For now the include statements follow the scpi_tcp.c template.
 */
#if defined _WIN32
#define _WIN32_WINNT 0x0501
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

#include <errno.h>
#include <glib.h>
#include <string.h>
#include <unistd.h>

#if !defined _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

#if HAVE_POLL
#include <poll.h>
#elif HAVE_SELECT
#include <sys/select.h>
#endif

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/*
 * Workaround because Windows cannot simply use established identifiers.
 * https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-shutdown
 */
#if !defined SHUT_RDWR && defined SD_BOTH
#  define SHUT_RDWR SD_BOTH
#endif

#define LOG_PREFIX "tcp"

/**
 * Check whether a file descriptor is readable (without blocking).
 *
 * @param[in] fd The file descriptor to check for readability.
 *
 * @return TRUE when readable, FALSE when read would block or when
 *   readability could not get determined.
 *
 * @since 6.0
 *
 * TODO Move to common code, applies to non-sockets as well.
 */
SR_PRIV gboolean sr_fd_is_readable(int fd)
{
#if HAVE_POLL
	struct pollfd fds[1];
	int ret;

	memset(fds, 0, sizeof(fds));
	fds[0].fd = fd;
	fds[0].events = POLLIN;
	ret = poll(fds, ARRAY_SIZE(fds), -1);
	if (ret < 0)
		return FALSE;
	if (!ret)
		return FALSE;
	if (!(fds[0].revents & POLLIN))
		return FALSE;

	return TRUE;
#elif HAVE_SELECT
	fd_set rfds;
	struct timeval tv;
	int ret;

	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	memset(&tv, 0, sizeof(tv));
	ret = select(fd + 1, &rfds, NULL, NULL, &tv);
	if (ret < 0)
		return FALSE;
	if (!ret)
		return FALSE;
	if (!FD_ISSET(fd, rfds))
		return FALSE;
	return TRUE;
#else
	(void)fd;
	return FALSE;
#endif
}

/**
 * Create a TCP communication instance.
 *
 * @param[in] host_addr The host name or IP address (a string).
 * @param[in] tcp_port The TCP port number.
 *
 * @return A @ref sr_tcp_dev_inst structure on success. #NULL otherwise.
 *
 * @since 6.0
 */
SR_PRIV struct sr_tcp_dev_inst *sr_tcp_dev_inst_new(
	const char *host_addr, const char *tcp_port)
{
	char *host, *port;
	struct sr_tcp_dev_inst *tcp;

	host = NULL;
	if (host_addr && *host_addr)
		host = g_strdup(host_addr);
	port = NULL;
	if (tcp_port && *tcp_port)
		port = g_strdup(tcp_port);

	tcp = g_malloc0(sizeof(*tcp));
	if (!tcp)
		return NULL;
	tcp->host_addr = host;
	tcp->tcp_port = port;
	tcp->sock_fd = -1;
	return tcp;
}

/**
 * Release a TCP communication instance.
 *
 * @param[in] tcp TCP connection instance to free. If NULL, the function will do
 *                nothing.
 *
 * @since 6.0
 */
SR_PRIV void sr_tcp_dev_inst_free(struct sr_tcp_dev_inst *tcp)
{

	if (!tcp)
		return;

	(void)sr_tcp_disconnect(tcp);
	g_free(tcp->host_addr);
	g_free(tcp->tcp_port);
	g_free(tcp);
}

/**
 * Construct display name for a TCP communication instance.
 *
 * @param[in] tcp The TCP communication instance to print the name of.
 * @param[in] prefix An optional prefix text, or #NULL.
 * @param[in] separator An optional separator character, or NUL.
 * @param[out] path The caller provided buffer to fill in.
 * @param[in] path_len The buffer's maximum length to fill in.
 *
 * @return SR_OK on success, SR_ERR_* otherwise.
 *
 * @since 6.0
 */
SR_PRIV int sr_tcp_get_port_path(struct sr_tcp_dev_inst *tcp,
	const char *prefix, char separator, char *path, size_t path_len)
{
	char sep_text[2];

	/* Only construct connection name for full parameter sets. */
	if (!tcp || !tcp->host_addr || !tcp->tcp_port)
		return SR_ERR_ARG;

	/* Normalize input. Apply defaults. */
	if (!prefix)
		prefix = "";
	if (!*prefix && !separator)
		separator = ':';

	/* Turn everything into strings. Simplifies the printf() call. */
	sep_text[0] = separator;
	sep_text[1] = '\0';

	/* Construct the resulting connection name. */
	snprintf(path, path_len, "%s%s%s%s%s",
		prefix, *prefix ? sep_text : "",
		tcp->host_addr, sep_text, tcp->tcp_port);
	return SR_OK;
}

/**
 * Connect to a remote TCP communication peer.
 *
 * @param[in] tcp The TCP communication instance to connect.
 *
 * @return SR_OK on success, SR_ERR_* otherwise.
 *
 * @since 6.0
 */
SR_PRIV int sr_tcp_connect(struct sr_tcp_dev_inst *tcp)
{
	struct addrinfo hints;
	struct addrinfo *results, *r;
	int ret;
	int fd;

	if (!tcp)
		return SR_ERR_ARG;
	if (!tcp->host_addr || !tcp->tcp_port)
		return SR_ERR_ARG;

	/* Lookup address information for the caller's spec. */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	ret = getaddrinfo(tcp->host_addr, tcp->tcp_port, &hints, &results);
	if (ret != 0) {
		sr_err("Address lookup failed: %s:%s: %s.",
			tcp->host_addr, tcp->tcp_port, gai_strerror(ret));
		return SR_ERR_DATA;
	}

	/* Try to connect using the resulting address details. */
	fd = -1;
	for (r = results; r; r = r->ai_next) {
		fd = socket(r->ai_family, r->ai_socktype, r->ai_protocol);
		if (fd < 0)
			continue;
		ret = connect(fd, r->ai_addr, r->ai_addrlen);
		if (ret != 0) {
			close(fd);
			fd = -1;
			continue;
		}
		break;
	}
	freeaddrinfo(results);
	if (fd < 0) {
		sr_err("Failed to connect to %s:%s: %s.",
			tcp->host_addr, tcp->tcp_port, g_strerror(errno));
		return SR_ERR_IO;
	}

	tcp->sock_fd = fd;
	return SR_OK;
}

/**
 * Disconnect from a remote TCP communication peer.
 *
 * @param[in] tcp The TCP communication instance to disconnect.
 *
 * @return SR_OK on success, SR_ERR_* otherwise.
 *
 * @since 6.0
 */
SR_PRIV int sr_tcp_disconnect(struct sr_tcp_dev_inst *tcp)
{

	if (!tcp)
		return SR_ERR_ARG;

	if (tcp->sock_fd < 0)
		return SR_OK;

	shutdown(tcp->sock_fd, SHUT_RDWR);
	close(tcp->sock_fd);
	tcp->sock_fd = -1;
	return SR_OK;
}

/**
 * Send transmit data to a TCP connection.
 * Does a single operating system call, can return with short
 * transmit byte counts. Will not continue after short writes,
 * callers need to handle the condition.
 *
 * @param[in] tcp The TCP communication instance to send to.
 * @param[in] data The data bytes to send.
 * @param[in] dlen The number of bytes to send.
 *
 * @return Number of transmitted bytes on success, SR_ERR_* otherwise.
 *
 * @since 6.0
 */
SR_PRIV int sr_tcp_write_bytes(struct sr_tcp_dev_inst *tcp,
	const uint8_t *data, size_t dlen)
{
	ssize_t rc;
	size_t written;

	if (!tcp)
		return SR_ERR_ARG;
	if (!dlen)
		return 0;
	if (!data)
		return SR_ERR_ARG;

	if (tcp->sock_fd < 0)
		return SR_ERR_IO;

	rc = send(tcp->sock_fd, data, dlen, 0);
	if (rc < 0)
		return SR_ERR_IO;
	written = (size_t)rc;
	return written;
}

/**
 * Fetch receive data from a TCP connection.
 * Does a single operating system call, can return with short
 * receive byte counts. Will not continue after short reads,
 * callers need to handle the condition.
 *
 * @param[in] tcp The TCP communication instance to read from.
 * @param[in] data Caller provided buffer for receive data.
 * @param[in] dlen The maximum number of bytes to receive.
 * @param[in] nonblocking Whether to block for receive data.
 *
 * @return Number of received bytes on success, SR_ERR_* otherwise.
 *
 * @since 6.0
 */
SR_PRIV int sr_tcp_read_bytes(struct sr_tcp_dev_inst *tcp,
	uint8_t *data, size_t dlen, gboolean nonblocking)
{
	ssize_t rc;
	size_t got;

	if (!tcp)
		return SR_ERR_ARG;
	if (!dlen)
		return 0;
	if (!data)
		return SR_ERR_ARG;

	if (tcp->sock_fd < 0)
		return SR_ERR_IO;

	if (nonblocking && !sr_fd_is_readable(tcp->sock_fd))
		return 0;

	rc = recv(tcp->sock_fd, data, dlen, 0);
	if (rc < 0)
		return SR_ERR_IO;
	got = (size_t)rc;
	return got;
}

/**
 * Register receive callback for a TCP connection.
 * The connection must have been established before. The callback
 * gets invoked when receive data is available. Or when a timeout
 * has expired.
 *
 * This is a simple wrapper around @ref sr_session_source_add().
 *
 * @param[in] session See @ref sr_session_source_add().
 * @param[in] tcp The TCP communication instance to read from.
 * @param[in] events See @ref sr_session_source_add().
 * @param[in] timeout See @ref sr_session_source_add().
 * @param[in] cb See @ref sr_session_source_add().
 * @param[in] cb_data See @ref sr_session_source_add().
 *
 * @return SR_OK on success, SR_ERR* otherwise.
 *
 * @since 6.0
 */
SR_PRIV int sr_tcp_source_add(struct sr_session *session,
	struct sr_tcp_dev_inst *tcp, int events, int timeout,
	sr_receive_data_callback cb, void *cb_data)
{
	if (!tcp || tcp->sock_fd < 0)
		return SR_ERR_ARG;
	return sr_session_source_add(session, tcp->sock_fd,
		events, timeout, cb, cb_data);
}

/**
 * Unregister receive callback for a TCP connection.
 *
 * This is a simple wrapper around @ref sr_session_source_remove().
 *
 * @param[in] session See @ref sr_session_source_remove().
 * @param[in] tcp The TCP communication instance to unregister.
 *
 * @return SR_OK on success, SR_ERR* otherwise.
 *
 * @since 6.0
 */
SR_PRIV int sr_tcp_source_remove(struct sr_session *session,
	struct sr_tcp_dev_inst *tcp)
{
	if (!tcp || tcp->sock_fd < 0)
		return SR_ERR_ARG;
	return sr_session_source_remove(session, tcp->sock_fd);
}
