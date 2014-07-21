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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdlib.h>
#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

#define UNI_T_UT_D04_NEW "1a86.e008"

static const int32_t hwopts[] = {
	SR_CONF_CONN,
};

static const int32_t hwcaps[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_CONTINUOUS,
};

SR_PRIV struct sr_dev_driver tecpel_dmm_8061_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut60a_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut60e_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut60g_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut61b_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut61c_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut61d_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut61e_driver_info;
SR_PRIV struct sr_dev_driver voltcraft_vc820_driver_info;
SR_PRIV struct sr_dev_driver voltcraft_vc830_driver_info;
SR_PRIV struct sr_dev_driver voltcraft_vc840_driver_info;
SR_PRIV struct sr_dev_driver tenma_72_7745_driver_info;
SR_PRIV struct sr_dev_driver tenma_72_7750_driver_info;

SR_PRIV struct dmm_info udmms[] = {
	{
		"Tecpel", "DMM-8061", 2400,
		FS9721_PACKET_SIZE,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c,
		&tecpel_dmm_8061_driver_info, receive_data_TECPEL_DMM_8061,
	},
	{
		"UNI-T", "UT60A", 2400,
		FS9721_PACKET_SIZE,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL,
		&uni_t_ut60a_driver_info, receive_data_UNI_T_UT60A,
	},
	{
		"UNI-T", "UT60E", 2400,
		FS9721_PACKET_SIZE,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c,
		&uni_t_ut60e_driver_info, receive_data_UNI_T_UT60E,
	},
	{
		/* The baudrate is actually 19230, see "Note 1" below. */
		"UNI-T", "UT60G", 19200,
		ES519XX_11B_PACKET_SIZE,
		sr_es519xx_19200_11b_packet_valid, sr_es519xx_19200_11b_parse,
		NULL,
		&uni_t_ut60g_driver_info, receive_data_UNI_T_UT60G,
	},
	{
		"UNI-T", "UT61B", 2400,
		FS9922_PACKET_SIZE,
		sr_fs9922_packet_valid, sr_fs9922_parse,
		NULL,
		&uni_t_ut61b_driver_info, receive_data_UNI_T_UT61B,
	},
	{
		"UNI-T", "UT61C", 2400,
		FS9922_PACKET_SIZE,
		sr_fs9922_packet_valid, sr_fs9922_parse,
		NULL,
		&uni_t_ut61c_driver_info, receive_data_UNI_T_UT61C,
	},
	{
		"UNI-T", "UT61D", 2400,
		FS9922_PACKET_SIZE,
		sr_fs9922_packet_valid, sr_fs9922_parse,
		NULL,
		&uni_t_ut61d_driver_info, receive_data_UNI_T_UT61D,
	},
	{
		/* The baudrate is actually 19230, see "Note 1" below. */
		"UNI-T", "UT61E", 19200,
		ES519XX_14B_PACKET_SIZE,
		sr_es519xx_19200_14b_packet_valid, sr_es519xx_19200_14b_parse,
		NULL,
		&uni_t_ut61e_driver_info, receive_data_UNI_T_UT61E,
	},
	{
		"Voltcraft", "VC-820", 2400,
		FS9721_PACKET_SIZE,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL,
		&voltcraft_vc820_driver_info, receive_data_VOLTCRAFT_VC820,
	},
	{
		/*
		 * Note: The VC830 doesn't set the 'volt' and 'diode' bits of
		 * the FS9922 protocol. Instead, it only sets the user-defined
		 * bit "z1" to indicate "diode mode" and "voltage".
		 */
		"Voltcraft", "VC-830", 2400,
		FS9922_PACKET_SIZE,
		sr_fs9922_packet_valid, sr_fs9922_parse,
		&sr_fs9922_z1_diode,
		&voltcraft_vc830_driver_info, receive_data_VOLTCRAFT_VC830,
	},
	{
		"Voltcraft", "VC-840", 2400,
		FS9721_PACKET_SIZE,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c,
		&voltcraft_vc840_driver_info, receive_data_VOLTCRAFT_VC840,
	},
	{
		"Tenma", "72-7745", 2400,
		FS9721_PACKET_SIZE,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c,
		&tenma_72_7745_driver_info, receive_data_TENMA_72_7745,
	},
	{
		/* The baudrate is actually 19230, see "Note 1" below. */
		"Tenma", "72-7750", 19200,
		ES519XX_11B_PACKET_SIZE,
		sr_es519xx_19200_11b_packet_valid, sr_es519xx_19200_11b_parse,
		NULL,
		&tenma_72_7750_driver_info, receive_data_TENMA_72_7750,
	},
};

