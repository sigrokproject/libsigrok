/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Lars-Peter Clausen <lars@metafoo.de>
 * Copyright (C) 2016 Aurelien Jacobs <aurel@gnuage.org>
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
 * The special __sr_driver_list section contains pointers to all hardware
 * drivers which were built into the library according to its configuration
 * (will depend on the availability of dependencies, as well as user provided
 * specs). The __start and __stop symbols point to the start and end of the
 * section. They are used to iterate over the list of all drivers which were
 * included in the library.
 */
SR_PRIV extern const struct sr_dev_driver *sr_driver_list__start[];
SR_PRIV extern const struct sr_dev_driver *sr_driver_list__stop[];

/**
 * Initialize the driver list in a fresh libsigrok context.
 *
 * @param ctx Pointer to a libsigrok context struct. Must not be NULL.
 *
 * @private
 */
SR_API void sr_drivers_init(struct sr_context *ctx)
{
	GArray *array;

	array = g_array_new(TRUE, FALSE, sizeof(struct sr_dev_driver *));
#ifdef HAVE_DRIVERS
	for (const struct sr_dev_driver **drivers = sr_driver_list__start + 1;
	     drivers < sr_driver_list__stop; drivers++)
		g_array_append_val(array, *drivers);
#endif
	ctx->driver_list = (struct sr_dev_driver **)g_array_free(array, FALSE);
}
