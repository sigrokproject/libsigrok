/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Vlad Ivanov <vlad.ivanov@lab-systems.ru>
 * Copyright (C) 2024 Daniel Anselmi <danselmi@gmx.ch>
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
#include <scpi.h>

#include "protocol.h"

enum {
	RS_CMD_PRESET,
	RS_CMD_RESET_STATUS,
	RS_CMD_CONTROL_REMOTE,
	RS_CMD_CONTROL_LOCAL,
	RS_CMD_CONTROL_REMOTEQM,
	RS_CMD_SET_ENABLE,
	RS_CMD_SET_DISABLE,
	RS_CMD_SET_FREQ,
	RS_CMD_SET_POWER,
	RS_CMD_SET_CLK_SRC_INT,
	RS_CMD_SET_CLK_SRC_EXT,
	RS_CMD_GET_ENABLE,
	RS_CMD_GET_FREQ,
	RS_CMD_GET_POWER,
	RS_CMD_GET_CLK_SRC,
};

enum {
	RS_RESP_OUTP_ON,
	RS_RESP_OUTP_OFF,
	RS_RESP_CLK_SRC_INT,
	RS_RESP_CLK_SRC_EXT,
};

const char *commands_sme0x[] = {
	[RS_CMD_PRESET]           = "*RST",
	[RS_CMD_RESET_STATUS]     = "*CLS",
	[RS_CMD_CONTROL_REMOTE]   = "\n",
	[RS_CMD_CONTROL_LOCAL]    = NULL,
	[RS_CMD_CONTROL_REMOTEQM] = NULL,
	[RS_CMD_SET_ENABLE]       = ":OUTP ON",
	[RS_CMD_SET_DISABLE]      = ":OUTP OFF",
	[RS_CMD_SET_FREQ]         = ":FREQ %.1fHz",
	[RS_CMD_SET_POWER]        = ":POW %.1fdBm",
	[RS_CMD_SET_CLK_SRC_INT]  = ":ROSC:SOUR INT",
	[RS_CMD_SET_CLK_SRC_EXT]  = ":ROSC:SOUR EXT",
	[RS_CMD_GET_ENABLE]       = ":OUTP?",
	[RS_CMD_GET_FREQ]         = ":FREQ?",
	[RS_CMD_GET_POWER]        = ":POW?",
	[RS_CMD_GET_CLK_SRC]      = ":ROSC:SOUR?",
};

const char *responses_sme0x[] = {
	[RS_RESP_OUTP_ON]       = "1",
	[RS_RESP_OUTP_OFF]      = "0",
	[RS_RESP_CLK_SRC_INT]   = "INT",
	[RS_RESP_CLK_SRC_EXT]   = "EXT",
};

const char *commands_smx100[] = {
	[RS_CMD_PRESET]           = "*RST",
	[RS_CMD_RESET_STATUS]     = "*CLS",
	[RS_CMD_CONTROL_REMOTE]   = ":SYST:DLOC ON",
	[RS_CMD_CONTROL_LOCAL]    = ":SYST:DLOC OFF",
	[RS_CMD_CONTROL_REMOTEQM] = ":SYST:DLOC?",
	[RS_CMD_SET_ENABLE]       = ":OUTP ON",
	[RS_CMD_SET_DISABLE]      = ":OUTP OFF",
	[RS_CMD_SET_FREQ]         = ":FREQ %.3fHz",
	[RS_CMD_SET_POWER]        = ":POW %.2fdBm",
	[RS_CMD_SET_CLK_SRC_INT]  = ":ROSC:SOUR INT",
	[RS_CMD_SET_CLK_SRC_EXT]  = ":ROSC:SOUR EXT",
	[RS_CMD_GET_ENABLE]       = ":OUTP?",
	[RS_CMD_GET_FREQ]         = ":FREQ?",
	[RS_CMD_GET_POWER]        = ":POW?",
	[RS_CMD_GET_CLK_SRC]      = ":ROSC:SOUR?",
};

const char *responses_smx100[] = {
	[RS_RESP_OUTP_ON]       = "1",
	[RS_RESP_OUTP_OFF]      = "0",
	[RS_RESP_CLK_SRC_INT]   = "INT",
	[RS_RESP_CLK_SRC_EXT]   = "EXT",
};

