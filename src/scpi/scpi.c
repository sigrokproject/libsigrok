/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 poljar (Damir Jelić) <poljarinho@gmail.com>
 * Copyright (C) 2015 Bert Vermeulen <bert@biot.com>
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
#include <glib.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "scpi"

#define SCPI_OPC_RETRY_COUNT	100
#define SCPI_OPC_RETRY_DELAY_US	(10 * 1000)

static const char *scpi_vendors[][2] = {
	{ "Agilent Technologies", "Agilent" },
	{ "CHROMA", "Chroma" },
	{ "Chroma ATE", "Chroma" },
	{ "HEWLETT-PACKARD", "HP" },
	{ "Keysight Technologies", "Keysight" },
	{ "PHILIPS", "Philips" },
	{ "RIGOL TECHNOLOGIES", "Rigol" },
	{ "Siglent Technologies", "Siglent" },
};

/**
 * Parse a string representation of a boolean-like value into a gboolean.
 * Similar to sr_parse_boolstring but rejects strings which do not represent
 * a boolean-like value.
 *
 * @param str String to convert.
 * @param ret Pointer to a gboolean where the result of the conversion will be
 * stored.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
static int parse_strict_bool(const char *str, gboolean *ret)
{
	if (!str)
		return SR_ERR_ARG;

	if (!g_strcmp0(str, "1") ||
	    !g_ascii_strncasecmp(str, "y", 1) ||
	    !g_ascii_strncasecmp(str, "t", 1) ||
	    !g_ascii_strncasecmp(str, "yes", 3) ||
	    !g_ascii_strncasecmp(str, "true", 4) ||
	    !g_ascii_strncasecmp(str, "on", 2)) {
		*ret = TRUE;
		return SR_OK;
	} else if (!g_strcmp0(str, "0") ||
		   !g_ascii_strncasecmp(str, "n", 1) ||
		   !g_ascii_strncasecmp(str, "f", 1) ||
		   !g_ascii_strncasecmp(str, "no", 2) ||
		   !g_ascii_strncasecmp(str, "false", 5) ||
		   !g_ascii_strncasecmp(str, "off", 3)) {
		*ret = FALSE;
		return SR_OK;
	}

	return SR_ERR;
}

SR_PRIV extern const struct sr_scpi_dev_inst scpi_serial_dev;
SR_PRIV extern const struct sr_scpi_dev_inst scpi_tcp_raw_dev;
SR_PRIV extern const struct sr_scpi_dev_inst scpi_tcp_rigol_dev;
SR_PRIV extern const struct sr_scpi_dev_inst scpi_usbtmc_libusb_dev;
SR_PRIV extern const struct sr_scpi_dev_inst scpi_vxi_dev;
SR_PRIV extern const struct sr_scpi_dev_inst scpi_visa_dev;
SR_PRIV extern const struct sr_scpi_dev_inst scpi_libgpib_dev;

static const struct sr_scpi_dev_inst *scpi_devs[] = {
	&scpi_tcp_raw_dev,
	&scpi_tcp_rigol_dev,
#ifdef HAVE_LIBUSB_1_0
	&scpi_usbtmc_libusb_dev,
#endif
#if HAVE_RPC
	&scpi_vxi_dev,
#endif
#ifdef HAVE_LIBREVISA
	&scpi_visa_dev,
#endif
#ifdef HAVE_LIBGPIB
	&scpi_libgpib_dev,
#endif
#ifdef HAVE_SERIAL_COMM
	&scpi_serial_dev, /* Must be last as it matches any resource. */
#endif
};

static struct sr_dev_inst *sr_scpi_scan_resource(struct drv_context *drvc,
	const char *resource, const char *serialcomm,
	struct sr_dev_inst *(*probe_device)(struct sr_scpi_dev_inst *scpi))
{
	struct sr_scpi_dev_inst *scpi;
	struct sr_dev_inst *sdi;

	if (!(scpi = scpi_dev_inst_new(drvc, resource, serialcomm)))
		return NULL;

	if (sr_scpi_open(scpi) != SR_OK) {
		sr_info("Couldn't open SCPI device.");
		sr_scpi_free(scpi);
		return NULL;
	};

	sdi = probe_device(scpi);

	sr_scpi_close(scpi);

	if (sdi)
		sdi->status = SR_ST_INACTIVE;
	else
		sr_scpi_free(scpi);

	return sdi;
}

/**
 * Send a SCPI command with a variadic argument list without mutex.
 *
 * @param scpi Previously initialized SCPI device structure.
 * @param format Format string.
 * @param args Argument list.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
static int scpi_send_variadic(struct sr_scpi_dev_inst *scpi,
	const char *format, va_list args)
{
	va_list args_copy;
	char *buf;
	int len, ret;

	/* Get length of buffer required. */
	va_copy(args_copy, args);
	len = sr_vsnprintf_ascii(NULL, 0, format, args_copy);
	va_end(args_copy);

	/* Allocate buffer and write out command. */
	buf = g_malloc0(len + 2);
	sr_vsprintf_ascii(buf, format, args);
	if (buf[len - 1] != '\n')
		buf[len] = '\n';

	/* Send command. */
	ret = scpi->send(scpi->priv, buf);

	/* Free command buffer. */
	g_free(buf);

	return ret;
}

/**
 * Send a SCPI command without mutex.
 *
 * @param scpi Previously initialized SCPI device structure.
 * @param format Format string, to be followed by any necessary arguments.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
static int scpi_send(struct sr_scpi_dev_inst *scpi, const char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	ret = scpi_send_variadic(scpi, format, args);
	va_end(args);

	return ret;
}

/**
 * Send data to SCPI device without mutex.
 *
 * TODO: This is only implemented in TcpRaw, but never used.
 * TODO: Use Mutex at all?
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param buf Buffer with data to send.
 * @param len Number of bytes to send.
 *
 * @return Number of bytes read, or SR_ERR upon failure.
 */
static int scpi_write_data(struct sr_scpi_dev_inst *scpi, char *buf, int maxlen)
{
	return scpi->write_data(scpi->priv, buf, maxlen);
}

/**
 * Read part of a response from SCPI device without mutex.
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param buf Buffer to store result.
 * @param maxlen Maximum number of bytes to read.
 *
 * @return Number of bytes read, or SR_ERR upon failure.
 */
