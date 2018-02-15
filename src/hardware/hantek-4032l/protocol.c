/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Andreas Zschunke <andreas.zschunke@gmx.net>
 * Copyright (C) 2017 Andrej Valek <andy@skyrain.eu>
 * Copyright (C) 2017 Uwe Hermann <uwe@hermann-uwe.de>
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

#define H4032L_USB_TIMEOUT 500

enum h4032l_cmd {
	CMD_CONFIGURE = 0x2b1a, /* Also arms the logic analyzer. */
	CMD_STATUS = 0x4b3a,
	CMD_GET = 0x6b5a
};

struct __attribute__((__packed__)) h4032l_status_packet {
	uint32_t magic;
	uint32_t values;
	uint32_t status;
};

SR_PRIV int h4032l_receive_data(int fd, int revents, void *cb_data)
{
	struct timeval tv;
	struct drv_context *drvc;

	(void)fd;
	(void)revents;

	drvc = (struct drv_context *)cb_data;

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	return TRUE;
}

void LIBUSB_CALL h4032l_usb_callback(struct libusb_transfer *transfer)
{
	const struct sr_dev_inst *sdi = transfer->user_data;
	struct dev_context *devc = sdi->priv;
	struct drv_context *drvc = sdi->driver->context;
	struct sr_usb_dev_inst *usb = sdi->conn;
	gboolean cmd = FALSE;
	uint32_t max_samples = 512 / sizeof(uint32_t);
	uint32_t *buffer;
	struct h4032l_status_packet *status;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint32_t number_samples;
	int ret;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		sr_err("%s error: %d.", __func__, transfer->status);
		return;
	}

	buffer = (uint32_t *)transfer->buffer;

	switch (devc->status) {
	case H4032L_STATUS_IDLE:
		sr_err("USB callback called in idle.");
		break;
	case H4032L_STATUS_CMD_CONFIGURE:
		/* Select status request as next. */
		cmd = TRUE;
		devc->cmd_pkt.cmd = CMD_STATUS;
		devc->status = H4032L_STATUS_CMD_STATUS;
		break;
	case H4032L_STATUS_CMD_STATUS:
		/* Select status request as next. */
		devc->status = H4032L_STATUS_RESPONSE_STATUS;
		break;
	case H4032L_STATUS_RESPONSE_STATUS:
		/*
		 * Check magic and if status is complete, then select
		 * First Transfer as next.
		 */
		status = (struct h4032l_status_packet *)transfer->buffer;
		if (status->magic != H4032L_STATUS_PACKET_MAGIC) {
			devc->status = H4032L_STATUS_CMD_STATUS;
			devc->cmd_pkt.cmd = CMD_STATUS;
			cmd = TRUE;
		} else if (status->status == 2) {
			devc->status = H4032L_STATUS_RESPONSE_STATUS_CONTINUE;
		} else {
			devc->status = H4032L_STATUS_RESPONSE_STATUS_RETRY;
		}
		break;
	case H4032L_STATUS_RESPONSE_STATUS_RETRY:
		devc->status = H4032L_STATUS_CMD_STATUS;
		devc->cmd_pkt.cmd = CMD_STATUS;
		cmd = TRUE;
		break;
	case H4032L_STATUS_RESPONSE_STATUS_CONTINUE:
		devc->status = H4032L_STATUS_CMD_GET;
		devc->cmd_pkt.cmd = CMD_GET;
		cmd = TRUE;
		break;
	case H4032L_STATUS_CMD_GET:
		devc->status = H4032L_STATUS_FIRST_TRANSFER;
		break;
	case H4032L_STATUS_FIRST_TRANSFER:
		if (buffer[0] != H4032L_START_PACKET_MAGIC) {
			sr_err("Mismatch magic number of start poll.");
			devc->status = H4032L_STATUS_IDLE;
			break;
		}
		devc->status = H4032L_STATUS_TRANSFER;
		max_samples--;
		buffer++;
		break;
	case H4032L_STATUS_TRANSFER:
		number_samples = (devc->remaining_samples < max_samples) ? devc->remaining_samples : max_samples;
		devc->remaining_samples -= number_samples;
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = number_samples * sizeof(uint32_t);
		logic.unitsize = sizeof(uint32_t);
		logic.data = buffer;
		sr_session_send(sdi, &packet);
		sr_dbg("Remaining: %d %08X %08X.", devc->remaining_samples,
			buffer[0], buffer[1]);
		if (devc->remaining_samples == 0) {
			std_session_send_df_end(sdi);
			usb_source_remove(sdi->session, drvc->sr_ctx);
			devc->status = H4032L_STATUS_IDLE;
			if (buffer[number_samples] != H4032L_END_PACKET_MAGIC)
				sr_err("Mismatch magic number of end poll.");
		}
		break;
	}

	if (devc->status != H4032L_STATUS_IDLE) {
		if (cmd) {
			/* Setup new USB cmd packet, reuse transfer object. */
			sr_dbg("New command: %d.", devc->status);
			libusb_fill_bulk_transfer(transfer, usb->devhdl,
				2 | LIBUSB_ENDPOINT_OUT,
				(unsigned char *)&devc->cmd_pkt,
				sizeof(struct h4032l_cmd_pkt),
				h4032l_usb_callback, (void *)sdi,
				H4032L_USB_TIMEOUT);
		} else {
			/* Setup new USB poll packet, reuse transfer object. */
			sr_dbg("Poll: %d.", devc->status);
			libusb_fill_bulk_transfer(transfer, usb->devhdl,
				6 | LIBUSB_ENDPOINT_IN,
				devc->buffer, ARRAY_SIZE(devc->buffer),
				h4032l_usb_callback,
				(void *)sdi, H4032L_USB_TIMEOUT);
		}
		/* Send prepared USB packet. */
		if ((ret = libusb_submit_transfer(transfer)) != 0) {
			sr_err("Failed to submit transfer: %s.",
			       libusb_error_name(ret));
			devc->status = H4032L_STATUS_IDLE;
		}
	} else {
		sr_dbg("Now idle.");
	}

	if (devc->status == H4032L_STATUS_IDLE)
		libusb_free_transfer(transfer);
}

