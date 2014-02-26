/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013, 2014 Matthias Heidbrink <m-sigrok@heidbrink.biz>
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

/** @file
 *  Gossen Metrawatt Metrahit 1x/2x drivers
 *  @internal
 */

#include <string.h>
#include "protocol.h"

/* Serial communication parameters for Metrahit 1x/2x with 'RS232' adaptor */
#define SERIALCOMM_1X_RS232 "8228/6n1/dtr=1/rts=1/flow=0" /* =8192, closer with divider */
#define SERIALCOMM_2X_RS232 "9600/6n1/dtr=1/rts=1/flow=0"
#define SERIALCOMM_2X "9600/8n1/dtr=1/rts=1/flow=0"
#define VENDOR_GMC "Gossen Metrawatt"

SR_PRIV struct sr_dev_driver gmc_mh_1x_2x_rs232_driver_info;
SR_PRIV struct sr_dev_driver gmc_mh_2x_bd232_driver_info;

static const int32_t hwopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

/** Hardware capabilities for Metrahit 1x/2x devices in send mode. */
static const int32_t hwcaps_sm[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_THERMOMETER,    /**< All GMC 1x/2x multimeters seem to support this */
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_CONTINUOUS,
};

/** Hardware capabilities for Metrahit 2x devices in bidirectional Mode. */
static const int32_t hwcaps_bd[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_THERMOMETER,    /**< All GMC 1x/2x multimeters seem to support this */
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_CONTINUOUS,
	SR_CONF_POWER_OFF,
};


/* TODO:
 * - For the 29S SR_CONF_ENERGYMETER, too.
 * - SR_CONF_PATTERN_MODE for some 2x devices
 * - SR_CONF_DATALOG for 22M, 26M, 29S and storage adaptors.
 * Need to implement device-specific lists.
 */

/** Init driver gmc_mh_1x_2x_rs232. */
static int init_1x_2x_rs232(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, &gmc_mh_1x_2x_rs232_driver_info, LOG_PREFIX);
}

/** Init driver gmc_mh_2x_bd232. */
static int init_2x_bd232(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, &gmc_mh_2x_bd232_driver_info, LOG_PREFIX);
}

/**
 * Read single byte from serial port.
 *
 * @retval -1 Timeout or error.
 * @retval other Byte.
 */
static int read_byte(struct sr_serial_dev_inst *serial, gint64 timeout)
{
	uint8_t result = 0;
	int rc = 0;

	for (;;) {
		rc = serial_read(serial, &result, 1);
		if (rc == 1) {
			sr_spew("read: 0x%02x/%d", result, result);
			return result;
		}
		if (g_get_monotonic_time() > timeout)
			return -1;
		g_usleep(2000);
	}
}

/**
 * Try to detect GMC 1x/2x multimeter model in send mode for max. 1 second.
 *
 * @param serial Configured, open serial port.
 *
 * @retval NULL Detection failed.
 * @retval other Model code.
 */
static enum model scan_model_sm(struct sr_serial_dev_inst *serial)
{
	int byte, bytecnt, cnt;
	enum model model;
	gint64 timeout_us;

	model = METRAHIT_NONE;
	timeout_us = g_get_monotonic_time() + 1 * 1000 * 1000;

	/*
	 * Try to find message consisting of device code and several
	 * (at least 4) data bytes.
	 */
	for (bytecnt = 0; bytecnt < 100; bytecnt++) {
		byte = read_byte(serial, timeout_us);
		if ((byte == -1) || (timeout_us < g_get_monotonic_time()))
			break;
		if ((byte & MSGID_MASK) == MSGID_INF) {
			if (!(model = gmc_decode_model_sm(byte & MSGC_MASK)))
				break;
			/* Now expect (at least) 4 data bytes. */
			for (cnt = 0; cnt < 4; cnt++) {
				byte = read_byte(serial, timeout_us);
				if ((byte == -1) ||
						((byte & MSGID_MASK) != MSGID_DATA))
				{
					model = METRAHIT_NONE;
					bytecnt = 100;
					break;
				}
			}
			break;
		}
	}

	return model;
}

/**
 * Scan for Metrahit 1x and Metrahit 2x in send mode using Gossen Metrawatt
 * 'RS232' interface.
 *
 * The older 1x models use 8192 baud and the newer 2x 9600 baud.
 * The DMM usually sends up to about 20 messages per second. However, depending
 * on configuration and measurement mode the intervals can be much larger and
 * then the detection might not work.
 */