static int scpi_read_data(struct sr_scpi_dev_inst *scpi, char *buf, int maxlen)
{
	return scpi->read_data(scpi->priv, buf, maxlen);
}

/**
 * Make sure a string buffer contains free space, chunked resize.
 *
 * Allocates more space ('add_space' in addition to existing content)
 * when free buffer space is below 'threshold'.
 *
 * @param[in] buf The buffer where free space is desired.
 * @param[in] threshold Minimum amount of free space accepted.
 * @param[in] add_space Chunk to ensure when buffer gets extended.
 */
static void scpi_make_string_space(GString *buf,
	size_t threshold, size_t add_space)
{
	size_t have_space, prev_size;

	have_space = buf->allocated_len - buf->len;
	if (have_space < threshold) {
		prev_size = buf->len;
		g_string_set_size(buf, prev_size + add_space);
		g_string_set_size(buf, prev_size);
	}
}

/**
 * Do a non-blocking read of up to the allocated length, and
 * check if a timeout has occured, without mutex.
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param response Buffer to which the response is appended.
 * @param abs_timeout_us Absolute timeout in microseconds
 *
 * @return read length on success, SR_ERR* on failure.
 */
static int scpi_read_response(struct sr_scpi_dev_inst *scpi,
	GString *response, gint64 abs_timeout_us)
{
	int len, space;

	space = response->allocated_len - response->len;
	len = scpi->read_data(scpi->priv, &response->str[response->len], space);

	if (len < 0) {
		sr_err("Incompletely read SCPI response.");
		return SR_ERR;
	}

	if (len > 0) {
		g_string_set_size(response, response->len + len);
		return len;
	}

	if (g_get_monotonic_time() > abs_timeout_us) {
		sr_err("Timed out waiting for SCPI response.");
		return SR_ERR_TIMEOUT;
	}

	return 0;
}

/**
 * Send a SCPI command, receive the reply and store the reply in
 * scpi_response, without mutex.
 *
 * @param[in] scpi Previously initialised SCPI device structure.
 * @param[in] command The SCPI command to send to the device.
 * @param[out] scpi_response Pointer where to store the SCPI response.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
static int scpi_get_data(struct sr_scpi_dev_inst *scpi,
	const char *command, GString **scpi_response)
{
	int ret;
	GString *response;
	gint64 timeout;

	/* Optionally send caller provided command. */
	if (command) {
		if (scpi_send(scpi, command) != SR_OK)
			return SR_ERR;
	}

	/* Initiate SCPI read operation. */
	if (sr_scpi_read_begin(scpi) != SR_OK)
		return SR_ERR;

	/* Keep reading until completion or until timeout. */
	response = *scpi_response;
	timeout = g_get_monotonic_time() + scpi->read_timeout_us;
	while (!sr_scpi_read_complete(scpi)) {
		/* Resize the buffer when free space drops below a threshold. */
		scpi_make_string_space(response, 128, 1024);

		/* Read another chunk of the response. */
		ret = scpi_read_response(scpi, response, timeout);
		if (ret < 0)
			return ret;
		if (ret > 0)
			timeout = g_get_monotonic_time() + scpi->read_timeout_us;
		if (ret == 0 && scpi->read_pause_us)
			g_usleep(scpi->read_pause_us);
	}

	return SR_OK;
}

SR_PRIV GSList *sr_scpi_scan(struct drv_context *drvc, GSList *options,
	struct sr_dev_inst *(*probe_device)(struct sr_scpi_dev_inst *scpi))
{
	GSList *resources, *l, *devices;
	struct sr_dev_inst *sdi;
	const char *resource, *conn;
	const char *serialcomm, *comm;
	gchar **res;
	unsigned i;

	resource = NULL;
	serialcomm = NULL;
	(void)sr_serial_extract_options(options, &resource, &serialcomm);

	devices = NULL;
	for (i = 0; i < ARRAY_SIZE(scpi_devs); i++) {
		if (resource && strcmp(resource, scpi_devs[i]->prefix) != 0)
			continue;
		if (!scpi_devs[i]->scan)
			continue;
		resources = scpi_devs[i]->scan(drvc);
		for (l = resources; l; l = l->next) {
			res = g_strsplit(l->data, ":", 2);
			if (!res[0]) {
				g_strfreev(res);
				continue;
			}
			conn = res[0];
			comm = serialcomm ? : res[1];
			sdi = sr_scpi_scan_resource(drvc, conn, comm, probe_device);
			if (sdi) {
				devices = g_slist_append(devices, sdi);
				sdi->connection_id = g_strdup(l->data);
			}
			g_strfreev(res);
		}
		g_slist_free_full(resources, g_free);
	}

	if (!devices && resource) {
		sdi = sr_scpi_scan_resource(drvc, resource, serialcomm, probe_device);
		if (sdi)
			devices = g_slist_append(NULL, sdi);
	}

	/* Tack a copy of the newly found devices onto the driver list. */
	if (devices)
		drvc->instances = g_slist_concat(drvc->instances, g_slist_copy(devices));

	return devices;
}

SR_PRIV struct sr_scpi_dev_inst *scpi_dev_inst_new(struct drv_context *drvc,
	const char *resource, const char *serialcomm)
{
	size_t i;
	const struct sr_scpi_dev_inst *scpi_dev;
	struct sr_scpi_dev_inst *scpi;
	gchar **params;
	int ret;

	for (i = 0; i < ARRAY_SIZE(scpi_devs); i++) {
		scpi_dev = scpi_devs[i];
		if (strncmp(resource, scpi_dev->prefix, strlen(scpi_dev->prefix)) != 0)
			continue;
		sr_dbg("Opening %s device %s.", scpi_dev->name, resource);
		scpi = g_malloc0(sizeof(*scpi));
		*scpi = *scpi_dev;
		scpi->priv = g_malloc0(scpi->priv_size);
		scpi->read_timeout_us = 1000 * 1000;
		scpi->read_pause_us = 0;
		params = g_strsplit(resource, "/", 0);
		ret = scpi->dev_inst_new(scpi->priv, drvc,
			resource, params, serialcomm);
		g_strfreev(params);
		if (ret != SR_OK) {
			sr_scpi_free(scpi);
			scpi = NULL;
		}
		return scpi;
	}

	return NULL;
}

