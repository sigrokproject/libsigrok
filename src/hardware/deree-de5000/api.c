/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Janne Huttunen <jahuttun@gmail.com>
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
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

static void std_dev_attach(struct sr_dev_driver *di, struct sr_dev_inst *sdi)
{
	struct drv_context *drvc;

	drvc = di->context;

	sdi->driver = di;
	drvc->instances = g_slist_append(drvc->instances, sdi);
}

#define LOG_PREFIX "deree-de5000"

SR_PRIV struct sr_dev_driver deree_de5000_driver_info;

static int init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, es51919_serial_clean);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = es51919_serial_scan(options, "DER EE", "DE-5000")))
		return NULL;

	std_dev_attach(di, sdi);

	return g_slist_append(NULL, sdi);
}

SR_PRIV struct sr_dev_driver deree_de5000_driver_info = {
	.name = "deree-de5000",
	.longname = "DER EE DE-5000",
	.api_version = 1,
	.init = init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = es51919_serial_config_get,
	.config_set = es51919_serial_config_set,
	.config_list = es51919_serial_config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = es51919_serial_acquisition_start,
	.dev_acquisition_stop = es51919_serial_acquisition_stop,
	.context = NULL,
};
