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

#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

static const int hwopts[] = {
	SR_HWOPT_CONN,
	SR_HWOPT_SERIALCOMM,
	0,
};

static const int hwcaps[] = {
	SR_HWCAP_MULTIMETER,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_CONTINUOUS,
	0,
};

static const char *probe_names[] = {
	"Probe",
	NULL,
};

SR_PRIV struct sr_dev_driver digitek_dt4000zc_driver_info;
SR_PRIV struct sr_dev_driver tekpower_tp4000zc_driver_info;
SR_PRIV struct sr_dev_driver metex_me31_driver_info;
SR_PRIV struct sr_dev_driver peaktech_3410_driver_info;
SR_PRIV struct sr_dev_driver mastech_mas345_driver_info;
SR_PRIV struct sr_dev_driver va_va18b_driver_info;
SR_PRIV struct sr_dev_driver metex_m3640d_driver_info;
SR_PRIV struct sr_dev_driver peaktech_4370_driver_info;

static struct sr_dev_driver *di_dt4000zc = &digitek_dt4000zc_driver_info;
static struct sr_dev_driver *di_tp4000zc = &tekpower_tp4000zc_driver_info;
static struct sr_dev_driver *di_me31 = &metex_me31_driver_info;
static struct sr_dev_driver *di_3410 = &peaktech_3410_driver_info;
static struct sr_dev_driver *di_mas345 = &mastech_mas345_driver_info;
static struct sr_dev_driver *di_va18b = &va_va18b_driver_info;
static struct sr_dev_driver *di_m3640d = &metex_m3640d_driver_info;
static struct sr_dev_driver *di_4370 = &peaktech_4370_driver_info;

/* After hw_init() this will point to a device-specific entry (see above). */
static struct sr_dev_driver *di = NULL;

SR_PRIV struct dmm_info dmms[] = {
	{
		"Digitek", "DT4000ZC", "2400/8n1", 2400,
		FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		dmm_details_dt4000zc,
	},
	{
		"TekPower", "TP4000ZC", "2400/8n1", 2400,
		FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		dmm_details_tp4000zc,
	},
	{
		"Metex", "ME-31", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
	},
	{
		"Peaktech", "3410", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
	},
	{
		"MASTECH", "MAS345", "600/7n2/rts=0/dtr=1", 600,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
	},
	{
		"V&A", "VA18B", "2400/8n1", 2400,
		FS9721_PACKET_SIZE, NULL,
		sr_fs9721_packet_valid, sr_fs9721_parse,
		dmm_details_va18b,
	},
	{
		"Metex", "M-3640D", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
	},
	{
		"PeakTech", "4370", "1200/7n2/rts=0/dtr=1", 1200,
		METEX14_PACKET_SIZE, sr_metex14_packet_request,
		sr_metex14_packet_valid, sr_metex14_parse,
		NULL,
	},
};

/* Properly close and free all devices. */
static int clear_instances(void)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *l;

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

static int hw_init(int dmm)
{
	struct drv_context *drvc;

	if (!(drvc = g_try_malloc0(sizeof(struct drv_context)))) {
		sr_err("Driver context malloc failed.");
		return SR_ERR_MALLOC;
	}

	if (dmm == DIGITEK_DT4000ZC)
		di = di_dt4000zc;
	if (dmm == TEKPOWER_TP4000ZC)
		di = di_tp4000zc;
	if (dmm == METEX_ME31)
		di = di_me31;
	if (dmm == PEAKTECH_3410)
		di = di_3410;
	if (dmm == MASTECH_MAS345)
		di = di_mas345;
	if (dmm == VA_VA18B)
		di = di_va18b;
	if (dmm == METEX_M3640D)
		di = di_m3640d;
	if (dmm == PEAKTECH_4370)
		di = di_4370;
	sr_dbg("Selected '%s' subdriver.", di->name);

	di->priv = drvc;

	return SR_OK;
}