static GSList *scan_1x_2x_rs232(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_probe *probe;
	struct sr_serial_dev_inst *serial;
	GSList *l, *devices;
	const char *conn, *serialcomm;
	enum model model;
	gboolean serialcomm_given;

	devices = NULL;
	drvc = (&gmc_mh_1x_2x_rs232_driver_info)->priv;
	drvc->instances = NULL;
	conn = serialcomm = NULL;
	model = METRAHIT_NONE;
	serialcomm_given = FALSE;

	sr_spew("scan_1x_2x_rs232() called!");

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			serialcomm_given = TRUE;
			break;
		}
	}
	if (!conn)
		return NULL;
	if (!serialcomm)
		serialcomm = SERIALCOMM_2X_RS232;

	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

	if (serial_open(serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK) {
		sr_serial_dev_inst_free(serial);
		return NULL;
	}

	serial_flush(serial);

	model = scan_model_sm(serial);

	/*
	 * If detection failed and no user-supplied parameters,
	 * try second baud rate.
	 */
	if ((model == METRAHIT_NONE) && !serialcomm_given) {
		serialcomm = SERIALCOMM_1X_RS232;
		g_free(serial->serialcomm);
		serial->serialcomm = g_strdup(serialcomm);
		if (serial_set_paramstr(serial, serialcomm) == SR_OK) {
			serial_flush(serial);
			model = scan_model_sm(serial);
		}
	}

	if (model != METRAHIT_NONE) {
		sr_spew("%s %s detected!", VENDOR_GMC, gmc_model_str(model));
		if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, VENDOR_GMC,
				gmc_model_str(model), "")))
			return NULL;
		if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
			sr_err("Device context malloc failed.");
			return NULL;
		}
		devc->model = model;
		devc->limit_samples = 0;
		devc->limit_msec = 0;
		devc->num_samples = 0;
		devc->elapsed_msec = g_timer_new();
		devc->settings_ok = FALSE;

		sdi->conn = serial;
		sdi->priv = devc;
		sdi->driver = &gmc_mh_1x_2x_rs232_driver_info;
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
			return NULL;
		sdi->probes = g_slist_append(sdi->probes, probe);
		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);
	}

	return devices;
}

/** Scan for Metrahit 2x in a bidirectional mode using Gossen Metrawatt 'BD 232' interface.
 *
 */
static GSList *scan_2x_bd232(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_probe *probe;
	struct sr_serial_dev_inst *serial;
	GSList *l, *devices;
	const char *conn, *serialcomm;
	int cnt, byte;
	gint64 timeout_us;

	sdi = NULL;
	devc = NULL;
	conn = serialcomm = NULL;
	devices = NULL;

	drvc = (&gmc_mh_2x_bd232_driver_info)->priv;
	drvc->instances = NULL;

	sr_spew("scan_2x_bd232() called!");

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
		serialcomm = SERIALCOMM_2X;

	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

	if (serial_open(serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		goto exit_err;

	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		goto exit_err;
	}

	if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, VENDOR_GMC, NULL, NULL)))
		goto exit_err;

	sdi->priv = devc;

	/* Send message 03 "Query multimeter version and status" */
	sdi->conn = serial;
	sdi->priv = devc;
	if (req_stat14(sdi, TRUE) != SR_OK)
		goto exit_err;

	/* Wait for reply from device(s) for up to 2s. */
	timeout_us = g_get_monotonic_time() + 2*1000*1000;

	while (timeout_us > g_get_monotonic_time()) {
		/* Receive reply (14 bytes) */
		devc->buflen = 0;
		for (cnt = 0; cnt < 14; cnt++) {
			byte = read_byte(serial, timeout_us);
			if (byte != -1)
				devc->buf[devc->buflen++] = (byte & MASK_6BITS);
		}

		if (devc->buflen != 14)
			continue;

		devc->addr = devc->buf[0];
		process_msg14(sdi);
		devc->buflen = 0;

		if (devc->model != METRAHIT_NONE) {
			sr_spew("%s %s detected!", VENDOR_GMC, gmc_model_str(devc->model));

			devc->elapsed_msec = g_timer_new();

			sdi->model = g_strdup(gmc_model_str(devc->model));
			sdi->version = g_strdup_printf("Firmware %d.%d", devc->fw_ver_maj, devc->fw_ver_min);
			sdi->conn = serial;
			sdi->priv = devc;
			sdi->driver = &gmc_mh_2x_bd232_driver_info;
			if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
				goto exit_err;
			sdi->probes = g_slist_append(sdi->probes, probe);
			drvc->instances = g_slist_append(drvc->instances, sdi);
			devices = g_slist_append(devices, sdi);

			if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
				sr_err("Device context malloc failed.");
				goto exit_err;
			}

			if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, VENDOR_GMC, NULL, NULL)))
				goto exit_err;
		}
	};

	/* Free last alloc if no device found */
	if (devc->model == METRAHIT_NONE) {
		g_free(devc);
		sr_dev_inst_free(sdi);
	}

	return devices;

exit_err:
	sr_info("scan_2x_bd232(): Error!");

	if (serial)
		sr_serial_dev_inst_free(serial);
	if (devc)
		g_free(devc);
	if (sdi)
		sr_dev_inst_free(sdi);

	return NULL;
}

/** Driver device list function */
static GSList *dev_list_1x_2x_rs232(void)
{
	return ((struct drv_context *)(gmc_mh_1x_2x_rs232_driver_info.priv))->instances;
}

