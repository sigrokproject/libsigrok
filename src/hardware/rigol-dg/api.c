/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Timo Kokkonen <tjko@iki.fi>
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
#include "scpi.h"
#include "protocol.h"

static struct sr_dev_driver rigol_dg_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_SIGNAL_GENERATOR,
};

static const uint32_t dg1000z_devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t dg1000z_devopts_cg[] = {
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PATTERN_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OUTPUT_FREQUENCY | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_AMPLITUDE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OFFSET | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_PHASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DUTY_CYCLE | SR_CONF_GET | SR_CONF_SET,
};

static const double phase_min_max_step[] = { 0.0, 360.0, 0.001 };

#define WAVEFORM_DEFAULT WFO_FREQUENCY | WFO_AMPLITUDE | WFO_OFFSET | WFO_PHASE

static const struct waveform_spec dg810_waveforms[] = {
	{ "SIN",   WF_SINE,     1.0E-6,  10.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "SQU",   WF_SQUARE,   1.0E-6,   5.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "RAMP",  WF_RAMP,     1.0E-6,   0.2E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "PULSE", WF_PULSE,    1.0E-6,   5.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "USER",  WF_ARB,      1.0E-6,   5.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "NOISE", WF_NOISE,  100.0E+6, 100.0E+6, 0.0E-0, WFO_AMPLITUDE | WFO_OFFSET },
	{ "DC",    WF_DC,       0.0E-0,   0.0E+0, 0.0E-0, WFO_OFFSET },
};

static const struct channel_spec dg811_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(dg810_waveforms) },
};

static const struct channel_spec dg812_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(dg810_waveforms) },
	{ "CH2",  ARRAY_AND_SIZE(dg810_waveforms) },
};

static const struct waveform_spec dg820_waveforms[] = {
	{ "SIN",   WF_SINE,     1.0E-6,  25.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "SQU",   WF_SQUARE,   1.0E-6,  10.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "RAMP",  WF_RAMP,     1.0E-6,   0.5E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "PULSE", WF_PULSE,    1.0E-6,  10.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "USER",  WF_ARB,      1.0E-6,  10.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "NOISE", WF_NOISE,  100.0E+6, 100.0E+6, 0.0E-0, WFO_AMPLITUDE | WFO_OFFSET },
	{ "DC",    WF_DC,       0.0E-0,   0.0E+0, 0.0E-0, WFO_OFFSET },
};

static const struct channel_spec dg821_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(dg820_waveforms) },
};

static const struct channel_spec dg822_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(dg820_waveforms) },
	{ "CH2",  ARRAY_AND_SIZE(dg820_waveforms) },
};

static const struct waveform_spec dg830_waveforms[] = {
	{ "SIN",   WF_SINE,     1.0E-6,  35.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "SQU",   WF_SQUARE,   1.0E-6,  10.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "RAMP",  WF_RAMP,     1.0E-6,   1.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "PULSE", WF_PULSE,    1.0E-6,  10.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "USER",  WF_ARB,      1.0E-6,  10.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "NOISE", WF_NOISE,  100.0E+6, 100.0E+6, 0.0E-0, WFO_AMPLITUDE | WFO_OFFSET },
	{ "DC",    WF_DC,       0.0E-0,   0.0E+0, 0.0E-0, WFO_OFFSET },
};

static const struct channel_spec dg831_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(dg830_waveforms) },
};

static const struct channel_spec dg832_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(dg830_waveforms) },
	{ "CH2",  ARRAY_AND_SIZE(dg830_waveforms) },
};

static const struct waveform_spec dg952_waveforms[] = {
	{ "SIN",   WF_SINE,     1.0E-6,  50.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "SQU",   WF_SQUARE,   1.0E-6,  15.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "RAMP",  WF_RAMP,     1.0E-6,   1.5E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "PULSE", WF_PULSE,    1.0E-6,  15.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "USER",  WF_ARB,      1.0E-6,  15.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "NOISE", WF_NOISE,  100.0E+6, 100.0E+6, 0.0E-0, WFO_AMPLITUDE | WFO_OFFSET },
	{ "DC",    WF_DC,       0.0E-0,   0.0E+0, 0.0E-0, WFO_OFFSET },
};

