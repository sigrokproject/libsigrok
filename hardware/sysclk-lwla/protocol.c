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

#include "protocol.h"
#include <string.h>

/* Bit mask for the RLE repeat-count-follows flag. */
#define RLE_FLAG_LEN_FOLLOWS ((uint64_t)1 << 35)

/* Start address of capture status memory area to read. */
#define CAP_STAT_ADDR 5

/* Number of 64-bit words read from the capture status memory. */
#define CAP_STAT_LEN 5

/* The bitstream filenames are indexed by the clock source enumeration.
 */
static const char bitstream_map[][32] = {
	"sysclk-lwla1034-off.rbf",
	"sysclk-lwla1034-int.rbf",
	"sysclk-lwla1034-extpos.rbf",
	"sysclk-lwla1034-extneg.rbf",
};

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

/* Set up the LWLA in preparation for an acquisition session.
 */
static int capture_setup(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct acquisition_state *acq;
	uint64_t divider_count;
	uint64_t memory_limit;
	uint16_t command[3 + 10*4];

	devc = sdi->priv;
	acq  = devc->acquisition;

	command[0] = LWLA_WORD(CMD_CAP_SETUP);
	command[1] = LWLA_WORD(0); /* address */
	command[2] = LWLA_WORD(10); /* length */

	command[3] = LWLA_WORD_0(devc->channel_mask);
	command[4] = LWLA_WORD_1(devc->channel_mask);
	command[5] = LWLA_WORD_2(devc->channel_mask);
	command[6] = LWLA_WORD_3(devc->channel_mask);

	/* Set the clock divide counter maximum for samplerates of up to
	 * 100 MHz. At the highest samplerate of 125 MHz the clock divider
	 * is bypassed.
	 */
	if (!acq->bypass_clockdiv && devc->samplerate > 0)
		divider_count = SR_MHZ(100) / devc->samplerate - 1;
	else
		divider_count = 0;

	command[7]  = LWLA_WORD_0(divider_count);
	command[8]  = LWLA_WORD_1(divider_count);
	command[9]  = LWLA_WORD_2(divider_count);
	command[10] = LWLA_WORD_3(divider_count);

	command[11] = LWLA_WORD_0(devc->trigger_values);
	command[12] = LWLA_WORD_1(devc->trigger_values);
	command[13] = LWLA_WORD_2(devc->trigger_values);
	command[14] = LWLA_WORD_3(devc->trigger_values);

	command[15] = LWLA_WORD_0(devc->trigger_edge_mask);
	command[16] = LWLA_WORD_1(devc->trigger_edge_mask);
	command[17] = LWLA_WORD_2(devc->trigger_edge_mask);
	command[18] = LWLA_WORD_3(devc->trigger_edge_mask);

	command[19] = LWLA_WORD_0(devc->trigger_mask);
	command[20] = LWLA_WORD_1(devc->trigger_mask);
	command[21] = LWLA_WORD_2(devc->trigger_mask);
	command[22] = LWLA_WORD_3(devc->trigger_mask);

	/* Set the capture memory full threshold. This is slightly less
	 * than the actual maximum, most likely in order to compensate for
	 * pipeline latency.
	 */
	memory_limit = MEMORY_DEPTH - 16;

	command[23] = LWLA_WORD_0(memory_limit);
	command[24] = LWLA_WORD_1(memory_limit);
	command[25] = LWLA_WORD_2(memory_limit);
	command[26] = LWLA_WORD_3(memory_limit);

	/* Fill remaining 64-bit words with zeroes. */
	memset(&command[27], 0, 16 * sizeof(uint16_t));

	return lwla_send_command(sdi->conn, command, G_N_ELEMENTS(command));
}

/* Issue a register write command as an asynchronous USB transfer.
 */
static int issue_write_reg(const struct sr_dev_inst *sdi,
			   unsigned int reg, unsigned int value)
{
	struct dev_context *devc;
	struct acquisition_state *acq;

	devc = sdi->priv;
	acq  = devc->acquisition;

	acq->xfer_buf_out[0] = LWLA_WORD(CMD_WRITE_REG);
	acq->xfer_buf_out[1] = LWLA_WORD(reg);
	acq->xfer_buf_out[2] = LWLA_WORD_0(value);
	acq->xfer_buf_out[3] = LWLA_WORD_1(value);

	acq->xfer_out->length = 4 * sizeof(uint16_t);

	return submit_transfer(devc, acq->xfer_out);
}

