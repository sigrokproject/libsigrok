/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Eva Kissling <eva.kissling@bluewin.ch>
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

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t devopts[] = {
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

SR_PRIV struct sr_dev_driver ipdbg_la_driver_info;

static void ipdbg_la_split_addr_port(const char *conn, char **addr,
	char **port)
{
	char **strs = g_strsplit(conn, "/", 3);

	*addr = g_strdup(strs[1]);
	*port = g_strdup(strs[2]);

	g_strfreev(strs);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	GSList *devices;

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;
	const char *conn;
	struct sr_config *src;
	GSList *l;

	conn = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (!conn)
		return NULL;

	struct ipdbg_la_tcp *tcp = ipdbg_la_tcp_new();

	ipdbg_la_split_addr_port(conn, &tcp->address, &tcp->port);

	if (!tcp->address)
		return NULL;

	if (ipdbg_la_tcp_open(tcp) != SR_OK)
		return NULL;

	ipdbg_la_send_reset(tcp);
	ipdbg_la_send_reset(tcp);

	if (ipdbg_la_request_id(tcp) != SR_OK)
		return NULL;

	struct sr_dev_inst *sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("ipdbg.org");
	sdi->model = g_strdup("IPDBG LA");
	sdi->version = g_strdup("v1.0");
	sdi->driver = di;

	struct dev_context *devc = ipdbg_la_dev_new();
	sdi->priv = devc;

	ipdbg_la_get_addrwidth_and_datawidth(tcp, devc);

	sr_dbg("addr_width = %d, data_width = %d\n", devc->addr_width,
		devc->data_width);
	sr_dbg("limit samples = %" PRIu64 "\n", devc->limit_samples_max);

	for (uint32_t i = 0; i < devc->data_width; i++) {
		char *name = g_strdup_printf("CH%d", i);
		sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, name);
		g_free(name);
	}

	sdi->inst_type = SR_INST_USER;
	sdi->conn = tcp;

	ipdbg_la_tcp_close(tcp);

	devices = g_slist_append(devices, sdi);

	return std_scan_complete(di, devices);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	struct drv_context *drvc = di->context;
	struct sr_dev_inst *sdi;
	GSList *l;

	if (drvc) {
		for (l = drvc->instances; l; l = l->next) {
			sdi = l->data;
			struct ipdbg_la_tcp *tcp = sdi->conn;
			if (tcp) {
				ipdbg_la_tcp_close(tcp);
				ipdbg_la_tcp_free(tcp);
				g_free(tcp);
			}
			sdi->conn = NULL;
		}
	}

	return std_dev_clear(di);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct ipdbg_la_tcp *tcp = sdi->conn;

	if (!tcp)
		return SR_ERR;

	if (ipdbg_la_tcp_open(tcp) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	/* Should be called before a new call to scan(). */
	struct ipdbg_la_tcp *tcp = sdi->conn;

	if (tcp)
		ipdbg_la_tcp_close(tcp);

	sdi->conn = NULL;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;

	(void)cg;

	switch (key) {
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;

	(void)cg;

	switch (key) {
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		{
			uint64_t limit_samples = g_variant_get_uint64(data);
			if (limit_samples <= devc->limit_samples_max )
				devc->limit_samples = limit_samples;
		}
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct ipdbg_la_tcp *tcp = sdi->conn;
	struct dev_context *devc = sdi->priv;

	ipdbg_la_convert_trigger(sdi);
	ipdbg_la_send_trigger(devc, tcp);
	ipdbg_la_send_delay(devc, tcp);

	/* If the device stops sending for longer than it takes to send a byte,
	 * that means it's finished. But wait at least 100 ms to be safe.
	 */
	sr_session_source_add(sdi->session, tcp->socket, G_IO_IN, 100,
		ipdbg_la_receive_data, (struct sr_dev_inst *)sdi);

	ipdbg_la_send_start(tcp);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct ipdbg_la_tcp *tcp = sdi->conn;
	struct dev_context *devc = sdi->priv;

	uint8_t byte;

	if (devc->num_transfers > 0) {
		while (devc->num_transfers <
			(devc->limit_samples_max * devc->data_width_bytes)) {
			ipdbg_la_tcp_receive(tcp, &byte);
			devc->num_transfers++;
		}
	}

	ipdbg_la_send_reset(tcp);
	ipdbg_la_abort_acquisition(sdi);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver ipdbg_la_driver_info = {
	.name = "ipdbg-la",
	.longname = "IPDBG LA",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};

SR_REGISTER_DEV_DRIVER(ipdbg_la_driver_info);
