/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Aurelien Jacobs <aurel@gnuage.org>
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

#include <stdlib.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

static const int32_t hwopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const int32_t hwcaps[] = {
	SR_CONF_ENERGYMETER,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_CONTINUOUS,
};

SR_PRIV struct sr_dev_driver teleinfo_driver_info;
static struct sr_dev_driver *di = &teleinfo_driver_info;

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct sr_dev_inst *sdi;
	struct sr_probe *probe;
	GSList *devices = NULL, *l;
	const char *conn = NULL, *serialcomm = NULL;
	uint8_t buf[292];
	size_t len;
	struct sr_config *src;

	len = sizeof(buf);

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
		serialcomm = "1200/7e1";

	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;
	if (serial_open(serial, SERIAL_RDONLY | SERIAL_NONBLOCK) != SR_OK)
		return NULL;

	sr_info("Probing serial port %s.", conn);

	drvc = di->priv;
	drvc->instances = NULL;
	serial_flush(serial);

	/* Let's get a bit of data and see if we can find a packet. */
	if (serial_stream_detect(serial, buf, &len, len,
	                         teleinfo_packet_valid, 3000, 1200) != SR_OK)
		goto scan_cleanup;

	sr_info("Found device on port %s.", conn);

	if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, "EDF", "Teleinfo", "")))
		goto scan_cleanup;

	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		goto scan_cleanup;
	}

	devc->optarif = teleinfo_get_optarif(buf);

	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->priv = devc;
	sdi->driver = di;

	if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P")))
		goto scan_cleanup;
	sdi->probes = g_slist_append(sdi->probes, probe);

	if (devc->optarif == OPTARIF_BASE) {
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "BASE")))
			goto scan_cleanup;
		sdi->probes = g_slist_append(sdi->probes, probe);
	} else if (devc->optarif == OPTARIF_HC) {
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "HP")))
			goto scan_cleanup;
		sdi->probes = g_slist_append(sdi->probes, probe);
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "HC")))
			goto scan_cleanup;
		sdi->probes = g_slist_append(sdi->probes, probe);
	} else if (devc->optarif == OPTARIF_EJP) {
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "HN")))
			goto scan_cleanup;
		sdi->probes = g_slist_append(sdi->probes, probe);
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "HPM")))
			goto scan_cleanup;
		sdi->probes = g_slist_append(sdi->probes, probe);
	} else if (devc->optarif == OPTARIF_BBR) {
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "HPJB")))
			goto scan_cleanup;
		sdi->probes = g_slist_append(sdi->probes, probe);
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "HPJW")))
			goto scan_cleanup;
		sdi->probes = g_slist_append(sdi->probes, probe);
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "HPJR")))
			goto scan_cleanup;
		sdi->probes = g_slist_append(sdi->probes, probe);
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "HCJB")))
			goto scan_cleanup;
		sdi->probes = g_slist_append(sdi->probes, probe);
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "HCJW")))
			goto scan_cleanup;
		sdi->probes = g_slist_append(sdi->probes, probe);
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "HCJR")))
			goto scan_cleanup;
		sdi->probes = g_slist_append(sdi->probes, probe);
	}

	if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "IINST")))
		goto scan_cleanup;
	sdi->probes = g_slist_append(sdi->probes, probe);

	if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "PAPP")))
		goto scan_cleanup;
	sdi->probes = g_slist_append(sdi->probes, probe);

	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

scan_cleanup:
	serial_close(serial);

	return devices;
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int cleanup(void)
{
	return std_dev_clear(di, NULL);
}

static int config_set(int key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	struct dev_context *devc;

	(void)probe_group;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		sr_dbg("Setting sample limit to %" PRIu64 ".", devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = g_variant_get_uint64(data);
		sr_dbg("Setting time limit to %" PRIu64 "ms.", devc->limit_msec);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	(void)sdi;
	(void)probe_group;

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

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_serial_dev_inst *serial = sdi->conn;
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	devc->session_cb_data = cb_data;

	/*
	 * Reset the number of samples to take. If we've already collected our
	 * quota, but we start a new session, and don't reset this, we'll just
	 * quit without acquiring any new samples.
	 */
	devc->num_samples = 0;
	devc->start_time = g_get_monotonic_time();

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Poll every 50ms, or whenever some data comes in. */
	serial_source_add(serial, G_IO_IN, 50, teleinfo_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	return std_serial_dev_acquisition_stop(sdi, cb_data,
			std_serial_dev_close, sdi->conn, LOG_PREFIX);
}

SR_PRIV struct sr_dev_driver teleinfo_driver_info = {
	.name = "teleinfo",
	.longname = "Teleinfo",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = NULL,
	.config_get = NULL,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
