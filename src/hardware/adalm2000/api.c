/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Analog Devices Inc.
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


static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
};

static const uint32_t devopts_cg_analog_group[] = {
};

static const uint32_t devopts_cg_analog_channel[] = {
};

static const uint32_t devopts_cg[] = {
};

static struct sr_dev_driver adalm2000_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;

	struct sr_channel_group *cg, *acg;
	struct sr_config *src;
	GSList *l, *devices;
	struct CONTEXT_INFO **devlist;
	unsigned int i, j, len;
	char *conn;
	char channel_name[16];
	char ip[30];
	gboolean ip_connection;

	conn = NULL;
	ip_connection = FALSE;

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = (char *) g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (conn) {
		if (strstr(conn, "tcp")) {
			strtok(conn, "/");
			snprintf(ip, 30, "ip:%s", strtok(NULL, "/"));
			ip_connection = TRUE;
		}
	}
	devices = NULL;

	len = sr_libm2k_context_get_all(&devlist);

	for (i = 0; i < len; i++) {
		struct CONTEXT_INFO *info = (struct CONTEXT_INFO *) devlist[i];

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		devc = g_malloc0(sizeof(struct dev_context));

		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup(info->manufacturer);
		sdi->model = g_strdup(info->product);
		sdi->serial_num = g_strdup(info->serial);
		sdi->connection_id = g_strdup(info->uri);
		if (ip_connection) {
			sdi->conn = g_strdup(ip);
		} else {
			sdi->conn = g_strdup(info->uri);
		}

		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup("Logic");

		for (j = 0; j < DEFAULT_NUM_LOGIC_CHANNELS; j++) {
			snprintf(channel_name, 16, "DIO%d", j);
			ch = sr_channel_new(sdi, j, SR_CHANNEL_LOGIC, TRUE,
					    channel_name);
			cg->channels = g_slist_append(cg->channels, ch);
		}
		sdi->channel_groups = g_slist_append(NULL, cg);

		acg = g_malloc0(sizeof(struct sr_channel_group));
		acg->name = g_strdup("Analog");
		sdi->channel_groups = g_slist_append(sdi->channel_groups, acg);

		for (j = 0; j < DEFAULT_NUM_ANALOG_CHANNELS; j++) {
			snprintf(channel_name, 16, "A%d", j);

			cg = g_malloc0(sizeof(struct sr_channel_group));
			cg->name = g_strdup(channel_name);

			ch = sr_channel_new(sdi, j,
					    SR_CHANNEL_ANALOG, TRUE,
					    channel_name);

			acg->channels = g_slist_append(acg->channels, ch);

			cg->channels = g_slist_append(cg->channels, ch);
			sdi->channel_groups = g_slist_append(
				sdi->channel_groups, cg);

		}
		devc->m2k = NULL;
		devc->logic_unitsize = 2;
		devc->buffersize = 1 << 16;
		sr_analog_init(&devc->packet, &devc->encoding, &devc->meaning,
			       &devc->spec, 6);

		devc->meaning.mq = SR_MQ_VOLTAGE;
		devc->meaning.unit = SR_UNIT_VOLT;
		devc->meaning.mqflags = 0;

		sdi->priv = devc;
		sdi->inst_type = SR_INST_USB;

		devices = g_slist_append(devices, sdi);
	}
	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	devc->m2k = sr_libm2k_context_open(sdi->conn);
	if (!devc->m2k) {
		sr_err("Failed to open device");
		return SR_ERR;
	}
	sr_libm2k_context_adc_calibrate(devc->m2k);

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	sr_info("Closing device on ...");
	ret = sr_libm2k_context_close(&(devc->m2k));
	if (ret) {
		sr_err("Failed to close device");
		return SR_ERR;
	}
	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
		       const struct sr_dev_inst *sdi,
		       const struct sr_channel_group *cg)
{
	struct sr_channel *ch;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg,
					       scanopts, drvopts,
					       devopts);

		default:
			return SR_ERR_NA;
		}
	} else {
		ch = cg->channels->data;

		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			if (ch->type == SR_CHANNEL_ANALOG) {
				if (strcmp(cg->name, "Analog") == 0) {
					*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_analog_group));
				} else {
					*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_analog_channel));
				}
			} else {
				*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
			}
			break;
		default:
			return SR_ERR_NA;
		}
	}
	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	/* TODO: configure hardware, reset acquisition state, set up
	 * callbacks and send header packet. */

	(void)sdi;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	/* TODO: stop acquisition. */

	(void)sdi;

	return SR_OK;
}

static struct sr_dev_driver adalm2000_driver_info = {
	.name = "adalm2000",
	.longname = "ADALM2000",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(adalm2000_driver_info);
