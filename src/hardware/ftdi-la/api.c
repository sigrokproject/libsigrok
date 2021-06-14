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

#include <ctype.h>
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

static const struct ftdi_chip_desc ft2232h_desc = {
	.vendor = 0x0403,
	.product = 0x6010,

	.multi_iface = TRUE,
	.num_ifaces = 2,

	.base_clock = 120000000u,
	.bitbang_divisor = 2u,
	/* My testing on two separate FT2232H chips indicates that channel A
	 * can run successfully at 15MHz but that channel B will run at 7.5MHz
	 * if you ask for 15. It's strange, but I'm not gonna turn down three
	 * extra MHz by limiting both to 12 :) */
	.max_sample_rates = {15000000u, 12000000u},

	.channel_names = {
		"ADBUS0", "ADBUS1", "ADBUS2", "ADBUS3", "ADBUS4", "ADBUS5", "ADBUS6", "ADBUS7",
		"BDBUS0", "BDBUS1", "BDBUS2", "BDBUS3", "BDBUS4", "BDBUS5", "BDBUS6", "BDBUS7",
	}
};

static const struct ftdi_chip_desc ft2232h_tumpa_desc = {
	.vendor = 0x0403,
	.product = 0x8a98,

	.multi_iface = TRUE,
	.num_ifaces = 1, /* Second interface reserved for UART */

	.base_clock = 120000000u,
	.bitbang_divisor = 2u,
	.max_sample_rates = {15000000u, 12000000u},

	/* 20 pin JTAG header */
	.channel_names = {
		"TCK", "TDI", "TDO", "TMS", "RST", "nTRST", "DBGRQ", "RTCK",
	}
};

static const struct ftdi_chip_desc ft4232h_desc = {
	.vendor = 0x0403,
	.product = 0x6011,

	.multi_iface = TRUE,
	.num_ifaces = 4,

	.base_clock = 120000000u,
	.bitbang_divisor = 2u,
	/* TODO: It's likely that channel A (and maybe C or D too) can run at
	 * 15MHz on the FT4232H just like on the FT2232H, as the two chips use
	 * the same die internally. However, since I don't have a FT4232 to
	 * test with, I'm playing it safe and capping them all to 12MHz for
	 * now. */
	.max_sample_rates = {12000000u, 12000000u, 12000000u, 12000000u},

	.channel_names = {
		"ADBUS0", "ADBUS1", "ADBUS2", "ADBUS3",	"ADBUS4", "ADBUS5", "ADBUS6", "ADBUS7",
		"BDBUS0", "BDBUS1", "BDBUS2", "BDBUS3", "BDBUS4", "BDBUS5", "BDBUS6", "BDBUS7",
		"CDBUS0", "CDBUS1", "CDBUS2", "CDBUS3", "CDBUS4", "CDBUS5", "CDBUS6", "CDBUS7",
		"DDBUS0", "DDBUS1", "DDBUS2", "DDBUS3", "DDBUS4", "DDBUS5", "DDBUS6", "DDBUS7",
	}
};

static const struct ftdi_chip_desc ft232h_desc = {
	.vendor = 0x0403,
	.product = 0x6014,

	.multi_iface = TRUE,
	.num_ifaces = 1,

	.base_clock = 120000000u,
	.bitbang_divisor = 2u,
	/* TODO: This can also probably be 15MHz. See FT4232H comment above. */
	.max_sample_rates = {12000000u},

	.channel_names = {
		"ADBUS0", "ADBUS1", "ADBUS2", "ADBUS3", "ADBUS4", "ADBUS5", "ADBUS6", "ADBUS7",
	}
};

/* TODO: The FT230X and FT231X are a new generation of full-speed chips that
 * reportedly lack the bitbang erratum that makes the FT232R unusable. They
 * ought to be usable with this driver's code as-is, but I don't have the
 * hardware to validate this, so they aren't in the list of chips yet. I would
 * expect them to have a descriptor almost identical to the former FT232R
 * descriptor that was removed from this driver ("git blame" this comment to
 * find it). */

static const struct ftdi_chip_desc *chip_descs[] = {
	&ft2232h_desc,
	&ft2232h_tumpa_desc,
	&ft4232h_desc,
	&ft232h_desc,
	NULL,
};

