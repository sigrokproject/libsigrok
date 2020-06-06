/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
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

/*
 * The default serial communication settings on the device are 9600
 * baud, 9 data bits. The 9th bit isn't actually used, and the vendor
 * software uses Mark parity to absorb the extra bit.
 *
 * Since 9 data bits is not a standard available in POSIX, we use two
 * stop bits to skip over the extra bit instead.
 */
#define SERIALCOMM "9600/8n2"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_POWER_SUPPLY,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_CHANNEL_CONFIG | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg[] = {
	SR_CONF_VOLTAGE | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CURRENT | SR_CONF_GET,
	SR_CONF_CURRENT_LIMIT | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const char *channel_modes[] = {
	"Independent",
	"Series",
	"Parallel",
};

static const struct pps_model models[] = {
	{ PPS_3203T_3S, "PPS3203T-3S",
		CHANMODE_INDEPENDENT | CHANMODE_SERIES | CHANMODE_PARALLEL,
		3,
		{
			/* Channel 1 */
			{ { 0, 32, 0.01 }, { 0, 3, 0.001 } },
			/* Channel 2 */
			{ { 0, 32, 0.01 }, { 0, 3, 0.001 } },
			/* Channel 3 */
			{ { 0, 6, 0.01 }, { 0, 3, 0.001 } },
		},
	},
};

static GSList *scan(struct sr_dev_driver *di, GSList *options, int modelid)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	struct sr_serial_dev_inst *serial;
	GSList *l;
	const struct pps_model *model;
	uint8_t packet[PACKET_SIZE];
	unsigned int i;
	int delay_ms, ret;
	const char *conn, *serialcomm;
	char channel[10];

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

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	/* This is how the vendor software scans for hardware. */
	memset(packet, 0, PACKET_SIZE);
	packet[0] = 0xaa;
	packet[1] = 0xaa;
	delay_ms = serial_timeout(serial, PACKET_SIZE);
	if (serial_write_blocking(serial, packet, PACKET_SIZE, delay_ms) < PACKET_SIZE) {
		sr_err("Unable to write while probing for hardware.");
		return NULL;
	}
	/* The device responds with a 24-byte packet when it receives a packet.
	 * At 9600 baud, 300ms is long enough for it to have arrived. */
	g_usleep(300 * 1000);
	memset(packet, 0, PACKET_SIZE);
	if ((ret = serial_read_nonblocking(serial, packet, PACKET_SIZE)) < 0) {
		sr_err("Unable to read while probing for hardware: %s",
				sr_strerror(ret));
		return NULL;
	}
	if (ret != PACKET_SIZE || packet[0] != 0xaa || packet[1] != 0xaa) {
		/* Doesn't look like an Atten PPS. */
		return NULL;
	}

	model = NULL;
	for (i = 0; i < ARRAY_SIZE(models); i++) {
		if (models[i].modelid == modelid) {
			model = &models[i];
			break;
		}
	}
	if (!model) {
		sr_err("Unknown modelid %d", modelid);
		return NULL;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("Atten");
	sdi->model = g_strdup(model->name);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	for (i = 0; i < MAX_CHANNELS; i++) {
		snprintf(channel, 10, "CH%d", i + 1);
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, channel);
		cg = g_malloc(sizeof(struct sr_channel_group));
		cg->name = g_strdup(channel);
		cg->channels = g_slist_append(NULL, ch);
		cg->priv = NULL;
		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	}

	devc = g_malloc0(sizeof(struct dev_context));
	devc->model = model;
	devc->config = g_malloc0(sizeof(struct per_channel_config) * model->num_channels);
	devc->delay_ms = delay_ms;
	sdi->priv = devc;

	serial_close(serial);

	return std_scan_complete(di, g_slist_append(NULL, sdi));
}

