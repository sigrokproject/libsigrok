/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include "protocol.h"

SR_PRIV struct sr_dev_driver mic_985xx_driver_info;
static struct sr_dev_driver *di = &mic_985xx_driver_info;

/* Properly close and free all devices. */
static int clear_instances(void)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *l;

	if (!(drvc = di->priv))
		return SR_OK;

	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data))
			continue;
		if (!(devc = sdi->priv))
			continue;

		/* TODO */

		sr_dev_inst_free(sdi);
	}

	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
}

static int hw_init(struct sr_context *sr_ctx)
{
	return std_hw_init(sr_ctx, di, DRIVER_LOG_DOMAIN);
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
	(void)sdi;

	/* TODO */

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	(void)sdi;

	/* TODO */

	return SR_OK;
}

static int hw_cleanup(void)
{
	clear_instances();

	/* TODO */

	return SR_OK;
}

static int hw_config_get(int id, const void **value,
			 const struct sr_dev_inst *sdi)
{
	(void)sdi;
	(void)value;

	switch (id) {
	/* TODO */
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_config_set(int id, const void *value,
			 const struct sr_dev_inst *sdi)
{
	(void)value;

	int ret;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't set config options.");
		return SR_ERR;
	}

	ret = SR_OK;
	switch (id) {
	/* TODO */
	default:
		sr_err("Unknown hardware capability: %d.", id);
		ret = SR_ERR_ARG;
	}

	return ret;
}

static int hw_config_list(int key, const void **data,
			  const struct sr_dev_inst *sdi)
{
	(void)sdi;
	(void)data;

	switch (key) {
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	(void)sdi;
	(void)cb_data;

	/* TODO */

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't stop acquisition.");
		return SR_ERR;
	}

	/* TODO */

	return SR_OK;
}

SR_PRIV struct sr_dev_driver mic_985xx_driver_info = {
	.name = "mic-985xx",
	.longname = "MIC 985xx",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.config_get = hw_config_get,
	.config_set = hw_config_set,
	.config_list = hw_config_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