/**
 * Open SCPI device.
 *
 * @param scpi Previously initialized SCPI device structure.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_open(struct sr_scpi_dev_inst *scpi)
{
	g_mutex_init(&scpi->scpi_mutex);

	return scpi->open(scpi);
}

/**
 * Get the connection ID of the SCPI device.
 *
 * Callers must free the allocated memory regardless of the routine's
 * return code. See @ref g_free().
 *
 * @param[in] scpi Previously initialized SCPI device structure.
 * @param[out] connection_id Pointer where to store the connection ID.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_connection_id(struct sr_scpi_dev_inst *scpi,
	char **connection_id)
{
	return scpi->connection_id(scpi, connection_id);
}

/**
 * Add an event source for an SCPI device.
 *
 * @param session The session to add the event source to.
 * @param scpi Previously initialized SCPI device structure.
 * @param events Events to check for.
 * @param timeout Max time to wait before the callback is called, ignored if 0.
 * @param cb Callback function to add. Must not be NULL.
 * @param cb_data Data for the callback function. Can be NULL.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors.
 */
SR_PRIV int sr_scpi_source_add(struct sr_session *session,
	struct sr_scpi_dev_inst *scpi, int events, int timeout,
	sr_receive_data_callback cb, void *cb_data)
{
	return scpi->source_add(session, scpi->priv, events, timeout, cb, cb_data);
}

/**
 * Remove event source for an SCPI device.
 *
 * @param session The session to remove the event source from.
 * @param scpi Previously initialized SCPI device structure.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors, SR_ERR_BUG upon
 *         internal errors.
 */
SR_PRIV int sr_scpi_source_remove(struct sr_session *session,
	struct sr_scpi_dev_inst *scpi)
{
	return scpi->source_remove(session, scpi->priv);
}

/**
 * Send a SCPI command.
 *
 * @param scpi Previously initialized SCPI device structure.
 * @param format Format string, to be followed by any necessary arguments.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_send(struct sr_scpi_dev_inst *scpi,
	const char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	g_mutex_lock(&scpi->scpi_mutex);
	ret = scpi_send_variadic(scpi, format, args);
	g_mutex_unlock(&scpi->scpi_mutex);
	va_end(args);

	return ret;
}

/**
 * Send a SCPI command with a variadic argument list.
 *
 * @param scpi Previously initialized SCPI device structure.
 * @param format Format string.
 * @param args Argument list.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_send_variadic(struct sr_scpi_dev_inst *scpi,
	const char *format, va_list args)
{
	int ret;

	g_mutex_lock(&scpi->scpi_mutex);
	ret = scpi_send_variadic(scpi, format, args);
	g_mutex_unlock(&scpi->scpi_mutex);

	return ret;
}

/**
 * Begin receiving an SCPI reply.
 *
 * @param scpi Previously initialised SCPI device structure.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_read_begin(struct sr_scpi_dev_inst *scpi)
{
	return scpi->read_begin(scpi->priv);
}

/**
 * Read part of a response from SCPI device.
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param buf Buffer to store result.
 * @param maxlen Maximum number of bytes to read.
 *
 * @return Number of bytes read, or SR_ERR upon failure.
 */
SR_PRIV int sr_scpi_read_data(struct sr_scpi_dev_inst *scpi,
	char *buf, int maxlen)
{
	int ret;

	g_mutex_lock(&scpi->scpi_mutex);
	ret = scpi_read_data(scpi, buf, maxlen);
	g_mutex_unlock(&scpi->scpi_mutex);

	return ret;
}

/**
 * Send data to SCPI device.
 *
 * TODO: This is only implemented in TcpRaw, but never used.
 * TODO: Use Mutex at all?
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param buf Buffer with data to send.
 * @param len Number of bytes to send.
 *
 * @return Number of bytes read, or SR_ERR upon failure.
 */
SR_PRIV int sr_scpi_write_data(struct sr_scpi_dev_inst *scpi,
	char *buf, int maxlen)
{
	int ret;

	g_mutex_lock(&scpi->scpi_mutex);
	ret = scpi_write_data(scpi, buf, maxlen);
	g_mutex_unlock(&scpi->scpi_mutex);

	return ret;
}

/**
 * Re-check whether remaining payload data contains "end of message".
 *
 * This executes after a binary block has completed, and is passed
 * the remaining previously accumulated receive data. It is only
 * important to those physical transports where "end of message" is
 * communicated by means of payload data, not in handshake signals
 * that are outside of payload bytes.
 *
 * @param scpi Previously initialized SCPI device structure.
 * @param buf Buffer position after binary block.
 * @param len Number of bytes that were received after the block.
 *
 * @return zero upon success, non-zero upon failure.
 */
SR_PRIV int sr_scpi_block_ends(struct sr_scpi_dev_inst *scpi,
	char *buf, size_t len)
{
	if (scpi->block_ends)
		return scpi->block_ends(scpi->priv, buf, len);
	return SR_OK;
}

/**
 * Check whether a complete SCPI response has been received.
 *
 * @param scpi Previously initialised SCPI device structure.
 *
 * @return 1 if complete, 0 otherwise.
 */
SR_PRIV int sr_scpi_read_complete(struct sr_scpi_dev_inst *scpi)
{
	return scpi->read_complete(scpi->priv);
}

/**
 * Close SCPI device.
 *
 * @param scpi Previously initialized SCPI device structure.
 *
 * @return SR_OK on success, SR_ERR on failure.
 */
SR_PRIV int sr_scpi_close(struct sr_scpi_dev_inst *scpi)
{
	int ret;

	g_mutex_lock(&scpi->scpi_mutex);
	ret = scpi->close(scpi);
	g_mutex_unlock(&scpi->scpi_mutex);
	g_mutex_clear(&scpi->scpi_mutex);

	return ret;
}

/**
 * Free SCPI device.
 *
 * @param scpi Previously initialized SCPI device structure. If NULL,
 *             this function does nothing.
 */
SR_PRIV void sr_scpi_free(struct sr_scpi_dev_inst *scpi)
{
	if (!scpi)
		return;

	scpi->free(scpi->priv);
	g_free(scpi->priv);
	g_free(scpi->curr_channel_name);
	g_free(scpi);
}

/**
 * Send a SCPI command, receive the reply and store the reply in scpi_response.
 *
 * Callers must free the allocated memory regardless of the routine's
 * return code. See @ref g_free().
 *
 * @param[in] scpi Previously initialised SCPI device structure.
 * @param[in] command The SCPI command to send to the device (can be NULL).
 * @param[out] scpi_response Pointer where to store the SCPI response.
 *
 * @return SR_OK on success, SR_ERR* on failure.
 */
