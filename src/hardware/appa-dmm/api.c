/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Martin Eitzenberger <x@cymaphore.net>
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

/**
 * @file
 * @version 1
 *
 * APPA B Interface
 *
 * Based on APPA Communication Protocol v2.8
 *
 * Driver for modern APPA meters (handheld, bench, clamp). Communication is
 * done over a serial interface using the known APPA-Frames, see below. The
 * base protocol is always the same and deviates only where the models have
 * differences in ablities, range and features.
 *
 * Supporting Live data and downloading LOG and MEM data from devices.
 * Connection is done via BLE or optical serial (USB, EA232, EA485).
 *
 * Utilizes the APPA transport protocol for packet handling.
 *
 * Support for calibration information is prepared but not implemented.
 */

#include <config.h>
#include "protocol.h"

static const uint32_t appadmm_scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t appadmm_drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t appadmm_devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *appadmm_data_sources[] = {
	"Live", /**< APPADMM_DATA_SOURCE_LIVE */
	"MEM", /**< APPADMM_DATA_SOURCE_MEM */
	"LOG", /**< APPADMM_DATA_SOURCE_LOG */
};

/**
 * Scanning function
 * Invoked by the Protocol-specific scan functions
 *
 * @param di Driver instance
 * @param options Options
 * @param arg_protocol APPA-Protocol variant to use
 * @retval Device on success
 * @retval NULL on error
 */
static GSList *appadmm_scan(struct sr_dev_driver *di, GSList *options,
	enum appadmm_protocol_e arg_protocol)
{
	struct drv_context *drvc;
	struct appadmm_context *devc;
	GSList *devices;
	const char *conn;
	const char *serialcomm;
	struct sr_dev_inst *sdi;
	struct sr_serial_dev_inst *serial;
	struct sr_channel_group *group;
	struct sr_channel *channel_primary;
	struct sr_channel *channel_secondary;

	int retr;

	GSList *it;
	struct sr_config *src;

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

	/* Device context is used instead of another ..._info struct here */
	devc = g_malloc0(sizeof(struct appadmm_context));
	appadmm_clear_context(devc);

	devc->protocol = arg_protocol;

	serialcomm = APPADMM_CONF_SERIAL;
	conn = NULL;
	for (it = options; it; it = it->next) {
		src = it->data;
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
		serialcomm = APPADMM_CONF_SERIAL;

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) < SR_OK)
		return NULL;

	if (serial_flush(serial) < SR_OK)
		return NULL;

	sdi = g_malloc0(sizeof(*sdi));

	sdi->conn = serial;
	sdi->inst_type = SR_INST_SERIAL;
	sdi->status = SR_ST_INACTIVE;
	sdi->driver = di;
	sdi->priv = devc;

	sr_tp_appa_init(&devc->appa_inst, serial);

	switch (devc->protocol) {
	case APPADMM_PROTOCOL_GENERIC:
		appadmm_op_identify(sdi);
		break;
	case APPADMM_PROTOCOL_100:
		appadmm_100_op_identify(sdi);
		break;
	case APPADMM_PROTOCOL_300:
		appadmm_300_op_identify(sdi);
		break;
	case APPADMM_PROTOCOL_500:
		appadmm_500_op_identify(sdi);
		break;
	default:
		break;
	}

	/* If received model is invalid or nothing received, abort */
	if (devc->model_id == APPADMM_MODEL_ID_INVALID
		|| devc->model_id < APPADMM_MODEL_ID_INVALID
		|| devc->model_id > APPADMM_MODEL_ID_OVERFLOW) {
		sr_err("APPA-Device NOT FOUND or INVALID; No valid response "
			"to read_information request.");
		sr_serial_dev_inst_free(serial);
		serial_close(serial);
		g_free(sdi);
		g_free(devc);
		return NULL;
	}

	if (devc->protocol == APPADMM_PROTOCOL_100) {
		devc->rate_interval = APPADMM_RATE_INTERVAL_100;
		sr_err("WARNING! EXPERIMENTAL!");
		sr_err("Support for APPA 10x(N) has only been implemented by");
		sr_err("spec and never been tested. Expect problems. Please");
		sr_err("report your success or failure in using it.");
	}

	else if (devc->protocol == APPADMM_PROTOCOL_300) {
		devc->rate_interval = APPADMM_RATE_INTERVAL_300;
		sr_err("WARNING! EXPERIMENTAL!");
		sr_err("Support for APPA 30x has only been implemented by");
		sr_err("spec and never been tested. Expect problems. Please");
		sr_err("report your success or failure in using it.");
	}

	else if (devc->protocol == APPADMM_PROTOCOL_500)
		devc->rate_interval = APPADMM_RATE_INTERVAL_500;

