/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Lars-Peter Clausen <lars@metafoo.de>
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
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

SR_PRIV void sr_sw_limits_init(struct sr_sw_limits *limits)
{
	limits->limit_samples = 0;
	limits->limit_msec = 0;
}

SR_PRIV int sr_sw_limits_config_get(struct sr_sw_limits *limits, uint32_t key,
	GVariant **data)
{
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(limits->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(limits->limit_msec / 1000);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

SR_PRIV int sr_sw_limits_config_set(struct sr_sw_limits *limits, uint32_t key,
	GVariant *data)
{
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		limits->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_MSEC:
		limits->limit_msec = g_variant_get_uint64(data) * 1000;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

SR_PRIV void sr_sw_limits_acquisition_start(struct sr_sw_limits *limits)
{
	limits->samples_read = 0;
	limits->start_time = g_get_monotonic_time();
}

SR_PRIV gboolean sr_sw_limits_check(struct sr_sw_limits *limits)
{
	if (limits->limit_samples) {
		if (limits->samples_read >= limits->limit_samples)
			return TRUE;
	}

	if (limits->limit_msec) {
		guint64 now;
		now = g_get_monotonic_time();
		if (now > limits->start_time &&
			now - limits->start_time > limits->limit_msec)
			return TRUE;
	}

	return FALSE;
}

SR_PRIV void sr_sw_limits_update_samples_read(struct sr_sw_limits *limits,
	uint64_t samples_read)
{
	limits->samples_read += samples_read;
}