/* Issue a register write command as an asynchronous USB transfer for the
 * next register/value pair of the currently active register write sequence.
 */
static int issue_next_write_reg(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct regval_pair *regval;
	int ret;

	devc = sdi->priv;

	if (devc->reg_write_pos >= devc->reg_write_len) {
		sr_err("Already written all registers in sequence.");
		return SR_ERR_BUG;
	}
	regval = &devc->reg_write_seq[devc->reg_write_pos];

	ret = issue_write_reg(sdi, regval->reg, regval->val);
	if (ret != SR_OK)
		return ret;

	++devc->reg_write_pos;
	return SR_OK;
}

/* Issue a capture status request as an asynchronous USB transfer.
 */
static void request_capture_status(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct acquisition_state *acq;

	devc = sdi->priv;
	acq  = devc->acquisition;

	acq->xfer_buf_out[0] = LWLA_WORD(CMD_CAP_STATUS);
	acq->xfer_buf_out[1] = LWLA_WORD(CAP_STAT_ADDR);
	acq->xfer_buf_out[2] = LWLA_WORD(CAP_STAT_LEN);

	acq->xfer_out->length = 3 * sizeof(uint16_t);

	if (submit_transfer(devc, acq->xfer_out) == SR_OK)
		devc->state = STATE_STATUS_REQUEST;
}

/* Issue a request for the capture buffer fill level as
 * an asynchronous USB transfer.
 */
static void request_capture_length(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct acquisition_state *acq;

	devc = sdi->priv;
	acq  = devc->acquisition;

	acq->xfer_buf_out[0] = LWLA_WORD(CMD_READ_REG);
	acq->xfer_buf_out[1] = LWLA_WORD(REG_MEM_FILL);

	acq->xfer_out->length = 2 * sizeof(uint16_t);

	if (submit_transfer(devc, acq->xfer_out) == SR_OK)
		devc->state = STATE_LENGTH_REQUEST;
}

/* Initiate the capture memory read operation:  Reset the acquisition state
 * and start a sequence of register writes in order to set up the device for
 * reading from the capture buffer.
 */
static void issue_read_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct acquisition_state *acq;
	struct regval_pair *regvals;

	devc = sdi->priv;
	acq  = devc->acquisition;

	/* Reset RLE state. */
	acq->rle = RLE_STATE_DATA;
	acq->sample  = 0;
	acq->run_len = 0;

	acq->samples_done = 0;

	/* For some reason, the start address is 4 rather than 0. */
	acq->mem_addr_done = 4;
	acq->mem_addr_next = 4;
	acq->mem_addr_stop = acq->mem_addr_fill;

	/* Sample position in the packet output buffer. */
	acq->out_index = 0;

	regvals = devc->reg_write_seq;

	regvals[0].reg = REG_DIV_BYPASS;
	regvals[0].val = 1;

	regvals[1].reg = REG_MEM_CTRL2;
	regvals[1].val = 2;

	regvals[2].reg = REG_MEM_CTRL4;
	regvals[2].val = 4;

	devc->reg_write_pos = 0;
	devc->reg_write_len = 3;

	if (issue_next_write_reg(sdi) == SR_OK)
		devc->state = STATE_READ_PREPARE;
}

/* Issue a command as an asynchronous USB transfer which returns the device
 * to normal state after a read operation.  Sets a new device context state
 * on success.
 */
static void issue_read_end(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (issue_write_reg(sdi, REG_DIV_BYPASS, 0) == SR_OK)
		devc->state = STATE_READ_END;
}

/* Decode an incoming reponse to a buffer fill level request and act on it
 * as appropriate.  Note that this function changes the device context state.
 */
static void process_capture_length(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct acquisition_state *acq;

	devc = sdi->priv;
	acq  = devc->acquisition;

	if (acq->xfer_in->actual_length != 4) {
		sr_err("Received size %d doesn't match expected size 4.",
		       acq->xfer_in->actual_length);
		devc->transfer_error = TRUE;
		return;
	}
	acq->mem_addr_fill = LWLA_READ32(acq->xfer_buf_in);

	sr_dbg("%zu words in capture buffer.", acq->mem_addr_fill);

	if (acq->mem_addr_fill > 0 && sdi->status == SR_ST_ACTIVE)
		issue_read_start(sdi);
	else
		issue_read_end(sdi);
}

