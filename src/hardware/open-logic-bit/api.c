/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2021 Ultra-Embedded <admin@ultra-embedded.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include "protocol.h"

struct olb_targets_s
{
	uint16_t vid;
	uint16_t pid;
	int      iface;
	int      num_channels; // -1 then query target
};

static struct olb_targets_s target_list[] = {
	{ 0x0403, 0x6014, INTERFACE_A, 24 }
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_TEST_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *channel_names[] = {
	"0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",  "9",  "10",  "11",  "12",  "13",  "14",  "15",
	"16",  "17",  "18",  "19",  "20",  "21",  "22",  "23",  "24",  "25",  "26",  "27",  "28",  "29",  "30",  "31"
};

static const char *test_mode[] = {
	"False", "True"
};

static const uint64_t samplerates[] = {
	SR_MHZ(1), SR_MHZ(10), SR_MHZ(25), SR_MHZ(50), SR_MHZ(100)
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
};

static void clear_helper(struct dev_context *devc)
{
	ftdi_free(devc->ftdic);
	g_free(devc->data_buf);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	ret = ftdi_set_interface(devc->ftdic, devc->dev_iface);
	if (ret < 0) {
		sr_err("Failed to set FTDI interface A (%d): %s", ret,
		       ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}

	ret = ftdi_usb_open_desc(devc->ftdic, devc->dev_vid, devc->dev_pid,
				 NULL, NULL);
	if (ret < 0) {
		sr_err("Failed to open device (%d): %s", ret,
		       ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}

	if ((ret = ftdi_usb_purge_buffers(devc->ftdic)) < 0) {
		sr_err("Failed to purge FTDI RX/TX buffers (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}

	ret = ftdi_set_bitmode(devc->ftdic, 0xff, BITMODE_RESET);
	if (ret < 0) {
		sr_err("Failed to reset the FTDI chip bitmode (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}

	ret = ftdi_set_bitmode(devc->ftdic, 0xff, BITMODE_SYNCFF);
	if (ret < 0) {
		sr_err("Failed to put FTDI chip into sync FIFO mode (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}

	ret = ftdi_set_latency_timer(devc->ftdic, 2);
	if (ret < 0) {
		sr_err("Failed to set FTDI latency timer (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}

	ret = ftdi_read_data_set_chunksize(devc->ftdic, 64 * 1024);
	if (ret < 0) {
		sr_err("Failed to set FTDI read data chunk size (%d): %s.",
		       ret, ftdi_get_error_string(devc->ftdic));
		goto err_dev_open_close_ftdic;
	}

	return SR_OK;

err_dev_open_close_ftdic:
	openlb_close(devc);

	return SR_ERR;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	return openlb_close(devc);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct olb_targets_s *target;
	int ret;
	int max_channels;
	unsigned int i;

	(void)options;

	devc = g_malloc0(sizeof(struct dev_context));

	/* Allocate memory for the incoming data. */
	devc->data_buf      = g_malloc0(DATA_BUF_SIZE * sizeof(uint32_t));
	devc->data_pos      = 0;
	devc->num_samples   = 0;
	devc->sample_rate   = SR_MHZ(100);
	devc->num_channels  = 32;
	devc->cfg_test_mode = 0;

	if (!(devc->ftdic = ftdi_new())) {
		sr_err("Failed to initialize libftdi.");
		goto err_free_sample_buf;
	}

	target = NULL;
	for (i = 0; i < ARRAY_SIZE(target_list); i++) {
		target = &target_list[i];

		/* Try and open device by vid/pid */
		ret = ftdi_usb_open_desc(devc->ftdic, target->vid, target->pid, NULL, NULL);

		/* All fine - we found a candidate */
		if (ret == 0) {
			sr_info("Found a candidate device for open-logic-bit (vid=%04x, pid=%04x)", 
					 target->vid, target->pid);
			ftdi_usb_close(devc->ftdic);
			break;
		}

		/* Not found, try next candidate */
		target = NULL;
	}

	/* Found a candidate device */
	if (target != NULL) {
		devc->dev_vid   = target->vid;
		devc->dev_pid   = target->pid;
		devc->dev_iface = target->iface;

		ret = ftdi_usb_open_desc(devc->ftdic, devc->dev_vid, devc->dev_pid, NULL, NULL);
		if (ret < 0) {
			/* Log errors, except for -3 ("device not found"). */
			if (ret != -3)
				sr_err("Failed to open device (%d): %s", ret,
				       ftdi_get_error_string(devc->ftdic));
			goto err_free_ftdic;
		}

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup("OpenLogicBit");
		sdi->model  = NULL;
		sdi->priv   = devc;

		ftdi_usb_close(devc->ftdic);

		/* Try and query number of channels supported on the device
		   unless explicit number of channels listed */
		max_channels = target->num_channels;
		if (max_channels == -1 && dev_open(sdi) == SR_OK) {
			max_channels = openlb_read_max_channels(sdi);
			if (max_channels < 0)
				sr_err("Failed to read number of supported device channels.");
			dev_close(sdi);
		}

		devc->num_channels = (uint32_t)max_channels;
		for (i = 0; i < devc->num_channels; i++)
			sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, channel_names[i]);

		return std_scan_complete(di, g_slist_append(NULL, sdi));
	}

err_free_ftdic:
	ftdi_free(devc->ftdic);
err_free_sample_buf:
	g_free(devc->data_buf);
	g_free(devc);

	return NULL;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	(void)cg;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->sample_rate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_TEST_MODE:
		*data = g_variant_new_string(test_mode[devc->cfg_test_mode]);
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
	int idx;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		devc->sample_rate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		devc->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_TEST_MODE:
		idx = std_str_idx(data, ARRAY_AND_SIZE(test_mode));
		if (idx < 0)
			return SR_ERR_ARG;
		devc->cfg_test_mode = idx;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, NO_OPTS, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_TEST_MODE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(test_mode));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc = sdi->priv;

	if (!devc->ftdic)
		return SR_ERR_BUG;

	/* Reset some device structure state. */
	devc->seq_num     = 1;
	devc->data_pos    = 0;
	devc->num_samples = 0;

	if (openlb_convert_triggers(sdi) != SR_OK) {
		sr_err("Failed to configure trigger.");
		return SR_ERR;
	}

	if ((ret = openlb_start_acquisition(devc)) < 0)
		return ret;

	std_session_send_df_header(sdi);

	/* Hook up a dummy handler to receive data from the device. */
	sr_session_source_add(sdi->session, -1, 0, 0, openlb_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	sr_session_source_remove(sdi->session, -1);
	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver openlb_driver_info = {
	.name = "openlb",
	.longname = "Open-Logic-Bit",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
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
SR_REGISTER_DEV_DRIVER(openlb_driver_info);
