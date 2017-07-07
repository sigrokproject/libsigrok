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

#include <config.h>
#include <stdio.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
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

/**
 * Allocate and initialize a new struct sr_channel and add it to sdi.
 *
 * @param[in] sdi The device instance the channel is connected to.
 *                Must not be NULL.
 * @param[in] index @copydoc sr_channel::index
 * @param[in] type @copydoc sr_channel::type
 * @param[in] enabled @copydoc sr_channel::enabled
 * @param[in] name @copydoc sr_channel::name
 *
 * @return A new struct sr_channel*.
 *
 * @private
 */
SR_PRIV struct sr_channel *sr_channel_new(struct sr_dev_inst *sdi,
		int index, int type, gboolean enabled, const char *name)
{
	struct sr_channel *ch;

	ch = g_malloc0(sizeof(struct sr_channel));
	ch->sdi = sdi;
	ch->index = index;
	ch->type = type;
	ch->enabled = enabled;
	if (name)
		ch->name = g_strdup(name);

	sdi->channels = g_slist_append(sdi->channels, ch);

	return ch;
}

/**
 * Set the name of the specified channel.
 *
 * If the channel already has a different name assigned to it, it will be
 * removed, and the new name will be saved instead.
 *
 * @param[in] channel The channel whose name to set. Must not be NULL.
 * @param[in] name The new name that the specified channel should get.
 *                 A copy of the string is made.
 *
 * @return SR_OK on success, or SR_ERR_ARG on invalid arguments.
 *
 * @since 0.3.0
 */
SR_API int sr_dev_channel_name_set(struct sr_channel *channel,
		const char *name)
{
	if (!channel)
		return SR_ERR_ARG;

	g_free(channel->name);
	channel->name = g_strdup(name);

	return SR_OK;
}

/**
 * Enable or disable a channel.
 *
 * @param[in] channel The channel to enable or disable. Must not be NULL.
 * @param[in] state TRUE to enable the channel, FALSE to disable.
 *
 * @return SR_OK on success or SR_ERR on failure. In case of invalid
 *         arguments, SR_ERR_ARG is returned and the channel enabled state
 *         remains unchanged.
 *
 * @since 0.3.0
 */