SR_PRIV int sr_scpi_get_string(struct sr_scpi_dev_inst *scpi,
	const char *command, char **scpi_response)
{
	GString *response;

	*scpi_response = NULL;

	response = g_string_sized_new(1024);
	if (sr_scpi_get_data(scpi, command, &response) != SR_OK) {
		if (response)
			g_string_free(response, TRUE);
		return SR_ERR;
	}

	/* Get rid of trailing linefeed if present */
	if (response->len >= 1 && response->str[response->len - 1] == '\n')
		g_string_truncate(response, response->len - 1);

	/* Get rid of trailing carriage return if present */
	if (response->len >= 1 && response->str[response->len - 1] == '\r')
		g_string_truncate(response, response->len - 1);

	sr_spew("Got response: '%.70s', length %" G_GSIZE_FORMAT ".",
		response->str, response->len);

	*scpi_response = g_string_free(response, FALSE);

	return SR_OK;
}

/**
 * Do a non-blocking read of up to the allocated length, and
 * check if a timeout has occured.
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param response Buffer to which the response is appended.
 * @param abs_timeout_us Absolute timeout in microseconds
 *
 * @return read length on success, SR_ERR* on failure.
 */
SR_PRIV int sr_scpi_read_response(struct sr_scpi_dev_inst *scpi,
	GString *response, gint64 abs_timeout_us)
{
	int ret;

	g_mutex_lock(&scpi->scpi_mutex);
	ret = scpi_read_response(scpi, response, abs_timeout_us);
	g_mutex_unlock(&scpi->scpi_mutex);

	return ret;
}

SR_PRIV int sr_scpi_get_data(struct sr_scpi_dev_inst *scpi,
	const char *command, GString **scpi_response)
{
	int ret;

	g_mutex_lock(&scpi->scpi_mutex);
	ret = scpi_get_data(scpi, command, scpi_response);
	g_mutex_unlock(&scpi->scpi_mutex);

	return ret;
}

/**
 * Send a SCPI command, read the reply, parse it as a bool value and store the
 * result in scpi_response.
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK on success, SR_ERR* on failure.
 */
SR_PRIV int sr_scpi_get_bool(struct sr_scpi_dev_inst *scpi,
	const char *command, gboolean *scpi_response)
{
	int ret;
	char *response;

	response = NULL;

	ret = sr_scpi_get_string(scpi, command, &response);
	if (ret != SR_OK && !response)
		return ret;

	if (parse_strict_bool(response, scpi_response) == SR_OK)
		ret = SR_OK;
	else
		ret = SR_ERR_DATA;

	g_free(response);

	return ret;
}

/**
 * Send a SCPI command, read the reply, parse it as an integer and store the
 * result in scpi_response.
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK on success, SR_ERR* on failure.
 */
SR_PRIV int sr_scpi_get_int(struct sr_scpi_dev_inst *scpi,
	const char *command, int *scpi_response)
{
	int ret;
	struct sr_rational ret_rational;
	char *response;

	response = NULL;

	ret = sr_scpi_get_string(scpi, command, &response);
	if (ret != SR_OK && !response)
		return ret;

	ret = sr_parse_rational(response, &ret_rational);
	if (ret == SR_OK && (ret_rational.p % ret_rational.q) == 0) {
		*scpi_response = ret_rational.p / ret_rational.q;
	} else {
		sr_dbg("get_int: non-integer rational=%" PRId64 "/%" PRIu64,
			ret_rational.p, ret_rational.q);
		ret = SR_ERR_DATA;
	}

	g_free(response);

	return ret;
}

/**
 * Send a SCPI command, read the reply, parse it as a float and store the
 * result in scpi_response.
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK on success, SR_ERR* on failure.
 */
SR_PRIV int sr_scpi_get_float(struct sr_scpi_dev_inst *scpi,
	const char *command, float *scpi_response)
{
	int ret;
	char *response;

	response = NULL;

	ret = sr_scpi_get_string(scpi, command, &response);
	if (ret != SR_OK && !response)
		return ret;

	if (sr_atof_ascii(response, scpi_response) == SR_OK)
		ret = SR_OK;
	else
		ret = SR_ERR_DATA;

	g_free(response);

	return ret;
}

/**
 * Send a SCPI command, read the reply, parse it as a double and store the
 * result in scpi_response.
 *
 * @param scpi Previously initialised SCPI device structure.
 * @param command The SCPI command to send to the device (can be NULL).
 * @param scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK on success, SR_ERR* on failure.
 */
SR_PRIV int sr_scpi_get_double(struct sr_scpi_dev_inst *scpi,
	const char *command, double *scpi_response)
{
	int ret;
	char *response;

	response = NULL;

	ret = sr_scpi_get_string(scpi, command, &response);
	if (ret != SR_OK && !response)
		return ret;

	if (sr_atod_ascii(response, scpi_response) == SR_OK)
		ret = SR_OK;
	else
		ret = SR_ERR_DATA;

	g_free(response);

	return ret;
}

/**
 * Send a SCPI *OPC? command, read the reply and return the result of the
 * command.
 *
 * @param scpi Previously initialised SCPI device structure.
 *
 * @return SR_OK on success, SR_ERR* on failure.
 */
SR_PRIV int sr_scpi_get_opc(struct sr_scpi_dev_inst *scpi)
{
	unsigned int retries;
	gboolean opc;
	int ret;

	retries = SCPI_OPC_RETRY_COUNT;
	while (retries--) {
		opc = FALSE;
		ret = sr_scpi_get_bool(scpi, SCPI_CMD_OPC, &opc);
		if (ret == SR_OK && opc)
			return SR_OK;
		if (retries)
			g_usleep(SCPI_OPC_RETRY_DELAY_US);
	}

	return SR_ERR;
}

/**
 * Send a SCPI command, read the reply, parse it as comma separated list of
 * floats and store the as an result in scpi_response.
 *
 * Callers must free the allocated memory (unless it's NULL) regardless of
 * the routine's return code. See @ref g_array_free().
 *
 * @param[in] scpi Previously initialised SCPI device structure.
 * @param[in] command The SCPI command to send to the device (can be NULL).
 * @param[out] scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK upon successfully parsing all values, SR_ERR* upon a parsing
 *         error or upon no response.
 */
