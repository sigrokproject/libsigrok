/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

/** @cond PRIVATE */
#define LOG_PREFIX "device"
/** @endcond */

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

/** @private
 *  Allocate and initialize new struct sr_channel
 *  @param[in]  index @copydoc sr_channel::index
 *  @param[in]  type @copydoc sr_channel::type
 *  @param[in]  enabled @copydoc sr_channel::enabled
 *  @param[in]  name @copydoc sr_channel::name
 *
 *  @return NULL (failure) or new struct sr_channel*.
 */
SR_PRIV struct sr_channel *sr_channel_new(int index, int type,
		gboolean enabled, const char *name)
{
	struct sr_channel *ch;

	if (!(ch = g_try_malloc0(sizeof(struct sr_channel)))) {
		sr_err("Channel malloc failed.");
		return NULL;
	}

	ch->index = index;
	ch->type = type;
	ch->enabled = enabled;
	if (name)
		ch->name = g_strdup(name);

	return ch;
}

/**
 * Set the name of the specified channel in the specified device.
 *
 * If the channel already has a different name assigned to it, it will be
 * removed, and the new name will be saved instead.
 *
 * @param sdi The device instance the channel is connected to.
 * @param[in] channelnum The number of the channel whose name to set.
 *                 Note that the channel numbers start at 0.
 * @param[in] name The new name that the specified channel should get. A copy
 *             of the string is made.
 *
 * @return SR_OK on success, or SR_ERR_ARG on invalid arguments.
 *
 * @since 0.3.0
 */
SR_API int sr_dev_channel_name_set(const struct sr_dev_inst *sdi,
		int channelnum, const char *name)
{
	GSList *l;
	struct sr_channel *ch;
	int ret;

	if (!sdi) {
		sr_err("%s: sdi was NULL", __func__);
		return SR_ERR_ARG;
	}

	ret = SR_ERR_ARG;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->index == channelnum) {
			g_free(ch->name);
			ch->name = g_strdup(name);
			ret = SR_OK;
			break;
		}
	}

	return ret;
}

/**
 * Enable or disable a channel on the specified device.
 *
 * @param sdi The device instance the channel is connected to.
 * @param channelnum The channel number, starting from 0.
 * @param state TRUE to enable the channel, FALSE to disable.
 *
 * @return SR_OK on success or SR_ERR on failure.  In case of invalid
 *         arguments, SR_ERR_ARG is returned and the channel enabled state
 *         remains unchanged.
 *
 * @since 0.3.0
 */
