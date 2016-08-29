/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Daniel Elstner <daniel.kitta@gmail.com>
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
#include "protocol.h"
#include "lwla.h"

/* Submit an already filled-in USB transfer.
 */
static int submit_transfer(struct dev_context *devc,
			   struct libusb_transfer *xfer)
{
	int ret;

	ret = libusb_submit_transfer(xfer);

	if (ret != 0) {
		sr_err("Submit transfer failed: %s.", libusb_error_name(ret));
		devc->transfer_error = TRUE;
		return SR_ERR;
	}

	return SR_OK;
}

/* Set up transfer for the next register in a write sequence.
 */
static void next_reg_write(struct acquisition_state *acq)
{
	struct regval *regval;

	regval = &acq->reg_sequence[acq->reg_seq_pos];

	acq->xfer_buf_out[0] = LWLA_WORD(CMD_WRITE_REG);
	acq->xfer_buf_out[1] = LWLA_WORD(regval->reg);
	acq->xfer_buf_out[2] = LWLA_WORD_0(regval->val);
	acq->xfer_buf_out[3] = LWLA_WORD_1(regval->val);

	acq->xfer_out->length = 4 * sizeof(acq->xfer_buf_out[0]);
}

/* Set up transfer for the next register in a read sequence.
 */
static void next_reg_read(struct acquisition_state *acq)
{
	unsigned int addr;

	addr = acq->reg_sequence[acq->reg_seq_pos].reg;

	acq->xfer_buf_out[0] = LWLA_WORD(CMD_READ_REG);
	acq->xfer_buf_out[1] = LWLA_WORD(addr);

	acq->xfer_out->length = 2 * sizeof(acq->xfer_buf_out[0]);
}

/* Decode the response to a register read request.
 */
static int read_reg_response(struct acquisition_state *acq)
{
	uint32_t value;

	if (acq->xfer_in->actual_length != 4) {
		sr_err("Received size %d doesn't match expected size 4.",
		       acq->xfer_in->actual_length);
		return SR_ERR;
	}
	value = LWLA_TO_UINT32(acq->xfer_buf_in[0]);
	acq->reg_sequence[acq->reg_seq_pos].val = value;

	return SR_OK;
}

/* Enter a new state and submit the corresponding request to the device.
 */
static int submit_request(const struct sr_dev_inst *sdi,
			  enum protocol_state state)
{
	struct dev_context *devc;
	struct acquisition_state *acq;
	int ret;

	devc = sdi->priv;
	acq = devc->acquisition;

	devc->state = state;

	acq->xfer_out->length = 0;
	acq->reg_seq_pos = 0;
	acq->reg_seq_len = 0;

	/* Perform the model-specific action for the new state. */
	ret = (*devc->model->prepare_request)(sdi);

	if (ret != SR_OK) {
		devc->transfer_error = TRUE;
		return ret;
	}

	if (acq->reg_seq_pos < acq->reg_seq_len) {
		if ((state & STATE_EXPECT_RESPONSE) != 0)
			next_reg_read(acq);
		else
			next_reg_write(acq);
	}

	return submit_transfer(devc, acq->xfer_out);
}

/* Evaluate and act on the response to a capture status request.
 */
static void handle_status_response(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct acquisition_state *acq;
	unsigned int old_status;

	devc = sdi->priv;
	acq = devc->acquisition;
	old_status = acq->status;

	if ((*devc->model->handle_response)(sdi) != SR_OK) {
		devc->transfer_error = TRUE;
		return;
	}
	devc->state = STATE_STATUS_WAIT;

	sr_spew("Captured %u words, %" PRIu64 " ms, status 0x%02X.",
		acq->mem_addr_fill, acq->duration_now, acq->status);

	if ((~old_status & acq->status & STATUS_TRIGGERED) != 0)
		sr_info("Capture triggered.");

	if (acq->duration_now >= acq->duration_max) {
		sr_dbg("Time limit reached, stopping capture.");
		submit_request(sdi, STATE_STOP_CAPTURE);
	} else if ((acq->status & STATUS_TRIGGERED) == 0) {
		sr_spew("Waiting for trigger.");
	} else if ((acq->status & STATUS_MEM_AVAIL) == 0) {
		sr_dbg("Capture memory filled.");
		submit_request(sdi, STATE_LENGTH_REQUEST);
	} else if ((acq->status & STATUS_CAPTURING) != 0) {
		sr_spew("Sampling in progress.");
	}
}