SR_PRIV int sr_scpi_get_floatv(struct sr_scpi_dev_inst *scpi,
	const char *command, GArray **scpi_response)
{
	int ret;
	float tmp;
	char *response;
	gchar **ptr, **tokens;
	size_t token_count;
	GArray *response_array;

	*scpi_response = NULL;

	response = NULL;
	ret = sr_scpi_get_string(scpi, command, &response);
	if (ret != SR_OK && !response)
		return ret;

	tokens = g_strsplit(response, ",", 0);
	token_count = g_strv_length(tokens);

	response_array = g_array_sized_new(TRUE, FALSE,
		sizeof(float), token_count + 1);

	ptr = tokens;
	while (*ptr) {
		ret = sr_atof_ascii(*ptr, &tmp);
		if (ret != SR_OK) {
			ret = SR_ERR_DATA;
			break;
		}
		response_array = g_array_append_val(response_array, tmp);
		ptr++;
	}
	g_strfreev(tokens);
	g_free(response);

	if (ret != SR_OK && response_array->len == 0) {
		g_array_free(response_array, TRUE);
		return SR_ERR_DATA;
	}

	*scpi_response = response_array;

	return ret;
}

/**
 * Send a SCPI command, read the reply, parse it as comma separated list of
 * unsigned 8 bit integers and store the as an result in scpi_response.
 *
 * Callers must free the allocated memory (unless it's NULL) regardless of
 * the routine's return code. See @ref g_array_free().
 *
 * @param[in] scpi Previously initialised SCPI device structure.
 * @param[in] command The SCPI command to send to the device (can be NULL).
 * @param[out] scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK upon successfully parsing all values, SR_ERR* upon a parsing
 *         error or upon no response.
 */
SR_PRIV int sr_scpi_get_uint8v(struct sr_scpi_dev_inst *scpi,
	const char *command, GArray **scpi_response)
{
	int tmp, ret;
	char *response;
	gchar **ptr, **tokens;
	size_t token_count;
	GArray *response_array;

	*scpi_response = NULL;

	response = NULL;
	ret = sr_scpi_get_string(scpi, command, &response);
	if (ret != SR_OK && !response)
		return ret;

	tokens = g_strsplit(response, ",", 0);
	token_count = g_strv_length(tokens);

	response_array = g_array_sized_new(TRUE, FALSE,
		sizeof(uint8_t), token_count + 1);

	ptr = tokens;
	while (*ptr) {
		ret = sr_atoi(*ptr, &tmp);
		if (ret != SR_OK) {
			ret = SR_ERR_DATA;
			break;
		}
		response_array = g_array_append_val(response_array, tmp);
		ptr++;
	}
	g_strfreev(tokens);
	g_free(response);

	if (response_array->len == 0) {
		g_array_free(response_array, TRUE);
		return SR_ERR_DATA;
	}

	*scpi_response = response_array;

	return ret;
}

