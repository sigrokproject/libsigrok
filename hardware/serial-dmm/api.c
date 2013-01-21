/*
 * This file is part of the sigrok project.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

static const int hwopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
	0,
};

static const int hwcaps[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_CONTINUOUS,
	0,
};

SR_PRIV struct sr_dev_driver digitek_dt4000zc_driver_info;
SR_PRIV struct sr_dev_driver tekpower_tp4000zc_driver_info;
SR_PRIV struct sr_dev_driver metex_me31_driver_info;
SR_PRIV struct sr_dev_driver peaktech_3410_driver_info;
SR_PRIV struct sr_dev_driver mastech_mas345_driver_info;
SR_PRIV struct sr_dev_driver va_va18b_driver_info;
SR_PRIV struct sr_dev_driver metex_m3640d_driver_info;
SR_PRIV struct sr_dev_driver peaktech_4370_driver_info;
SR_PRIV struct sr_dev_driver pce_pce_dm32_driver_info;
SR_PRIV struct sr_dev_driver radioshack_22_168_driver_info;
SR_PRIV struct sr_dev_driver radioshack_22_812_driver_info;
SR_PRIV struct sr_dev_driver voltcraft_vc820_ser_driver_info;
SR_PRIV struct sr_dev_driver voltcraft_vc840_ser_driver_info;
SR_PRIV struct sr_dev_driver uni_t_ut61e_ser_driver_info;

SR_PRIV struct dmm_info dmms[] = {
	{
		"Digitek", "DT4000ZC", "2400/8n1", 2400,
		FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		dmm_details_dt4000zc,
		&digitek_dt4000zc_driver_info, receive_data_DIGITEK_DT4000ZC,
	},
	{
		"TekPower", "TP4000ZC", "2400/8n1", 2400,
		FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		dmm_details_tp4000zc,
		&tekpower_tp4000zc_driver_info, receive_data_TEKPOWER_TP4000ZC,
	},
	{
		"Metex", "ME-31", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&metex_me31_driver_info, receive_data_METEX_ME31,
	},
	{
		"Peaktech", "3410", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&peaktech_3410_driver_info, receive_data_PEAKTECH_3410,
	},
	{
		"MASTECH", "MAS345", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&mastech_mas345_driver_info, receive_data_MASTECH_MAS345,
	},
	{
		"V&A", "VA18B", "2400/8n1", 2400,
		FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		dmm_details_va18b,
		&va_va18b_driver_info, receive_data_VA_VA18B,
	},
	{
		"Metex", "M-3640D", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&metex_m3640d_driver_info, receive_data_METEX_M3640D,
	},
	{
		"PeakTech", "4370", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&peaktech_4370_driver_info, receive_data_PEAKTECH_4370,
	},
	{
		"PCE", "PCE-DM32", "2400/8n1", 2400,
		FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		dmm_details_pce_dm32,
		&pce_pce_dm32_driver_info, receive_data_PCE_PCE_DM32,
	},
	{
		"RadioShack", "22-168", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
		&radioshack_22_168_driver_info, receive_data_RADIOSHACK_22_168,
	},
	{
		"RadioShack", "22-812", "4800/8n1/rts=0/dtr=1", 4800,
		RS9LCD_PACKET_SIZE, NULL,
		sr_rs9lcd_packet_valid, sr_rs9lcd_parse,
		NULL,
		&radioshack_22_812_driver_info, receive_data_RADIOSHACK_22_812,
	},
	{
		"Voltcraft", "VC-820 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL,
		&voltcraft_vc820_ser_driver_info,
		receive_data_VOLTCRAFT_VC820_SER,
	},
	{
		"Voltcraft", "VC-840 (UT-D02 cable)", "2400/8n1/rts=0/dtr=1",
		2400, FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		NULL,
		&voltcraft_vc840_ser_driver_info,
		receive_data_VOLTCRAFT_VC840_SER,
	},
	{
		/* Note: ES51922 baudrate is actually 19230! */
		"UNI-T", "UT61E (UT-D02 cable)", "19200/7o1/rts=0/dtr=1",
		19200, ES51922_PACKET_SIZE, NULL,
		sr_es51922_packet_valid, sr_es51922_parse, NULL,
		&uni_t_ut61e_ser_driver_info, receive_data_UNI_T_UT61E_SER,
	},
};

/* Properly close and free all devices. */
static int clear_instances(int dmm)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *l;
	struct sr_dev_driver *di;

	di = dmms[dmm].di;

	if (!(drvc = di->priv))
		return SR_OK;

	drvc = di->priv;
	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data))
			continue;
		if (!(devc = sdi->priv))
			continue;
		sr_serial_dev_inst_free(devc->serial);
		sr_dev_inst_free(sdi);
	}
	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
}