/* Initiate a sequence of register write commands with the effect of
 * cancelling a running capture operation.  This sets a new device state
 * if issuing the first command succeeds.
 */
static void issue_stop_capture(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct regval_pair *regvals;

	devc = sdi->priv;

	if (devc->stopping_in_progress)
		return;

	regvals = devc->reg_write_seq;

	regvals[0].reg = REG_CMD_CTRL2;
	regvals[0].val = 10;

	regvals[1].reg = REG_CMD_CTRL3;
	regvals[1].val = 0;

	regvals[2].reg = REG_CMD_CTRL4;
	regvals[2].val = 0;

	regvals[3].reg = REG_CMD_CTRL1;
	regvals[3].val = 0;

	regvals[4].reg = REG_DIV_BYPASS;
	regvals[4].val = 0;

	devc->reg_write_pos = 0;
	devc->reg_write_len = 5;

	if (issue_next_write_reg(sdi) == SR_OK) {
		devc->stopping_in_progress = TRUE;
		devc->state = STATE_STOP_CAPTURE;
	}
}

/* Decode an incoming capture status reponse and act on it as appropriate.
 * Note that this function changes the device state.
 */
static void process_capture_status(const struct sr_dev_inst *sdi)
{
	uint64_t duration;
	struct dev_context *devc;
	struct acquisition_state *acq;
	unsigned int mem_fill;
	unsigned int flags;

	devc = sdi->priv;
	acq  = devc->acquisition;

	if (acq->xfer_in->actual_length != CAP_STAT_LEN * 8) {
		sr_err("Received size %d doesn't match expected size %d.",
		       acq->xfer_in->actual_length, CAP_STAT_LEN * 8);
		devc->transfer_error = TRUE;
		return;
	}

	/* TODO: Find out the actual bit width of these fields as stored
	 * in the FPGA.  These fields are definitely less than 64 bit wide
	 * internally, and the unused bits occasionally even contain garbage.
	 */
	mem_fill = LWLA_READ32(&acq->xfer_buf_in[0]);
	duration = LWLA_READ32(&acq->xfer_buf_in[8]);
	flags    = LWLA_READ32(&acq->xfer_buf_in[16]) & STATUS_FLAG_MASK;

	/* The LWLA1034 runs at 125 MHz if the clock divider is bypassed.
	 * However, the time base used for the duration is apparently not
	 * adjusted for this "boost" mode.  Whereas normally the duration
	 * unit is 1 ms, it is 0.8 ms when the clock divider is bypassed.
	 * As 0.8 = 100 MHz / 125 MHz, it seems that the internal cycle
	 * counter period is the same as at the 100 MHz setting.
	 */
	if (acq->bypass_clockdiv)
		acq->duration_now = duration * 4 / 5;
	else
		acq->duration_now = duration;

	sr_spew("Captured %u words, %" PRIu64 " ms, flags 0x%02X.",
		mem_fill, acq->duration_now, flags);

	if ((flags & STATUS_TRIGGERED) > (acq->capture_flags & STATUS_TRIGGERED))
		sr_info("Capture triggered.");

	acq->capture_flags = flags;

	if (acq->duration_now >= acq->duration_max) {
		sr_dbg("Time limit reached, stopping capture.");
		issue_stop_capture(sdi);
		return;
	}
	devc->state = STATE_STATUS_WAIT;

	if ((acq->capture_flags & STATUS_TRIGGERED) == 0) {
		sr_spew("Waiting for trigger.");
	} else if ((acq->capture_flags & STATUS_MEM_AVAIL) == 0) {
		sr_dbg("Capture memory filled.");
		request_capture_length(sdi);
	} else if ((acq->capture_flags & STATUS_CAPTURING) != 0) {
		sr_spew("Sampling in progress.");
	}
}

/* Issue a capture buffer read request as an asynchronous USB transfer.
 * The address and size of the memory area to read are derived from the
 * current acquisition state.
 */
