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
#include "sigrok.h"
#include "sigrok-internal.h"

static GSList *devs = NULL;

/**
 * Scan the system for attached logic analyzers / devices.
 *
 * This will try to autodetect all supported logic analyzer devices:
 *
 *  - Those attached via USB (can be reliably detected via USB VID/PID).
 *
 *  - Those using a (real or virtual) serial port (detected by sending
 *    device-specific commands to all OS-specific serial port devices such
 *    as /dev/ttyS*, /dev/ttyUSB*, /dev/ttyACM*, and others).
 *    The autodetection for this kind of devices can potentially be unreliable.
 *
 *    Also, sending various bytes/commands to (all!) devices which happen to
 *    be attached to the system via a (real or virtual) serial port can be
 *    problematic. There is no way for libsigrok to know how unknown devices
 *    react to the bytes libsigrok sends. Potentially they could lead to the
 *    device getting into invalid/error states, losing/overwriting data, or...
 *
 * In addition to the detection, the devices that are found are also
 * initialized automatically. On some devices, this involves a firmware upload,
 * or other such measures.
 *
 * The order in which the system is scanned for devices is not specified. The
 * caller should not assume or rely on any specific order.
 *
 * After the system has been scanned for devices, the list of detected (and
 * supported) devices can be acquired via sr_dev_list().
 *
 * TODO: Error checks?
 * TODO: Option to only scan for specific devices or device classes.
 *
 * @return SR_OK upon success, SR_ERR_BUG upon internal errors.
 */
SR_API int sr_dev_scan(void)
{
	int i;
	struct sr_dev_driver **drivers;

	drivers = sr_driver_list();
	if (!drivers[0]) {
		sr_err("dev: %s: no supported hardware drivers", __func__);
		return SR_ERR_BUG;
	}

	/*
	 * Initialize all drivers first. Since the init() call may involve
	 * a firmware upload and associated delay, we may as well get all
	 * of these out of the way first.
	 */
	for (i = 0; drivers[i]; i++)
		sr_driver_init(drivers[i]);

	return SR_OK;
}

/**
 * Return the list of logic analyzer devices libsigrok has detected.
 *
 * If the libsigrok-internal device list is empty, a scan for attached
 * devices -- via a call to sr_dev_scan() -- is performed first.
 *
 * TODO: Error handling?
 *
 * @return The list (GSList) of detected devices, or NULL if none were found.
 */
SR_API GSList *sr_dev_list(void)
{
	if (!devs)
		sr_dev_scan();

	return devs;
}

/**
 * Create a new device.
 *
 * The device is added to the (libsigrok-internal) list of devices, but
 * additionally a pointer to the newly created device is also returned.
 *
 * The device has no probes attached to it yet after this call. You can
 * use sr_dev_probe_add() to add one or more probes.
 *
 * TODO: Should return int, so that we can return SR_OK, SR_ERR_* etc.
 *
 * It is the caller's responsibility to g_free() the allocated memory when
 * no longer needed. TODO: Using which API function?
 *
 * @param driver TODO.
 *               If 'driver' is NULL, the created device is a "virtual" one.
 * @param driver_index TODO
 *
 * @return Pointer to the newly allocated device, or NULL upon errors.
 */
SR_API struct sr_dev *sr_dev_new(const struct sr_dev_driver *driver,
				 int driver_index)
{
	struct sr_dev *dev;

	/* TODO: Check if driver_index valid? */

	if (!(dev = g_try_malloc0(sizeof(struct sr_dev)))) {
		sr_err("dev: %s: dev malloc failed", __func__);
		return NULL;
	}

	dev->driver = (struct sr_dev_driver *)driver;
	dev->driver_index = driver_index;
	devs = g_slist_append(devs, dev);

	return dev;
}

/**
 * Add a probe with the specified name to the specified device.
 *
 * The added probe is automatically enabled (the 'enabled' field is TRUE).
 *
 * The 'trigger' field of the added probe is set to NULL. A trigger can be
 * added via sr_dev_trigger_set().
 *
 * TODO: Are duplicate names allowed?
 * TODO: Do we enforce a maximum probe number for a device?
 * TODO: Error if the max. probe number for the specific LA is reached, e.g.
 *       if the caller tries to add more probes than the device actually has.
 *
 * @param dev The device to which to add a probe with the specified name.
 *            Must not be NULL.
 * @param name The name of the probe to add to this device. Must not be NULL.
 *             TODO: Maximum length, allowed characters, etc.
 *
 * @return SR_OK upon success, SR_ERR_MALLOC upon memory allocation errors,
 *         or SR_ERR_ARG upon invalid arguments.
 *         If something other than SR_OK is returned, 'dev' is unchanged.
 */
