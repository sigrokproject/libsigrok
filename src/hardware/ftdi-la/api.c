/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Sergey Alirzaev <zl29ah@gmail.com>
 * Copyright (C) 2021 Thomas Hebb <tommyhebb@gmail.com>
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
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CONN | SR_CONF_GET,
};

static const uint64_t samplerates[] = {
	SR_HZ(3600),
	SR_MHZ(10),
	SR_HZ(1),
};

static const struct ftdi_chip_desc ft2232h_desc = {
	.vendor = 0x0403,
	.product = 0x6010,

	.multi_iface = TRUE,
	.num_ifaces = 2,

	.base_clock = 120000000u,
	.bitbang_divisor = 2u,

	.channel_names = {
		"ADBUS0", "ADBUS1", "ADBUS2", "ADBUS3",
		"ADBUS4", "ADBUS5", "ADBUS6", "ADBUS7",
		/* TODO: BDBUS[0..7] channels. */
		NULL
	}
};

static const struct ftdi_chip_desc ft2232h_tumpa_desc = {
	.vendor = 0x0403,
	.product = 0x8a98,

	.multi_iface = TRUE,
	.num_ifaces = 1, /* Second interface reserved for UART */

	.base_clock = 120000000u,
	.bitbang_divisor = 2u,

	/* 20 pin JTAG header */
	.channel_names = {
		"TCK", "TDI", "TDO", "TMS", "RST", "nTRST", "DBGRQ", "RTCK",
		NULL
	}
};

static const struct ftdi_chip_desc ft4232h_desc = {
	.vendor = 0x0403,
	.product = 0x6011,

	.multi_iface = TRUE,
	.num_ifaces = 4,

	.base_clock = 120000000u,
	.bitbang_divisor = 2u,

	.channel_names = {
		"ADBUS0", "ADBUS1", "ADBUS2", "ADBUS3",	"ADBUS4", "ADBUS5", "ADBUS6", "ADBUS7",
		/* TODO: BDBUS[0..7], CDBUS[0..7], DDBUS[0..7] channels. */
		NULL
	}
};

static const struct ftdi_chip_desc ft232r_desc = {
	.vendor = 0x0403,
	.product = 0x6001,

	.base_clock = 48000000u,
	.bitbang_divisor = 1u,

	.channel_names = {
		"TXD", "RXD", "RTS#", "CTS#", "DTR#", "DSR#", "DCD#", "RI#",
		NULL
	}
};

static const struct ftdi_chip_desc ft232h_desc = {
	.vendor = 0x0403,
	.product = 0x6014,

	.multi_iface = TRUE,
	.num_ifaces = 1,

	.base_clock = 120000000u,
	.bitbang_divisor = 2u,

	.channel_names = {
		"ADBUS0", "ADBUS1", "ADBUS2", "ADBUS3", "ADBUS4", "ADBUS5", "ADBUS6", "ADBUS7",
		NULL
	}
};

static const struct ftdi_chip_desc *chip_descs[] = {
	&ft2232h_desc,
	&ft2232h_tumpa_desc,
	&ft4232h_desc,
	&ft232r_desc,
	&ft232h_desc,
	NULL,
};