static void request_read_mem(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct acquisition_state *acq;
	size_t count;

	devc = sdi->priv;
	acq  = devc->acquisition;

	if (acq->mem_addr_next >= acq->mem_addr_stop)
		return;

	/* Always read a multiple of 8 device words. */
	count = (acq->mem_addr_stop - acq->mem_addr_next + 7) / 8 * 8;
	count = MIN(count, READ_CHUNK_LEN);

	acq->xfer_buf_out[0] = LWLA_WORD(CMD_READ_MEM);
	acq->xfer_buf_out[1] = LWLA_WORD_0(acq->mem_addr_next);
	acq->xfer_buf_out[2] = LWLA_WORD_1(acq->mem_addr_next);
	acq->xfer_buf_out[3] = LWLA_WORD_0(count);
	acq->xfer_buf_out[4] = LWLA_WORD_1(count);

	acq->xfer_out->length = 5 * sizeof(uint16_t);

	if (submit_transfer(devc, acq->xfer_out) == SR_OK) {
		acq->mem_addr_next += count;
		devc->state = STATE_READ_REQUEST;
	}
}

/* Demangle and decompress incoming sample data from the capture buffer.
 * The data chunk is taken from the acquisition state, and is expected to
 * contain a multiple of 8 device words.
 * All data currently in the acquisition buffer will be processed.  Packets
 * of decoded samples are sent off to the session bus whenever the output
 * buffer becomes full while decoding.
 */
static int process_sample_data(const struct sr_dev_inst *sdi)
{
	uint64_t sample;
	uint64_t high_nibbles;
	uint64_t word;
	struct dev_context *devc;
	struct acquisition_state *acq;
	uint8_t *out_p;
	uint16_t *slice;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	size_t expect_len;
	size_t actual_len;
	size_t out_max_samples;
	size_t out_run_samples;
	size_t ri;
	size_t in_words_left;
	size_t si;

	devc = sdi->priv;
	acq  = devc->acquisition;

	if (acq->mem_addr_done >= acq->mem_addr_stop
			|| acq->samples_done >= acq->samples_max)
		return SR_OK;

	in_words_left = MIN(acq->mem_addr_stop - acq->mem_addr_done,
			    READ_CHUNK_LEN);
	expect_len = LWLA1034_MEMBUF_LEN(in_words_left) * sizeof(uint16_t);
	actual_len = acq->xfer_in->actual_length;

	if (actual_len != expect_len) {
		sr_err("Received size %zu does not match expected size %zu.",
		       actual_len, expect_len);
		devc->transfer_error = TRUE;
		return SR_ERR;
	}
	acq->mem_addr_done += in_words_left;

	/* Prepare session packet. */
	packet.type    = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = UNIT_SIZE;
	logic.data     = acq->out_packet;

	slice = acq->xfer_buf_in;
	si = 0; /* word index within slice */

	for (;;) {
		/* Calculate number of samples to write into packet. */
		out_max_samples = MIN(acq->samples_max - acq->samples_done,
				      PACKET_LENGTH - acq->out_index);
		out_run_samples = MIN(acq->run_len, out_max_samples);

		/* Expand run-length samples into session packet. */
		sample = acq->sample;
		out_p = &acq->out_packet[acq->out_index * UNIT_SIZE];

		for (ri = 0; ri < out_run_samples; ++ri) {
			out_p[0] =  sample        & 0xFF;
			out_p[1] = (sample >>  8) & 0xFF;
			out_p[2] = (sample >> 16) & 0xFF;
			out_p[3] = (sample >> 24) & 0xFF;
			out_p[4] = (sample >> 32) & 0xFF;
			out_p += UNIT_SIZE;
		}
		acq->run_len -= out_run_samples;
		acq->out_index += out_run_samples;
		acq->samples_done += out_run_samples;

		/* Packet full or sample count limit reached? */
		if (out_run_samples == out_max_samples) {
			logic.length = acq->out_index * UNIT_SIZE;
			sr_session_send(sdi, &packet);
			acq->out_index = 0;

			if (acq->samples_done >= acq->samples_max)
				return SR_OK; /* sample limit reached */
			if (acq->run_len > 0)
				continue; /* need another packet */
		}

		if (in_words_left == 0)
			break; /* done with current chunk */

		/* Now work on the current slice. */
		high_nibbles = LWLA_READ32(&slice[8 * 2]);
		word = LWLA_READ32(&slice[si * 2]);
		word |= (high_nibbles << (4 * si + 4)) & ((uint64_t)0xF << 32);

		if (acq->rle == RLE_STATE_DATA) {
			acq->sample = word & ALL_CHANNELS_MASK;
			acq->run_len = ((word >> NUM_PROBES) & 1) + 1;
			if (word & RLE_FLAG_LEN_FOLLOWS)
				acq->rle = RLE_STATE_LEN;
		} else {
			acq->run_len += word << 1;
			acq->rle = RLE_STATE_DATA;
		}

		/* Move to next word. */
		if (++si >= 8) {
			si = 0;
			slice += 9 * 2;
		}
		--in_words_left;
	}

	/* Send out partially filled packet if this was the last chunk. */
	if (acq->mem_addr_done >= acq->mem_addr_stop && acq->out_index > 0) {
		logic.length = acq->out_index * UNIT_SIZE;
		sr_session_send(sdi, &packet);
		acq->out_index = 0;
	}
	return SR_OK;
}