/*
 * Note 1: The actual baudrate of the Cyrustek ES519xx chip used in this DMM
 * is 19230. However, the WCH CH9325 chip (UART to USB/HID) used in (some
 * versions of) the UNI-T UT-D04 cable doesn't support 19230 baud. It only
 * supports 19200, and setting an unsupported baudrate will result in the
 * default of 2400 being used (which will not work with this DMM, of course).
 */

static int dev_clear(int dmm)
{
	return std_dev_clear(udmms[dmm].di, NULL);
}

static int init(struct sr_context *sr_ctx, int dmm)
{
	sr_dbg("Selected '%s' subdriver.", udmms[dmm].di->name);

	return std_init(sr_ctx, udmms[dmm].di, LOG_PREFIX);
}

static GSList *scan(GSList *options, int dmm)
{
	GSList *usb_devices, *devices, *l;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct sr_config *src;
	struct sr_channel *ch;
	const char *conn;

	drvc = udmms[dmm].di->priv;

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

		if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
			sr_err("Device context malloc failed.");
			return NULL;
		}

		devc->first_run = TRUE;

		if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE,
				udmms[dmm].vendor, udmms[dmm].device, NULL))) {
			sr_err("sr_dev_inst_new returned NULL.");
			return NULL;
		}
		sdi->priv = devc;
		sdi->driver = udmms[dmm].di;
		if (!(ch = sr_channel_new(0, SR_CHANNEL_ANALOG, TRUE, "P1")))
			return NULL;
		sdi->channels = g_slist_append(sdi->channels, ch);

		sdi->inst_type = SR_INST_USB;
		sdi->conn = usb;

		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);
	}

	return devices;
}

static GSList *dev_list(int dmm)
{
	return ((struct drv_context *)(udmms[dmm].di->priv))->instances;
}

