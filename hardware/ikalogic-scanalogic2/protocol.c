/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Marc Schink <sigrok-dev@marcschink.de>
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

#include "protocol.h"

extern struct sr_dev_driver ikalogic_scanalogic2_driver_info;
static struct sr_dev_driver *di = &ikalogic_scanalogic2_driver_info;

extern uint64_t sl2_samplerates[NUM_SAMPLERATES];

static void stop_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	unsigned int i;

	devc = sdi->priv;

	/* Remove USB file descriptors from polling. */
	for (i = 0; i < devc->num_usbfd; i++)
		sr_source_remove(devc->usbfd[i]);

	g_free(devc->usbfd);

	packet.type = SR_DF_END;
	sr_session_send(devc->cb_data, &packet);

	sdi->status = SR_ST_ACTIVE;
}

static void abort_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	unsigned int i;

	devc = sdi->priv;

	/* Remove USB file descriptors from polling. */
	for (i = 0; i < devc->num_usbfd; i++)
		sr_source_remove(devc->usbfd[i]);

	g_free(devc->usbfd);

	packet.type = SR_DF_END;
	sr_session_send(devc->cb_data, &packet);

	sdi->driver->dev_close(sdi);
}

static void buffer_sample_data(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	unsigned int offset, packet_length;

	devc = sdi->priv;

	if (devc->probes[devc->channel]->enabled) {
		offset = devc->sample_packet * PACKET_NUM_SAMPLE_BYTES;

		/*
		 * Determine the packet length to ensure that the last packet
		 * will not exceed the buffer size.
		 */
		packet_length = MIN(PACKET_NUM_SAMPLE_BYTES,
			MAX_DEV_SAMPLE_BYTES - offset);

		/*
		 * Skip the first 4 bytes of the source buffer because they
		 * contain channel and packet information only.
		 */
		memcpy(devc->sample_buffer[devc->channel] + offset,
			devc->xfer_data_in + 4, packet_length);
	}
}

static void process_sample_data(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint8_t i, j, tmp, buffer[PACKET_NUM_SAMPLES], *ptr[NUM_PROBES];
	uint16_t offset, n = 0;
	int8_t k;

	devc = sdi->priv;
	offset = devc->sample_packet * PACKET_NUM_SAMPLE_BYTES;

	/*
	 * Array of pointers to the sample data of all channels up to the last
	 * enabled one for an uniform access to them. Note that the currently
	 * received samples always belong to the last enabled channel.
	 */
	for (i = 0; i < devc->num_enabled_probes - 1; i++)
		ptr[i] = devc->sample_buffer[devc->probe_map[i]] + offset;

	/*
	 * Skip the first 4 bytes of the buffer because they contain channel
	 * and packet information only.
	 */
	ptr[i] = devc->xfer_data_in + 4;

	for (i = 0; i < PACKET_NUM_SAMPLE_BYTES; i++) {
		/* Stop processing if all requested samples are processed. */
		if (devc->samples_processed == devc->limit_samples)
			break;

		k = 7;

		if (devc->samples_processed == 0) {
			/*
			 * Adjust the position of the first sample to be
			 * processed because possibly more samples than
			 * necessary might have been acquired. This is because
			 * the number of acquired samples is always rounded up
			 * to a multiple of 8.
			 */
			k = k - (devc->pre_trigger_bytes * 8) +
				devc->pre_trigger_samples;

			sr_dbg("Start processing at sample: %d.", 7 - k);

			/*
			 * Send the trigger before the first sample is
			 * processed if no pre trigger samples were calculated
			 * through the capture ratio.
			 */
			if (devc->trigger_type != TRIGGER_TYPE_NONE &&
					devc->pre_trigger_samples == 0) {
				packet.type = SR_DF_TRIGGER;
				sr_session_send(devc->cb_data, &packet);
			}
		}

		for (; k >= 0; k--) {
			/*
			 * Stop processing if all requested samples are
			 * processed.
			 */
			if (devc->samples_processed == devc->limit_samples)
				break;

			buffer[n] = 0;

			/*
			 * Extract the current sample for each enabled channel
			 * and store them in the buffer.
			 */
			for (j = 0; j < devc->num_enabled_probes; j++) {
				tmp = (ptr[j][i] & (1 << k)) >> k;
				buffer[n] |= tmp << devc->probe_map[j];
			}

			n++;
			devc->samples_processed++;

			/*
			 * Send all processed samples and the trigger if the
			 * number of processed samples reaches the calculated
			 * number of pre trigger samples.
			 */
			if (devc->samples_processed == devc->pre_trigger_samples &&
					devc->trigger_type != TRIGGER_TYPE_NONE) {
				packet.type = SR_DF_LOGIC;
				packet.payload = &logic;
				logic.length = n;
				logic.unitsize = 1;
				logic.data = buffer;
				sr_session_send(devc->cb_data, &packet);

				packet.type = SR_DF_TRIGGER;
				sr_session_send(devc->cb_data, &packet);

				n = 0;
			}
		}
	}

	if (n > 0) {
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = n;
		logic.unitsize = 1;
		logic.data = buffer;
		sr_session_send(devc->cb_data, &packet);
	}
}

