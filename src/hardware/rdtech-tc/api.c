/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Andreas Sandberg <andreas@sandberg.pp.se>
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

#include <fcntl.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "libsigrok-internal.h"
#include "protocol.h"

#define RDTECH_TC_SERIALCOMM "115200/8n1"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_ENERGYMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static GSList *rdtech_tc_scan(struct sr_dev_driver *di,
	const char *conn, const char *serialcomm)
{
	struct sr_serial_dev_inst *serial;
	GSList *devices;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	size_t i;
	const struct rdtech_tc_channel_desc *pch;
	struct sr_channel *ch;
	struct feed_queue_analog *feed;

	serial = sr_serial_dev_inst_new(conn, serialcomm);
	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		goto err_out;

	devc = g_malloc0(sizeof(*devc));
	sr_sw_limits_init(&devc->limits);

	if (rdtech_tc_probe(serial, devc) != SR_OK) {
		sr_err("Failed to find a supported RDTech TC device.");
		goto err_out_serial;
	}

	sdi = g_malloc0(sizeof(*sdi));
	sdi->priv = devc;
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("RDTech");
	sdi->model = g_strdup(devc->dev_info.model_name);
	sdi->version = g_strdup(devc->dev_info.fw_ver);
	sdi->serial_num = g_strdup_printf("%08" PRIu32, devc->dev_info.serial_num);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	devc->feeds = g_malloc0(devc->channel_count * sizeof(devc->feeds[0]));
	for (i = 0; i < devc->channel_count; i++) {
		pch = &devc->channels[i];
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, pch->name);
		feed = feed_queue_analog_alloc(sdi, 1, pch->digits, ch);
		feed_queue_analog_mq_unit(feed, pch->mq, 0, pch->unit);
		feed_queue_analog_scale_offset(feed, &pch->scale, NULL);
		devc->feeds[i] = feed;
	}

	devices = g_slist_append(NULL, sdi);
	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return std_scan_complete(di, devices);

err_out_serial:
	g_free(devc);
	serial_close(serial);
err_out:
	sr_serial_dev_inst_free(serial);

	return NULL;
}

static void clear_helper(struct dev_context *devc)
{
	size_t idx;

	if (!devc)
		return;

	if (devc->feeds) {
		for (idx = 0; idx < devc->channel_count; idx++)
			feed_queue_analog_free(devc->feeds[idx]);
		g_free(devc->feeds);
	}
}

static int dev_clear(const struct sr_dev_driver *driver)
{
	return std_dev_clear_with_callback(driver, (std_dev_clear_callback)clear_helper);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	const char *conn;
	const char *serialcomm;

	conn = NULL;
	serialcomm = RDTECH_TC_SERIALCOMM;
	(void)sr_serial_extract_options(options, &conn, &serialcomm);
	if (!conn)
		return NULL;

	return rdtech_tc_scan(di, conn, serialcomm);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;

	return sr_sw_limits_config_get(&devc->limits, key, data);
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;

	return sr_sw_limits_config_set(&devc->limits, key, data);
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	devc = sdi->priv;
	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 50,
		rdtech_tc_receive_data, (void *)sdi);

	return rdtech_tc_poll(sdi, TRUE);
}

static struct sr_dev_driver rdtech_tc_driver_info = {
	.name = "rdtech-tc",
	.longname = "RDTech TC66C USB power meter",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(rdtech_tc_driver_info);