SR_PRIV int rs_sme0x_init(const struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;
	const char *cmd;

	if (!sdi || !sdi->priv)
		return SR_ERR;
	devc = sdi->priv;

	cmd = devc->model_config->commands[RS_CMD_PRESET];
	if (cmd && (ret = sr_scpi_send(sdi->conn, cmd)) != SR_OK)
		return ret;

	cmd = devc->model_config->commands[RS_CMD_RESET_STATUS];
	if (cmd && (ret = sr_scpi_send(sdi->conn, cmd)) != SR_OK)
		return ret;

	return SR_OK;
}

SR_PRIV int rs_sme0x_mode_remote(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const char *cmd;

	if (!sdi || !sdi->priv)
		return SR_ERR;
	devc = sdi->priv;

	cmd = devc->model_config->commands[RS_CMD_CONTROL_REMOTE];
	if (cmd)
		return sr_scpi_send(sdi->conn, cmd);

	return SR_OK;
}

SR_PRIV int rs_sme0x_mode_local(const struct sr_dev_inst *sdi)
{
	int ret, resp_dlock;
	struct dev_context *devc;
	const char *cmd_set;
	const char *cmd_get;

	if (!sdi || !sdi->priv)
		return SR_ERR;
	devc = sdi->priv;

	cmd_set = devc->model_config->commands[RS_CMD_CONTROL_LOCAL];
	cmd_get = devc->model_config->commands[RS_CMD_CONTROL_REMOTEQM];
	if (!cmd_set)
		return SR_OK;

	ret = SR_OK;
	resp_dlock = 0;
	do {
		ret = sr_scpi_send(sdi->conn, cmd_set);
		if (ret == SR_OK) {
			if (cmd_get)
				ret = sr_scpi_get_int(sdi->conn, cmd_get, &resp_dlock);
			else
				break;
		}
	} while(ret == SR_OK && resp_dlock == 1);

	return ret;
}

SR_PRIV int rs_sme0x_sync(const struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;

	if (!sdi || !sdi->priv)
		return SR_ERR;
	devc = sdi->priv;

	if ((ret = rs_sme0x_get_enable(sdi, &devc->enable)) != SR_OK)
		return ret;
	if ((ret = rs_sme0x_get_freq(sdi, &devc->freq) != SR_OK))
		return ret;
	if ((ret = rs_sme0x_get_power(sdi, &devc->power) != SR_OK))
		return ret;
	if ((ret = rs_sme0x_get_clk_src_idx(sdi, &devc->clk_source_idx) != SR_OK))
		return ret;

	return SR_OK;
}

SR_PRIV int rs_sme0x_get_enable(const struct sr_dev_inst *sdi, gboolean *enable)
{
	int ret;
	char *buf;
	struct dev_context *devc;
	const char *resp_on;
	const char *resp_off;

	if (!sdi || !sdi->priv)
		return SR_ERR;
	devc = sdi->priv;

	ret = sr_scpi_get_string(sdi->conn,
		devc->model_config->commands[RS_CMD_GET_ENABLE], &buf);
	if (ret != SR_OK)
		return ret;

	resp_on = devc->model_config->responses[RS_RESP_OUTP_ON];
	resp_off = devc->model_config->responses[RS_RESP_OUTP_OFF];
	if (strcmp(buf, resp_on) == 0) {
		ret = SR_OK;
		*enable = TRUE;
	} else if (strcmp(buf, resp_off) == 0) {
		ret = SR_OK;
		*enable = FALSE;
	} else {
		ret = SR_ERR;
	}

	g_free(buf);
	return ret;
}

