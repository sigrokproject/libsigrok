/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
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
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t devopts[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
};

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dmm_info *dmm;
	struct sr_config *src;
	GSList *l, *devices;
	const char *conn, *serialcomm;
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int dropped, ret;
	size_t len;
	uint8_t buf[128];

	dmm = (struct dmm_info *)di;

	conn = serialcomm = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (!conn)
		return NULL;

	if (!serialcomm)
		serialcomm = dmm->conn;

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	sr_info("Probing serial port %s.", conn);

	drvc = di->context;
	devices = NULL;
	serial_flush(serial);

	/* Request a packet if the DMM requires this. */
	if (dmm->packet_request) {
		if ((ret = dmm->packet_request(serial)) < 0) {
			sr_err("Failed to request packet: %d.", ret);
			return FALSE;
		}
	}

	/*
	 * There's no way to get an ID from the multimeter. It just sends data
	 * periodically (or upon request), so the best we can do is check if
	 * the packets match the expected format.
	 */

	/* Let's get a bit of data and see if we can find a packet. */
	len = sizeof(buf);
	ret = serial_stream_detect(serial, buf, &len, dmm->packet_size,
				   dmm->packet_valid, 3000,
				   dmm->baudrate);
	if (ret != SR_OK)
		goto scan_cleanup;

	/*
	 * If we dropped more than two packets worth of data, something is
	 * wrong. We shouldn't quit however, since the dropped bytes might be
	 * just zeroes at the beginning of the stream. Those can occur as a
	 * combination of the nonstandard cable that ships with some devices
	 * and the serial port or USB to serial adapter.
	 */
	dropped = len - dmm->packet_size;
	if (dropped > 2 * dmm->packet_size)
		sr_warn("Had to drop too much data.");

	sr_info("Found device on port %s.", conn);

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup(dmm->vendor);
	sdi->model = g_strdup(dmm->device);
	devc = g_malloc0(sizeof(struct dev_context));
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->priv = devc;
	sdi->driver = di;
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P1");
	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