/* Finish an acquisition session.  This sends the end packet to the session
 * bus and removes the listener for asynchronous USB transfers.
 */
static void end_acquisition(struct sr_dev_inst *sdi)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;

	drvc = sdi->driver->priv;
	devc = sdi->priv;

	if (devc->state == STATE_IDLE)
		return;

	devc->state = STATE_IDLE;

	/* Remove USB file descriptors from polling. */
	usb_source_remove(drvc->sr_ctx);

	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);

	lwla_free_acquisition_state(devc->acquisition);
	devc->acquisition = NULL;

	sdi->status = SR_ST_ACTIVE;
}

/* USB output transfer completion callback.
 */
static void receive_transfer_out(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	sdi  = transfer->user_data;
	devc = sdi->priv;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		sr_err("Transfer to device failed: %d.", transfer->status);
		devc->transfer_error = TRUE;
		return;
	}

	if (devc->reg_write_pos < devc->reg_write_len) {
		issue_next_write_reg(sdi);
	} else {
		switch (devc->state) {
		case STATE_START_CAPTURE:
			devc->state = STATE_STATUS_WAIT;
			break;
		case STATE_STATUS_REQUEST:
			devc->state = STATE_STATUS_RESPONSE;
			submit_transfer(devc, devc->acquisition->xfer_in);
			break;
		case STATE_STOP_CAPTURE:
			if (sdi->status == SR_ST_ACTIVE)
				request_capture_length(sdi);
			else
				end_acquisition(sdi);
			break;
		case STATE_LENGTH_REQUEST:
			devc->state = STATE_LENGTH_RESPONSE;
			submit_transfer(devc, devc->acquisition->xfer_in);
			break;
		case STATE_READ_PREPARE:
			request_read_mem(sdi);
			break;
		case STATE_READ_REQUEST:
			devc->state = STATE_READ_RESPONSE;
			submit_transfer(devc, devc->acquisition->xfer_in);
			break;
		case STATE_READ_END:
			end_acquisition(sdi);
			break;
		default:
			sr_err("Unexpected device state %d.", devc->state);
			break;
		}
	}
}

/* USB input transfer completion callback.
 */
static void receive_transfer_in(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct acquisition_state *acq;

	sdi  = transfer->user_data;
	devc = sdi->priv;
	acq  = devc->acquisition;

	if (transfer->status != LIBUSB_TRANSFER_COMPLETED) {
		sr_err("Transfer from device failed: %d.", transfer->status);
		devc->transfer_error = TRUE;
		return;
	}

	switch (devc->state) {
	case STATE_STATUS_RESPONSE:
		process_capture_status(sdi);
		break;
	case STATE_LENGTH_RESPONSE:
		process_capture_length(sdi);
		break;
	case STATE_READ_RESPONSE:
		if (process_sample_data(sdi) == SR_OK
				&& acq->mem_addr_next < acq->mem_addr_stop
				&& acq->samples_done < acq->samples_max)
			request_read_mem(sdi);
		else
			issue_read_end(sdi);
		break;
	default:
		sr_err("Unexpected device state %d.", devc->state);
		break;
	}
}

