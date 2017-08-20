/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Marcus Comstedt <marcus@mc.pp.se>
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
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/*
 * This marks the start of the driver list. This file must be linked
 * before any actual drivers.
 */

SR_PRIV const struct sr_dev_driver *sr_driver_list__start[]
	__attribute__((section (SR_DRIVER_LIST_SECTION),
		       used, aligned(sizeof(struct sr_dev_driver *))))
  = { NULL /* Dummy item, as zero length arrays are not allowed by C99 */ };