static const struct channel_spec dg952_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(dg952_waveforms) },
	{ "CH2",  ARRAY_AND_SIZE(dg952_waveforms) },
};

static const struct waveform_spec dg972_waveforms[] = {
	{ "SIN",   WF_SINE,     1.0E-6,  70.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "SQU",   WF_SQUARE,   1.0E-6,  20.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "RAMP",  WF_RAMP,     1.0E-6,   1.5E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "PULSE", WF_PULSE,    1.0E-6,  20.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "USER",  WF_ARB,      1.0E-6,  20.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "NOISE", WF_NOISE,  100.0E+6, 100.0E+6, 0.0E-0, WFO_AMPLITUDE | WFO_OFFSET },
	{ "DC",    WF_DC,       0.0E-0,   0.0E+0, 0.0E-0, WFO_OFFSET },
};

static const struct channel_spec dg972_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(dg972_waveforms) },
	{ "CH2",  ARRAY_AND_SIZE(dg972_waveforms) },
};

static const struct waveform_spec dg992_waveforms[] = {
	{ "SIN",   WF_SINE,     1.0E-6, 100.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "SQU",   WF_SQUARE,   1.0E-6,  25.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "RAMP",  WF_RAMP,     1.0E-6,   2.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "PULSE", WF_PULSE,    1.0E-6,  25.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "USER",  WF_ARB,      1.0E-6,  25.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "NOISE", WF_NOISE,  100.0E+6, 100.0E+6, 0.0E-0, WFO_AMPLITUDE | WFO_OFFSET },
	{ "DC",    WF_DC,       0.0E-0,   0.0E+0, 0.0E-0, WFO_OFFSET },
};

static const struct channel_spec dg992_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(dg992_waveforms) },
	{ "CH2",  ARRAY_AND_SIZE(dg992_waveforms) },
};

static const struct waveform_spec dg1022z_waveforms[] = {
	{ "SIN",   WF_SINE,     1.0E-6,  25.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "SQU",   WF_SQUARE,   1.0E-6,  25.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "RAMP",  WF_RAMP,     1.0E-6,   0.5E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "PULSE", WF_PULSE,    1.0E-6,  15.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "USER",  WF_ARB,      1.0E-6,  10.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "NOISE", WF_NOISE,   25.0E+6,  25.0E+6, 0.0E-0, WFO_AMPLITUDE | WFO_OFFSET },
	{ "DC",    WF_DC,       0.0E-0,   0.0E+0, 0.0E-0, WFO_OFFSET },
};

static const struct channel_spec dg1022z_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(dg1022z_waveforms) },
	{ "CH2",  ARRAY_AND_SIZE(dg1022z_waveforms) },
};

static const struct waveform_spec dg1032z_waveforms[] = {
	{ "SIN",   WF_SINE,     1.0E-6, 30.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "SQU",   WF_SQUARE,   1.0E-6, 25.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "RAMP",  WF_RAMP,     1.0E-6,  0.5E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "PULSE", WF_PULSE,    1.0E-6, 15.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "USER",  WF_ARB,      1.0E-6, 10.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "NOISE", WF_NOISE,   30.0E+6, 30.0E+6, 0.0E-0, WFO_AMPLITUDE | WFO_OFFSET },
	{ "DC",    WF_DC,       0.0E-0 , 0.0E+0, 0.0E-0, WFO_OFFSET },
};

static const struct channel_spec dg1032z_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(dg1032z_waveforms) },
	{ "CH2",  ARRAY_AND_SIZE(dg1032z_waveforms) },
};

static const struct waveform_spec dg1062z_waveforms[] = {
	{ "SIN",   WF_SINE,     1.0E-6, 60.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "SQU",   WF_SQUARE,   1.0E-6, 25.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "RAMP",  WF_RAMP,     1.0E-6,  1.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "PULSE", WF_PULSE,    1.0E-6, 25.0E+6, 1.0E-6, WAVEFORM_DEFAULT | WFO_DUTY_CYCLE },
	{ "USER",  WF_ARB,      1.0E-6, 20.0E+6, 1.0E-6, WAVEFORM_DEFAULT },
	{ "NOISE", WF_NOISE,   60.0E+6, 60.0E+6, 0.0E-0, WFO_AMPLITUDE | WFO_OFFSET },
	{ "DC",    WF_DC,       0.0E-0,  0.0E+0, 0.0E-0, WFO_OFFSET },
};

