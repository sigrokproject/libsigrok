/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2017,2019 Frank Stettner <frank-stettner@gmx.net>
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
#include <strings.h>
#include "scpi.h"
#include "protocol.h"

static struct sr_dev_driver scpi_pps_driver_info;
static struct sr_dev_driver hp_ib_pps_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_POWER_SUPPLY,
};

static const struct pps_channel_instance pci[] = {
	{ SR_MQ_VOLTAGE, SCPI_CMD_GET_MEAS_VOLTAGE, "V" },
	{ SR_MQ_CURRENT, SCPI_CMD_GET_MEAS_CURRENT, "I" },
	{ SR_MQ_POWER, SCPI_CMD_GET_MEAS_POWER, "P" },
	{ SR_MQ_FREQUENCY, SCPI_CMD_GET_MEAS_FREQUENCY, "F" },
};

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi,
		int (*get_hw_id)(struct sr_scpi_dev_inst *scpi,
		struct sr_scpi_hw_info **scpi_response))
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_scpi_hw_info *hw_info;
	struct sr_channel_group *cg;
	struct sr_channel *ch;
	const struct scpi_pps *device;
	struct pps_channel *pch;
	struct channel_spec *channels;
	struct channel_group_spec *channel_groups, *cgs;
	struct pps_channel_group *pcg;
	GRegex *model_re;
	GMatchInfo *model_mi;
	GSList *l;
	uint64_t mask;
	unsigned int num_channels, num_channel_groups, ch_num, ch_idx, i, j;
	int ret;
	const char *vendor;
	char ch_name[16];

	if (get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't get IDN response.");
		return NULL;
	}

	device = NULL;
	for (i = 0; i < num_pps_profiles; i++) {
		vendor = sr_vendor_alias(hw_info->manufacturer);
		if (g_ascii_strcasecmp(vendor, pps_profiles[i].vendor))
			continue;
		model_re = g_regex_new(pps_profiles[i].model, 0, 0, NULL);
		if (g_regex_match(model_re, hw_info->model, 0, &model_mi))
			device = &pps_profiles[i];
		g_match_info_unref(model_mi);
		g_regex_unref(model_re);
		if (device)
			break;
	}
	if (!device) {
		sr_scpi_hw_info_free(hw_info);
		return NULL;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor = g_strdup(vendor);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->conn = scpi;
	sdi->driver = &scpi_pps_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sdi->serial_num = g_strdup(hw_info->serial_number);

	devc = g_malloc0(sizeof(struct dev_context));
	devc->device = device;
	sr_sw_limits_init(&devc->limits);
	sdi->priv = devc;

	if (device->num_channels) {
		/* Static channels and groups. */
		channels = (struct channel_spec *)device->channels;
		num_channels = device->num_channels;
		channel_groups = (struct channel_group_spec *)device->channel_groups;
		num_channel_groups = device->num_channel_groups;
	} else {
		/* Channels and groups need to be probed. */
		ret = device->probe_channels(sdi, hw_info, &channels, &num_channels,
				&channel_groups, &num_channel_groups);
		if (ret != SR_OK) {
			sr_err("Failed to probe for channels.");
			return NULL;
		}
		/*
		 * Since these were dynamically allocated, we'll need to free them
		 * later.
		 */
		devc->channels = channels;
		devc->channel_groups = channel_groups;
	}

	ch_idx = 0;
	for (ch_num = 0; ch_num < num_channels; ch_num++) {
		/* Create one channel per measurable output unit. */
		for (i = 0; i < ARRAY_SIZE(pci); i++) {
			if (!sr_scpi_cmd_get(devc->device->commands, pci[i].command))
				continue;
			g_snprintf(ch_name, 16, "%s%s", pci[i].prefix,
					channels[ch_num].name);
			ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_ANALOG, TRUE,
					ch_name);
			pch = g_malloc0(sizeof(struct pps_channel));
			pch->hw_output_idx = ch_num;
			pch->hwname = channels[ch_num].name;
			pch->mq = pci[i].mq;
			ch->priv = pch;
		}
	}

	for (i = 0; i < num_channel_groups; i++) {
		cgs = &channel_groups[i];
		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup(cgs->name);
		for (j = 0, mask = 1; j < 64; j++, mask <<= 1) {
			if (cgs->channel_index_mask & mask) {
				for (l = sdi->channels; l; l = l->next) {
					ch = l->data;
					pch = ch->priv;
					/* Add mqflags from channel_group_spec only to voltage
					 * and current channels.
					 */
					if (pch->mq == SR_MQ_VOLTAGE || pch->mq == SR_MQ_CURRENT)
						pch->mqflags = cgs->mqflags;
					else
						pch->mqflags = 0;
					if (pch->hw_output_idx == j)
						cg->channels = g_slist_append(cg->channels, ch);
				}
			}
		}
		pcg = g_malloc0(sizeof(struct pps_channel_group));
		pcg->features = cgs->features;
		cg->priv = pcg;
		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	}

	sr_scpi_hw_info_free(hw_info);
	hw_info = NULL;

	/* Don't send SCPI_CMD_LOCAL for HP 66xxB using SCPI over GPIB. */
	if (!(devc->device->dialect == SCPI_DIALECT_HP_66XXB &&
			scpi->transport == SCPI_TRANSPORT_LIBGPIB))
		sr_scpi_cmd(sdi, devc->device->commands, 0, NULL, SCPI_CMD_LOCAL);

	return sdi;
}

