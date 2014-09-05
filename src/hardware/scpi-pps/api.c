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
#include "protocol.h"

SR_PRIV struct sr_dev_driver scpi_pps_driver_info;
static struct sr_dev_driver *di = &scpi_pps_driver_info;
extern unsigned int num_pps_profiles;
extern const struct scpi_pps pps_profiles[];

static const int32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_scpi_hw_info *hw_info;
	struct sr_channel_group *cg;
	struct sr_channel *ch;
	const struct scpi_pps *device;
	const struct channel_group_spec *cgs;
	struct pps_channel_group *pcg;
	uint64_t mask;
	unsigned int i, j;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't get IDN response.");
		return NULL;
	}

	device = NULL;
	for (i = 0; i < num_pps_profiles; i++) {
		if (!strcasecmp(hw_info->manufacturer, pps_profiles[i].idn_vendor) &&
				!strcmp(hw_info->model, pps_profiles[i].idn_model)) {
			device = &pps_profiles[i];
			break;
		}
	}
	if (!device) {
		sr_scpi_hw_info_free(hw_info);
		return NULL;
	}

	sdi = sr_dev_inst_new(0, SR_ST_ACTIVE, device->vendor, device->idn_model,
			hw_info->firmware_version);
	sdi->conn = scpi;
	sdi->driver = di;
	sdi->inst_type = SR_INST_SCPI;
	devc = g_malloc0(sizeof(struct dev_context));
	devc->device = device;
	sdi->priv = devc;

	for (i = 0; i < device->num_channels; i++) {
		ch = sr_channel_new(i, SR_CHANNEL_ANALOG, TRUE,
				device->channels[i].name);
		sdi->channels = g_slist_append(sdi->channels, ch);
	}

	for (i = 0; i < device->num_channel_groups; i++) {
		cgs = &device->channel_groups[i];
		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup(cgs->name);
		for (j = 0, mask = 1; j < 64; j++, mask <<= 1) {
			if (cgs->channel_index_mask & mask) {
				ch = g_slist_nth_data(sdi->channels, j);
				cg->channels = g_slist_append(cg->channels, ch);
			}
		}
		pcg = g_malloc0(sizeof(struct pps_channel_group));
		pcg->features = cgs->features;
		cg->priv = pcg;
		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	}

	/* SCPI devices commonly lock the panel keys when accessed remotely. */
	scpi_cmd(sdi, SCPI_CMD_KEY_UNLOCK);
	sr_scpi_close(scpi);

	return sdi;
}

