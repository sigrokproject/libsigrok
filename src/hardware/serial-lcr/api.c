/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Janne Huttunen <jahuttun@gmail.com>
 * Copyright (C) 2019 Gerhard Sittig <gerhard.sittig@gmx.net>
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
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include <math.h>
#include "protocol.h"
#include <stdint.h>
#include <string.h>

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_LCRMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
	SR_CONF_OUTPUT_FREQUENCY | SR_CONF_GET | SR_CONF_LIST,
	SR_CONF_EQUIV_CIRCUIT_MODEL | SR_CONF_GET | SR_CONF_LIST,
};

static struct sr_dev_inst *scan_packet_check_devinst;

static void scan_packet_check_setup(struct sr_dev_inst *sdi)
{
	scan_packet_check_devinst = sdi;
}

static gboolean scan_packet_check_func(const uint8_t *buf)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	const struct lcr_info *lcr;
	struct lcr_parse_info *info;

	/* Get a reference to the LCR model that is getting checked. */
	sdi = scan_packet_check_devinst;
	if (!sdi)
		return FALSE;
	devc = sdi->priv;
	if (!devc)
		return FALSE;
	lcr = devc->lcr_info;
	if (!lcr)
		return FALSE;

	/* Synchronize to the stream of LCR packets. */
	if (!lcr->packet_valid(buf))
		return FALSE;

	/* Have LCR packets _processed_, gather current configuration. */
	info = &devc->parse_info;
	memset(info, 0, sizeof(*info));
	if (lcr->packet_parse(buf, NULL, NULL, info) == SR_OK) {
                devc->output_freq = info->output_freq;
                if (info->circuit_model)
                        devc->circuit_model = info->circuit_model;
        }

	return TRUE;
}

static int scan_lcr_port(const struct lcr_info *lcr,
	const char *conn, struct sr_serial_dev_inst *serial)
{
	size_t len;
	uint8_t buf[128];
	int ret;
	size_t dropped;

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return SR_ERR_IO;
	sr_info("Probing serial port %s.", conn);

	/*
	 * See if we can detect a device of specified type.
	 *
	 * No supported device provides a means to "identify" yet. No
	 * supported device requires "packet request" yet. They all just
	 * send data periodically. So we check if the packets match the
	 * probed device's expected format.
	 */
	if (lcr->packet_request) {
		ret = lcr->packet_request(serial);
		if (ret < 0) {
			sr_err("Failed to request packet: %d.", ret);
			goto scan_port_cleanup;
		}
	}
	len = sizeof(buf);
	ret = serial_stream_detect(serial, buf, &len,
		lcr->packet_size, lcr->packet_valid, 3000);
	if (ret != SR_OK)
		goto scan_port_cleanup;

	/*
	 * If the packets were found to match after more than two packets
	 * got dropped, something is wrong. This is worth warning about,
	 * but isn't fatal. The dropped bytes might be due to nonstandard
	 * cables that ship with some devices.
	 */
	dropped = len - lcr->packet_size;
	if (dropped > 2 * lcr->packet_size)
		sr_warn("Had to drop unexpected amounts of data.");

	/* Create a device instance for the found device. */
	sr_info("Found %s %s device on port %s.", lcr->vendor, lcr->model, conn);

scan_port_cleanup:
	/* Keep serial port open if probe succeeded. */
	if (ret != SR_OK)
		serial_close(serial);

	return ret;
}

static struct sr_dev_inst *create_lcr_sdi(struct lcr_info *lcr,
	struct sr_serial_dev_inst *serial)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	size_t ch_idx;
	const char **ch_fmts;
	const char *fmt;
	char ch_name[8];

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup(lcr->vendor);
	sdi->model = g_strdup(lcr->model);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	devc = g_malloc0(sizeof(*devc));
	sdi->priv = devc;
	devc->lcr_info = lcr;
	sr_sw_limits_init(&devc->limits);
	ch_fmts = lcr->channel_formats;
	for (ch_idx = 0; ch_idx < lcr->channel_count; ch_idx++) {
		fmt = (ch_fmts && ch_fmts[ch_idx]) ? ch_fmts[ch_idx] : "P%zu";
		snprintf(ch_name, sizeof(ch_name), fmt, ch_idx + 1);
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, ch_name);
	}

	return sdi;
}

