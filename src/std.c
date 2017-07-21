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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/**
 * @file
 *
 * Standard API helper functions.
 *
 * @internal
 */

#include <config.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "std"

/**
 * Standard driver init() callback API helper.
 *
 * This function can be used to simplify most driver's init() API callback.
 *
 * Create a new 'struct drv_context' (drvc), assign sr_ctx to it, and
 * then assign 'drvc' to the 'struct sr_dev_driver' (di) that is passed.
 *
 * @param[in] di The driver instance to use. Must not be NULL.
 * @param[in] sr_ctx The libsigrok context to assign. May be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 */
SR_PRIV int std_init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	struct drv_context *drvc;

	if (!di) {
		sr_err("%s: Invalid argument.", __func__);
		return SR_ERR_ARG;
	}

	drvc = g_malloc0(sizeof(struct drv_context));
	drvc->sr_ctx = sr_ctx;
	drvc->instances = NULL;
	di->context = drvc;

	return SR_OK;
}

/**
 * Standard driver cleanup() callback API helper.
 *
 * This function can be used to simplify most driver's cleanup() API callback.
 *
 * Free all device instances by calling sr_dev_clear() and then release any
 * resources allocated by std_init().
 *
 * @param[in] di The driver instance to use. Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval other Other error.
 */
SR_PRIV int std_cleanup(const struct sr_dev_driver *di)
{
	int ret;

	if (!di) {
		sr_err("%s: Invalid argument.", __func__);
		return SR_ERR_ARG;
	}

	ret = sr_dev_clear(di);
	g_free(di->context);

	return ret;
}

/**
 * Dummmy driver dev_open() callback API helper.
 *
 * @param[in] sdi The device instance to use. May be NULL (unused).
 *
 * @retval SR_OK Success.
 */
SR_PRIV int std_dummy_dev_open(struct sr_dev_inst *sdi)
{
	(void)sdi;

	return SR_OK;
}

/**
 * Dummmy driver dev_close() callback API helper.
 *
 * @param[in] sdi The device instance to use. May be NULL (unused).
 *
 * @retval SR_OK Success.
 */
SR_PRIV int std_dummy_dev_close(struct sr_dev_inst *sdi)
{
	(void)sdi;

	return SR_OK;
}

/**
 * Dummmy driver dev_acquisition_start() callback API helper.
 *
 * @param[in] sdi The device instance to use. May be NULL (unused).
 *
 * @retval SR_OK Success.
 */
SR_PRIV int std_dummy_dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	(void)sdi;

	return SR_OK;
}

/**
 * Dummmy driver dev_acquisition_stop() callback API helper.
 *
 * @param[in] sdi The device instance to use. May be NULL (unused).
 *
 * @retval SR_OK Success.
 */
SR_PRIV int std_dummy_dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	(void)sdi;

	return SR_OK;
}

/**
 * Standard API helper for sending an SR_DF_HEADER packet.
 *
 * This function can be used to simplify most drivers'
 * dev_acquisition_start() API callback.
 *
 * @param[in] sdi The device instance to use. Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval other Other error.
 */
SR_PRIV int std_session_send_df_header(const struct sr_dev_inst *sdi)
{
	const char *prefix;
	int ret;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;

	if (!sdi) {
		sr_err("%s: Invalid argument.", __func__);
		return SR_ERR_ARG;
	}

	prefix = (sdi->driver) ? sdi->driver->name : "unknown";

	/* Send header packet to the session bus. */
	sr_dbg("%s: Sending SR_DF_HEADER packet.", prefix);
	packet.type = SR_DF_HEADER;
	packet.payload = (uint8_t *)&header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);

	if ((ret = sr_session_send(sdi, &packet)) < 0) {
		sr_err("%s: Failed to send SR_DF_HEADER packet: %d.", prefix, ret);
		return ret;
	}

	return SR_OK;
}

/**
 * Standard API helper for sending an SR_DF_END packet.
 *
 * This function can be used to simplify most drivers'
 * dev_acquisition_stop() API callback.
 *
 * @param[in] sdi The device instance to use. Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval other Other error.
 */