SR_API int sr_dev_channel_enable(const struct sr_dev_inst *sdi, int channelnum,
		gboolean state)
{
	GSList *l;
	struct sr_channel *ch;
	int ret;
	gboolean was_enabled;

	if (!sdi)
		return SR_ERR_ARG;

	ret = SR_ERR_ARG;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->index == channelnum) {
			was_enabled = ch->enabled;
			ch->enabled = state;
			ret = SR_OK;
			if (!state != !was_enabled && sdi->driver
					&& sdi->driver->config_channel_set) {
				ret = sdi->driver->config_channel_set(
					sdi, ch, SR_CHANNEL_SET_ENABLED);
				/* Roll back change if it wasn't applicable. */
				if (ret == SR_ERR_ARG)
					ch->enabled = was_enabled;
			}
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
 * @param[in] key The option that should be checked for is supported by the
 *            specified device.
 *
 * @retval TRUE Device has the specified option
 * @retval FALSE Device does not have the specified option, invalid input
 *         parameters or other error conditions.
 *
 * @since 0.2.0
 */
SR_API gboolean sr_dev_has_option(const struct sr_dev_inst *sdi, int key)
{
	GVariant *gvar;
	const int *devopts;
	gsize num_opts, i;
	int ret;

	if (!sdi || !sdi->driver || !sdi->driver->config_list)
		return FALSE;

	if (sdi->driver->config_list(SR_CONF_DEVICE_OPTIONS,
				&gvar, sdi, NULL) != SR_OK)
		return FALSE;

	ret = FALSE;
	devopts = g_variant_get_fixed_array(gvar, &num_opts, sizeof(int32_t));
	for (i = 0; i < num_opts; i++) {
		if ((devopts[i] & SR_CONF_MASK) == key) {
			ret = TRUE;
			break;
		}
	}
	g_variant_unref(gvar);

	return ret;
}

/** @private
 *  Allocate and init new device instance struct.
 *  @param[in]  index   @copydoc sr_dev_inst::index
 *  @param[in]  status  @copydoc sr_dev_inst::status
 *  @param[in]  vendor  @copydoc sr_dev_inst::vendor
 *  @param[in]  model   @copydoc sr_dev_inst::model
 *  @param[in]  version @copydoc sr_dev_inst::version
 *
 *  @retval NULL Error
 *  @retval struct sr_dev_inst *. Dynamically allocated, free using
 *              sr_dev_inst_free().
 */
SR_PRIV struct sr_dev_inst *sr_dev_inst_new(int status,
		const char *vendor, const char *model, const char *version)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = g_try_malloc(sizeof(struct sr_dev_inst)))) {
		sr_err("Device instance malloc failed.");
		return NULL;
	}

	sdi->driver = NULL;
	sdi->status = status;
	sdi->inst_type = -1;
	sdi->vendor = vendor ? g_strdup(vendor) : NULL;
	sdi->model = model ? g_strdup(model) : NULL;
	sdi->version = version ? g_strdup(version) : NULL;
	sdi->serial_num = NULL;
	sdi->connection_id = NULL;
	sdi->channels = NULL;
	sdi->channel_groups = NULL;
	sdi->session = NULL;
	sdi->conn = NULL;
	sdi->priv = NULL;

	return sdi;
}

/** @private
 *  Free device instance struct created by sr_dev_inst().
 *  @param sdi device instance to free.
 */
SR_PRIV void sr_dev_inst_free(struct sr_dev_inst *sdi)
{
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	GSList *l;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		g_free(ch->name);
		g_free(ch->priv);
		g_free(ch);
	}
	g_slist_free(sdi->channels);

	for (l = sdi->channel_groups; l; l = l->next) {
		cg = l->data;
		g_free(cg->name);
		g_slist_free(cg->channels);
		g_free(cg->priv);
		g_free(cg);
	}
	g_slist_free(sdi->channel_groups);

	g_free(sdi->vendor);
	g_free(sdi->model);
	g_free(sdi->version);
	g_free(sdi->serial_num);
	g_free(sdi->connection_id);
	g_free(sdi);
}

#ifdef HAVE_LIBUSB_1_0

/** @private
 *  Allocate and init struct for USB device instance.
 *  @param[in]  bus @copydoc sr_usb_dev_inst::bus
 *  @param[in]  address @copydoc sr_usb_dev_inst::address
 *  @param[in]  hdl @copydoc sr_usb_dev_inst::devhdl
 *
 *  @retval NULL Error
 *  @retval other struct sr_usb_dev_inst * for USB device instance.
 */
SR_PRIV struct sr_usb_dev_inst *sr_usb_dev_inst_new(uint8_t bus,
			uint8_t address, struct libusb_device_handle *hdl)
{
	struct sr_usb_dev_inst *udi;

	if (!(udi = g_try_malloc(sizeof(struct sr_usb_dev_inst)))) {
		sr_err("USB device instance malloc failed.");
		return NULL;
	}

	udi->bus = bus;
	udi->address = address;
	udi->devhdl = hdl;

	return udi;
}

/** @private
 *  Free struct * allocated by sr_usb_dev_inst().
 *  @param usb  struct* to free. Must not be NULL.
 */
SR_PRIV void sr_usb_dev_inst_free(struct sr_usb_dev_inst *usb)
{
	g_free(usb);
}

#endif

#ifdef HAVE_LIBSERIALPORT

