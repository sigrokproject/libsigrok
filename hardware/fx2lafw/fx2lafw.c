/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
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

#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "sigrok.h"
#include "sigrok-internal.h"
#include "fx2lafw.h"


/*
 * API callbacks
 */

static int hw_init(const char *deviceinfo)
{
	(void)deviceinfo;
	return 0;
}

static int hw_dev_open(int device_index)
{
	(void)device_index;
	return SR_OK;
}

static int hw_dev_close(int device_index)
{
	(void)device_index;
	return SR_OK;
}

static int hw_cleanup(void)
{
	return SR_OK;
}

static void *hw_dev_info_get(int device_index, int device_info_id)
{
	(void)device_index;
	(void)device_info_id;
	return NULL;
}

static int hw_dev_status_get(int device_index)
{
	(void)device_index;
	return SR_ST_NOT_FOUND;
}

static int *hw_hwcap_get_all(void)
{
	return NULL;
}

static int hw_dev_config_set(int dev_index, int capability, void *value)
{
	(void)dev_index;
	(void)capability;
	(void)value;
	return SR_OK;
}

static int hw_dev_acquisition_start(int dev_index, gpointer session_data)
{
	(void)dev_index;
	(void)session_data;
	return SR_OK;
}

/* This stops acquisition on ALL devices, ignoring device_index. */
static int hw_dev_acquisition_stop(int dev_index, gpointer session_data)
{
	(void)dev_index;
	(void)session_data;
	return SR_OK;
}

SR_PRIV struct sr_dev_plugin fx2lafw_plugin_info = {
	.name = "fx2lafw",
	.longname = "fx2lafw",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_info_get = hw_dev_info_get,
	.dev_status_get = hw_dev_status_get,
	.hwcap_get_all = hw_hwcap_get_all,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
};