#ifdef HAVE_BLUETOOTH
	else if (devc->appa_inst.serial->bt_conn_type == SER_BT_CONN_APPADMM)
		/* Models with the AMICCOM A8105 have troubles with
		 * higher rates over BLE, let them run without time windows
		 */
		devc->rate_interval = APPADMM_RATE_INTERVAL_DISABLE;
#endif/*#ifdef HAVE_BLUETOOTH*/

	else
		devc->rate_interval = APPADMM_RATE_INTERVAL_DEFAULT;

	sr_info("APPA-Device DETECTED; Vendor: %s, Model: %s, "
		"OEM-Model: %s, Version: %s, Serial number: %s, Model ID: %i",
		sdi->vendor,
		sdi->model,
		appadmm_model_id_name(devc->model_id),
		sdi->version,
		sdi->serial_num,
		devc->model_id);

	channel_primary = sr_channel_new(sdi,
		APPADMM_CHANNEL_DISPLAY_PRIMARY,
		SR_CHANNEL_ANALOG,
		TRUE,
		appadmm_channel_name(APPADMM_CHANNEL_DISPLAY_PRIMARY));

	channel_secondary = sr_channel_new(sdi,
		APPADMM_CHANNEL_DISPLAY_SECONDARY,
		SR_CHANNEL_ANALOG,
		TRUE,
		appadmm_channel_name(APPADMM_CHANNEL_DISPLAY_SECONDARY));

	group = g_malloc0(sizeof(*group));
	group->name = g_strdup("Display");
	sdi->channel_groups = g_slist_append(sdi->channel_groups, group);

	group->channels = g_slist_append(group->channels, channel_primary);
	group->channels = g_slist_append(group->channels, channel_secondary);

	devices = g_slist_append(devices, sdi);

	if ((retr = serial_close(serial)) < SR_OK) {
		sr_err("Unable to close device after scan");
		return NULL;
	}

	return std_scan_complete(di, devices);
}

static GSList *appadmm_generic_scan(struct sr_dev_driver *di, GSList *options)
{
	return appadmm_scan(di, options, APPADMM_PROTOCOL_GENERIC);
}

static GSList *appadmm_100_scan(struct sr_dev_driver *di, GSList *options)
{
	return appadmm_scan(di, options, APPADMM_PROTOCOL_100);
}

static GSList *appadmm_300_scan(struct sr_dev_driver *di, GSList *options)
{
	return appadmm_scan(di, options, APPADMM_PROTOCOL_300);
}

static GSList *appadmm_500_scan(struct sr_dev_driver *di, GSList *options)
{
	return appadmm_scan(di, options, APPADMM_PROTOCOL_500);
}

static int appadmm_config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct appadmm_context *devc;

	(void) cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_FRAMES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_DATA_SOURCE:
		return(*data =
			g_variant_new_string(appadmm_data_sources[devc->data_source]))
			!= NULL ? SR_OK : SR_ERR_ARG;
	default:
		return SR_ERR_NA;
	}

	return SR_ERR_ARG;
}

static int appadmm_config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct appadmm_context *devc;

	int idx;
	int retr;

	(void) cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	retr = SR_OK;
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_FRAMES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	case SR_CONF_DATA_SOURCE:
		if ((idx = std_str_idx(data,
			ARRAY_AND_SIZE(appadmm_data_sources))) < 0)
			return SR_ERR_ARG;
		devc->data_source = idx;
		break;
	default:
		retr = SR_ERR_NA;
	}

	return retr;
}

static int appadmm_config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int retr;

	retr = SR_OK;

	if (!sdi)
		return STD_CONFIG_LIST(key, data, sdi, cg,
		appadmm_scanopts, appadmm_drvopts, appadmm_devopts);

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg,
			appadmm_scanopts, appadmm_drvopts, appadmm_devopts);
	case SR_CONF_DATA_SOURCE:
		*data =
			g_variant_new_strv(ARRAY_AND_SIZE(appadmm_data_sources));
		break;
	default:
		return SR_ERR_NA;
	}

	return retr;
}

