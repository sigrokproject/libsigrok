/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Larry Hernandez <l.gr@dartmouth.edu>
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
#include "protocol.h"

static int v1_fpga_firmware_upload(const struct sr_dev_inst *sdi)
{
	return dslogic_fpga_firmware_upload(sdi);
}

static int v1_fpga_config(const struct sr_dev_inst *sdi)
{
	return fpga_configure(sdi);
}

static int v1_acquisition_start(const struct sr_dev_inst *sdi)
{
	return command_start_acquisition(sdi);
}

static int v1_acquisition_stop(const struct sr_dev_inst *sdi)
{
	return command_stop_acquisition(sdi);
}

static int v1_set_samplerate(const struct sr_dev_inst *sdi, uint64_t rate)
{
	struct dev_context *devc = sdi->priv;
	devc->cur_samplerate = rate;
	return SR_OK;  /* V1 applies samplerate via fpga_configure on next acq */
}

static int v1_set_voltage_threshold(const struct sr_dev_inst *sdi, double low, double high)
{
	(void)low;  /* V1 uses a single threshold midpoint */
	return dslogic_set_voltage_threshold(sdi, high);
}

static int v1_set_trigger(const struct sr_dev_inst *sdi)
{
	(void)sdi;
	return SR_OK;  /* V1 sets trigger inside fpga_configure */
}

static int v1_set_external_clock(const struct sr_dev_inst *sdi, gboolean ext)
{
	struct dev_context *devc = sdi->priv;
	devc->external_clock = ext;
	return SR_OK;
}

static int v1_set_clock_edge(const struct sr_dev_inst *sdi, int edge)
{
	struct dev_context *devc = sdi->priv;
	devc->clock_edge = edge;
	return SR_OK;
}

static int v1_security_check(const struct sr_dev_inst *sdi)
{
	(void)sdi;
	return SR_OK;  /* V1 hardware has no security check */
}

SR_PRIV const struct dslogic_protocol_ops dslogic_v1_ops = {
	.fpga_firmware_upload = v1_fpga_firmware_upload,
	.fpga_config = v1_fpga_config,
	.acquisition_start = v1_acquisition_start,
	.acquisition_stop = v1_acquisition_stop,
	.set_samplerate = v1_set_samplerate,
	.set_voltage_threshold = v1_set_voltage_threshold,
	.set_trigger = v1_set_trigger,
	.set_external_clock = v1_set_external_clock,
	.set_clock_edge = v1_set_clock_edge,
	.security_check = v1_security_check,
};