SR_PRIV int std_session_send_df_end(const struct sr_dev_inst *sdi)
{
	const char *prefix;
	int ret;
	struct sr_datafeed_packet packet;

	if (!sdi) {
		sr_err("%s: Invalid argument.", __func__);
		return SR_ERR_ARG;
	}

	prefix = (sdi->driver) ? sdi->driver->name : "unknown";

	sr_dbg("%s: Sending SR_DF_END packet.", prefix);

	packet.type = SR_DF_END;
	packet.payload = NULL;

	if ((ret = sr_session_send(sdi, &packet)) < 0) {
		sr_err("%s: Failed to send SR_DF_END packet: %d.", prefix, ret);
		return ret;
	}

	return SR_OK;
}

#ifdef HAVE_LIBSERIALPORT

/**
 * Standard serial driver dev_open() callback API helper.
 *
 * This function can be used to implement the dev_open() driver API
 * callback in drivers that use a serial port. The port is opened
 * with the SERIAL_RDWR flag.
 *
 * @param[in] sdi The device instance to use. Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval other Serial port open failed.
 */
SR_PRIV int std_serial_dev_open(struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;

	if (!sdi) {
		sr_err("%s: Invalid argument.", __func__);
		return SR_ERR_ARG;
	}

	serial = sdi->conn;

	return serial_open(serial, SERIAL_RDWR);
}

/**
 * Standard serial driver dev_close() callback API helper.
 *
 * This function can be used to implement the dev_close() driver API
 * callback in drivers that use a serial port.
 *
 * @param[in] sdi The device instance to use. Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval other Serial port close failed.
 */
SR_PRIV int std_serial_dev_close(struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;

	if (!sdi) {
		sr_err("%s: Invalid argument.", __func__);
		return SR_ERR_ARG;
	}

	serial = sdi->conn;

	return serial_close(serial);
}

/**
 * Standard serial driver dev_acquisition_stop() callback API helper.
 *
 * This function can be used to simplify most (serial port based) drivers'
 * dev_acquisition_stop() API callback.
 *
 * @param[in] sdi The device instance for which acquisition should stop.
 *                Must not be NULL.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval other Other error.
 */
SR_PRIV int std_serial_dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;
	const char *prefix;
	int ret;

	if (!sdi) {
		sr_err("%s: Invalid argument.", __func__);
		return SR_ERR_ARG;
	}

	serial = sdi->conn;
	prefix = sdi->driver->name;

	if ((ret = serial_source_remove(sdi->session, serial)) < 0) {
		sr_err("%s: Failed to remove source: %d.", prefix, ret);
		return ret;
	}

	if ((ret = sr_dev_close(sdi)) < 0) {
		sr_err("%s: Failed to close device: %d.", prefix, ret);
		return ret;
	}

	return std_session_send_df_end(sdi);
}

#endif

/**
 * Standard driver dev_clear() callback API helper.
 *
 * Clear driver, this means, close all instances.
 *
 * This function can be used to implement the dev_clear() driver API
 * callback. dev_close() is called before every sr_dev_inst is cleared.
 *
 * The only limitation is driver-specific device contexts (sdi->priv / devc).
 * These are freed, but any dynamic allocation within structs stored
 * there cannot be freed.
 *
 * @param[in] driver The driver which will have its instances released.
 *                   Must not be NULL.
 * @param[in] clear_private If not NULL, this points to a function called
 *            with sdi->priv (devc) as argument. The function can then clear
 *            any device instance-specific resources kept there.
 *            It must NOT clear the struct pointed to by sdi->priv (devc),
 *            since this function will always free it after clear_private()
 *            has run.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument.
 * @retval SR_ERR_BUG Implementation bug.
 * @retval other Other error.
 */