static int read_lcr_port(struct sr_dev_inst *sdi,
	const struct lcr_info *lcr, struct sr_serial_dev_inst *serial)
{
	size_t len;
	uint8_t buf[128];
	int ret;

	serial_flush(serial);
	if (lcr->packet_request) {
		ret = lcr->packet_request(serial);
		if (ret < 0) {
			sr_err("Failed to request packet: %d.", ret);
			return ret;
		}
	}

	/*
	 * Receive a few more packets (and process them!) to have the
	 * current output frequency and circuit model parameter values
	 * detected. The above "stream detect" phase only synchronized
	 * to the packets by checking their validity, but it cannot
	 * provide details. This phase here runs a modified "checker"
	 * routine which also extracts details from LCR packets after
	 * the device got detected and parameter storage was prepared.
	 */
	sr_info("Retrieving current acquisition parameters.");
	len = sizeof(buf);
	scan_packet_check_setup(sdi);
	ret = serial_stream_detect(serial, buf, &len,
		lcr->packet_size, scan_packet_check_func, 1500);
	scan_packet_check_setup(NULL);

	return ret;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct lcr_info *lcr;
	struct sr_config *src;
	GSList *l, *devices;
	const char *conn, *serialcomm;
	struct sr_serial_dev_inst *serial;
	int ret;
	struct sr_dev_inst *sdi;

	lcr = (struct lcr_info *)di;

	/* Get serial port name and communication parameters. */
	conn = NULL;
	serialcomm = lcr->comm;
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

	devices = NULL;
	/* TODO Handle ambiguous conn= specs, see serial-dmm. */

	/* Open the serial port, check data packets. */
	serial = sr_serial_dev_inst_new(conn, serialcomm);
	ret = scan_lcr_port(lcr, conn, serial);
	if (ret != SR_OK) {
		/* Probe failed, release 'serial'. */
		sr_serial_dev_inst_free(serial);
	} else {
		/* Create and return device instance, keep 'serial' alive. */
		sdi = create_lcr_sdi(lcr, serial);
		devices = g_slist_append(devices, sdi);
		(void)read_lcr_port(sdi, lcr, serial);
		serial_close(serial);
	}

	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	const struct lcr_info *lcr;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_FRAMES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_OUTPUT_FREQUENCY:
		*data = g_variant_new_double(devc->output_freq);
		return SR_OK;
	case SR_CONF_EQUIV_CIRCUIT_MODEL:
		if (!devc->circuit_model)
			return SR_ERR_NA;
		*data = g_variant_new_string(devc->circuit_model);
		return SR_OK;
	default:
		lcr = devc->lcr_info;
		if (!lcr)
			return SR_ERR_NA;
		if (!lcr->config_get)
			return SR_ERR_NA;
		return lcr->config_get(key, data, sdi, cg);
	}
	/* UNREACH */
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	const struct lcr_info *lcr;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_FRAMES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	default:
		lcr = devc->lcr_info;
		if (!lcr)
			return SR_ERR_NA;
		if (!lcr->config_set)
			return SR_ERR_NA;
		return lcr->config_set(key, data, sdi, cg);
	}
	/* UNREACH */
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	const struct lcr_info *lcr;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg,
			scanopts, drvopts, devopts);
	default:
		break;
	}

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	switch (key) {
	default:
		lcr = devc->lcr_info;
		if (!lcr || !lcr->config_list)
			return SR_ERR_NA;
		return lcr->config_list(key, data, sdi, cg);
	}
	/* UNREACH */
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	devc = sdi->priv;

	/*
	 * Clear values that were gathered during scan or in a previous
	 * acquisition, so that this acquisition's data feed immediately
	 * starts with meta packets before first measurement values, and
	 * also communicates subsequent parameter changes.
	 */
	devc->output_freq = 0;
	devc->circuit_model = NULL;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 50,
		lcr_receive_data, (void *)sdi);

	return SR_OK;
}

#define LCR_ES51919(id, vendor, model) \
	&((struct lcr_info) { \
		{ \
			.name = id, \
			.longname = vendor " " model, \
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
		vendor, model, ES51919_CHANNEL_COUNT, NULL, \
		ES51919_COMM_PARAM, ES51919_PACKET_SIZE, \
		0, NULL, \
		es51919_packet_valid, es51919_packet_parse, \
		NULL, NULL, es51919_config_list, \
	}).di

SR_REGISTER_DEV_DRIVER_LIST(lcr_es51919_drivers,
	LCR_ES51919("deree-de5000", "DER EE", "DE-5000"),
	LCR_ES51919("mastech-ms5308", "MASTECH", "MS5308"),
	LCR_ES51919("peaktech-2170", "PeakTech", "2170"),
	LCR_ES51919("uni-t-ut612", "UNI-T", "UT612"),
);

#define LCR_VC4080(id, vendor, model) \
	&((struct lcr_info) { \
		{ \
			.name = id, \
			.longname = vendor " " model, \
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
		vendor, model, \
		VC4080_CHANNEL_COUNT, vc4080_channel_formats, \
		VC4080_COMM_PARAM, VC4080_PACKET_SIZE, \
		500, vc4080_packet_request, \
		vc4080_packet_valid, vc4080_packet_parse, \
		NULL, NULL, vc4080_config_list, \
	}).di

SR_REGISTER_DEV_DRIVER_LIST(lcr_vc4080_drivers,
	LCR_VC4080("peaktech-2165", "PeakTech", "2165"),
	LCR_VC4080("voltcraft-4080", "Voltcraft", "4080"),
);
