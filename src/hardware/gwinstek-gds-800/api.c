/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Richard Allen <rsaxvc@rsaxvc.net>
 * Copyright (C) 2015 Martin Lederhilger <martin.lederhilger@gmx.at>
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
#include <math.h>
#include "protocol.h"

#define IDN_RETRIES 3 /* at least 2 */

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
};

static const uint32_t devopts_cg_analog[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};


static const struct {
	uint16_t num;
	uint16_t denom;
	const char * nr3;
} vdivs[] = {
	/* millivolts */
/*
These are only available with 1x probe configuration.
TODO: fetch probe config programmatically.
	{   2, 1000, "0.002" },
	{   5, 1000, "0.005" },
	{  10, 1000, "0.01" },
*/
	{  20, 1000, "0.02" },
	{  50, 1000, "0.05" },
	{ 100, 1000, "0.1" },
	{ 200, 1000, "0.2" },
	{ 500, 1000, "0.5" },
	/* volts */
	{   1,    1, "1" },
	{   2,    1, "2" },
	{   5,    1, "5" },
};

static struct sr_dev_driver gwinstek_gds_800_driver_info;
static struct sr_dev_driver gwinstek_gds_2000_driver_info;

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi, struct sr_dev_driver *driver_info)
{
	char command[20];
	gboolean channel_enabled;
	struct sr_channel *ch;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_scpi_hw_info *hw_info;
	int i;

	/*
	 * If there is already data in the GDS receive buffer,
	 * the first SCPI IDN may fail, so try a few times
	 */
	for (i = 0; i < IDN_RETRIES; i++) {
		/* Request the GDS to identify itself */
		if (sr_scpi_get_hw_id(scpi, &hw_info) == SR_OK)
			break;
	}

	if (i == IDN_RETRIES) {
		sr_info("Couldn't get IDN response.");
		return NULL;
	}

	if (strcmp(hw_info->manufacturer, "GW") != 0 ||
		!(strncmp(hw_info->model, "GDS-8", 5) == 0 ||
		  strncmp(hw_info->model, "GDS-2", 5) == 0 )) {
		sr_scpi_hw_info_free(hw_info);
		return NULL;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor = g_strdup(hw_info->manufacturer);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->conn = scpi;
	sdi->driver = driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->channels = NULL;
	sdi->channel_groups = NULL;

	sr_scpi_hw_info_free(hw_info);

	devc = g_malloc0(sizeof(struct dev_context));
	devc->frame_limit = 1;
	devc->sample_rate = 0.0;
	devc->df_started = FALSE;
	if ((strncmp(sdi->model, "GDS-2", 5) == 0) &&
		(strlen(sdi->model) == 8) &&
		(sdi->model[7] == '4')) {
		devc->num_acq_channel = 4;
	} else {
		devc->num_acq_channel = 2;
	}
	sdi->priv = devc;

	devc->analog_groups = g_malloc0_n(devc->num_acq_channel,
		sizeof(struct sr_channel_group*));

	/* Add analog channels. */
	for (i = 0; i < devc->num_acq_channel; i++) {
		g_snprintf(command, sizeof(command), "CHANnel%d:DISPlay?", i + 1);

		if (sr_scpi_get_bool(scpi, command, &channel_enabled) != SR_OK)
			return NULL;

		gwinstek_gds_800_fetch_volts_per_div(sdi->conn, i, &devc->vdivs[i]);

		devc->analog_groups[i] = g_malloc0(sizeof(struct sr_channel_group));

		devc->analog_groups[i]->name = g_strdup_printf("CH%u", i + 1);
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, channel_enabled, devc->analog_groups[i]->name);
		devc->analog_groups[i]->channels = g_slist_append(NULL, ch);

		sdi->channel_groups = g_slist_append(sdi->channel_groups,
			devc->analog_groups[i]);
	}

	return sdi;
}

static struct sr_dev_inst *probe_device800(struct sr_scpi_dev_inst *scpi)
{
	return probe_device(scpi, &gwinstek_gds_800_driver_info);
}

static struct sr_dev_inst *probe_device2000(struct sr_scpi_dev_inst *scpi)
{
	return probe_device(scpi, &gwinstek_gds_2000_driver_info);
}

static GSList *scan800(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device800);
}