/**
 * Start Data Acquisition, for Live, LOG and MEM alike.
 *
 * For MEM and LOG entries, check if the device is capable of such a feature
 * and request the amount of data present. Otherwise acquisition will instantly
 * fail.
 *
 * @param sdi Serial Device Instance
 * @retval SR_OK on success
 * @retval SR_ERR_... on error
 */
static int appadmm_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct appadmm_context *devc;
	struct sr_serial_dev_inst *serial;
	enum appadmm_storage_e storage;

	int retr;

	devc = sdi->priv;
	serial = sdi->conn;

	retr = SR_ERR_NA;

	switch (devc->data_source) {
	case APPADMM_DATA_SOURCE_LIVE:
		sr_sw_limits_acquisition_start(&devc->limits);
		if ((retr = std_session_send_df_header(sdi)) < SR_OK)
			return retr;

		switch (devc->protocol) {
		case APPADMM_PROTOCOL_GENERIC:
			retr = serial_source_add(sdi->session, serial,
				G_IO_IN, 10,
				appadmm_acquire_live, (void *) sdi);
			break;
		case APPADMM_PROTOCOL_100:
			retr = serial_source_add(sdi->session, serial,
				G_IO_IN, 10,
				appadmm_100_acquire_live, (void *) sdi);
			break;
		case APPADMM_PROTOCOL_300:
			retr = serial_source_add(sdi->session, serial,
				G_IO_IN, 10,
				appadmm_300_acquire_live, (void *) sdi);
			break;
		case APPADMM_PROTOCOL_500:
			retr = serial_source_add(sdi->session, serial,
				G_IO_IN, 10,
				appadmm_500_acquire_live, (void *) sdi);
			break;
		default:
			retr = SR_ERR_NA;
			break;
		}
		break;

	case APPADMM_DATA_SOURCE_MEM:
	case APPADMM_DATA_SOURCE_LOG:
		switch (devc->protocol) {
		case APPADMM_PROTOCOL_GENERIC:
			if ((retr = appadmm_op_storage_info(sdi)) < SR_OK)
				return retr;

			break;
		case APPADMM_PROTOCOL_500:
			if ((retr = appadmm_500_op_storage_info(sdi)) < SR_OK)
				return retr;
			break;
		default:
			retr = SR_ERR_NA;
			break;
		}

		switch (devc->data_source) {
		case APPADMM_DATA_SOURCE_MEM:
			storage = APPADMM_STORAGE_MEM;
			break;
		case APPADMM_DATA_SOURCE_LOG:
			storage = APPADMM_STORAGE_LOG;
			break;
		default:
			return SR_ERR_BUG;
		}

		devc->error_counter = 0;

		/* Frame limit is used for selecting the amount of data read
		 * from the device. Thhis way the user can reduce the amount
		 * of data downloaded from the device. */
		if (devc->limits.limit_frames < 1
			|| devc->limits.limit_frames >
			(uint64_t) devc->storage_info[storage].amount)
			devc->limits.limit_frames =
			devc->storage_info[storage].amount;

		sr_sw_limits_acquisition_start(&devc->limits);
		if ((retr = std_session_send_df_header(sdi)) < SR_OK)
			return retr;

		if (devc->storage_info[storage].rate > 0) {
			sr_session_send_meta(sdi, SR_CONF_SAMPLE_INTERVAL,
				g_variant_new_uint64(devc->storage_info[storage].rate));
		}

		switch (devc->protocol) {
		case APPADMM_PROTOCOL_GENERIC:
			retr = serial_source_add(sdi->session, serial,
				G_IO_IN, 10,
				appadmm_acquire_storage, (void *) sdi);
			break;
		case APPADMM_PROTOCOL_500:
			retr = serial_source_add(sdi->session, serial,
				G_IO_IN, 10,
				appadmm_500_acquire_storage, (void *) sdi);
			break;
		default:
			retr = SR_ERR_NA;
			break;
		}
		break;
	}

	return retr;
}