SR_PRIV int rs_sme0x_get_freq(const struct sr_dev_inst *sdi, double *freq)
{
	int ret;
	struct dev_context *devc;

	if (!sdi || !sdi->priv)
		return SR_ERR;
	devc = sdi->priv;

	ret = sr_scpi_get_double(sdi->conn,
		devc->model_config->commands[RS_CMD_GET_FREQ], freq);

	if (ret != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int rs_sme0x_get_power(const struct sr_dev_inst *sdi, double *power)
{
	int ret;
	struct dev_context *devc;
	const char * cmd;

	if (!sdi || !sdi->priv)
		return SR_ERR;
	devc = sdi->priv;

	cmd = devc->model_config->commands[RS_CMD_GET_POWER];
	ret = sr_scpi_get_double(sdi->conn, cmd, power);
	if (ret != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int rs_sme0x_get_clk_src_idx(const struct sr_dev_inst *sdi, int *idx)
{
	char *buf;
	int ret;
	struct dev_context *devc;
	const char *cmd;
	const char *resp_int;
	const char *resp_ext;

	if (!sdi || !sdi->priv)
		return SR_ERR;
	devc = sdi->priv;

	cmd = devc->model_config->commands[RS_CMD_GET_CLK_SRC];
	ret = sr_scpi_get_string(sdi->conn, cmd, &buf);
	if (ret != SR_OK || !buf)
		return SR_ERR;

	resp_int = devc->model_config->responses[RS_RESP_CLK_SRC_INT];
	resp_ext = devc->model_config->responses[RS_RESP_CLK_SRC_EXT];
	if (strcmp(buf, resp_int) == 0) {
		ret = SR_OK;
		*idx = 0;
	} else if (strcmp(buf, resp_ext) == 0) {
		ret = SR_OK;
		*idx = 1;
	} else {
		ret = SR_ERR;
	}

	g_free(buf);
	return ret;
}

SR_PRIV int rs_sme0x_set_enable(const struct sr_dev_inst *sdi, gboolean enable)
{
	struct dev_context *devc;

	if (!sdi || !sdi->priv)
		return SR_ERR;
	devc = sdi->priv;

	if (devc->enable == enable)
		return SR_OK;

	devc->enable = enable;

	return sr_scpi_send(sdi->conn, (devc->enable) ?
		devc->model_config->commands[RS_CMD_SET_ENABLE] :
		devc->model_config->commands[RS_CMD_SET_DISABLE]);
}

SR_PRIV int rs_sme0x_set_freq(const struct sr_dev_inst *sdi, double freq)
{
	struct dev_context *devc;

	if (!sdi || !sdi->priv)
		return SR_ERR;
	devc = sdi->priv;

	if ((freq > devc->freq_max) || (freq < devc->freq_min))
		return SR_ERR_ARG;

	return sr_scpi_send(sdi->conn,
		devc->model_config->commands[RS_CMD_SET_FREQ], freq);
}

SR_PRIV int rs_sme0x_set_power(const struct sr_dev_inst *sdi, double power)
{
	struct dev_context *devc;

	if (!sdi || !sdi->priv)
		return SR_ERR;
	devc = sdi->priv;

	if ((power > devc->power_max) || (power < devc->power_min))
		return SR_ERR_ARG;

	return sr_scpi_send(sdi->conn,
		devc->model_config->commands[RS_CMD_SET_POWER], power);
}

SR_PRIV int rs_sme0x_set_clk_src(const struct sr_dev_inst *sdi, int idx)
{
	struct dev_context *devc;

	if (!sdi || !sdi->priv || !sdi->conn)
		return SR_ERR;
	devc = sdi->priv;

	if (devc->clk_source_idx == idx)
		return SR_OK;

	devc->clk_source_idx = idx;

	return sr_scpi_send(sdi->conn, (idx == 0) ?
		devc->model_config->commands[RS_CMD_SET_CLK_SRC_INT] :
		devc->model_config->commands[RS_CMD_SET_CLK_SRC_EXT]);
}

SR_PRIV int rs_sme0x_get_minmax_freq(const struct sr_dev_inst *sdi,
	double *min_freq, double *max_freq)
{
	int ret;

	if (!sdi || !sdi->conn)
		return SR_ERR;

	if ((ret = sr_scpi_get_double(sdi->conn, "FREQ? MIN", min_freq)) != SR_OK)
		return ret;

	return sr_scpi_get_double(sdi->conn, "FREQ? MAX", max_freq);
}

SR_PRIV int rs_sme0x_get_minmax_power(const struct sr_dev_inst *sdi,
	double *min_power, double *max_power)
{
	int ret;

	if (!sdi || !sdi->conn)
		return SR_ERR;

	if ((ret = sr_scpi_get_double(sdi->conn, "POW? MIN", min_power)) != SR_OK)
		return ret;

	return sr_scpi_get_double(sdi->conn, "POW? MAX", max_power);
}