/* Evaluate and act on the response to a capture length request.
 */
static void handle_length_response(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct acquisition_state *acq;

	devc = sdi->priv;
	acq = devc->acquisition;

	if ((*devc->model->handle_response)(sdi) != SR_OK) {
		devc->transfer_error = TRUE;
		return;
	}
	acq->rle = RLE_STATE_DATA;
	acq->sample = 0;
	acq->run_len = 0;
	acq->samples_done = 0;
	acq->mem_addr_done = acq->mem_addr_next;
	acq->out_index = 0;

	if (acq->mem_addr_next >= acq->mem_addr_stop) {
		submit_request(sdi, STATE_READ_FINISH);
		return;
	}
	sr_dbg("%u words in capture buffer.",
	       acq->mem_addr_stop - acq->mem_addr_next);

	submit_request(sdi, STATE_READ_PREPARE);
}

/* Evaluate and act on the response to a capture memory read request.
 */
static void handle_read_response(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct acquisition_state *acq;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	unsigned int end_addr;

	devc = sdi->priv;
	acq = devc->acquisition;

	/* Prepare session packet. */
	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = (devc->model->num_channels + 7) / 8;
	logic.data = acq->out_packet;

	end_addr = MIN(acq->mem_addr_next, acq->mem_addr_stop);
	acq->in_index = 0;

	/*
	 * Repeatedly call the model-specific read response handler until
	 * all data received in the transfer has been accounted for.
	 */
	while (!devc->cancel_requested
			&& (acq->run_len > 0 || acq->mem_addr_done < end_addr)
			&& acq->samples_done < acq->samples_max) {

		if ((*devc->model->handle_response)(sdi) != SR_OK) {
			devc->transfer_error = TRUE;
			return;
		}
		if (acq->out_index * logic.unitsize >= PACKET_SIZE) {
			/* Send off full logic packet. */
			logic.length = acq->out_index * logic.unitsize;
			sr_session_send(sdi, &packet);
			acq->out_index = 0;
		}
	}

	if (!devc->cancel_requested
			&& acq->samples_done < acq->samples_max
			&& acq->mem_addr_next < acq->mem_addr_stop) {
		/* Request the next block. */
		submit_request(sdi, STATE_READ_REQUEST);
		return;
	}

	/* Send partially filled packet as it is the last one. */
	if (!devc->cancel_requested && acq->out_index > 0) {
 		logic.length = acq->out_index * logic.unitsize;
		sr_session_send(sdi, &packet);
		acq->out_index = 0;
	}
	submit_request(sdi, STATE_READ_FINISH);
}

/* Destroy and unset the acquisition state record.
 */
static void clear_acquisition_state(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct acquisition_state *acq;

	devc = sdi->priv;
	acq = devc->acquisition;

	devc->acquisition = NULL;

	if (acq) {
		libusb_free_transfer(acq->xfer_out);
		libusb_free_transfer(acq->xfer_in);
		g_free(acq);
	}
}

/* USB I/O source callback.
 */
static int transfer_event(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct timeval tv;
	int ret;

	(void)fd;

	sdi = cb_data;
	devc = sdi->priv;
	drvc = sdi->driver->context;

	if (!devc || !drvc)
		return G_SOURCE_REMOVE;

	/* Handle pending USB events without blocking. */
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	ret = libusb_handle_events_timeout_completed(drvc->sr_ctx->libusb_ctx,
						     &tv, NULL);
	if (ret != 0) {
		sr_err("Event handling failed: %s.", libusb_error_name(ret));
		devc->transfer_error = TRUE;
	}

	if (!devc->transfer_error && devc->state == STATE_STATUS_WAIT) {
		if (devc->cancel_requested)
			submit_request(sdi, STATE_STOP_CAPTURE);
		else if (revents == 0) /* status poll timeout */
			submit_request(sdi, STATE_STATUS_REQUEST);
	}

	/* Stop processing events if an error occurred on a transfer. */
	if (devc->transfer_error)
		devc->state = STATE_IDLE;

	if (devc->state != STATE_IDLE)
		return G_SOURCE_CONTINUE;

	sr_info("Acquisition stopped.");

	/* We are done, clean up and send end packet to session bus. */
	clear_acquisition_state(sdi);
	std_session_send_df_end(sdi);

	return G_SOURCE_REMOVE;
}