static GSList *scan2000(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device2000);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret;
	struct sr_scpi_dev_inst *scpi = sdi->conn;

	if ((ret = sr_scpi_open(scpi)) < 0) {
		sr_err("Failed to open SCPI device: %s.", sr_strerror(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;

	scpi = sdi->conn;

	if (!scpi)
		return SR_ERR_BUG;

	return sr_scpi_close(scpi);
}

static int vdiv_tuple_idx(GVariant *data)
{
	unsigned int i;
	uint64_t low, high;

	g_variant_get(data, "(tt)", &low, &high);

	for (i = 0; i < ARRAY_SIZE(vdivs); i++)
		if ((vdivs[i].num == low) && (vdivs[i].denom == high))
			return i;

	return -1;
}

static GVariant *vdiv_tuple_array(void)
{
    unsigned int i;
    GVariant *rational[2];
    GVariantBuilder gvb;

    g_variant_builder_init(&gvb, G_VARIANT_TYPE_TUPLE);

    for (i = 0; i < ARRAY_SIZE(vdivs); i++) {
        rational[0] = g_variant_new_uint64(vdivs[i].num);
        rational[1] = g_variant_new_uint64(vdivs[i].denom);

        /* FIXME: Valgrind reports a memory leak here. */
        g_variant_builder_add_value(&gvb, g_variant_new_tuple(rational, 2));
    }

    return g_variant_builder_end(&gvb);
}


static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int channel = -1;

	float smallest_diff = INFINITY;
	unsigned int i;
	int idx;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_NUM_VDIV:
		*data = g_variant_new_int32(ARRAY_SIZE(vdivs));
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->sample_rate);
		break;
	case SR_CONF_LIMIT_FRAMES:
		*data = g_variant_new_uint64(devc->frame_limit);
		break;
	case SR_CONF_VDIV:
		if ((channel = std_cg_idx(cg, devc->analog_groups, devc->num_acq_channel)) < 0) {
			sr_dbg("Negative channel: %d.", channel);
			return SR_ERR_ARG;
		}
		idx = -1;
		for (i = 0; i < ARRAY_SIZE(vdivs); i++) {
			float vdiv = (float)vdivs[i].num / vdivs[i].denom;
			float diff = fabsf(devc->vdivs[channel] - vdiv);
			if (diff < smallest_diff) {
				smallest_diff = diff;
				idx = i;
			}
		}
		if (idx < 0) {
			sr_dbg("Negative vdiv index: %d.", idx);
			return SR_ERR_NA;
		}
		*data = g_variant_new("(tt)", vdivs[idx].num, vdivs[idx].denom);
		break;

	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int i, idx, err;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_FRAMES:
		devc->frame_limit = g_variant_get_uint64(data);
		break;
	case SR_CONF_VDIV:
		if (!cg) {
			sr_err("No channel group specified");
			return SR_ERR_CHANNEL_GROUP;
		}
		if ((i = std_cg_idx(cg, devc->analog_groups, devc->num_acq_channel)) < 0) {
			sr_err("Unable to identify specified channel group");
			return SR_ERR_ARG;
		}
		if ((idx = vdiv_tuple_idx(data)) < 0) {
			sr_err("Unable to identify tuple index");
			return SR_ERR_ARG;
		}
		devc->vdivs[i] = (float)vdivs[idx].num / vdivs[idx].denom;
		err = sr_scpi_send(sdi->conn, ":CHANnel%i:SCALe %s", i + 1, vdivs[idx].nr3);
		if (SR_OK != err)
			sr_err("Failed to set VDIV.");
		return err;

	default:
		return SR_ERR_NA;
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
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
                default:
                        return SR_ERR_NA;
                }
	} else {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_analog));
			return SR_OK;
		case SR_CONF_VDIV:
			*data = vdiv_tuple_array();
			return SR_OK;
		default:
			return SR_ERR_NA;
		}
	}
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;

	scpi = sdi->conn;
	devc = sdi->priv;

	devc->state = START_ACQUISITION;
	devc->cur_acq_frame = 0;

	sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 50,
			gwinstek_gds_800_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;

	scpi = sdi->conn;
	devc = sdi->priv;

	if (devc->df_started) {
		std_session_send_df_frame_end(sdi);
		std_session_send_df_end(sdi);
		devc->df_started = FALSE;
	}

	sr_scpi_source_remove(sdi->session, scpi);

	return SR_OK;
}

static struct sr_dev_driver gwinstek_gds_800_driver_info = {
	.name = "gwinstek-gds-800",
	.longname = "GW Instek GDS-800 series",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan800,
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
SR_REGISTER_DEV_DRIVER(gwinstek_gds_800_driver_info);

static struct sr_dev_driver gwinstek_gds_2000_driver_info = {
	.name = "gwinstek-gds-2000",
	.longname = "GW Instek GDS-2000 series",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan2000,
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
SR_REGISTER_DEV_DRIVER(gwinstek_gds_2000_driver_info);