static void scan_device(struct libusb_device *dev, GSList **devices)
{
	struct libusb_device_descriptor usb_desc;
	struct libusb_device_handle *hdl;
	const struct ftdi_chip_desc *desc;
	struct dev_context *devc;
	char vendor[127], model[127], serial_num[127];
	char connection_id[64];
	struct sr_dev_inst *sdi;
	int rv;

	libusb_get_device_descriptor(dev, &usb_desc);

	desc = NULL;
	for (unsigned long i = 0; i < ARRAY_SIZE(chip_descs); i++) {
		desc = chip_descs[i];
		if (!desc)
			break;
		if (desc->vendor == usb_desc.idVendor &&
			desc->product == usb_desc.idProduct)
			break;
	}

	if (!desc)
		return;

	if ((rv = libusb_open(dev, &hdl)) != 0) {
		sr_warn("Failed to open potential device with "
			"VID:PID %04x:%04x: %s.", usb_desc.idVendor,
			usb_desc.idProduct, libusb_error_name(rv));
		return;
	}

	if (usb_desc.iManufacturer != 0) {
		if (libusb_get_string_descriptor_ascii(hdl, usb_desc.iManufacturer,
				(unsigned char *)vendor, sizeof(vendor)) < 0) {
			goto out_close_hdl;
		}
	} else {
		sr_dbg("The device lacks a manufacturer descriptor.");
		g_snprintf(vendor, sizeof(vendor), "Generic");
	}

	if (usb_desc.iProduct != 0) {
		if (libusb_get_string_descriptor_ascii(hdl, usb_desc.iProduct,
				(unsigned char *)model, sizeof(model)) < 0) {
			goto out_close_hdl;
		}
	} else {
		sr_dbg("The device lacks a product descriptor.");
		switch (usb_desc.idProduct) {
		case 0x6001:
			g_snprintf(model, sizeof(model), "FT232R");
			break;
		case 0x6010:
			g_snprintf(model, sizeof(model), "FT2232H");
			break;
		case 0x6011:
			g_snprintf(model, sizeof(model), "FT4232H");
			break;
		case 0x6014:
			g_snprintf(model, sizeof(model), "FT232H");
			break;
		case 0x8a98:
			g_snprintf(model, sizeof(model), "FT2232H-TUMPA");
			break;
		default:
			g_snprintf(model, sizeof(model), "Unknown");
			break;
		}
	}

	if (usb_desc.iSerialNumber != 0) {
		if (libusb_get_string_descriptor_ascii(hdl, usb_desc.iSerialNumber,
				(unsigned char *)serial_num, sizeof(serial_num)) < 0) {
			goto out_close_hdl;
		}
	} else {
		sr_dbg("The device lacks a serial number.");
		serial_num[0] = '\0';
	}

	if (usb_get_port_path(dev, connection_id, sizeof(connection_id)) < 0)
		goto out_close_hdl;

	sr_dbg("Found an FTDI device: %s.", model);

	devc = g_malloc0(sizeof(struct dev_context));

	devc->desc = desc;
	/* TODO: Expose all interfaces */
	devc->usb_iface_idx = 0;
	devc->ftdi_iface_idx = desc->multi_iface ? 1 : 0;
	devc->in_ep_addr = LIBUSB_ENDPOINT_IN | 1;
	devc->in_ep_pkt_size = libusb_get_max_packet_size(dev, devc->in_ep_addr);

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup(vendor);
	sdi->model = g_strdup(model);
	sdi->serial_num = g_strdup(serial_num);
	sdi->priv = devc;
	sdi->connection_id = g_strdup(connection_id);
	sdi->inst_type = SR_INST_USB;
	sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(dev),
			libusb_get_device_address(dev), NULL);

	for (char *const *chan = &(desc->channel_names[0]); *chan; chan++)
		sr_channel_new(sdi, chan - &(desc->channel_names[0]),
				SR_CHANNEL_LOGIC, TRUE, *chan);

	*devices = g_slist_append(*devices, sdi);

out_close_hdl:
	libusb_close(hdl);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config *src;
	struct sr_usb_dev_inst *usb;
	const char *conn;
	GSList *l, *conn_devices;
	GSList *devices;
	struct drv_context *drvc;
	libusb_device **devlist;
	int i;

	drvc = di->context;
	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		if (src->key == SR_CONF_CONN) {
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);
	else
		conn_devices = NULL;

	devices = NULL;
	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if (conn) {
			for (l = conn_devices; l; l = l->next) {
				usb = l->data;
				if (usb->bus == libusb_get_bus_number(devlist[i])
				    && usb->address == libusb_get_device_address(devlist[i]))
					break;
			}
			if (!l)
				/* This device is not one that matched the conn
				 * specification. */
				continue;
		}

		scan_device(devlist[i], &devices);
	}
	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);
	libusb_free_device_list(devlist, 1);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct drv_context *drvc = di->context;
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	int ret;

	ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);
	if (ret != SR_OK)
		return ret;

	libusb_detach_kernel_driver(usb->devhdl, devc->usb_iface_idx);
	/* Ignore failures and just try to claim anyway */

	ret = libusb_claim_interface(usb->devhdl, devc->usb_iface_idx);
	if (ret < 0) {
		sr_err("Failed to claim interface: %s.", libusb_error_name(ret));
		goto err_close_usb;
	}

	return SR_OK;

err_close_usb:
	sr_usb_close(usb);

	return SR_ERR;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;

	libusb_release_interface(usb->devhdl, devc->usb_iface_idx);
	sr_usb_close(usb);

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_CONN:
		if (!sdi || !sdi->conn)
			return SR_ERR_ARG;
		usb = sdi->conn;
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t value;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_MSEC:
		value = g_variant_get_uint64(data);
		/* TODO: Implement. */
		(void)value;
		return SR_ERR_NA;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_SAMPLERATE:
		value = g_variant_get_uint64(data);
		return ftdi_la_set_samplerate(sdi, value);
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static struct sr_dev_driver ftdi_la_driver_info = {
	.name = "ftdi-la",
	.longname = "FTDI LA",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = ftdi_la_start_acquisition,
	.dev_acquisition_stop = ftdi_la_stop_acquisition,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(ftdi_la_driver_info);