SR_PRIV int ikalogic_scanalogic2_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct timeval tv;
	int64_t current_time, time_elapsed;
	int ret = 0;

	(void)fd;
	(void)revents;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	drvc = di->priv;
	current_time = g_get_monotonic_time();

	if (devc->state == STATE_WAIT_DATA_READY &&
			!devc->wait_data_ready_locked) {
		time_elapsed = current_time - devc->wait_data_ready_time;

		/*
		 * Check here for stopping in addition to the transfer
		 * callback functions to avoid waiting until the
		 * WAIT_DATA_READY_INTERVAL has expired.
		 */
		if (sdi->status == SR_ST_STOPPING) {
			if (!devc->stopping_in_progress) {
				devc->next_state = STATE_RESET_AND_IDLE;
				devc->stopping_in_progress = TRUE;
				ret = libusb_submit_transfer(devc->xfer_in);
			}
		} else if (time_elapsed >= WAIT_DATA_READY_INTERVAL) {
			devc->wait_data_ready_locked = TRUE;
			ret = libusb_submit_transfer(devc->xfer_in);
		}
	}

	if (ret != 0) {
		sr_err("Submit transfer failed: %s.", libusb_error_name(ret));
		abort_acquisition(sdi);
		return TRUE;
	}

	tv.tv_sec = 0;
	tv.tv_usec = 0;

	libusb_handle_events_timeout_completed(drvc->sr_ctx->libusb_ctx, &tv,
		NULL);

	/* Check if an error occurred on a transfer. */
	if (devc->transfer_error)
		abort_acquisition(sdi);

	return TRUE;
}

SR_PRIV void sl2_receive_transfer_in( struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	uint8_t last_channel;
	int ret = 0;

	sdi = transfer->user_data;
	devc = sdi->priv;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		sr_err("Transfer to device failed: %i.", transfer->status);
		devc->transfer_error = TRUE;
		return;
	}

	if (sdi->status == SR_ST_STOPPING && !devc->stopping_in_progress) {
		devc->next_state = STATE_RESET_AND_IDLE;
		devc->stopping_in_progress = TRUE;

		if (libusb_submit_transfer(devc->xfer_in) != 0) {
			sr_err("Submit transfer failed: %s.",
				libusb_error_name(ret));
			devc->transfer_error = TRUE;
		}

		return;
	}

	if (devc->state != devc->next_state)
		sr_spew("State changed from %i to %i.",
			devc->state, devc->next_state);
	devc->state = devc->next_state;

	if (devc->state == STATE_WAIT_DATA_READY) {
		/* Check if the received data are a valid device status. */
		if (devc->xfer_data_in[0] == 0x05) {
			if (devc->xfer_data_in[1] == STATUS_WAITING_FOR_TRIGGER)
				sr_dbg("Waiting for trigger.");
			else if (devc->xfer_data_in[1] == STATUS_SAMPLING)
				sr_dbg("Sampling in progress.");
		}

		/*
		 * Check if the received data are a valid device status and the
		 * sample data are ready.
		 */
		if (devc->xfer_data_in[0] == 0x05 &&
				devc->xfer_data_in[1] == STATUS_DATA_READY) {
			devc->next_state = STATE_RECEIVE_DATA;
			ret = libusb_submit_transfer(transfer);
		} else {
			devc->wait_data_ready_locked = FALSE;
			devc->wait_data_ready_time = g_get_monotonic_time();
		}
	} else if (devc->state == STATE_RECEIVE_DATA) {
		last_channel = devc->probe_map[devc->num_enabled_probes - 1];

		if (devc->channel < last_channel) {
			buffer_sample_data(sdi);
		} else if (devc->channel == last_channel) {
			process_sample_data(sdi);
		} else {
			/*
			 * Stop acquisition because all samples of enabled
			 * probes are processed.
			 */
			devc->next_state = STATE_RESET_AND_IDLE;
		}

		devc->sample_packet++;
		devc->sample_packet %= devc->num_sample_packets;

		if (devc->sample_packet == 0)
			devc->channel++;

		ret = libusb_submit_transfer(transfer);
	} else if (devc->state == STATE_RESET_AND_IDLE) {
		/* Check if the received data are a valid device status. */
		if (devc->xfer_data_in[0] == 0x05) {
			if (devc->xfer_data_in[1] == STATUS_DEVICE_READY) {
				devc->next_state = STATE_IDLE;
				devc->xfer_data_out[0] = CMD_IDLE;
			} else {
				devc->next_state = STATE_WAIT_DEVICE_READY;
				devc->xfer_data_out[0] = CMD_RESET;
			}

			ret = libusb_submit_transfer(devc->xfer_out);
		} else {
			/*
			 * The received device status is invalid which
			 * indicates that the device is not ready to accept
			 * commands. Request a new device status until a valid
			 * device status is received.
			 */
			ret = libusb_submit_transfer(transfer);
		}
	} else if (devc->state == STATE_WAIT_DEVICE_READY) {
		/* Check if the received data are a valid device status. */
		if (devc->xfer_data_in[0] == 0x05) {
			if (devc->xfer_data_in[1] == STATUS_DEVICE_READY) {
				devc->next_state = STATE_IDLE;
				devc->xfer_data_out[0] = CMD_IDLE;
			} else {
				/*
				 * The received device status is valid but the
				 * device is not ready. Probably the device did
				 * not recognize the last reset. Reset the
				 * device again.
				 */
				devc->xfer_data_out[0] = CMD_RESET;
			}

			ret = libusb_submit_transfer(devc->xfer_out);
		} else {
			/*
			 * The device is not ready and therefore not able to
			 * change to the idle state. Request a new device
			 * status until the device is ready.
			 */
			ret = libusb_submit_transfer(transfer);
		}
	}

	if (ret != 0) {
		sr_err("Submit transfer failed: %s.", libusb_error_name(ret));
		devc->transfer_error = TRUE;
	}
}

