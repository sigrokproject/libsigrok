/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
 * With protocol information from the hantekdso project,
 * Copyright (C) 2008 Oleg Khudyakov <prcoder@gmail.com>
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
#include <string.h>
#include <glib.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

static int send_begin(const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	int ret;
	unsigned char buffer[] = {0x0f, 0x03, 0x03, 0x03, 0x68, 0xac, 0xfe,
	0x00, 0x01, 0x00};

	sr_dbg("Sending CTRL_BEGINCOMMAND.");

	usb = sdi->conn;
	if ((ret = libusb_control_transfer(usb->devhdl,
			LIBUSB_REQUEST_TYPE_VENDOR, CTRL_BEGINCOMMAND,
			0, 0, buffer, sizeof(buffer), 200)) != sizeof(buffer)) {
		sr_err("Failed to send begincommand: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int send_bulkcmd(const struct sr_dev_inst *sdi, uint8_t *cmdstring, int cmdlen)
{
	struct sr_usb_dev_inst *usb;
	int ret, tmp;

	usb = sdi->conn;

	if (send_begin(sdi) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_bulk_transfer(usb->devhdl, DSO_EP_OUT, cmdstring,
			cmdlen, &tmp, 200)) != 0)
		return SR_ERR;

	return SR_OK;
}

static int dso_getmps(libusb_device *dev)
{
	struct libusb_device_descriptor des;
	struct libusb_config_descriptor *conf_dsc;
	const struct libusb_interface_descriptor *intf_dsc;
	int mps;

	libusb_get_device_descriptor(dev, &des);

	if (des.bNumConfigurations != 1)
		return 0;

	if (libusb_get_config_descriptor(dev, 0, &conf_dsc) != 0)
		return 0;

	mps = 0;
	intf_dsc = &(conf_dsc->interface[0].altsetting[0]);
	if (intf_dsc->bNumEndpoints != 2)
		goto err;

	if ((intf_dsc->endpoint[0].bEndpointAddress & 0x8f) !=
	    (2 | LIBUSB_ENDPOINT_OUT))
		/* The first endpoint should be 2 (outbound). */
		goto err;

	if ((intf_dsc->endpoint[1].bEndpointAddress & 0x8f) !=
	    (6 | LIBUSB_ENDPOINT_IN))
		/* The second endpoint should be 6 (inbound). */
		goto err;

	mps = intf_dsc->endpoint[1].wMaxPacketSize;

err:
	if (conf_dsc)
		libusb_free_config_descriptor(conf_dsc);

	return mps;
}

SR_PRIV int dso_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct drv_context *drvc = sdi->driver->context;
	struct sr_usb_dev_inst *usb;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	int err, i;
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
			usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));
			if (strcmp(sdi->connection_id, connection_id))
				/* This is not the one. */
				continue;
		}

		if (!(err = libusb_open(devlist[i], &usb->devhdl))) {
			if (usb->address == 0xff)
				/*
				 * first time we touch this device after firmware upload,
				 * so we don't know the address yet.
				 */
				usb->address = libusb_get_device_address(devlist[i]);

			if (!(devc->epin_maxpacketsize = dso_getmps(devlist[i])))
				sr_err("Wrong endpoint profile.");
			else {
				sdi->status = SR_ST_ACTIVE;
				sr_info("Opened device on %d.%d (logical) / "
						"%s (physical) interface %d.",
					usb->bus, usb->address,
					sdi->connection_id, USB_INTERFACE);
			}
		} else {
			sr_err("Failed to open device: %s.",
			       libusb_error_name(err));
		}

		/* If we made it here, we handled the device (somehow). */
		break;
	}
	libusb_free_device_list(devlist, 1);

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV void dso_close(struct sr_dev_inst *sdi)
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

