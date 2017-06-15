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
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "input/null"

static int init(struct sr_input *in, GHashTable *options)
{
	(void)in;
	(void)options;

	return SR_OK;
}

static int receive(struct sr_input *in, GString *buf)
{
	(void)in;
	(void)buf;

	return SR_OK;
}

static int end(struct sr_input *in)
{
	(void)in;

	return SR_OK;
}

SR_PRIV struct sr_input_module input_null = {
	.id = "null",
	.name = "Null",
	.desc = "Null input (discards all input)",
	.exts = NULL,
	.options = NULL,
	.init = init,
	.receive = receive,
	.end = end,
	.reset = NULL,
};
