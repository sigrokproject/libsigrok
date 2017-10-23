/*
 * This file is part of the libsigrok project.
 *
 * Copyright 2017 Google, Inc
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
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
#include <libusb.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define TWINKIE_VID		0x18d1
#define TWINKIE_PID		0x500a

#define USB_INTERFACE		1
#define USB_CONFIGURATION	1
#define USB_COMMANDS_IFACE	2

#define MAX_RENUM_DELAY_MS	3000
#define NUM_SIMUL_TRANSFERS	32

#define SAMPLE_RATE		SR_KHZ(2400)

static const char vbus_cmd[] = "tw vbus";

/* CC1 & CC2 are always present */
#define LOGIC_CHANNELS_COUNT    2

static const int32_t hwopts[] = {
	SR_CONF_CONN,
	SR_CONF_NUM_ANALOG_CHANNELS,
};

static const int32_t hwcaps[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_CONTINUOUS,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
};

static const struct chan {
	char *name;
	int type;
} chan_defs[] = {
	{"CC1", SR_CHANNEL_LOGIC},
	{"CC2", SR_CHANNEL_LOGIC},
	{"VBUS_V", SR_CHANNEL_ANALOG},
	{"VBUS_A", SR_CHANNEL_ANALOG},

	{NULL, 0}
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct sr_channel *ch;
	struct sr_channel_group *cc_grp, *vbus_grp[VBUS_GRP_COUNT];
	struct sr_config *src;
	GSList *l, *devices, *conn_devices;
	struct libusb_device_descriptor des;
	struct libusb_config_descriptor *cfg;
	libusb_device **devlist;
	int ret, i, j;
	const char *conn;
	char connection_id[64];
	/* By default, disable VBUS analog */
	int vbus_channels = 0;

	drvc = di->context;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_NUM_ANALOG_CHANNELS:
			vbus_channels = MIN(2, g_variant_get_int32(src->data));
			break;
		}
	}
	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	else
		conn_devices = NULL;

	/* Find all Twinkie devices */
	devices = NULL;
	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if (conn) {
			usb = NULL;
			for (l = conn_devices; l; l = l->next) {
				usb = l->data;
				if (usb->bus == libusb_get_bus_number(devlist[i])
				    && usb->address == libusb_get_device_address(devlist[i]))
					break;
			}
			if (!l)
				/* This device matched none of the ones that
				 * matched the conn specification. */
				continue;
		}

		if ((ret = libusb_get_device_descriptor(devlist[i], &des)) != 0) {
			sr_warn("Failed to get device descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		if (des.idVendor != TWINKIE_VID || des.idProduct != TWINKIE_PID)
			continue;

		if ((ret = libusb_get_active_config_descriptor(devlist[i], &cfg)) != 0) {
			sr_warn("Failed to get device configuraton: %s.",
				libusb_error_name(ret));
			continue;
		}

		usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INITIALIZING;
		sdi->vendor = g_strdup("Chromium");
		sdi->model = g_strdup("Twinkie");
		sdi->driver = di;
		sdi->connection_id = g_strdup(connection_id);

		if (vbus_channels && cfg->bNumInterfaces < 3) {
			sr_warn("VBUS channels not available in this firmware.");
			vbus_channels = 0;
		}

		if (!(devc = g_try_malloc0(sizeof(struct dev_context))))
			return NULL;
		sdi->priv = devc;

		cc_grp = g_malloc0(sizeof(struct sr_channel_group));
		cc_grp->name = g_strdup("CCx");
		for (j = 0; j < vbus_channels; j++) {
			vbus_grp[j] = g_malloc0(sizeof(struct sr_channel_group));
			vbus_grp[j]->name = g_strdup(j == VBUS_V ? "VBUS_V"
								 : "VBUS_A");
		}

		for (j = 0; chan_defs[j].name; j++) {
			struct sr_channel_group *grp = cc_grp;
			if (j >= LOGIC_CHANNELS_COUNT) { /* Analog channels */
				if (j - LOGIC_CHANNELS_COUNT >= vbus_channels)
					break;
				grp = vbus_grp[j - LOGIC_CHANNELS_COUNT];
			}
			ch = sr_channel_new(sdi, j, chan_defs[j].type, TRUE,
					    chan_defs[j].name);
			grp->channels = g_slist_append(grp->channels, ch);
		}
		sdi->channel_groups = g_slist_append(NULL, cc_grp);
		for (j = 0; j < vbus_channels; j++) {
			sdi->channel_groups = g_slist_append(sdi->channel_groups,
							     vbus_grp[j]);
			sr_analog_init(&devc->vbus_packet[j],
				       &devc->vbus_encoding,
				       &devc->vbus_meaning[j],
				       &devc->vbus_spec, 3);
			devc->vbus_meaning[j].channels = vbus_grp[j]->channels;
		}
		/* other encoding default settings in sr_analog_init are OK (eg float) */
		devc->vbus_encoding.is_signed = TRUE;
		devc->vbus_meaning[VBUS_V].mq = SR_MQ_VOLTAGE;
		devc->vbus_meaning[VBUS_V].mqflags = SR_MQFLAG_DC;
		devc->vbus_meaning[VBUS_V].unit = SR_UNIT_VOLT;
		devc->vbus_meaning[VBUS_A].mq = SR_MQ_CURRENT;
		devc->vbus_meaning[VBUS_A].unit = SR_UNIT_AMPERE;

		devc->vbus_channels = vbus_channels;
		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);

		sr_dbg("Found a Twinkie dongle.");
		sdi->status = SR_ST_INACTIVE;
		sdi->inst_type = SR_INST_USB;
		sdi->conn = sr_usb_dev_inst_new(
			libusb_get_bus_number(devlist[i]),
			libusb_get_device_address(devlist[i]), NULL);
	}
	libusb_free_device_list(devlist, 1);
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);

	return devices;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	libusb_device **devlist;
	struct sr_usb_dev_inst *usb;
	struct libusb_device_descriptor des;
	struct drv_context *drvc;
	struct dev_context *devc;
	int ret, i, device_count;
	char connection_id[64];

	di = sdi->driver;
	devc = sdi->priv;
	drvc = di->context;
	usb = sdi->conn;

	if (sdi->status == SR_ST_ACTIVE)
		/* Device is already in use. */
		return SR_ERR;

	device_count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (device_count < 0) {
		sr_err("Failed to get device list: %s.",
		       libusb_error_name(device_count));
		return SR_ERR;
	}

	for (i = 0; i < device_count; i++) {
		if ((ret = libusb_get_device_descriptor(devlist[i], &des))) {
			sr_err("Failed to get device descriptor: %s.",
			       libusb_error_name(ret));
			continue;
		}

		if (des.idVendor != TWINKIE_VID || des.idProduct != TWINKIE_PID)
			continue;

		if ((sdi->status == SR_ST_INITIALIZING) ||
				(sdi->status == SR_ST_INACTIVE)) {
			/*
			 * Check device by its physical USB bus/port address.
			 */
			usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));
			if (strcmp(sdi->connection_id, connection_id))
				/* This is not the one. */
				continue;
		}

		if (!(ret = libusb_open(devlist[i], &usb->devhdl))) {
			if (usb->address == 0xff)
				/*
				 * First time we touch this device after FW
				 * upload, so we don't know the address yet.
				 */
				usb->address = libusb_get_device_address(devlist[i]);
		} else {
			sr_err("Failed to open device: %s.",
			       libusb_error_name(ret));
			break;
		}

		ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
		if (ret == LIBUSB_ERROR_BUSY) {
			sr_err("Unable to claim USB interface. Another "
			       "program or driver has already claimed it.");
			break;
		} else if (ret == LIBUSB_ERROR_NO_DEVICE) {
			sr_err("Device has been disconnected.");
			break;
		} else if (ret != 0) {
			sr_err("Unable to claim interface: %s.",
			       libusb_error_name(ret));
			break;
		}
		if (devc->vbus_channels &&
		    (ret = libusb_claim_interface(usb->devhdl, USB_COMMANDS_IFACE))) {
			sr_err("Unable to claim commands interface %d/%s.", ret,
			       libusb_error_name(ret));
			/* Cannot use the analog channels for VBUS. */
			devc->vbus_channels = 0;
		}

		if ((ret = twinkie_init_device(sdi)) != SR_OK) {
			sr_err("Failed to init device.");
			break;
		}

		sdi->status = SR_ST_ACTIVE;
		sr_info("Opened device %d.%d, interface %d.",
			usb->bus, usb->address, USB_INTERFACE);

		break;
	}
	libusb_free_device_list(devlist, 1);

	if (sdi->status != SR_ST_ACTIVE) {
		if (usb->devhdl) {
			libusb_release_interface(usb->devhdl, USB_INTERFACE);
			libusb_close(usb->devhdl);
			usb->devhdl = NULL;
		}
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;

	usb = sdi->conn;
	if (usb->devhdl == NULL)
		return SR_ERR;

	sr_info("Closing device %d.%d.", usb->bus, usb->address);
	devc = sdi->priv;
	if (devc->vbus_channels)
		libusb_release_interface(usb->devhdl, USB_COMMANDS_IFACE);
	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct sr_usb_dev_inst *usb;
	char str[128];
	int ret;

	(void)cg;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_CONN:
		if (!sdi || !sdi->conn)
			return SR_ERR_ARG;
		usb = sdi->conn;
		if (usb->address == 255)
			/* Device still needs to re-enumerate after firmware
			 * upload, so we don't know its (future) address. */
			return SR_ERR;
		snprintf(str, 128, "%d.%d", usb->bus, usb->address);
		*data = g_variant_new_string(str);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(SAMPLE_RATE);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int ret;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwopts, ARRAY_SIZE(hwopts), sizeof(int32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static void abort_acquisition(struct dev_context *devc)
{
	int i;

	devc->sent_samples = -1;

	for (i = devc->num_transfers - 1; i >= 0; i--) {
		if (devc->transfers[i])
			libusb_cancel_transfer(devc->transfers[i]);
	}
}

static int receive_data(int fd, int revents, void *cb_data)
{
	struct timeval tv;
	struct dev_context *devc;
	struct drv_context *drvc;
	const struct sr_dev_inst *sdi;
	struct sr_dev_driver *di;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	di = sdi->driver;
	drvc = di->context;
	devc = sdi->priv;

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	if (devc->sent_samples == -2) {
		abort_acquisition(devc);
	}

	return TRUE;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *transfer;
	unsigned int i, timeout, num_transfers, cc_transfers;
	int ret;
	unsigned char *buf;
	size_t size, convsize;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	drvc = di->context;
	devc = sdi->priv;
	usb = sdi->conn;

	devc->sent_samples = 0;
	/* reset per-CC context */
	memset(devc->cc, 0, sizeof(devc->cc));

	timeout = 1000;
	cc_transfers = num_transfers = 10;
	size = 10*1024;
	convsize = size * 8 * 256 /* largest size : only rollbacks/no edges */;
	devc->submitted_transfers = 0;
	if (devc->vbus_channels)
		num_transfers += 2;

	devc->convbuffer_size = convsize;
	if (!(devc->convbuffer = g_try_malloc(convsize))) {
		sr_err("Conversion buffer malloc failed.");
		return SR_ERR_MALLOC;
	}
	memset(devc->convbuffer, 0, devc->convbuffer_size);

	devc->transfers = g_try_malloc0(sizeof(*devc->transfers) * num_transfers);
	if (!devc->transfers) {
		sr_err("USB transfers malloc failed.");
		g_free(devc->convbuffer);
		return SR_ERR_MALLOC;
	}

	devc->num_transfers = num_transfers;
	for (i = 0; i < cc_transfers; i++) {
		if (!(buf = g_try_malloc(size))) {
			sr_err("USB transfer buffer malloc failed.");
			if (devc->submitted_transfers)
				abort_acquisition(devc);
			else {
				g_free(devc->transfers);
				g_free(devc->convbuffer);
			}
			return SR_ERR_MALLOC;
		}
		transfer = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(transfer, usb->devhdl,
				3 | LIBUSB_ENDPOINT_IN, buf, size,
				twinkie_receive_transfer, (void *)sdi, timeout);
		if ((ret = libusb_submit_transfer(transfer)) != 0) {
			sr_err("Failed to submit transfer: %s.",
			       libusb_error_name(ret));
			libusb_free_transfer(transfer);
			g_free(buf);
			abort_acquisition(devc);
			return SR_ERR;
		}
		devc->transfers[i] = transfer;
		devc->submitted_transfers++;
	}
	if (devc->vbus_channels) {
		struct libusb_transfer *out_xfer = libusb_alloc_transfer(0);
		struct libusb_transfer *in_xfer = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(out_xfer, usb->devhdl,
					  2 | LIBUSB_ENDPOINT_OUT, (uint8_t *)vbus_cmd,
					  sizeof(vbus_cmd) - 1, twinkie_vbus_sent,
					  (void *)sdi, timeout);
		libusb_fill_bulk_transfer(in_xfer, usb->devhdl,
					  2 | LIBUSB_ENDPOINT_IN, (uint8_t *)devc->vbus_data,
					  sizeof(devc->vbus_data),
					  twinkie_vbus_recv, (void *)sdi, timeout);
		if ((ret = libusb_submit_transfer(out_xfer)) != 0) {
			sr_err("Failed to submit VBUS transfer: %s.",
			       libusb_error_name(ret));
			libusb_free_transfer(out_xfer);
			abort_acquisition(devc);
			return SR_ERR;
		}
		devc->transfers[cc_transfers + 0] = out_xfer;
		devc->transfers[cc_transfers + 1] = in_xfer;
		devc->submitted_transfers++;
	}

	devc->ctx = drvc->sr_ctx;

	usb_source_add(sdi->session, devc->ctx, timeout, receive_data,
		      (void *)sdi);

	/* Send header packet to the session bus. */
	std_session_send_df_header(sdi);

	if ((ret = twinkie_start_acquisition(sdi)) != SR_OK) {
		abort_acquisition(devc);
		return ret;
	}

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	abort_acquisition(sdi->priv);

	return SR_OK;
}

static struct sr_dev_driver chromium_twinkie_driver_info = {
	.name = "chromium-twinkie",
	.longname = "Chromium Twinkie",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = NULL,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
};
SR_REGISTER_DEV_DRIVER(chromium_twinkie_driver_info);
