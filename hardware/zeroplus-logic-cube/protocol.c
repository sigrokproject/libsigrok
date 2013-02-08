/*
 * This file is part of the sigrok project.
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
	else if (type == MEMORY_SIZE_64K)
		return 64 * 1024;
	else if (type == MEMORY_SIZE_128K)
		return 128 * 1024;
	else if (type == MEMORY_SIZE_512K)
		return 512 * 1024;
	else
		return 0;
}

SR_PRIV int zp_set_samplerate(struct dev_context *devc, uint64_t samplerate)
{
	int i;

	for (i = 0; zp_supported_samplerates_200[i]; i++)
		if (samplerate == zp_supported_samplerates_200[i])
			break;

	if (!zp_supported_samplerates_200[i] || samplerate > devc->max_samplerate) {
		sr_err("Unsupported samplerate: %" PRIu64 "Hz.", samplerate);
		return SR_ERR_ARG;
	}

	sr_info("Setting samplerate to %" PRIu64 "Hz.", samplerate);

	if (samplerate >= SR_MHZ(1))
		analyzer_set_freq(samplerate / SR_MHZ(1), FREQ_SCALE_MHZ);
	else if (samplerate >= SR_KHZ(1))
		analyzer_set_freq(samplerate / SR_KHZ(1), FREQ_SCALE_KHZ);
	else
		analyzer_set_freq(samplerate, FREQ_SCALE_HZ);

	devc->cur_samplerate = samplerate;

	return SR_OK;
}

SR_PRIV int set_limit_samples(struct dev_context *devc, uint64_t samples)
{
	devc->limit_samples = samples;

	if (samples <= 2 * 1024)
		devc->memory_size = MEMORY_SIZE_8K;
	else if (samples <= 16 * 1024)
		devc->memory_size = MEMORY_SIZE_64K;
	else if (samples <= 32 * 1024 || devc->max_memory_size <= 32 * 1024)
		devc->memory_size = MEMORY_SIZE_128K;
	else
		devc->memory_size = MEMORY_SIZE_512K;

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
	unsigned int ramsize, n, triggerbar;

	ramsize = get_memory_size(devc->memory_size) / 4;
	if (devc->trigger) {
		n = ramsize;
		if (devc->max_memory_size < n)
			n = devc->max_memory_size;
		if (devc->limit_samples < n)
			n = devc->limit_samples;
		n = n * devc->capture_ratio / 100;
		if (n > ramsize - 8)
			triggerbar = ramsize - 8;
		else
			triggerbar = n;
	} else {
		triggerbar = 0;
	}
	analyzer_set_triggerbar_address(triggerbar);
	analyzer_set_ramsize_trigger_address(ramsize - triggerbar);

	sr_dbg("triggerbar_address = %d(0x%x)", triggerbar, triggerbar);
	sr_dbg("ramsize_triggerbar_address = %d(0x%x)",
	       ramsize - triggerbar, ramsize - triggerbar);
}
