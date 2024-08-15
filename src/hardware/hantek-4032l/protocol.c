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
	CMD_RESET = 0x00b3, /* Also arms the logic analyzer. */
	CMD_CONFIGURE = 0x2b1a,
	CMD_STATUS = 0x4b3a,
	CMD_GET = 0x6b5a
};

struct h4032l_status_packet {
	uint32_t magic;
	uint32_t values;
	uint32_t status;
	uint32_t usbxi_data;
	uint32_t fpga_version;
};

static void abort_acquisition(struct dev_context *devc)
{
	int i;

	devc->acq_aborted = TRUE;

	for (i = devc->num_transfers - 1; i >= 0; i--) {
		if (devc->transfers[i])
			libusb_cancel_transfer(devc->transfers[i]);
	}

	devc->status = H4032L_STATUS_IDLE;
}

static void finish_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct drv_context *drvc = sdi->driver->context;

	std_session_send_df_end(sdi);
	usb_source_remove(sdi->session, drvc->sr_ctx);

	devc->num_transfers = 0;
	g_free(devc->transfers);
}

static void free_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi = transfer->user_data;
	struct dev_context *devc = sdi->priv;
	unsigned int i;

	if ((transfer->buffer != (unsigned char *)&devc->cmd_pkt) &&
	    (transfer->buffer != devc->buf)) {
		g_free(transfer->buffer);
	}

	transfer->buffer = NULL;
	libusb_free_transfer(transfer);

	for (i = 0; i < devc->num_transfers; i++) {
		if (devc->transfers[i] == transfer) {
			devc->transfers[i] = NULL;
			break;
		}
	}

	if (--devc->submitted_transfers == 0)
		finish_acquisition(sdi);
}

static void resubmit_transfer(struct libusb_transfer *transfer)
{
	int ret;

	if ((ret = libusb_submit_transfer(transfer)) == LIBUSB_SUCCESS)
		return;

	sr_err("%s: %s", __func__, libusb_error_name(ret));
	free_transfer(transfer);
}

static void send_data(struct sr_dev_inst *sdi,
	uint32_t *data, size_t sample_count)
{
	struct dev_context *devc = sdi->priv;
	struct sr_datafeed_logic logic = {
		.length = sample_count * sizeof(uint32_t),
		.unitsize = sizeof(uint32_t),
		.data = data
	};
	const struct sr_datafeed_packet packet = {
		.type = SR_DF_LOGIC,
		.payload = &logic
	};
	size_t trigger_offset;

	if (devc->trigger_pos >= devc->sent_samples &&
		devc->trigger_pos < (devc->sent_samples + sample_count)) {
		/* Get trigger position. */
		trigger_offset = devc->trigger_pos - devc->sent_samples;
		logic.length = trigger_offset * sizeof(uint32_t);
		if (logic.length)
			sr_session_send(sdi, &packet);

		/* Send trigger position. */
		std_session_send_df_trigger(sdi);

		/* Send rest of data. */
		logic.length = (sample_count - trigger_offset) * sizeof(uint32_t);
		logic.data = data + trigger_offset;
		if (logic.length)
			sr_session_send(sdi, &packet);
	} else {
		sr_session_send(sdi, &packet);
	}

	devc->sent_samples += sample_count;
}

void LIBUSB_CALL h4032l_data_transfer_callback(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *const sdi = transfer->user_data;
	struct dev_context *const devc = sdi->priv;
	uint32_t max_samples = transfer->actual_length / sizeof(uint32_t);
	uint32_t *buf;
	uint32_t num_samples;

	/*
	 * If acquisition has already ended, just free any queued up
	 * transfer that come in.
	 */
	if (devc->acq_aborted) {
		free_transfer(transfer);
		return;
	}

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
		sr_dbg("%s error: %d.", __func__, transfer->status);

	/* Cancel pending transfers. */
	if (transfer->actual_length == 0) {
		resubmit_transfer(transfer);
		return;
	}

	buf = (uint32_t *)transfer->buffer;

	num_samples = MIN(devc->remaining_samples, max_samples);
	devc->remaining_samples -= num_samples;
	send_data(sdi, buf, num_samples);
	sr_dbg("Remaining: %d %08X %08X.", devc->remaining_samples,
		buf[0], buf[1]);

	/* Close data receiving. */
	if (devc->remaining_samples == 0) {
		if (buf[num_samples] != H4032L_END_PACKET_MAGIC)
			sr_err("Mismatch magic number of end poll.");

		abort_acquisition(devc);
		free_transfer(transfer);
	} else {
		if (((devc->submitted_transfers - 1) * H4032L_DATA_BUFFER_SIZE) <
		    (int32_t)(devc->remaining_samples * sizeof(uint32_t)))
			resubmit_transfer(transfer);
		else
			free_transfer(transfer);
	}
}

