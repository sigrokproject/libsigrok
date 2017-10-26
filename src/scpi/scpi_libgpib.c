/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Martin Ling <martin-sigrok@earth.li>
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
#include <gpib/ib.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "scpi_gpib"

struct scpi_gpib {
	char *name;
	int descriptor;
	int read_started;
};

static int scpi_gpib_dev_inst_new(void *priv, struct drv_context *drvc,
		const char *resource, char **params, const char *serialcomm)
{
	struct scpi_gpib *gscpi = priv;

	(void)drvc;
	(void)resource;
	(void)serialcomm;

	if (!params || !params[1])
			return SR_ERR;

	gscpi->name = g_strdup(params[1]);

	return SR_OK;
}

static int scpi_gpib_open(struct sr_scpi_dev_inst *scpi)
{
	struct scpi_gpib *gscpi = scpi->priv;

	if ((gscpi->descriptor = ibfind(gscpi->name)) < 0)
		return SR_ERR;

	return SR_OK;
}

static int scpi_gpib_source_add(struct sr_session *session, void *priv,
		int events, int timeout, sr_receive_data_callback cb, void *cb_data)
{
	(void) priv;

	/* Hook up a dummy handler to receive data from the device. */
	return sr_session_source_add(session, -1, events, timeout, cb, cb_data);
}

static int scpi_gpib_source_remove(struct sr_session *session, void *priv)
{
	(void) priv;

	return sr_session_source_remove(session, -1);
}

static int scpi_gpib_send(void *priv, const char *command)
{
	struct scpi_gpib *gscpi = priv;
	int len = strlen(command);

	ibwrt(gscpi->descriptor, command, len);

	if (ibsta & ERR)
	{
		sr_err("Error while sending SCPI command: '%s': iberr = %s.",
			command, gpib_error_string(iberr));
		return SR_ERR;
	}

	if (ibcnt < len)
	{
		sr_err("Failed to send all of SCPI command: '%s': "
				"len = %d, ibcnt = %d.", command, len, ibcnt);
		return SR_ERR;
	}

	sr_spew("Successfully sent SCPI command: '%s'.", command);

	return SR_OK;
}

static int scpi_gpib_read_begin(void *priv)
{
	struct scpi_gpib *gscpi = priv;

	gscpi->read_started = 0;

	return SR_OK;
}

static int scpi_gpib_read_data(void *priv, char *buf, int maxlen)
{
	struct scpi_gpib *gscpi = priv;

	ibrd(gscpi->descriptor, buf, maxlen);

	if (ibsta & ERR)
	{
		sr_err("Error while reading SCPI response: "
			"iberr = %s, ibsta = %d.",
			gpib_error_string(iberr), ibsta);
		return SR_ERR;
	}

	gscpi->read_started = 1;

	return ibcnt;
}

static int scpi_gpib_read_complete(void *priv)
{
	struct scpi_gpib *gscpi = priv;

	return gscpi->read_started && (ibsta & END);
}

static int scpi_gpib_close(struct sr_scpi_dev_inst *scpi)
{
	struct scpi_gpib *gscpi = scpi->priv;

	/* Put device in back local mode to prevent lock-out of front panel. */
	ibloc(gscpi->descriptor);
	/* Now it's safe to close the handle. */
	ibonl(gscpi->descriptor, 0);

	return SR_OK;
}

static void scpi_gpib_free(void *priv)
{
	struct scpi_gpib *gscpi = priv;

	g_free(gscpi->name);
}

SR_PRIV const struct sr_scpi_dev_inst scpi_libgpib_dev = {
	.name = "GPIB",
	.prefix = "libgpib",
	.priv_size = sizeof(struct scpi_gpib),
	.dev_inst_new = scpi_gpib_dev_inst_new,
	.open = scpi_gpib_open,
	.source_add = scpi_gpib_source_add,
	.source_remove = scpi_gpib_source_remove,
	.send = scpi_gpib_send,
	.read_begin = scpi_gpib_read_begin,
	.read_data = scpi_gpib_read_data,
	.read_complete = scpi_gpib_read_complete,
	.close = scpi_gpib_close,
	.free = scpi_gpib_free,
};
