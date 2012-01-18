/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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

static GSList *devices = NULL;

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
 * supported) devices can be acquired via sr_device_list().
 *
 * TODO: Error checks?
 * TODO: Option to only scan for specific devices or device classes.
 *
 * @return SR_OK upon success, SR_ERR upon errors.
 */
int sr_device_scan(void)
{
	GSList *plugins, *l;
	struct sr_device_plugin *plugin;

	if (!(plugins = sr_list_hwplugins())) {
		sr_err("dev: %s: no supported devices/hwplugins", __func__);
		return SR_ERR; /* TODO: More specific error? */
	}

	/*
	 * Initialize all plugins first. Since the init() call may involve
	 * a firmware upload and associated delay, we may as well get all
	 * of these out of the way first.
	 */
	for (l = plugins; l; l = l->next) {
		plugin = l->data;
		/* TODO: Handle 'plugin' being NULL. */
		sr_init_hwplugins(plugin);
	}

	return SR_OK;
}

/**
 * Return the list of logic analyzer devices libsigrok has detected.
 *
 * If the libsigrok-internal device list is empty, a scan for attached
 * devices -- via a call to sr_device_scan() -- is performed first.
 *
 * TODO: Error handling?
 *
 * @return The list (GSList) of detected devices, or NULL if none were found.
 */
GSList *sr_device_list(void)
{
	if (!devices)
		sr_device_scan();

	return devices;
}

/**
 * Create a new device.
 *
 * The device is added to the (libsigrok-internal) list of devices, but
 * additionally a pointer to the newly created device is also returned.
 *
 * The device has no probes attached to it yet after this call. You can
 * use sr_device_probe_add() to add one or more probes.
 *
 * TODO: Should return int, so that we can return SR_OK, SR_ERR_* etc.
 *
 * It is the caller's responsibility to g_free() the allocated memory when
 * no longer needed. TODO: Using which API function?
 *
 * @param plugin TODO.
 *               If 'plugin' is NULL, the created device is a "virtual" one.
 * @param plugin_index TODO
 *
 * @return Pointer to the newly allocated device, or NULL upon errors.
 */
struct sr_device *sr_device_new(const struct sr_device_plugin *plugin,
				int plugin_index)
{
	struct sr_device *device;

	/* TODO: Check if plugin_index valid? */

	if (!(device = g_try_malloc0(sizeof(struct sr_device)))) {
		sr_err("dev: %s: device malloc failed", __func__);
		return NULL;
	}

	device->plugin = (struct sr_device_plugin *)plugin;
	device->plugin_index = plugin_index;
	devices = g_slist_append(devices, device);

	return device;
}

/**
 * Clear all probes of the specified device.
 *
 * This removes/clears the 'name' and 'trigger' fields of all probes of
 * the device.
 *
 * The order in which the probes are cleared is not specified. The caller
 * should not assume or rely on a specific order.
 *
 * TODO: Rename to sr_device_clear_probes() or sr_device_probe_clear_all().
 *
 * @param device The device whose probes to clear. Must not be NULL.
 *               Note: device->probes is allowed to be NULL (in that case,
 *               there are no probes, thus none have to be cleared).
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments.
 *         If something other than SR_OK is returned, 'device' is unchanged.
 */