void LIBUSB_CALL h4032l_usb_callback(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *const sdi = transfer->user_data;
	struct dev_context *const devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	gboolean cmd = FALSE;
	uint32_t max_samples = transfer->actual_length / sizeof(uint32_t);
	uint32_t *buf;
	struct h4032l_status_packet *status;
	uint32_t num_samples;
	int ret;

	/*
	 * If acquisition has already ended, just free any queued up
	 * transfers that come in.
	 */
	if (devc->acq_aborted) {
		free_transfer(transfer);
		return;
	}

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED)
		sr_dbg("%s error: %d.", __func__, transfer->status);

	buf = (uint32_t *)transfer->buffer;

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
		if (status->magic != H4032L_STATUS_PACKET_MAGIC)
			devc->status = H4032L_STATUS_RESPONSE_STATUS;
		else if (status->status == 2)
			devc->status = H4032L_STATUS_RESPONSE_STATUS_CONTINUE;
		else
			devc->status = H4032L_STATUS_RESPONSE_STATUS_RETRY;
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
		/* Trigger has been captured. */
		std_session_send_df_header(sdi);
		break;
	case H4032L_STATUS_FIRST_TRANSFER:
		/* Drop packets until H4032L_START_PACKET_MAGIC. */
		if (buf[0] != H4032L_START_PACKET_MAGIC) {
			sr_dbg("Mismatch magic number of start poll.");
			break;
		}
		devc->status = H4032L_STATUS_TRANSFER;
		max_samples--;
		buf++;
		/* Fallthrough. */
	case H4032L_STATUS_TRANSFER:
		num_samples = MIN(devc->remaining_samples, max_samples);
		devc->remaining_samples -= num_samples;
		send_data(sdi, buf, num_samples);
		sr_dbg("Remaining: %d %08X %08X.", devc->remaining_samples,
		       buf[0], buf[1]);
		break;
	}

	/* Start data receiving. */
	if (devc->status == H4032L_STATUS_TRANSFER) {
		if ((ret = h4032l_start_data_transfers(sdi)) != SR_OK) {
			sr_err("Can not start data transfers: %d", ret);
			devc->status = H4032L_STATUS_IDLE;
		}
	} else if (devc->status != H4032L_STATUS_IDLE) {
		if (cmd) {
			/* Setup new USB cmd packet, reuse transfer object. */
			sr_dbg("New command: %d.", devc->status);
			libusb_fill_bulk_transfer(transfer, usb->devhdl,
				2 | LIBUSB_ENDPOINT_OUT,
				(unsigned char *)&devc->cmd_pkt,
				sizeof(struct h4032l_cmd_pkt),
				h4032l_usb_callback,
				(void *)sdi, H4032L_USB_TIMEOUT);
		} else {
			/* Setup new USB poll packet, reuse transfer object. */
			sr_dbg("Poll: %d.", devc->status);
			libusb_fill_bulk_transfer(transfer, usb->devhdl,
				6 | LIBUSB_ENDPOINT_IN,
				devc->buf, ARRAY_SIZE(devc->buf),
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
		free_transfer(transfer);
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

SR_PRIV int h4032l_start_data_transfers(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct libusb_transfer *transfer;
	uint8_t *buf;
	unsigned int num_transfers;
	unsigned int i;
	int ret;

	devc->submitted_transfers = 0;

	/*
	 * Set number of data transfers regarding to size of buffer.
	 * FPGA version 0 can't transfer multiple transfers at once.
	 */
	if ((num_transfers = MIN(devc->remaining_samples * sizeof(uint32_t) /
	    H4032L_DATA_BUFFER_SIZE, devc->fpga_version ?
	    H4032L_DATA_TRANSFER_MAX_NUM : 1)) == 0)
		num_transfers = 1;

	g_free(devc->transfers);
	devc->transfers = g_malloc(sizeof(*devc->transfers) * num_transfers);
	devc->num_transfers = num_transfers;

	for (i = 0; i < num_transfers; i++) {
		buf = g_malloc(H4032L_DATA_BUFFER_SIZE);
		transfer = libusb_alloc_transfer(0);

		libusb_fill_bulk_transfer(transfer, usb->devhdl,
			6 | LIBUSB_ENDPOINT_IN,
			buf, H4032L_DATA_BUFFER_SIZE,
			h4032l_data_transfer_callback,
			(void *)sdi, H4032L_USB_TIMEOUT);

		/* Send prepared usb packet. */
		if ((ret = libusb_submit_transfer(transfer)) != 0) {
			sr_err("Failed to submit transfer: %s.",
			       libusb_error_name(ret));
			libusb_free_transfer(transfer);
			g_free(buf);
			abort_acquisition(devc);
			return SR_ERR;
		}
		devc->transfers[i] = transfer;
		devc->submitted_transfers++;
	}

	return SR_OK;
}

SR_PRIV int h4032l_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct libusb_transfer *transfer;
	unsigned char buf[] = {0x0f, 0x03, 0x03, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	int ret;

	/* Send reset command to arm the logic analyzer. */
	if ((ret = libusb_control_transfer(usb->devhdl,
		LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT, CMD_RESET,
		0x00, 0x00, buf, ARRAY_SIZE(buf), H4032L_USB_TIMEOUT)) < 0) {
		sr_err("Failed to send vendor request %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	/* Wait for reset vendor request. */
	g_usleep(20 * 1000);

	/* Send configure command. */
	devc->cmd_pkt.cmd = CMD_CONFIGURE;
	devc->status = H4032L_STATUS_CMD_CONFIGURE;
	devc->remaining_samples = devc->cmd_pkt.sample_size;

	transfer = libusb_alloc_transfer(0);

	libusb_fill_bulk_transfer(transfer, usb->devhdl,
		2 | LIBUSB_ENDPOINT_OUT, (unsigned char *)&devc->cmd_pkt,
		sizeof(struct h4032l_cmd_pkt), h4032l_usb_callback,
		(void *)sdi, H4032L_USB_TIMEOUT);

	if ((ret = libusb_submit_transfer(transfer)) != 0) {
		sr_err("Failed to submit transfer: %s.",
		       libusb_error_name(ret));
		libusb_free_transfer(transfer);
		return SR_ERR;
	}

	devc->transfers = g_malloc0(sizeof(*devc->transfers));
	devc->submitted_transfers++;
	devc->num_transfers = 1;
	devc->transfers[0] = transfer;

	return SR_OK;
}

SR_PRIV int h4032l_stop(struct sr_dev_inst *sdi)
{
	abort_acquisition(sdi->priv);

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
			if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
				continue;

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

SR_PRIV int h4032l_get_fpga_version(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct h4032l_status_packet *status;
	int transferred;
	int i, ret;

	/* Set command to status. */
	devc->cmd_pkt.magic = H4032L_CMD_PKT_MAGIC;
	devc->cmd_pkt.cmd = CMD_STATUS;

	/* Send status request. */
	if ((ret = libusb_bulk_transfer(usb->devhdl,
		2 | LIBUSB_ENDPOINT_OUT, (unsigned char *)&devc->cmd_pkt,
		sizeof(struct h4032l_cmd_pkt), &transferred, H4032L_USB_TIMEOUT)) < 0) {
		sr_err("Unable to send FPGA version request: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	/* Attempt to get FGPA version. */
	for (i = 0; i < 10; i++) {
		if ((ret = libusb_bulk_transfer(usb->devhdl,
			6 | LIBUSB_ENDPOINT_IN, devc->buf,
			ARRAY_SIZE(devc->buf), &transferred, H4032L_USB_TIMEOUT)) < 0) {
			sr_err("Unable to receive FPGA version: %s.",
			       libusb_error_name(ret));
			return SR_ERR;
		}
		status = (struct h4032l_status_packet *)devc->buf;
		if (status->magic == H4032L_STATUS_PACKET_MAGIC) {
			sr_dbg("FPGA version: 0x%x.", status->fpga_version);
			devc->fpga_version = status->fpga_version;
			return SR_OK;
		}
	}

	sr_err("Unable to get FPGA version.");

	return SR_ERR;
}