static int hw_init(struct sr_context *sr_ctx, int dmm)
{
	struct drv_context *drvc;

	if (!(drvc = g_try_malloc0(sizeof(struct drv_context)))) {
		sr_err("Driver context malloc failed.");
		return SR_ERR_MALLOC;
	}

	sr_dbg("Selected '%s' subdriver.", dmms[dmm].di->name);

	drvc->sr_ctx = sr_ctx;
	dmms[dmm].di->priv = drvc;

	return SR_OK;
}

static GSList *scan(const char *conn, const char *serialcomm, int dmm)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_probe *probe;
	struct sr_serial_dev_inst *serial;
	GSList *devices;
	int dropped, ret;
	size_t len;
	uint8_t buf[128];

	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

	if (serial_open(serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		return NULL;

	sr_info("Probing serial port %s.", conn);

	drvc = dmms[dmm].di->priv;
	devices = NULL;
	serial_flush(serial);

	/* Request a packet if the DMM requires this. */
	if (dmms[dmm].packet_request) {
		if ((ret = dmms[dmm].packet_request(serial)) < 0) {
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
	ret = serial_stream_detect(serial, buf, &len, dmms[dmm].packet_size,
				   dmms[dmm].packet_valid, 1000,
				   dmms[dmm].baudrate);
	if (ret != SR_OK)
		goto scan_cleanup;

	/*
	 * If we dropped more than two packets worth of data, something is
	 * wrong. We shouldn't quit however, since the dropped bytes might be
	 * just zeroes at the beginning of the stream. Those can occur as a
	 * combination of the nonstandard cable that ships with some devices
	 * and the serial port or USB to serial adapter.
	 */
	dropped = len - dmms[dmm].packet_size;
	if (dropped > 2 * dmms[dmm].packet_size)
		sr_warn("Had to drop too much data.");

	sr_info("Found device on port %s.", conn);

	if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, dmms[dmm].vendor,
				    dmms[dmm].device, "")))
		goto scan_cleanup;

	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		goto scan_cleanup;
	}

	devc->serial = serial;

	sdi->priv = devc;
	sdi->driver = dmms[dmm].di;
	if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
		goto scan_cleanup;
	sdi->probes = g_slist_append(sdi->probes, probe);
	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

scan_cleanup:
	serial_close(serial);

	return devices;
}

static GSList *hw_scan(GSList *options, int dmm)
{
	struct sr_config *src;
	GSList *l, *devices;
	const char *conn, *serialcomm;

	conn = serialcomm = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = src->value;
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = src->value;
			break;
		}
	}
	if (!conn)
		return NULL;

	if (serialcomm) {
		/* Use the provided comm specs. */
		devices = scan(conn, serialcomm, dmm);
	} else {
		/* Try the default. */
		devices = scan(conn, dmms[dmm].conn, dmm);
	}

	return devices;
}

