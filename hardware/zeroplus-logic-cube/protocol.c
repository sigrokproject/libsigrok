/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

#include "protocol.h"

SR_PRIV unsigned int get_memory_size(int type)
{
	if (type == MEMORY_SIZE_8K)
		return 8 * 1024;
	else if (type <= MEMORY_SIZE_8M)
		return (32 * 1024) << type;
	else
		return 0;
}

SR_PRIV int clz(unsigned int x)
{
	int n = 0;
	if (x == 0)
		return 32;
	if (!(x & 0xFFFF0000)) {
		n = n + 16;
		x = x << 16;
	}
	if (!(x & 0xFF000000)) {
		n = n + 8;
		x = x << 8;
	}
	if (!(x & 0xF0000000)) {
		n = n + 4;
		x = x << 4;
	}
	if (!(x & 0xC0000000)) {
		n = n + 2;
		x = x << 2;
	}
	if (!(x & 0x80000000))
		n = n + 1;
	return n;
}

SR_PRIV int set_limit_samples(struct dev_context *devc, uint64_t samples)
{
	if (samples > devc->max_sample_depth)
		samples = devc->max_sample_depth;

	devc->limit_samples = samples;

	if (samples <= 2 * 1024)
		devc->memory_size = MEMORY_SIZE_8K;
	else if (samples <= 16 * 1024)
		devc->memory_size = MEMORY_SIZE_64K;
	else
		devc->memory_size = 19 - clz(samples - 1);

	sr_info("Setting memory size to %dK.",
		get_memory_size(devc->memory_size) / 1024);

	analyzer_set_memory_size(devc->memory_size);

	return SR_OK;
}

SR_PRIV int set_capture_ratio(struct dev_context *devc, uint64_t ratio)
{
	if (ratio > 100) {
		sr_err("Invalid capture ratio: %" PRIu64 ".", ratio);
		return SR_ERR_ARG;
	}

	devc->capture_ratio = ratio;

	sr_info("Setting capture ratio to %d%%.", devc->capture_ratio);

	return SR_OK;
}

SR_PRIV void set_triggerbar(struct dev_context *devc)
{
	unsigned int trigger_depth, triggerbar, ramsize_trigger;

	trigger_depth = get_memory_size(devc->memory_size) / 4;
	if (devc->limit_samples < trigger_depth)
		trigger_depth = devc->limit_samples;

	if (devc->trigger)
		triggerbar = trigger_depth * devc->capture_ratio / 100;
	else
		triggerbar = 0;

	ramsize_trigger = trigger_depth - triggerbar;
	/* Matches USB packet captures from official app/driver */
	if (triggerbar > 2)
		triggerbar -= 2;
	else {
		ramsize_trigger -= 1;
		triggerbar = 0;
	}

	analyzer_set_triggerbar_address(triggerbar);
	analyzer_set_ramsize_trigger_address(ramsize_trigger);

	sr_dbg("triggerbar_address = %d(0x%x)", triggerbar, triggerbar);
	sr_dbg("ramsize_triggerbar_address = %d(0x%x)",
	       ramsize_trigger, ramsize_trigger);
}
