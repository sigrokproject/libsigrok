/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Christer Ekholm <christerekholm@gmail.com>
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
#include "protocol.h"

SR_PRIV int hantek_6xxx_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct drv_context *drvc = sdi->driver->context;
	struct sr_usb_dev_inst *usb;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	int err = SR_ERR, i;
	char connection_id[64];

	devc = sdi->priv;
	usb = sdi->conn;

	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != devc->profile->fw_vid
		    || des.idProduct != devc->profile->fw_pid)
			continue;

		if ((sdi->status == SR_ST_INITIALIZING) ||
				(sdi->status == SR_ST_INACTIVE)) {
			/*
			 * Check device by its physical USB bus/port address.
			 */
			if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
				continue;

			if (strcmp(sdi->connection_id, connection_id))
				/* This is not the one. */
				continue;
		}

		if (!(err = libusb_open(devlist[i], &usb->devhdl))) {
			if (usb->address == 0xff) {
				/*
				 * First time we touch this device after firmware upload,
				 * so we don't know the address yet.
				 */
				usb->address = libusb_get_device_address(devlist[i]);
			}

			sr_info("Opened device on %d.%d (logical) / "
					"%s (physical) interface %d.",
				usb->bus, usb->address,
				sdi->connection_id, USB_INTERFACE);

			err = SR_OK;
		} else {
			sr_err("Failed to open device: %s.",
			       libusb_error_name(err));
			err = SR_ERR;
		}

		/* If we made it here, we handled the device (somehow). */
		break;
	}

	libusb_free_device_list(devlist, 1);

	return err;
}

SR_PRIV void hantek_6xxx_close(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;

	usb = sdi->conn;

	if (!usb->devhdl)
		return;

	sr_info("Closing device on %d.%d (logical) / %s (physical) interface %d.",
		usb->bus, usb->address, sdi->connection_id, USB_INTERFACE);
	libusb_release_interface(usb->devhdl, USB_INTERFACE);
	libusb_close(usb->devhdl);
	usb->devhdl = NULL;
	sdi->status = SR_ST_INACTIVE;
}

SR_PRIV int hantek_6xxx_get_channeldata(const struct sr_dev_inst *sdi,
		libusb_transfer_cb_fn cb, uint32_t data_amount)
{
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *transfer;
	int ret;
	unsigned char *buf;

	sr_dbg("Request channel data.");

	usb = sdi->conn;

	if (!(buf = g_try_malloc(data_amount))) {
		sr_err("Failed to malloc USB endpoint buffer.");
		return SR_ERR_MALLOC;
	}
	transfer = libusb_alloc_transfer(0);
	libusb_fill_bulk_transfer(transfer, usb->devhdl, HANTEK_EP_IN, buf,
			data_amount, cb, (void *)sdi, 4000);
	if ((ret = libusb_submit_transfer(transfer)) < 0) {
		sr_err("Failed to submit transfer: %s.",
			libusb_error_name(ret));
		/* TODO: Free them all. */
		libusb_free_transfer(transfer);
		g_free(buf);
		return SR_ERR;
	}

	return SR_OK;
}

static uint8_t samplerate_to_reg(uint64_t samplerate)
{
	const uint64_t samplerate_values[] = {SAMPLERATE_VALUES};
	const uint8_t samplerate_regs[] = {SAMPLERATE_REGS};
	uint32_t i;

	for (i = 0; i < ARRAY_SIZE(samplerate_values); i++) {
		if (samplerate_values[i] == samplerate)
			return samplerate_regs[i];
	}

	sr_err("Failed to convert samplerate: %" PRIu64 ".", samplerate);

	return samplerate_regs[ARRAY_SIZE(samplerate_values) - 1];
}

static uint8_t voltage_to_reg(uint8_t state)
{
	const uint8_t vdiv_reg[] = {VDIV_REG};

	if (state < ARRAY_SIZE(vdiv_reg)) {
		return vdiv_reg[state];
	} else {
		sr_err("Failed to convert vdiv: %d.", state);
		return vdiv_reg[ARRAY_SIZE(vdiv_reg) - 1];
	}
}

static int write_control(const struct sr_dev_inst *sdi,
		enum control_requests reg, uint8_t value)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	int ret;

	sr_spew("hantek_6xxx_write_control: 0x%x 0x%x", reg, value);

	if ((ret = libusb_control_transfer(usb->devhdl,
			LIBUSB_REQUEST_TYPE_VENDOR, (uint8_t)reg,
			0, 0, &value, 1, 100)) <= 0) {
		sr_err("Failed to control transfer: 0x%x: %s.", reg,
			libusb_error_name(ret));
		return ret;
	}

	return 0;
}

SR_PRIV int hantek_6xxx_start_data_collecting(const struct sr_dev_inst *sdi)
{
	sr_dbg("trigger");
	return write_control(sdi, TRIGGER_REG, 1);
}

SR_PRIV int hantek_6xxx_stop_data_collecting(const struct sr_dev_inst *sdi)
{
	return write_control(sdi, TRIGGER_REG, 0);
}

SR_PRIV int hantek_6xxx_update_samplerate(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	sr_dbg("update samplerate %d", samplerate_to_reg(devc->samplerate));

	return write_control(sdi, SAMPLERATE_REG, samplerate_to_reg(devc->samplerate));
}

SR_PRIV int hantek_6xxx_update_vdiv(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	int ret1, ret2;

	sr_dbg("update vdiv %d %d", voltage_to_reg(devc->voltage[0]),
		voltage_to_reg(devc->voltage[1]));

	ret1 = write_control(sdi, VDIV_CH1_REG, voltage_to_reg(devc->voltage[0]));
	ret2 = write_control(sdi, VDIV_CH2_REG, voltage_to_reg(devc->voltage[1]));

	return MIN(ret1, ret2);
}

SR_PRIV int hantek_6xxx_update_coupling(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint8_t coupling = 0xFF & ((devc->coupling[1] << 4) | devc->coupling[0]);

	if (devc->has_coupling) {
		sr_dbg("update coupling 0x%x", coupling);
		return write_control(sdi, COUPLING_REG, coupling);
	} else {
		sr_dbg("coupling not supported");
		return SR_OK;
	}
}

SR_PRIV int hantek_6xxx_update_channels(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint8_t chan = devc->ch_enabled[1] ? 2 : 1;
	sr_dbg("update channels amount %d", chan);

	return write_control(sdi, CHANNELS_REG, chan);
}

SR_PRIV int hantek_6xxx_init(const struct sr_dev_inst *sdi)
{
	sr_dbg("Initializing");

	hantek_6xxx_update_samplerate(sdi);
	hantek_6xxx_update_vdiv(sdi);
	hantek_6xxx_update_coupling(sdi);
	// hantek_6xxx_update_channels(sdi); /* Only 2 channel mode supported. */

	return SR_OK;
}