int sr_device_clear(struct sr_device *device)
{
	unsigned int pnum;

	if (!device) {
		sr_err("dev: %s: device was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* Note: device->probes can be NULL, this is handled correctly. */

	for (pnum = 1; pnum <= g_slist_length(device->probes); pnum++)
		sr_device_probe_clear(device, pnum);

	return SR_OK;
}

/**
 * Clear the specified probe in the specified device.
 *
 * The probe itself still exists afterwards, but its 'name' and 'trigger'
 * fields are g_free()'d and set to NULL.
 *
 * @param device The device in which the specified (to be cleared) probe
 *               resides. Must not be NULL.
 * @param probenum The number of the probe to clear.
 *                 Note that the probe numbers start at 1 (not 0!).
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or SR_ERR
 *         upon other errors.
 *         If something other than SR_OK is returned, 'device' is unchanged.
 */
int sr_device_probe_clear(struct sr_device *device, int probenum)
{
	struct sr_probe *p;

	if (!device) {
		sr_err("dev: %s: device was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* TODO: Sanity check on 'probenum'. */

	if (!(p = sr_device_probe_find(device, probenum))) {
		sr_err("dev: %s: probe %d not found", __func__, probenum);
		return SR_ERR; /* TODO: More specific error? */
	}

	/* If the probe has a name, remove it. */
	g_free(p->name);
	p->name = NULL;

	/* If the probe has a trigger, remove it. */
	g_free(p->trigger);
	p->trigger = NULL;

	return SR_OK;
}

/**
 * Add a probe with the specified name to the specified device.
 *
 * The added probe is automatically enabled (the 'enabled' field is TRUE).
 *
 * The 'trigger' field of the added probe is set to NULL. A trigger can be
 * added via sr_device_trigger_set().
 *
 * TODO: Are duplicate names allowed?
 * TODO: Do we enforce a maximum probe number for a device?
 * TODO: Error if the max. probe number for the specific LA is reached, e.g.
 *       if the caller tries to add more probes than the device actually has.
 *
 * @param device The device to which to add a probe with the specified name.
 *               Must not be NULL.
 * @param name The name of the probe to add to this device. Must not be NULL.
 *             TODO: Maximum length, allowed characters, etc.
 *
 * @return SR_OK upon success, SR_ERR_MALLOC upon memory allocation errors,
 *         or SR_ERR_ARG upon invalid arguments.
 *         If something other than SR_OK is returned, 'device' is unchanged.
 */
int sr_device_probe_add(struct sr_device *device, const char *name)
{
	struct sr_probe *p;
	int probenum;

	if (!device) {
		sr_err("dev: %s: device was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!name) {
		sr_err("dev: %s: name was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* TODO: Further checks to ensure name is valid. */

	probenum = g_slist_length(device->probes) + 1;

	if (!(p = g_try_malloc0(sizeof(struct sr_probe)))) {
		sr_err("dev: %s: p malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	p->index = probenum;
	p->enabled = TRUE;
	p->name = g_strdup(name);
	p->trigger = NULL;
	device->probes = g_slist_append(device->probes, p);

	return SR_OK;
}

/**
 * Find the probe with the specified number in the specified device.
 *
 * TODO
 *
 * @param device TODO. Must not be NULL.
 * @param probenum The number of the probe whose 'struct sr_probe' we want.
 *                 Note that the probe numbers start at 1 (not 0!).
 *
 * TODO: Should return int.
 * TODO: probenum should be unsigned.
 *
 * @return A pointer to the requested probe's 'struct sr_probe', or NULL
 *         if the probe could not be found.
 */
struct sr_probe *sr_device_probe_find(const struct sr_device *device,
				      int probenum)
{
	GSList *l;
	struct sr_probe *p, *found_probe;

	if (!device) {
		sr_err("dev: %s: device was NULL", __func__);
		return NULL; /* TODO: SR_ERR_ARG */
	}

	/* TODO: Sanity check on probenum. */

	found_probe = NULL;
	for (l = device->probes; l; l = l->next) {
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
 * TODO: Rename to sr_device_set_probe_name().
 *
 * @param device TODO
 * @param probenum The number of the probe whose name to set.
 *                 Note that the probe numbers start at 1 (not 0!).
 * @param name The new name that the specified probe should get.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or SR_ERR
 *         upon other errors.
 *         If something other than SR_OK is returned, 'device' is unchanged.
 */
int sr_device_probe_name(struct sr_device *device, int probenum,
			 const char *name)
{
	struct sr_probe *p;

	if (!device) {
		sr_err("dev: %s: device was NULL", __func__);
		return SR_ERR_ARG;
	}

	p = sr_device_probe_find(device, probenum);
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
 * @param device TODO
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments.
 *         If something other than SR_OK is returned, 'device' is unchanged.
 */
int sr_device_trigger_clear(struct sr_device *device)
{
	struct sr_probe *p;
	unsigned int pnum; /* TODO: uint16_t? */

	if (!device) {
		sr_err("dev: %s: device was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!device->probes) {
		sr_err("dev: %s: device->probes was NULL", __func__);
		return SR_ERR_ARG;
	}

	for (pnum = 1; pnum <= g_slist_length(device->probes); pnum++) {
		p = sr_device_probe_find(device, pnum);
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
 * @param device TODO. Must not be NULL.
 * @param probenum The number of the probe. TODO.
 *                 Note that the probe numbers start at 1 (not 0!).
 * @param trigger TODO.
 *                TODO: Is NULL allowed?
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or SR_ERR
 *         upon other errors.
 *         If something other than SR_OK is returned, 'device' is unchanged.
 */
int sr_device_trigger_set(struct sr_device *device, int probenum,
			  const char *trigger)
{
	struct sr_probe *p;

	if (!device) {
		sr_err("dev: %s: device was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* TODO: Sanity check on 'probenum'. */

	/* TODO: Sanity check on 'trigger'. */

	p = sr_device_probe_find(device, probenum);
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
 * TODO: Should return int?
 *
 * @param device Pointer to the device to be checked. Must not be NULL.
 *               The device's 'plugin' field must not be NULL either.
 * @param hwcap The capability that should be checked (whether it's supported
 *              by the specified device).
 *
 * @return TRUE, if the device has the specified capability, FALSE otherwise.
 *         FALSE is also returned upon invalid input parameters or other
 *         error conditions.
 */
gboolean sr_device_has_hwcap(const struct sr_device *device, int hwcap)
{
	int *capabilities, i;

	if (!device) {
		sr_err("dev: %s: device was NULL", __func__);
		return FALSE; /* TODO: SR_ERR_ARG. */
	}

	if (!device->plugin) {
		sr_err("dev: %s: device->plugin was NULL", __func__);
		return FALSE; /* TODO: SR_ERR_ARG. */
	}

	/* TODO: Sanity check on 'hwcap'. */

	if (!(capabilities = device->plugin->get_capabilities())) {
		sr_err("dev: %s: device has no capabilities", __func__);
		return FALSE; /* TODO: SR_ERR*. */
	}

	for (i = 0; capabilities[i]; i++) {
		if (capabilities[i] != hwcap)
			continue;
		sr_spew("dev: %s: found hwcap %d", __func__, hwcap);
		return TRUE;
	}

	sr_spew("dev: %s: hwcap %d not found", __func__, hwcap);

	return FALSE;
}
