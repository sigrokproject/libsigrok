/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "config.h"


SR_PRIV struct sr_dev_driver driver_info;
static struct sr_dev_driver *di = &driver_info;

/* TODO move to header file */
struct drv_context { GSList *instances; };
struct dev_context { };

/* Properly close and free all devices. */
static int clear_instances(void)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *l;

	drvc = di->priv;
	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("TODO: %s: sdi was NULL, continuing", __func__);
			continue;
		}
		if (!(devc = sdi->priv)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("TODO: %s: sdi->priv was NULL, continuing", __func__);
			continue;
		}

		/* TODO */

		sr_dev_inst_free(sdi);
	}

	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
}

static int hw_init(void)
{
	struct drv_context *drvc;

	if (!(drvc = g_try_malloc0(sizeof(struct drv_context)))) {
		sr_err("TODO: driver context malloc failed.");
		return SR_ERR;
	}

	/* TODO */

	di->priv = drvc;

	return SR_OK;
}

static GSList *hw_scan(GSList *options)
{
	struct drv_context *drvc;
	GSList *devices;

	(void)options;
	devices = NULL;
	drvc = di->priv;
	drvc->instances = NULL;

	/* TODO */

	return devices;
}

static GSList *hw_dev_list(void)
{
	struct drv_context *drvc;

	drvc = di->priv;

	return drvc->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{

	/* TODO */

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{

	/* TODO */

	return SR_OK;
}

static int hw_cleanup(void)
{

	clear_instances();

	/* TODO */

	return SR_OK;
}

static int hw_info_get(int info_id, const void **data,
       const struct sr_dev_inst *sdi)
{


	switch (info_id) {
	/* TODO */
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
		const void *value)
{
	int ret;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	ret = SR_OK;
	switch (hwcap) {
	/* TODO */
	default:
		ret = SR_ERR_ARG;
	}

	return ret;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{

	/* TODO */

	return SR_OK;
}

static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data)
{

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	/* TODO */

	return SR_OK;
}

SR_PRIV struct sr_dev_driver driver_info = {
	.name = "fluke-dmm",
	.longname = "Fluke 18x/28x series DMMs",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