static int get_channel_offsets(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	GString *gs;
	int chan, v, ret;

	sr_dbg("Getting channel offsets.");

	devc = sdi->priv;
	usb = sdi->conn;

	ret = libusb_control_transfer(usb->devhdl,
			LIBUSB_ENDPOINT_IN | LIBUSB_REQUEST_TYPE_VENDOR,
			CTRL_READ_EEPROM, EEPROM_CHANNEL_OFFSETS, 0,
			(unsigned char *)&devc->channel_levels,
			sizeof(devc->channel_levels), 200);
	if (ret != sizeof(devc->channel_levels)) {
		sr_err("Failed to get channel offsets: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	/* Comes in as 16-bit numbers with the second byte always 0 on
	 * the DSO-2090. Guessing this is supposed to be big-endian,
	 * since that's how voltage offsets are submitted back to the DSO.
	 * Convert to host order now, so we can use them natively.
	 */
	for (chan = 0; chan < NUM_CHANNELS; chan++) {
		for (v = 0; v < 9; v++) {
			devc->channel_levels[chan][v][0] =
				g_ntohs(devc->channel_levels[chan][v][0]);
			devc->channel_levels[chan][v][1] =
				g_ntohs(devc->channel_levels[chan][v][1]);
		}
	}

	if (sr_log_loglevel_get() >= SR_LOG_DBG) {
		gs = g_string_sized_new(128);
		for (chan = 0; chan < NUM_CHANNELS; chan++) {
			g_string_printf(gs, "CH%d:", chan + 1);
			for (v = 0; v < 9; v++) {
				g_string_append_printf(gs, " %.4x-%.4x",
					devc->channel_levels[chan][v][0],
					devc->channel_levels[chan][v][1]);
			}
			sr_dbg("%s", gs->str);
		}
		g_string_free(gs, TRUE);
	}

	return SR_OK;
}

/* See http://openhantek.sourceforge.net/doc/namespaceHantek.html#ac1cd181814cf3da74771c29800b39028 */
static int dso2250_set_trigger_samplerate(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int ret, tmp;
	int base;
	uint8_t cmdstring[12];


	sr_dbg("Preparing CMD_SET_TRIGGER_SAMPLERATE.");

	devc = sdi->priv;
	usb = sdi->conn;

	memset(cmdstring, 0, sizeof(cmdstring));
	/* Command */
	cmdstring[0] = CMD_2250_SET_TRIGGERSOURCE;
	sr_dbg("Trigger source %s.", devc->triggersource);
	if (!strcmp("CH2", devc->triggersource))
		tmp = 3;
	else if (!strcmp("CH1", devc->triggersource))
		tmp = 2;
	else if (!strcmp("EXT", devc->triggersource))
		tmp = 0;
	else {
		sr_err("Invalid trigger source: '%s'.", devc->triggersource);
		return SR_ERR_ARG;
	}
	cmdstring[2] = tmp;


	sr_dbg("Trigger slope: %d.", devc->triggerslope);
	cmdstring[2] |= (devc->triggerslope == SLOPE_NEGATIVE ? 1 : 0) << 3;

	if (send_begin(sdi) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_bulk_transfer(usb->devhdl, DSO_EP_OUT,
			cmdstring, 8, &tmp, 100)) != 0) {
		sr_err("Failed to set trigger/samplerate: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}
	sr_dbg("Sent CMD_2250_SET_TRIGGERSOURCE.");


	/* Frame size */
	sr_dbg("Frame size: %d.", devc->framesize);
	cmdstring[0] = CMD_2250_SET_RECORD_LENGTH;
	cmdstring[2] = devc->framesize == FRAMESIZE_SMALL ? 0x01 : 0x02;

	if (send_begin(sdi) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_bulk_transfer(usb->devhdl, DSO_EP_OUT,
			cmdstring, 4, &tmp, 100)) != 0) {
		sr_err("Failed to set record length: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}
	sr_dbg("Sent CMD_2250_SET_RECORD_LENGTH.");


	memset(cmdstring, 0, sizeof(cmdstring));
	cmdstring[0] = CMD_2250_SET_SAMPLERATE;
	/* Timebase fast */
	sr_dbg("Time base index: %d.", devc->timebase);
	base = 100e6;
	if (devc->timebase < TIME_40us) {
		if (devc->framesize != FRAMESIZE_SMALL) {
			sr_err("Timebase < 40us only supported with 10K buffer.");
			return SR_ERR_ARG;
		}

		/* Fast mode on */
		base = 200e6;
		cmdstring[2] |= 1;
	}

	/* Downsampling on */
	cmdstring[2] |= 2;
	/* Downsampler = 1comp((Base / Samplerate) - 2)
	 *  Base == 100Msa resp. 200MSa
	 *
	 * Example for 500kSa/s:
	 *  100e6 / 500e3 => 200
	 *  200 - 2 => 198
	 *  1comp(198) => ff39 */

	tmp = base * timebase_to_time(devc->timebase);
	tmp = 200;
	if (tmp < 0)
		return SR_ERR_ARG;
	tmp -= 2;
	if (tmp < 0)
		return SR_ERR_ARG;
	tmp = ~tmp;
	sr_dbg("sample rate value: 0x%x.", tmp & 0xffff);
	cmdstring[4] = (tmp >> 0) & 0xff;
	cmdstring[5] = (tmp >> 8) & 0xff;

	if (send_begin(sdi) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_bulk_transfer(usb->devhdl, DSO_EP_OUT,
			cmdstring, 8, &tmp, 100)) != 0) {
		sr_err("Failed to set sample rate: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}
	sr_dbg("Sent CMD_2250_SET_SAMPLERATE.");


	/* Enabled channels: 00=CH1 01=CH2 10=both */
	memset(cmdstring, 0, sizeof(cmdstring));
	cmdstring[0] = CMD_2250_SET_CHANNELS;
	sr_dbg("Channels CH1=%d CH2=%d", devc->ch_enabled[0], devc->ch_enabled[1]);
	cmdstring[2] = (devc->ch_enabled[0] ? 0 : 1) + (devc->ch_enabled[1] ? 2 : 0);

	if (send_begin(sdi) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_bulk_transfer(usb->devhdl, DSO_EP_OUT,
			cmdstring, 4, &tmp, 100)) != 0) {
		sr_err("Failed to set channels: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}
	sr_dbg("Sent CMD_2250_SET_CHANNELS.");



	/* Trigger slope: 0=positive 1=negative */
	memset(cmdstring, 0, sizeof(cmdstring));
	cmdstring[0] = CMD_2250_SET_TRIGGERPOS_AND_BUFFER;

	/* Horizontal trigger position */
	/* TODO for big buffer */
	/* TODO */
	sr_dbg("Trigger position: %3.2f.", devc->triggerposition);
//	tmp = 0x77fff + 0x8000 * devc->triggerposition;
//	cmdstring[6] = tmp & 0xff;
//	cmdstring[7] = (tmp >> 8) & 0xff;
//	cmdstring[10] = (tmp >> 16) & 0xff;

	cmdstring[2]=0xff;
	cmdstring[3]=0xff;
	cmdstring[4]=0x07;

	cmdstring[6]=0xff;
	cmdstring[7]=0xd7;
	cmdstring[8]=0x07;

	if (send_begin(sdi) != SR_OK)
		return SR_ERR;

	/* TODO: 12 bytes according to documentation? */
	if ((ret = libusb_bulk_transfer(usb->devhdl, DSO_EP_OUT,
			cmdstring, 10, &tmp, 100)) != 0) {
		sr_err("Failed to set trigger position: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}
	sr_dbg("Sent CMD_2250_SET_TRIGGERPOS_AND_BUFFER.");

	return SR_OK;
}

static int dso_set_trigger_samplerate(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int ret, tmp;
	uint8_t cmdstring[12];
	uint16_t timebase_small[] = { 0xffff, 0xfffc, 0xfff7, 0xffe8, 0xffce,
		0xff9c, 0xff07, 0xfe0d, 0xfc19, 0xf63d, 0xec79, 0xd8f1 };
	uint16_t timebase_large[] = { 0xffff, 0x0000, 0xfffc, 0xfff7, 0xffe8,
		0xffce, 0xff9d, 0xff07, 0xfe0d, 0xfc19, 0xf63d, 0xec79 };

	devc = sdi->priv;
	if (devc->profile->fw_pid == 0x2250)
		return dso2250_set_trigger_samplerate(sdi);

	sr_dbg("Preparing CMD_SET_TRIGGER_SAMPLERATE.");

	usb = sdi->conn;

	memset(cmdstring, 0, sizeof(cmdstring));
	/* Command */
	cmdstring[0] = CMD_SET_TRIGGER_SAMPLERATE;

	/* Trigger source */
	sr_dbg("Trigger source %s.", devc->triggersource);
	if (!strcmp("CH2", devc->triggersource))
		tmp = 0;
	else if (!strcmp("CH1", devc->triggersource))
		tmp = 1;
	else if (!strcmp("EXT", devc->triggersource))
		tmp = 2;
	else {
		sr_err("Invalid trigger source: '%s'.", devc->triggersource);
		return SR_ERR_ARG;
	}
	cmdstring[2] = tmp;

	/* Frame size */
	sr_dbg("Frame size: %d.", devc->framesize);
	cmdstring[2] |= (devc->framesize == FRAMESIZE_SMALL ? 0x01 : 0x02) << 2;

	/* Timebase fast */
	sr_dbg("Time base index: %d.", devc->timebase);
	if (devc->framesize == FRAMESIZE_SMALL) {
		if (devc->timebase < TIME_20us)
			tmp = 0;
		else if (devc->timebase == TIME_20us)
			tmp = 1;
		else if (devc->timebase == TIME_40us)
			tmp = 2;
		else if (devc->timebase == TIME_100us)
			tmp = 3;
		else if (devc->timebase >= TIME_200us)
			tmp = 4;
	} else {
		if (devc->timebase < TIME_40us) {
			sr_err("Timebase < 40us only supported with 10K buffer.");
			return SR_ERR_ARG;
		}
		else if (devc->timebase == TIME_40us)
			tmp = 0;
		else if (devc->timebase == TIME_100us)
			tmp = 2;
		else if (devc->timebase == TIME_200us)
			tmp = 3;
		else if (devc->timebase >= TIME_400us)
			tmp = 4;
	}
	cmdstring[2] |= (tmp & 0x07) << 5;

	/* Enabled channels: 00=CH1 01=CH2 10=both */
	sr_dbg("Channels CH1=%d CH2=%d", devc->ch_enabled[0], devc->ch_enabled[1]);
	tmp = (((devc->ch_enabled[1] ? 1 : 0) << 1) + (devc->ch_enabled[0] ? 1 : 0)) - 1;
	cmdstring[3] = tmp;

	/* Fast rates channel */
	/* TODO: Is this right? */
	tmp = devc->timebase < TIME_10us ? 1 : 0;
	cmdstring[3] |= tmp << 2;

	/* Trigger slope: 0=positive 1=negative */
	/* TODO: Does this work? */
	sr_dbg("Trigger slope: %d.", devc->triggerslope);
	cmdstring[3] |= (devc->triggerslope == SLOPE_NEGATIVE ? 1 : 0) << 3;

	/* Timebase slow */
	if (devc->timebase < TIME_100us)
		tmp = 0;
	else if (devc->timebase > TIME_400ms)
		tmp = 0xffed;
	else {
		if (devc->framesize == FRAMESIZE_SMALL)
			tmp = timebase_small[devc->timebase - 3];
		else
			tmp = timebase_large[devc->timebase - 3];
	}
	cmdstring[4] = tmp & 0xff;
	cmdstring[5] = (tmp >> 8) & 0xff;

	/* Horizontal trigger position */
	sr_dbg("Trigger position: %3.2f.", devc->triggerposition);
	tmp = 0x77fff + 0x8000 * devc->triggerposition;
	cmdstring[6] = tmp & 0xff;
	cmdstring[7] = (tmp >> 8) & 0xff;
	cmdstring[10] = (tmp >> 16) & 0xff;

	if (send_begin(sdi) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_bulk_transfer(usb->devhdl, DSO_EP_OUT,
			cmdstring, sizeof(cmdstring), &tmp, 100)) != 0) {
		sr_err("Failed to set trigger/samplerate: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}
	sr_dbg("Sent CMD_SET_TRIGGER_SAMPLERATE.");

	return SR_OK;
}


static int dso_set_filters(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int ret, tmp;
	uint8_t cmdstring[8];

	sr_dbg("Preparing CMD_SET_FILTERS.");

	devc = sdi->priv;
	usb = sdi->conn;

	memset(cmdstring, 0, sizeof(cmdstring));
	cmdstring[0] = CMD_SET_FILTERS;
	cmdstring[1] = 0x0f;
	if (devc->filter[0]) {
		sr_dbg("Turning on CH1 filter.");
		cmdstring[2] |= 0x80;
	}
	if (devc->filter[1]) {
		sr_dbg("Turning on CH2 filter.");
		cmdstring[2] |= 0x40;
	}
	/*
	 * Not supported: filtering on the trigger
	 * cmdstring[2] |= 0x20;
	 */

	if (send_begin(sdi) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_bulk_transfer(usb->devhdl, DSO_EP_OUT,
			cmdstring, sizeof(cmdstring), &tmp, 100)) != 0) {
		sr_err("Failed to set filters: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	sr_dbg("Sent CMD_SET_FILTERS.");

	return SR_OK;
}

static int dso2250_set_voltage(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int ret, tmp;
	uint8_t cmdstring[8];

	sr_dbg("Preparing CMD_SET_VOLTAGE.");

	devc = sdi->priv;
	usb = sdi->conn;

	memset(cmdstring, 0, sizeof(cmdstring));
	cmdstring[0] = CMD_SET_VOLTAGE;
	/* TODO */
	cmdstring[2] = 0x08;

	if (send_begin(sdi) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_bulk_transfer(usb->devhdl, DSO_EP_OUT,
			cmdstring, sizeof(cmdstring), &tmp, 100)) != 0) {
		sr_err("Failed to set voltage: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	sr_dbg("Sent CMD_SET_VOLTAGE.");


	/* CH1 volts/div is encoded in bits 0-1 */
	sr_dbg("CH1 vdiv index: %d.", devc->voltage[0]);
	switch (devc->voltage[0]) {
	case VDIV_1V:
	case VDIV_100MV:
	case VDIV_10MV:
		cmdstring[2] |= 0x00;
		break;
	case VDIV_2V:
	case VDIV_200MV:
	case VDIV_20MV:
		cmdstring[2] |= 0x01;
		break;
	case VDIV_5V:
	case VDIV_500MV:
	case VDIV_50MV:
		cmdstring[2] |= 0x02;
		break;
	}

	/* CH2 volts/div is encoded in bits 2-3 */
	sr_dbg("CH2 vdiv index: %d.", devc->voltage[1]);
	switch (devc->voltage[1]) {
	case VDIV_1V:
	case VDIV_100MV:
	case VDIV_10MV:
		cmdstring[2] |= 0x00;
		break;
	case VDIV_2V:
	case VDIV_200MV:
	case VDIV_20MV:
		cmdstring[2] |= 0x04;
		break;
	case VDIV_5V:
	case VDIV_500MV:
	case VDIV_50MV:
		cmdstring[2] |= 0x08;
		break;
	}

		return SR_OK;
}

static int dso_set_voltage(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int ret, tmp;
	uint8_t cmdstring[8];

	devc = sdi->priv;
	if (devc->profile->fw_pid == 0x2250)
		return dso2250_set_voltage(sdi);

	sr_dbg("Preparing CMD_SET_VOLTAGE.");

	usb = sdi->conn;

	memset(cmdstring, 0, sizeof(cmdstring));
	cmdstring[0] = CMD_SET_VOLTAGE;
	cmdstring[1] = 0x0f;
	cmdstring[2] = 0x30;

	/* CH1 volts/div is encoded in bits 0-1 */
	sr_dbg("CH1 vdiv index: %d.", devc->voltage[0]);
	switch (devc->voltage[0]) {
	case VDIV_1V:
	case VDIV_100MV:
	case VDIV_10MV:
		cmdstring[2] |= 0x00;
		break;
	case VDIV_2V:
	case VDIV_200MV:
	case VDIV_20MV:
		cmdstring[2] |= 0x01;
		break;
	case VDIV_5V:
	case VDIV_500MV:
	case VDIV_50MV:
		cmdstring[2] |= 0x02;
		break;
	}

	/* CH2 volts/div is encoded in bits 2-3 */
	sr_dbg("CH2 vdiv index: %d.", devc->voltage[1]);
	switch (devc->voltage[1]) {
	case VDIV_1V:
	case VDIV_100MV:
	case VDIV_10MV:
		cmdstring[2] |= 0x00;
		break;
	case VDIV_2V:
	case VDIV_200MV:
	case VDIV_20MV:
		cmdstring[2] |= 0x04;
		break;
	case VDIV_5V:
	case VDIV_500MV:
	case VDIV_50MV:
		cmdstring[2] |= 0x08;
		break;
	}

	if (send_begin(sdi) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_bulk_transfer(usb->devhdl, DSO_EP_OUT,
			cmdstring, sizeof(cmdstring), &tmp, 100)) != 0) {
		sr_err("Failed to set voltage: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	sr_dbg("Sent CMD_SET_VOLTAGE.");

	return SR_OK;
}

static int dso_set_relays(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	GString *gs;
	int ret, i;
	uint8_t relays[17] = { 0x00, 0x04, 0x08, 0x02, 0x20, 0x40, 0x10, 0x01,
			0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

	sr_dbg("Preparing CTRL_SETRELAYS.");

	devc = sdi->priv;
	usb = sdi->conn;

	if (devc->voltage[0] < VDIV_1V)
		relays[1] = ~relays[1];

	if (devc->voltage[0] < VDIV_100MV)
		relays[2] = ~relays[2];

	sr_dbg("CH1 coupling: %d.", devc->coupling[0]);
	if (devc->coupling[0] != COUPLING_AC)
		relays[3] = ~relays[3];

	if (devc->voltage[1] < VDIV_1V)
		relays[4] = ~relays[4];

	if (devc->voltage[1] < VDIV_100MV)
		relays[5] = ~relays[5];

	sr_dbg("CH2 coupling: %d.", devc->coupling[1]);
	if (devc->coupling[1] != COUPLING_AC)
		relays[6] = ~relays[6];

	if (!strcmp(devc->triggersource, "EXT"))
		relays[7] = ~relays[7];

	if (sr_log_loglevel_get() >= SR_LOG_DBG) {
		gs = g_string_sized_new(128);
		g_string_printf(gs, "Relays:");
		for (i = 0; i < 17; i++)
			g_string_append_printf(gs, " %.2x", relays[i]);
		sr_dbg("%s", gs->str);
		g_string_free(gs, TRUE);
	}

	if ((ret = libusb_control_transfer(usb->devhdl,
			LIBUSB_REQUEST_TYPE_VENDOR, CTRL_SETRELAYS,
			0, 0, relays, 17, 100)) != sizeof(relays)) {
		sr_err("Failed to set relays: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	sr_dbg("Sent CTRL_SETRELAYS.");

	return SR_OK;
}

static int dso_set_voffsets(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int offset, ret;
	uint16_t *ch_levels;
	uint8_t offsets[17];

	sr_dbg("Preparing CTRL_SETOFFSET.");

	devc = sdi->priv;
	usb = sdi->conn;

	memset(offsets, 0, sizeof(offsets));
	/* Channel 1 */
	ch_levels = devc->channel_levels[0][devc->voltage[0]];
	offset = (ch_levels[1] - ch_levels[0]) * devc->voffset_ch1 + ch_levels[0];
	offsets[0] = (offset >> 8) | 0x20;
	offsets[1] = offset & 0xff;
	sr_dbg("CH1 offset: %3.2f (%.2x%.2x).", devc->voffset_ch1,
	       offsets[0], offsets[1]);

	/* Channel 2 */
	ch_levels = devc->channel_levels[1][devc->voltage[1]];
	offset = (ch_levels[1] - ch_levels[0]) * devc->voffset_ch2 + ch_levels[0];
	offsets[2] = (offset >> 8) | 0x20;
	offsets[3] = offset & 0xff;
	sr_dbg("CH2 offset: %3.2f (%.2x%.2x).", devc->voffset_ch2,
	       offsets[2], offsets[3]);

	/* Trigger */
	offset = MAX_VERT_TRIGGER * devc->voffset_trigger;
	offsets[4] = (offset >> 8) | 0x20;
	offsets[5] = offset & 0xff;
	sr_dbg("Trigger offset: %3.2f (%.2x%.2x).", devc->voffset_trigger,
			offsets[4], offsets[5]);

	if ((ret = libusb_control_transfer(usb->devhdl,
			LIBUSB_REQUEST_TYPE_VENDOR, CTRL_SETOFFSET,
			0, 0, offsets, sizeof(offsets), 100)) != sizeof(offsets)) {
		sr_err("Failed to set offsets: %s.", libusb_error_name(ret));
		return SR_ERR;
	}
	sr_dbg("Sent CTRL_SETOFFSET.");

	return SR_OK;
}

SR_PRIV int dso_enable_trigger(const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	int ret, tmp;
	uint8_t cmdstring[2];

	sr_dbg("Sending CMD_ENABLE_TRIGGER.");

	usb = sdi->conn;

	memset(cmdstring, 0, sizeof(cmdstring));
	cmdstring[0] = CMD_ENABLE_TRIGGER;
	cmdstring[1] = 0x00;

	if (send_begin(sdi) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_bulk_transfer(usb->devhdl, DSO_EP_OUT,
			cmdstring, sizeof(cmdstring), &tmp, 100)) != 0) {
		sr_err("Failed to enable trigger: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int dso_force_trigger(const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	int ret, tmp;
	uint8_t cmdstring[2];

	sr_dbg("Sending CMD_FORCE_TRIGGER.");

	usb = sdi->conn;

	memset(cmdstring, 0, sizeof(cmdstring));
	cmdstring[0] = CMD_FORCE_TRIGGER;
	cmdstring[1] = 0x00;

	if (send_begin(sdi) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_bulk_transfer(usb->devhdl, DSO_EP_OUT,
			cmdstring, sizeof(cmdstring), &tmp, 100)) != 0) {
		sr_err("Failed to force trigger: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int dso_init(const struct sr_dev_inst *sdi)
{

	sr_dbg("Initializing DSO.");

	if (get_channel_offsets(sdi) != SR_OK)
		return SR_ERR;

	if (dso_set_trigger_samplerate(sdi) != SR_OK)
		return SR_ERR;

	if (dso_set_filters(sdi) != SR_OK)
		return SR_ERR;

	if (dso_set_voltage(sdi) != SR_OK)
		return SR_ERR;

	if (dso_set_relays(sdi) != SR_OK)
		return SR_ERR;

	if (dso_set_voffsets(sdi) != SR_OK)
		return SR_ERR;

	if (dso_enable_trigger(sdi) != SR_OK)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV int dso_get_capturestate(const struct sr_dev_inst *sdi,
		uint8_t *capturestate, uint32_t *trigger_offset)
{
	struct sr_usb_dev_inst *usb;
	int ret, tmp, i;
	unsigned int bitvalue, toff;
	uint8_t cmdstring[2], inbuf[512];

	sr_dbg("Sending CMD_GET_CAPTURESTATE.");

	usb = sdi->conn;

	cmdstring[0] = CMD_GET_CAPTURESTATE;
	cmdstring[1] = 0;

	if ((ret = send_bulkcmd(sdi, cmdstring, sizeof(cmdstring))) != SR_OK) {
		sr_dbg("Failed to send get_capturestate command: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	if ((ret = libusb_bulk_transfer(usb->devhdl, DSO_EP_IN,
			inbuf, 512, &tmp, 100)) != 0) {
		sr_dbg("Failed to get capturestate: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}
	*capturestate = inbuf[0];
	toff = (inbuf[1] << 16) | (inbuf[3] << 8) | inbuf[2];

	/*
	 * This conversion comes from the openhantek project.
	 * Each set bit in the 24-bit value inverts all bits with a lower
	 * value. No idea why the device reports the trigger point this way.
	 */
	bitvalue = 1;
	for (i = 0; i < 24; i++) {
		/* Each set bit inverts all bits with a lower value. */
		if (toff & bitvalue)
			toff ^= bitvalue - 1;
		bitvalue <<= 1;
	}
	*trigger_offset = toff;

	return SR_OK;
}

SR_PRIV int dso_capture_start(const struct sr_dev_inst *sdi)
{
	int ret;
	uint8_t cmdstring[2];

	sr_dbg("Sending CMD_CAPTURE_START.");

	cmdstring[0] = CMD_CAPTURE_START;
	cmdstring[1] = 0;

	if ((ret = send_bulkcmd(sdi, cmdstring, sizeof(cmdstring))) != SR_OK) {
		sr_err("Failed to send capture_start command: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int dso_get_channeldata(const struct sr_dev_inst *sdi,
		libusb_transfer_cb_fn cb)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *transfer;
	int num_transfers, ret, i;
	uint8_t cmdstring[2];
	unsigned char *buf;

	sr_dbg("Sending CMD_GET_CHANNELDATA.");

	devc = sdi->priv;
	usb = sdi->conn;

	cmdstring[0] = CMD_GET_CHANNELDATA;
	cmdstring[1] = 0;

	if ((ret = send_bulkcmd(sdi, cmdstring, sizeof(cmdstring))) != SR_OK) {
		sr_err("Failed to get channel data: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	/* TODO: DSO-2xxx only. */
	num_transfers = devc->framesize *
			sizeof(unsigned short) / devc->epin_maxpacketsize;
	sr_dbg("Queueing up %d transfers.", num_transfers);
	for (i = 0; i < num_transfers; i++) {
		if (!(buf = g_try_malloc(devc->epin_maxpacketsize))) {
			sr_err("Failed to malloc USB endpoint buffer.");
			return SR_ERR_MALLOC;
		}
		transfer = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(transfer, usb->devhdl, DSO_EP_IN, buf,
				devc->epin_maxpacketsize, cb, (void *)sdi, 40);
		if ((ret = libusb_submit_transfer(transfer)) != 0) {
			sr_err("Failed to submit transfer: %s.",
			       libusb_error_name(ret));
			/* TODO: Free them all. */
			libusb_free_transfer(transfer);
			g_free(buf);
			return SR_ERR;
		}
	}

	return SR_OK;
}
