/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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
#include "agilent-dmm.h"

static const int32_t hwopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const int32_t hwcaps[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_CONTINUOUS,
};

extern const struct agdmm_job agdmm_jobs_u123x[];
extern const struct agdmm_recv agdmm_recvs_u123x[];
extern const struct agdmm_job agdmm_jobs_u125x[];
extern const struct agdmm_recv agdmm_recvs_u125x[];

/* This works on all the Agilent U12xxA series, although the
 * U127xA can apparently also run at 19200/8n1. */
#define SERIALCOMM "9600/8n1"

static const struct agdmm_profile supported_agdmm[] = {
	{ AGILENT_U1231A, "U1231A", agdmm_jobs_u123x, agdmm_recvs_u123x },
	{ AGILENT_U1232A, "U1232A", agdmm_jobs_u123x, agdmm_recvs_u123x },
	{ AGILENT_U1233A, "U1233A", agdmm_jobs_u123x, agdmm_recvs_u123x },
	{ AGILENT_U1251A, "U1251A", agdmm_jobs_u125x, agdmm_recvs_u125x },
	{ AGILENT_U1252A, "U1252A", agdmm_jobs_u125x, agdmm_recvs_u125x },
	{ AGILENT_U1253A, "U1253A", agdmm_jobs_u125x, agdmm_recvs_u125x },
	{ 0, NULL, NULL, NULL }
};

SR_PRIV struct sr_dev_driver agdmm_driver_info;
static struct sr_dev_driver *di = &agdmm_driver_info;

/* Properly close and free all devices. */
static int clear_instances(void)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	GSList *l;

	if (!(drvc = di->priv))
		return SR_OK;

	drvc = di->priv;
	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data))
			continue;
		if (!(devc = sdi->priv))
			continue;
		serial = sdi->conn;
		sr_serial_dev_inst_free(serial);
		sr_dev_inst_free(sdi);
	}
	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
}

static int hw_init(struct sr_context *sr_ctx)
{
	return std_hw_init(sr_ctx, di, DRIVER_LOG_DOMAIN);
}

static GSList *hw_scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_probe *probe;
	struct sr_serial_dev_inst *serial;
	GSList *l, *devices;
	int len, i;
	const char *conn, *serialcomm;
	char *buf, **tokens;

	drvc = di->priv;
	drvc->instances = NULL;

	devices = NULL;
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
		serialcomm = SERIALCOMM;

	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

	if (serial_open(serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		return NULL;

	serial_flush(serial);
	if (serial_write(serial, "*IDN?\r\n", 7) == -1) {
		sr_err("Unable to send identification string: %s.",
		       strerror(errno));
		return NULL;
	}

	len = 128;
	if (!(buf = g_try_malloc(len))) {
		sr_err("Serial buffer malloc failed.");
		return NULL;
	}
	serial_readline(serial, &buf, &len, 150);
	if (!len)
		return NULL;

	tokens = g_strsplit(buf, ",", 4);
	if (!strcmp("Agilent Technologies", tokens[0])
			&& tokens[2] && tokens[3]) {
		for (i = 0; supported_agdmm[i].model; i++) {
			if (strcmp(supported_agdmm[i].modelname, tokens[1]))
				continue;
			if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, tokens[0],
					tokens[1], tokens[3])))
				return NULL;
			if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
				sr_err("Device context malloc failed.");
				return NULL;
			}
			devc->profile = &supported_agdmm[i];
			devc->cur_mq = -1;
			sdi->inst_type = SR_INST_SERIAL;
			sdi->conn = serial;
			sdi->priv = devc;
			sdi->driver = di;
			if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
				return NULL;
			sdi->probes = g_slist_append(sdi->probes, probe);
			drvc->instances = g_slist_append(drvc->instances, sdi);
			devices = g_slist_append(devices, sdi);
			break;
		}
	}
	g_strfreev(tokens);
	g_free(buf);

	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return devices;
}

static GSList *hw_dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;
	if (serial_open(serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;
	if (serial && serial->fd != -1) {
		serial_close(serial);
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

static int hw_cleanup(void)
{

	clear_instances();

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	switch (id) {
	case SR_CONF_LIMIT_MSEC:
		/* TODO: not yet implemented */
		if (g_variant_get_uint64(data) == 0) {
			sr_err("LIMIT_MSEC can't be 0.");
			return SR_ERR;
		}
		devc->limit_msec = g_variant_get_uint64(data);
		sr_dbg("Setting time limit to %" PRIu64 "ms.",
		       devc->limit_msec);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi)
{

	(void)sdi;

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

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	devc->cb_data = cb_data;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, DRIVER_LOG_DOMAIN);

	/* Poll every 100ms, or whenever some data comes in. */
	serial = sdi->conn;
	sr_source_add(serial->fd, G_IO_IN, 100, agdmm_receive_data, (void *)sdi);

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	return std_hw_dev_acquisition_stop_serial(sdi, cb_data, hw_dev_close,
			sdi->conn, DRIVER_LOG_DOMAIN);
}

SR_PRIV struct sr_dev_driver agdmm_driver_info = {
	.name = "agilent-dmm",
	.longname = "Agilent U12xx series DMMs",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.config_get = NULL,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