/**
 * @private
 *
 * Both parameters are copied to newly allocated strings, and freed
 * automatically by sr_serial_dev_inst_free().
 *
 * @param[in] port OS-specific serial port specification. Examples:
 *                 "/dev/ttyUSB0", "/dev/ttyACM1", "/dev/tty.Modem-0", "COM1".
 * @param[in] serialcomm A serial communication parameters string, in the form
 *              of \<speed\>/\<data bits\>\<parity\>\<stopbits\>, for example
 *              "9600/8n1" or "600/7o2". This is an optional parameter;
 *              it may be filled in later.
 *
 * @return A pointer to a newly initialized struct sr_serial_dev_inst,
 *         or NULL on error.
 */
SR_PRIV struct sr_serial_dev_inst *sr_serial_dev_inst_new(const char *port,
		const char *serialcomm)
{
	struct sr_serial_dev_inst *serial;

	if (!port) {
		sr_err("Serial port required.");
		return NULL;
	}

	if (!(serial = g_try_malloc0(sizeof(struct sr_serial_dev_inst)))) {
		sr_err("Serial device instance malloc failed.");
		return NULL;
	}

	serial->port = g_strdup(port);
	if (serialcomm)
		serial->serialcomm = g_strdup(serialcomm);

	return serial;
}

/** @private
 *  Free struct sr_serial_dev_inst * allocated by sr_serial_dev_inst().
 *  @param serial   struct sr_serial_dev_inst * to free. Must not be NULL.
 */
SR_PRIV void sr_serial_dev_inst_free(struct sr_serial_dev_inst *serial)
{
	g_free(serial->port);
	g_free(serial->serialcomm);
	g_free(serial);
}
#endif

/** @private */
SR_PRIV struct sr_usbtmc_dev_inst *sr_usbtmc_dev_inst_new(const char *device)
{
	struct sr_usbtmc_dev_inst *usbtmc;

	if (!device) {
		sr_err("Device name required.");
		return NULL;
	}

	if (!(usbtmc = g_try_malloc0(sizeof(struct sr_usbtmc_dev_inst)))) {
		sr_err("USBTMC device instance malloc failed.");
		return NULL;
	}

	usbtmc->device = g_strdup(device);
	usbtmc->fd = -1;

	return usbtmc;
}

/** @private */
SR_PRIV void sr_usbtmc_dev_inst_free(struct sr_usbtmc_dev_inst *usbtmc)
{
	g_free(usbtmc->device);
	g_free(usbtmc);
}

/**
 * Get the list of devices/instances of the specified driver.
 *
 * @param driver The driver to use. Must not be NULL.
 *
 * @return The list of devices/instances of this driver, or NULL upon errors
 *         or if the list is empty.
 *
 * @since 0.2.0
 */
SR_API GSList *sr_dev_list(const struct sr_dev_driver *driver)
{
	if (driver && driver->dev_list)
		return driver->dev_list();
	else
		return NULL;
}

/**
 * Clear the list of device instances a driver knows about.
 *
 * @param driver The driver to use. This must be a pointer to one of
 *               the entries returned by sr_driver_list(). Must not be NULL.
 *
 * @retval SR_OK Success
 * @retval SR_ERR_ARG Invalid driver
 *
 * @since 0.2.0
 */
SR_API int sr_dev_clear(const struct sr_dev_driver *driver)
{
	int ret;

	if (!driver) {
		sr_err("Invalid driver.");
		return SR_ERR_ARG;
	}

	if (driver->dev_clear)
		ret = driver->dev_clear();
	else
		ret = std_dev_clear(driver, NULL);

	return ret;
}

/**
 * Open the specified device.
 *
 * @param sdi Device instance to use. Must not be NULL.
 *
 * @return SR_OK upon success, a negative error code upon errors.
 *
 * @since 0.2.0
 */
SR_API int sr_dev_open(struct sr_dev_inst *sdi)
{
	int ret;

	if (!sdi || !sdi->driver || !sdi->driver->dev_open)
		return SR_ERR;

	ret = sdi->driver->dev_open(sdi);

	return ret;
}

/**
 * Close the specified device.
 *
 * @param sdi Device instance to use. Must not be NULL.
 *
 * @return SR_OK upon success, a negative error code upon errors.
 *
 * @since 0.2.0
 */