SR_API int sr_dev_probe_add(struct sr_dev *dev, const char *name)
{
	struct sr_probe *p;
	int probenum;

	if (!dev) {
		sr_err("dev: %s: dev was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!name) {
		sr_err("dev: %s: name was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* TODO: Further checks to ensure name is valid. */

	probenum = g_slist_length(dev->probes) + 1;

	if (!(p = g_try_malloc0(sizeof(struct sr_probe)))) {
		sr_err("dev: %s: p malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	p->index = probenum;
	p->enabled = TRUE;
	p->name = g_strdup(name);
	p->trigger = NULL;
	dev->probes = g_slist_append(dev->probes, p);

	return SR_OK;
}

/**
 * Find the probe with the specified number in the specified device.
 *
 * TODO
 *
 * @param dev TODO. Must not be NULL.
 * @param probenum The number of the probe whose 'struct sr_probe' we want.
 *                 Note that the probe numbers start at 1 (not 0!).
 *
 * TODO: Should return int.
 * TODO: probenum should be unsigned.
 *
 * @return A pointer to the requested probe's 'struct sr_probe', or NULL
 *         if the probe could not be found.
 */
SR_API struct sr_probe *sr_dev_probe_find(const struct sr_dev *dev,
					  int probenum)
{
	GSList *l;
	struct sr_probe *p, *found_probe;

	if (!dev) {
		sr_err("dev: %s: dev was NULL", __func__);
		return NULL; /* TODO: SR_ERR_ARG */
	}

	/* TODO: Sanity check on probenum. */

	found_probe = NULL;
	for (l = dev->probes; l; l = l->next) {
		p = l->data;
		/* TODO: Check for p != NULL. */
		if (p->index == probenum) {
			found_probe = p;
			break;
		}
	}

	return found_probe;
}

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
 * Remove all triggers set up for the specified device.
 *
 * TODO: Better description.
 *
 * @param dev TODO
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments.
 *         If something other than SR_OK is returned, 'dev' is unchanged.
 */
SR_API int sr_dev_trigger_clear(struct sr_dev *dev)
{
	struct sr_probe *p;
	unsigned int pnum; /* TODO: uint16_t? */

	if (!dev) {
		sr_err("dev: %s: dev was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!dev->probes) {
		sr_err("dev: %s: dev->probes was NULL", __func__);
		return SR_ERR_ARG;
	}

	for (pnum = 1; pnum <= g_slist_length(dev->probes); pnum++) {
		p = sr_dev_probe_find(dev, pnum);
		/* TODO: Silently ignore probes which cannot be found? */
		if (p) {
			g_free(p->trigger);
			p->trigger = NULL;
		}
	}

	return SR_OK;
}

/**
 * Add a trigger to the specified device.
 *
 * TODO: Better description.
 * TODO: Describe valid format of the 'trigger' string.
 *
 * @param dev TODO. Must not be NULL.
 * @param probenum The number of the probe. TODO.
 *                 Note that the probe numbers start at 1 (not 0!).
 * @param trigger TODO.
 *                TODO: Is NULL allowed?
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or SR_ERR
 *         upon other errors.
 *         If something other than SR_OK is returned, 'dev' is unchanged.
 */
SR_API int sr_dev_trigger_set(struct sr_dev *dev, int probenum,
			      const char *trigger)
{
	struct sr_probe *p;

	if (!dev) {
		sr_err("dev: %s: dev was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* TODO: Sanity check on 'probenum'. */

	/* TODO: Sanity check on 'trigger'. */

	p = sr_dev_probe_find(dev, probenum);
	if (!p) {
		sr_err("dev: %s: probe %d not found", __func__, probenum);
		return SR_ERR; /* TODO: More specific error? */
	}

	/* If the probe already has a trigger, kill it first. */
	g_free(p->trigger);

	p->trigger = g_strdup(trigger);

	return SR_OK;
}

/**
 * Determine whether the specified device has the specified capability.
 *
 * @param dev Pointer to the device to be checked. Must not be NULL.
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
SR_API gboolean sr_dev_has_hwcap(const struct sr_dev *dev, int hwcap)
{
	int *hwcaps, i;

	sr_spew("dev: %s: requesting hwcap %d", __func__, hwcap);

	if (!dev) {
		sr_err("dev: %s: dev was NULL", __func__);
		return FALSE;
	}

	/*
	 * Virtual devices (which have dev->driver set to NULL) always say that
	 * they don't have the capability (they can't call hwcap_get_all()).
	 */
	if (!dev->driver) {
		sr_dbg("dev: %s: dev->driver was NULL, this seems to be "
		       "a virtual device without capabilities", __func__);
		return FALSE;
	}

	/* TODO: Sanity check on 'hwcap'. */

	if (!(hwcaps = dev->driver->hwcap_get_all())) {
		sr_err("dev: %s: dev has no capabilities", __func__);
		return FALSE;
	}

	for (i = 0; hwcaps[i]; i++) {
		if (hwcaps[i] != hwcap)
			continue;
		sr_spew("dev: %s: found hwcap %d", __func__, hwcap);
		return TRUE;
	}

	sr_spew("dev: %s: hwcap %d not found", __func__, hwcap);

	return FALSE;
}

/**
 * Returns information about the given device.
 *
 * @param dev Pointer to the device to be checked. Must not be NULL.
 *            The device's 'driver' field must not be NULL either.
 * @param id The type of information.
 * @param data The return value. Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or SR_ERR
 *         upon other errors.
 */
SR_API int sr_dev_info_get(const struct sr_dev *dev, int id, const void **data)
{
	if ((dev == NULL) || (dev->driver == NULL))
		return SR_ERR_ARG;

	if (data == NULL)
		return SR_ERR_ARG;

	*data = dev->driver->dev_info_get(dev->driver_index, id);

	if (*data == NULL)
		return SR_ERR;

	return SR_OK;
}