static GSList *scan(GSList *options)
{
	return sr_scpi_scan(di->priv, options, probe_device);
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int dev_clear(void)
{
	return std_dev_clear(di, NULL);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	scpi = sdi->conn;
	if (sr_scpi_open(scpi) < 0)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	scpi = sdi->conn;
	if (scpi) {
		scpi_cmd(sdi, SCPI_CMD_KEY_UNLOCK);
		sr_scpi_close(scpi);
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

static int cleanup(void)
{
	return SR_OK;
}

static int config_get(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	struct sr_channel *ch;
	double d;
	int ret;
	char *s;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	scpi = sdi->conn;

	ret = SR_OK;
	if (!cg) {
		/* No channel group: global options. */
		switch (key) {
		case SR_CONF_OVER_TEMPERATURE_PROTECTION:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_OVER_TEMPERATURE_PROTECTION) == SR_OK) {
				if (sr_scpi_get_string(scpi, NULL, &s) == SR_OK) {
					*data = g_variant_new_boolean(!strcmp(s, "ON"));
					ret = SR_OK;
				}
			}
			break;
			return SR_ERR_NA;
			break;
		case SR_CONF_OUTPUT_CHANNEL_CONFIG:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_OUTPUT_CHANNEL_CONFIG) == SR_OK) {
				if (sr_scpi_get_string(scpi, NULL, &s) == SR_OK) {
					*data = g_variant_new_string(s);
					g_free(s);
					ret = SR_OK;
				}
			}
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		/*
		 * These options only apply to channel groups with a single
		 * channel -- they're per-channel settings for the device.
		 */
		if (g_slist_length(cg->channels) > 1)
			return SR_ERR_NA;
		ch = cg->channels->data;

		switch (key) {
		case SR_CONF_OUTPUT_REGULATION:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_OUTPUT_REGULATION, ch->name) == SR_OK) {
				if (sr_scpi_get_string(scpi, NULL, &s) == SR_OK) {
					if (strcmp(s, "CC") && strcmp(s, "CV") && strcmp(s, "UR")) {
						sr_dbg("Unknown response to SCPI_CMD_GET_OUTPUT_REGULATION: %s", s);
					} else {
						*data = g_variant_new_string(s);
						g_free(s);
						ret = SR_OK;
					}
				}
			}
			break;
		case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ENABLED,
					ch->name) == SR_OK) {
				if (sr_scpi_get_string(scpi, NULL, &s) == SR_OK) {
					*data = g_variant_new_boolean(!strcmp(s, "ON"));
					ret = SR_OK;
				}
			}
			break;
		case SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ACTIVE,
					ch->name) == SR_OK) {
				if (sr_scpi_get_string(scpi, NULL, &s) == SR_OK) {
					*data = g_variant_new_boolean(!strcmp(s, "YES"));
					ret = SR_OK;
				}
			}
			break;
		case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_THRESHOLD,
					ch->name) == SR_OK) {
				if (sr_scpi_get_double(scpi, NULL, &d) == SR_OK) {
					*data = g_variant_new_double(d);
					ret = SR_OK;
				}
			}
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ENABLED,
					ch->name) == SR_OK) {
				if (sr_scpi_get_string(scpi, NULL, &s) == SR_OK) {
					*data = g_variant_new_boolean(!strcmp(s, "ON"));
					ret = SR_OK;
				}
			}
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ACTIVE,
					ch->name) == SR_OK) {
				if (sr_scpi_get_string(scpi, NULL, &s) == SR_OK) {
					*data = g_variant_new_boolean(!strcmp(s, "YES"));
					ret = SR_OK;
				}
			}
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_OVER_CURRENT_PROTECTION_THRESHOLD,
					ch->name) == SR_OK) {
				if (sr_scpi_get_double(scpi, NULL, &d) == SR_OK) {
					*data = g_variant_new_double(d);
					ret = SR_OK;
				}
			}
			break;
		case SR_CONF_OUTPUT_VOLTAGE:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_MEAS_VOLTAGE, ch->name) == SR_OK) {
				if (sr_scpi_get_double(scpi, NULL, &d) == SR_OK) {
					*data = g_variant_new_double(d);
					ret = SR_OK;
				}
			}
			break;
		case SR_CONF_OUTPUT_VOLTAGE_MAX:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_VOLTAGE_MAX, ch->name) == SR_OK) {
				if (sr_scpi_get_double(scpi, NULL, &d) == SR_OK) {
					*data = g_variant_new_double(d);
					ret = SR_OK;
				}
			}
			break;
		case SR_CONF_OUTPUT_CURRENT:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_MEAS_CURRENT, ch->name) == SR_OK) {
				if (sr_scpi_get_double(scpi, NULL, &d) == SR_OK) {
					*data = g_variant_new_double(d);
					ret = SR_OK;
				}
			}
			break;
		case SR_CONF_OUTPUT_CURRENT_MAX:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_CURRENT_MAX, ch->name) == SR_OK) {
				if (sr_scpi_get_double(scpi, NULL, &d) == SR_OK) {
					*data = g_variant_new_double(d);
					ret = SR_OK;
				}
			}
			break;
		case SR_CONF_OUTPUT_ENABLED:
			ret = SR_ERR;
			if (scpi_cmd(sdi, SCPI_CMD_GET_OUTPUT_ENABLED, ch->name) == SR_OK) {
				if (sr_scpi_get_string(scpi, NULL, &s) == SR_OK) {
					*data = g_variant_new_boolean(!strcmp(s, "ON"));
					ret = SR_OK;
				}
			}
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return ret;
}

