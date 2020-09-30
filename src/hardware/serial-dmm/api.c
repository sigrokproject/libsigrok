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

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dmm_info *dmm;
	struct sr_config *src;
	GSList *l, *devices;
	const char *conn, *serialcomm;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int ret;
	size_t dropped, len, packet_len;
	uint8_t buf[128];
	size_t ch_idx;
	char ch_name[12];

	dmm = (struct dmm_info *)di;

	conn = dmm->conn;
	serialcomm = dmm->serialcomm;
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

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;
	sr_info("Probing serial port %s.", conn);

	if (dmm->after_open) {
		ret = dmm->after_open(serial);
		if (ret != SR_OK) {
			sr_err("Activity after port open failed: %d.", ret);
			return NULL;
		}
	}

	devices = NULL;

	/* Request a packet if the DMM requires this. */
	if (dmm->packet_request) {
		if ((ret = dmm->packet_request(serial)) < 0) {
			sr_err("Failed to request packet: %d.", ret);
			return NULL;
		}
	}

	/*
	 * There's no way to get an ID from the multimeter. It just sends data
	 * periodically (or upon request), so the best we can do is check if
	 * the packets match the expected format.
	 *
	 * If we dropped more than two packets worth of data, something is
	 * wrong. We shouldn't quit however, since the dropped bytes might be
	 * just zeroes at the beginning of the stream. Those can occur as a
	 * combination of the nonstandard cable that ships with some devices
	 * and the serial port or USB to serial adapter.
	 */
	len = sizeof(buf);
	ret = serial_stream_detect(serial, buf, &len, dmm->packet_size,
		dmm->packet_valid, dmm->packet_valid_len, &packet_len, 3000);
	if (ret != SR_OK)
		goto scan_cleanup;
	dropped = len - dmm->packet_size;
	if (dropped > 2 * packet_len)
		sr_warn("Packet search dropped a lot of data.");
	sr_info("Found device on port %s.", conn);

	/*
	 * Setup optional additional callbacks when sub device drivers
	 * happen to provide them. (This is a compromise to do it here,
	 * and not extend the DMM_CONN() et al set of macros.)
	 */
	if (strcmp(dmm->di.name, "brymen-bm52x") == 0) {
		/* Applicable to BM520s but not to BM820s. */
		dmm->dmm_state_init = brymen_bm52x_state_init;
		dmm->dmm_state_free = brymen_bm52x_state_free;
		dmm->config_get = brymen_bm52x_config_get;
		dmm->config_set = brymen_bm52x_config_set;
		dmm->config_list = brymen_bm52x_config_list;
		dmm->acquire_start = brymen_bm52x_acquire_start;
	}
	if (dmm->dmm_state_init)
		dmm->dmm_state = dmm->dmm_state_init();

	/* Setup the device instance. */
	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup(dmm->vendor);
	sdi->model = g_strdup(dmm->device);
	devc = g_malloc0(sizeof(*devc));
	sr_sw_limits_init(&devc->limits);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->priv = devc;

	/* Create (optionally device dependent) channel(s). */
	dmm->channel_count = 1;
	if (dmm->packet_parse == sr_brymen_bm52x_parse)
		dmm->channel_count = BRYMEN_BM52X_DISPLAY_COUNT;
	if (dmm->packet_parse == sr_brymen_bm86x_parse)
		dmm->channel_count = BRYMEN_BM86X_DISPLAY_COUNT;
	if (dmm->packet_parse == sr_eev121gw_3displays_parse) {
		dmm->channel_count = EEV121GW_DISPLAY_COUNT;
		dmm->channel_formats = eev121gw_channel_formats;
	}
	if (dmm->packet_parse == sr_metex14_4packets_parse)
		dmm->channel_count = 4;
	if (dmm->packet_parse == sr_ms2115b_parse) {
		dmm->channel_count = MS2115B_DISPLAY_COUNT;
		dmm->channel_formats = ms2115b_channel_formats;
	}
	for (ch_idx = 0; ch_idx < dmm->channel_count; ch_idx++) {
		size_t ch_num;
		const char *fmt;
		fmt = "P%zu";
		if (dmm->channel_formats && dmm->channel_formats[ch_idx])
			fmt = dmm->channel_formats[ch_idx];
		ch_num = ch_idx + 1;
		snprintf(ch_name, sizeof(ch_name), fmt, ch_num);
		sr_channel_new(sdi, ch_idx, SR_CHANNEL_ANALOG, TRUE, ch_name);
	}

	/* Add found device to result set. */
	devices = g_slist_append(devices, sdi);