static int hw_init_digitek_dt4000zc(void)
{
	return hw_init(DIGITEK_DT4000ZC);
}

static int hw_init_tekpower_tp4000zc(void)
{
	return hw_init(TEKPOWER_TP4000ZC);
}

static int hw_init_metex_me31(void)
{
	return hw_init(METEX_ME31);
}

static int hw_init_peaktech_3410(void)
{
	return hw_init(PEAKTECH_3410);
}

static int hw_init_mastech_mas345(void)
{
	return hw_init(MASTECH_MAS345);
}

static int hw_init_va_va18b(void)
{
	return hw_init(VA_VA18B);
}

static int hw_init_metex_m3640d(void)
{
	return hw_init(METEX_M3640D);
}

static int hw_init_peaktech_4370(void)
{
	return hw_init(PEAKTECH_4370);
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

	sr_info("Probing port %s.", conn);

	drvc = di->priv;
	devices = NULL;
	serial_flush(serial);

	/*
	 * There's no way to get an ID from the multimeter. It just sends data
	 * periodically, so the best we can do is check if the packets match
	 * the expected format.
	 */

	/* Let's get a bit of data and see if we can find a packet. */
	len = sizeof(buf);

	/* Request a packet if the DMM requires this. */
	if (dmms[dmm].packet_request) {
		if ((ret = dmms[dmm].packet_request(serial)) < 0) {
			sr_err("Failed to request packet: %d.", ret);
			return FALSE;
		}
	}

	ret = serial_stream_detect(serial, buf, &len, dmms[dmm].packet_size,
				   dmms[dmm].packet_valid, 1000,
				   dmms[dmm].baudrate);
	if (ret != SR_OK)
		goto scan_cleanup;

	/*
	 * If we dropped more than two packets worth of data, something is
	 * wrong. We shouldn't quit however, since the dropped bytes might be
	 * just zeroes at the beginning of the stream. Those can occur as a
	 * combination of the nonstandard cable that ships with this device and
	 * the serial port or USB to serial adapter.
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
	sdi->driver = di;
	if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
		goto scan_cleanup;
	sdi->probes = g_slist_append(sdi->probes, probe);
	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

scan_cleanup:
	serial_close(serial);

	return devices;
}

static GSList *hw_scan(GSList *options)
{
	struct sr_hwopt *opt;
	GSList *l, *devices;
	const char *conn, *serialcomm;
	int dmm;

	conn = serialcomm = NULL;
	for (l = options; l; l = l->next) {
		opt = l->data;
		switch (opt->hwopt) {
		case SR_HWOPT_CONN:
			conn = opt->value;
			break;
		case SR_HWOPT_SERIALCOMM:
			serialcomm = opt->value;
			break;
		}
	}
	if (!conn)
		return NULL;

	if (!strcmp(di->name, "digitek-dt4000zc"))
		dmm = 0;
	if (!strcmp(di->name, "tekpower-tp4000zc"))
		dmm = 1;
	if (!strcmp(di->name, "metex-me31"))
		dmm = 2;
	if (!strcmp(di->name, "peaktech-3410"))
		dmm = 3;
	if (!strcmp(di->name, "mastech-mas345"))
		dmm = 4;
	if (!strcmp(di->name, "va-va18b"))
		dmm = 5;
	if (!strcmp(di->name, "metex-m3640d"))
		dmm = 6;
	if (!strcmp(di->name, "peaktech-4370"))
		dmm = 7;

	if (serialcomm) {
		/* Use the provided comm specs. */
		devices = scan(conn, serialcomm, dmm);
	} else {
		/* Try the default. */
		devices = scan(conn, dmms[dmm].conn, dmm);
	}

	return devices;
}

