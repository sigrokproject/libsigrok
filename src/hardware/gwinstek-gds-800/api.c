/*
 * This file is part of the libsigrok project.
 *
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
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t devopts[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LIMIT_FRAMES | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET,
};

static struct sr_dev_driver gwinstek_gds_800_driver_info;

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_scpi_hw_info *hw_info;
	struct sr_channel_group *cg;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't get IDN response.");
		return NULL;
	}

	if (strcmp(hw_info->manufacturer, "GW") != 0 ||
	    strncmp(hw_info->model, "GDS-8", 5) != 0) {
		sr_scpi_hw_info_free(hw_info);
		return NULL;
	}

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor = g_strdup(hw_info->manufacturer);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->conn = scpi;
	sdi->driver = &gwinstek_gds_800_driver_info;
	sdi->inst_type = SR_INST_SCPI;
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->channels = NULL;
	sdi->channel_groups = NULL;

	sr_scpi_hw_info_free(hw_info);

	devc = g_malloc0(sizeof(struct dev_context));
	devc->frame_limit = 1;
	devc->sample_rate = 0.0;
	devc->df_started = FALSE;
	sdi->priv = devc;

	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "CH1");
	sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "CH2");

	cg = g_malloc0(sizeof(struct sr_channel_group));
	cg->name = g_strdup("");
	cg->channels = g_slist_append(cg->channels, g_slist_nth_data(sdi->channels, 0));
	cg->channels = g_slist_append(cg->channels, g_slist_nth_data(sdi->channels, 1));
	cg->priv = NULL;
	sdi->channel_groups = g_slist_append(NULL, cg);

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret;
	struct sr_scpi_dev_inst *scpi = sdi->conn;

	if ((ret = sr_scpi_open(scpi)) < 0) {
		sr_err("Failed to open SCPI device: %s.", sr_strerror(ret));
		return SR_ERR;
	}

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
		if (sr_scpi_close(scpi) < 0)
			return SR_ERR;
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->sample_rate);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_LIMIT_FRAMES:
		devc->frame_limit = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	(void)sdi;
	(void)cg;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			scanopts, ARRAY_SIZE(scanopts), sizeof(uint32_t));
		return SR_OK;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		return SR_OK;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;

	scpi = sdi->conn;
	devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

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
	struct sr_datafeed_packet packet;

	scpi = sdi->conn;
	devc = sdi->priv;

	if (devc->df_started) {
		packet.type = SR_DF_FRAME_END;
		sr_session_send(sdi, &packet);

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
	.scan = scan,
	.dev_list = std_dev_list,
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