SR_PRIV void sl2_receive_transfer_out( struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	int ret = 0;

	sdi = transfer->user_data;
	devc = sdi->priv;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		sr_err("Transfer to device failed: %i.", transfer->status);
		devc->transfer_error = TRUE;
		return;
	}

	if (sdi->status == SR_ST_STOPPING && !devc->stopping_in_progress) {
		devc->next_state = STATE_RESET_AND_IDLE;
		devc->stopping_in_progress = TRUE;

		if (libusb_submit_transfer(devc->xfer_in) != 0) {
			sr_err("Submit transfer failed: %s.",
				libusb_error_name(ret));

			devc->transfer_error = TRUE;
		}

		return;
	}

	if (devc->state != devc->next_state)
		sr_spew("State changed from %i to %i.",
			devc->state, devc->next_state);
	devc->state = devc->next_state;

	if (devc->state == STATE_IDLE) {
		stop_acquisition(sdi);
	} else if (devc->state == STATE_SAMPLE) {
		devc->next_state = STATE_WAIT_DATA_READY;
		ret = libusb_submit_transfer(devc->xfer_in);
	} else if (devc->state == STATE_WAIT_DEVICE_READY) {
		ret = libusb_submit_transfer(devc->xfer_in);
	}

	if (ret != 0) {
		sr_err("Submit transfer failed: %s.", libusb_error_name(ret));
		devc->transfer_error = TRUE;
	}
}

SR_PRIV int sl2_set_samplerate(const struct sr_dev_inst *sdi,
		uint64_t samplerate)
{
	struct dev_context *devc;
	unsigned int i;

	devc = sdi->priv;

	for (i = 0; i < NUM_SAMPLERATES; i++) {
		if (sl2_samplerates[i] == samplerate) {
			devc->samplerate = samplerate;
			devc->samplerate_id = NUM_SAMPLERATES - i - 1;
			return SR_OK;
		}
	}

	return SR_ERR_ARG;
}

SR_PRIV int sl2_set_limit_samples(const struct sr_dev_inst *sdi,
				  uint64_t limit_samples)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (limit_samples == 0) {
		sr_err("Invalid number of limit samples: %" PRIu64 ".",
			limit_samples);
		return SR_ERR_ARG;
	}

	if (limit_samples > MAX_SAMPLES)
		limit_samples = MAX_SAMPLES;

	sr_dbg("Limit samples set to %" PRIu64 ".", limit_samples);

	devc->limit_samples = limit_samples;

	return SR_OK;
}