scan_cleanup:
	serial_close(serial);

	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct dmm_info *dmm;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_FRAMES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	default:
		dmm = (struct dmm_info *)sdi->driver;
		if (!dmm || !dmm->config_get)
			return SR_ERR_NA;
		return dmm->config_get(dmm->dmm_state, key, data, sdi, cg);
	}
	/* UNREACH */
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct dmm_info *dmm;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	(void)cg;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_FRAMES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	default:
		dmm = (struct dmm_info *)sdi->driver;
		if (!dmm || !dmm->config_set)
			return SR_ERR_NA;
		return dmm->config_set(dmm->dmm_state, key, data, sdi, cg);
	}
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dmm_info *dmm;
	int ret;

	/* Use common logic for standard keys. */
	if (!sdi)
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);

	/*
	 * Check for device specific config_list handler. ERR N/A from
	 * that handler is non-fatal, just falls back to common logic.
	 */
	dmm = (struct dmm_info *)sdi->driver;
	if (dmm && dmm->config_list) {
		ret = dmm->config_list(dmm->dmm_state, key, data, sdi, cg);
		if (ret != SR_ERR_NA)
			return ret;
	}

	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct dmm_info *dmm;
	sr_receive_data_callback cb_func;
	void *cb_data;
	int ret;
	struct sr_serial_dev_inst *serial;

	devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	cb_func = receive_data;
	cb_data = (void *)sdi;
	dmm = (struct dmm_info *)sdi->driver;
	if (dmm && dmm->acquire_start) {
		ret = dmm->acquire_start(dmm->dmm_state, sdi,
			&cb_func, &cb_data);
		if (ret < 0)
			return ret;
	}

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 50,
		cb_func, cb_data);

	return SR_OK;
}

#define DMM_ENTRY(ID, CHIPSET, VENDOR, MODEL, \
		CONN, SERIALCOMM, PACKETSIZE, TIMEOUT, DELAY, \
		OPEN, REQUEST, VALID, PARSE, DETAILS, \
		INIT_STATE, FREE_STATE, VALID_LEN, PARSE_LEN, \
		CFG_GET, CFG_SET, CFG_LIST, ACQ_START) \
	&((struct dmm_info) { \
		{ \
			.name = ID, \
			.longname = VENDOR " " MODEL, \
			.api_version = 1, \
			.init = std_init, \
			.cleanup = std_cleanup, \
			.scan = scan, \
			.dev_list = std_dev_list, \
			.dev_clear = std_dev_clear, \
			.config_get = config_get, \
			.config_set = config_set, \
			.config_list = config_list, \
			.dev_open = std_serial_dev_open, \
			.dev_close = std_serial_dev_close, \
			.dev_acquisition_start = dev_acquisition_start, \
			.dev_acquisition_stop = std_serial_dev_acquisition_stop, \
			.context = NULL, \
		}, \
		VENDOR, MODEL, CONN, SERIALCOMM, PACKETSIZE, TIMEOUT, DELAY, \
		REQUEST, 1, NULL, VALID, PARSE, DETAILS, \
		sizeof(struct CHIPSET##_info), \
		NULL, INIT_STATE, FREE_STATE, \
		OPEN, VALID_LEN, PARSE_LEN, \
		CFG_GET, CFG_SET, CFG_LIST, ACQ_START, \
	}).di

#define DMM_CONN(ID, CHIPSET, VENDOR, MODEL, \
		CONN, SERIALCOMM, PACKETSIZE, TIMEOUT, DELAY, \
		REQUEST, VALID, PARSE, DETAILS) \
	DMM_ENTRY(ID, CHIPSET, VENDOR, MODEL, \
		CONN, SERIALCOMM, PACKETSIZE, TIMEOUT, DELAY, \
		NULL, REQUEST, VALID, PARSE, DETAILS, \
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL)