#define APPADMM_DRIVER_ENTRY(ARG_NAME, ARG_LONGNAME, ARG_PROTOCOL_SCAN) \
&((struct sr_dev_driver){ \
	.name = ARG_NAME, \
	.longname = ARG_LONGNAME, \
	.api_version = 1, \
	.init = std_init, \
	.cleanup = std_cleanup, \
	.scan = ARG_PROTOCOL_SCAN, \
	.dev_list = std_dev_list, \
	.dev_clear = std_dev_clear, \
	.config_get = appadmm_config_get, \
	.config_set = appadmm_config_set, \
	.config_list = appadmm_config_list, \
	.dev_open = std_serial_dev_open, \
	.dev_close = std_serial_dev_close, \
	.dev_acquisition_start = appadmm_acquisition_start, \
	.dev_acquisition_stop = std_serial_dev_acquisition_stop, \
	.context = NULL, \
})

/**
 * List of assigned driver names
 */
SR_REGISTER_DEV_DRIVER_LIST(appadmm_drivers,
	APPADMM_DRIVER_ENTRY("appa-dmm",
		"APPA 150, 170, 208, 500, A, S and sFlex-Series",
		appadmm_generic_scan),
	APPADMM_DRIVER_ENTRY("appa-10xn",
		"APPA 10x(N) Series (EXPERIMENTAL)",
		appadmm_100_scan),
	APPADMM_DRIVER_ENTRY("appa-300",
		"APPA 207 and 300 Series (EXPERIMENTAL)",
		appadmm_300_scan),
	APPADMM_DRIVER_ENTRY("appa-503-505",
		"APPA 503 and 505",
		appadmm_500_scan),
	APPADMM_DRIVER_ENTRY("benning-dmm",
		"BENNING MM 10-1, MM 12, CM 9-2, CM 10-1, CM 12, -PV",
		appadmm_generic_scan),
	APPADMM_DRIVER_ENTRY("benning-mm11",
		"BENNING MM 11 (EXPERIMENTAL)",
		appadmm_100_scan),
	APPADMM_DRIVER_ENTRY("cmt-35xx",
		"CMT 35xx Series",
		appadmm_generic_scan),
	APPADMM_DRIVER_ENTRY("ht-8100",
		"HT Instruments HT8100",
		appadmm_generic_scan),
	APPADMM_DRIVER_ENTRY("ideal-492-495",
		"IDEAL 61-492 and 61-495 (EXPERIMENTAL)",
		appadmm_100_scan),
	APPADMM_DRIVER_ENTRY("ideal-497-498",
		"IDEAL 61-497 and 61-498",
		appadmm_500_scan),
	APPADMM_DRIVER_ENTRY("iso-tech-idm10xn",
		"ISO-TECH IDM10x(N) (EXPERIMENTAL)",
		appadmm_100_scan),
	APPADMM_DRIVER_ENTRY("iso-tech-idm30x",
		"ISO-TECH IDM207 and IDM30x Series (EXPERIMENTAL)",
		appadmm_300_scan),
	APPADMM_DRIVER_ENTRY("iso-tech-idm50x",
		"ISO-TECH IDM50x Series",
		appadmm_500_scan),
	APPADMM_DRIVER_ENTRY("kps-dmm",
		"KPS DMM9000BT, DMM3500BT, DCM7000BT, DCM8000BT",
		appadmm_generic_scan),
	APPADMM_DRIVER_ENTRY("megger-dmm",
		"MEGGER DCM1500S and DPM1000",
		appadmm_generic_scan),
	APPADMM_DRIVER_ENTRY("metravi-dmm",
		"METRAVI PRO Solar-1",
		appadmm_generic_scan),
	APPADMM_DRIVER_ENTRY("rspro-idm50x",
		"RS PRO IDM50x",
		appadmm_500_scan),
	APPADMM_DRIVER_ENTRY("rspro-s",
		"RS PRO S and 150 Series",
		appadmm_generic_scan),
	APPADMM_DRIVER_ENTRY("sefram-dmm",
		"Sefram 7xxx and MW35x6BF Series",
		appadmm_generic_scan),
	APPADMM_DRIVER_ENTRY("sefram-735x-legacy",
		"Sefram 7351 and 7355",
		appadmm_generic_scan),
	APPADMM_DRIVER_ENTRY("voltcraft-vc930",
		"Voltcraft VC-930",
		appadmm_500_scan),
	APPADMM_DRIVER_ENTRY("voltcraft-vc950",
		"Voltcraft VC-950",
		appadmm_500_scan),
	);