SR_PRIV void sl2_configure_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_probe *probe;
	uint8_t trigger_type;
	int probe_index, num_triggers_anyedge;
	char *trigger;
	GSList *l;

	devc = sdi->priv;

	/* Disable the trigger by default. */
	devc->trigger_channel = TRIGGER_CHANNEL_0;
	devc->trigger_type = TRIGGER_TYPE_NONE;

	num_triggers_anyedge = 0;

	for (l = sdi->probes, probe_index = 0; l; l = l->next, probe_index++) {
		probe = l->data;
		trigger = probe->trigger;

		if (!trigger || !probe->enabled)
			continue;

		switch (*trigger) {
		case 'r':
			trigger_type = TRIGGER_TYPE_POSEDGE;
			break;
		case 'f':
			trigger_type = TRIGGER_TYPE_NEGEDGE;
			break;
		case 'c':
			trigger_type = TRIGGER_TYPE_ANYEDGE;
			num_triggers_anyedge++;
			break;
		default:
			continue;
		}

		devc->trigger_channel = probe_index + 1;
		devc->trigger_type = trigger_type;
	}

	/*
	 * Set trigger to any edge on all channels if the trigger for each
	 * channel is set to any edge.
	 */
	if (num_triggers_anyedge == NUM_PROBES) {
		devc->trigger_channel = TRIGGER_CHANNEL_ALL;
		devc->trigger_type = TRIGGER_TYPE_ANYEDGE;
	}

	sr_dbg("Trigger set to channel 0x%02x and type 0x%02x.",
		devc->trigger_channel, devc->trigger_type);
}

SR_PRIV int sl2_set_capture_ratio(const struct sr_dev_inst *sdi,
				  uint64_t capture_ratio)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (capture_ratio > 100) {
		sr_err("Invalid capture ratio: %" PRIu64 " %%.", capture_ratio);
		return SR_ERR_ARG;
	}

	sr_info("Capture ratio set to %" PRIu64 " %%.", capture_ratio);

	devc->capture_ratio = capture_ratio;

	return SR_OK;
}

SR_PRIV int sl2_set_after_trigger_delay(const struct sr_dev_inst *sdi,
					uint64_t after_trigger_delay)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (after_trigger_delay > MAX_AFTER_TRIGGER_DELAY) {
		sr_err("Invalid after trigger delay: %" PRIu64 " ms.",
			after_trigger_delay);
		return SR_ERR_ARG;
	}

	sr_info("After trigger delay set to %" PRIu64 " ms.",
		after_trigger_delay);

	devc->after_trigger_delay = after_trigger_delay;

	return SR_OK;
}

SR_PRIV void sl2_calculate_trigger_samples(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint64_t pre_trigger_samples, post_trigger_samples;
	uint16_t pre_trigger_bytes, post_trigger_bytes;
	uint8_t cr;

	devc = sdi->priv;
	cr = devc->capture_ratio;

	/* Ignore the capture ratio if no trigger is enabled. */
	if (devc->trigger_type == TRIGGER_TYPE_NONE)
		cr = 0;

	pre_trigger_samples = (devc->limit_samples * cr) / 100;
	post_trigger_samples = (devc->limit_samples * (100 - cr)) / 100;

	/*
	 * Increase the number of post trigger samples by one to compensate the
	 * possible loss of a sample through integer rounding.
	 */
	if (pre_trigger_samples + post_trigger_samples != devc->limit_samples)
		post_trigger_samples++;

	/*
	 * The device requires the number of samples in multiples of 8 which
	 * will also be called sample bytes in the following.
	 */
	pre_trigger_bytes = pre_trigger_samples / 8;
	post_trigger_bytes = post_trigger_samples / 8;

	/*
	 * Round up the number of sample bytes to ensure that at least the
	 * requested number of samples will be acquired. Note that due to this
	 * rounding the buffer to store these sample bytes needs to be at least
	 * one sample byte larger than the minimal number of sample bytes
	 * needed to store the requested samples.
	 */
	if (pre_trigger_samples % 8 != 0)
		pre_trigger_bytes++;

	if (post_trigger_samples % 8 != 0)
		post_trigger_bytes++;

	sr_info("Pre trigger samples: %" PRIu64 ".", pre_trigger_samples);
	sr_info("Post trigger samples: %" PRIu64 ".", post_trigger_samples);
	sr_dbg("Pre trigger sample bytes: %" PRIu16 ".", pre_trigger_bytes);
	sr_dbg("Post trigger sample bytes: %" PRIu16 ".", post_trigger_bytes);

	devc->pre_trigger_samples = pre_trigger_samples;
	devc->pre_trigger_bytes = pre_trigger_bytes;
	devc->post_trigger_bytes = post_trigger_bytes;
}

SR_PRIV int sl2_get_device_info(struct sr_usb_dev_inst usb,
		struct device_info *dev_info)
{
	struct drv_context *drvc;
	uint8_t buffer[PACKET_LENGTH];
	int ret;