#define DMM(ID, CHIPSET, VENDOR, MODEL, SERIALCOMM, PACKETSIZE, TIMEOUT, \
		DELAY, REQUEST, VALID, PARSE, DETAILS) \
	DMM_CONN(ID, CHIPSET, VENDOR, MODEL, \
		NULL, SERIALCOMM, PACKETSIZE, TIMEOUT, DELAY, \
		REQUEST, VALID, PARSE, DETAILS)

#define DMM_LEN(ID, CHIPSET, VENDOR, MODEL, \
		CONN, SERIALCOMM, PACKETSIZE, TIMEOUT, DELAY, \
		INIT, FREE, OPEN, REQUEST, VALID, PARSE, DETAILS) \
	DMM_ENTRY(ID, CHIPSET, VENDOR, MODEL, \
		CONN, SERIALCOMM, PACKETSIZE, TIMEOUT, DELAY, \
		OPEN, REQUEST, NULL, NULL, DETAILS, \
		INIT, FREE, VALID, PARSE, NULL, NULL, NULL, NULL)

SR_REGISTER_DEV_DRIVER_LIST(serial_dmm_drivers,
	/*
	 * The items are sorted by chipset first and then model name.
	 *
	 * This reflects the developer's perspective and is preferrable
	 * during maintenance, as a vendor/product based sort order does
	 * not work well for rebranded models, and from a support point
	 * of view it's more important to identify similarities between
	 * models and compatible devices.
	 *
	 * Fold marks {{{ }}} with matching braces were added, to further
	 * speed up navigation in the long list.
	 */
	/* asycii based meters {{{ */
	DMM(
		"metrix-mx56c", asycii, "Metrix", "MX56C",
		"2400/8n1", ASYCII_PACKET_SIZE, 0, 0, NULL,
		sr_asycii_packet_valid, sr_asycii_parse, NULL
	),
	/* }}} */
	/* bm25x based meters {{{ */
	DMM(
		"brymen-bm25x", bm25x,
		"Brymen", "BM25x", "9600/8n1/rts=1/dtr=1",
		BRYMEN_BM25X_PACKET_SIZE, 0, 0, NULL,
		sr_brymen_bm25x_packet_valid, sr_brymen_bm25x_parse,
		NULL
	),
	/* }}} */
	/* bm52x based meters {{{ */
	DMM_CONN(
		"brymen-bm52x", brymen_bm52x, "Brymen", "BM52x",
		"hid/bu86x", NULL, BRYMEN_BM52X_PACKET_SIZE, 4000, 500,
		sr_brymen_bm52x_packet_request,
		sr_brymen_bm52x_packet_valid, sr_brymen_bm52x_parse,
		NULL
	),
	DMM_CONN(
		"brymen-bm82x", brymen_bm52x, "Brymen", "BM82x",
		"hid/bu86x", NULL, BRYMEN_BM52X_PACKET_SIZE, 4000, 500,
		sr_brymen_bm82x_packet_request,
		sr_brymen_bm82x_packet_valid, sr_brymen_bm52x_parse,
		NULL
	),
	/* }}} */
	/* bm85x based meters {{{ */
	DMM_LEN(
		"brymen-bm85x", brymen_bm85x, "Brymen", "BM85x",
		NULL, "9600/8n1/dtr=1/rts=1",
		BRYMEN_BM85x_PACKET_SIZE_MIN, 2000, 400,
		NULL, NULL, /* INIT/FREE for DMM state */
		brymen_bm85x_after_open, brymen_bm85x_packet_request,
		brymen_bm85x_packet_valid, brymen_bm85x_parse,
		NULL
	),
	/* }}} */
	/* bm86x based meters {{{ */
	DMM_CONN(
		"brymen-bm86x", brymen_bm86x, "Brymen", "BM86x",
		"hid/bu86x", NULL, BRYMEN_BM86X_PACKET_SIZE, 500, 100,
		sr_brymen_bm86x_packet_request,
		sr_brymen_bm86x_packet_valid, sr_brymen_bm86x_parse,
		NULL
	),
	/* }}} */
	/* dtm0660 based meters {{{ */
	DMM(
		"peaktech-3415", dtm0660,
		"PeakTech", "3415", "2400/8n1/rts=0/dtr=1",
		DTM0660_PACKET_SIZE, 0, 0, NULL,
		sr_dtm0660_packet_valid, sr_dtm0660_parse, NULL
	),
	DMM(
		"velleman-dvm4100", dtm0660,
		"Velleman", "DVM4100", "2400/8n1/rts=0/dtr=1",
		DTM0660_PACKET_SIZE, 0, 0, NULL,
		sr_dtm0660_packet_valid, sr_dtm0660_parse, NULL
	),
	/* }}} */
	/* eev121gw based meters {{{ */
	DMM(
		"eevblog-121gw", eev121gw, "EEVblog", "121GW",
		"115200/8n1", EEV121GW_PACKET_SIZE, 0, 0, NULL,
		sr_eev121gw_packet_valid, sr_eev121gw_3displays_parse, NULL
	),
	/* }}} */
	/* es519xx based meters {{{ */
	DMM(
		"iso-tech-idm103n", es519xx,
		"ISO-TECH", "IDM103N", "2400/7o1/rts=0/dtr=1",
		ES519XX_11B_PACKET_SIZE, 0, 0, NULL,
		sr_es519xx_2400_11b_packet_valid, sr_es519xx_2400_11b_parse,
		NULL
	),
	/*
	 * Note: ES51922 and ES51986 baudrate is actually 19230. This is
	 * "out" by .15%, and so is well within the typical 1% margin
	 * that is considered acceptable in UART communication, and thus
	 * should not cause an issue.
	 *
	 * However, using 19230 as baudrate here will not work, as most DMM
	 * cables do not support that baudrate!
	 */
	DMM(
		"tenma-72-7750-ser", es519xx,
		"Tenma", "72-7750 (UT-D02 cable)", "19200/7o1/rts=0/dtr=1",
		ES519XX_11B_PACKET_SIZE, 0, 0, NULL,
		sr_es519xx_19200_11b_packet_valid, sr_es519xx_19200_11b_parse,
		NULL
	),
	DMM(
		"uni-t-ut60g-ser", es519xx,
		"UNI-T", "UT60G (UT-D02 cable)", "19200/7o1/rts=0/dtr=1",
		ES519XX_11B_PACKET_SIZE, 0, 0, NULL,
		sr_es519xx_19200_11b_packet_valid, sr_es519xx_19200_11b_parse,
		NULL
	),
	DMM(
		"uni-t-ut61e-ser", es519xx,
		"UNI-T", "UT61E (UT-D02 cable)", "19200/7o1/rts=0/dtr=1",
		ES519XX_14B_PACKET_SIZE, 0, 0, NULL,
		sr_es519xx_19200_14b_packet_valid, sr_es519xx_19200_14b_parse,
		NULL
	),
	/* }}} */
	/* fs9721 based meters {{{ */
	DMM(
		"digitek-dt4000zc", fs9721,
		"Digitek", "DT4000ZC", "2400/8n1/dtr=1",
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_10_temp_c
	),
	DMM(
		"mastech-ms8250b", fs9721,
		"MASTECH", "MS8250B", "2400/8n1/rts=0/dtr=1",
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL
	),
	DMM(
		"pce-pce-dm32", fs9721,
		"PCE", "PCE-DM32", "2400/8n1",
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_01_10_temp_f_c
	),
	DMM(
		"peaktech-3330", fs9721,
		"PeakTech", "3330", "2400/8n1/dtr=1",
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_01_10_temp_f_c
	),
	DMM(
		"tecpel-dmm-8061-ser", fs9721,
		"Tecpel", "DMM-8061 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c
	),
	DMM(
		"tekpower-tp4000ZC", fs9721,
		"TekPower", "TP4000ZC", "2400/8n1/dtr=1",
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_10_temp_c
	),
	DMM(
		"tenma-72-7745-ser", fs9721,
		"Tenma", "72-7745 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c
	),
	DMM(
		"uni-t-ut60a-ser", fs9721,
		"UNI-T", "UT60A (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL
	),
	DMM(
		"uni-t-ut60e-ser", fs9721,
		"UNI-T", "UT60E (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c
	),
	DMM(
		"va-va18b", fs9721,
		"V&A", "VA18B", "2400/8n1",
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_01_temp_c
	),
	DMM(
		"va-va40b", fs9721,
		"V&A", "VA40B", "2400/8n1",
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_max_c_min
	),
	DMM(
		"voltcraft-vc820-ser", fs9721,
		"Voltcraft", "VC-820 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL
	),
	DMM(
		"voltcraft-vc840-ser", fs9721,
		"Voltcraft", "VC-840 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		FS9721_PACKET_SIZE, 0, 0, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		sr_fs9721_00_temp_c
	),
	/* }}} */
	/* fs9922 based meters {{{ */
	DMM(
		"gwinstek-gdm-397", fs9922,
		"GW Instek", "GDM-397", "2400/8n1/rts=0/dtr=1",
		FS9922_PACKET_SIZE, 0, 0, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse, NULL
	),
	DMM(
		"sparkfun-70c", fs9922,
		"SparkFun", "70C", "2400/8n1/rts=0/dtr=1",
		FS9922_PACKET_SIZE, 0, 0, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse, NULL
	),
	DMM(
		"uni-t-ut61b-ser", fs9922,
		"UNI-T", "UT61B (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		FS9922_PACKET_SIZE, 0, 0, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse, NULL
	),
	DMM(
		"uni-t-ut61c-ser", fs9922,
		"UNI-T", "UT61C (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		FS9922_PACKET_SIZE, 0, 0, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse, NULL
	),
	DMM(
		"uni-t-ut61d-ser", fs9922,
		"UNI-T", "UT61D (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		FS9922_PACKET_SIZE, 0, 0, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse, NULL
	),
	DMM_CONN(
		"victor-dmm", fs9922, "Victor", "Victor DMMs",
		"hid/victor", "2400/8n1", FS9922_PACKET_SIZE, 0, 0, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse, NULL
	),
	DMM(
		/*
		 * Note: The VC830 doesn't set the 'volt' and 'diode' bits of
		 * the FS9922 protocol. Instead, it only sets the user-defined
		 * bit "z1" to indicate "diode mode" and "voltage".
		 */
		"voltcraft-vc830-ser", fs9922,
		"Voltcraft", "VC-830 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		FS9922_PACKET_SIZE, 0, 0, NULL,
		sr_fs9922_packet_valid, sr_fs9922_parse,
		&sr_fs9922_z1_diode
	),
	/* }}} */
	/* m2110 based meters {{{ */
	DMM(
		"bbcgm-2010", m2110,
		"BBC Goertz Metrawatt", "M2110", "1200/7n2",
		BBCGM_M2110_PACKET_SIZE, 0, 0, NULL,
		sr_m2110_packet_valid, sr_m2110_parse,
		NULL
	),
	/* }}} */
	/* ms2115b based meters {{{ */
	DMM(
		"mastech-ms2115b", ms2115b,
		"MASTECH", "MS2115B", "1200/8n1",
		MS2115B_PACKET_SIZE, 0, 0, NULL,
		sr_ms2115b_packet_valid, sr_ms2115b_parse,
		NULL
	),
	/* }}} */
	/* ms8250d based meters {{{ */
	DMM(
		"mastech-ms8250d", ms8250d,
		"MASTECH", "MS8250D", "2400/8n1/rts=0/dtr=1",
		MS8250D_PACKET_SIZE, 0, 0, NULL,
		sr_ms8250d_packet_valid, sr_ms8250d_parse,
		NULL
	),
	/* }}} */
	/* metex14 based meters {{{ */
	DMM(
		"mastech-mas345", metex14,
		"MASTECH", "MAS345", "600/7n2/rts=0/dtr=1",
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"metex-m3640d", metex14,
		"Metex", "M-3640D", "1200/7n2/rts=0/dtr=1",
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"metex-m3860m", metex14,
		"Metex", "M-3860M", "9600/7n2/rts=0/dtr=1",
		4 * METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_4packets_valid, sr_metex14_4packets_parse,
		NULL
	),
	DMM(
		"metex-m4650cr", metex14,
		"Metex", "M-4650CR", "1200/7n2/rts=0/dtr=1",
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"metex-me21", metex14,
		"Metex", "ME-21", "2400/7n2/rts=0/dtr=1",
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"metex-me31", metex14,
		"Metex", "ME-31", "600/7n2/rts=0/dtr=1",
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"peaktech-3410", metex14,
		"PeakTech", "3410", "600/7n2/rts=0/dtr=1",
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"peaktech-4370", metex14,
		"PeakTech", "4370", "1200/7n2/rts=0/dtr=1",
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"peaktech-4390a", metex14,
		"PeakTech", "4390A", "9600/7n2/rts=0/dtr=1",
		4 * METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_4packets_valid, sr_metex14_4packets_parse,
		NULL
	),
	DMM(
		"radioshack-22-168", metex14,
		"RadioShack", "22-168", "1200/7n2/rts=0/dtr=1",
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"radioshack-22-805", metex14,
		"RadioShack", "22-805", "600/7n2/rts=0/dtr=1",
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"voltcraft-m3650cr", metex14,
		"Voltcraft", "M-3650CR", "1200/7n2/rts=0/dtr=1",
		METEX14_PACKET_SIZE, 150, 20, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"voltcraft-m3650d", metex14,
		"Voltcraft", "M-3650D", "1200/7n2/rts=0/dtr=1",
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"voltcraft-m4650cr", metex14,
		"Voltcraft", "M-4650CR", "1200/7n2/rts=0/dtr=1",
		METEX14_PACKET_SIZE, 0, 0, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	DMM(
		"voltcraft-me42", metex14,
		"Voltcraft", "ME-42", "600/7n2/rts=0/dtr=1",
		METEX14_PACKET_SIZE, 250, 60, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL
	),
	/* }}} */
	/* rs9lcd based meters {{{ */
	DMM(
		"radioshack-22-812", rs9lcd,
		"RadioShack", "22-812", "4800/8n1/rts=0/dtr=1",
		RS9LCD_PACKET_SIZE, 0, 0, NULL,
		sr_rs9lcd_packet_valid, sr_rs9lcd_parse,
		NULL
	),
	/* }}} */
	/* ut71x based meters {{{ */
	DMM(
		"tenma-72-7730-ser", ut71x,
		"Tenma", "72-7730 (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"tenma-72-7732-ser", ut71x,
		"Tenma", "72-7732 (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"tenma-72-9380a-ser", ut71x,
		"Tenma", "72-9380A (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut71a-ser", ut71x,
		"UNI-T", "UT71A (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut71b-ser", ut71x,
		"UNI-T", "UT71B (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut71c-ser", ut71x,
		"UNI-T", "UT71C (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut71d-ser", ut71x,
		"UNI-T", "UT71D (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut71e-ser", ut71x,
		"UNI-T", "UT71E (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"uni-t-ut804-ser", ut71x,
		"UNI-T", "UT804", "2400/7o1/rts=0/dtr=1",
		UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"voltcraft-vc920-ser", ut71x,
		"Voltcraft", "VC-920 (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"voltcraft-vc940-ser", ut71x,
		"Voltcraft", "VC-940 (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	DMM(
		"voltcraft-vc960-ser", ut71x,
		"Voltcraft", "VC-960 (UT-D02 cable)", "2400/7o1/rts=0/dtr=1",
		UT71X_PACKET_SIZE, 0, 0, NULL,
		sr_ut71x_packet_valid, sr_ut71x_parse, NULL
	),
	/* }}} */
	/* vc870 based meters {{{ */
	DMM(
		"voltcraft-vc870-ser", vc870,
		"Voltcraft", "VC-870 (UT-D02 cable)", "9600/8n1/rts=0/dtr=1",
		VC870_PACKET_SIZE, 0, 0, NULL,
		sr_vc870_packet_valid, sr_vc870_parse, NULL
	),
	/* }}} */
	/* vc96 based meters {{{ */
	DMM(
		"voltcraft-vc96", vc96,
		"Voltcraft", "VC-96", "1200/8n2",
		VC96_PACKET_SIZE, 0, 0, NULL,
		sr_vc96_packet_valid, sr_vc96_parse,
		NULL
	),
	/* }}} */
	/*
	 * The list is sorted. Add new items in the respective chip's group.
	 */
);