/* Internal, serves both sr_scpi_get_block(), sr_scpi_get_text_then_block(). */
static int scpi_get_block_int(struct sr_scpi_dev_inst *scpi,
	const char *command,
	sr_scpi_block_find_cb cb_func, void *cb_data,
	GString **text_response, GByteArray **block_response)
{
	int ret;
	GString *response;
	size_t prev_size;
	size_t blk_off;
	char buf[10];
	unsigned long ul_value;
	size_t digits_count, bytes_count;
	gint64 timeout;
	size_t trail_count;
	uint8_t *blk_bytes;

	if (text_response)
		*text_response = NULL;
	if (block_response)
		*block_response = NULL;

	g_mutex_lock(&scpi->scpi_mutex);

	/* Optionally send a caller provided command. */
	if (command) {
		ret = scpi_send(scpi, command);
		sr_dbg("SCPI get block, sent command, ret %d.", ret);
		if (ret != SR_OK) {
			g_mutex_unlock(&scpi->scpi_mutex);
			return SR_ERR;
		}
	}

	/* Start reading a reponse message. */
	ret = sr_scpi_read_begin(scpi);
	sr_dbg("SCPI get block, read begin, ret %d.", ret);
	if (ret != SR_OK) {
		g_mutex_unlock(&scpi->scpi_mutex);
		return SR_ERR;
	}

	/*
	 * Get an optional leading text before the data block when the
	 * caller provided routine can determine the boundary between
	 * the text and the block parts of the response. Absence of a
	 * boundary finder assumes that the response is just a block.
	 */
	response = g_string_sized_new(1024);
	timeout = g_get_monotonic_time() + scpi->read_timeout_us;
	while (cb_func) {
		/* Extend buffer space in sensible chunks. */
		scpi_make_string_space(response, 128, 1024);
		/* Accumulate more receive data. */
		ret = scpi_read_response(scpi, response, timeout);
		sr_dbg("SCPI get block, text read, ret %d, len %zu.",
			ret, response->len);
		if (ret < 0) {
			g_mutex_unlock(&scpi->scpi_mutex);
			g_string_free(response, TRUE);
			return ret;
		}
		if (ret == 0) {
			if (scpi->read_pause_us)
				g_usleep(scpi->read_pause_us);
			continue;
		}
		timeout = g_get_monotonic_time() + scpi->read_timeout_us;
		/*
		 * Run caller's routine to find the start of the block.
		 * Negative return is a fatal error. Zero return found
		 * the offset. Non-zero positive return needs more data.
		 */
		blk_off = 0;
		ret = cb_func(cb_data, response->str, response->len, &blk_off);
		sr_dbg("SCPI get block, text check, ret %d, off %zu.",
			ret, blk_off);
		if (ret < 0) {
			g_mutex_unlock(&scpi->scpi_mutex);
			g_string_free(response, TRUE);
			return ret;
		}
		if (ret > 0)
			continue;
		/* Block boundary detected. Grab text before the block. */
		if (blk_off > response->len)
			blk_off = response->len;
		if (text_response)
			*text_response = g_string_new_len(response->str, blk_off);
		g_string_erase(response, 0, blk_off);
		break;
	}

	/*
	 * Get (the first chunk of) the block part of the response.
	 *
	 * When we get here, there either is no text before the block,
	 * or the text before the block was already taken above. Either
	 * way, remaining code exclusively deals with the data block.
	 * Get enough receive data to determine the block's length.
	 */
	while (response->len < 2) {
		ret = scpi_read_response(scpi, response, timeout);
		sr_dbg("SCPI get block, block start read, ret %d, len %zu.",
			ret, response->len);
		if (ret < 0) {
			g_mutex_unlock(&scpi->scpi_mutex);
			g_string_free(response, TRUE);
			return ret;
		}
		if (ret == 0 && scpi->read_pause_us)
			g_usleep(scpi->read_pause_us);
	}

	/*
	 * Get the data block length, then strip off that length detail
	 * from the input buffer, leaving just the data bytes which were
	 * received so far. Then read more bytes when necessary.
	 *
	 * SCPI protocol data blocks are preceeded with a length spec.
	 * The length spec consists of a '#' marker, one digit which
	 * specifies the character count of the length spec, and the
	 * respective number of characters which specify the data block's
	 * length. Raw data bytes follow, thus one must no longer assume
	 * that the received input stream would be an ASCII string, or
	 * that CR or LF characters had a meaning within that block which
	 * corresponds to message termination. Example:
	 *   #210abcdefghij<LF>
	 *
	 * Zero length is legal but unsupported in this implementation.
	 * It is referred to as "indefinite length block", the number of
	 * bytes is unknown or is not communicated in advance. The end
	 * of the bytes sequence depends on the physical transport's
	 * end-of-message condition. Which is only reliably available
	 * in those transports which communicate message length or end
	 * out of band, separate from the response text or raw bytes.
	 * The use of indefinite length blocks in firmware is considered
	 * rare. Whether or not a trailing LF character must be seen as
	 * part of the raw bytes sequence is yet to get determined. Emit
	 * a warning for users' and developers' awareness when we see
	 * this response format. Examples:
	 *   #0bytes_of_unknown_length_with_uncertain_end_condition
	 *   #10not_supported_either<LF><EOI>
	 *
	 * The SCPI 1999.0 specification (see page 220 and following in
	 * the "HCOPy" description) references IEEE 488.2, especially
	 * section 8.7.9 for DEFINITE LENGTH and section 8.7.10 for
	 * INDEFINITE LENGTH ARBITRARY BLOCK RESPONSE DATA. The latter
	 * with a leading "#0" length and a trailing "NL^END" marker.
	 */
	if (response->str[0] != '#') {
		g_mutex_unlock(&scpi->scpi_mutex);
		g_string_free(response, TRUE);
		return SR_ERR_DATA;
	}
	buf[0] = response->str[1];
	buf[1] = '\0';
	ret = sr_atoul_base(buf, &ul_value, NULL, 10);
	if (ret == SR_OK && !ul_value) {
		sr_err("unsupported INDEFINITE LENGTH ARBITRARY BLOCK RESPONSE");
		ret = SR_ERR_NA;
	}
	if (ret != SR_OK) {
		g_mutex_unlock(&scpi->scpi_mutex);
		g_string_free(response, TRUE);
		return ret;
	}
	digits_count = ul_value;

	timeout = g_get_monotonic_time() + scpi->read_timeout_us;
	while (response->len < 2 + digits_count) {
		ret = scpi_read_response(scpi, response, timeout);
		sr_dbg("SCPI get block, block len read, ret %d, len %zu.",
			ret, response->len);
		if (ret < 0) {
			g_mutex_unlock(&scpi->scpi_mutex);
			g_string_free(response, TRUE);
			return ret;
		}
		if (ret == 0 && scpi->read_pause_us)
			g_usleep(scpi->read_pause_us);
	}
	memcpy(buf, &response->str[2], digits_count);
	buf[digits_count] = '\0';
	ret = sr_atoul_base(buf, &ul_value, NULL, 10);
	if (ret == SR_OK && !ul_value) {
		sr_err("unsupported INDEFINITE LENGTH ARBITRARY BLOCK RESPONSE");
		ret = SR_ERR_NA;
	}
	if (ret != SR_OK) {
		g_mutex_unlock(&scpi->scpi_mutex);
		g_string_free(response, TRUE);
		return ret;
	}
	bytes_count = ul_value;

	sr_spew("SCPI get block, text %*s, digits %zu, number %zu.",
		(int)(2 + digits_count), response->str,
		digits_count, bytes_count);
	sr_dbg("SCPI get block, bytes count %zu.", bytes_count);
	g_string_erase(response, 0, 2 + digits_count);

	/*
	 * Re-allocate the buffer size to the now known bytes count
	 * (and include some more space for response termination).
	 * Keep reading more chunks of response data as necessary.
	 *
	 * Do not stall here for incomplete reads. Truncate the data
	 * and return the partial response upon timeouts (bug 1323).
	 */
	prev_size = response->len;
	g_string_set_size(response, bytes_count + 16);
	g_string_set_size(response, prev_size);
	timeout = g_get_monotonic_time() + scpi->read_timeout_us;
	while (response->len < bytes_count) {
		prev_size = response->len;
		ret = scpi_read_response(scpi, response, timeout);
		sr_dbg("SCPI get block, read block, ret %d, len %zu.",
			ret, response->len);
		if (ret == SR_ERR_TIMEOUT) {
			sr_dbg("SCPI get block, timeout, had %zu, cap to %zu..",
				response->len, prev_size);
			bytes_count = prev_size;
			break;
		}
		if (ret < 0) {
			g_mutex_unlock(&scpi->scpi_mutex);
			g_string_free(response, TRUE);
			return ret;
		}
		if (ret == 0) {
			if (scpi->read_pause_us)
				g_usleep(scpi->read_pause_us);
			continue;
		}
		timeout = g_get_monotonic_time() + scpi->read_timeout_us;
	}

	/*
	 * Depending on underlying physical transports the data block
	 * could be followed by more data which signals end-of-message.
	 * Keep reading until the transport detects the response's
	 * completion. Tell transports that binary mode has ended.
	 */
	trail_count = response->len - bytes_count;
	sr_dbg("SCPI get block, block ends, trail length %zu.", trail_count);
	sr_scpi_block_ends(scpi, &response->str[bytes_count], trail_count);
	timeout = g_get_monotonic_time() + scpi->read_timeout_us;
	while (!sr_scpi_read_complete(scpi)) {
		scpi_make_string_space(response, 4, 16);
		ret = scpi_read_response(scpi, response, timeout);
		sr_dbg("SCPI get block, read end-of-message, ret %d.", ret);
		if (ret < 0) {
			g_mutex_unlock(&scpi->scpi_mutex);
			g_string_free(response, TRUE);
			return ret;
		}
		if (ret == 0 && scpi->read_pause_us)
			g_usleep(scpi->read_pause_us);
	}

	g_mutex_unlock(&scpi->scpi_mutex);

	/* Convert received data to byte array. */
	sr_dbg("SCPI get block, got block, return bytes %zu.", bytes_count);
	if (block_response) {
		blk_bytes = (uint8_t *)g_string_free(response, FALSE);
		*block_response = g_byte_array_new_take(blk_bytes, bytes_count);
	} else {
		g_string_free(response, TRUE);
	}

	return SR_OK;
}