static const struct channel_spec dg1062z_channels[] = {
	{ "CH1",  ARRAY_AND_SIZE(dg1062z_waveforms) },
	{ "CH2",  ARRAY_AND_SIZE(dg1062z_waveforms) },
};

static const struct scpi_command cmdset_dg1000z[] = {
	{ PSG_CMD_SETUP_LOCAL, "SYST:KLOC:STATE OFF", },
/*	{ PSG_CMD_SELECT_CHANNEL, "SYST:CHAN:CUR CH%s", }, */
	{ PSG_CMD_GET_CHANNEL, "SYST:CHAN:CUR?", },
	{ PSG_CMD_GET_ENABLED, "OUTP%s:STATE?", },
	{ PSG_CMD_SET_ENABLE, "OUTP%s:STATE ON", },
	{ PSG_CMD_SET_DISABLE, "OUTP%s:STATE OFF", },
	{ PSG_CMD_GET_SOURCE, "SOUR%s:APPL?", },
	{ PSG_CMD_SET_SOURCE, "SOUR%s:APPL:%s", },
	{ PSG_CMD_GET_FREQUENCY, "SOUR%s:FREQ?", },
	{ PSG_CMD_SET_FREQUENCY, "SOUR%s:FREQ %f", },
	{ PSG_CMD_GET_AMPLITUDE, "SOUR%s:VOLT?", },
	{ PSG_CMD_SET_AMPLITUDE, "SOUR%s:VOLT %f", },
	{ PSG_CMD_GET_OFFSET, "SOUR%s:VOLT:OFFS?", },
	{ PSG_CMD_SET_OFFSET, "SOUR%s:VOLT:OFFS %f", },
	{ PSG_CMD_GET_PHASE, "SOUR%s:PHAS?", },
	{ PSG_CMD_SET_PHASE, "SOUR%s:PHAS %f", },
	{ PSG_CMD_GET_DCYCL_PULSE, "SOUR%s:FUNC:PULS:DCYC?", },
	{ PSG_CMD_SET_DCYCL_PULSE, "SOUR%s:FUNC:PULS:DCYC %f", },
	{ PSG_CMD_GET_DCYCL_SQUARE, "SOUR%s:FUNC:SQU:DCYC?", },
	{ PSG_CMD_SET_DCYCL_SQUARE, "SOUR%s:FUNC:SQU:DCYC %f", },
	{ PSG_CMD_COUNTER_GET_ENABLED, "COUN:STAT?", },
	{ PSG_CMD_COUNTER_SET_ENABLE, "COUN:STAT ON", },
	{ PSG_CMD_COUNTER_SET_DISABLE, "COUN:STAT OFF", },
	{ PSG_CMD_COUNTER_MEASURE, "COUN:MEAS?", },
	ALL_ZERO
};