/** Driver device list function */
static GSList *dev_list_2x_bd232(void)
{
	return ((struct drv_context *)(gmc_mh_2x_bd232_driver_info.priv))
			->instances;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	std_serial_dev_close(sdi);

	sdi->status = SR_ST_INACTIVE;

	/* Free dynamically allocated resources. */
	if ((devc = sdi->priv) && devc->elapsed_msec) {
		g_timer_destroy(devc->elapsed_msec);
		devc->elapsed_msec = NULL;
		devc->model = METRAHIT_NONE;
	}

	return SR_OK;
}

static int cleanup_sm_rs232(void)
{
	return std_dev_clear(&gmc_mh_1x_2x_rs232_driver_info, NULL);
}

static int cleanup_2x_bd232(void)
{
	return std_dev_clear(&gmc_mh_2x_bd232_driver_info, NULL);
}

/** Get value of configuration item */
static int config_get(int key, GVariant **data, const struct sr_dev_inst *sdi,
		      const struct sr_probe_group *probe_group)
{
	int ret;
	struct dev_context *devc;

	(void)sdi;
	(void)data;
	(void)probe_group;

	ret = SR_OK;

	(void)probe_group;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(devc->limit_msec);
		break;

	case SR_CONF_POWER_OFF:
		*data = g_variant_new_boolean(FALSE);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

/** Implementation of config_list, auxiliary function for common parts, */
static int config_list_common(int key, GVariant **data, const struct sr_dev_inst *sdi,
			      const struct sr_probe_group *probe_group)
{
	(void)sdi;
	(void)probe_group;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
						  hwopts, ARRAY_SIZE(hwopts), sizeof(int32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

/** Implementation of config_list for Metrahit 1x/2x send mode */
static int config_list_sm(int key, GVariant **data, const struct sr_dev_inst *sdi,
			  const struct sr_probe_group *probe_group)
{
	(void)sdi;
	(void)probe_group;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
						  hwcaps_sm, ARRAY_SIZE(hwcaps_sm), sizeof(int32_t));
		break;
	default:
		return config_list_common(key, data, sdi, probe_group);
	}

	return SR_OK;
}

/** Implementation of config_list for Metrahit 2x bidirectional mode */
static int config_list_bd(int key, GVariant **data, const struct sr_dev_inst *sdi,
			  const struct sr_probe_group *probe_group)
{
	(void)sdi;
	(void)probe_group;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
						  hwcaps_bd, ARRAY_SIZE(hwcaps_bd), sizeof(int32_t));
		break;
	default:
		return config_list_common(key, data, sdi, probe_group);
	}

	return SR_OK;
}

static int dev_acquisition_start_1x_2x_rs232(const struct sr_dev_inst *sdi,
					     void *cb_data)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (!sdi || !cb_data || !(devc = sdi->priv))
		return SR_ERR_BUG;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc->cb_data = cb_data;
	devc->settings_ok = FALSE;
	devc->buflen = 0;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Start timer, if required. */
	if (devc->limit_msec)
		g_timer_start(devc->elapsed_msec);

	/* Poll every 40ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(serial, G_IO_IN, 40, gmc_mh_1x_2x_receive_data,
			  (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_start_2x_bd232(const struct sr_dev_inst *sdi,
					  void *cb_data)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (!sdi || !cb_data || !(devc = sdi->priv))
		return SR_ERR_BUG;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc->cb_data = cb_data;
	devc->settings_ok = FALSE;
	devc->buflen = 0;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Start timer, if required. */
	if (devc->limit_msec)
		g_timer_start(devc->elapsed_msec);

	/* Poll every 40ms, or whenever some data comes in. */
	serial = sdi->conn;
	serial_source_add(serial, G_IO_IN, 40, gmc_mh_2x_receive_data,
			  (void *)sdi);

	/* Send start message */
	return req_meas14(sdi);
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

SR_PRIV struct sr_dev_driver gmc_mh_1x_2x_rs232_driver_info = {
	.name = "gmc-mh-1x-2x-rs232",
	.longname = "Gossen Metrawatt Metrahit 1x/2x, RS232 interface",
	.api_version = 1,
	.init = init_1x_2x_rs232,
	.cleanup = cleanup_sm_rs232,
	.scan = scan_1x_2x_rs232,
	.dev_list = dev_list_1x_2x_rs232,
	.dev_clear = NULL,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list_sm,
	.dev_open = std_serial_dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start_1x_2x_rs232,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};

SR_PRIV struct sr_dev_driver gmc_mh_2x_bd232_driver_info = {
	.name = "gmc-mh-2x-bd232",
	.longname = "Gossen Metrawatt Metrahit 2x, BD232/SI232-II interface",
	.api_version = 1,
	.init = init_2x_bd232,
	.cleanup = cleanup_2x_bd232,
	.scan = scan_2x_bd232,
	.dev_list = dev_list_2x_bd232,
	.dev_clear = NULL,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list_bd,
	.dev_open = std_serial_dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start_2x_bd232,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