/**
 * Send a SCPI command, read the reply, parse it as binary data with a
 * "definite length block" header, while optional text can precede the
 * binary block.
 *
 * Callers must provide a routine which determines the position in the
 * response message where the block starts. In the absence of a block
 * finding routine the response is assumed to be a block only without
 * leading text. Common SCPI support code will keep reading until the
 * caller's routine either finds the start of the block, or communicates
 * an error condition. Callers must not modify the input text during
 * block search, the routine can get invoked arbitrary number of times.
 *
 * Callers must free the allocated memory (unless it's #NULL) regardless of
 * the routine's exit code. See @ref g_string_free(), @ref g_byte_array_free().
 *
 * @param[in] scpi Previously initialised SCPI device structure.
 * @param[in] command The SCPI command to send to the device (can be NULL).
 * @param[in] cb_func Caller provided routine to find the start of the block.
 * @param[in] cb_data Context for caller provided callback routine.
 * @param[out] text_response Pointer where to store the text result.
 * @param[out] block_response Pointer where to store the bytes result.
 *
 * @return SR_OK upon successfully parsing all values, SR_ERR* upon a parsing
 *         error or upon no response.
 */
SR_PRIV int sr_scpi_get_text_then_block(struct sr_scpi_dev_inst *scpi,
	const char *command,
	sr_scpi_block_find_cb cb_func, void *cb_data,
	GString **text_response, GByteArray **block_response)
{
	return scpi_get_block_int(scpi, command, cb_func, cb_data,
		text_response, block_response);
}

/**
 * Send a SCPI command, read the reply, parse it as binary data with a
 * "definite length block" header and store the result in scpi_response.
 *
 * Callers must free the allocated memory (unless it's NULL) regardless of
 * the routine's return code. See @ref g_byte_array_free().
 *
 * The "indefinite length block" is not supported by this implementation.
 * Because not all supported physical transports signal the end-of-message
 * condition out of band.
 *
 * @param[in] scpi Previously initialised SCPI device structure.
 * @param[in] command The SCPI command to send to the device (can be NULL).
 * @param[out] scpi_response Pointer where to store the parsed result.
 *
 * @return SR_OK upon successfully parsing all values, SR_ERR* upon a parsing
 *         error or upon no response.
 */
SR_PRIV int sr_scpi_get_block(struct sr_scpi_dev_inst *scpi,
	const char *command, GByteArray **scpi_response)
{
	return scpi_get_block_int(scpi, command, NULL, NULL,
		NULL, scpi_response);
}

/**
 * Send the *IDN? SCPI command, receive the reply, parse it and store the
 * reply as a sr_scpi_hw_info structure in the supplied scpi_response pointer.
 *
 * Callers must free the allocated memory regardless of the routine's
 * return code. See @ref sr_scpi_hw_info_free().
 *
 * @param[in] scpi Previously initialised SCPI device structure.
 * @param[out] scpi_response Pointer where to store the hw_info structure.
 *
 * @return SR_OK upon success, SR_ERR* on failure.
 */
SR_PRIV int sr_scpi_get_hw_id(struct sr_scpi_dev_inst *scpi,
	struct sr_scpi_hw_info **scpi_response)
{
	int num_tokens, ret;
	char *response;
	gchar **tokens;
	struct sr_scpi_hw_info *hw_info;
	gchar *idn_substr;

	*scpi_response = NULL;
	response = NULL;
	tokens = NULL;

	ret = sr_scpi_get_string(scpi, SCPI_CMD_IDN, &response);
	if (ret != SR_OK && !response)
		return ret;

	/*
	 * The response to a '*IDN?' is specified by the SCPI spec. It contains
	 * a comma-separated list containing the manufacturer name, instrument
	 * model, serial number of the instrument and the firmware version.
	 *
	 * BEWARE! Although strictly speaking a smaller field count is invalid,
	 * this implementation also accepts IDN responses with one field less,
	 * and assumes that the serial number is missing. Some GWInstek DMMs
	 * were found to do this. Keep warning about this condition, which may
	 * need more consideration later.
	 */
	tokens = g_strsplit(response, ",", 0);
	num_tokens = g_strv_length(tokens);
	if (num_tokens < 3) {
		sr_dbg("IDN response not according to spec: '%s'", response);
		g_strfreev(tokens);
		g_free(response);
		return SR_ERR_DATA;
	}
	if (num_tokens < 4) {
		sr_warn("Short IDN response, assume missing serial number.");
	}
	g_free(response);

	hw_info = g_malloc0(sizeof(*hw_info));

	idn_substr = g_strstr_len(tokens[0], -1, "IDN ");
	if (idn_substr == NULL)
		hw_info->manufacturer = g_strstrip(g_strdup(tokens[0]));
	else
		hw_info->manufacturer = g_strstrip(g_strdup(idn_substr + 4));

	hw_info->model = g_strstrip(g_strdup(tokens[1]));
	if (num_tokens < 4) {
		hw_info->serial_number = g_strdup("Unknown");
		hw_info->firmware_version = g_strstrip(g_strdup(tokens[2]));
	} else {
		hw_info->serial_number = g_strstrip(g_strdup(tokens[2]));
		hw_info->firmware_version = g_strstrip(g_strdup(tokens[3]));
	}

	g_strfreev(tokens);

	*scpi_response = hw_info;

	return SR_OK;
}

/**
 * Free a sr_scpi_hw_info struct.
 *
 * @param hw_info Pointer to the struct to free. If NULL, this
 *                function does nothing.
 */