static const struct device_spec device_models[] = {
	{ "Rigol Technologies", "DG811",
		ARRAY_AND_SIZE(dg1000z_devopts),
		ARRAY_AND_SIZE(dg1000z_devopts_cg),
		ARRAY_AND_SIZE(dg811_channels),
		cmdset_dg1000z,
	},
	{ "Rigol Technologies", "DG812",
		ARRAY_AND_SIZE(dg1000z_devopts),
		ARRAY_AND_SIZE(dg1000z_devopts_cg),
		ARRAY_AND_SIZE(dg812_channels),
		cmdset_dg1000z,
	},
	{ "Rigol Technologies", "DG821",
		ARRAY_AND_SIZE(dg1000z_devopts),
		ARRAY_AND_SIZE(dg1000z_devopts_cg),
		ARRAY_AND_SIZE(dg821_channels),
		cmdset_dg1000z,
	},
	{ "Rigol Technologies", "DG822",
		ARRAY_AND_SIZE(dg1000z_devopts),
		ARRAY_AND_SIZE(dg1000z_devopts_cg),
		ARRAY_AND_SIZE(dg822_channels),
		cmdset_dg1000z,
	},
	{ "Rigol Technologies", "DG831",
		ARRAY_AND_SIZE(dg1000z_devopts),
		ARRAY_AND_SIZE(dg1000z_devopts_cg),
		ARRAY_AND_SIZE(dg831_channels),
		cmdset_dg1000z,
	},
	{ "Rigol Technologies", "DG832",
		ARRAY_AND_SIZE(dg1000z_devopts),
		ARRAY_AND_SIZE(dg1000z_devopts_cg),
		ARRAY_AND_SIZE(dg832_channels),
		cmdset_dg1000z,
	},
	{ "Rigol Technologies", "DG952",
		ARRAY_AND_SIZE(dg1000z_devopts),
		ARRAY_AND_SIZE(dg1000z_devopts_cg),
		ARRAY_AND_SIZE(dg952_channels),
		cmdset_dg1000z,
	},
	{ "Rigol Technologies", "DG972",
		ARRAY_AND_SIZE(dg1000z_devopts),
		ARRAY_AND_SIZE(dg1000z_devopts_cg),
		ARRAY_AND_SIZE(dg972_channels),
		cmdset_dg1000z,
	},
	{ "Rigol Technologies", "DG992",
		ARRAY_AND_SIZE(dg1000z_devopts),
		ARRAY_AND_SIZE(dg1000z_devopts_cg),
		ARRAY_AND_SIZE(dg992_channels),
		cmdset_dg1000z,
	},
	{ "Rigol Technologies", "DG1022Z",
		ARRAY_AND_SIZE(dg1000z_devopts),
		ARRAY_AND_SIZE(dg1000z_devopts_cg),
		ARRAY_AND_SIZE(dg1022z_channels),
		cmdset_dg1000z,
	},
	{ "Rigol Technologies", "DG1032Z",
		ARRAY_AND_SIZE(dg1000z_devopts),
		ARRAY_AND_SIZE(dg1000z_devopts_cg),
		ARRAY_AND_SIZE(dg1032z_channels),
		cmdset_dg1000z,
	},
	{ "Rigol Technologies", "DG1062Z",
		ARRAY_AND_SIZE(dg1000z_devopts),
		ARRAY_AND_SIZE(dg1000z_devopts_cg),
		ARRAY_AND_SIZE(dg1062z_channels),
		cmdset_dg1000z,
	},
};

static void check_device_quirks(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	gboolean is_8xx, is_9xx;

	devc = sdi->priv;

	/*
	 * Check for counter issue in DG800/DG900 units...
	 *
	 * TODO: Add firmware version check if Rigol fixes this issue
	 *       in future firmware versions.
	 */
	is_8xx = g_ascii_strncasecmp(sdi->model, "DG8", strlen("DG8")) == 0;
	is_9xx = g_ascii_strncasecmp(sdi->model, "DG9", strlen("DG9")) == 0;
	if (is_8xx || is_9xx) {
		devc->quirks |= RIGOL_DG_COUNTER_BUG;
		devc->quirks |= RIGOL_DG_COUNTER_CH2_CONFLICT;
	}
}

