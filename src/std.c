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

/** @file
  * Standard API helper functions.
  * @internal
  */

#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "std"

/**
 * Standard sr_driver_init() API helper.
 *
 * This function can be used to simplify most driver's init() API callback.
 *
 * It creates a new 'struct drv_context' (drvc), assigns sr_ctx to it, and
 * then 'drvc' is assigned to the 'struct sr_dev_driver' (di) that is passed.
 *
 * @param sr_ctx The libsigrok context to assign.
 * @param di The driver instance to use.
 * @param[in] prefix A driver-specific prefix string used for log messages.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR_MALLOC upon memory allocation errors.
 */
SR_PRIV int std_init(struct sr_context *sr_ctx, struct sr_dev_driver *di,
		     const char *prefix)
{
	struct drv_context *drvc;

	if (!di) {
		sr_err("%s: Invalid driver, cannot initialize.", prefix);
		return SR_ERR_ARG;
	}

	if (!(drvc = g_try_malloc(sizeof(struct drv_context)))) {
		sr_err("%s: Driver context malloc failed.", prefix);
		return SR_ERR_MALLOC;
	}

	drvc->sr_ctx = sr_ctx;
	drvc->instances = NULL;
	di->priv = drvc;

	return SR_OK;
}

/**
 * Standard API helper for sending an SR_DF_HEADER packet.
 *
 * This function can be used to simplify most driver's
 * dev_acquisition_start() API callback.
 *
 * @param sdi The device instance to use.
 * @param prefix A driver-specific prefix string used for log messages.
 * 		 Must not be NULL. An empty string is allowed.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments, or
 *         SR_ERR upon other errors.
 */
SR_PRIV int std_session_send_df_header(const struct sr_dev_inst *sdi,
				       const char *prefix)
{
	int ret;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;

	if (!prefix) {
		sr_err("Invalid prefix.");
		return SR_ERR_ARG;
	}

	sr_dbg("%s: Starting acquisition.", prefix);

	/* Send header packet to the session bus. */
	sr_dbg("%s: Sending SR_DF_HEADER packet.", prefix);
	packet.type = SR_DF_HEADER;
	packet.payload = (uint8_t *)&header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);

	if ((ret = sr_session_send(sdi, &packet)) < 0) {
		sr_err("%s: Failed to send header packet: %d.", prefix, ret);
		return ret;
	}

	return SR_OK;
}

#ifdef HAVE_LIBSERIALPORT

/**
 * Standard serial driver dev_open() helper.
 *
 * This function can be used to implement the dev_open() driver API
 * callback in drivers that use a serial port. The port is opened
 * with the SERIAL_RDWR flag.
 *
 * If the open succeeded, the status field of the given sdi is set
 * to SR_ST_ACTIVE.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Serial port open failed.
 */
SR_PRIV int std_serial_dev_open(struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;
	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

/**
 * Standard serial driver dev_close() helper.
 *
 * This function can be used to implement the dev_close() driver API
 * callback in drivers that use a serial port.
 *
 * After closing the port, the status field of the given sdi is set
 * to SR_ST_INACTIVE.
 *
 * @retval SR_OK Success.
 */
SR_PRIV int std_serial_dev_close(struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;
	if (serial && sdi->status == SR_ST_ACTIVE) {
		serial_close(serial);
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

/**
 * Standard sr_session_stop() API helper.
 *
 * This function can be used to simplify most (serial port based) driver's
 * dev_acquisition_stop() API callback.
 *
 * @param sdi The device instance for which acquisition should stop.
 *            Must not be NULL.
 * @param cb_data Opaque 'cb_data' pointer. Must not be NULL.
 * @param dev_close_fn Function pointer to the driver's dev_close().
 *               	  Must not be NULL.
 * @param serial The serial device instance (struct serial_dev_inst *).
 *               Must not be NULL.
 * @param[in] prefix A driver-specific prefix string used for log messages.
 *               Must not be NULL. An empty string is allowed.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid arguments.
 * @retval SR_ERR_DEV_CLOSED Device is closed.
 * @retval SR_ERR Other errors.
 */
SR_PRIV int std_serial_dev_acquisition_stop(struct sr_dev_inst *sdi,
			void *cb_data, dev_close_callback dev_close_fn,
			struct sr_serial_dev_inst *serial, const char *prefix)
{
	int ret;
	struct sr_datafeed_packet packet;

	if (!prefix) {
		sr_err("Invalid prefix.");
		return SR_ERR_ARG;
	}

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("%s: Device inactive, can't stop acquisition.", prefix);
		return SR_ERR_DEV_CLOSED;
	}

	sr_dbg("%s: Stopping acquisition.", prefix);

	if ((ret = serial_source_remove(sdi->session, serial)) < 0) {
		sr_err("%s: Failed to remove source: %d.", prefix, ret);
		return ret;
	}

	if ((ret = dev_close_fn(sdi)) < 0) {
		sr_err("%s: Failed to close device: %d.", prefix, ret);
		return ret;
	}

	/* Send SR_DF_END packet to the session bus. */
	sr_dbg("%s: Sending SR_DF_END packet.", prefix);
	packet.type = SR_DF_END;
	packet.payload = NULL;
	if ((ret = sr_session_send(cb_data, &packet)) < 0) {
		sr_err("%s: Failed to send SR_DF_END packet: %d.", prefix, ret);
		return ret;
	}

	return SR_OK;
}

#endif

/**
 * Standard driver dev_clear() helper.
 *
 * Clear driver, this means, close all instances.
 *
 * This function can be used to implement the dev_clear() driver API
 * callback. dev_close() is called before every sr_dev_inst is cleared.
 *
 * The only limitation is driver-specific device contexts (sdi->priv).
 * These are freed, but any dynamic allocation within structs stored
 * there cannot be freed.
 *
 * @param driver The driver which will have its instances released.
 * @param clear_private If not NULL, this points to a function called
 * with sdi->priv as argument. The function can then clear any device
 * instance-specific resources kept there. It must also clear the struct
 * pointed to by sdi->priv.
 *
 * @return SR_OK on success.
 */
SR_PRIV int std_dev_clear(const struct sr_dev_driver *driver,
		std_dev_clear_callback clear_private)
{
	struct drv_context *drvc;
	struct sr_dev_inst *sdi;
	struct sr_channel_group *cg;
	GSList *l, *lcg;
	int ret;

	if (!(drvc = driver->priv))
		/* Driver was never initialized, nothing to do. */
		return SR_OK;

	ret = SR_OK;
	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data)) {
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
		}
		if (clear_private)
			/* The helper function is responsible for freeing
			 * its own sdi->priv! */
			clear_private(sdi->priv);
		else
			g_free(sdi->priv);

		/* Channel groups */
		for (lcg = sdi->channel_groups; lcg; lcg = lcg->next) {
			cg = lcg->data;
			g_free(cg->name);
			g_slist_free(cg->channels);
			g_free(cg);
		}
		sr_dev_inst_free(sdi);
	}

	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return ret;
}