SR_PRIV int std_dev_clear_with_callback(const struct sr_dev_driver *driver,
		std_dev_clear_callback clear_private)
{
	struct drv_context *drvc;
	struct sr_dev_inst *sdi;
	GSList *l;
	int ret;

	if (!driver) {
		sr_err("%s: Invalid argument.", __func__);
		return SR_ERR_ARG;
	}

	drvc = driver->context; /* Caller checked for context != NULL. */

	ret = SR_OK;
	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data)) {
			sr_err("%s: Invalid device instance.", __func__);
			ret = SR_ERR_BUG;
			continue;
		}
		if (driver->dev_close)
			driver->dev_close(sdi);

		if (sdi->conn) {
#ifdef HAVE_LIBSERIALPORT
			if (sdi->inst_type == SR_INST_SERIAL)
				sr_serial_dev_inst_free(sdi->conn);
#endif
#ifdef HAVE_LIBUSB_1_0
			if (sdi->inst_type == SR_INST_USB)
				sr_usb_dev_inst_free(sdi->conn);
#endif
			if (sdi->inst_type == SR_INST_SCPI)
				sr_scpi_free(sdi->conn);
			if (sdi->inst_type == SR_INST_MODBUS)
				sr_modbus_free(sdi->conn);
		}

		/* Clear driver-specific stuff, if any. */
		if (clear_private)
			clear_private(sdi->priv);

		/* Clear sdi->priv (devc). */
		g_free(sdi->priv);

		sr_dev_inst_free(sdi);
	}

	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return ret;
}

SR_PRIV int std_dev_clear(const struct sr_dev_driver *driver)
{
	return std_dev_clear_with_callback(driver, NULL);
}

/**
 * Standard driver dev_list() callback API helper.
 *
 * This function can be used as the dev_list() callback by most drivers.
 *
 * Return the devices contained in the driver context instances list.
 *
 * @param[in] di The driver instance to use. Must not be NULL.
 *
 * @retval NULL Error, or the list is empty.
 * @retval other The list of device instances of this driver.
 */
SR_PRIV GSList *std_dev_list(const struct sr_dev_driver *di)
{
	struct drv_context *drvc;

	if (!di) {
		sr_err("%s: Invalid argument.", __func__);
		return NULL;
	}

	drvc = di->context;

	return drvc->instances;
}

/**
 * Standard driver scan() callback API helper.
 *
 * This function can be used to perform common tasks required by a driver's
 * scan() callback. It will initialize the driver for each device on the list
 * and add the devices on the list to the driver's device instance list.
 * Usually it should be used as the last step in the scan() callback, right
 * before returning.
 *
 * Note: This function can only be used if std_init() has been called
 * previously by the driver.
 *
 * Example:
 * @code{c}
 * static GSList *scan(struct sr_dev_driver *di, GSList *options)
 * {
 *     struct GSList *device;
 *     struct sr_dev_inst *sdi;
 *
 *     sdi = g_new0(sr_dev_inst, 1);
 *     sdi->vendor = ...;
 *     ...
 *     devices = g_slist_append(devices, sdi);
 *     ...
 *     return std_scan_complete(di, devices);
 * }
 * @endcode
 *
 * @param[in] di The driver instance to use. Must not be NULL.
 * @param[in] devices List of newly discovered devices (struct sr_dev_inst).
 *                    May be NULL.
 *
 * @return The @p devices list.
 */
SR_PRIV GSList *std_scan_complete(struct sr_dev_driver *di, GSList *devices)
{
	struct drv_context *drvc;
	GSList *l;

	if (!di) {
		sr_err("Invalid driver instance (di), cannot complete scan.");
		return NULL;
	}

	drvc = di->context;

	for (l = devices; l; l = l->next) {
		struct sr_dev_inst *sdi = l->data;
		if (!sdi) {
			sr_err("Invalid device instance, cannot complete scan.");
			return NULL;
		}
		sdi->driver = di;
	}

	drvc->instances = g_slist_concat(drvc->instances, g_slist_copy(devices));

	return devices;
}

SR_PRIV int std_opts_config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg,
	const uint32_t scanopts[], size_t scansize, const uint32_t drvopts[],
	size_t drvsize, const uint32_t devopts[], size_t devsize)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		/* Always return scanopts, regardless of sdi or cg. */
		if (!scanopts)
			return SR_ERR_ARG;
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
			scanopts, scansize, sizeof(uint32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		if (!sdi) {
			/* sdi == NULL: return drvopts. */
			if (!drvopts)
				return SR_ERR_ARG;
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				drvopts, drvsize, sizeof(uint32_t));
		} else if (sdi && !cg) {
			/* sdi != NULL, cg == NULL: return devopts. */
			if (!devopts)
				return SR_ERR_ARG;
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, devsize, sizeof(uint32_t));
		} else {
			/*
			 * Note: sdi != NULL, cg != NULL is not handled by
			 * this function since it's very driver-specific.
			 */
			sr_err("%s: %s: sdi/cg != NULL: not handling.",
			       sdi->driver->name, __func__);
			return SR_ERR_ARG;
		}
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