uint16_t h4032l_voltage2pwm(double voltage)
{
	/*
	 * word PwmA - channel A Vref PWM value, pseudocode:
	 * -6V < ThresholdVoltage < +6V
	 * Vref = 1.8 - ThresholdVoltage
	 * if Vref > 10.0
	 * 	Vref = 10.0
	 * if Vref < -5.0
	 * 	Vref = -5.0
	 * pwm = ToInt((Vref + 5.0) / 15.0 * 4096.0)
	 * if pwm > 4095
	 * 	pwm = 4095
	 */
	voltage = 1.8 - voltage;
	if (voltage > 10.0)
		voltage = 10.0;
	else if (voltage < -5.0)
		voltage = -5.0;

	return (uint16_t) ((voltage + 5.0) * (4096.0 / 15.0));
}

SR_PRIV int h4032l_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct libusb_transfer *transfer;
	int ret;

	/* Send configure command to arm the logic analyzer. */
	devc->cmd_pkt.cmd = CMD_CONFIGURE;
	devc->status = H4032L_STATUS_CMD_CONFIGURE;
	devc->remaining_samples = devc->cmd_pkt.sample_size;

	transfer = libusb_alloc_transfer(0);

	libusb_fill_bulk_transfer(transfer, usb->devhdl,
		2 | LIBUSB_ENDPOINT_OUT, (unsigned char *)&devc->cmd_pkt,
		sizeof(struct h4032l_cmd_pkt), h4032l_usb_callback,
		(void *)sdi, H4032L_USB_TIMEOUT);

	if ((ret = libusb_submit_transfer(transfer)) != 0) {
		sr_err("Failed to submit transfer: %s.", libusb_error_name(ret));
		libusb_free_transfer(transfer);
		return SR_ERR;
	}

	std_session_send_df_header(sdi);

	return SR_OK;
}

SR_PRIV int h4032l_dev_open(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc = sdi->driver->context;
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	int ret = SR_ERR, i, device_count;
	char connection_id[64];

	device_count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (device_count < 0) {
		sr_err("Failed to get device list: %s.",
		       libusb_error_name(device_count));
		return SR_ERR;
	}

	for (i = 0; i < device_count; i++) {
		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != H4032L_USB_VENDOR ||
		    des.idProduct != H4032L_USB_PRODUCT)
			continue;

		if ((sdi->status == SR_ST_INITIALIZING) ||
		    (sdi->status == SR_ST_INACTIVE)) {
			/* Check device by its physical USB bus/port address. */
			usb_get_port_path(devlist[i], connection_id,
					  sizeof(connection_id));
			if (strcmp(sdi->connection_id, connection_id))
				/* This is not the one. */
				continue;
		}

		if (!(ret = libusb_open(devlist[i], &usb->devhdl))) {
			if (usb->address == 0xff)
				/*
				 * First time we touch this device after FW
				 * upload, so we don't know the address yet.
				 */
				usb->address =
				    libusb_get_device_address(devlist[i]);
		} else {
			sr_err("Failed to open device: %s.",
			       libusb_error_name(ret));
			ret = SR_ERR;
			break;
		}

		ret = SR_OK;
		break;
	}

	libusb_free_device_list(devlist, 1);
	return ret;
}
