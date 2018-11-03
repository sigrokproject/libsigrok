/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2017 John Chajecki <subs@qcontinuum.plus.com>
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
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"
#include "protocol.h"

/*
 * This test violates the SCPI protocol, and confuses other devices.
 * Disable it for now, until a better location was found.
 */
#define ECHO_TEST 0

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_SET,
};

/* Vendor, model, number of channels, poll period */
static const struct fluke_scpi_dmm_model supported_models[] = {
	{ "FLUKE", "45", 2, 0 },
};

static struct sr_dev_driver fluke_45_driver_info;

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_scpi_hw_info *hw_info;
	const struct scpi_command *cmdset = fluke_45_cmdset;
	unsigned int i;
	const struct fluke_scpi_dmm_model *model = NULL;
	gchar *channel_name;
#if ECHO_TEST
	char *response;
#endif

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->conn = scpi;

#if ECHO_TEST
	/* Test for serial port ECHO enabled. */
	response = NULL;
	sr_scpi_get_string(scpi, "ECHO-TEST", &response);
	if (response && strcmp(response, "ECHO-TEST") == 0) {
		sr_err("Serial port ECHO is ON. Please turn it OFF!");
		return NULL;
	}
#endif

	/* Get device IDN. */
	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't get IDN response, retrying.");
		sr_scpi_close(scpi);
		sr_scpi_open(scpi);
		if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
			sr_info("Couldn't get IDN response.");
			return NULL;
		}
	}

	/* Check IDN. */
	for (i = 0; i < ARRAY_SIZE(supported_models); i++) {
		if (!g_ascii_strcasecmp(hw_info->manufacturer,
					supported_models[i].vendor) &&
				!strcmp(hw_info->model, supported_models[i].model)) {
			model = &supported_models[i];
			break;
		}
	}
	if (!model) {
		sr_scpi_hw_info_free(hw_info);
		return NULL;
	}

	/* Set up device parameters. */
	sdi->vendor = g_strdup(model->vendor);
	sdi->model = g_strdup(model->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->conn = scpi;
	sdi->driver = &fluke_45_driver_info;
	sdi->inst_type = SR_INST_SCPI;

	devc = g_malloc0(sizeof(struct dev_context));
	devc->num_channels = model->num_channels;
	devc->cmdset = cmdset;

	/* Create channels. */
	for (i = 0; i < devc->num_channels; i++) {
		channel_name = g_strdup_printf("P%d", i + 1);
		sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, channel_name);
	}

	sdi->priv = devc;

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	int ret;

	scpi = sdi->conn;

	if ((ret = sr_scpi_open(scpi) < 0)) {
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

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	devc = sdi->priv;

	return sr_sw_limits_config_set(&devc->limits, key, data);
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;

	(void)cg;

	return sr_sw_limits_config_get(&devc->limits, key, data);
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	int ret;

	scpi = sdi->conn;
	devc = sdi->priv;

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	if ((ret = sr_scpi_source_add(sdi->session, scpi, G_IO_IN, 10,
			fl45_scpi_receive_data, (void *)sdi)) != SR_OK)
		return ret;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;
	double d;

	scpi = sdi->conn;

	/*
	 * A requested value is certainly on the way. Retrieve it now,
	 * to avoid leaving the device in a state where it's not expecting
	 * commands.
	 */
	sr_scpi_get_double(scpi, NULL, &d);
	sr_scpi_source_remove(sdi->session, scpi);

	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver fluke_45_driver_info = {
	.name = "fluke-45",
	.longname = "Fluke 45",
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
SR_REGISTER_DEV_DRIVER(fluke_45_driver_info);