/* USB output transfer completion callback.
 */
static void LIBUSB_CALL transfer_out_completed(struct libusb_transfer *transfer)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct acquisition_state *acq;

	sdi = transfer->user_data;
	devc = sdi->priv;
	acq = devc->acquisition;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		sr_err("Transfer to device failed (state %d): %s.",
		       devc->state, libusb_error_name(transfer->status));
		devc->transfer_error = TRUE;
		return;
	}

	/* If this was a read request, wait for the response. */
	if ((devc->state & STATE_EXPECT_RESPONSE) != 0) {
		submit_transfer(devc, acq->xfer_in);
		return;
	}
	if (acq->reg_seq_pos < acq->reg_seq_len)
		acq->reg_seq_pos++; /* register write completed */

	/* Repeat until all queued registers have been written. */
	if (acq->reg_seq_pos < acq->reg_seq_len && !devc->cancel_requested) {
		next_reg_write(acq);
		submit_transfer(devc, acq->xfer_out);
		return;
	}

	switch (devc->state) {
	case STATE_START_CAPTURE:
		sr_info("Acquisition started.");

		if (!devc->cancel_requested)
			devc->state = STATE_STATUS_WAIT;
		else
			submit_request(sdi, STATE_STOP_CAPTURE);
		break;
	case STATE_STOP_CAPTURE:
		if (!devc->cancel_requested)
			submit_request(sdi, STATE_LENGTH_REQUEST);
		else
			devc->state = STATE_IDLE;
		break;
	case STATE_READ_PREPARE:
		if (acq->mem_addr_next < acq->mem_addr_stop && !devc->cancel_requested)
			submit_request(sdi, STATE_READ_REQUEST);
		else
			submit_request(sdi, STATE_READ_FINISH);
		break;
	case STATE_READ_FINISH:
		devc->state = STATE_IDLE;
		break;
	default:
		sr_err("Unexpected device state %d.", devc->state);
		devc->transfer_error = TRUE;
		break;
	}
}

/* USB input transfer completion callback.
 */
static void LIBUSB_CALL transfer_in_completed(struct libusb_transfer *transfer)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct acquisition_state *acq;

	sdi = transfer->user_data;
	devc = sdi->priv;
	acq = devc->acquisition;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		sr_err("Transfer from device failed (state %d): %s.",
		       devc->state, libusb_error_name(transfer->status));
		devc->transfer_error = TRUE;
		return;
	}
	if ((devc->state & STATE_EXPECT_RESPONSE) == 0) {
		sr_err("Unexpected completion of input transfer (state %d).",
		       devc->state);
		devc->transfer_error = TRUE;
		return;
	}

	if (acq->reg_seq_pos < acq->reg_seq_len && !devc->cancel_requested) {
		/* Complete register read sequence. */
		if (read_reg_response(acq) != SR_OK) {
			devc->transfer_error = TRUE;
			return;
		}
		/* Repeat until all queued registers have been read. */
		if (++acq->reg_seq_pos < acq->reg_seq_len) {
			next_reg_read(acq);
			submit_transfer(devc, acq->xfer_out);
			return;
		}
	}

	switch (devc->state) {
	case STATE_STATUS_REQUEST:
		if (devc->cancel_requested)
			submit_request(sdi, STATE_STOP_CAPTURE);
		else
			handle_status_response(sdi);
		break;
	case STATE_LENGTH_REQUEST:
		if (devc->cancel_requested)
			submit_request(sdi, STATE_READ_FINISH);
		else
			handle_length_response(sdi);
		break;
	case STATE_READ_REQUEST:
		handle_read_response(sdi);
		break;
	default:
		sr_err("Unexpected device state %d.", devc->state);
		devc->transfer_error = TRUE;
		break;
	}
}

/* Set up the acquisition state record.
 */
