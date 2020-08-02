/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Francois Gervais <francoisgervais@gmail.com>
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
#include "tplink-hs.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_ENERGYMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
};

static GSList *tplink_hs_scan(struct sr_dev_driver *di, const char *conn)
{
	GSList *devices = NULL;
	struct dev_context *devc = NULL;
	struct sr_dev_inst *sdi = NULL;
	gchar **params;
	int i;

	params = g_strsplit(conn, "/", 0);
	if (!params || !params[1] || !params[2]) {
		sr_err("Invalid Parameters.");
		g_strfreev(params);
		return NULL;
	}
	if (g_ascii_strncasecmp(params[0], "tcp", 3)) {
		sr_err("Only TCP (tcp-raw) protocol is currently supported.");
		g_strfreev(params);
		return NULL;
	}

	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);
	devc->read_timeout = 1000 * 1000;
	devc->ops = &tplink_hs_dev_ops;
	devc->address = g_strdup(params[1]);
	devc->port = g_strdup(params[2]);
	g_strfreev(params);

	if (tplink_hs_probe(devc) != SR_OK) {
		sr_err("Failed to find a supported TP-Link HS device.");
		goto err;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("TP-Link");
	sdi->model = g_strdup(devc->dev_info.model);
	sdi->version = g_strdup(devc->dev_info.sw_ver);
	sdi->serial_num = g_strdup(devc->dev_info.device_id);
	sdi->priv = devc;

	for (i = 0; devc->dev_info.channels[i].name; i++)
		sr_channel_new(sdi, i, devc->dev_info.channels[i].type,
				TRUE, devc->dev_info.channels[i].name);

	devices = g_slist_append(devices, sdi);

	return std_scan_complete(di, devices);

err:
	g_free(devc);
	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config *src;
	const char *conn = NULL;

	for (GSList *l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (!conn)
		return NULL;

	return tplink_hs_scan(di, conn);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->ops->open(devc) != SR_OK)
		return SR_ERR;

	devc->pollfd.fd = devc->socket;
	devc->pollfd.events = G_IO_IN;
	devc->pollfd.revents = 0;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->ops->close(devc) != SR_OK)
		return SR_ERR;

	return SR_OK;
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
	struct dev_context *devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	sr_session_source_add(sdi->session, -1, 0, 250,
			tplink_hs_receive_data, (void *)sdi);

	sr_session_source_add_pollfd(sdi->session, &devc->pollfd,
		0, tplink_hs_receive_data,
		(void *)sdi);

	devc->ops->start(devc);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	devc->ops->stop(devc);

	sr_session_source_remove_pollfd(sdi->session, &devc->pollfd);
	sr_session_source_remove(sdi->session, -1);
	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver tplink_hs_driver_info = {
	.name = "tplink-hs",
	.longname = "TP-Link HS110 Wi-Fi Smart Plug with Energy Monitoring",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = NULL,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(tplink_hs_driver_info);