SR_API int sr_dev_channel_enable(struct sr_channel *channel, gboolean state)
{
	int ret;
	gboolean was_enabled;
	struct sr_dev_inst *sdi;

	if (!channel)
		return SR_ERR_ARG;

	sdi = channel->sdi;
	was_enabled = channel->enabled;
	channel->enabled = state;
	if (!state != !was_enabled && sdi->driver
			&& sdi->driver->config_channel_set) {
		ret = sdi->driver->config_channel_set(
			sdi, channel, SR_CHANNEL_SET_ENABLED);
		/* Roll back change if it wasn't applicable. */
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

/* Returns the next enabled channel, wrapping around if necessary. */
/** @private */
SR_PRIV struct sr_channel *sr_next_enabled_channel(const struct sr_dev_inst *sdi,

		struct sr_channel *cur_channel)
{
	struct sr_channel *next_channel;
	GSList *l;

	next_channel = cur_channel;
	do {
		l = g_slist_find(sdi->channels, next_channel);
		if (l && l->next)
			next_channel = l->next->data;
		else
			next_channel = sdi->channels->data;
	} while (!next_channel->enabled);

	return next_channel;
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
 * @retval TRUE Device has the specified option.
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

/**
 * Enumerate the configuration options of the specified item.
 *
 * @param driver Pointer to the driver to be checked. Must not be NULL.
 * @param sdi Pointer to the device instance to be checked. May be NULL to
 *            check driver options.
 * @param cg Pointer to a channel group, if a specific channel group is to
 *           be checked. Must be NULL to check device-wide options.
 *
 * @return A GArray * of enum sr_configkey values, or NULL on invalid
 *         arguments. The array must be freed by the caller using
 *         g_array_free().
 *
 * @since 0.4.0
 */
SR_API GArray *sr_dev_options(const struct sr_dev_driver *driver,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	GVariant *gvar;
	const uint32_t *opts;
	uint32_t opt;
	gsize num_opts, i;
	GArray *result;

	if (!driver || !driver->config_list)
		return NULL;

	if (sdi && sdi->driver != driver)
		return NULL;

	if (driver->config_list(SR_CONF_DEVICE_OPTIONS, &gvar, sdi, cg) != SR_OK)
		return NULL;

	opts = g_variant_get_fixed_array(gvar, &num_opts, sizeof(uint32_t));

	result = g_array_sized_new(FALSE, FALSE, sizeof(uint32_t), num_opts);

	for (i = 0; i < num_opts; i++) {
		opt = opts[i] & SR_CONF_MASK;
		g_array_insert_val(result, i, opt);
	}

	g_variant_unref(gvar);

	return result;
}

/**
 * Enumerate the configuration capabilities supported by a device instance
 * for a given configuration key.
 *
 * @param sdi Pointer to the device instance to be checked. Must not be NULL.
 *            If the device's 'driver' field is NULL (virtual device), this
 *            function will always return FALSE (virtual devices don't have
 *            a hardware capabilities list).
 * @param cg Pointer to a channel group, if a specific channel group is to
 *           be checked. Must be NULL to check device-wide options.
 * @param[in] key The option that should be checked for is supported by the
 *            specified device.
 *
 * @retval A bitmask of enum sr_configcap values, which will be zero for
 *         invalid inputs or if the key is unsupported.
 *
 * @since 0.4.0
 */
SR_API int sr_dev_config_capabilities_list(const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg, const int key)
{
	GVariant *gvar;
	const int *devopts;
	gsize num_opts, i;
	int ret;

	if (!sdi || !sdi->driver || !sdi->driver->config_list)
		return 0;

	if (sdi->driver->config_list(SR_CONF_DEVICE_OPTIONS,
				&gvar, sdi, cg) != SR_OK)
		return 0;

	ret = 0;
	devopts = g_variant_get_fixed_array(gvar, &num_opts, sizeof(int32_t));
	for (i = 0; i < num_opts; i++) {
		if ((devopts[i] & SR_CONF_MASK) == key) {
			ret = devopts[i] & ~SR_CONF_MASK;
			break;
		}
	}
	g_variant_unref(gvar);

	return ret;
}

/**
 * Allocate and init a new user-generated device instance.
 *
 * @param vendor Device vendor.
 * @param model Device model.
 * @param version Device version.
 *
 * @retval struct sr_dev_inst *. Dynamically allocated, free using
 *         sr_dev_inst_free().
 */
SR_API struct sr_dev_inst *sr_dev_inst_user_new(const char *vendor,
		const char *model, const char *version)
{
	struct sr_dev_inst *sdi;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));

	sdi->vendor = g_strdup(vendor);
	sdi->model = g_strdup(model);
	sdi->version = g_strdup(version);
	sdi->inst_type = SR_INST_USER;

	return sdi;
}

/**
 * Add a new channel to the specified device instance.
 *
 * @param[in] index @copydoc sr_channel::index
 * @param[in] type @copydoc sr_channel::type
 * @param[in] name @copydoc sr_channel::name
 *
 * @return SR_OK Success.
 * @return SR_OK Invalid argument.
 */
SR_API int sr_dev_inst_channel_add(struct sr_dev_inst *sdi, int index, int type, const char *name)
{
	if (!sdi || sdi->inst_type != SR_INST_USER || index < 0)
		return SR_ERR_ARG;

	sr_channel_new(sdi, index, type, TRUE, name);

	return SR_OK;
}

/**
 * Free device instance struct created by sr_dev_inst().
 *
 * @param sdi Device instance to free. If NULL, the function will do nothing.
 *
 * @private
 */
SR_PRIV void sr_dev_inst_free(struct sr_dev_inst *sdi)
{
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	GSList *l;

	if (!sdi)
		return;

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

	if (sdi->session)
		sr_session_dev_remove(sdi->session, sdi);

	g_free(sdi->vendor);
	g_free(sdi->model);
	g_free(sdi->version);
	g_free(sdi->serial_num);
	g_free(sdi->connection_id);
	g_free(sdi);
}

#ifdef HAVE_LIBUSB_1_0

/**
 * Allocate and init a struct for a USB device instance.
 *
 * @param[in] bus @copydoc sr_usb_dev_inst::bus
 * @param[in] address @copydoc sr_usb_dev_inst::address
 * @param[in] hdl @copydoc sr_usb_dev_inst::devhdl
 *
 * @return The struct sr_usb_dev_inst * for USB device instance.
 *
 * @private
 */
SR_PRIV struct sr_usb_dev_inst *sr_usb_dev_inst_new(uint8_t bus,
			uint8_t address, struct libusb_device_handle *hdl)
{
	struct sr_usb_dev_inst *udi;

	udi = g_malloc0(sizeof(struct sr_usb_dev_inst));
	udi->bus = bus;
	udi->address = address;
	udi->devhdl = hdl;

	return udi;
}

/**
 * Free struct sr_usb_dev_inst * allocated by sr_usb_dev_inst().
 *
 * @param usb The struct sr_usb_dev_inst * to free. If NULL, this
 *            function does nothing.
 *
 * @private
 */
SR_PRIV void sr_usb_dev_inst_free(struct sr_usb_dev_inst *usb)
{
	g_free(usb);
}

#endif

#ifdef HAVE_LIBSERIALPORT

/**
 * Allocate and init a struct for a serial device instance.
 *
 * Both parameters are copied to newly allocated strings, and freed
 * automatically by sr_serial_dev_inst_free().
 *
 * @param[in] port OS-specific serial port specification. Examples:
 *                 "/dev/ttyUSB0", "/dev/ttyACM1", "/dev/tty.Modem-0", "COM1".
 *                 Must not be NULL.
 * @param[in] serialcomm A serial communication parameters string, in the form
 *              of \<speed\>/\<data bits\>\<parity\>\<stopbits\>, for example
 *              "9600/8n1" or "600/7o2". This is an optional parameter;
 *              it may be filled in later. Can be NULL.
 *
 * @return A pointer to a newly initialized struct sr_serial_dev_inst,
 *         or NULL on error.
 *
 * @private
 */
SR_PRIV struct sr_serial_dev_inst *sr_serial_dev_inst_new(const char *port,
		const char *serialcomm)
{
	struct sr_serial_dev_inst *serial;

	serial = g_malloc0(sizeof(struct sr_serial_dev_inst));
	serial->port = g_strdup(port);
	if (serialcomm)
		serial->serialcomm = g_strdup(serialcomm);

	return serial;
}

/**
 * Free struct sr_serial_dev_inst * allocated by sr_serial_dev_inst().
 *
 * @param serial The struct sr_serial_dev_inst * to free. If NULL, this
 *               function will do nothing.
 *
 * @private
 */
SR_PRIV void sr_serial_dev_inst_free(struct sr_serial_dev_inst *serial)
{
	if (!serial)
		return;

	g_free(serial->port);
	g_free(serial->serialcomm);
	g_free(serial);
}
#endif

/** @private */
SR_PRIV struct sr_usbtmc_dev_inst *sr_usbtmc_dev_inst_new(const char *device)
{
	struct sr_usbtmc_dev_inst *usbtmc;

	usbtmc = g_malloc0(sizeof(struct sr_usbtmc_dev_inst));
	usbtmc->device = g_strdup(device);
	usbtmc->fd = -1;

	return usbtmc;
}

/** @private */
SR_PRIV void sr_usbtmc_dev_inst_free(struct sr_usbtmc_dev_inst *usbtmc)
{
	if (!usbtmc)
		return;

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
		return driver->dev_list(driver);
	else
		return NULL;
}

/**
 * Clear the list of device instances a driver knows about.
 *
 * @param driver The driver to use. This must be a pointer to one of
 *               the entries returned by sr_driver_list(). Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid driver.
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
		ret = driver->dev_clear(driver);
	else
		ret = std_dev_clear(driver, NULL);

	return ret;
}

/**
 * Open the specified device instance.
 *
 * If the device instance is already open (sdi->status == SR_ST_ACTIVE),
 * SR_ERR will be returned and no re-opening of the device will be attempted.
 *
 * If opening was successful, sdi->status is set to SR_ST_ACTIVE, otherwise
 * it will be left unchanged.
 *
 * @param sdi Device instance to use. Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid arguments.
 * @retval SR_ERR Device instance was already active, or other error.
 *
 * @since 0.2.0
 */
SR_API int sr_dev_open(struct sr_dev_inst *sdi)
{
	int ret;

	if (!sdi || !sdi->driver || !sdi->driver->dev_open)
		return SR_ERR_ARG;

	if (sdi->status == SR_ST_ACTIVE) {
		sr_err("%s: Device instance already active, can't re-open.",
			sdi->driver->name);
		return SR_ERR;
	}

	sr_dbg("%s: Opening device instance.", sdi->driver->name);

	ret = sdi->driver->dev_open(sdi);

	if (ret == SR_OK)
		sdi->status = SR_ST_ACTIVE;

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

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("%s: Device instance not active, can't close.",
			sdi->driver->name);
		return SR_ERR_DEV_CLOSED;
	}

	sr_dbg("%s: Closing device.", sdi->driver->name)

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
SR_API struct sr_dev_driver *sr_dev_inst_driver_get(const struct sr_dev_inst *sdi)
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
SR_API const char *sr_dev_inst_vendor_get(const struct sr_dev_inst *sdi)
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
SR_API const char *sr_dev_inst_model_get(const struct sr_dev_inst *sdi)
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
SR_API const char *sr_dev_inst_version_get(const struct sr_dev_inst *sdi)
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
SR_API const char *sr_dev_inst_sernum_get(const struct sr_dev_inst *sdi)
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
 * @return A copy of the connection ID string or NULL. The caller is responsible
 *         for g_free()ing the string when it is no longer needed.
 */
SR_API const char *sr_dev_inst_connid_get(const struct sr_dev_inst *sdi)
{
#ifdef HAVE_LIBUSB_1_0
	struct drv_context *drvc;
	int cnt, i, a, b;
	char connection_id[64];
	struct sr_usb_dev_inst *usb;
	struct libusb_device **devlist;
#endif

	if (!sdi)
		return NULL;

#ifdef HAVE_LIBSERIALPORT
	struct sr_serial_dev_inst *serial;

	if ((!sdi->connection_id) && (sdi->inst_type == SR_INST_SERIAL)) {
		/* connection_id isn't populated, let's do that here. */

		serial = sdi->conn;
		((struct sr_dev_inst *)sdi)->connection_id = g_strdup(serial->port);
	}
#endif

#ifdef HAVE_LIBUSB_1_0
	if ((!sdi->connection_id) && (sdi->inst_type == SR_INST_USB)) {
		/* connection_id isn't populated, let's do that here. */

		drvc = sdi->driver->context;
		usb = sdi->conn;

		if ((cnt = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist)) < 0) {
			sr_err("Failed to retrieve device list: %s.",
			       libusb_error_name(cnt));
			return NULL;
		}

		for (i = 0; i < cnt; i++) {
			/* Find the USB device by the logical address we know. */
			b = libusb_get_bus_number(devlist[i]);
			a = libusb_get_device_address(devlist[i]);
			if (b != usb->bus || a != usb->address)
				continue;

			usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));
			((struct sr_dev_inst *)sdi)->connection_id = g_strdup(connection_id);
			break;
		}

		libusb_free_device_list(devlist, 1);
	}
#endif

	return sdi->connection_id;
}

/**
 * Queries a device instances' channel list.
 *
 * @param sdi Device instance to use. Must not be NULL.
 *
 * @return The GSList of channels or NULL.
 */
SR_API GSList *sr_dev_inst_channels_get(const struct sr_dev_inst *sdi)
{
	if (!sdi)
		return NULL;

	return sdi->channels;
}

/**
 * Queries a device instances' channel groups list.
 *
 * @param sdi Device instance to use. Must not be NULL.
 *
 * @return The GSList of channel groups or NULL.
 */
SR_API GSList *sr_dev_inst_channel_groups_get(const struct sr_dev_inst *sdi)
{
	if (!sdi)
		return NULL;

	return sdi->channel_groups;
}

/** @} */
