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

#include <glib.h>
#include <stdlib.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "scpi_serial"

#define BUFFER_SIZE 1024

struct scpi_serial {
	struct sr_serial_dev_inst *serial;
	char buffer[BUFFER_SIZE];
	size_t count;
	size_t read;
};

static const struct {
	uint16_t vendor_id;
	uint16_t product_id;
	const char *serialcomm;
} scpi_serial_usb_ids[] = {
	{ 0x0403, 0xed72, "115200/8n1/flow=1" }, /* Hameg HO720 */
	{ 0x0403, 0xed73, "115200/8n1/flow=1" }, /* Hameg HO730 */
};

static GSList *scpi_serial_scan(struct drv_context *drvc)
{
	GSList *l, *r, *resources = NULL;
	gchar *res;
	unsigned i;

	(void)drvc;

	for (i = 0; i < ARRAY_SIZE(scpi_serial_usb_ids); i++) {
		if (!(l = sr_serial_find_usb(scpi_serial_usb_ids[i].vendor_id,
					scpi_serial_usb_ids[i].product_id)))
			continue;
		for (r = l; r; r = r->next) {
			if (scpi_serial_usb_ids[i].serialcomm)
				res = g_strdup_printf("%s:%s", (char *) r->data,
				                      scpi_serial_usb_ids[i].serialcomm);
			else
				res = g_strdup(r->data);
			resources = g_slist_append(resources, res);
		}
		g_slist_free_full(l, g_free);
	}

	return resources;
}

static int scpi_serial_dev_inst_new(void *priv, struct drv_context *drvc,
		const char *resource, char **params, const char *serialcomm)
{
	struct scpi_serial *sscpi = priv;

	(void)drvc;
	(void)params;

	sscpi->serial = sr_serial_dev_inst_new(resource, serialcomm);

	return SR_OK;
}

static int scpi_serial_open(void *priv)
{
	struct scpi_serial *sscpi = priv;
	struct sr_serial_dev_inst *serial = sscpi->serial;

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return SR_ERR;

	if (serial_flush(serial) != SR_OK)
		return SR_ERR;

	sscpi->count = 0;
	sscpi->read = 0;

	return SR_OK;
}

static int scpi_serial_source_add(struct sr_session *session, void *priv,
		int events, int timeout, sr_receive_data_callback cb, void *cb_data)
{
	struct scpi_serial *sscpi = priv;
	struct sr_serial_dev_inst *serial = sscpi->serial;

	return serial_source_add(session, serial, events, timeout, cb, cb_data);
}

static int scpi_serial_source_remove(struct sr_session *session, void *priv)
{
	struct scpi_serial *sscpi = priv;
	struct sr_serial_dev_inst *serial = sscpi->serial;

	return serial_source_remove(session, serial);
}

static int scpi_serial_send(void *priv, const char *command)
{
	int len, result, written;
	gchar *terminated_command;
	struct scpi_serial *sscpi = priv;
	struct sr_serial_dev_inst *serial = sscpi->serial;

	terminated_command = g_strconcat(command, "\n", NULL);
	len = strlen(terminated_command);
	written = 0;
	while (written < len) {
		result = serial_write_nonblocking(serial,
				terminated_command + written, len - written);
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

static int scpi_serial_read_begin(void *priv)
{
	(void) priv;

	return SR_OK;
}

static int scpi_serial_read_data(void *priv, char *buf, int maxlen)
{
	struct scpi_serial *sscpi = priv;
	int len, ret;

	len = BUFFER_SIZE - sscpi->count;

	/* Try to read new data into the buffer if there is space. */
	if (len > 0) {
		ret = serial_read_nonblocking(sscpi->serial, sscpi->buffer + sscpi->count,
				BUFFER_SIZE - sscpi->count);

		if (ret < 0)
			return ret;

		sscpi->count += ret;

		if (ret > 0)
			sr_spew("Read %d bytes into buffer.", ret);
	}

	/* Return as many bytes as possible from buffer, excluding any trailing newline. */
	if (sscpi->read < sscpi->count) {
		len = sscpi->count - sscpi->read;
		if (len > maxlen)
			len = maxlen;
		if (sscpi->buffer[sscpi->read + len - 1] == '\n')
			len--;
		sr_spew("Returning %d bytes from buffer.", len);
		memcpy(buf, sscpi->buffer + sscpi->read, len);
		sscpi->read += len;
		if (sscpi->read == BUFFER_SIZE) {
			sr_spew("Resetting buffer.");
			sscpi->count = 0;
			sscpi->read = 0;
		}
		return len;
	}

	return 0;
}

static int scpi_serial_read_complete(void *priv)
{
	struct scpi_serial *sscpi = priv;

	/* If the next character is a newline, discard it and report complete. */
	if (sscpi->read < sscpi->count && sscpi->buffer[sscpi->read] == '\n') {
		sscpi->read++;
		return 1;
	} else {
		return 0;
	}
}

static int scpi_serial_close(void *priv)
{
	struct scpi_serial *sscpi = priv;

	return serial_close(sscpi->serial);
}

static void scpi_serial_free(void *priv)
{
	struct scpi_serial *sscpi = priv;

	sr_serial_dev_inst_free(sscpi->serial);
}

SR_PRIV const struct sr_scpi_dev_inst scpi_serial_dev = {
	.name          = "serial",
	.prefix        = "",
	.priv_size     = sizeof(struct scpi_serial),
	.scan          = scpi_serial_scan,
	.dev_inst_new  = scpi_serial_dev_inst_new,
	.open          = scpi_serial_open,
	.source_add    = scpi_serial_source_add,
	.source_remove = scpi_serial_source_remove,
	.send          = scpi_serial_send,
	.read_begin    = scpi_serial_read_begin,
	.read_data     = scpi_serial_read_data,
	.read_complete = scpi_serial_read_complete,
	.close         = scpi_serial_close,
	.free          = scpi_serial_free,
};
