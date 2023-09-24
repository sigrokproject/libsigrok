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

#include "config.h"

#include <errno.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <string.h>

#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "scpi_tcp"

#define LENGTH_BYTES sizeof(uint32_t)

struct scpi_tcp {
	struct sr_tcp_dev_inst *tcp_dev;
	uint8_t length_buf[LENGTH_BYTES];
	size_t length_bytes_read;
	size_t response_length;
	size_t response_bytes_read;
};

static int scpi_tcp_dev_inst_new(void *priv, struct drv_context *drvc,
		const char *resource, char **params, const char *serialcomm)
{
	struct scpi_tcp *tcp = priv;

	(void)drvc;
	(void)resource;
	(void)serialcomm;

	if (!params || !params[1] || !params[2]) {
		sr_err("Invalid parameters.");
		return SR_ERR;
	}

	tcp->tcp_dev = sr_tcp_dev_inst_new(params[1], params[2]);
	if (!tcp->tcp_dev)
		return SR_ERR;

	return SR_OK;
}

static int scpi_tcp_open(struct sr_scpi_dev_inst *scpi)
{
	struct scpi_tcp *tcp = scpi->priv;
	int ret;

	ret = sr_tcp_connect(tcp->tcp_dev);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static int scpi_tcp_connection_id(struct sr_scpi_dev_inst *scpi,
		char **connection_id)
{
	struct scpi_tcp *tcp = scpi->priv;
	char conn_text[128];
	int ret;

	ret = sr_tcp_get_port_path(tcp->tcp_dev, scpi->prefix, '/',
		conn_text, sizeof(conn_text));
	if (ret != SR_OK)
		return ret;

	*connection_id = g_strdup(conn_text);
	return SR_OK;
}

static int scpi_tcp_source_add(struct sr_session *session, void *priv,
		int events, int timeout, sr_receive_data_callback cb, void *cb_data)
{
	struct scpi_tcp *tcp = priv;

	return sr_tcp_source_add(session, tcp->tcp_dev,
		events, timeout, cb, cb_data);
}

static int scpi_tcp_source_remove(struct sr_session *session, void *priv)
{
	struct scpi_tcp *tcp = priv;

	return sr_tcp_source_remove(session, tcp->tcp_dev);
}

/* Transmit text, usually a command. tcp-raw and tcp-rigol modes. */
static int scpi_tcp_send(void *priv, const char *command)
{
	struct scpi_tcp *tcp = priv;
	const uint8_t *wrptr;
	size_t wrlen, written;
	int ret;

	wrptr = (const uint8_t *)command;
	wrlen = strlen(command);
	ret = sr_tcp_write_bytes(tcp->tcp_dev, wrptr, wrlen);
	if (ret < 0) {
		sr_err("Send error: %s", g_strerror(errno));
		return SR_ERR;
	}
	written = (size_t)ret;
	if (written < wrlen) {
		sr_dbg("Only sent %zu/%zu bytes of SCPI command: '%s'.",
			written, wrlen, command);
	}

	sr_spew("Successfully sent SCPI command: '%s'.", command);

	return SR_OK;
}

/* Start reception across multiple read calls. tcp-raw and tcp-rigol modes. */
static int scpi_tcp_read_begin(void *priv)
{
	struct scpi_tcp *tcp = priv;

	tcp->response_bytes_read = 0;
	tcp->length_bytes_read = 0;

	return SR_OK;
}

/* Receive response data. tcp-raw mode. */
static int scpi_tcp_raw_read_data(void *priv, char *buf, int maxlen)
{
	struct scpi_tcp *tcp = priv;
	uint8_t *rdptr;
	size_t rdlen, rcvd;
	int ret;

	/* Get another chunk of receive data. */
	rdptr = (uint8_t *)buf;
	rdlen = maxlen;
	ret = sr_tcp_read_bytes(tcp->tcp_dev, rdptr, rdlen, FALSE);
	if (ret < 0) {
		sr_err("Receive error: %s", g_strerror(errno));
		return SR_ERR;
	}
	rcvd = (size_t)ret;

	/*
	 * Raw data mode (in contrast to Rigol mode). Prepare to answer
	 * the "completed" condition while the payload's length is not
	 * known. Pretend that the length buffer had been received.
	 * Assume that short reads correspond to the end of a response,
	 * while full reads of the caller specified size suggest that
	 * more data can follow.
	 */
	tcp->length_bytes_read = LENGTH_BYTES;
	tcp->response_length = rcvd < rdlen ? rcvd : rdlen + 1;
	tcp->response_bytes_read = rcvd;

	return rcvd;
}

/* Transmit data of given length. tcp-raw mode. */
static int scpi_tcp_raw_write_data(void *priv, char *buf, int len)
{
	struct scpi_tcp *tcp = priv;
	const uint8_t *wrptr;
	size_t wrlen, sent;
	int ret;

	wrptr = (const uint8_t *)buf;
	wrlen = len;
	ret = sr_tcp_write_bytes(tcp->tcp_dev, wrptr, wrlen);
	if (ret < 0) {
		sr_err("Send error: %s.", g_strerror(errno));
		return SR_ERR;
	}
	sent = (size_t)ret;

	return sent;
}

/* Receive response data. tcp-rigol mode. */
static int scpi_tcp_rigol_read_data(void *priv, char *buf, int maxlen)
{
	struct scpi_tcp *tcp = priv;
	uint8_t *rdptr;
	size_t rdlen, rcvd;
	int ret;

	/*
	 * Rigol mode, chunks are prefixed by a length spec.
	 * Get more length bytes when we haven't read them before.
	 * Return "zero length read" if length has yet to get received.
	 * Otherwise get chunk length from length bytes buffer.
	 */
	if (tcp->length_bytes_read < sizeof(tcp->length_buf)) {
		rdptr = &tcp->length_buf[tcp->length_bytes_read];
		rdlen = sizeof(tcp->length_buf) - tcp->length_bytes_read;
		ret = sr_tcp_read_bytes(tcp->tcp_dev, rdptr, rdlen, FALSE);
		if (ret < 0) {
			sr_err("Receive error: %s", g_strerror(errno));
			return SR_ERR;
		}
		rcvd = (size_t)ret;
		tcp->length_bytes_read += rcvd;
		if (tcp->length_bytes_read < sizeof(tcp->length_buf))
			return 0;
		tcp->response_length = read_u32le(tcp->length_buf);
	}

	/* Received more chunk data than announced size? Fatal. */
	if (tcp->response_bytes_read >= tcp->response_length)
		return SR_ERR;

	/* Read another chunk of the receive data. */
	rdptr = (uint8_t *)buf;
	rdlen = maxlen;
	ret = sr_tcp_read_bytes(tcp->tcp_dev, rdptr, rdlen, FALSE);
	if (ret < 0) {
		sr_err("Receive error: %s", g_strerror(errno));
		return SR_ERR;
	}
	rcvd = (size_t)ret;
	tcp->response_bytes_read += rcvd;

	return rcvd;
}

/* Check reception completion. tcp-raw and tcp-rigol modes. */
static int scpi_tcp_read_complete(void *priv)
{
	struct scpi_tcp *tcp = priv;
	gboolean have_length, have_response;

	have_length = tcp->length_bytes_read == LENGTH_BYTES;
	have_response = tcp->response_bytes_read >= tcp->response_length;

	return have_length && have_response;
}

static int scpi_tcp_close(struct sr_scpi_dev_inst *scpi)
{
	struct scpi_tcp *tcp = scpi->priv;
	int ret;

	ret = sr_tcp_disconnect(tcp->tcp_dev);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static void scpi_tcp_free(void *priv)
{
	struct scpi_tcp *tcp = priv;

	sr_tcp_dev_inst_free(tcp->tcp_dev);
}

SR_PRIV const struct sr_scpi_dev_inst scpi_tcp_raw_dev = {
	.name          = "RAW TCP",
	.prefix        = "tcp-raw",
	.transport     = SCPI_TRANSPORT_RAW_TCP,
	.priv_size     = sizeof(struct scpi_tcp),
	.dev_inst_new  = scpi_tcp_dev_inst_new,
	.open          = scpi_tcp_open,
	.connection_id = scpi_tcp_connection_id,
	.source_add    = scpi_tcp_source_add,
	.source_remove = scpi_tcp_source_remove,
	.send          = scpi_tcp_send,
	.read_begin    = scpi_tcp_read_begin,
	.read_data     = scpi_tcp_raw_read_data,
	.write_data    = scpi_tcp_raw_write_data,
	.read_complete = scpi_tcp_read_complete,
	.close         = scpi_tcp_close,
	.free          = scpi_tcp_free,
};

SR_PRIV const struct sr_scpi_dev_inst scpi_tcp_rigol_dev = {
	.name          = "RIGOL TCP",
	.prefix        = "tcp-rigol",
	.transport     = SCPI_TRANSPORT_RIGOL_TCP,
	.priv_size     = sizeof(struct scpi_tcp),
	.dev_inst_new  = scpi_tcp_dev_inst_new,
	.open          = scpi_tcp_open,
	.connection_id = scpi_tcp_connection_id,
	.source_add    = scpi_tcp_source_add,
	.source_remove = scpi_tcp_source_remove,
	.send          = scpi_tcp_send,
	.read_begin    = scpi_tcp_read_begin,
	.read_data     = scpi_tcp_rigol_read_data,
	.read_complete = scpi_tcp_read_complete,
	.close         = scpi_tcp_close,
	.free          = scpi_tcp_free,
};