static gchar *hpib_get_revision(struct sr_scpi_dev_inst *scpi)
{
	int ret;
	gboolean matches;
	char *response;
	GRegex *version_regex;

	ret = sr_scpi_get_string(scpi, "ROM?", &response);
	if (ret != SR_OK && !response)
		return NULL;

	/* Example version string: "B01 B01" */
	version_regex = g_regex_new("[A-Z][0-9]{2} [A-Z][0-9]{2}", 0, 0, NULL);
	matches = g_regex_match(version_regex, response, 0, NULL);
	g_regex_unref(version_regex);

	if (!matches) {
		/* Not a valid version string. Ignore it. */
		g_free(response);
		response = NULL;
	} else {
		/* Replace space with dot. */
		response[3] = '.';
	}

	return response;
}

/*
 * This function assumes the response is in the form "HP<model_number>"
 *
 * HP made many GPIB (then called HP-IB) instruments before the SCPI command
 * set was introduced into the standard. We haven't seen any non-HP instruments
 * which respond to the "ID?" query, so assume all are HP for now.
 */
static int hpib_get_hw_id(struct sr_scpi_dev_inst *scpi,
			  struct sr_scpi_hw_info **scpi_response)
{
	int ret;
	char *response;
	struct sr_scpi_hw_info *hw_info;

	ret = sr_scpi_get_string(scpi, "ID?", &response);
	if ((ret != SR_OK) || !response)
		return SR_ERR;

	hw_info = g_malloc0(sizeof(struct sr_scpi_hw_info));

	*scpi_response = hw_info;
	hw_info->model = response;
	hw_info->firmware_version = hpib_get_revision(scpi);
	hw_info->manufacturer = g_strdup("HP");

	return SR_OK;
}

static struct sr_dev_inst *probe_scpi_pps_device(struct sr_scpi_dev_inst *scpi)
{
	return probe_device(scpi, sr_scpi_get_hw_id);
}

static struct sr_dev_inst *probe_hpib_pps_device(struct sr_scpi_dev_inst *scpi)
{
	return probe_device(scpi, hpib_get_hw_id);
}

static GSList *scan_scpi_pps(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_scpi_pps_device);
}

static GSList *scan_hpib_pps(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_hpib_pps_device);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	GVariant *beeper;

	scpi = sdi->conn;
	if (sr_scpi_open(scpi) < 0)
		return SR_ERR;

	devc = sdi->priv;

	/* Don't send SCPI_CMD_REMOTE for HP 66xxB using SCPI over GPIB. */
	if (!(devc->device->dialect == SCPI_DIALECT_HP_66XXB &&
			scpi->transport == SCPI_TRANSPORT_LIBGPIB))
		sr_scpi_cmd(sdi, devc->device->commands, 0, NULL, SCPI_CMD_REMOTE);

	devc->beeper_was_set = FALSE;
	if (sr_scpi_cmd_resp(sdi, devc->device->commands, 0, NULL,
			&beeper, G_VARIANT_TYPE_BOOLEAN, SCPI_CMD_BEEPER) == SR_OK) {
		if (g_variant_get_boolean(beeper)) {
			devc->beeper_was_set = TRUE;
			sr_scpi_cmd(sdi, devc->device->commands,
				0, NULL, SCPI_CMD_BEEPER_DISABLE);
		}
		g_variant_unref(beeper);
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;

	devc = sdi->priv;
	scpi = sdi->conn;

	if (!scpi)
		return SR_ERR_BUG;

	if (devc->beeper_was_set)
		sr_scpi_cmd(sdi, devc->device->commands,
			0, NULL, SCPI_CMD_BEEPER_ENABLE);

	/* Don't send SCPI_CMD_LOCAL for HP 66xxB using SCPI over GPIB. */
	if (!(devc->device->dialect == SCPI_DIALECT_HP_66XXB &&
			scpi->transport == SCPI_TRANSPORT_LIBGPIB))
		sr_scpi_cmd(sdi, devc->device->commands, 0, NULL, SCPI_CMD_LOCAL);

	return sr_scpi_close(scpi);
}

