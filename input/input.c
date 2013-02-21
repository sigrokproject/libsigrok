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

#include "libsigrok.h"
#include "libsigrok-internal.h"

/**
 * @file
 *
 * Input file/data format handling.
 */

/**
 * @defgroup grp_input Input formats
 *
 * Input file/data format handling.
 *
 * @{
 */

/** @cond PRIVATE */
extern SR_PRIV struct sr_input_format input_chronovu_la8;
extern SR_PRIV struct sr_input_format input_binary;
extern SR_PRIV struct sr_input_format input_vcd;
extern SR_PRIV struct sr_input_format input_wav;
/* @endcond */

static struct sr_input_format *input_module_list[] = {
	&input_vcd,
	&input_chronovu_la8,
	&input_wav,
	/* This one has to be last, because it will take any input. */
	&input_binary,
	NULL,
};

SR_API struct sr_input_format **sr_input_list(void)
{
	return input_module_list;
}

/** @} */
