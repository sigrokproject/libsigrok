/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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

#include "protocol.h"

static const int hwopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
	0,
};

static const int hwcaps[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_MSEC,
	0,
};

SR_PRIV struct sr_dev_driver brymen_bm857_driver_info;
static struct sr_dev_driver *di = &brymen_bm857_driver_info;

static int hw_init(struct sr_context *sr_ctx)
{
	return std_hw_init(sr_ctx, di, DRIVER_LOG_DOMAIN);
}

static void free_instance(void *inst)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	if (!(sdi = inst))
		return;
	if (!(devc = sdi->priv))
		return;
	sr_serial_dev_inst_free(devc->serial);
	sr_dev_inst_free(sdi);
}

/* Properly close and free all devices. */
static int clear_instances(void)
{
	struct drv_context *drvc;

	if (!(drvc = di->priv))
		return SR_OK;

	g_slist_free_full(drvc->instances, free_instance);
	drvc->instances = NULL;

	return SR_OK;
}

static GSList *brymen_scan(const char *conn, const char *serialcomm)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_probe *probe;
	struct sr_serial_dev_inst *serial;
	GSList *devices;
	int ret;
	uint8_t buf[128];
	size_t len;

	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

	if (serial_open(serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		return NULL;

	sr_info("Probing port %s.", conn);

	devices = NULL;

	/* Request reading */
	if ((ret = brymen_packet_request(serial)) < 0) {
		sr_err("Unable to send command: %d.", ret);
		goto scan_cleanup;
	}

	len = 128;
	ret = brymen_stream_detect(serial, buf, &len, brymen_packet_length,
			     brymen_packet_is_valid, 1000, 9600);
	if (ret != SR_OK)
		goto scan_cleanup;

	sr_info("Found device on port %s.", conn);

	if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, "Brymen", "BM85x", "")))
		goto scan_cleanup;

	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		goto scan_cleanup;
	}

	devc->serial = serial;
	drvc = di->priv;
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
	struct drv_context *drvc;
	struct sr_config *src;
	GSList *devices, *l;
	const char *conn, *serialcomm;

	(void)options;

	devices = NULL;
	drvc = di->priv;
	drvc->instances = NULL;

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
	if (!conn) {
		return NULL;
	}

	if (serialcomm) {
		/* Use the provided comm specs. */
		devices = brymen_scan(conn, serialcomm);
	} else {
		/* But 9600/8n1 should work all of the time. */
		devices = brymen_scan(conn, "9600/8n1/dtr=1/rts=1");
	}

	return devices;
}

static GSList *hw_dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
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

	devc = sdi->priv;

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

static int config_list(int key, const void **data,
		       const struct sr_dev_inst *sdi)
{
	(void)sdi;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = hwopts;
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = hwcaps;
		break;
	default:
		sr_err("Unknown config key: %d.", key);
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_config_set(int id, const void *value,
			     const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't set config options.");
		return SR_ERR;
	}

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	ret = SR_OK;
	switch (id) {
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = *(const uint64_t *)value;
		break;
	case SR_CONF_LIMIT_MSEC:
		devc->limit_msec = *(const uint64_t *)value;
		break;
	default:
		sr_err("Unknown hardware capability: %d.", id);
		ret = SR_ERR_ARG;
	}

	return ret;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	devc->cb_data = cb_data;

	/*
	 * Reset the number of samples to take. If we've already collected our
	 * quota, but we start a new session, and don't reset this, we'll just
	 * quit without acquiring any new samples.
	 */
	devc->num_samples = 0;
	devc->starttime = g_get_monotonic_time();

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, DRIVER_LOG_DOMAIN);

	/* Poll every 50ms, or whenever some data comes in. */
	sr_source_add(devc->serial->fd, G_IO_IN, 50,
		      brymen_dmm_receive_data, (void *)sdi);

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't stop acquisition.");
		return SR_ERR;
	}

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

SR_PRIV struct sr_dev_driver brymen_bm857_driver_info = {
	.name = "brymen-bm857",
	.longname = "Brymen BM857",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.config_list = config_list,
	.config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