/* iface_idx indicates which device channel to scan. -1 scans all of them. */
static void scan_device(struct libusb_device *dev, GSList **devices, int iface_idx)
{
	struct libusb_device_descriptor usb_desc;
	struct libusb_config_descriptor *config;
	const struct libusb_interface_descriptor *iface;
	struct libusb_device_handle *hdl;
	const struct ftdi_chip_desc *desc;
	struct dev_context *devc;
	char vendor[127], model[127], serial_num[127];
	char connection_id[64];
	struct sr_dev_inst *sdi;
	unsigned int num_ifaces;
	int in_ep_idx;
	int rv;

	libusb_get_device_descriptor(dev, &usb_desc);

	if (usb_desc.idVendor == 0x0403 && usb_desc.idProduct == 0x6001) {
		sr_warn("Detected an FT232R, which FTDI-LA no longer supports "
			"due to a silicon bug. See "
			"https://sigrok.org/wiki/FTDI-LA#FT232R_Support_Removal"
			" for more information.");
		return;
	}

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

	if ((rv = libusb_get_active_config_descriptor(dev, &config)) != 0) {
		sr_warn("Failed to get config descriptor for device: %s.",
			libusb_error_name(rv));
		goto out_close_hdl;
	}

	num_ifaces = desc->multi_iface ? desc->num_ifaces : 1;
	if (config->bNumInterfaces < num_ifaces) {
		sr_err("Found FTDI device with fewer USB interfaces than we "
			"expect for its type. This is a bug in libsigrok.");
		goto out_free_config;
	}

	sr_dbg("Found a %d-channel FTDI device: %s.", num_ifaces, model);

	for (unsigned int i = 0; i < num_ifaces; i++) {
		/* If the user asked for a specific interface, skip the others. */
		if (iface_idx >= 0 && (unsigned int)iface_idx != i)
			continue;

		if (config->interface[i].num_altsetting <= 0) {
			sr_err("FTDI interface %d has bad num_altsetting %d",
				i, config->interface[i].num_altsetting);
			goto out_free_config;
		}

		iface = &config->interface[i].altsetting[0];

		/* Locate the IN endpoint */
		in_ep_idx = -1;
		for (uint8_t j = 0; j < iface->bNumEndpoints; j++) {
			/* LIBUSB_TRANSFER_TYPE_BULK should more properly be
			 * LIBUSB_ENDPOINT_TRANSFER_TYPE_BULK, but we currently support
			 * libusb 1.0.20-rc3, which does not include that enum value, for
			 * Windows builds. */
			if ((iface->endpoint[j].bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN &&
					(iface->endpoint[j].bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) == LIBUSB_TRANSFER_TYPE_BULK) {
				in_ep_idx = j;
				break;
			}
		}
		if (in_ep_idx == -1) {
			sr_err("FTDI interface %d has no bulk IN endpoint", i);
			goto out_free_config;
		}

		devc = g_malloc0(sizeof(struct dev_context));
		devc->desc = desc;
		devc->usb_iface_idx = i;
		devc->ftdi_iface_idx = desc->multi_iface ? i + 1 : i;
		devc->in_ep_addr = iface->endpoint[in_ep_idx].bEndpointAddress;
		devc->in_ep_pkt_size = iface->endpoint[in_ep_idx].wMaxPacketSize;

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup(vendor);
		sdi->model = g_strdup(model);
		sdi->serial_num = g_strdup(serial_num);
		sdi->priv = devc;
		if (num_ifaces > 1) {
			sdi->connection_id = g_strdup_printf("%s, channel %c",
					connection_id, 'A' + i);
		} else {
			sdi->connection_id = g_strdup(connection_id);
		}
		sdi->inst_type = SR_INST_USB;
		sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(dev),
				libusb_get_device_address(dev), NULL);

		for (int chan = 0; chan < 8; chan++)
			sr_channel_new(sdi, chan, SR_CHANNEL_LOGIC, TRUE,
					desc->channel_names[(i*8) + chan]);

		*devices = g_slist_append(*devices, sdi);
	}

out_free_config:
	libusb_free_config_descriptor(config);

out_close_hdl:
	libusb_close(hdl);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config *src;
	struct sr_usb_dev_inst *usb;
	const char *conn;
	gchar **conn_parts;
	gboolean conn_has_usb = FALSE;
	GSList *l, *conn_devices = NULL;
	int conn_iface = -1;
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

	if (conn && conn[0]) {
		conn_parts = g_strsplit(conn, "/", 2);

		/* USB identifier */
		if (conn_parts[0][0]) {
			conn_has_usb = TRUE;
			conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn_parts[0]);
		}

		/* Interface identifier (e.g. A or B; case-insensitive) */
		if (conn_parts[1]) {
			if (strlen(conn_parts[1]) != 1 || !isalpha(conn_parts[1][0])) {
				sr_err("Invalid interface ID: %s.", conn_parts[1]);
			} else {
				conn_iface = toupper(conn_parts[1][0]) - 'A';
			}
		}

		g_strfreev(conn_parts);
	}

	devices = NULL;
	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if (conn_has_usb) {
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

		scan_device(devlist[i], &devices, conn_iface);
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

static GVariant *get_samplerates(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	uint64_t samplerates[] = {
		SR_HZ(3600),
		devc->desc->max_sample_rates[devc->usb_iface_idx],
		SR_HZ(1),
	};
	
	return std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates));
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = get_samplerates(sdi);
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