static GSList *hw_dev_list(int dmm)
{
	struct drv_context *drvc;

	drvc = dmms[dmm].di->priv;

	return drvc->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	if (serial_open(devc->serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	if (devc->serial && devc->serial->fd != -1) {
		serial_close(devc->serial);
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

static int hw_cleanup(int dmm)
{
	clear_instances(dmm);

	return SR_OK;
}

static int hw_info_get(int info_id, const void **data,
		       const struct sr_dev_inst *sdi)
{
	(void)sdi;

	switch (info_id) {
	case SR_DI_HWOPTS:
		*data = hwopts;
		break;
	case SR_DI_HWCAPS:
		*data = hwcaps;
		break;
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
			     const void *value)
{
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	switch (hwcap) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = *(const uint64_t *)value;
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = *(const uint64_t *)value;
		sr_dbg("Setting time limit to %" PRIu64 "ms.",
		       devc->limit_msec);
		break;
	default:
		sr_err("Unknown capability: %d.", hwcap);
		return SR_ERR;
		break;
	}

	return SR_OK;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data, int dmm)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct dev_context *devc;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	sr_dbg("Starting acquisition.");

	devc->cb_data = cb_data;

	/*
	 * Reset the number of samples to take. If we've already collected our
	 * quota, but we start a new session, and don't reset this, we'll just
	 * quit without acquiring any new samples.
	 */
	devc->num_samples = 0;
	devc->starttime = g_get_monotonic_time();

	/* Send header packet to the session bus. */
	sr_dbg("Sending SR_DF_HEADER.");
	packet.type = SR_DF_HEADER;
	packet.payload = (uint8_t *)&header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	sr_session_send(devc->cb_data, &packet);

	/* Poll every 50ms, or whenever some data comes in. */
	sr_source_add(devc->serial->fd, G_IO_IN, 50,
		      dmms[dmm].receive_data, (void *)sdi);

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	sr_dbg("Stopping acquisition.");

	sr_source_remove(devc->serial->fd);
	hw_dev_close((struct sr_dev_inst *)sdi);

	/* Send end packet to the session bus. */
	sr_dbg("Sending SR_DF_END.");
	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	return SR_OK;
}

/* Driver-specific API function wrappers */
#define HW_INIT(X) \
static int hw_init_##X(struct sr_context *sr_ctx) { return hw_init(sr_ctx, X); }
#define HW_CLEANUP(X) \
static int hw_cleanup_##X(void) { return hw_cleanup(X); }
#define HW_SCAN(X) \
static GSList *hw_scan_##X(GSList *options) { return hw_scan(options, X); }
#define HW_DEV_LIST(X) \
static GSList *hw_dev_list_##X(void) { return hw_dev_list(X); }
#define CLEAR_INSTANCES(X) \
static int clear_instances_##X(void) { return clear_instances(X); }
#define HW_DEV_ACQUISITION_START(X) \
static int hw_dev_acquisition_start_##X(const struct sr_dev_inst *sdi, \
void *cb_data) { return hw_dev_acquisition_start(sdi, cb_data, X); }

/* Driver structs and API function wrappers */
#define DRV(ID, ID_UPPER, NAME, LONGNAME) \
HW_INIT(ID_UPPER) \
HW_CLEANUP(ID_UPPER) \
HW_SCAN(ID_UPPER) \
HW_DEV_LIST(ID_UPPER) \
CLEAR_INSTANCES(ID_UPPER) \
HW_DEV_ACQUISITION_START(ID_UPPER) \
SR_PRIV struct sr_dev_driver ID##_driver_info = { \
	.name = NAME, \
	.longname = LONGNAME, \
	.api_version = 1, \
	.init = hw_init_##ID_UPPER, \
	.cleanup = hw_cleanup_##ID_UPPER, \
	.scan = hw_scan_##ID_UPPER, \
	.dev_list = hw_dev_list_##ID_UPPER, \
	.dev_clear = clear_instances_##ID_UPPER, \
	.dev_open = hw_dev_open, \
	.dev_close = hw_dev_close, \
	.info_get = hw_info_get, \
	.dev_config_set = hw_dev_config_set, \
	.dev_acquisition_start = hw_dev_acquisition_start_##ID_UPPER, \
	.dev_acquisition_stop = hw_dev_acquisition_stop, \
	.priv = NULL, \
};

DRV(digitek_dt4000zc, DIGITEK_DT4000ZC, "digitek-dt4000zc", "Digitek DT4000ZC")
DRV(tekpower_tp4000zc, TEKPOWER_TP4000ZC, "tekpower-tp4000zc", "TekPower TP4000ZC")
DRV(metex_me31, METEX_ME31, "metex-me31", "Metex ME-31")
DRV(peaktech_3410, PEAKTECH_3410, "peaktech-3410", "PeakTech 3410")
DRV(mastech_mas345, MASTECH_MAS345, "mastech-mas345", "MASTECH MAS345")
DRV(va_va18b, VA_VA18B, "va-va18b", "V&A VA18B")
DRV(metex_m3640d, METEX_M3640D, "metex-m3640d", "Metex M-3640D")
DRV(peaktech_4370, PEAKTECH_4370, "peaktech-4370", "PeakTech 4370")
DRV(pce_pce_dm32, PCE_PCE_DM32, "pce-pce-dm32", "PCE PCE-DM32")
DRV(radioshack_22_168, RADIOSHACK_22_168, "radioshack-22-168", "RadioShack 22-168")
DRV(radioshack_22_812, RADIOSHACK_22_812, "radioshack-22-812", "RadioShack 22-812")
DRV(voltcraft_vc820_ser, VOLTCRAFT_VC820_SER, "voltcraft-vc820-ser", "Voltcraft VC-820 (UT-D02 cable)")
DRV(voltcraft_vc840_ser, VOLTCRAFT_VC840_SER, "voltcraft-vc840-ser", "Voltcraft VC-840 (UT-D02 cable)")
DRV(uni_t_ut61e_ser, UNI_T_UT61E_SER, "uni-t-ut61e-ser", "UNI-T UT61E (UT-D02 cable)")
