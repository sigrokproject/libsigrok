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

#include <config.h>
#include <string.h>
#include "protocol.h"

/* Serial communication parameters for Metrahit 1x/2x with 'RS232' adaptor */
#define SERIALCOMM_1X_RS232 "8228/6n1/dtr=1/rts=1/flow=0" /* =8192, closer with divider */
#define SERIALCOMM_2X_RS232 "9600/6n1/dtr=1/rts=1/flow=0"
#define SERIALCOMM_2X "9600/8n1/dtr=1/rts=1/flow=0"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
	SR_CONF_THERMOMETER, /**< All GMC 1x/2x multimeters seem to support this */
};

/** Hardware capabilities for Metrahit 1x/2x devices in send mode. */
static const uint32_t devopts_sm[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

/** Hardware capabilities for Metrahit 2x devices in bidirectional Mode. */
static const uint32_t devopts_bd[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_POWER_OFF | SR_CONF_GET | SR_CONF_SET,
};

/* TODO:
 * - For the 29S SR_CONF_ENERGYMETER, too.
 * - SR_CONF_PATTERN_MODE for some 2x devices
 * - SR_CONF_DATALOG for 22M, 26M, 29S and storage adaptors.
 * Need to implement device-specific lists.
 */

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
		rc = serial_read_nonblocking(serial, &result, 1);
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
	timeout_us = g_get_monotonic_time() + (1 * 1000 * 1000);

	/*
	 * Try to find message consisting of device code and several
	 * (at least 4) data bytes.
	 */
	serial_flush(serial);
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
static GSList *scan_1x_2x_rs232(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	GSList *l, *devices;
	const char *conn, *serialcomm;
	enum model model;
	gboolean serialcomm_given;

	devices = NULL;
	conn = serialcomm = NULL;
	serialcomm_given = FALSE;

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

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK) {
		sr_serial_dev_inst_free(serial);
		return NULL;
	}

	model = scan_model_sm(serial);

	/*
	 * If detection failed and no user-supplied parameters,
	 * try second baud rate.
	 */
	if ((model == METRAHIT_NONE) && !serialcomm_given) {
		serialcomm = SERIALCOMM_1X_RS232;
		g_free(serial->serialcomm);
		serial->serialcomm = g_strdup(serialcomm);
		if (serial_set_paramstr(serial, serialcomm) == SR_OK)
			model = scan_model_sm(serial);
	}

	if (model != METRAHIT_NONE) {
		sr_spew("%s detected!", gmc_model_str(model));
		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup("Gossen Metrawatt");
		sdi->model = g_strdup(gmc_model_str(model));
		devc = g_malloc0(sizeof(struct dev_context));
		sr_sw_limits_init(&devc->limits);
		devc->model = model;
		devc->settings_ok = FALSE;
		sdi->conn = serial;
		sdi->priv = devc;
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P1");
		devices = g_slist_append(devices, sdi);
	}

	return std_scan_complete(di, devices);
}

/**
 * Scan for Metrahit 2x in a bidirectional mode using Gossen Metrawatt
 * 'BD 232' interface.
 */
static GSList *scan_2x_bd232(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	GSList *l, *devices;
	const char *conn, *serialcomm;
	int cnt, byte;
	gint64 timeout_us;

	sdi = NULL;
	devc = NULL;
	conn = serialcomm = NULL;
	devices = NULL;

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

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		goto exit_err;

	devc = g_malloc0(sizeof(struct dev_context));

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Gossen Metrawatt");
	sdi->priv = devc;

	/* Send message 03 "Query multimeter version and status" */
	sdi->conn = serial;
	if (req_stat14(sdi, TRUE) != SR_OK)
		goto exit_err;

	/* Wait for reply from device(s) for up to 2s. */
	timeout_us = g_get_monotonic_time() + (2 * 1000 * 1000);

	while (timeout_us > g_get_monotonic_time()) {
		/* Receive reply (14 bytes) */
		devc->buflen = 0;
		for (cnt = 0; cnt < GMC_REPLY_SIZE; cnt++) {
			byte = read_byte(serial, timeout_us);
			if (byte != -1)
				devc->buf[devc->buflen++] = (byte & MASK_6BITS);
		}

		if (devc->buflen != GMC_REPLY_SIZE)
			continue;

		devc->addr = devc->buf[0];
		process_msg14(sdi);
		devc->buflen = 0;

		if (devc->model != METRAHIT_NONE) {
			sr_spew("%s detected!", gmc_model_str(devc->model));
			sr_sw_limits_init(&devc->limits);
			sdi->model = g_strdup(gmc_model_str(devc->model));
			sdi->version = g_strdup_printf("Firmware %d.%d", devc->fw_ver_maj, devc->fw_ver_min);
			sdi->conn = serial;
			sdi->priv = devc;
			sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P1");
			devices = g_slist_append(devices, sdi);
			devc = g_malloc0(sizeof(struct dev_context));
			sdi = g_malloc0(sizeof(struct sr_dev_inst));
			sdi->status = SR_ST_INACTIVE;
			sdi->vendor = g_strdup("Gossen Metrawatt");
		}
	};

	/* Free last alloc that was done in preparation. */
	g_free(devc);
	sr_dev_inst_free(sdi);

	return std_scan_complete(di, devices);

exit_err:
	sr_serial_dev_inst_free(serial);
	g_free(devc);
	sr_dev_inst_free(sdi);

	return NULL;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	devc->model = METRAHIT_NONE;

	return std_serial_dev_close(sdi);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_POWER_OFF:
		*data = g_variant_new_boolean(FALSE);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

/** Implementation of config_list for Metrahit 1x/2x send mode */
static int config_list_sm(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts_sm);
}

/** Implementation of config_list for Metrahit 2x bidirectional mode */
static int config_list_bd(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts_bd);
}

static int dev_acquisition_start_1x_2x_rs232(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	devc = sdi->priv;
	devc->settings_ok = FALSE;
	devc->buflen = 0;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 40,
			gmc_mh_1x_2x_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_start_2x_bd232(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	devc = sdi->priv;
	devc->settings_ok = FALSE;
	devc->buflen = 0;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 40,
			gmc_mh_2x_receive_data, (void *)sdi);

	/* Send start message */
	return req_meas14(sdi);
}

static struct sr_dev_driver gmc_mh_1x_2x_rs232_driver_info = {
	.name = "gmc-mh-1x-2x-rs232",
	.longname = "Gossen Metrawatt Metrahit 1x/2x, RS232 interface",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan_1x_2x_rs232,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list_sm,
	.dev_open = std_serial_dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start_1x_2x_rs232,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(gmc_mh_1x_2x_rs232_driver_info);

static struct sr_dev_driver gmc_mh_2x_bd232_driver_info = {
	.name = "gmc-mh-2x-bd232",
	.longname = "Gossen Metrawatt Metrahit 2x, BD232/SI232-II interface",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan_2x_bd232,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list_bd,
	.dev_open = std_serial_dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start_2x_bd232,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(gmc_mh_2x_bd232_driver_info);
