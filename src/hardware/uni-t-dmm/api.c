/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012-2013 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t devopts[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET | SR_CONF_GET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET | SR_CONF_GET,
};

/*
 * Note 1: The actual baudrate of the Cyrustek ES519xx chip used in this DMM
 * is 19230. However, the WCH CH9325 chip (UART to USB/HID) used in (some
 * versions of) the UNI-T UT-D04 cable doesn't support 19230 baud. It only
 * supports 19200, and setting an unsupported baudrate will result in the
 * default of 2400 being used (which will not work with this DMM, of course).
 */

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	GSList *usb_devices, *devices, *l;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct dmm_info *dmm;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	const char *conn;

	drvc = di->context;
	dmm = (struct dmm_info *)di;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (!conn)
		return NULL;

	devices = NULL;
	if (!(usb_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn))) {
		g_slist_free_full(usb_devices, g_free);
		return NULL;
	}

	for (l = usb_devices; l; l = l->next) {
		usb = l->data;
		devc = g_malloc0(sizeof(struct dev_context));
		devc->first_run = TRUE;
		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup(dmm->vendor);
		sdi->model = g_strdup(dmm->device);
		sdi->priv = devc;
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P1");
		sdi->inst_type = SR_INST_USB;
		sdi->conn = usb;
		devices = g_slist_append(devices, sdi);
	}

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	int ret;

	di = sdi->driver;
	drvc = di->context;
	usb = sdi->conn;

	if ((ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb)) == SR_OK)
		sdi->status = SR_ST_ACTIVE;

	return ret;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	/* TODO */

	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;

	return sr_sw_limits_config_set(&devc->limits, key, data);
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	(void)sdi;
	(void)cg;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->limits);

	std_session_send_df_header(sdi);

	sr_session_source_add(sdi->session, -1, 0, 10 /* poll_timeout */,
			uni_t_dmm_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	sr_dbg("Stopping acquisition.");
	std_session_send_df_end(sdi);
	sr_session_source_remove(sdi->session, -1);

	return SR_OK;
}

