/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Matthias Heidbrink <m-sigrok@heidbrink.biz>
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

#define SERIALCOMM "9600/8n1"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_POWER_SUPPLY,
};

static const uint32_t devopts[] = {
	SR_CONF_VOLTAGE | SR_CONF_SET,
	SR_CONF_CURRENT | SR_CONF_SET,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_SET,
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	GSList *l;
	const char *conn, *serialcomm;

	conn = serialcomm = NULL;

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
		serialcomm = SERIALCOMM;

	/*
	 * We cannot scan for this device because it is write-only.
	 * So just check that the port parameters are valid and assume that
	 * the device is there.
	 */

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	serial_flush(serial);
	serial_close(serial);

	sr_spew("Conrad DIGI 35 CPU assumed at %s.", conn);

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Conrad");
	sdi->model = g_strdup("DIGI 35 CPU");
	devc = g_malloc0(sizeof(struct dev_context));
	sr_sw_limits_init(&devc->limits);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->priv = devc;
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "CH1");

	return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	double dblval;

	(void)cg;

	switch (key) {
	case SR_CONF_VOLTAGE:
		dblval = g_variant_get_double(data);
		if ((dblval < 0.0) || (dblval > 35.0)) {
			sr_err("Voltage out of range (0 - 35.0)!");
			return SR_ERR_ARG;
		}
		return send_msg1(sdi, 'V', (int) (dblval * 10 + 0.5));
	case SR_CONF_CURRENT:
		dblval = g_variant_get_double(data);
		if ((dblval < 0.01) || (dblval > 2.55)) {
			sr_err("Current out of range (0 - 2.55)!");
			return SR_ERR_ARG;
		}
		return send_msg1(sdi, 'C', (int) (dblval * 100 + 0.5));
	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		if (g_variant_get_boolean(data))
			return send_msg1(sdi, 'V', 900);
		else /* Constant current mode */
			return send_msg1(sdi, 'V', 901);
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
}

static struct sr_dev_driver conrad_digi_35_cpu_driver_info = {
	.name = "conrad-digi-35-cpu",
	.longname = "Conrad DIGI 35 CPU",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = NULL,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = std_dummy_dev_acquisition_start,
	.dev_acquisition_stop = std_dummy_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(conrad_digi_35_cpu_driver_info);
