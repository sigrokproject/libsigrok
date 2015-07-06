/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
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

#include <glib.h>
#include <glib/gstdio.h>
#include "protocol.h"
#include "dslogic.h"

#pragma pack(push, 1)

struct version_info {
	uint8_t major;
	uint8_t minor;
};

struct cmd_start_acquisition {
	uint8_t flags;
	uint8_t sample_delay_h;
	uint8_t sample_delay_l;
};

#pragma pack(pop)

#define USB_TIMEOUT 100

static int command_get_fw_version(libusb_device_handle *devhdl,
				  struct version_info *vi)
{
	int ret;

	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_ENDPOINT_IN, CMD_GET_FW_VERSION, 0x0000, 0x0000,
		(unsigned char *)vi, sizeof(struct version_info), USB_TIMEOUT);

	if (ret < 0) {
		sr_err("Unable to get version info: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int command_get_revid_version(struct sr_dev_inst *sdi, uint8_t *revid)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	libusb_device_handle *devhdl = usb->devhdl;
	int cmd, ret;

	cmd = devc->dslogic ? DS_CMD_GET_REVID_VERSION : CMD_GET_REVID_VERSION;
	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_ENDPOINT_IN, cmd, 0x0000, 0x0000, revid, 1, USB_TIMEOUT);

	if (ret < 0) {
		sr_err("Unable to get REVID: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int fx2lafw_command_start_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	uint64_t samplerate;
	struct cmd_start_acquisition cmd;
	int delay, ret;

	devc = sdi->priv;
	usb = sdi->conn;
	samplerate = devc->cur_samplerate;

	/* Compute the sample rate. */
	if (devc->sample_wide && samplerate > MAX_16BIT_SAMPLE_RATE) {
		sr_err("Unable to sample at %" PRIu64 "Hz "
		       "when collecting 16-bit samples.", samplerate);
		return SR_ERR;
	}

	delay = 0;
	cmd.flags = cmd.sample_delay_h = cmd.sample_delay_l = 0;
	if ((SR_MHZ(48) % samplerate) == 0) {
		cmd.flags = CMD_START_FLAGS_CLK_48MHZ;
		delay = SR_MHZ(48) / samplerate - 1;
		if (delay > MAX_SAMPLE_DELAY)
			delay = 0;
	}

	if (delay == 0 && (SR_MHZ(30) % samplerate) == 0) {
		cmd.flags = CMD_START_FLAGS_CLK_30MHZ;
		delay = SR_MHZ(30) / samplerate - 1;
	}

	sr_dbg("GPIF delay = %d, clocksource = %sMHz.", delay,
		(cmd.flags & CMD_START_FLAGS_CLK_48MHZ) ? "48" : "30");

	if (delay <= 0 || delay > MAX_SAMPLE_DELAY) {
		sr_err("Unable to sample at %" PRIu64 "Hz.", samplerate);
		return SR_ERR;
	}

	cmd.sample_delay_h = (delay >> 8) & 0xff;
	cmd.sample_delay_l = delay & 0xff;

	/* Select the sampling width. */
	cmd.flags |= devc->sample_wide ? CMD_START_FLAGS_SAMPLE_16BIT :
		CMD_START_FLAGS_SAMPLE_8BIT;

	/* Send the control message. */
	ret = libusb_control_transfer(usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_ENDPOINT_OUT, CMD_START, 0x0000, 0x0000,
			(unsigned char *)&cmd, sizeof(cmd), USB_TIMEOUT);
	if (ret < 0) {
		sr_err("Unable to send start command: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

/**
 * Check the USB configuration to determine if this is an fx2lafw device.
 *
 * @return TRUE if the device's configuration profile matches fx2lafw
 *         configuration, FALSE otherwise.
 */
SR_PRIV gboolean match_manuf_prod(libusb_device *dev, const char *manufacturer,
		const char *product)
{
	struct libusb_device_descriptor des;
	struct libusb_device_handle *hdl;
	gboolean ret;
	unsigned char strdesc[64];

	hdl = NULL;
	ret = FALSE;
	while (!ret) {
		/* Assume the FW has not been loaded, unless proven wrong. */
		if (libusb_get_device_descriptor(dev, &des) != 0)
			break;

		if (libusb_open(dev, &hdl) != 0)
			break;

		if (libusb_get_string_descriptor_ascii(hdl,
				des.iManufacturer, strdesc, sizeof(strdesc)) < 0)
			break;
		if (strcmp((const char *)strdesc, manufacturer))
			break;

		if (libusb_get_string_descriptor_ascii(hdl,
				des.iProduct, strdesc, sizeof(strdesc)) < 0)
			break;
		if (strcmp((const char *)strdesc, product))
			break;

		ret = TRUE;
	}
	if (hdl)
		libusb_close(hdl);

	return ret;
}

SR_PRIV int fx2lafw_dev_open(struct sr_dev_inst *sdi, struct sr_dev_driver *di)
{
	libusb_device **devlist;
	struct sr_usb_dev_inst *usb;
	struct libusb_device_descriptor des;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct version_info vi;
	int ret, i, device_count;
	uint8_t revid;
	char connection_id[64];

	drvc = di->context;
	devc = sdi->priv;
	usb = sdi->conn;

	if (sdi->status == SR_ST_ACTIVE)
		/* Device is already in use. */
		return SR_ERR;

	device_count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (device_count < 0) {
		sr_err("Failed to get device list: %s.",
		       libusb_error_name(device_count));
		return SR_ERR;
	}

	for (i = 0; i < device_count; i++) {
		if ((ret = libusb_get_device_descriptor(devlist[i], &des))) {
			sr_err("Failed to get device descriptor: %s.",
			       libusb_error_name(ret));
			continue;
		}

		if (des.idVendor != devc->profile->vid
		    || des.idProduct != devc->profile->pid)
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

		if (!(ret = libusb_open(devlist[i], &usb->devhdl))) {
			if (usb->address == 0xff)
				/*
				 * First time we touch this device after FW
				 * upload, so we don't know the address yet.
				 */
				usb->address = libusb_get_device_address(devlist[i]);
		} else {
			sr_err("Failed to open device: %s.",
			       libusb_error_name(ret));
			break;
		}

		if (libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER)) {
			if (libusb_kernel_driver_active(usb->devhdl, USB_INTERFACE) == 1) {
				if ((ret = libusb_detach_kernel_driver(usb->devhdl, USB_INTERFACE)) < 0) {
					sr_err("Failed to detach kernel driver: %s.",
						libusb_error_name(ret));
					return SR_ERR;
				}
			}
		}

		ret = command_get_fw_version(usb->devhdl, &vi);
		if (ret != SR_OK) {
			sr_err("Failed to get firmware version.");
			break;
		}

		ret = command_get_revid_version(sdi, &revid);
		if (ret != SR_OK) {
			sr_err("Failed to get REVID.");
			break;
		}

		/*
		 * Changes in major version mean incompatible/API changes, so
		 * bail out if we encounter an incompatible version.
		 * Different minor versions are OK, they should be compatible.
		 */
		if (vi.major != FX2LAFW_REQUIRED_VERSION_MAJOR) {
			sr_err("Expected firmware version %d.x, "
			       "got %d.%d.", FX2LAFW_REQUIRED_VERSION_MAJOR,
			       vi.major, vi.minor);
			break;
		}

		sdi->status = SR_ST_ACTIVE;
		sr_info("Opened device on %d.%d (logical) / %s (physical), "
			"interface %d, firmware %d.%d.",
			usb->bus, usb->address, connection_id,
			USB_INTERFACE, vi.major, vi.minor);

		sr_info("Detected REVID=%d, it's a Cypress CY7C68013%s.",
			revid, (revid != 1) ? " (FX2)" : "A (FX2LP)");

		break;
	}
	libusb_free_device_list(devlist, 1);

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	return SR_OK;
}

SR_PRIV struct dev_context *fx2lafw_dev_new(void)
{
	struct dev_context *devc;

	devc = g_malloc0(sizeof(struct dev_context));
	devc->profile = NULL;
	devc->fw_updated = 0;
	devc->cur_samplerate = 0;
	devc->limit_samples = 0;
	devc->capture_ratio = 0;
	devc->sample_wide = FALSE;
	devc->stl = NULL;

	return devc;
}

SR_PRIV void fx2lafw_abort_acquisition(struct dev_context *devc)
{
	int i;

	devc->acq_aborted = TRUE;

	for (i = devc->num_transfers - 1; i >= 0; i--) {
		if (devc->transfers[i])
			libusb_cancel_transfer(devc->transfers[i]);
	}
}

static void finish_acquisition(struct sr_dev_inst *sdi)
{
	struct sr_datafeed_packet packet;
	struct dev_context *devc;

	devc = sdi->priv;

	/* Terminate session. */
	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);

	/* Remove fds from polling. */
	usb_source_remove(sdi->session, devc->ctx);

	devc->num_transfers = 0;
	g_free(devc->transfers);

	if (devc->stl) {
		soft_trigger_logic_free(devc->stl);
		devc->stl = NULL;
	}
}

static void free_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	unsigned int i;

	sdi = transfer->user_data;
	devc = sdi->priv;

	g_free(transfer->buffer);
	transfer->buffer = NULL;
	libusb_free_transfer(transfer);

	for (i = 0; i < devc->num_transfers; i++) {
		if (devc->transfers[i] == transfer) {
			devc->transfers[i] = NULL;
			break;
		}
	}

	devc->submitted_transfers--;
	if (devc->submitted_transfers == 0)
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

SR_PRIV void LIBUSB_CALL fx2lafw_receive_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	gboolean packet_has_error = FALSE;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	unsigned int num_samples;
	int trigger_offset, cur_sample_count, unitsize;
	int pre_trigger_samples;

	sdi = transfer->user_data;
	devc = sdi->priv;

	/*
	 * If acquisition has already ended, just free any queued up
	 * transfer that come in.
	 */
	if (devc->acq_aborted) {
		free_transfer(transfer);
		return;
	}

	sr_dbg("receive_transfer(): status %s received %d bytes.",
		libusb_error_name(transfer->status), transfer->actual_length);

	/* Save incoming transfer before reusing the transfer struct. */
	unitsize = devc->sample_wide ? 2 : 1;
	cur_sample_count = transfer->actual_length / unitsize;

	switch (transfer->status) {
	case LIBUSB_TRANSFER_NO_DEVICE:
		fx2lafw_abort_acquisition(devc);
		free_transfer(transfer);
		return;
	case LIBUSB_TRANSFER_COMPLETED:
	case LIBUSB_TRANSFER_TIMED_OUT: /* We may have received some data though. */
		break;
	default:
		packet_has_error = TRUE;
		break;
	}

	if (transfer->actual_length == 0 || packet_has_error) {
		devc->empty_transfer_count++;
		if (devc->empty_transfer_count > MAX_EMPTY_TRANSFERS) {
			/*
			 * The FX2 gave up. End the acquisition, the frontend
			 * will work out that the samplecount is short.
			 */
			fx2lafw_abort_acquisition(devc);
			free_transfer(transfer);
		} else {
			resubmit_transfer(transfer);
		}
		return;
	} else {
		devc->empty_transfer_count = 0;
	}

	if (devc->trigger_fired) {
		if (!devc->limit_samples || devc->sent_samples < devc->limit_samples) {
			/* Send the incoming transfer to the session bus. */
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			if (devc->limit_samples && devc->sent_samples + cur_sample_count > devc->limit_samples)
				num_samples = devc->limit_samples - devc->sent_samples;
			else
				num_samples = cur_sample_count;
			logic.length = num_samples * unitsize;
			logic.unitsize = unitsize;
			logic.data = transfer->buffer;
			sr_session_send(devc->cb_data, &packet);
			devc->sent_samples += num_samples;
		}
	} else {
		trigger_offset = soft_trigger_logic_check(devc->stl,
			transfer->buffer, transfer->actual_length, &pre_trigger_samples);
		if (trigger_offset > -1) {
			devc->sent_samples += pre_trigger_samples;
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			num_samples = cur_sample_count - trigger_offset;
			if (devc->limit_samples &&
					num_samples > devc->limit_samples - devc->sent_samples)
				num_samples = devc->limit_samples - devc->sent_samples;
			logic.length = num_samples * unitsize;
			logic.unitsize = unitsize;
			logic.data = transfer->buffer + trigger_offset * unitsize;
			sr_session_send(devc->cb_data, &packet);
			devc->sent_samples += num_samples;

			devc->trigger_fired = TRUE;
		}
	}

	if (devc->limit_samples && devc->sent_samples >= devc->limit_samples) {
		fx2lafw_abort_acquisition(devc);
		free_transfer(transfer);
	} else
		resubmit_transfer(transfer);
}

static unsigned int to_bytes_per_ms(unsigned int samplerate)
{
	return samplerate / 1000;
}

SR_PRIV size_t fx2lafw_get_buffer_size(struct dev_context *devc)
{
	size_t s;

	/*
	 * The buffer should be large enough to hold 10ms of data and
	 * a multiple of 512.
	 */
	s = 10 * to_bytes_per_ms(devc->cur_samplerate);
	return (s + 511) & ~511;
}

SR_PRIV unsigned int fx2lafw_get_number_of_transfers(struct dev_context *devc)
{
	unsigned int n;

	/* Total buffer size should be able to hold about 500ms of data. */
	n = (500 * to_bytes_per_ms(devc->cur_samplerate) /
		fx2lafw_get_buffer_size(devc));

	if (n > NUM_SIMUL_TRANSFERS)
		return NUM_SIMUL_TRANSFERS;

	return n;
}

SR_PRIV unsigned int fx2lafw_get_timeout(struct dev_context *devc)
{
	size_t total_size;
	unsigned int timeout;

	total_size = fx2lafw_get_buffer_size(devc) *
			fx2lafw_get_number_of_transfers(devc);
	timeout = total_size / to_bytes_per_ms(devc->cur_samplerate);
	return timeout + timeout / 4; /* Leave a headroom of 25% percent. */
}
