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

#define LOG_PREFIX "serial-lcr-es51919"

struct lcr_es51919_info {
	struct sr_dev_driver di;
	const char *vendor;
	const char *model;
};

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di, es51919_serial_clean);
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct lcr_es51919_info *lcr;
	struct sr_dev_inst *sdi;

	lcr = (struct lcr_es51919_info *)di;

	if (!(sdi = es51919_serial_scan(options, lcr->vendor, lcr->model)))
		return NULL;

	return std_scan_complete(di, g_slist_append(NULL, sdi));
}

#define LCR_ES51919(id, vendor, model) \
	&((struct lcr_es51919_info) { \
		{ \
			.name = id, \
			.longname = vendor " " model, \
			.api_version = 1, \
			.init = std_init, \
			.cleanup = std_cleanup, \
			.scan = scan, \
			.dev_list = std_dev_list, \
			.dev_clear = dev_clear, \
			.config_get = es51919_serial_config_get, \
			.config_set = es51919_serial_config_set, \
			.config_list = es51919_serial_config_list, \
			.dev_open = std_serial_dev_open, \
			.dev_close = std_serial_dev_close, \
			.dev_acquisition_start = es51919_serial_acquisition_start, \
			.dev_acquisition_stop = std_serial_dev_acquisition_stop, \
			.context = NULL, \
		}, \
		vendor, model, \
	}).di

SR_REGISTER_DEV_DRIVER_LIST(lcr_es51919_drivers,
	LCR_ES51919("deree-de5000", "DER EE", "DE-5000"),
);
