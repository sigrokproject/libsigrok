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

#include "sigrok.h"
#include "sigrok-internal.h"

extern SR_PRIV struct sr_input_format input_chronovu_la8;
extern SR_PRIV struct sr_input_format input_binary;

static struct sr_input_format *input_module_list[] = {
	&input_chronovu_la8,
	/* This one has to be last, because it will take any input. */
	&input_binary,
	NULL,
};

SR_API struct sr_input_format **sr_input_list(void)
{
	return input_module_list;
}
