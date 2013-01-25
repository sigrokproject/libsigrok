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
#include "config.h" /* Needed for HAVE_LIBUSB_1_0 and others. */
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "device: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

/**
 * @file
 *
 * Device handling in libsigrok.
 */

/**
 * @defgroup grp_devices Devices
 *
 * Device handling in libsigrok.
 *
 * @{
 */

/** @private */
SR_PRIV struct sr_probe *sr_probe_new(int index, int type,
		gboolean enabled, const char *name)
{
	struct sr_probe *probe;

	if (!(probe = g_try_malloc0(sizeof(struct sr_probe)))) {
		sr_err("Probe malloc failed.");
		return NULL;
	}

	probe->index = index;
	probe->type = type;
	probe->enabled = enabled;
	if (name)
		probe->name = g_strdup(name);

	return probe;
}

/**
 * Set the name of the specified probe in the specified device.
 *
 * If the probe already has a different name assigned to it, it will be
 * removed, and the new name will be saved instead.
 *
 * @param sdi The device instance the probe is connected to.
 * @param probenum The number of the probe whose name to set.
 *                 Note that the probe numbers start at 0.
 * @param name The new name that the specified probe should get. A copy
 *             of the string is made.
 *
 * @return SR_OK on success, or SR_ERR_ARG on invalid arguments.
 */
SR_API int sr_dev_probe_name_set(const struct sr_dev_inst *sdi,
		int probenum, const char *name)
{
	GSList *l;
	struct sr_probe *probe;
	int ret;

	if (!sdi) {
		sr_err("%s: sdi was NULL", __func__);
		return SR_ERR_ARG;
	}

	ret = SR_ERR_ARG;
	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		if (probe->index == probenum) {
			g_free(probe->name);
			probe->name = g_strdup(name);
			ret = SR_OK;
			break;
		}
	}

	return ret;
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
 * @param probenum The probe number, starting from 0.
 * @param trigger Trigger string, in the format used by sigrok-cli
 *
 * @return SR_OK on success, or SR_ERR_ARG on invalid arguments.
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
 * Determine whether the specified device instance has the specified
 * capability.
 *
 * @param sdi Pointer to the device instance to be checked. Must not be NULL.
 *            If the device's 'driver' field is NULL (virtual device), this
 *            function will always return FALSE (virtual devices don't have
 *            a hardware capabilities list).
 * @param hwcap The capability that should be checked (whether it's supported
 *              by the specified device).
 *
 * @return TRUE if the device has the specified capability, FALSE otherwise.
 *         FALSE is also returned upon invalid input parameters or other
 *         error conditions.
 */
SR_API gboolean sr_dev_has_hwcap(const struct sr_dev_inst *sdi, int hwcap)
{
	const int *hwcaps;
	int i;

	if (!sdi || !sdi->driver)
		return FALSE;

	if (sdi->driver->config_list(SR_CONF_DEVICE_OPTIONS,
			(const void **)&hwcaps, NULL) != SR_OK)
		return FALSE;

	for (i = 0; hwcaps[i]; i++) {
		if (hwcaps[i] == hwcap)
			return TRUE;
	}

	return FALSE;
}

/** @private */
SR_PRIV struct sr_dev_inst *sr_dev_inst_new(int index, int status,
		const char *vendor, const char *model, const char *version)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = g_try_malloc(sizeof(struct sr_dev_inst)))) {
		sr_err("%s: sdi malloc failed", __func__);
		return NULL;
	}

	sdi->driver = NULL;
	sdi->index = index;
	sdi->status = status;
	sdi->inst_type = -1;
	sdi->vendor = vendor ? g_strdup(vendor) : NULL;
	sdi->model = model ? g_strdup(model) : NULL;
	sdi->version = version ? g_strdup(version) : NULL;
	sdi->probes = NULL;
	sdi->priv = NULL;

	return sdi;
}

/** @private */
SR_PRIV void sr_dev_inst_free(struct sr_dev_inst *sdi)
{
	struct sr_probe *probe;
	GSList *l;

	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		g_free(probe->name);
		g_free(probe);
	}

	g_free(sdi->priv);
	g_free(sdi->vendor);
	g_free(sdi->model);
	g_free(sdi->version);
	g_free(sdi);

}

#ifdef HAVE_LIBUSB_1_0

/** @private */
SR_PRIV struct sr_usb_dev_inst *sr_usb_dev_inst_new(uint8_t bus,
			uint8_t address, struct libusb_device_handle *hdl)
{
	struct sr_usb_dev_inst *udi;

	if (!(udi = g_try_malloc(sizeof(struct sr_usb_dev_inst)))) {
		sr_err("%s: udi malloc failed", __func__);
		return NULL;
	}

	udi->bus = bus;
	udi->address = address;
	udi->devhdl = hdl;

	return udi;
}

/** @private */
SR_PRIV void sr_usb_dev_inst_free(struct sr_usb_dev_inst *usb)
{
	(void)usb;

	/* Nothing to do for this device instance type. */
}

#endif

/** @private
 * @param pathname OS-specific serial port specification. Examples:
 * "/dev/ttyUSB0", "/dev/ttyACM1", "/dev/tty.Modem-0", "COM1".
 * @param serialcomm A serial communication parameters string, in the form
 * of <speed>/<data bits><parity><stopbits>, for example "9600/8n1" or
 * "600/7o2". This is an optional parameter; it may be filled in later.
 * @return A pointer to a newly initialized struct sr_serial_dev_inst,
 * or NULL on error.
 *
 * Both parameters are copied to newly allocated strings, and freed
 * automatically by sr_serial_dev_inst_free().
 */
SR_PRIV struct sr_serial_dev_inst *sr_serial_dev_inst_new(const char *port,
		const char *serialcomm)
{
	struct sr_serial_dev_inst *serial;

	if (!port) {
		sr_err("hwdriver: serial port required");
		return NULL;
	}

	if (!(serial = g_try_malloc0(sizeof(struct sr_serial_dev_inst)))) {
		sr_err("hwdriver: serial malloc failed");
		return NULL;
	}

	serial->port = g_strdup(port);
	if (serialcomm)
		serial->serialcomm = g_strdup(serialcomm);
	serial->fd = -1;

	return serial;
}

/** @private */
SR_PRIV void sr_serial_dev_inst_free(struct sr_serial_dev_inst *serial)
{

	g_free(serial->port);
	g_free(serial->serialcomm);
	g_free(serial);

}

SR_API int sr_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
		const void *value)
{
	int ret;

	if (!sdi || !sdi->driver || !sdi->driver->config_set) {
		sr_err("Unable to set config option.");
		return SR_ERR;
	}

	ret = sdi->driver->config_set(hwcap, value, sdi);

	return ret;
}

SR_API GSList *sr_dev_inst_list(const struct sr_dev_driver *driver)
{

	if (driver && driver->dev_list)
		return driver->dev_list();
	else
		return NULL;
}

SR_API int sr_dev_inst_clear(const struct sr_dev_driver *driver)
{

	if (driver && driver->dev_clear)
		return driver->dev_clear();
	else
		return SR_OK;
}

/** @} */
