/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 Mathieu Pilato <pilato.mathieu@free.fr>
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
#include "protocol.h"

static struct sr_dev_driver atorch_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_ENERGYMETER,
	SR_CONF_POWERMETER,
	SR_CONF_ELECTRONIC_LOAD,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
};

static int create_channels_feed_queues(struct sr_dev_inst *sdi,
	struct dev_context *devc)
{
	size_t i;
	struct sr_channel *sr_ch;
	const struct atorch_channel_desc *at_ch;
	struct feed_queue_analog *feed;
	const struct atorch_device_profile *p;

	p = devc->profile;
	devc->feeds = g_malloc0(p->channel_count * sizeof(devc->feeds[0]));
	for (i = 0; i < p->channel_count; i++) {
		at_ch = &p->channels[i];
		sr_ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, at_ch->name);
		feed = feed_queue_analog_alloc(sdi, 1, at_ch->digits, sr_ch);
		feed_queue_analog_mq_unit(feed, at_ch->mq, at_ch->flags, at_ch->unit);
		feed_queue_analog_scale_offset(feed, &at_ch->scale, NULL);
		devc->feeds[i] = feed;
	}

	return SR_OK;
}

static GSList *atorch_scan(struct sr_dev_driver *di,
	const char *conn, const char *serialcomm)
{
	struct sr_serial_dev_inst *serial;
	GSList *devices;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;

	serial = sr_serial_dev_inst_new(conn, serialcomm);
	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		goto err_out;

	devc = g_malloc0(sizeof(*devc));

	if (atorch_probe(serial, devc) != SR_OK) {
		sr_err("Failed to find a supported Atorch device.");
		goto err_out_serial;
	}

	sr_sw_limits_init(&devc->limits);

	sdi = g_malloc0(sizeof(*sdi));
	sdi->priv = devc;
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Atorch");
	sdi->model = g_strdup(devc->profile->device_name);
	sdi->version = NULL;
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	create_channels_feed_queues(sdi, devc);

	serial_close(serial);

	devices = g_slist_append(NULL, sdi);
	return std_scan_complete(di, devices);

err_out_serial:
	g_free(devc);
	serial_close(serial);
err_out:
	sr_serial_dev_inst_free(serial);

	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	const char *serial_device, *serial_options;

	serial_device = NULL;
	serial_options = "9600/8n1";

	(void)sr_serial_extract_options(options, &serial_device, &serial_options);
	if (!serial_device || !*serial_device)
		return NULL;

	return atorch_scan(di, serial_device, serial_options);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi || !data)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_FRAMES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	default:
		return SR_ERR_NA;
	}
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)data;
	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_FRAMES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_set(&devc->limits, key, data);
	default:
		return SR_ERR_NA;
	}
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;
	struct dev_context *devc;

	serial = sdi->conn;
	devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	serial_source_add(sdi->session, serial, G_IO_IN, 100,
		atorch_receive_data_callback, (void *)sdi);

	return SR_OK;
}

static void clear_helper(struct dev_context *devc)
{
	size_t idx;

	if (!devc)
		return;

	if (devc->feeds && devc->profile) {
		for (idx = 0; idx < devc->profile->channel_count; idx++)
			feed_queue_analog_free(devc->feeds[idx]);
		g_free(devc->feeds);
	}
}

static int dev_clear(const struct sr_dev_driver *driver)
{
	return std_dev_clear_with_callback(driver, (std_dev_clear_callback)clear_helper);
}

static struct sr_dev_driver atorch_driver_info = {
	.name = "atorch",
	.longname = "atorch meters and loads",
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
SR_REGISTER_DEV_DRIVER(atorch_driver_info);
