/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Vlad Ivanov <vlad.ivanov@lab-systems.ru>
 * Copyright (C) 2021 Daniel Anselmi <danselmi@gmx.ch>
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

#ifndef LIBSIGROK_HARDWARE_ROHDE_SCHWARZ_SME_0X_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ROHDE_SCHWARZ_SME_0X_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "rohde-schwarz-sme-0x"

struct rs_device_model_config{
	double freq_step;
	double power_step;
	const char **commands;
	const char **responses;
};

struct rs_device_model {
	const char *model_str;
	const struct rs_device_model_config *model_config;
};

struct dev_context {
	const struct rs_device_model_config *model_config;
	double freq;
	double power;
	gboolean enable;
	int clk_source_idx;

	double freq_min;
	double freq_max;
	double power_min;
	double power_max;
};

extern const char *commands_sme0x[];
extern const char *commands_smx100[];
extern const char *responses_sme0x[];
extern const char *responses_smx100[];

SR_PRIV int rs_sme0x_init(const struct sr_dev_inst *sdi);
SR_PRIV int rs_sme0x_mode_remote(const struct sr_dev_inst *sdi);
SR_PRIV int rs_sme0x_mode_local(const struct sr_dev_inst *sdi);
SR_PRIV int rs_sme0x_sync(const struct sr_dev_inst *sdi);
SR_PRIV int rs_sme0x_get_enable(const struct sr_dev_inst *sdi,
        gboolean *enable);
SR_PRIV int rs_sme0x_get_freq(const struct sr_dev_inst *sdi, double *freq);
SR_PRIV int rs_sme0x_get_power(const struct sr_dev_inst *sdi, double *power);
SR_PRIV int rs_sme0x_get_clk_src_idx(const struct sr_dev_inst *sdi, int *idx);
SR_PRIV int rs_sme0x_set_enable(const struct sr_dev_inst *sdi,
        gboolean enable);
SR_PRIV int rs_sme0x_set_freq(const struct sr_dev_inst *sdi, double freq);
SR_PRIV int rs_sme0x_set_power(const struct sr_dev_inst *sdi, double power);
SR_PRIV int rs_sme0x_set_clk_src(const struct sr_dev_inst *sdi, int idx);

SR_PRIV int rs_sme0x_get_minmax_freq(const struct sr_dev_inst
        *sdi, double *min_freq, double *max_freq);
SR_PRIV int rs_sme0x_get_minmax_power(const struct sr_dev_inst *sdi,
        double *min_power, double *max_power);

#endif

