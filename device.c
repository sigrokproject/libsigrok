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

#include <stdio.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

/**
 * Set the name of the specified probe in the specified device.
 *
 * If the probe already has a different name assigned to it, it will be
 * removed, and the new name will be saved instead.
 *
 * @param dev TODO
 * @param probenum The number of the probe whose name to set.
 *                 Note that the probe numbers start at 1 (not 0!).
 * @param name The new name that the specified probe should get.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or SR_ERR
 *         upon other errors.
 *         If something other than SR_OK is returned, 'dev' is unchanged.
 */
SR_API int sr_dev_probe_name_set(struct sr_dev *dev, int probenum,
				 const char *name)
{
	struct sr_probe *p;

	if (!dev) {
		sr_err("dev: %s: dev was NULL", __func__);
		return SR_ERR_ARG;
	}

	p = sr_dev_probe_find(dev, probenum);
	if (!p) {
		sr_err("dev: %s: probe %d not found", __func__, probenum);
		return SR_ERR; /* TODO: More specific error? */
	}

	/* TODO: Sanity check on 'name'. */

	/* If the probe already has a name, kill it first. */
	g_free(p->name);

	p->name = g_strdup(name);

	return SR_OK;
}

/**
 * Enable or disable a probe on the specified device.
 *
 * @param sdi The device instance the probe is connected to.
 * @param probenum The probe number, starting from 0.
 * @param state TRUE to enable the probe, FALSE to disable.
 *
 * @return SR_OK on success, or SR_ERR_ARG on invalid arguments.
 */
SR_API int sr_dev_probe_enable(const struct sr_dev_inst *sdi, int probenum,
		gboolean state)
{
	GSList *l;
	struct sr_probe *probe;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;

	ret = SR_ERR_ARG;
	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		if (probe->index == probenum) {
			probe->enabled = state;
			ret = SR_OK;
			break;
		}
	}

	return ret;
}

/**
 * Add a trigger to the specified device (and the specified probe).
 *
 * If the specified probe of this device already has a trigger, it will
 * be silently replaced.
 *
 * @param sdi Must not be NULL.
 * @param probenum The number of the probe.
 *                 Note that the probe numbers start at 1 (not 0!).
 * @param trigger trigger string, in the format used by sigrok-cli
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments.
 */
SR_API int sr_dev_trigger_set(const struct sr_dev_inst *sdi, int probenum,
		const char *trigger)
{
	GSList *l;
	struct sr_probe *probe;
	int ret;

	if (!sdi)
		return SR_ERR_ARG;

	ret = SR_ERR_ARG;
	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		if (probe->index == probenum) {
			/* If the probe already has a trigger, kill it first. */
			g_free(probe->trigger);
			probe->trigger = g_strdup(trigger);
			ret = SR_OK;
			break;
		}
	}

	return ret;
}

/**
 * Determine whether the specified device has the specified capability.
 *
 * @param dev Pointer to the device instance to be checked. Must not be NULL.
 *            If the device's 'driver' field is NULL (virtual device), this
 *            function will always return FALSE (virtual devices don't have
 *            a hardware capabilities list).
 * @param hwcap The capability that should be checked (whether it's supported
 *              by the specified device).
 *
 * @return TRUE, if the device has the specified capability, FALSE otherwise.
 *         FALSE is also returned upon invalid input parameters or other
 *         error conditions.
 */
SR_API gboolean sr_dev_has_hwcap(const struct sr_dev_inst *sdi, int hwcap)
{
	const int *hwcaps;
	int i;

	if (!sdi || !sdi->driver)
		return FALSE;

	if (sdi->driver->info_get(SR_DI_HWCAPS,
			(const void **)&hwcaps, NULL) != SR_OK)
		return FALSE;

	for (i = 0; hwcaps[i]; i++) {
		if (hwcaps[i] == hwcap)
			return TRUE;
	}

	return FALSE;
}

