/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Aurelien Jacobs <aurel@gnuage.org>
 *
 * Inspired by the VXI11 Ethernet Protocol for Linux:
 * http://optics.eee.nottingham.ac.uk/vxi11/
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

#include <rpc/rpc.h>
#include "vxi.h"
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "scpi_vxi"
#define VXI_DEFAULT_TIMEOUT  2000  /* in ms */

struct scpi_vxi {
	char *address;
	char *instrument;
	CLIENT *client;
	Device_Link link;
	unsigned int max_send_size;
	unsigned int read_complete;
};

static int scpi_vxi_open(void *priv)
{
	struct scpi_vxi *vxi = priv;
	Create_LinkParms link_parms;
	Create_LinkResp *link_resp;

	vxi->client = clnt_create(vxi->address, DEVICE_CORE, DEVICE_CORE_VERSION, "tcp");
	if (vxi->client == NULL) {
		sr_err("Client creation failed for %s", vxi->address);
		return SR_ERR;
	}

	/* Set link parameters */
	link_parms.clientId = (long) vxi->client;
	link_parms.lockDevice = 0;
	link_parms.lock_timeout = VXI_DEFAULT_TIMEOUT;
	link_parms.device = "inst0";

	if (!(link_resp = create_link_1(&link_parms, vxi->client))) {
		sr_err("Link creation failed for %s", vxi->address);
		return SR_ERR;
	}
	vxi->link = link_resp->lid;
	vxi->max_send_size = link_resp->maxRecvSize;

	/* Set a default maxRecvSize for devices which do not specify it */
	if (vxi->max_send_size <= 0)
		vxi->max_send_size = 4096;

	return SR_OK;
}

static int scpi_vxi_source_add(void *priv, int events, int timeout,
			sr_receive_data_callback_t cb, void *cb_data)
{
	(void)priv;

	/* Hook up a dummy handler to receive data from the device. */
	return sr_source_add(-1, events, timeout, cb, cb_data);
}

static int scpi_vxi_source_remove(void *priv)
{
	(void)priv;

	return sr_source_remove(-1);
}

/* Operation Flags */
#define DF_WAITLOCK  0x01  /* wait if the operation is locked by another link */
#define DF_END       0x08  /* an END indicator is sent with last byte of buffer */
#define DF_TERM      0x80  /* a termination char is set during a read */

static int scpi_vxi_send(void *priv, const char *command)
{
	struct scpi_vxi *vxi = priv;
	Device_WriteResp *write_resp;
	Device_WriteParms write_parms;
	char *terminated_command;
	unsigned int len;

	terminated_command = g_strdup_printf("%s\r\n", command);
	len = strlen(terminated_command);

	write_parms.lid           = vxi->link;
	write_parms.io_timeout    = VXI_DEFAULT_TIMEOUT;
	write_parms.lock_timeout  = VXI_DEFAULT_TIMEOUT;
	write_parms.flags         = DF_END;
	write_parms.data.data_len = MIN(len, vxi->max_send_size);
	write_parms.data.data_val = terminated_command;

	if (!(write_resp = device_write_1(&write_parms, vxi->client))
	    || write_resp->error) {
		sr_err("Device write failed for %s with error %d",
		       vxi->address, write_resp->error);
		return SR_ERR;
	}

	g_free(terminated_command);

	if (write_resp->size < len)
		sr_dbg("Only sent %d/%d bytes of SCPI command: '%s'.",
		       write_resp->size, len, command);
	else
		sr_spew("Successfully sent SCPI command: '%s'.", command);

	return SR_OK;
}

static int scpi_vxi_read_begin(void *priv)
{
	struct scpi_vxi *vxi = priv;

	vxi->read_complete = 0;

	return SR_OK;
}

/* Read Response Reason Flags */
#define RRR_SIZE  0x01  /* requestSize bytes have been transferred */
#define RRR_TERM  0x02  /* a termination char has been read */
#define RRR_END   0x04  /* an END indicator has been read */

static int scpi_vxi_read_data(void *priv, char *buf, int maxlen)
{
	struct scpi_vxi *vxi = priv;
	Device_ReadParms read_parms;
	Device_ReadResp *read_resp;

	read_parms.lid          = vxi->link;
	read_parms.io_timeout   = VXI_DEFAULT_TIMEOUT;
	read_parms.lock_timeout = VXI_DEFAULT_TIMEOUT;
	read_parms.flags        = 0;
	read_parms.termChar     = 0;
	read_parms.requestSize  = maxlen;

	if (!(read_resp = device_read_1(&read_parms, vxi->client))
	    || read_resp->error) {
		sr_err("Device read failed for %s with error %d",
		       vxi->address, read_resp->error);
		return SR_ERR;
	}

	memcpy(buf, read_resp->data.data_val, read_resp->data.data_len);
	vxi->read_complete = read_resp->reason & (RRR_SIZE | RRR_TERM | RRR_END);
	return read_resp->data.data_len;  /* actual number of bytes received */
}

static int scpi_vxi_read_complete(void *priv)
{
	struct scpi_vxi *vxi = priv;

	return vxi->read_complete;
}

static int scpi_vxi_close(void *priv)
{
	struct scpi_vxi *vxi = priv;
	Device_Error *dev_error;

	if (!(dev_error = destroy_link_1(&vxi->link, vxi->client))) {
		sr_err("Link destruction failed for %s", vxi->address);
		return SR_ERR;
	}

	clnt_destroy(vxi->client);

	return SR_OK;
}

static void scpi_vxi_free(void *priv)
{
	struct scpi_vxi *vxi = priv;

	g_free(vxi->address);
	g_free(vxi->instrument);
	g_free(vxi);
}

SR_PRIV struct sr_scpi_dev_inst *scpi_vxi_dev_inst_new(const char *address,
			const char *instrument)
{
	struct sr_scpi_dev_inst *scpi;
	struct scpi_vxi *vxi;

	scpi = g_malloc(sizeof(struct sr_scpi_dev_inst));
	vxi = g_malloc0(sizeof(struct scpi_vxi));

	vxi->address = g_strdup(address);
	vxi->instrument = g_strdup(instrument ? instrument : "inst0");

	scpi->open = scpi_vxi_open;
	scpi->source_add = scpi_vxi_source_add;
	scpi->source_remove = scpi_vxi_source_remove;
	scpi->send = scpi_vxi_send;
	scpi->read_begin = scpi_vxi_read_begin;
	scpi->read_data = scpi_vxi_read_data;
	scpi->read_complete = scpi_vxi_read_complete;
	scpi->close = scpi_vxi_close;
	scpi->free = scpi_vxi_free;
	scpi->priv = vxi;

	return scpi;
}
