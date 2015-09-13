/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Uwe Hermann <uwe@hermann-uwe.de>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <config.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "transform/nop"

static int receive(const struct sr_transform *t,
		struct sr_datafeed_packet *packet_in,
		struct sr_datafeed_packet **packet_out)
{
	if (!t || !t->sdi || !packet_in || !packet_out)
		return SR_ERR_ARG;

	/* Do nothing, just pass on packets unmodified. */
	sr_spew("Received packet of type %d, passing on unmodified.", packet_in->type);
	*packet_out = packet_in;

	return SR_OK;
}

SR_PRIV struct sr_transform_module transform_nop = {
	.id = "nop",
	.name = "NOP",
	.desc = "Do nothing",
	.options = NULL,
	.init = NULL,
	.receive = receive,
	.cleanup = NULL,
};
