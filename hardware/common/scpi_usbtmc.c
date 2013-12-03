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

/* Message logging helpers with subsystem-specific prefix string. */
#define LOG_PREFIX "scpi_usbtmc: "
#define sr_log(l, s, args...) sr_log(l, LOG_PREFIX s, ## args)
#define sr_spew(s, args...) sr_spew(LOG_PREFIX s, ## args)
#define sr_dbg(s, args...) sr_dbg(LOG_PREFIX s, ## args)
#define sr_info(s, args...) sr_info(LOG_PREFIX s, ## args)
#define sr_warn(s, args...) sr_warn(LOG_PREFIX s, ## args)

SR_PRIV int scpi_usbtmc_open(void *priv)
{
	struct sr_usbtmc_dev_inst *usbtmc = priv;

	if ((usbtmc->fd = open(usbtmc->device, O_RDWR)) < 0)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int scpi_usbtmc_source_add(void *priv, int events, int timeout,
			sr_receive_data_callback_t cb, void *cb_data)
{
	struct sr_usbtmc_dev_inst *usbtmc = priv;

	return sr_source_add(usbtmc->fd, events, timeout, cb, cb_data);
}

SR_PRIV int scpi_usbtmc_source_remove(void *priv)
{
	struct sr_usbtmc_dev_inst *usbtmc = priv;

	return sr_source_remove(usbtmc->fd);
}

SR_PRIV int scpi_usbtmc_send(void *priv, const char *command)
{
	struct sr_usbtmc_dev_inst *usbtmc = priv;
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

SR_PRIV int scpi_usbtmc_receive(void *priv, char **scpi_response)
{
	struct sr_usbtmc_dev_inst *usbtmc = priv;
	GString *response;
	char buf[256];
	int len;

	response = g_string_sized_new(1024);

	len = read(usbtmc->fd, buf, sizeof(buf));

	if (len < 0) {
		sr_err("Read error: %s", strerror(errno));
		g_string_free(response, TRUE);
		return SR_ERR;
	}

	response = g_string_append_len(response, buf, len);

	*scpi_response = response->str;

	sr_dbg("SCPI response received (length %d): '%.50s'",
	       response->len, response->str);

	g_string_free(response, FALSE);

	return SR_OK;
}

SR_PRIV int scpi_usbtmc_close(void *priv)
{
	struct sr_usbtmc_dev_inst *usbtmc = priv;

	if (close(usbtmc->fd) < 0)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV struct sr_scpi_dev_inst *scpi_usbtmc_dev_inst_new(const char *device)
{
	struct sr_scpi_dev_inst *scpi;
	struct sr_usbtmc_dev_inst *usbtmc;

	scpi = g_try_malloc(sizeof(struct sr_scpi_dev_inst));

	if (!(usbtmc = sr_usbtmc_dev_inst_new(device)))
	{
		g_free(scpi);
		return NULL;
	}

	scpi->open = scpi_usbtmc_open;
	scpi->source_add = scpi_usbtmc_source_add;
	scpi->source_remove = scpi_usbtmc_source_remove;
	scpi->send = scpi_usbtmc_send;
	scpi->receive = scpi_usbtmc_receive;
	scpi->close = scpi_usbtmc_close;
	scpi->free = sr_usbtmc_dev_inst_free;
	scpi->priv = usbtmc;

	return scpi;
}
