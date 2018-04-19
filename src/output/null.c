/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/null"

static int receive(const struct sr_output *o, const struct sr_datafeed_packet *packet,
		GString **out)
{
	(void)o;
	(void)packet;

	*out = NULL;

	return SR_OK;
}

SR_PRIV struct sr_output_module output_null = {
	.id = "null",
	.name = "Null output",
	.desc = "Null output (discards all data)",
	.exts = NULL,
	.flags = 0,
	.options = NULL,
	.receive = receive,
};