static int dev_open(struct sr_dev_inst *sdi, int dmm)
{
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	int ret;

	drvc = udmms[dmm].di->priv;
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

static int cleanup(int dmm)
{
	return dev_clear(dmm);
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;

	switch (id) {
	case SR_CONF_LIMIT_MSEC:
		if (g_variant_get_uint64(data) == 0) {
			sr_err("Time limit cannot be 0.");
			return SR_ERR;
		}
		devc->limit_msec = g_variant_get_uint64(data);
		sr_dbg("Setting time limit to %" PRIu64 "ms.",
		       devc->limit_msec);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		if (g_variant_get_uint64(data) == 0) {
			sr_err("Sample limit cannot be 0.");
			return SR_ERR;
		}
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	(void)sdi;
	(void)cg;

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

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data, int dmm)
{
	struct dev_context *devc;

	devc = sdi->priv;

	devc->cb_data = cb_data;

	devc->starttime = g_get_monotonic_time();

	/* Send header packet to the session bus. */
	std_session_send_df_header(sdi, LOG_PREFIX);

	sr_session_source_add(sdi->session, 0, 0, 10 /* poll_timeout */,
		      udmms[dmm].receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_datafeed_packet packet;

	(void)cb_data;

	sr_dbg("Stopping acquisition.");

	/* Send end packet to the session bus. */
	sr_dbg("Sending SR_DF_END.");
	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);

	sr_session_source_remove(sdi->session, 0);

	return SR_OK;
}

/* Driver-specific API function wrappers */
#define HW_INIT(X) \
static int init_##X(struct sr_context *sr_ctx) { return init(sr_ctx, X); }
#define HW_CLEANUP(X) \
static int cleanup_##X(void) { return cleanup(X); }
#define HW_SCAN(X) \
static GSList *scan_##X(GSList *options) { return scan(options, X); }
#define HW_DEV_LIST(X) \
static GSList *dev_list_##X(void) { return dev_list(X); }
#define HW_DEV_CLEAR(X) \
static int dev_clear_##X(void) { return dev_clear(X); }
#define HW_DEV_ACQUISITION_START(X) \
static int dev_acquisition_start_##X(const struct sr_dev_inst *sdi, \
void *cb_data) { return dev_acquisition_start(sdi, cb_data, X); }
#define HW_DEV_OPEN(X) \
static int dev_open_##X(struct sr_dev_inst *sdi) { return dev_open(sdi, X); }

/* Driver structs and API function wrappers */
#define DRV(ID, ID_UPPER, NAME, LONGNAME) \
HW_INIT(ID_UPPER) \
HW_CLEANUP(ID_UPPER) \
HW_SCAN(ID_UPPER) \
HW_DEV_LIST(ID_UPPER) \
HW_DEV_CLEAR(ID_UPPER) \
HW_DEV_ACQUISITION_START(ID_UPPER) \
HW_DEV_OPEN(ID_UPPER) \
SR_PRIV struct sr_dev_driver ID##_driver_info = { \
	.name = NAME, \
	.longname = LONGNAME, \
	.api_version = 1, \
	.init = init_##ID_UPPER, \
	.cleanup = cleanup_##ID_UPPER, \
	.scan = scan_##ID_UPPER, \
	.dev_list = dev_list_##ID_UPPER, \
	.dev_clear = dev_clear_##ID_UPPER, \
	.config_get = NULL, \
	.config_set = config_set, \
	.config_list = config_list, \
	.dev_open = dev_open_##ID_UPPER, \
	.dev_close = dev_close, \
	.dev_acquisition_start = dev_acquisition_start_##ID_UPPER, \
	.dev_acquisition_stop = dev_acquisition_stop, \
	.priv = NULL, \
};

DRV(tecpel_dmm_8061, TECPEL_DMM_8061, "tecpel-dmm-8061", "Tecpel DMM-8061")
DRV(uni_t_ut60a, UNI_T_UT60A, "uni-t-ut60a", "UNI-T UT60A")
DRV(uni_t_ut60e, UNI_T_UT60E, "uni-t-ut60e", "UNI-T UT60E")
DRV(uni_t_ut60g, UNI_T_UT60G, "uni-t-ut60g", "UNI-T UT60G")
DRV(uni_t_ut61b, UNI_T_UT61B, "uni-t-ut61b", "UNI-T UT61B")
DRV(uni_t_ut61c, UNI_T_UT61C, "uni-t-ut61c", "UNI-T UT61C")
DRV(uni_t_ut61d, UNI_T_UT61D, "uni-t-ut61d", "UNI-T UT61D")
DRV(uni_t_ut61e, UNI_T_UT61E, "uni-t-ut61e", "UNI-T UT61E")
DRV(voltcraft_vc820, VOLTCRAFT_VC820, "voltcraft-vc820", "Voltcraft VC-820")
DRV(voltcraft_vc830, VOLTCRAFT_VC830, "voltcraft-vc830", "Voltcraft VC-830")
DRV(voltcraft_vc840, VOLTCRAFT_VC840, "voltcraft-vc840", "Voltcraft VC-840")
DRV(tenma_72_7745, TENMA_72_7745, "tenma-72-7745", "Tenma 72-7745")
DRV(tenma_72_7750, TENMA_72_7750, "tenma-72-7750", "Tenma 72-7750")
