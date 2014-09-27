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

#include <string.h>
#include <errno.h>
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

static const uint32_t devopts[] = {
	SR_CONF_POWER_SUPPLY,
	SR_CONF_CONTINUOUS,
	SR_CONF_OUTPUT_CHANNEL_CONFIG | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg[] = {
	SR_CONF_OUTPUT_VOLTAGE | SR_CONF_GET,
	SR_CONF_OUTPUT_VOLTAGE_MAX | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OUTPUT_CURRENT | SR_CONF_GET,
	SR_CONF_OUTPUT_CURRENT_MAX | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OUTPUT_ENABLED | SR_CONF_GET | SR_CONF_SET,
};

static const char *channel_modes[] = {
	"Independent",
	"Series",
	"Parallel",
};

static struct pps_model models[] = {
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


SR_PRIV struct sr_dev_driver atten_pps3203_driver_info;
static struct sr_dev_driver *di = &atten_pps3203_driver_info;

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(GSList *options, int modelid)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	struct sr_serial_dev_inst *serial;
	GSList *l, *devices;
	struct pps_model *model;
	uint8_t packet[PACKET_SIZE];
	unsigned int i;
	int ret;
	const char *conn, *serialcomm;
	char channel[10];

	devices = NULL;
	drvc = di->priv;
	drvc->instances = NULL;

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

	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;
	serial_flush(serial);

	/* This is how the vendor software channels for hardware. */
	memset(packet, 0, PACKET_SIZE);
	packet[0] = 0xaa;
	packet[1] = 0xaa;
	if (serial_write_blocking(serial, packet, PACKET_SIZE) == -1) {
		sr_err("Unable to write while probing for hardware: %s",
				strerror(errno));
		return NULL;
	}
	/* The device responds with a 24-byte packet when it receives a packet.
	 * At 9600 baud, 300ms is long enough for it to have arrived. */
	g_usleep(300 * 1000);
	memset(packet, 0, PACKET_SIZE);
	if ((ret = serial_read_nonblocking(serial, packet, PACKET_SIZE)) < 0) {
		sr_err("Unable to read while probing for hardware: %s",
				strerror(errno));
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

	sdi = sr_dev_inst_new(SR_ST_INACTIVE, "Atten", model->name, NULL);
	sdi->driver = di;
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	for (i = 0; i < MAX_CHANNELS; i++) {
		snprintf(channel, 10, "CH%d", i + 1);
		ch = sr_channel_new(i, SR_CHANNEL_ANALOG, TRUE, channel);
		sdi->channels = g_slist_append(sdi->channels, ch);
		cg = g_malloc(sizeof(struct sr_channel_group));
		cg->name = g_strdup(channel);
		cg->channels = g_slist_append(NULL, ch);
		cg->priv = NULL;
		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	}

	devc = g_malloc0(sizeof(struct dev_context));
	devc->model = model;
	devc->config = g_malloc0(sizeof(struct per_channel_config) * model->num_channels);
	sdi->priv = devc;
	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return devices;
}

static GSList *scan_3203(GSList *options)
{
	return scan(options, PPS_3203T_3S);
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int cleanup(void)
{
	return std_dev_clear(di, NULL);
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	int channel, ret;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	ret = SR_OK;
	if (!cg) {
		/* No channel group: global options. */
		switch (key) {
		case SR_CONF_OUTPUT_CHANNEL_CONFIG:
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
		case SR_CONF_OUTPUT_VOLTAGE:
			*data = g_variant_new_double(devc->config[channel].output_voltage_last);
			break;
		case SR_CONF_OUTPUT_VOLTAGE_MAX:
			*data = g_variant_new_double(devc->config[channel].output_voltage_max);
			break;
		case SR_CONF_OUTPUT_CURRENT:
			*data = g_variant_new_double(devc->config[channel].output_current_last);
			break;
		case SR_CONF_OUTPUT_CURRENT_MAX:
			*data = g_variant_new_double(devc->config[channel].output_current_max);
			break;
		case SR_CONF_OUTPUT_ENABLED:
			*data = g_variant_new_boolean(devc->config[channel].output_enabled);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return ret;
}

static int find_str(const char *str, const char **strings, int array_size)
{
	int idx, i;

	idx = -1;
	for (i = 0; i < array_size; i++) {
		if (!strcmp(str, strings[i])) {
			idx = i;
			break;
		}
	}

	return idx;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	gdouble dval;
	int channel, ret, ival;
	const char *sval;
	gboolean bval;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	ret = SR_OK;
	devc = sdi->priv;
	if (!cg) {
		/* No channel group: global options. */
		switch (key) {
		case SR_CONF_OUTPUT_CHANNEL_CONFIG:
			sval = g_variant_get_string(data, NULL);
			if ((ival = find_str(sval, channel_modes,
							ARRAY_SIZE(channel_modes))) == -1) {
				ret = SR_ERR_ARG;
				break;
			}
			if (devc->model->channel_modes && (1 << ival) == 0) {
				/* Not supported on this model. */
				ret = SR_ERR_ARG;
			}
			if (ival == devc->channel_mode_set)
				/* Nothing to do. */
				break;
			devc->channel_mode_set = ival;
			devc->config_dirty = TRUE;
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
			bval = g_variant_get_boolean(data);
			if (bval == devc->over_current_protection_set)
				/* Nothing to do. */
				break;
			devc->over_current_protection_set = bval;
			devc->config_dirty = TRUE;
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		/* Channel group specified: per-channel options. */
		/* We only ever have one channel per channel group in this driver. */
		ch = cg->channels->data;
		channel = ch->index;

		switch (key) {
		case SR_CONF_OUTPUT_VOLTAGE_MAX:
			dval = g_variant_get_double(data);
			if (dval < 0 || dval > devc->model->channels[channel].voltage[1])
				ret = SR_ERR_ARG;
			devc->config[channel].output_voltage_max = dval;
			devc->config_dirty = TRUE;
			break;
		case SR_CONF_OUTPUT_CURRENT_MAX:
			dval = g_variant_get_double(data);
			if (dval < 0 || dval > devc->model->channels[channel].current[1])
				ret = SR_ERR_ARG;
			devc->config[channel].output_current_max = dval;
			devc->config_dirty = TRUE;
			break;
		case SR_CONF_OUTPUT_ENABLED:
			bval = g_variant_get_boolean(data);
			if (bval == devc->config[channel].output_enabled_set)
				/* Nothing to do. */
				break;
			devc->config[channel].output_enabled_set = bval;
			devc->config_dirty = TRUE;
			break;
		default:
			ret = SR_ERR_NA;
		}
	}


	return ret;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	GVariant *gvar;
	GVariantBuilder gvb;
	int channel, ret, i;

	/* Always available, even without sdi. */
	if (key == SR_CONF_SCAN_OPTIONS) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		return SR_OK;
	}

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	ret = SR_OK;
	if (!cg) {
		/* No channel group: global options. */
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
			break;
		case SR_CONF_OUTPUT_CHANNEL_CONFIG:
			if (devc->model->channel_modes == CHANMODE_INDEPENDENT) {
				/* The 1-channel models. */
				*data = g_variant_new_strv(channel_modes, 1);
			} else {
				/* The other models support all modes. */
				*data = g_variant_new_strv(channel_modes, ARRAY_SIZE(channel_modes));
			}
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		/* Channel group specified: per-channel options. */
		if (!sdi)
			return SR_ERR_ARG;
		/* We only ever have one channel per channel group in this driver. */
		ch = cg->channels->data;
		channel = ch->index;

		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					devopts_cg, ARRAY_SIZE(devopts_cg), sizeof(uint32_t));
			break;
		case SR_CONF_OUTPUT_VOLTAGE_MAX:
			g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
			/* Min, max, step. */
			for (i = 0; i < 3; i++) {
				gvar = g_variant_new_double(devc->model->channels[channel].voltage[i]);
				g_variant_builder_add_value(&gvb, gvar);
			}
			*data = g_variant_builder_end(&gvb);
			break;
		case SR_CONF_OUTPUT_CURRENT_MAX:
			g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
			/* Min, max, step. */
			for (i = 0; i < 3; i++) {
				gvar = g_variant_new_double(devc->model->channels[channel].current[i]);
				g_variant_builder_add_value(&gvb, gvar);
			}
			*data = g_variant_builder_end(&gvb);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return ret;
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

static int dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	uint8_t packet[PACKET_SIZE];

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
	memset(devc->packet, 0x44, PACKET_SIZE);
	devc->packet_size = 0;

	devc->acquisition_running = TRUE;

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 50,
			atten_pps3xxx_receive_data, (void *)sdi);
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Send a "channel" configuration packet now. */
	memset(packet, 0, PACKET_SIZE);
	packet[0] = 0xaa;
	packet[1] = 0xaa;
	send_packet(sdi, packet);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
	devc->acquisition_running = FALSE;

	return SR_OK;
}

SR_PRIV struct sr_dev_driver atten_pps3203_driver_info = {
	.name = "atten-pps3203",
	.longname = "Atten PPS3203T-3S",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan_3203,
	.dev_list = dev_list,
	.dev_clear = NULL,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