static void clear_helper(struct dev_context *devc)
{
	g_free(devc->channels);
	g_free(devc->channel_groups);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	const GVariantType *gvtype;
	unsigned int i;
	int channel_group_cmd;
	char *channel_group_name;
	int cmd, ret;
	const char *s;
	int reg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if (cg) {
		/*
		 * These options only apply to channel groups with a single
		 * channel -- they're per-channel settings for the device.
		 */

		/*
		 * Config keys are handled below depending on whether a channel
		 * group was provided by the frontend. However some of these
		 * take a CG on one PPS but not on others. Check the device's
		 * profile for that here, and NULL out the channel group as needed.
		 */
		for (i = 0; i < devc->device->num_devopts; i++) {
			if (devc->device->devopts[i] == key) {
				cg = NULL;
				break;
			}
		}
	}

	gvtype = NULL;
	cmd = -1;
	switch (key) {
	case SR_CONF_ENABLED:
		gvtype = G_VARIANT_TYPE_BOOLEAN;
		cmd = SCPI_CMD_GET_OUTPUT_ENABLED;
		break;
	case SR_CONF_VOLTAGE:
		gvtype = G_VARIANT_TYPE_DOUBLE;
		cmd = SCPI_CMD_GET_MEAS_VOLTAGE;
		break;
	case SR_CONF_VOLTAGE_TARGET:
		gvtype = G_VARIANT_TYPE_DOUBLE;
		cmd = SCPI_CMD_GET_VOLTAGE_TARGET;
		break;
	case SR_CONF_OUTPUT_FREQUENCY:
		gvtype = G_VARIANT_TYPE_DOUBLE;
		cmd = SCPI_CMD_GET_MEAS_FREQUENCY;
		break;
	case SR_CONF_OUTPUT_FREQUENCY_TARGET:
		gvtype = G_VARIANT_TYPE_DOUBLE;
		cmd = SCPI_CMD_GET_FREQUENCY_TARGET;
		break;
	case SR_CONF_CURRENT:
		gvtype = G_VARIANT_TYPE_DOUBLE;
		cmd = SCPI_CMD_GET_MEAS_CURRENT;
		break;
	case SR_CONF_CURRENT_LIMIT:
		gvtype = G_VARIANT_TYPE_DOUBLE;
		cmd = SCPI_CMD_GET_CURRENT_LIMIT;
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
		gvtype = G_VARIANT_TYPE_BOOLEAN;
		cmd = SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ENABLED;
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE:
		if (devc->device->dialect == SCPI_DIALECT_HP_66XXB ||
			devc->device->dialect == SCPI_DIALECT_HP_COMP)
			gvtype = G_VARIANT_TYPE_STRING;
		else
			gvtype = G_VARIANT_TYPE_BOOLEAN;
		cmd = SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ACTIVE;
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		gvtype = G_VARIANT_TYPE_DOUBLE;
		cmd = SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_THRESHOLD;
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		gvtype = G_VARIANT_TYPE_BOOLEAN;
		cmd = SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ENABLED;
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE:
		if (devc->device->dialect == SCPI_DIALECT_HP_66XXB ||
			devc->device->dialect == SCPI_DIALECT_HP_COMP)
			gvtype = G_VARIANT_TYPE_STRING;
		else
			gvtype = G_VARIANT_TYPE_BOOLEAN;
		cmd = SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ACTIVE;
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		gvtype = G_VARIANT_TYPE_DOUBLE;
		cmd = SCPI_CMD_GET_OVER_CURRENT_PROTECTION_THRESHOLD;
		break;
	case SR_CONF_OVER_TEMPERATURE_PROTECTION:
		gvtype = G_VARIANT_TYPE_BOOLEAN;
		cmd = SCPI_CMD_GET_OVER_TEMPERATURE_PROTECTION;
		break;
	case SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE:
		if (devc->device->dialect == SCPI_DIALECT_HP_66XXB ||
			devc->device->dialect == SCPI_DIALECT_HP_COMP)
			gvtype = G_VARIANT_TYPE_STRING;
		else
			gvtype = G_VARIANT_TYPE_BOOLEAN;
		cmd = SCPI_CMD_GET_OVER_TEMPERATURE_PROTECTION_ACTIVE;
		break;
	case SR_CONF_REGULATION:
		gvtype = G_VARIANT_TYPE_STRING;
		cmd = SCPI_CMD_GET_OUTPUT_REGULATION;
		break;
	default:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	}
	if (!gvtype)
		return SR_ERR_NA;

	channel_group_cmd = 0;
	channel_group_name = NULL;
	if (cg) {
		channel_group_cmd = SCPI_CMD_SELECT_CHANNEL;
		channel_group_name = g_strdup(cg->name);
	}

	ret = sr_scpi_cmd_resp(sdi, devc->device->commands,
		channel_group_cmd, channel_group_name, data, gvtype, cmd);
	g_free(channel_group_name);

	/*
	 * Handle special cases
	 */

	if (cmd == SCPI_CMD_GET_OUTPUT_REGULATION) {
		if (devc->device->dialect == SCPI_DIALECT_PHILIPS) {
			/*
			* The Philips PM2800 series returns VOLT/CURR. We always return
			* a GVariant string in the Rigol notation (CV/CC/UR).
			*/
			s = g_variant_get_string(*data, NULL);
			if (!g_strcmp0(s, "VOLT")) {
				g_variant_unref(*data);
				*data = g_variant_new_string("CV");
			} else if (!g_strcmp0(s, "CURR")) {
				g_variant_unref(*data);
				*data = g_variant_new_string("CC");
			}
		}
		if (devc->device->dialect == SCPI_DIALECT_HP_COMP) {
			/* Evaluate Status Register from a HP 66xx in COMP mode. */
			s = g_variant_get_string(*data, NULL);
			sr_atoi(s, &reg);
			g_variant_unref(*data);
			if (reg & (1 << 0))
				*data = g_variant_new_string("CV");
			else if (reg & (1 << 1))
				*data = g_variant_new_string("CC");
			else if (reg & (1 << 2))
				*data = g_variant_new_string("UR");
			else if (reg & (1 << 9))
				*data = g_variant_new_string("CC-");
			else
				*data = g_variant_new_string("");
		}
		if (devc->device->dialect == SCPI_DIALECT_HP_66XXB) {
			/* Evaluate Operational Status Register from a HP 66xxB. */
			s = g_variant_get_string(*data, NULL);
			sr_atoi(s, &reg);
			g_variant_unref(*data);
			if (reg & (1 << 8))
				*data = g_variant_new_string("CV");
			else if (reg & (1 << 10))
				*data = g_variant_new_string("CC");
			else if (reg & (1 << 11))
				*data = g_variant_new_string("CC-");
			else
				*data = g_variant_new_string("UR");
		}

		s = g_variant_get_string(*data, NULL);
		if (g_strcmp0(s, "CV") && g_strcmp0(s, "CC") && g_strcmp0(s, "CC-") &&
			g_strcmp0(s, "UR") && g_strcmp0(s, "")) {

			sr_err("Unknown response to SCPI_CMD_GET_OUTPUT_REGULATION: %s", s);
			ret = SR_ERR_DATA;
		}
	}

	if (cmd == SCPI_CMD_GET_OVER_VOLTAGE_PROTECTION_ACTIVE) {
		if (devc->device->dialect == SCPI_DIALECT_HP_COMP) {
			/* Evaluate Status Register from a HP 66xx in COMP mode. */
			s = g_variant_get_string(*data, NULL);
			sr_atoi(s, &reg);
			g_variant_unref(*data);
			*data = g_variant_new_boolean(reg & (1 << 3));
		}
		if (devc->device->dialect == SCPI_DIALECT_HP_66XXB) {
			/* Evaluate Questionable Status Register bit 0 from a HP 66xxB. */
			s = g_variant_get_string(*data, NULL);
			sr_atoi(s, &reg);
			g_variant_unref(*data);
			*data = g_variant_new_boolean(reg & (1 << 0));
		}
	}

	if (cmd == SCPI_CMD_GET_OVER_CURRENT_PROTECTION_ACTIVE) {
		if (devc->device->dialect == SCPI_DIALECT_HP_COMP) {
			/* Evaluate Status Register from a HP 66xx in COMP mode. */
			s = g_variant_get_string(*data, NULL);
			sr_atoi(s, &reg);
			g_variant_unref(*data);
			*data = g_variant_new_boolean(reg & (1 << 6));
		}
		if (devc->device->dialect == SCPI_DIALECT_HP_66XXB) {
			/* Evaluate Questionable Status Register bit 1 from a HP 66xxB. */
			s = g_variant_get_string(*data, NULL);
			sr_atoi(s, &reg);
			g_variant_unref(*data);
			*data = g_variant_new_boolean(reg & (1 << 1));
		}
	}

	if (cmd == SCPI_CMD_GET_OVER_TEMPERATURE_PROTECTION_ACTIVE) {
		if (devc->device->dialect == SCPI_DIALECT_HP_COMP) {
			/* Evaluate Status Register from a HP 66xx in COMP mode. */
			s = g_variant_get_string(*data, NULL);
			sr_atoi(s, &reg);
			g_variant_unref(*data);
			*data = g_variant_new_boolean(reg & (1 << 4));
		}
		if (devc->device->dialect == SCPI_DIALECT_HP_66XXB) {
			/* Evaluate Questionable Status Register bit 4 from a HP 66xxB. */
			s = g_variant_get_string(*data, NULL);
			sr_atoi(s, &reg);
			g_variant_unref(*data);
			*data = g_variant_new_boolean(reg & (1 << 4));
		}
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	double d;
	int channel_group_cmd;
	char *channel_group_name;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;

	channel_group_cmd = 0;
	channel_group_name = NULL;
	if (cg) {
		channel_group_cmd = SCPI_CMD_SELECT_CHANNEL;
		channel_group_name = g_strdup(cg->name);
	}

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_ENABLED:
		if (g_variant_get_boolean(data))
			ret = sr_scpi_cmd(sdi, devc->device->commands,
					channel_group_cmd, channel_group_name,
					SCPI_CMD_SET_OUTPUT_ENABLE);
		else
			ret = sr_scpi_cmd(sdi, devc->device->commands,
					channel_group_cmd, channel_group_name,
					SCPI_CMD_SET_OUTPUT_DISABLE);
		break;
	case SR_CONF_VOLTAGE_TARGET:
		d = g_variant_get_double(data);
		ret = sr_scpi_cmd(sdi, devc->device->commands,
				channel_group_cmd, channel_group_name,
				SCPI_CMD_SET_VOLTAGE_TARGET, d);
		break;
	case SR_CONF_OUTPUT_FREQUENCY_TARGET:
		d = g_variant_get_double(data);
		ret = sr_scpi_cmd(sdi, devc->device->commands,
				channel_group_cmd, channel_group_name,
				SCPI_CMD_SET_FREQUENCY_TARGET, d);
		break;
	case SR_CONF_CURRENT_LIMIT:
		d = g_variant_get_double(data);
		ret = sr_scpi_cmd(sdi, devc->device->commands,
				channel_group_cmd, channel_group_name,
				SCPI_CMD_SET_CURRENT_LIMIT, d);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED:
		if (g_variant_get_boolean(data))
			ret = sr_scpi_cmd(sdi, devc->device->commands,
					channel_group_cmd, channel_group_name,
					SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_ENABLE);
		else
			ret = sr_scpi_cmd(sdi, devc->device->commands,
					channel_group_cmd, channel_group_name,
					SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_DISABLE);
		break;
	case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
		d = g_variant_get_double(data);
		ret = sr_scpi_cmd(sdi, devc->device->commands,
				channel_group_cmd, channel_group_name,
				SCPI_CMD_SET_OVER_VOLTAGE_PROTECTION_THRESHOLD, d);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
		if (g_variant_get_boolean(data))
			ret = sr_scpi_cmd(sdi, devc->device->commands,
					channel_group_cmd, channel_group_name,
					SCPI_CMD_SET_OVER_CURRENT_PROTECTION_ENABLE);
		else
			ret = sr_scpi_cmd(sdi, devc->device->commands,
					channel_group_cmd, channel_group_name,
					SCPI_CMD_SET_OVER_CURRENT_PROTECTION_DISABLE);
		break;
	case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
		d = g_variant_get_double(data);
		ret = sr_scpi_cmd(sdi, devc->device->commands,
				channel_group_cmd, channel_group_name,
				SCPI_CMD_SET_OVER_CURRENT_PROTECTION_THRESHOLD, d);
		break;
	case SR_CONF_OVER_TEMPERATURE_PROTECTION:
		if (g_variant_get_boolean(data))
			ret = sr_scpi_cmd(sdi, devc->device->commands,
					channel_group_cmd, channel_group_name,
					SCPI_CMD_SET_OVER_TEMPERATURE_PROTECTION_ENABLE);
		else
			ret = sr_scpi_cmd(sdi, devc->device->commands,
					channel_group_cmd, channel_group_name,
					SCPI_CMD_SET_OVER_TEMPERATURE_PROTECTION_DISABLE);
		break;
	default:
		ret = sr_sw_limits_config_set(&devc->limits, key, data);
	}

	g_free(channel_group_name);

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	struct pps_channel *pch;
	const struct channel_spec *ch_spec;
	int i;
	const char *s[16];

	devc = (sdi) ? sdi->priv : NULL;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return std_opts_config_list(key, data, sdi, cg,
				ARRAY_AND_SIZE(scanopts),
				ARRAY_AND_SIZE(drvopts),
				(devc && devc->device) ? devc->device->devopts : NULL,
				(devc && devc->device) ? devc->device->num_devopts : 0);
			break;
		case SR_CONF_CHANNEL_CONFIG:
			if (!devc || !devc->device)
				return SR_ERR_ARG;
			/* Not used. */
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
		/*
		 * Per-channel-group options depending on a channel are actually
		 * done with the first channel. Channel groups in PPS can have
		 * more than one channel, but they will typically be of equal
		 * specification for use in series or parallel mode.
		 */
		ch = cg->channels->data;
		pch = ch->priv;
		if (!devc || !devc->device)
			return SR_ERR_ARG;
		ch_spec = &(devc->device->channels[pch->hw_output_idx]);

		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(devc->device->devopts_cg, devc->device->num_devopts_cg);
			break;
		case SR_CONF_VOLTAGE_TARGET:
			*data = std_gvar_min_max_step_array(ch_spec->voltage);
			break;
		case SR_CONF_OUTPUT_FREQUENCY_TARGET:
			*data = std_gvar_min_max_step_array(ch_spec->frequency);
			break;
		case SR_CONF_CURRENT_LIMIT:
			*data = std_gvar_min_max_step_array(ch_spec->current);
			break;
		case SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD:
			*data = std_gvar_min_max_step_array(ch_spec->ovp);
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
			*data = std_gvar_min_max_step_array(ch_spec->ocp);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	int ret;

	devc = sdi->priv;
	scpi = sdi->conn;

	/* Prime the pipe with the first channel. */
	devc->cur_acquisition_channel = sr_next_enabled_channel(sdi, NULL);

	/* Device specific initialization before acquisition starts. */
	if (devc->device->init_acquisition)
		devc->device->init_acquisition(sdi);

	if ((ret = sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 10,
			scpi_pps_receive_data, (void *)sdi)) != SR_OK)
		return ret;
	std_session_send_df_header(sdi);
	sr_sw_limits_acquisition_start(&devc->limits);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;

	scpi = sdi->conn;

	sr_scpi_source_remove(sdi->session, scpi);

	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver scpi_pps_driver_info = {
	.name = "scpi-pps",
	.longname = "SCPI PPS",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan_scpi_pps,
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

static struct sr_dev_driver hp_ib_pps_driver_info = {
	.name = "hpib-pps",
	.longname = "HP-IB PPS",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan_hpib_pps,
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
SR_REGISTER_DEV_DRIVER(scpi_pps_driver_info);
SR_REGISTER_DEV_DRIVER(hp_ib_pps_driver_info);