SR_PRIV void sr_scpi_hw_info_free(struct sr_scpi_hw_info *hw_info)
{
	if (!hw_info)
		return;

	g_free(hw_info->manufacturer);
	g_free(hw_info->model);
	g_free(hw_info->serial_number);
	g_free(hw_info->firmware_version);
	g_free(hw_info);
}

/**
 * Remove potentially enclosing pairs of quotes, un-escape content.
 * This implementation modifies the caller's buffer when quotes are found
 * and doubled quote characters need to get removed from the content.
 *
 * @param[in, out] s	The SCPI string to check and un-quote.
 *
 * @return The start of the un-quoted string.
 */
SR_PRIV const char *sr_scpi_unquote_string(char *s)
{
	size_t s_len;
	char quotes[3];
	char *rdptr;

	/* Immediately bail out on invalid or short input. */
	if (!s || !*s)
		return s;
	s_len = strlen(s);
	if (s_len < 2)
		return s;

	/* Check for matching quote characters front and back. */
	if (s[0] != '\'' && s[0] != '"')
		return s;
	if (s[0] != s[s_len - 1])
		return s;

	/* Need to strip quotes, and un-double quote chars inside. */
	quotes[0] = quotes[1] = *s;
	quotes[2] = '\0';
	s[s_len - 1] = '\0';
	s++;
	rdptr = s;
	while ((rdptr = strstr(rdptr, quotes)) != NULL) {
		memmove(rdptr, rdptr + 1, strlen(rdptr));
		rdptr++;
	}

	return s;
}

SR_PRIV const char *sr_vendor_alias(const char *raw_vendor)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(scpi_vendors); i++) {
		if (!g_ascii_strcasecmp(raw_vendor, scpi_vendors[i][0]))
			return scpi_vendors[i][1];
	}

	return raw_vendor;
}

SR_PRIV const char *sr_scpi_cmd_get(const struct scpi_command *cmdtable,
	int command)
{
	unsigned int i;
	const char *cmd;

	if (!cmdtable)
		return NULL;

	cmd = NULL;
	for (i = 0; cmdtable[i].string; i++) {
		if (cmdtable[i].command == command) {
			cmd = cmdtable[i].string;
			break;
		}
	}

	return cmd;
}

SR_PRIV int sr_scpi_cmd(const struct sr_dev_inst *sdi,
	const struct scpi_command *cmdtable,
	int channel_command, const char *channel_name,
	int command, ...)
{
	struct sr_scpi_dev_inst *scpi;
	va_list args;
	int ret;
	const char *channel_cmd;
	const char *cmd;

	scpi = sdi->conn;

	if (!(cmd = sr_scpi_cmd_get(cmdtable, command))) {
		/* Device does not implement this command, that's OK. */
		return SR_OK;
	}

	g_mutex_lock(&scpi->scpi_mutex);

	/* Select channel. */
	channel_cmd = sr_scpi_cmd_get(cmdtable, channel_command);
	if (channel_cmd && channel_name &&
			g_strcmp0(channel_name, scpi->curr_channel_name)) {
		sr_spew("sr_scpi_cmd(): new channel = %s", channel_name);
		g_free(scpi->curr_channel_name);
		scpi->curr_channel_name = g_strdup(channel_name);
		ret = scpi_send(scpi, channel_cmd, channel_name);
		if (ret != SR_OK)
			return ret;
	}

	va_start(args, command);
	ret = scpi_send_variadic(scpi, cmd, args);
	va_end(args);

	g_mutex_unlock(&scpi->scpi_mutex);

	return ret;
}

SR_PRIV int sr_scpi_cmd_resp(const struct sr_dev_inst *sdi,
	const struct scpi_command *cmdtable,
	int channel_command, const char *channel_name,
	GVariant **gvar, const GVariantType *gvtype, int command, ...)
{
	struct sr_scpi_dev_inst *scpi;
	va_list args;
	const char *channel_cmd;
	const char *cmd;
	GString *response;
	char *s;
	gboolean b;
	double d;
	int ret;

	scpi = sdi->conn;

	if (!(cmd = sr_scpi_cmd_get(cmdtable, command))) {
		/* Device does not implement this command. */
		return SR_ERR_NA;
	}

	g_mutex_lock(&scpi->scpi_mutex);

	/* Select channel. */
	channel_cmd = sr_scpi_cmd_get(cmdtable, channel_command);
	if (channel_cmd && channel_name &&
			g_strcmp0(channel_name, scpi->curr_channel_name)) {
		sr_spew("sr_scpi_cmd_get(): new channel = %s", channel_name);
		g_free(scpi->curr_channel_name);
		scpi->curr_channel_name = g_strdup(channel_name);
		ret = scpi_send(scpi, channel_cmd, channel_name);
		if (ret != SR_OK)
			return ret;
	}

	va_start(args, command);
	ret = scpi_send_variadic(scpi, cmd, args);
	va_end(args);
	if (ret != SR_OK) {
		g_mutex_unlock(&scpi->scpi_mutex);
		return ret;
	}

	response = g_string_sized_new(1024);
	ret = scpi_get_data(scpi, NULL, &response);
	if (ret != SR_OK) {
		g_mutex_unlock(&scpi->scpi_mutex);
		if (response)
			g_string_free(response, TRUE);
		return ret;
	}

	g_mutex_unlock(&scpi->scpi_mutex);

	/* Get rid of trailing linefeed if present */
	if (response->len >= 1 && response->str[response->len - 1] == '\n')
		g_string_truncate(response, response->len - 1);

	/* Get rid of trailing carriage return if present */
	if (response->len >= 1 && response->str[response->len - 1] == '\r')
		g_string_truncate(response, response->len - 1);

	s = g_string_free(response, FALSE);

	ret = SR_OK;
	if (g_variant_type_equal(gvtype, G_VARIANT_TYPE_BOOLEAN)) {
		if ((ret = parse_strict_bool(s, &b)) == SR_OK)
			*gvar = g_variant_new_boolean(b);
	} else if (g_variant_type_equal(gvtype, G_VARIANT_TYPE_DOUBLE)) {
		if ((ret = sr_atod_ascii(s, &d)) == SR_OK)
			*gvar = g_variant_new_double(d);
	} else if (g_variant_type_equal(gvtype, G_VARIANT_TYPE_STRING)) {
		*gvar = g_variant_new_string(s);
	} else {
		sr_err("Unable to convert to desired GVariant type.");
		ret = SR_ERR_NA;
	}

	g_free(s);

	return ret;
}
