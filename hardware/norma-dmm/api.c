/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Matthias Heidbrink <m-sigrok@heidbrink.biz>
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

#define BUF_MAX 50

#define SERIALCOMM "4800/8n1/dtr=1/rts=0/flow=1"

SR_PRIV struct sr_dev_driver norma_dmm_driver_info;
SR_PRIV struct sr_dev_driver siemens_b102x_driver_info;

static const char* get_brandstr(struct sr_dev_driver* drv)
{
	if (drv == &norma_dmm_driver_info)
		return "Norma";
	else
		return "Siemens";
}

static const char* get_typestr(int type, struct sr_dev_driver* drv)
{
	static const char* nameref[5][2] = {
		{"DM910", "B1024"},
		{"DM920", "B1025"},
		{"DM930", "B1026"},
		{"DM940", "B1027"},
		{"DM950", "B1028"}};

	if ((type < 1) || (type > 5))
		return "Unknown type!";

	return nameref[type-1][(drv == &siemens_b102x_driver_info)];
}

static int init_norma_dmm(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, &norma_dmm_driver_info, LOG_PREFIX);
}

static int init_siemens_b102x(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, &siemens_b102x_driver_info, LOG_PREFIX);
}

static GSList *do_scan(struct sr_dev_driver* drv, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_channel *ch;
	struct sr_serial_dev_inst *serial;
	GSList *l, *devices;
	int len, cnt;
	const char *conn, *serialcomm;
	char *buf;
	char req[10];
	int auxtype;

	devices = NULL;
	drvc = drv->priv;
	drvc->instances = NULL;
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

	if (!(buf = g_try_malloc(BUF_MAX))) {
		sr_err("Serial buffer malloc failed.");
		return NULL;
	}

	snprintf(req, sizeof(req), "%s\r\n",
		 nmadmm_requests[NMADMM_REQ_IDN].req_str);
	for (cnt = 0; cnt < 7; cnt++) {
		if (serial_write(serial, req, strlen(req)) == -1) {
			sr_err("Unable to send identification request: %d %s.",
			       errno, strerror(errno));
			return NULL;
		}
		len = BUF_MAX;
		serial_readline(serial, &buf, &len, 1500);
		if (!len)
			continue;
		buf[BUF_MAX - 1] = '\0';

		/* Match ID string, e.g. "1834 065 V1.06,IF V1.02" (DM950) */
		if (g_regex_match_simple("^1834 [^,]*,IF V*", (char *)buf, 0, 0)) {
			auxtype = xgittoint(buf[7]);
			sr_spew("%s %s DMM %s detected!", get_brandstr(drv), get_typestr(auxtype, drv), buf + 9);

			if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE,
						get_brandstr(drv), get_typestr(auxtype, drv), buf + 9)))
				return NULL;
			if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
				sr_err("Device context malloc failed.");
				return NULL;
			}
			devc->type = auxtype;
			devc->version = g_strdup(&buf[9]);
			devc->elapsed_msec = g_timer_new();

			sdi->conn = serial;
			sdi->priv = devc;
			sdi->driver = drv;
			if (!(ch = sr_channel_new(0, SR_CHANNEL_ANALOG, TRUE,
				"P1")))
				return NULL;
			sdi->channels = g_slist_append(sdi->channels, ch);
			drvc->instances = g_slist_append(drvc->instances, sdi);
			devices = g_slist_append(devices, sdi);
			break;
		}

		/*
		 * The interface of the DM9x0 contains a cap that needs to
		 * charge for up to 10s before the interface works, if not
		 * powered externally. Therefore wait a little to improve
		 * chances.
		 */
		if (cnt == 3) {
			sr_info("Waiting 5s to allow interface to settle.");
			g_usleep(5 * 1000 * 1000);
		}
	}

	g_free(buf);

	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return devices;
}

static GSList *scan_norma_dmm(GSList *options)
{
	return do_scan(&norma_dmm_driver_info, options);
}

static GSList *scan_siemens_b102x(GSList *options)
{
	return do_scan(&siemens_b102x_driver_info, options);
}

static GSList *dev_list_norma_dmm(void)
{
	return ((struct drv_context *)(norma_dmm_driver_info.priv))->instances;
}

static GSList *dev_list_siemens_b102x(void)
{
	return ((struct drv_context *)(siemens_b102x_driver_info.priv))->instances;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	std_serial_dev_close(sdi);

	/* Free dynamically allocated resources. */
	if ((devc = sdi->priv) && devc->version) {
		g_free(devc->version);
		devc->version = NULL;
		g_timer_destroy(devc->elapsed_msec);
	}

	return SR_OK;
}

static int cleanup_norma_dmm(void)
{
	return std_dev_clear(&norma_dmm_driver_info, NULL);
}

static int cleanup_siemens_b102x(void)
{
	return std_dev_clear(&siemens_b102x_driver_info, NULL);
}

static int config_set(int key, GVariant *data, const struct sr_dev_inst *sdi,
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
	case SR_CONF_LIMIT_MSEC:
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
				    void *cb_data)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (!sdi || !cb_data || !(devc = sdi->priv))
		return SR_ERR_BUG;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc->cb_data = cb_data;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Start timer, if required. */
	if (devc->limit_msec)
		g_timer_start(devc->elapsed_msec);

	/* Poll every 100ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(serial, G_IO_IN, 100, norma_dmm_receive_data,
		      (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;

	/* Stop timer, if required. */
	if (sdi && (devc = sdi->priv) && devc->limit_msec)
		g_timer_stop(devc->elapsed_msec);

	return std_serial_dev_acquisition_stop(sdi, cb_data, dev_close,
			sdi->conn, LOG_PREFIX);
}

SR_PRIV struct sr_dev_driver norma_dmm_driver_info = {
	.name = "norma-dmm",
	.longname = "Norma DM9x0 DMMs",
	.api_version = 1,
	.init = init_norma_dmm,
	.cleanup = cleanup_norma_dmm,
	.scan = scan_norma_dmm,
	.dev_list = dev_list_norma_dmm,
	.dev_clear = NULL,
	.config_get = NULL,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};


SR_PRIV struct sr_dev_driver siemens_b102x_driver_info = {
	.name = "siemens-b102x",
	.longname = "Siemens B102x DMMs",
	.api_version = 1,
	.init = init_siemens_b102x,
	.cleanup = cleanup_siemens_b102x,
	.scan = scan_siemens_b102x,
	.dev_list = dev_list_siemens_b102x,
	.dev_clear = NULL,
	.config_get = NULL,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