static struct sr_dev_inst *probe_device(struct sr_scpi_dev_inst *scpi)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_scpi_hw_info *hw_info;
	const struct device_spec *device;
	const struct scpi_command *cmdset;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	const char *command;
	unsigned int i, ch_idx;
	char tmp[16];

	sdi = NULL;
	devc = NULL;
	hw_info = NULL;

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK)
		goto error;

	device = NULL;
	for (i = 0; i < ARRAY_SIZE(device_models); i++) {
		if (g_ascii_strcasecmp(hw_info->manufacturer,
				device_models[i].vendor) != 0)
			continue;
		if (g_ascii_strcasecmp(hw_info->model,
				device_models[i].model) != 0)
			continue;
		device = &device_models[i];
		cmdset = device_models[i].cmdset;
		break;
	}
	if (!device)
		goto error;

	sdi = g_malloc0(sizeof(*sdi));
	sdi->vendor = g_strdup(hw_info->manufacturer);
	sdi->model = g_strdup(hw_info->model);
	sdi->version = g_strdup(hw_info->firmware_version);
	sdi->serial_num = g_strdup(hw_info->serial_number);
	sdi->conn = scpi;
	sdi->driver = &rigol_dg_driver_info;
	sdi->inst_type = SR_INST_SCPI;

	devc = g_malloc0(sizeof(*devc));
	devc->cmdset = cmdset;
	devc->device = device;
	devc->ch_status = g_malloc0((device->num_channels + 1) *
			sizeof(devc->ch_status[0]));
	sr_sw_limits_init(&devc->limits);
	sdi->priv = devc;

	/* Create channel group and channel for each device channel. */
	ch_idx = 0;
	for (i = 0; i < device->num_channels; i++) {
		ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_ANALOG, TRUE,
				device->channels[i].name);
		snprintf(tmp, sizeof(tmp), "%u", i + 1);
		cg = sr_channel_group_new(sdi, tmp, NULL);
		cg->channels = g_slist_append(cg->channels, ch);
	}

	/* Create channels for the frequency counter output. */
	ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_ANALOG, TRUE, "FREQ1");
	ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_ANALOG, TRUE, "PERIOD1");
	ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_ANALOG, TRUE, "DUTY1");
	ch = sr_channel_new(sdi, ch_idx++, SR_CHANNEL_ANALOG, TRUE, "WIDTH1");

	/* Put device back to "local" mode, in case only a scan was done... */
	command = sr_scpi_cmd_get(devc->cmdset, PSG_CMD_SETUP_LOCAL);
	if (command && *command) {
		sr_scpi_get_opc(scpi);
		sr_scpi_send(scpi, command);
	}

	sr_scpi_hw_info_free(hw_info);

	/* Check for device/firmware specific issues. */
	check_device_quirks(sdi);

	return sdi;

