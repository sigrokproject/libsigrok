/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Matthias Heidbrink <m-sigrok@heidbrink.biz>
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

/**
 * @file
 *
 * BBC Goerz Metrawatt M2110 ASCII protocol parser.
 *
 * Most probably the simplest multimeter protocol ever ;-) .
 */

#include <string.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "m2110"

SR_PRIV gboolean sr_m2110_packet_valid(const uint8_t *buf)
{
	float val;

	if ((buf[7] != '\r') || (buf[8] != '\n'))
		return FALSE;

	if (!strncmp((const char *)buf, "OVERRNG", 7))
		return TRUE;

	if (sscanf((const char *)buf, "%f", &val) == 1)
		return TRUE;
	else
		return FALSE;
}

SR_PRIV int sr_m2110_parse(const uint8_t *buf, float *floatval,
				struct sr_datafeed_analog *analog, void *info)
{
	float val;

	(void)info;

	/* We don't know the unit, so that's the best we can do. */
	analog->mq = SR_MQ_GAIN;
	analog->unit = SR_UNIT_UNITLESS;
	analog->mqflags = 0;

	if (!strncmp((const char *)buf, "OVERRNG", 7))
		*floatval = INFINITY;
	else if (sscanf((const char *)buf, "%f", &val) == 1)
		*floatval = val;

	return SR_OK;
}
