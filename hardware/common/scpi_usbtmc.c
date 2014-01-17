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

#include "libsigrok.h"
#include "libsigrok-internal.h"

#include <glib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define LOG_PREFIX "scpi_usbtmc"

#define MAX_READ_LENGTH 2048

struct usbtmc_scpi {
	struct sr_usbtmc_dev_inst *usbtmc;
	char response_buffer[MAX_READ_LENGTH];
	int response_length;
	int response_bytes_read;
};

static int scpi_usbtmc_dev_inst_new(void *priv, struct drv_context *drvc,
		const char *resource, char **params, const char *serialcomm)
{
	struct usbtmc_scpi *uscpi = priv;

	(void)drvc;
	(void)params;
	(void)serialcomm;

	if (!(uscpi->usbtmc = sr_usbtmc_dev_inst_new(resource)))
		return SR_ERR;

	return SR_OK;
}

static int scpi_usbtmc_open(void *priv)
{
	struct usbtmc_scpi *uscpi = priv;
	struct sr_usbtmc_dev_inst *usbtmc = uscpi->usbtmc;

	if ((usbtmc->fd = open(usbtmc->device, O_RDWR)) < 0)
		return SR_ERR;

	return SR_OK;
}

static int scpi_usbtmc_source_add(void *priv, int events, int timeout,
			sr_receive_data_callback_t cb, void *cb_data)
{
	struct usbtmc_scpi *uscpi = priv;
	struct sr_usbtmc_dev_inst *usbtmc = uscpi->usbtmc;

	return sr_source_add(usbtmc->fd, events, timeout, cb, cb_data);
}

static int scpi_usbtmc_source_remove(void *priv)
{
	struct usbtmc_scpi *uscpi = priv;
	struct sr_usbtmc_dev_inst *usbtmc = uscpi->usbtmc;

	return sr_source_remove(usbtmc->fd);
}

static int scpi_usbtmc_send(void *priv, const char *command)
{
	struct usbtmc_scpi *uscpi = priv;
	struct sr_usbtmc_dev_inst *usbtmc = uscpi->usbtmc;
	int len, out;

	len = strlen(command);
	out = write(usbtmc->fd, command, len);

	if (out < 0) {
		sr_err("Write error: %s", strerror(errno));
		return SR_ERR;
	}

	if (out < len) {
		sr_dbg("Only sent %d/%d bytes of SCPI command: '%s'.", out,
		       len, command);
	}

	sr_spew("Successfully sent SCPI command: '%s'.", command);

	return SR_OK;
}

static int scpi_usbtmc_read_begin(void *priv)
{
	struct usbtmc_scpi *uscpi = priv;
	struct sr_usbtmc_dev_inst *usbtmc = uscpi->usbtmc;
	int len;

	len = read(usbtmc->fd, uscpi->response_buffer, MAX_READ_LENGTH);

	if (len < 0) {
		sr_err("Read error: %s", strerror(errno));
		return SR_ERR;
	}

	uscpi->response_length = len;
	uscpi->response_bytes_read = 0;

	sr_spew("Read %d bytes from device into buffer", len);

	return SR_OK;
}

static int scpi_usbtmc_read_data(void *priv, char *buf, int maxlen)
{
	struct usbtmc_scpi *uscpi = priv;
	int read_length;

	sr_spew("%d bytes requested", maxlen);

	if (uscpi->response_bytes_read == uscpi->response_length) {
		sr_spew("Buffer is empty.");
		if (uscpi->response_length == MAX_READ_LENGTH) {
			sr_spew("Previous read was of maximum length, reading again.");
			if (scpi_usbtmc_read_begin(uscpi) != SR_OK)
				return SR_ERR;
		} else {
			return SR_ERR;
		}
	}

	read_length = uscpi->response_length - uscpi->response_bytes_read;

	if (read_length > maxlen)
		read_length = maxlen;

	memcpy(buf, uscpi->response_buffer + uscpi->response_bytes_read, read_length);

	uscpi->response_bytes_read += read_length;

	sr_spew("Returned %d bytes from buffer, %d/%d bytes of buffer now read",
			read_length, uscpi->response_bytes_read, uscpi->response_length);

	return read_length;
}

static int scpi_usbtmc_read_complete(void *priv)
{
	struct usbtmc_scpi *uscpi = priv;

	if (uscpi->response_length == MAX_READ_LENGTH
	    && uscpi->response_bytes_read == uscpi->response_length)
		scpi_usbtmc_read_begin(uscpi);

	return (uscpi->response_bytes_read >= uscpi->response_length);
}

static int scpi_usbtmc_close(void *priv)
{
	struct usbtmc_scpi *uscpi = priv;

	if (close(uscpi->usbtmc->fd) < 0)
		return SR_ERR;

	return SR_OK;
}

static void scpi_usbtmc_free(void *priv)
{
	struct usbtmc_scpi *uscpi = priv;

	sr_usbtmc_dev_inst_free(uscpi->usbtmc);
}

SR_PRIV const struct sr_scpi_dev_inst scpi_usbtmc_dev = {
	.name          = "USBTMC",
	.prefix        = "/dev/usbtmc",
	.priv_size     = sizeof(struct usbtmc_scpi),
	.dev_inst_new  = scpi_usbtmc_dev_inst_new,
	.open          = scpi_usbtmc_open,
	.source_add    = scpi_usbtmc_source_add,
	.source_remove = scpi_usbtmc_source_remove,
	.send          = scpi_usbtmc_send,
	.read_begin    = scpi_usbtmc_read_begin,
	.read_data     = scpi_usbtmc_read_data,
	.read_complete = scpi_usbtmc_read_complete,
	.close         = scpi_usbtmc_close,
	.free          = scpi_usbtmc_free,
};