static GSList *scan_3203(struct sr_dev_driver *di, GSList *options)
{
	return scan(di, options, PPS_3203T_3S);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	int channel;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if (!cg) {
		switch (key) {
		case SR_CONF_CHANNEL_CONFIG:
			*data = g_variant_new_string(channel_modes[devc->channel_mode]);
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
			*data = g_variant_new_boolean(devc->over_current_protection);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		/* We only ever have one channel per channel group in this driver. */
		ch = cg->channels->data;
		channel = ch->index;

		switch (key) {
		case SR_CONF_VOLTAGE:
			*data = g_variant_new_double(devc->config[channel].output_voltage_last);
			break;
		case SR_CONF_VOLTAGE_TARGET:
			*data = g_variant_new_double(devc->config[channel].output_voltage_max);
			break;
		case SR_CONF_CURRENT:
			*data = g_variant_new_double(devc->config[channel].output_current_last);
			break;
		case SR_CONF_CURRENT_LIMIT:
			*data = g_variant_new_double(devc->config[channel].output_current_max);
			break;
		case SR_CONF_ENABLED:
			*data = g_variant_new_boolean(devc->config[channel].output_enabled);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	gdouble dval;
	int channel, ival;
	gboolean bval;

	devc = sdi->priv;

	if (!cg) {
		switch (key) {
		case SR_CONF_CHANNEL_CONFIG:
			if ((ival = std_str_idx(data, ARRAY_AND_SIZE(channel_modes))) < 0)
				return SR_ERR_ARG;
			if (devc->model->channel_modes && (1 << ival) == 0)
				return SR_ERR_ARG; /* Not supported on this model. */
			if (ival == devc->channel_mode_set)
				break; /* Nothing to do. */
			devc->channel_mode_set = ival;
			devc->config_dirty = TRUE;
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
			bval = g_variant_get_boolean(data);
			if (bval == devc->over_current_protection_set)
				break; /* Nothing to do. */
			devc->over_current_protection_set = bval;
			devc->config_dirty = TRUE;
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		/* We only ever have one channel per channel group in this driver. */
		ch = cg->channels->data;
		channel = ch->index;

		switch (key) {
		case SR_CONF_VOLTAGE_TARGET:
			dval = g_variant_get_double(data);
			if (dval < 0 || dval > devc->model->channels[channel].voltage[1])
				return SR_ERR_ARG;
			devc->config[channel].output_voltage_max = dval;
			devc->config_dirty = TRUE;
			break;
		case SR_CONF_CURRENT_LIMIT:
			dval = g_variant_get_double(data);
			if (dval < 0 || dval > devc->model->channels[channel].current[1])
				return SR_ERR_ARG;
			devc->config[channel].output_current_max = dval;
			devc->config_dirty = TRUE;
			break;
		case SR_CONF_ENABLED:
			bval = g_variant_get_boolean(data);
			if (bval == devc->config[channel].output_enabled_set)
				break; /* Nothing to do. */
			devc->config[channel].output_enabled_set = bval;
			devc->config_dirty = TRUE;
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	int channel;

	devc = (sdi) ? sdi->priv : NULL;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
		case SR_CONF_CHANNEL_CONFIG:
			if (!devc || !devc->model)
				return SR_ERR_ARG;
			if (devc->model->channel_modes == CHANMODE_INDEPENDENT) {
				/* The 1-channel models. */
				*data = g_variant_new_strv(channel_modes, 1);
			} else {
				/* The other models support all modes. */
				*data = g_variant_new_strv(ARRAY_AND_SIZE(channel_modes));
			}
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		/* We only ever have one channel per channel group in this driver. */
		ch = cg->channels->data;
		channel = ch->index;

		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
			break;
		case SR_CONF_VOLTAGE_TARGET:
			if (!devc || !devc->model)
				return SR_ERR_ARG;
			*data = std_gvar_min_max_step_array(devc->model->channels[channel].voltage);
			break;
		case SR_CONF_CURRENT_LIMIT:
			if (!devc || !devc->model)
				return SR_ERR_ARG;
			*data = std_gvar_min_max_step_array(devc->model->channels[channel].current);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (devc->config_dirty)
		/* Some configuration changes were queued up but didn't
		 * get sent to the device, likely because we were never
		 * in acquisition mode. Send them out now. */
		send_config(sdi);

	return std_serial_dev_close(sdi);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	uint8_t packet[PACKET_SIZE];

	devc = sdi->priv;
	memset(devc->packet, 0x44, PACKET_SIZE);
	devc->packet_size = 0;

	devc->acquisition_running = TRUE;

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 50,
			atten_pps3xxx_receive_data, (void *)sdi);
	std_session_send_df_header(sdi);

	/* Send a "channel" configuration packet now. */
	memset(packet, 0, PACKET_SIZE);
	packet[0] = 0xaa;
	packet[1] = 0xaa;
	send_packet(sdi, packet);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	devc->acquisition_running = FALSE;

	return SR_OK;
}

static struct sr_dev_driver atten_pps3203_driver_info = {
	.name = "atten-pps3203",
	.longname = "Atten PPS3203T-3S",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan_3203,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(atten_pps3203_driver_info);