/* Initialize the LWLA.  This downloads a bitstream into the FPGA
 * and executes a simple device test sequence.
 */
SR_PRIV int lwla_init_device(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	uint32_t value;

	devc = sdi->priv;

	/* Select internal clock if it hasn't been set yet */
	if (devc->selected_clock_source == CLOCK_SOURCE_NONE)
		devc->selected_clock_source = CLOCK_SOURCE_INT;

	/* Force reload of bitstream */
	devc->cur_clock_source = CLOCK_SOURCE_NONE;

	ret = lwla_set_clock_source(sdi);

	if (ret != SR_OK)
		return ret;

	ret = lwla_write_reg(sdi->conn, REG_CMD_CTRL2, 100);
	if (ret != SR_OK)
		return ret;

	ret = lwla_read_reg(sdi->conn, REG_CMD_CTRL1, &value);
	if (ret != SR_OK)
		return ret;
	sr_dbg("Received test word 0x%08X back.", value);
	if (value != 0x12345678)
		return SR_ERR;

	ret = lwla_read_reg(sdi->conn, REG_CMD_CTRL4, &value);
	if (ret != SR_OK)
		return ret;
	sr_dbg("Received test word 0x%08X back.", value);
	if (value != 0x12345678)
		return SR_ERR;

	ret = lwla_read_reg(sdi->conn, REG_CMD_CTRL3, &value);
	if (ret != SR_OK)
		return ret;
	sr_dbg("Received test word 0x%08X back.", value);
	if (value != 0x87654321)
		return SR_ERR;

	return ret;
}

/* Select the LWLA clock source.  If the clock source changed from the
 * previous setting, this will download a new bitstream to the FPGA.
 */
SR_PRIV int lwla_set_clock_source(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	enum clock_source selected;
	size_t idx;

	devc = sdi->priv;
	selected = devc->selected_clock_source;

	if (devc->cur_clock_source != selected) {
		devc->cur_clock_source = CLOCK_SOURCE_NONE;
		idx = selected;
		if (idx >= G_N_ELEMENTS(bitstream_map)) {
			sr_err("Clock source (%d) out of range", selected);
			return SR_ERR_BUG;
		}
		ret = lwla_send_bitstream(sdi->conn, bitstream_map[idx]);
		if (ret == SR_OK)
			devc->cur_clock_source = selected;
		return ret;
	}
	return SR_OK;
}

/* Configure the LWLA in preparation for an acquisition session.
 */
SR_PRIV int lwla_setup_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct acquisition_state *acq;
	struct regval_pair regvals[7];
	int ret;

	devc = sdi->priv;
	usb  = sdi->conn;
	acq  = devc->acquisition;

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

	switch (devc->cur_clock_source) {
	case CLOCK_SOURCE_INT:
		sr_info("Internal clock, samplerate %" PRIu64 ".",
			devc->samplerate);
		if (devc->samplerate == 0)
			return SR_ERR_BUG;
		/* At 125 MHz, the clock divider is bypassed. */
		acq->bypass_clockdiv = (devc->samplerate > SR_MHZ(100));

		/* If only one of the limits is set, derive the other one. */
		if (devc->limit_msec == 0 && devc->limit_samples > 0)
			acq->duration_max = devc->limit_samples
					* 1000 / devc->samplerate + 1;
		else if (devc->limit_samples == 0 && devc->limit_msec > 0)
			acq->samples_max = devc->limit_msec
					* devc->samplerate / 1000;
		break;
	case CLOCK_SOURCE_EXT_FALL:
		sr_info("External clock, falling edge.");
		acq->bypass_clockdiv = TRUE;
		break;
	case CLOCK_SOURCE_EXT_RISE:
		sr_info("External clock, rising edge.");
		acq->bypass_clockdiv = TRUE;
		break;
	default:
		sr_err("No valid clock source has been configured.");
		return SR_ERR;
	}

	regvals[0].reg = REG_MEM_CTRL2;
	regvals[0].val = 2;

	regvals[1].reg = REG_MEM_CTRL2;
	regvals[1].val = 1;

	regvals[2].reg = REG_CMD_CTRL2;
	regvals[2].val = 10;

	regvals[3].reg = REG_CMD_CTRL3;
	regvals[3].val = 0x74;

	regvals[4].reg = REG_CMD_CTRL4;
	regvals[4].val = 0;

	regvals[5].reg = REG_CMD_CTRL1;
	regvals[5].val = 0;

	regvals[6].reg = REG_DIV_BYPASS;
	regvals[6].val = acq->bypass_clockdiv;

	ret = lwla_write_regs(usb, regvals, G_N_ELEMENTS(regvals));
	if (ret != SR_OK)
		return ret;

	return capture_setup(sdi);
}