static GSList *hw_dev_list(void)
{
	struct drv_context *drvc;

	drvc = di->priv;

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

static int hw_cleanup(void)
{
	clear_instances();

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
	case SR_DI_NUM_PROBES:
		*data = GINT_TO_POINTER(1);
		break;
	case SR_DI_PROBE_NAMES:
		*data = probe_names;
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
	case SR_HWCAP_LIMIT_SAMPLES:
		devc->limit_samples = *(const uint64_t *)value;
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
		break;
	default:
		sr_err("Unknown capability: %d.", hwcap);
		return SR_ERR;
		break;
	}

	return SR_OK;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct sr_datafeed_meta_analog meta;
	struct dev_context *devc;
	int (*receive_data)(int, int, void *) = NULL;

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

	/* Send header packet to the session bus. */
	sr_dbg("Sending SR_DF_HEADER.");
	packet.type = SR_DF_HEADER;
	packet.payload = (uint8_t *)&header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	sr_session_send(devc->cb_data, &packet);

	/* Send metadata about the SR_DF_ANALOG packets to come. */
	sr_dbg("Sending SR_DF_META_ANALOG.");
	packet.type = SR_DF_META_ANALOG;
	packet.payload = &meta;
	meta.num_probes = 1;
	sr_session_send(devc->cb_data, &packet);

	if (!strcmp(di->name, "digitek-dt4000zc"))
		receive_data = digitek_dt4000zc_receive_data;
	if (!strcmp(di->name, "tekpower-tp4000zc"))
		receive_data = tekpower_tp4000zc_receive_data;
	if (!strcmp(di->name, "metex-me31"))
		receive_data = metex_me31_receive_data;
	if (!strcmp(di->name, "peaktech-3410"))
		receive_data = peaktech_3410_receive_data;
	if (!strcmp(di->name, "mastech-mas345"))
		receive_data = mastech_mas345_receive_data;
	if (!strcmp(di->name, "va-va18b"))
		receive_data = va_va18b_receive_data;
	if (!strcmp(di->name, "metex-m3640d"))
		receive_data = metex_m3640d_receive_data;
	if (!strcmp(di->name, "peaktech-4370"))
		receive_data = peaktech_4370_receive_data;

	/* Poll every 50ms, or whenever some data comes in. */
	sr_source_add(devc->serial->fd, G_IO_IN, 50,
		      receive_data, (void *)sdi);

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

SR_PRIV struct sr_dev_driver digitek_dt4000zc_driver_info = {
	.name = "digitek-dt4000zc",
	.longname = "Digitek DT4000ZC",
	.api_version = 1,
	.init = hw_init_digitek_dt4000zc,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};

SR_PRIV struct sr_dev_driver tekpower_tp4000zc_driver_info = {
	.name = "tekpower-tp4000zc",
	.longname = "TekPower TP4000ZC",
	.api_version = 1,
	.init = hw_init_tekpower_tp4000zc,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};

SR_PRIV struct sr_dev_driver metex_me31_driver_info = {
	.name = "metex-me31",
	.longname = "Metex ME-31",
	.api_version = 1,
	.init = hw_init_metex_me31,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};

SR_PRIV struct sr_dev_driver peaktech_3410_driver_info = {
	.name = "peaktech-3410",
	.longname = "PeakTech 3410",
	.api_version = 1,
	.init = hw_init_peaktech_3410,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};

SR_PRIV struct sr_dev_driver mastech_mas345_driver_info = {
	.name = "mastech-mas345",
	.longname = "MASTECH MAS345",
	.api_version = 1,
	.init = hw_init_mastech_mas345,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};

SR_PRIV struct sr_dev_driver va_va18b_driver_info = {
	.name = "va-va18b",
	.longname = "V&A VA18B",
	.api_version = 1,
	.init = hw_init_va_va18b,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};

SR_PRIV struct sr_dev_driver metex_m3640d_driver_info = {
	.name = "metex-m3640d",
	.longname = "Metex M-3640D",
	.api_version = 1,
	.init = hw_init_metex_m3640d,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};

SR_PRIV struct sr_dev_driver peaktech_4370_driver_info = {
	.name = "peaktech-4370",
	.longname = "PeakTech 4370",
	.api_version = 1,
	.init = hw_init_peaktech_4370,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