static int config_set(int key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct sr_channel *ch;
	double d;
	int ret;
	const char *s;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;


	ret = SR_OK;
	if (!cg) {
		switch (key) {
		/* No channel group: global options. */
		case SR_CONF_OVER_TEMPERATURE_PROTECTION:
			s = g_variant_get_boolean(data) ? "ON" : "OFF";
			if (scpi_cmd(sdi, SCPI_CMD_SET_OVER_TEMPERATURE_PROTECTION, s) < 0)
				ret = SR_ERR;
			break;
		case SR_CONF_OUTPUT_CHANNEL_CONFIG:
			s = g_variant_get_string(data, NULL);
			if (scpi_cmd(sdi, SCPI_CMD_SET_OUTPUT_CHANNEL_CONFIG, s) < 0)
				ret = SR_ERR;
			break;
		default:
			ret = SR_ERR_NA;
		}
	} else {
		/* Channel group specified. */
		if (!sdi)
			return SR_ERR_ARG;
		if (g_slist_length(cg->channels) > 1)
			return SR_ERR_NA;
		ch = cg->channels->data;
		switch (key) {
		case SR_CONF_OUTPUT_VOLTAGE_MAX:
			d = g_variant_get_double(data);
			if (scpi_cmd(sdi, SCPI_CMD_SET_VOLTAGE_MAX, ch->name, d) < 0)
				ret = SR_ERR;
			break;
		case SR_CONF_OUTPUT_CURRENT_MAX:
			d = g_variant_get_double(data);
			if (scpi_cmd(sdi, SCPI_CMD_SET_CURRENT_MAX, ch->name, d) < 0)
				ret = SR_ERR;
			break;
		case SR_CONF_OUTPUT_ENABLED:
			s = g_variant_get_boolean(data) ? "ON" : "OFF";
			if (scpi_cmd(sdi, SCPI_CMD_SET_OUTPUT_ENABLED, ch->name, s) < 0)
				ret = SR_ERR;
			break;
		case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
			s = g_variant_get_boolean(data) ? "ON" : "OFF";
			if (scpi_cmd(sdi, SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_ENABLED,
					ch->name, s) < 0)
				ret = SR_ERR;
			break;
		case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
			d = g_variant_get_double(data);
			if (scpi_cmd(sdi, SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_THRESHOLD,
					ch->name, d) < 0)
				ret = SR_ERR;
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
			s = g_variant_get_boolean(data) ? "ON" : "OFF";
			if (scpi_cmd(sdi, SCPI_CMD_SET_OVER_CURRENT_PROTECTION_ENABLED,
					ch->name, s) < 0)
				ret = SR_ERR;
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
			d = g_variant_get_double(data);
			if (scpi_cmd(sdi, SCPI_CMD_SET_OVER_CURRENT_PROTECTION_THRESHOLD,
					ch->name, d) < 0)
				ret = SR_ERR;
			break;
		default:
			ret = SR_ERR_NA;
		}
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	struct channel_spec *ch_spec;
	GVariant *gvar;
	GVariantBuilder gvb;
	int ret, i;
	const char *s[16];

	/* Always available, even without sdi. */
	if (key == SR_CONF_SCAN_OPTIONS) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				scanopts, ARRAY_SIZE(scanopts), sizeof(int32_t));
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
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
					devc->device->devopts, devc->device->num_devopts,
					sizeof(int32_t));
			break;
		case SR_CONF_OUTPUT_CHANNEL_CONFIG:
			i = 0;
			if (devc->device->features & PPS_INDEPENDENT)
				s[i++] = "Independent";
			if (devc->device->features & PPS_SERIES)
				s[i++] = "Series";
			if (devc->device->features & PPS_PARALLEL)
				s[i++] = "Parallel";
			if (i == 0) {
				/*
				 * Shouldn't happen: independent-only devices
				 * shouldn't advertise this option at all.
				 */
				return SR_ERR_NA;
			}
			*data = g_variant_new_strv(s, i);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		/* Channel group specified. */
		if (!sdi)
			return SR_ERR_ARG;
		/*
		 * Per-channel-group options depending on a channel are actually
		 * done with the first channel. Channel groups in PPS can have
		 * more than one channel, but they will typically be of equal
		 * specification for use in series or parallel mode. Drop requests
		 * for groups with more than one channel just to make sure.
		 */
		if (g_slist_length(cg->channels) > 1)
			return SR_ERR_NA;
		ch = cg->channels->data;

		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
					devc->device->devopts_cg, devc->device->num_devopts_cg,
					sizeof(int32_t));
			break;
		case SR_CONF_OUTPUT_VOLTAGE_MAX:
			ch_spec = &(devc->device->channels[ch->index]);
			g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
			/* Min, max, write resolution. */
			for (i = 0; i < 3; i++) {
				gvar = g_variant_new_double(ch_spec->voltage[i]);
				g_variant_builder_add_value(&gvb, gvar);
			}
			*data = g_variant_builder_end(&gvb);
			break;
		case SR_CONF_OUTPUT_CURRENT_MAX:
			g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
			/* Min, max, step. */
			for (i = 0; i < 3; i++) {
				ch_spec = &(devc->device->channels[ch->index]);
				gvar = g_variant_new_double(ch_spec->current[i]);
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

static int dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	struct sr_channel *ch;
	int ret;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
	scpi = sdi->conn;
	devc->cb_data = cb_data;

	if ((ret = sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 100,
			scpi_pps_receive_data, (void *)sdi)) != SR_OK)
		return ret;
	std_session_send_df_header(sdi, LOG_PREFIX);

	/* Prime the pipe. */
	devc->state = STATE_VOLTAGE;
	ch = sdi->channels->data;
	devc->cur_channel = ch;
	scpi_cmd(sdi, SCPI_CMD_GET_MEAS_VOLTAGE, ch->name);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	float f;

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;
	scpi = sdi->conn;

	/*
	 * A requested value is certainly on the way. Retrieve it now,
	 * to avoid leaving the device in a state where it's not expecting
	 * commands.
	 */
	sr_scpi_get_float(scpi, NULL, &f);
	sr_scpi_source_remove(sdi->session, scpi);

	/* Just in case something is queued up. */
	devc->state = STATE_STOP;

	return SR_OK;
}

SR_PRIV struct sr_dev_driver scpi_pps_driver_info = {
	.name = "scpi-pps",
	.longname = "SCPI PPS",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