scan_cleanup:
	serial_close(serial);

	return devices;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
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
	struct sr_serial_dev_inst *serial;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	/*
	 * Reset the number of samples to take. If we've already collected our
	 * quota, but we start a new session, and don't reset this, we'll just
	 * quit without acquiring any new samples.
	 */
	devc->num_samples = 0;
	devc->starttime = g_get_monotonic_time();

	std_session_send_df_header(sdi, LOG_PREFIX);

	/* Poll every 50ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 50,
		      receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	return std_serial_dev_acquisition_stop(sdi, std_serial_dev_close,
			sdi->conn, LOG_PREFIX);
}

#define DMM(ID, CHIPSET, VENDOR, MODEL, CONN, BAUDRATE, PACKETSIZE, TIMEOUT, \
			DELAY, REQUEST, VALID, PARSE, DETAILS) \
	&(struct dmm_info) { \
		{ \
			.name = ID, \
			.longname = VENDOR " " MODEL, \
			.api_version = 1, \
			.init = init, \
			.cleanup = std_cleanup, \
			.scan = scan, \
			.dev_list = std_dev_list, \
			.config_get = NULL, \
			.config_set = config_set, \
			.config_list = config_list, \
			.dev_open = std_serial_dev_open, \
			.dev_close = std_serial_dev_close, \
			.dev_acquisition_start = dev_acquisition_start, \
			.dev_acquisition_stop = dev_acquisition_stop, \
			.context = NULL, \
		}, \
		VENDOR, MODEL, CONN, BAUDRATE, PACKETSIZE, TIMEOUT, DELAY, \
		REQUEST, VALID, PARSE, DETAILS, sizeof(struct CHIPSET##_info) \
	}

SR_PRIV const struct dmm_info *serial_dmm_drivers[] = {
	DMM(
		"bbcgm-2010", metex14,
		"BBC Goertz Metrawatt", "M2110", "1200/7n2", 1200,
		BBCGM_M2110_PACKET_SIZE, 0, 0, NULL,
		sr_m2110_packet_valid, sr_m2110_parse,
		NULL
	),
	DMM(
		"digitek-dt4000zc", fs9721,
		"Digitek", "DT4000ZC", "2400/8n1/dtr=1", 2400,
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_10_temp_c
	),
	DMM(
		"tekpower-tp4000ZC", fs9721,
		"TekPower", "TP4000ZC", "2400/8n1/dtr=1", 2400,
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_10_temp_c
	),
	DMM(
		"metex-me31", metex14,
		"Metex", "ME-31", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"peaktech-3410", metex14,
		"Peaktech", "3410", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"mastech-mas345", metex14,
		"MASTECH", "MAS345", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"mastech-ms8250b", fs9721,
		"MASTECH", "MS8250B", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL
	),
	DMM(
		"va-va18b", fs9721,
		"V&A", "VA18B", "2400/8n1", 2400,
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_01_temp_c
	),
	DMM(
		"va-va40b", fs9721,
		"V&A", "VA40B", "2400/8n1", 2400,
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_max_c_min
	),
	DMM(
		"metex-m3640d", metex14,
		"Metex", "M-3640D", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"metex-m4650cr", metex14,
		"Metex", "M-4650CR", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"peaktech-4370", metex14,
		"PeakTech", "4370", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"pce-pce-dm32", fs9721,
		"PCE", "PCE-DM32", "2400/8n1", 2400,
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_01_10_temp_f_c
	),
	DMM(
		"radioshack-22-168", metex14,
		"RadioShack", "22-168", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"radioshack-22-805", metex14,
		"RadioShack", "22-805", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"radioshack-22-812", rs9lcd,
		"RadioShack", "22-812", "4800/8n1/rts=0/dtr=1", 4800,
		RS9LCD_PACKET_SIZE, 0, 0, NULL,
		sr_rs9lcd_packet_valid, sr_rs9lcd_parse,
		NULL
	),
	DMM(
		"tecpel-dmm-8061-ser", fs9721,
		"Tecpel", "DMM-8061 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c
	),
	DMM(
		"voltcraft-m3650cr", metex14,
		"Voltcraft", "M-3650CR", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, 150, 20, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"voltcraft-m3650d", metex14,
		"Voltcraft", "M-3650D", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"voltcraft-m4650cr", metex14,
		"Voltcraft", "M-4650CR", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"voltcraft-me42", metex14,
		"Voltcraft", "ME-42", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, 250, 60, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"voltcraft-vc820-ser", fs9721,
		"Voltcraft", "VC-820 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL
	),
	DMM(
		/*
		 * Note: The VC830 doesn't set the 'volt' and 'diode' bits of
		 * the FS9922 protocol. Instead, it only sets the user-defined
		 * bit "z1" to indicate "diode mode" and "voltage".
		 */
		"voltcraft-vc830-ser", fs9922,
		"Voltcraft", "VC-830 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9922_PACKET_SIZE, 0, 0, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse,
		&sr_fs9922_z1_diode
	),
	DMM(
		"voltcraft-vc840-ser", fs9721,
		"Voltcraft", "VC-840 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c
	),
	DMM(
		"voltcraft-vc870-ser", vc870,
		"Voltcraft", "VC-870 (UT-D02 cable)", "9600/8n1/rts=0/dtr=1",
		9600, VC870_PACKET_SIZE, 0, 0, NULL,
		sr_vc870_packet_valid, sr_vc870_parse, NULL
	),
	DMM(
		"voltcraft-vc920-ser", ut71x,
		"Voltcraft", "VC-920 (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		2400, UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"voltcraft-vc940-ser", ut71x,
		"Voltcraft", "VC-940 (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		2400, UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"voltcraft-vc960-ser", ut71x, 
		"Voltcraft", "VC-960 (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		2400, UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut60a-ser", fs9721,
		"UNI-T", "UT60A (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL
	),
	DMM(
		"uni-t-ut60e-ser", fs9721,
		"UNI-T", "UT60E (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c
	),
	DMM(
		/* Note: ES51986 baudrate is actually 19230! */
		"uni-t-ut60g-ser", es519xx,
		"UNI-T", "UT60G (UT-D02 cable)", "19200/7o1/rts=0/dtr=1",
		19200, ES519XX_11B_PACKET_SIZE, 0, 0, NULL,
		sr_es519xx_19200_11b_packet_valid, sr_es519xx_19200_11b_parse,
		NULL
	),
	DMM(
		"uni-t-ut61b-ser", fs9922,
		"UNI-T", "UT61B (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9922_PACKET_SIZE, 0, 0, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse, NULL
	),
	DMM(
		"uni-t-ut61c-ser", fs9922,
		"UNI-T", "UT61C (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9922_PACKET_SIZE, 0, 0, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse, NULL
	),
	DMM(
		"uni-t-ut61d-ser", fs9922,
		"UNI-T", "UT61D (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9922_PACKET_SIZE, 0, 0, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse, NULL
	),
	DMM(
		"uni-t-ut61e-ser", es519xx,
		/* Note: ES51922 baudrate is actually 19230! */
		"UNI-T", "UT61E (UT-D02 cable)", "19200/7o1/rts=0/dtr=1",
		19200, ES519XX_14B_PACKET_SIZE, 0, 0, NULL,
		sr_es519xx_19200_14b_packet_valid, sr_es519xx_19200_14b_parse,
		NULL
	),
	DMM(
		"uni-t-ut71a-ser", ut71x,
		"UNI-T", "UT71A (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		2400, UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut71b-ser", ut71x,
		"UNI-T", "UT71B (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		2400, UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut71c-ser", ut71x,
		"UNI-T", "UT71C (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		2400, UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut71d-ser", ut71x,
		"UNI-T", "UT71D (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		2400, UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut71e-ser", ut71x,
		"UNI-T", "UT71E (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		2400, UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"iso-tech-idm103n", es519xx,
		"ISO-TECH", "IDM103N", "2400/7o1/rts=0/dtr=1",
		2400, ES519XX_11B_PACKET_SIZE, 0, 0, NULL,
		sr_es519xx_2400_11b_packet_valid, sr_es519xx_2400_11b_parse,
		NULL
	),
	DMM(
		"tenma-72-7730-ser", ut71x,
		"Tenma", "72-7730 (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		2400, UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"tenma-72-7732-ser", ut71x,
		"Tenma", "72-7732 (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		2400, UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"tenma-72-9380a-ser", ut71x,
		"Tenma", "72-9380A (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		2400, UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"tenma-72-7745-ser", fs9721,
		"Tenma", "72-7745 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c
	),
	DMM(
		"tenma-72-7750-ser", es519xx,
		/* Note: ES51986 baudrate is actually 19230! */
		"Tenma", "72-7750 (UT-D02 cable)", "19200/7o1/rts=0/dtr=1",
		19200, ES519XX_11B_PACKET_SIZE, 0, 0, NULL,
		sr_es519xx_19200_11b_packet_valid, sr_es519xx_19200_11b_parse,
		NULL
	),
	DMM(
		"brymen-bm25x", bm25x,
		"Brymen", "BM25x", "9600/8n1/rts=1/dtr=1",
		9600, BRYMEN_BM25X_PACKET_SIZE, 0, 0, NULL,
		sr_brymen_bm25x_packet_valid, sr_brymen_bm25x_parse,
		NULL
	),
	DMM(
		"velleman-dvm4100", dtm0660,
		"Velleman", "DVM4100", "2400/8n1/rts=0/dtr=1",
		2400, DTM0660_PACKET_SIZE, 0, 0, NULL,
		sr_dtm0660_packet_valid, sr_dtm0660_parse, NULL
	),
	DMM(
		"peaktech-3415", dtm0660,
		"Peaktech", "3415", "2400/8n1/rts=0/dtr=1",
		2400, DTM0660_PACKET_SIZE, 0, 0, NULL,
		sr_dtm0660_packet_valid, sr_dtm0660_parse, NULL
	),
	NULL
};