/* Start the capture operation on the LWLA device.  Beginning with this
 * function, all USB transfers will be asynchronous until the end of the
 * acquisition session.
 */
SR_PRIV int lwla_start_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct acquisition_state *acq;
	struct regval_pair *regvals;

	devc = sdi->priv;
	usb  = sdi->conn;
	acq  = devc->acquisition;

	acq->duration_now  = 0;
	acq->mem_addr_fill = 0;
	acq->capture_flags = 0;

	libusb_fill_bulk_transfer(acq->xfer_out, usb->devhdl, EP_COMMAND,
				  (unsigned char *)acq->xfer_buf_out, 0,
				  &receive_transfer_out,
				  (struct sr_dev_inst *)sdi, USB_TIMEOUT);

	libusb_fill_bulk_transfer(acq->xfer_in, usb->devhdl, EP_REPLY,
				  (unsigned char *)acq->xfer_buf_in,
				  sizeof acq->xfer_buf_in,
				  &receive_transfer_in,
				  (struct sr_dev_inst *)sdi, USB_TIMEOUT);

	regvals = devc->reg_write_seq;

	regvals[0].reg = REG_CMD_CTRL2;
	regvals[0].val = 10;

	regvals[1].reg = REG_CMD_CTRL3;
	regvals[1].val = 1;

	regvals[2].reg = REG_CMD_CTRL4;
	regvals[2].val = 0;

	regvals[3].reg = REG_CMD_CTRL1;
	regvals[3].val = 0;

	devc->reg_write_pos = 0;
	devc->reg_write_len = 4;

	devc->state = STATE_START_CAPTURE;

	return issue_next_write_reg(sdi);
}

/* Allocate an acquisition state object.
 */
SR_PRIV struct acquisition_state *lwla_alloc_acquisition_state(void)
{
	struct acquisition_state *acq;

	acq = g_try_new0(struct acquisition_state, 1);
	if (!acq) {
		sr_err("Acquisition state malloc failed.");
		return NULL;
	}

	acq->xfer_in = libusb_alloc_transfer(0);
	if (!acq->xfer_in) {
		sr_err("Transfer malloc failed.");
		g_free(acq);
		return NULL;
	}

	acq->xfer_out = libusb_alloc_transfer(0);
	if (!acq->xfer_out) {
		sr_err("Transfer malloc failed.");
		libusb_free_transfer(acq->xfer_in);
		g_free(acq);
		return NULL;
	}

	return acq;
}

/* Deallocate an acquisition state object.
 */
SR_PRIV void lwla_free_acquisition_state(struct acquisition_state *acq)
{
	if (acq) {
		libusb_free_transfer(acq->xfer_out);
		libusb_free_transfer(acq->xfer_in);
		g_free(acq);
	}
}

/* USB I/O source callback.
 */
SR_PRIV int lwla_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct timeval tv;
	int ret;

	(void)fd;

	sdi  = cb_data;
	devc = sdi->priv;
	drvc = sdi->driver->priv;

	if (!devc || !drvc)
		return FALSE;

	/* No timeout: return immediately. */
	tv.tv_sec  = 0;
	tv.tv_usec = 0;

	ret = libusb_handle_events_timeout_completed(drvc->sr_ctx->libusb_ctx,
						     &tv, NULL);
	if (ret != 0)
		sr_err("Event handling failed: %s.", libusb_error_name(ret));

	/* If no event flags are set the timeout must have expired. */
	if (revents == 0 && devc->state == STATE_STATUS_WAIT) {
		if (sdi->status == SR_ST_STOPPING)
			issue_stop_capture(sdi);
		else
			request_capture_status(sdi);
	}

	/* Check if an error occurred on a transfer. */
	if (devc->transfer_error)
		end_acquisition(sdi);

	return TRUE;
}