SR_PRIV GVariant *std_gvar_tuple_array(const uint64_t a[][2], unsigned int n)
{
	unsigned int i;
	GVariant *rational[2];
	GVariantBuilder gvb;

	g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);

	for (i = 0; i < n; i++) {
		rational[0] = g_variant_new_uint64(a[i][0]);
		rational[1] = g_variant_new_uint64(a[i][1]);

		/* FIXME: Valgrind reports a memory leak here. */
		g_variant_builder_add_value(&gvb, g_variant_new_tuple(rational, 2));
	}

	return g_variant_builder_end(&gvb);
}

SR_PRIV GVariant *std_gvar_tuple_rational(const struct sr_rational *r, unsigned int n)
{
	unsigned int i;
	GVariant *rational[2];
	GVariantBuilder gvb;

	g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);

	for (i = 0; i < n; i++) {
		rational[0] = g_variant_new_uint64(r[i].p);
		rational[1] = g_variant_new_uint64(r[i].q);

		/* FIXME: Valgrind reports a memory leak here. */
		g_variant_builder_add_value(&gvb, g_variant_new_tuple(rational, 2));
	}

	return g_variant_builder_end(&gvb);
}

static GVariant *samplerate_helper(const uint64_t samplerates[], unsigned int n, const char *str)
{
	GVariant *gvar;
	GVariantBuilder gvb;

	g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
	gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"), samplerates,
			n, sizeof(uint64_t));
	g_variant_builder_add(&gvb, "{sv}", str, gvar);

	return g_variant_builder_end(&gvb);
}

SR_PRIV GVariant *std_gvar_samplerates(const uint64_t samplerates[], unsigned int n)
{
	return samplerate_helper(samplerates, n, "samplerates");
}

SR_PRIV GVariant *std_gvar_samplerates_steps(const uint64_t samplerates[], unsigned int n)
{
	return samplerate_helper(samplerates, n, "samplerate-steps");
}

SR_PRIV GVariant *std_gvar_min_max_step(double min, double max, double step)
{
	GVariantBuilder gvb;

	g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);

	g_variant_builder_add_value(&gvb, g_variant_new_double(min));
	g_variant_builder_add_value(&gvb, g_variant_new_double(max));
	g_variant_builder_add_value(&gvb, g_variant_new_double(step));

	return g_variant_builder_end(&gvb);
}

SR_PRIV GVariant *std_gvar_min_max_step_array(const double a[3])
{
	unsigned int i;
	GVariantBuilder gvb;

	g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);

	for (i = 0; i < 3; i++)
		g_variant_builder_add_value(&gvb, g_variant_new_double(a[i]));

	return g_variant_builder_end(&gvb);
}

SR_PRIV GVariant *std_gvar_min_max_step_thresholds(const double min, const double max, const double step)
{
	double d;
	GVariant *gvar, *range[2];
	GVariantBuilder gvb;

	g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);

	for (d = min; d <= max; d += step) {
		range[0] = g_variant_new_double(d);
		range[1] = g_variant_new_double(d);

		gvar = g_variant_new_tuple(range, 2);
		g_variant_builder_add_value(&gvb, gvar);
	}

	return g_variant_builder_end(&gvb);
}

SR_PRIV GVariant *std_gvar_array_i32(const int32_t *a, unsigned int n)
{
	return g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				a, n, sizeof(int32_t));
}

SR_PRIV GVariant *std_gvar_array_u32(const uint32_t *a, unsigned int n)
{
	return g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				a, n, sizeof(uint32_t));
}

SR_PRIV GVariant *std_gvar_array_u64(const uint64_t *a, unsigned int n)
{
	return g_variant_new_fixed_array(G_VARIANT_TYPE_UINT64,
				a, n, sizeof(uint64_t));
}