	drvc = di->priv;

	if (!dev_info)
		return SR_ERR_ARG;

	if (sr_usb_open(drvc->sr_ctx->libusb_ctx, &usb) != SR_OK)
		return SR_ERR;

	/*
	 * Determine if a kernel driver is active on this interface and, if so,
	 * detach it.
	 */
	if (libusb_kernel_driver_active(usb.devhdl, USB_INTERFACE) == 1) {
		ret = libusb_detach_kernel_driver(usb.devhdl,
			USB_INTERFACE);

		if (ret < 0) {
			sr_err("Failed to detach kernel driver: %s.",
				libusb_error_name(ret));
			libusb_close(usb.devhdl);
			return SR_ERR;
		}
	}

	ret = libusb_claim_interface(usb.devhdl, USB_INTERFACE);

	if (ret) {
		sr_err("Failed to claim interface: %s.",
			libusb_error_name(ret));
		libusb_close(usb.devhdl);
		return SR_ERR;
	}

	memset(buffer, 0, sizeof(buffer));

	/*
	 * Reset the device to ensure it is in a proper state to request the
	 * device information.
	 */
	buffer[0] = CMD_RESET;
	if ((ret = sl2_transfer_out(usb.devhdl, buffer)) != PACKET_LENGTH) {
		sr_err("Resetting of device failed: %s.",
			libusb_error_name(ret));
		libusb_release_interface(usb.devhdl, USB_INTERFACE);
		libusb_close(usb.devhdl);
		return SR_ERR;
	}

	buffer[0] = CMD_INFO;
	if ((ret = sl2_transfer_out(usb.devhdl, buffer)) != PACKET_LENGTH) {
		sr_err("Requesting of device information failed: %s.",
			libusb_error_name(ret));
		libusb_release_interface(usb.devhdl, USB_INTERFACE);
		libusb_close(usb.devhdl);
		return SR_ERR;
	}

	if ((ret = sl2_transfer_in(usb.devhdl, buffer)) != PACKET_LENGTH) {
		sr_err("Receiving of device information failed: %s.",
			libusb_error_name(ret));
		libusb_release_interface(usb.devhdl, USB_INTERFACE);
		libusb_close(usb.devhdl);
		return SR_ERR;
	}

	memcpy(&(dev_info->serial), buffer + 1, sizeof(uint32_t));
	dev_info->serial = GUINT32_FROM_LE(dev_info->serial);

	dev_info->fw_ver_major = buffer[5];
	dev_info->fw_ver_minor = buffer[6];

	buffer[0] = CMD_RESET;
	if ((ret = sl2_transfer_out(usb.devhdl, buffer)) != PACKET_LENGTH) {
		sr_err("Device reset failed: %s.", libusb_error_name(ret));
		libusb_release_interface(usb.devhdl, USB_INTERFACE);
		libusb_close(usb.devhdl);
		return SR_ERR;
	}

	/*
	 * Set the device to idle state. If the device is not in idle state it
	 * possibly will reset itself after a few seconds without being used
	 * and thereby close the connection.
	 */
	buffer[0] = CMD_IDLE;
	if ((ret = sl2_transfer_out(usb.devhdl, buffer)) != PACKET_LENGTH) {
		sr_err("Failed to set device in idle state: %s.",
			libusb_error_name(ret));
		libusb_release_interface(usb.devhdl, USB_INTERFACE);
		libusb_close(usb.devhdl);
		return SR_ERR;
	}

	ret = libusb_release_interface(usb.devhdl, USB_INTERFACE);

	if (ret < 0) {
		sr_err("Failed to release interface: %s.",
			libusb_error_name(ret));
		libusb_close(usb.devhdl);
		return SR_ERR;
	}

	libusb_close(usb.devhdl);

	return SR_OK;
}

SR_PRIV int sl2_transfer_in(libusb_device_handle *dev_handle, uint8_t *data)
{
	return libusb_control_transfer(dev_handle, USB_REQUEST_TYPE_IN,
		USB_HID_GET_REPORT, USB_HID_REPORT_TYPE_FEATURE, USB_INTERFACE,
		(unsigned char *)data, PACKET_LENGTH, USB_TIMEOUT);
}

SR_PRIV int sl2_transfer_out(libusb_device_handle *dev_handle, uint8_t *data)
{
	return libusb_control_transfer(dev_handle, USB_REQUEST_TYPE_OUT,
		USB_HID_SET_REPORT, USB_HID_REPORT_TYPE_FEATURE, USB_INTERFACE,
		(unsigned char *)data, PACKET_LENGTH, USB_TIMEOUT);
}