error:
	sr_scpi_hw_info_free(hw_info);
	g_free(devc);
	sr_dev_inst_free(sdi);

	return NULL;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	return sr_scpi_scan(di->context, options, probe_device);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	return sr_scpi_open(sdi->conn);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	const char *command;

	devc = sdi->priv;
	scpi = sdi->conn;
	if (!scpi)
		return SR_ERR_BUG;

	/* Put unit back to "local" mode. */
	command = sr_scpi_cmd_get(devc->cmdset, PSG_CMD_SETUP_LOCAL);
	if (command && *command) {
		sr_scpi_get_opc(scpi);
		sr_scpi_send(scpi, command);
	}

	return sr_scpi_close(sdi->conn);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	struct sr_channel *ch;
	struct channel_status *ch_status;
	const struct sr_key_info *kinfo;
	uint32_t cmd;
	int ret;

	if (!sdi || !data)
		return SR_ERR_ARG;

	devc = sdi->priv;
	scpi = sdi->conn;
	ret = SR_OK;
	kinfo = sr_key_info_get(SR_KEY_CONFIG, key);

	if (!cg) {
		switch (key) {
		case SR_CONF_LIMIT_SAMPLES:
		case SR_CONF_LIMIT_MSEC:
			ret = sr_sw_limits_config_get(&devc->limits, key, data);
			break;
		default:
			sr_dbg("%s: Unsupported key: %d (%s)", __func__,
				(int)key, (kinfo ? kinfo->name : "unknown"));
			ret = SR_ERR_NA;
			break;
		}
	} else {
		ch = cg->channels->data;
		ch_status = &devc->ch_status[ch->index];

		switch (key) {
		case SR_CONF_ENABLED:
			sr_scpi_get_opc(scpi);
			ret = sr_scpi_cmd_resp(sdi, devc->cmdset,
				PSG_CMD_SELECT_CHANNEL, cg->name, data,
				G_VARIANT_TYPE_BOOLEAN, PSG_CMD_GET_ENABLED,
				cg->name);
			break;
		case SR_CONF_PATTERN_MODE:
			if ((ret = rigol_dg_get_channel_state(sdi, cg)) == SR_OK) {
				*data = g_variant_new_string(
					 rigol_dg_waveform_to_string(
						 ch_status->wf));
			}
			break;
		case SR_CONF_OUTPUT_FREQUENCY:
			if ((ret = rigol_dg_get_channel_state(sdi, cg)) == SR_OK)
				*data = g_variant_new_double(ch_status->freq);
			break;
		case SR_CONF_AMPLITUDE:
			if ((ret = rigol_dg_get_channel_state(sdi, cg)) == SR_OK)
				*data = g_variant_new_double(ch_status->ampl);
			break;
		case SR_CONF_OFFSET:
			if ((ret = rigol_dg_get_channel_state(sdi, cg)) == SR_OK)
				*data = g_variant_new_double(ch_status->offset);
			break;
		case SR_CONF_PHASE:
			if ((ret = rigol_dg_get_channel_state(sdi, cg)) == SR_OK)
				*data = g_variant_new_double(ch_status->phase);
			break;
		case SR_CONF_DUTY_CYCLE:
			if ((ret = rigol_dg_get_channel_state(sdi, cg)) != SR_OK)
				break;
			if (ch_status->wf == WF_SQUARE) {
				cmd = PSG_CMD_GET_DCYCL_SQUARE;
			} else if (ch_status->wf == WF_PULSE) {
				cmd = PSG_CMD_GET_DCYCL_PULSE;
			} else {
				ret = SR_ERR_NA;
				break;
			}
			sr_scpi_get_opc(scpi);
			ret = sr_scpi_cmd_resp(sdi, devc->cmdset,
				PSG_CMD_SELECT_CHANNEL, cg->name, data,
				G_VARIANT_TYPE_DOUBLE, cmd, cg->name);
			break;
		default:
			sr_dbg("%s: Unsupported (cg) key: %d (%s)", __func__,
				(int)key, (kinfo ? kinfo->name : "unknown"));
			ret = SR_ERR_NA;
			break;
		}
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	struct sr_channel *ch;
	const struct channel_spec *ch_spec;
	struct channel_status *ch_status;
	const struct sr_key_info *kinfo;
	int ret;
	uint32_t cmd;
	const char *mode, *mode_name, *new_mode;
	unsigned int i;

	if (!data || !sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	scpi = sdi->conn;
	kinfo = sr_key_info_get(SR_KEY_CONFIG, key);

	ret = SR_OK;

	if (!cg) {
		switch (key) {
		case SR_CONF_LIMIT_MSEC:
		case SR_CONF_LIMIT_SAMPLES:
			ret = sr_sw_limits_config_set(&devc->limits, key, data);
			break;
		default:
			sr_dbg("%s: Unsupported key: %d (%s)", __func__,
				(int)key, (kinfo ? kinfo->name : "unknown"));
			ret = SR_ERR_NA;
			break;
		}
	} else {
		ch = cg->channels->data;
		ch_spec = &devc->device->channels[ch->index];
		ch_status = &devc->ch_status[ch->index];

		if ((ret = rigol_dg_get_channel_state(sdi, cg)) != SR_OK)
			return ret;
		sr_scpi_get_opc(scpi);

		switch (key) {
		case SR_CONF_ENABLED:
			if (g_variant_get_boolean(data))
				cmd = PSG_CMD_SET_ENABLE;
			else
				cmd = PSG_CMD_SET_DISABLE;
			ret = sr_scpi_cmd(sdi, devc->cmdset,
				PSG_CMD_SELECT_CHANNEL, cg->name, cmd, cg->name);
			break;
		case SR_CONF_PATTERN_MODE:
			ret = SR_ERR_NA;
			new_mode = NULL;
			mode = g_variant_get_string(data, NULL);
			for (i = 0; i < ch_spec->num_waveforms; i++) {
				mode_name = rigol_dg_waveform_to_string(
						ch_spec->waveforms[i].waveform);
				if (g_ascii_strncasecmp(mode, mode_name,
						strlen(mode_name)) == 0)
					new_mode = ch_spec->waveforms[i].name;
			}
			if (new_mode)
				ret = sr_scpi_cmd(sdi, devc->cmdset,
					PSG_CMD_SELECT_CHANNEL, cg->name,
					PSG_CMD_SET_SOURCE, cg->name, new_mode);
			break;
		case SR_CONF_OUTPUT_FREQUENCY:
			ret = SR_ERR_NA;
			if (!(ch_status->wf_spec->opts & WFO_FREQUENCY))
				break;
			ret = sr_scpi_cmd(sdi, devc->cmdset,
				PSG_CMD_SELECT_CHANNEL, cg->name,
				PSG_CMD_SET_FREQUENCY, cg->name,
				g_variant_get_double(data));
			break;
		case SR_CONF_AMPLITUDE:
			ret = SR_ERR_NA;
			if (!(ch_status->wf_spec->opts & WFO_AMPLITUDE))
				break;
			ret = sr_scpi_cmd(sdi, devc->cmdset,
				PSG_CMD_SELECT_CHANNEL, cg->name,
				PSG_CMD_SET_AMPLITUDE, cg->name,
				g_variant_get_double(data));
			break;
		case SR_CONF_OFFSET:
			ret = SR_ERR_NA;
			if (!(ch_status->wf_spec->opts & WFO_OFFSET))
				break;
			ret = sr_scpi_cmd(sdi, devc->cmdset,
				PSG_CMD_SELECT_CHANNEL, cg->name,
				PSG_CMD_SET_OFFSET, cg->name,
				g_variant_get_double(data));
			break;
		case SR_CONF_PHASE:
			ret = SR_ERR_NA;
			if (!(ch_status->wf_spec->opts & WFO_PHASE))
				break;
			ret = sr_scpi_cmd(sdi, devc->cmdset,
				PSG_CMD_SELECT_CHANNEL, cg->name,
				PSG_CMD_SET_PHASE, cg->name,
				g_variant_get_double(data));
			break;
		case SR_CONF_DUTY_CYCLE:
			ret = SR_ERR_NA;
			if (!(ch_status->wf_spec->opts & WFO_DUTY_CYCLE))
				break;
			if (ch_status->wf == WF_SQUARE)
				cmd = PSG_CMD_SET_DCYCL_SQUARE;
			else if (ch_status->wf == WF_PULSE)
				cmd = PSG_CMD_SET_DCYCL_PULSE;
			else
				break;
			ret = sr_scpi_cmd(sdi, devc->cmdset,
				PSG_CMD_SELECT_CHANNEL, cg->name,
				cmd, cg->name, g_variant_get_double(data));
			break;
		default:
			sr_dbg("%s: Unsupported key: %d (%s)", __func__,
				(int)key, (kinfo ? kinfo->name : "unknown"));
			ret = SR_ERR_NA;
			break;
		}
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	const struct channel_spec *ch_spec;
	const struct waveform_spec *wf_spec;
	struct channel_status *ch_status;
	GVariantBuilder *b;
	unsigned int i;
	double fspec[3];

	devc = NULL;
	if (sdi)
		devc = sdi->priv;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return std_opts_config_list(key, data, sdi, cg,
				ARRAY_AND_SIZE(scanopts),
				ARRAY_AND_SIZE(drvopts),
				(devc && devc->device) ? devc->device->devopts : NULL,
				(devc && devc->device) ? devc->device->num_devopts : 0);
		default:
			return SR_ERR_NA;
		}
	} else {
		if (!devc || !devc->device)
			return SR_ERR_ARG;
		ch = cg->channels->data;
		ch_spec = &devc->device->channels[ch->index];
		ch_status = &devc->ch_status[ch->index];

		switch(key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(devc->device->devopts_cg,
					devc->device->num_devopts_cg);
			break;
		case SR_CONF_PATTERN_MODE:
			b = g_variant_builder_new(G_VARIANT_TYPE("as"));
			for (i = 0; i < ch_spec->num_waveforms; i++) {
				g_variant_builder_add(b, "s",
					rigol_dg_waveform_to_string(
						ch_spec->waveforms[i].waveform));
			}
			*data = g_variant_new("as", b);
			g_variant_builder_unref(b);
			break;
		case SR_CONF_OUTPUT_FREQUENCY:
			/*
			 * Frequency range depends on the currently active
			 * wave form.
			 */
			if (rigol_dg_get_channel_state(sdi, cg) != SR_OK)
				return SR_ERR_NA;
			wf_spec = rigol_dg_get_waveform_spec(ch_spec,
					ch_status->wf);
			if (!wf_spec)
				return SR_ERR_BUG;
			fspec[0] = wf_spec->freq_min;
			fspec[1] = wf_spec->freq_max;
			fspec[2] = wf_spec->freq_step;
			*data = std_gvar_min_max_step_array(fspec);
			break;
		case SR_CONF_PHASE:
			*data = std_gvar_min_max_step_array(phase_min_max_step);
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
	const char *cmd;
	char *response;
	GVariant *data;
	gboolean need_quirk;
	gboolean ch_active;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	scpi = sdi->conn;
	response = NULL;
	data = NULL;
	ret = SR_OK;

	if (!scpi)
		return SR_ERR_BUG;

	cmd = sr_scpi_cmd_get(devc->cmdset, PSG_CMD_COUNTER_GET_ENABLED);
	if (cmd && *cmd) {
		/* Check if counter is currently enabled. */
		ret = sr_scpi_get_string(scpi, cmd, &response);
		if (ret != SR_OK)
			return SR_ERR_NA;
		if (g_ascii_strncasecmp(response, "RUN", strlen("RUN")) == 0)
			devc->counter_enabled = TRUE;
		else
			devc->counter_enabled = FALSE;
		g_free(response);

		if (!devc->counter_enabled) {
			/*
			 * Enable counter if it was not already running.
			 * Some devices cannot use channel 2 and the counter
			 * at the same time. Some cannot respond right after
			 * enabling the counter and need a delay.
			 */

			need_quirk = devc->quirks & RIGOL_DG_COUNTER_CH2_CONFLICT;
			need_quirk &= devc->device->num_channels > 1;
			if (need_quirk) {
				sr_scpi_get_opc(scpi);
				ret = sr_scpi_cmd_resp(sdi, devc->cmdset,
					PSG_CMD_SELECT_CHANNEL, "2",
					&data, G_VARIANT_TYPE_BOOLEAN,
					PSG_CMD_GET_ENABLED, "2");
				if (ret != SR_OK)
					return SR_ERR_NA;
				ch_active = g_variant_get_boolean(data);
				g_variant_unref(data);
				if (ch_active) {
					sr_scpi_get_opc(scpi);
					ret = sr_scpi_cmd(sdi, devc->cmdset,
						PSG_CMD_SELECT_CHANNEL, "2",
						PSG_CMD_SET_DISABLE, "2");
					if (ret != SR_OK)
						return SR_ERR_NA;
				}
			}

			cmd = sr_scpi_cmd_get(devc->cmdset,
					PSG_CMD_COUNTER_SET_ENABLE);
			if (!cmd)
				return SR_ERR_BUG;
			sr_scpi_get_opc(scpi);
			ret = sr_scpi_send(scpi, cmd);

			need_quirk = devc->quirks & RIGOL_DG_COUNTER_BUG;
			if (need_quirk)
				g_usleep(RIGOL_DG_COUNTER_BUG_DELAY);
		}
	}

	if (ret == SR_OK) {
		sr_sw_limits_acquisition_start(&devc->limits);
		ret = std_session_send_df_header(sdi);
		if (ret == SR_OK) {
			ret = sr_scpi_source_add(sdi->session, scpi,
				G_IO_IN, 100, rigol_dg_receive_data,
				(void *)sdi);
		}
	}

	return ret;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	const char *cmd;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;
	scpi = sdi->conn;
	ret = SR_OK;

	cmd = sr_scpi_cmd_get(devc->cmdset, PSG_CMD_COUNTER_SET_DISABLE);
	if (cmd && *cmd && !devc->counter_enabled) {
		/*
		 * If counter was not running when acquisiton started,
		 * turn it off now. Some devices need a delay after
		 * disabling the counter.
		 */
		sr_scpi_get_opc(scpi);
		ret = sr_scpi_send(scpi, cmd);
		if (devc->quirks & RIGOL_DG_COUNTER_BUG)
			g_usleep(RIGOL_DG_COUNTER_BUG_DELAY);
	}

	sr_scpi_source_remove(sdi->session, scpi);
	std_session_send_df_end(sdi);

	return ret;
}

static struct sr_dev_driver rigol_dg_driver_info = {
	.name = "rigol-dg",
	.longname = "Rigol DG Series",
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
SR_REGISTER_DEV_DRIVER(rigol_dg_driver_info);