#define DMM(ID, CHIPSET, VENDOR, MODEL, BAUDRATE, PACKETSIZE, \
			VALID, PARSE, DETAILS) \
	&((struct dmm_info) { \
		{ \
			.name = ID, \
			.longname = VENDOR " " MODEL, \
			.api_version = 1, \
			.init = std_init, \
			.cleanup = std_cleanup, \
			.scan = scan, \
			.dev_list = std_dev_list, \
			.config_get = NULL, \
			.config_set = config_set, \
			.config_list = config_list, \
			.dev_open = dev_open, \
			.dev_close = dev_close, \
			.dev_acquisition_start = dev_acquisition_start, \
			.dev_acquisition_stop = dev_acquisition_stop, \
			.context = NULL, \
		}, \
		VENDOR, MODEL, BAUDRATE, PACKETSIZE, \
		VALID, PARSE, DETAILS, sizeof(struct CHIPSET##_info) \
	}).di

SR_REGISTER_DEV_DRIVER_LIST(uni_t_dmm_drivers,
	DMM(
		"tecpel-dmm-8061", fs9721,
		"Tecpel", "DMM-8061", 2400,
		FS9721_PACKET_SIZE,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c
	),
	DMM(
		"uni-t-ut372", ut372,
		"UNI-T", "UT372", 2400,
		UT372_PACKET_SIZE,
		sr_ut372_packet_valid, sr_ut372_parse,
		NULL
	),
	DMM(
		"uni-t-ut60a", fs9721,
		"UNI-T", "UT60A", 2400,
		FS9721_PACKET_SIZE,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL
	),
	DMM(
		"uni-t-ut60e", fs9721,
		"UNI-T", "UT60E", 2400,
		FS9721_PACKET_SIZE,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c
	),
	DMM(
		"uni-t-ut60g", es519xx,
		/* The baudrate is actually 19230, see "Note 1" below. */
		"UNI-T", "UT60G", 19200,
		ES519XX_11B_PACKET_SIZE,
		sr_es519xx_19200_11b_packet_valid, sr_es519xx_19200_11b_parse,
		NULL
	),
	DMM(
		"uni-t-ut61b", fs9922,
		"UNI-T", "UT61B", 2400,
		FS9922_PACKET_SIZE,
		sr_fs9922_packet_valid, sr_fs9922_parse,
		NULL
	),
	DMM(
		"uni-t-ut61c", fs9922,
		"UNI-T", "UT61C", 2400,
		FS9922_PACKET_SIZE,
		sr_fs9922_packet_valid, sr_fs9922_parse,
		NULL
	),
	DMM(
		"uni-t-ut61d", fs9922,
		"UNI-T", "UT61D", 2400,
		FS9922_PACKET_SIZE,
		sr_fs9922_packet_valid, sr_fs9922_parse,
		NULL
	),
	DMM(
		"uni-t-ut61e", es519xx,
		/* The baudrate is actually 19230, see "Note 1" below. */
		"UNI-T", "UT61E", 19200,
		ES519XX_14B_PACKET_SIZE,
		sr_es519xx_19200_14b_packet_valid, sr_es519xx_19200_14b_parse,
		NULL
	),
	DMM(
		"uni-t-ut71a", ut71x,
		"UNI-T", "UT71A", 2400, UT71X_PACKET_SIZE,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut71b", ut71x,
		"UNI-T", "UT71B", 2400, UT71X_PACKET_SIZE,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut71c", ut71x,
		"UNI-T", "UT71C", 2400, UT71X_PACKET_SIZE,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut71d", ut71x,
		"UNI-T", "UT71D", 2400, UT71X_PACKET_SIZE,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut71e", ut71x,
		"UNI-T", "UT71E", 2400, UT71X_PACKET_SIZE,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"voltcraft-vc820", fs9721,
		"Voltcraft", "VC-820", 2400,
		FS9721_PACKET_SIZE,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL
	),
	DMM(
		"voltcraft-vc830", fs9922,
		/*
		 * Note: The VC830 doesn't set the 'volt' and 'diode' bits of
		 * the FS9922 protocol. Instead, it only sets the user-defined
		 * bit "z1" to indicate "diode mode" and "voltage".
		 */
		"Voltcraft", "VC-830", 2400,
		FS9922_PACKET_SIZE,
		sr_fs9922_packet_valid, sr_fs9922_parse,
		&sr_fs9922_z1_diode
	),
	DMM(
		"voltcraft-vc840", fs9721,
		"Voltcraft", "VC-840", 2400,
		FS9721_PACKET_SIZE,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c
	),
	DMM(
		"voltcraft-vc870", vc870,
		"Voltcraft", "VC-870", 9600, VC870_PACKET_SIZE,
		sr_vc870_packet_valid, sr_vc870_parse, NULL
	),
	DMM(
		"voltcraft-vc920", ut71x,
		"Voltcraft", "VC-920", 2400, UT71X_PACKET_SIZE,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"voltcraft-vc940", ut71x,
		"Voltcraft", "VC-940", 2400, UT71X_PACKET_SIZE,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"voltcraft-vc960", ut71x,
		"Voltcraft", "VC-960", 2400, UT71X_PACKET_SIZE,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"tenma-72-7730", ut71x,
		"Tenma", "72-7730", 2400,
		UT71X_PACKET_SIZE,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"tenma-72-7732", ut71x,
		"Tenma", "72-7732", 2400,
		UT71X_PACKET_SIZE,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"tenma-72-9380a", ut71x,
		"Tenma", "72-9380A", 2400,
		UT71X_PACKET_SIZE,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"tenma-72-7745", es519xx,
		"Tenma", "72-7745", 2400,
		FS9721_PACKET_SIZE,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c
	),
	DMM(
		"tenma-72-7750", es519xx,
		/* The baudrate is actually 19230, see "Note 1" below. */
		"Tenma", "72-7750", 19200,
		ES519XX_11B_PACKET_SIZE,
		sr_es519xx_19200_11b_packet_valid, sr_es519xx_19200_11b_parse,
		NULL
	),
);
