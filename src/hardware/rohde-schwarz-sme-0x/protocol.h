/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Vlad Ivanov <vlad.ivanov@lab-systems.ru>
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

struct rs_sme0x_info {
	struct sr_dev_driver di;
	char *vendor;
	char *device;
};

struct rs_device_model {
	const char *model_str;
	double freq_max;
	double freq_min;
	double power_max;
	double power_min;
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Model-specific information */
	const struct rs_device_model *model_config;
};

SR_PRIV int rs_sme0x_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int rs_sme0x_mode_remote(struct sr_scpi_dev_inst *scpi);
SR_PRIV int rs_sme0x_close(struct sr_dev_inst *sdi);
SR_PRIV int rs_sme0x_get_freq(const struct sr_dev_inst *sdi, double *freq);
SR_PRIV int rs_sme0x_get_power(const struct sr_dev_inst *sdi, double *power);
SR_PRIV int rs_sme0x_set_freq(const struct sr_dev_inst *sdi, double freq);
SR_PRIV int rs_sme0x_set_power(const struct sr_dev_inst *sdi, double power);

#endif
