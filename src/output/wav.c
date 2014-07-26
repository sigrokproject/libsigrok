/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
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

#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/wav"

static int init(struct sr_output *o, GHashTable *options)
{
	(void)o;
	(void)options;

	return SR_OK;
}

static int receive(const struct sr_output *o, const struct sr_datafeed_packet *packet,
		GString **out)
{
	(void)o;
	(void)packet;
	(void)out;

	return SR_OK;
}

static int cleanup(struct sr_output *o)
{
	(void)o;

	return SR_OK;
}

SR_PRIV struct sr_output_module output_wav = {
	.id = "wav",
	.name = "WAV",
	.desc = "WAVE PCM sound module",
	.init = init,
	.receive = receive,
	.cleanup = cleanup,
};