static int init_acquisition_state(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct acquisition_state *acq;

	devc = sdi->priv;
	usb = sdi->conn;

	if (devc->acquisition) {
		sr_err("Acquisition still in progress?");
		return SR_ERR;
	}
	if (devc->cfg_clock_source == CLOCK_INTERNAL && devc->samplerate == 0) {
		sr_err("Samplerate not set.");
		return SR_ERR;
	}

	acq = g_try_malloc0(sizeof(struct acquisition_state));
	if (!acq)
		return SR_ERR_MALLOC;

	acq->xfer_in = libusb_alloc_transfer(0);
	if (!acq->xfer_in) {
		g_free(acq);
		return SR_ERR_MALLOC;
	}
	acq->xfer_out = libusb_alloc_transfer(0);
	if (!acq->xfer_out) {
		libusb_free_transfer(acq->xfer_in);
		g_free(acq);
		return SR_ERR_MALLOC;
	}

	libusb_fill_bulk_transfer(acq->xfer_out, usb->devhdl, EP_COMMAND,
				  (unsigned char *)acq->xfer_buf_out, 0,
				  &transfer_out_completed,
				  (struct sr_dev_inst *)sdi, USB_TIMEOUT_MS);

	libusb_fill_bulk_transfer(acq->xfer_in, usb->devhdl, EP_REPLY,
				  (unsigned char *)acq->xfer_buf_in,
				  sizeof(acq->xfer_buf_in),
				  &transfer_in_completed,
				  (struct sr_dev_inst *)sdi, USB_TIMEOUT_MS);

	if (devc->limit_msec > 0) {
		acq->duration_max = devc->limit_msec;
		sr_info("Acquisition time limit %" PRIu64 " ms.",
			devc->limit_msec);
	} else
		acq->duration_max = MAX_LIMIT_MSEC;

	if (devc->limit_samples > 0) {
		acq->samples_max = devc->limit_samples;
		sr_info("Acquisition sample count limit %" PRIu64 ".",
			devc->limit_samples);
	} else
		acq->samples_max = MAX_LIMIT_SAMPLES;

	if (devc->cfg_clock_source == CLOCK_INTERNAL) {
		sr_info("Internal clock, samplerate %" PRIu64 ".",
			devc->samplerate);
		/* Ramp up clock speed to enable samplerates above 100 MS/s. */
		acq->clock_boost = (devc->samplerate > SR_MHZ(100));

		/* If only one of the limits is set, derive the other one. */
		if (devc->limit_msec == 0 && devc->limit_samples > 0)
			acq->duration_max = devc->limit_samples
					* 1000 / devc->samplerate + 1;
		else if (devc->limit_samples == 0 && devc->limit_msec > 0)
			acq->samples_max = devc->limit_msec
					* devc->samplerate / 1000;
	} else {
		acq->clock_boost = TRUE;

		if (devc->cfg_clock_edge == EDGE_POSITIVE)
			sr_info("External clock, rising edge.");
		else
			sr_info("External clock, falling edge.");
	}

	acq->rle_enabled = devc->cfg_rle;
	devc->acquisition = acq;

	return SR_OK;
}

SR_PRIV int lwla_start_acquisition(const struct sr_dev_inst *sdi)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	int ret;
	const int poll_interval_ms = 100;

	drvc = sdi->driver->context;
	devc = sdi->priv;

	if (devc->state != STATE_IDLE) {
		sr_err("Not in idle state, cannot start acquisition.");
		return SR_ERR;
	}
	devc->cancel_requested = FALSE;
	devc->transfer_error = FALSE;

	ret = init_acquisition_state(sdi);
	if (ret != SR_OK)
		return ret;

	ret = (*devc->model->setup_acquisition)(sdi);
	if (ret != SR_OK) {
		sr_err("Failed to set up device for acquisition.");
		clear_acquisition_state(sdi);
		return ret;
	}
	/* Register event source for asynchronous USB I/O. */
	ret = usb_source_add(sdi->session, drvc->sr_ctx, poll_interval_ms,
			     &transfer_event, (struct sr_dev_inst *)sdi);
	if (ret != SR_OK) {
		clear_acquisition_state(sdi);
		return ret;
	}
	ret = submit_request(sdi, STATE_START_CAPTURE);

	if (ret == SR_OK)
		ret = std_session_send_df_header(sdi);

	if (ret != SR_OK) {
		usb_source_remove(sdi->session, drvc->sr_ctx);
		clear_acquisition_state(sdi);
	}

	return ret;
}
