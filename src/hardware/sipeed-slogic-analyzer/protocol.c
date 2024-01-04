/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 taorye <taorye@outlook.com>
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

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer);
static int command_start_acquisition(const struct sr_dev_inst *sdi);

SR_PRIV int sipeed_slogic_analyzer_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;

	devc = sdi->priv;
	if (!devc)
		return TRUE;

	drvc = sdi->driver->context;
	if (!drvc)
		return TRUE;

	struct timeval tv = {
		.tv_sec = 0,
		.tv_usec = 0,
	};
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	return TRUE;
}

SR_PRIV int sipeed_slogic_acquisition_start(const struct sr_dev_inst *sdi)
{
	/* TODO: configure hardware, reset acquisition state, set up
	 * callbacks and send header packet. */

	(void)sdi;DBG_VAL(sdi);
	struct dev_context *devc = sdi->priv;

	int timeout = get_timeout(devc);
	usb_source_add(sdi->session, sdi->session->ctx, timeout, sipeed_slogic_analyzer_receive_data, sdi);

	struct sr_usb_dev_inst *usb = sdi->conn;
	devc->sent_samples = 0;
	devc->acq_aborted = FALSE;
	devc->empty_transfer_count = 0;

	struct sr_trigger *trigger;
	if ((trigger = sr_session_trigger_get(sdi->session))) {
		int pre_trigger_samples = 0;
		if (devc->limit_samples > 0)
			pre_trigger_samples = (devc->capture_ratio * devc->limit_samples) / 100;
		devc->stl = soft_trigger_logic_new(sdi, trigger, pre_trigger_samples);
		if (!devc->stl)
			return SR_ERR_MALLOC;
		devc->trigger_fired = FALSE;
	} else {
		std_session_send_df_frame_begin(sdi);
		devc->trigger_fired = TRUE;
	}

	devc->submitted_transfers = 0;
	size_t num_transfers = get_number_of_transfers(devc);
	devc->num_transfers = num_transfers;
	devc->transfers = g_try_malloc0(sizeof(*devc->transfers) * devc->num_transfers);
	if (!devc->transfers) {
		sr_err("USB transfers malloc failed.");
		return SR_ERR_MALLOC;
	}
	size_t size = get_buffer_size(devc);
	for (int i = 0; i < devc->num_transfers; i++) {
		uint8_t *buf = g_try_malloc(size * (8+1)); /* max 8xu1 */
		if (!buf) {
			sr_err("USB transfer buffer malloc failed.");
			return SR_ERR_MALLOC;
		}
		struct libusb_transfer *transfer = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(transfer, usb->devhdl,
				1 | LIBUSB_ENDPOINT_IN, buf, size,
				receive_transfer, (void *)sdi, timeout);
		sr_info("submitting transfer: %d", i);
		int ret = 0;
		if ((ret = libusb_submit_transfer(transfer)) != 0) {
			sr_err("Failed to submit transfer: %s.",
			       libusb_error_name(ret));
			libusb_free_transfer(transfer);
			g_free(buf);
			sipeed_slogic_acquisition_stop(sdi);
			return SR_ERR;
		}
		devc->transfers[i] = transfer;
		devc->submitted_transfers++;
	}

	std_session_send_df_header(sdi);

	int ret = SR_OK;
	if ((ret = command_start_acquisition(sdi)) != SR_OK) {
		sipeed_slogic_acquisition_stop(sdi);
		return ret;
	}

	return SR_OK;
}

SR_PRIV int sipeed_slogic_acquisition_stop(struct sr_dev_inst *sdi)
{
	/* TODO: stop acquisition. */

	(void)sdi;DBG_VAL(sdi);
	struct dev_context *devc = sdi->priv;

	devc->acq_aborted = TRUE;
	for (int i = devc->num_transfers - 1; i >= 0; i--) {
		if (devc->transfers[i])
			libusb_cancel_transfer(devc->transfers[i]);
	}
	return SR_OK;
}

static void finish_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	std_session_send_df_end(sdi);

	usb_source_remove(sdi->session, sdi->session->ctx);

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

static void la_send_data_proc(struct sr_dev_inst *sdi,
	uint8_t *data, size_t length, size_t sample_width)
{
	const struct sr_datafeed_logic logic = {
		.length = length,
		.unitsize = sample_width,
		.data = data
	};

	const struct sr_datafeed_packet packet = {
		.type = SR_DF_LOGIC,
		.payload = &logic
	};

	sr_session_send(sdi, &packet);
}

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi = transfer->user_data;
	struct dev_context *devc = sdi->priv;
	gboolean packet_has_error = FALSE;
	unsigned int num_samples;
	int trigger_offset, cur_sample_count, unitsize, processed_samples;
	int pre_trigger_samples;

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
	unitsize = 1+(((1<<devc->logic_pattern)-1)>>3);
	cur_sample_count = transfer->actual_length * 8 / (1<<devc->logic_pattern);
	processed_samples = 0;

	switch (transfer->status) {
	case LIBUSB_TRANSFER_NO_DEVICE:
		sipeed_slogic_acquisition_stop(sdi);
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
			sipeed_slogic_acquisition_stop(sdi);
			free_transfer(transfer);
		} else {
			resubmit_transfer(transfer);
		}
		return;
	} else {
		devc->empty_transfer_count = 0;
	}

	uint8_t real_bits = 1<<devc->logic_pattern;