SR_API int sr_dev_close(struct sr_dev_inst *sdi)
{
	int ret;

	if (!sdi || !sdi->driver || !sdi->driver->dev_close)
		return SR_ERR;

	ret = sdi->driver->dev_close(sdi);

	return ret;
}

/**
 * Queries a device instances' driver.
 *
 * @param sdi Device instance to use. Must not be NULL.
 *
 * @return The driver instance or NULL on error.
 */
SR_API struct sr_dev_driver *sr_dev_inst_driver_get(struct sr_dev_inst *sdi)
{
	if (!sdi || !sdi->driver)
		return NULL;

	return sdi->driver;
}

/**
 * Queries a device instances' vendor.
 *
 * @param sdi Device instance to use. Must not be NULL.
 *
 * @return The vendor string or NULL.
 */
SR_API const char *sr_dev_inst_vendor_get(struct sr_dev_inst *sdi)
{
	if (!sdi)
		return NULL;

	return sdi->vendor;
}

/**
 * Queries a device instances' model.
 *
 * @param sdi Device instance to use. Must not be NULL.
 *
 * @return The model string or NULL.
 */
SR_API const char *sr_dev_inst_model_get(struct sr_dev_inst *sdi)
{
	if (!sdi)
		return NULL;

	return sdi->model;
}

/**
 * Queries a device instances' version.
 *
 * @param sdi Device instance to use. Must not be NULL.
 *
 * @return The version string or NULL.
 */
SR_API const char *sr_dev_inst_version_get(struct sr_dev_inst *sdi)
{
	if (!sdi)
		return NULL;

	return sdi->version;
}

/**
 * Queries a device instances' serial number.
 *
 * @param sdi Device instance to use. Must not be NULL.
 *
 * @return The serial number string or NULL.
 */
SR_API const char *sr_dev_inst_sernum_get(struct sr_dev_inst *sdi)
{
	if (!sdi)
		return NULL;

	return sdi->serial_num;
}

/**
 * Queries a device instances' connection identifier.
 *
 * @param sdi Device instance to use. Must not be NULL.
 *
 * @return A copy of the connection id string or NULL. The caller is responsible
 *         for g_free()ing the string when it is no longer needed.
 */
SR_API const char *sr_dev_inst_connid_get(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc;
	int r, cnt, i, a, b;
	char connection_id[64];

#ifdef HAVE_LIBUSB_1_0
	struct sr_usb_dev_inst *usb;
	struct libusb_device **devlist;
	struct libusb_device_descriptor des;
#endif

	if (!sdi)
		return NULL;

#ifdef HAVE_LIBSERIALPORT
	struct sr_serial_dev_inst *serial;

	if ((!sdi->connection_id) && (sdi->inst_type == SR_INST_SERIAL)) {
		/* connection_id isn't populated, let's do that here. */

		serial = sdi->conn;
		sdi->connection_id = g_strdup(serial->port);
	}
#endif


#ifdef HAVE_LIBUSB_1_0
	if ((!sdi->connection_id) && (sdi->inst_type == SR_INST_USB)) {
		/* connection_id isn't populated, let's do that here. */

		drvc = sdi->driver->priv;
		usb = sdi->conn;

		if ((cnt = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist)) < 0) {
			sr_err("Failed to retrieve device list: %s.",
			       libusb_error_name(cnt));
			return NULL;
		}

		for (i = 0; i < cnt; i++) {
			if ((r = libusb_get_device_descriptor(devlist[i], &des)) < 0) {
				sr_err("Failed to get device descriptor: %s.",
				       libusb_error_name(r));
				continue;
			}

			/* Find the USB device by the logical address we know. */
			b = libusb_get_bus_number(devlist[i]);
			a = libusb_get_device_address(devlist[i]);
			if (b != usb->bus || a != usb->address)
				continue;

			usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));
			sdi->connection_id = g_strdup(connection_id);
			break;
		}

		libusb_free_device_list(devlist, 1);
	}
#endif

	return sdi->connection_id;
}

/** @} */
