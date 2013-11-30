/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 poljar (Damir Jelić) <poljarinho@gmail.com>
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

#include "libsigrok.h"
#include "libsigrok-internal.h"

#include <glib.h>
#include <string.h>

/* Message logging helpers with subsystem-specific prefix string. */
#define LOG_PREFIX "scpi_serial: "
#define sr_log(l, s, args...) sr_log(l, LOG_PREFIX s, ## args)
#define sr_spew(s, args...) sr_spew(LOG_PREFIX s, ## args)
#define sr_dbg(s, args...) sr_dbg(LOG_PREFIX s, ## args)
#define sr_info(s, args...) sr_info(LOG_PREFIX s, ## args)
#define sr_warn(s, args...) sr_warn(LOG_PREFIX s, ## args)

#define SCPI_READ_RETRIES 100
#define SCPI_READ_RETRY_TIMEOUT 10000

SR_PRIV int scpi_serial_open(void *priv)
{
	struct sr_serial_dev_inst *serial = priv;

	if (serial_open(serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		return SR_ERR;

	if (serial_flush(serial) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int scpi_serial_source_add(void *priv, int events, int timeout,
			sr_receive_data_callback_t cb, void *cb_data)
{
	struct sr_serial_dev_inst *serial = priv;

	return serial_source_add(serial, events, timeout, cb, cb_data);
}

SR_PRIV int scpi_serial_source_remove(void *priv)
{
	struct sr_serial_dev_inst *serial = priv;

	return sr_source_remove(serial->fd);
}

SR_PRIV int scpi_serial_send(void *priv, const char *command)
{
	int len, result, written;
	gchar *terminated_command;
	struct sr_serial_dev_inst *serial = priv;

	terminated_command = g_strconcat(command, "\n", NULL);
	len = strlen(terminated_command);
	written = 0;
	while (written < len) {
		result = serial_write(serial, terminated_command + written, len - written);
		if (result < 0) {
			sr_err("Error while sending SCPI command: '%s'.", command);
			g_free(terminated_command);
			return SR_ERR;
		}
		written += result;
	}

	g_free(terminated_command);

	sr_spew("Successfully sent SCPI command: '%s'.", command);

	return SR_OK;
}

SR_PRIV int scpi_serial_receive(void *priv, char **scpi_response)
{
	int len, ret;
	char buf[256];
	unsigned int i;
	GString *response;
	struct sr_serial_dev_inst *serial = priv;

	response = g_string_sized_new(1024);

	for (i = 0; i <= SCPI_READ_RETRIES; i++) {
		while ((len = serial_read(serial, buf, sizeof(buf))) > 0)
			response = g_string_append_len(response, buf, len);

		if (response->len > 0 &&
		    response->str[response->len-1] == '\n') {
			sr_spew("Fetched full SCPI response.");
			break;
		}

		g_usleep(SCPI_READ_RETRY_TIMEOUT);
	}

	if (response->len == 0) {
		sr_dbg("No SCPI response received.");
		g_string_free(response, TRUE);
		*scpi_response = NULL;
		return SR_ERR;
	} else if (response->str[response->len - 1] == '\n') {
		/*
		 * The SCPI response contains a LF ('\n') at the end and we
		 * don't need this so replace it with a '\0' and decrement
		 * the length.
		 */
		response->str[--response->len] = '\0';
		ret = SR_OK;
	} else {
		sr_warn("Incomplete SCPI response received!");
		ret = SR_ERR;
	}

	/* Minor optimization: steal the string instead of copying. */
	*scpi_response = response->str;

	/* A SCPI response can be quite large, print at most 50 characters. */
	sr_dbg("SCPI response received (length %d): '%.50s'",
	       response->len, response->str);

	g_string_free(response, FALSE);

	return ret;
}

/* Some stubs to keep the compiler from whining. */
static int scpi_serial_read(void *priv, char *buf, int maxlen)
{
	return serial_read(priv, buf, maxlen);
}
static int scpi_serial_close(void *priv)
{
	return serial_close(priv);
}
static void scpi_serial_free(void *priv)
{
	return sr_serial_dev_inst_free(priv);
}

SR_PRIV struct sr_scpi_dev_inst *scpi_serial_dev_inst_new(const char *port,
		const char *serialcomm)
{
	struct sr_scpi_dev_inst *scpi;
	struct sr_serial_dev_inst *serial;

	scpi = g_try_malloc(sizeof(struct sr_scpi_dev_inst));

	if (!(serial = sr_serial_dev_inst_new(port, serialcomm)))
	{
		g_free(scpi);
		return NULL;
	}

	scpi->open = scpi_serial_open;
	scpi->source_add = scpi_serial_source_add;
	scpi->source_remove = scpi_serial_source_remove;
	scpi->send = scpi_serial_send;
	scpi->receive = scpi_serial_receive;
	scpi->read = scpi_serial_read;
	scpi->close = scpi_serial_close;
	scpi->free = scpi_serial_free;
	scpi->priv = serial;

	return scpi;
}