check_trigger:
	if (real_bits < 8) {
		for (int i = cur_sample_count-1; i>=0; i--) {
			
			((uint8_t *)transfer->buffer+get_buffer_size(devc))[i] = 
				(((uint8_t *)transfer->buffer)[i/(8/real_bits)] >> (real_bits*(i%(8/real_bits))))
				&((1<<real_bits)-1);
		}
	}
	if (devc->trigger_fired) {
		if (!devc->limit_samples || devc->sent_samples < devc->limit_samples) {
			/* Send the incoming transfer to the session bus. */
			num_samples = cur_sample_count - processed_samples;
			if (devc->limit_samples && devc->sent_samples + num_samples > devc->limit_samples)
				num_samples = devc->limit_samples - devc->sent_samples;

			la_send_data_proc(sdi, (uint8_t *)transfer->buffer + (real_bits<8?get_buffer_size(devc):0)
				+ processed_samples * unitsize,
				num_samples * unitsize, unitsize);
			devc->sent_samples += num_samples;
			processed_samples += num_samples;
		}
	} else {
		trigger_offset = soft_trigger_logic_check(devc->stl,
			transfer->buffer + processed_samples * unitsize,
			transfer->actual_length - processed_samples * unitsize,
			&pre_trigger_samples);
		if (trigger_offset > -1) {
			std_session_send_df_frame_begin(sdi);
			devc->sent_samples += pre_trigger_samples;
			num_samples = cur_sample_count - processed_samples - trigger_offset;
			if (devc->limit_samples &&
					devc->sent_samples + num_samples > devc->limit_samples)
				num_samples = devc->limit_samples - devc->sent_samples;

			la_send_data_proc(sdi, (uint8_t *)transfer->buffer + (real_bits<8?get_buffer_size(devc):0)
				+ processed_samples * unitsize
				+ trigger_offset * unitsize,
				num_samples * unitsize, unitsize);
			devc->sent_samples += num_samples;
			processed_samples += trigger_offset + num_samples;

			devc->trigger_fired = TRUE;
		}
	}

	const int frame_ended = devc->limit_samples && (devc->sent_samples >= devc->limit_samples);
	const int final_frame = devc->limit_frames && (devc->num_frames >= (devc->limit_frames - 1));

	if (frame_ended) {
		devc->num_frames++;
		devc->sent_samples = 0;
		devc->trigger_fired = FALSE;
		std_session_send_df_frame_end(sdi);

		/* There may be another trigger in the remaining data, go back and check for it */
		if (processed_samples < cur_sample_count) {
			/* Reset the trigger stage */
			if (devc->stl)
				devc->stl->cur_stage = 0;
			else {
				std_session_send_df_frame_begin(sdi);
				devc->trigger_fired = TRUE;
			}
			if (!final_frame)
				goto check_trigger;
		}
	}
	if (frame_ended && final_frame) {
		sipeed_slogic_acquisition_stop(sdi);
		free_transfer(transfer);
	} else
		resubmit_transfer(transfer);
}

#define USB_TIMEOUT 100

static int command_start_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	uint64_t samplerate, samplechannel;
	struct cmd_start_acquisition cmd;
	int ret;

	devc = sdi->priv;
	usb = sdi->conn;
	samplerate = devc->cur_samplerate;
	samplechannel = 1<<devc->logic_pattern;

	/* Compute the sample rate. */
	if (0) {
		sr_err("Unable to sample at %" PRIu64 "Hz "
		       "when collecting 16-bit samples.", samplerate);
		return SR_ERR;
	}

	if ((SR_MHZ(160) % samplerate) != 0 || samplechannel * samplerate > 40 * 8 * 1000 * 1000) {
		sr_err("Unable to sample at %" PRIu64 "Hz.", samplerate);
		return SR_ERR;
	}

	sr_dbg("SLogic samplerate(%dch) = %d, clocksource = %sMHz.", samplechannel, samplerate, "160");

	samplerate /= SR_MHZ(1);
	cmd.sample_rate_h = (samplerate >> 8) & 0xff;
	cmd.sample_rate_l = samplerate & 0xff;
	cmd.sample_channel = samplechannel;

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
