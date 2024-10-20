/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Lars-Peter Clausen <lars@metafoo.de>
 * Copyright (C) 2016 Aurelien Jacobs <aurel@gnuage.org>
 * Copyright (C) 2017 Marcus Comstedt <marcus@mc.pp.se>
 * Copyright (C) 2023 fenugrec <fenugrec@users.sourceforge.net>
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

#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

static struct device_node *devlist_head = NULL;

/* constructors will call this, before main() and GLib init.
 * We only assume static variables were initialized (i.e. contents of .bss
 * and .data sections).
 */
void sr_register_dev_node(struct device_node *devnode) {
	devnode->next = devlist_head;
	devlist_head = devnode;
}

void sr_register_dev_array(struct sr_dev_driver * const driver_array[], struct device_node *node_array, unsigned num) {
	unsigned i;
	struct device_node *dnode;

	for (i = 0; i < num; i++) {
		dnode = &node_array[i];
		dnode->dev = driver_array[i];
		sr_register_dev_node(dnode);
	}
}


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
	for (struct device_node *cur = devlist_head; cur; cur = cur->next) {
		g_array_append_val(array, cur->dev);
	}
	ctx->driver_list = (struct sr_dev_driver **)g_array_free(array, FALSE);
}
