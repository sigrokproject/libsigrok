/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Frank Stettner <frank-stettner@gmx.net>
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
#include "scpi.h"
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIPLEXER,
};

static const uint32_t devopts[] = {
	/*
	 * TODO Enable/disable multiple channel groups at once.
	 * SR_CONF_ENABLED | SR_CONF_SET,
	 */
};

static const uint32_t devopts_cg[] = {
	SR_CONF_ENABLED | SR_CONF_SET,
};

static struct sr_dev_driver hp_59306a_driver_info;

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct channel_group_context *cgc;
	size_t idx, nr;
	struct sr_channel_group *cg;

	/*
	 * The device cannot get identified by means of SCPI queries.
	 * Neither shall non-SCPI requests get emitted before reliable
	 * identification of the device. Assume that we only get here
	 * when user specs led us to believe it's safe to communicate
	 * to the expected kind of device.
	 */

	sdi = g_malloc0(sizeof(*sdi));
	sdi->vendor = g_strdup("Hewlett-Packard");
	sdi->model = g_strdup("59306A");
	sdi->conn = scpi;
	sdi->driver = &hp_59306a_driver_info;
	sdi->inst_type = SR_INST_SCPI;

	devc = g_malloc0(sizeof(*devc));
	sdi->priv = devc;

	devc->channel_count = 6;
	for (idx = 0; idx < devc->channel_count; idx++) {
		nr = idx + 1;

		cg = g_malloc0(sizeof(*cg));
		cg->name = g_strdup_printf("CH%zu", nr);

		cgc = g_malloc0(sizeof(*cgc));
		cgc->number = nr;
		cg->priv = cgc;

		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	}

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	const char *conn;

	/* Only scan for a device when conn= was specified. */
	conn = NULL;
	(void)sr_serial_extract_options(options, &conn, NULL);
	if (!conn)
		return NULL;

	return sr_scpi_scan(di->context, options, probe_device);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	return sr_scpi_open(sdi->conn);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	return sr_scpi_close(sdi->conn);
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	gboolean on;

	if (!cg) {
		switch (key) {
		/* TODO: Enable/disbale multiple channel groups at once. */
		case SR_CONF_ENABLED:
		default:
			return SR_ERR_NA;
		}
	} else {
		switch (key) {
		case SR_CONF_ENABLED:
			on = g_variant_get_boolean(data);
			return hp_59306a_switch_cg(sdi, cg, on);
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg,
				scanopts, drvopts, devopts);
		default:
			return SR_ERR_NA;
		}
	} else {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static struct sr_dev_driver hp_59306a_driver_info = {
	.name = "hp-59306a",
	.longname = "hp-59306a",
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
	.dev_acquisition_start = std_dummy_dev_acquisition_start,
	.dev_acquisition_stop = std_dummy_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(hp_59306a_driver_info);
