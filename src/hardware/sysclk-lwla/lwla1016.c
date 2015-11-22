/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Daniel Elstner <daniel.kitta@gmail.com>
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
#include "lwla.h"
#include "protocol.h"

/* Number of logic channels.
 */
#define NUM_CHANNELS	16

/* Unit size for the sigrok logic datafeed.
 */
#define UNIT_SIZE	((NUM_CHANNELS + 7) / 8)

/* Size of the acquisition buffer in device memory units.
 */
#define MEMORY_DEPTH	(256 * 1024)	/* 256k x 32 bit */

/* Capture memory read start address.
 */
#define READ_START_ADDR		2

/* Number of device memory units (32 bit) to read at a time.
 */
#define READ_CHUNK_LEN32	250

/** LWLA1016 register addresses.
 */
enum reg_addr {
	REG_CHAN_MASK	= 0x1000, /* bit mask of enabled channels */

	REG_DURATION	= 0x1010, /* capture duration in ms */

	REG_MEM_WR_PTR	= 0x1070,
	REG_MEM_RD_PTR	= 0x1074,
	REG_MEM_DATA	= 0x1078,
	REG_MEM_CTRL	= 0x107C,

	REG_CAP_COUNT	= 0x10B0,

	REG_TEST_ID	= 0x10B4, /* read */
	REG_TRG_SEL	= 0x10B4, /* write */

	REG_CAP_CTRL	= 0x10B8,

	REG_CAP_TOTAL	= 0x10BC, /* read */
	REG_DIV_COUNT	= 0x10BC, /* write */
};

/** Flag bits for REG_MEM_CTRL.
 */
enum mem_ctrl_flag {
	MEM_CTRL_RESET	= 1 << 0,
	MEM_CTRL_WRITE	= 1 << 1,
};

/** Flag bits for REG_CAP_CTRL.
 */
enum cap_ctrl_flag {
	CAP_CTRL_FIFO32_FULL	= 1 << 0, /* "fifo32_ful" bit */
	CAP_CTRL_FIFO64_FULL	= 1 << 1, /* "fifo64_ful" bit */
	CAP_CTRL_TRG_EN		= 1 << 2, /* "trg_en" bit */
	CAP_CTRL_CLR_TIMEBASE	= 1 << 3, /* "do_clr_timebase" bit */
	CAP_CTRL_FIFO_EMPTY	= 1 << 4, /* "fifo_empty" bit */
	CAP_CTRL_SAMPLE_EN	= 1 << 5, /* "sample_en" bit */
	CAP_CTRL_CNTR_NOT_ENDR	= 1 << 6, /* "cntr_not_endr" bit */
};

/* Available FPGA configurations.
 */
enum fpga_config {
	FPGA_100 = 0,	/* 100 MS/s, no compression */
	FPGA_100_TS,	/* 100 MS/s, timing-state mode */
};

/* FPGA bitstream resource filenames.
 */
static const char bitstream_map[][32] = {
	[FPGA_100]	= "sysclk-lwla1016-100.rbf",
	[FPGA_100_TS]	= "sysclk-lwla1016-100-ts.rbf",
};

/* Demangle incoming sample data from the transfer buffer.
 */
static void read_response(struct acquisition_state *acq)
{
	uint32_t *in_p, *out_p;
	size_t words_left, num_words;
	size_t max_samples, run_samples;
	size_t i;

	words_left = MIN(acq->mem_addr_next, acq->mem_addr_stop)
			- acq->mem_addr_done;
	/* Calculate number of samples to write into packet. */
	max_samples = MIN(acq->samples_max - acq->samples_done,
			  PACKET_SIZE / UNIT_SIZE - acq->out_index);
	run_samples = MIN(max_samples, 2 * words_left);

	/* Round up in case the samples limit is an odd number. */
	num_words = (run_samples + 1) / 2;
	/*
	 * Without RLE the output index will always be a multiple of two
	 * samples (at least before reaching the samples limit), thus 32-bit
	 * alignment is guaranteed.
	 */
	out_p = (uint32_t *)&acq->out_packet[acq->out_index * UNIT_SIZE];
	in_p  = &acq->xfer_buf_in[acq->in_index];
	/*
	 * Transfer two samples at a time, taking care to swap the 16-bit
	 * halves of each input word but keeping the samples themselves in
	 * the original Little Endian order.
	 */
	for (i = 0; i < num_words; i++)
		out_p[i] = LROTATE(in_p[i], 16);

	acq->in_index += num_words;
	acq->mem_addr_done += num_words;
	acq->out_index += run_samples;
	acq->samples_done += run_samples;
}

/* Demangle and decompress incoming sample data from the transfer buffer.
 */
static void read_response_rle(struct acquisition_state *acq)
{
	uint32_t *in_p;
	uint16_t *out_p;
	size_t words_left;
	size_t max_samples, run_samples;
	size_t wi, ri;
	uint32_t word;
	uint16_t sample;

	words_left = MIN(acq->mem_addr_next, acq->mem_addr_stop)
			- acq->mem_addr_done;
	in_p = &acq->xfer_buf_in[acq->in_index];

	for (wi = 0;; wi++) {
		/* Calculate number of samples to write into packet. */
		max_samples = MIN(acq->samples_max - acq->samples_done,
				  PACKET_SIZE / UNIT_SIZE - acq->out_index);
		run_samples = MIN(max_samples, acq->run_len);

		/* Expand run-length samples into session packet. */
		sample = acq->sample;
		out_p = &((uint16_t *)acq->out_packet)[acq->out_index];

		for (ri = 0; ri < run_samples; ri++)
			out_p[ri] = GUINT16_TO_LE(sample);

		acq->run_len -= run_samples;
		acq->out_index += run_samples;
		acq->samples_done += run_samples;

		if (run_samples == max_samples)
			break; /* packet full or sample limit reached */
		if (wi >= words_left)
			break; /* done with current transfer */

		word = GUINT32_FROM_LE(in_p[wi]);
		acq->sample = word >> 16;
		acq->run_len = (word & 0xFFFF) + 1;
	}
	acq->in_index += wi;
	acq->mem_addr_done += wi;
}

/* Select and transfer FPGA bitstream for the current configuration.
 */
static int apply_fpga_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct drv_context *drvc;
	int config;
	int ret;

	devc = sdi->priv;
	drvc = sdi->driver->context;

	if (sdi->status == SR_ST_INACTIVE)
		return SR_OK; /* the LWLA1016 has no off state */

	config = (devc->cfg_rle) ? FPGA_100_TS : FPGA_100;

	if (config == devc->active_fpga_config)
		return SR_OK; /* no change */

	ret = lwla_send_bitstream(drvc->sr_ctx, sdi->conn,
				  bitstream_map[config]);
	devc->active_fpga_config = (ret == SR_OK) ? config : FPGA_NOCONF;

	return ret;
}

/* Perform initialization self test.
 */
static int device_init_check(const struct sr_dev_inst *sdi)
{
	uint32_t value;
	int ret;

	ret = lwla_read_reg(sdi->conn, REG_TEST_ID, &value);
	if (ret != SR_OK)
		return ret;

	/* Ignore the value returned by the first read. */
	ret = lwla_read_reg(sdi->conn, REG_TEST_ID, &value);
	if (ret != SR_OK)
		return ret;

	if (value != 0x12345678) {
		sr_err("Received invalid test word 0x%08X.", value);
		return SR_ERR;
	}
	return SR_OK;
}

static int setup_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct acquisition_state *acq;
	uint32_t divider_count;
	int ret;

	devc = sdi->priv;
	usb  = sdi->conn;
	acq  = devc->acquisition;

	acq->reg_seq_pos = 0;
	acq->reg_seq_len = 0;

	lwla_queue_regval(acq, REG_CHAN_MASK, devc->channel_mask);

	if (devc->samplerate > 0 && devc->samplerate < SR_MHZ(100))
		divider_count = SR_MHZ(100) / devc->samplerate - 1;
	else
		divider_count = 0;

	lwla_queue_regval(acq, REG_DIV_COUNT, divider_count);

	lwla_queue_regval(acq, REG_CAP_CTRL, 0);
	lwla_queue_regval(acq, REG_DURATION, 0);

	lwla_queue_regval(acq, REG_MEM_CTRL, MEM_CTRL_RESET);
	lwla_queue_regval(acq, REG_MEM_CTRL, 0);
	lwla_queue_regval(acq, REG_MEM_CTRL, MEM_CTRL_WRITE);

	lwla_queue_regval(acq, REG_CAP_CTRL,
		CAP_CTRL_FIFO32_FULL | CAP_CTRL_FIFO64_FULL);

	lwla_queue_regval(acq, REG_CAP_CTRL, CAP_CTRL_FIFO_EMPTY);
	lwla_queue_regval(acq, REG_CAP_CTRL, 0);

	lwla_queue_regval(acq, REG_CAP_COUNT, MEMORY_DEPTH - 5);

	lwla_queue_regval(acq, REG_TRG_SEL,
		((devc->trigger_edge_mask & 0xFFFF) << 16)
		| (devc->trigger_values & 0xFFFF));

	ret = lwla_write_regs(usb, acq->reg_sequence, acq->reg_seq_len);
	acq->reg_seq_len = 0;

	return ret;
}

static int prepare_request(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct acquisition_state *acq;
	size_t count;

	devc = sdi->priv;
	acq  = devc->acquisition;

	acq->xfer_out->length = 0;
	acq->reg_seq_pos = 0;
	acq->reg_seq_len = 0;

	switch (devc->state) {
	case STATE_START_CAPTURE:
		lwla_queue_regval(acq, REG_CAP_CTRL, CAP_CTRL_TRG_EN
				| ((devc->trigger_mask & 0xFFFF) << 16));
		break;
	case STATE_STOP_CAPTURE:
		lwla_queue_regval(acq, REG_CAP_CTRL, 0);
		lwla_queue_regval(acq, REG_DIV_COUNT, 0);
		break;
	case STATE_READ_PREPARE:
		lwla_queue_regval(acq, REG_MEM_CTRL, 0);
		break;
	case STATE_READ_FINISH:
		lwla_queue_regval(acq, REG_MEM_CTRL, MEM_CTRL_RESET);
		lwla_queue_regval(acq, REG_MEM_CTRL, 0);
		break;
	case STATE_STATUS_REQUEST:
		lwla_queue_regval(acq, REG_CAP_CTRL, 0);
		lwla_queue_regval(acq, REG_MEM_WR_PTR, 0);
		lwla_queue_regval(acq, REG_DURATION, 0);
		break;
	case STATE_LENGTH_REQUEST:
		lwla_queue_regval(acq, REG_CAP_COUNT, 0);
		break;
	case STATE_READ_REQUEST:
		/* Always read a multiple of 8 device words. */
		count = MIN(READ_CHUNK_LEN32,
			    acq->mem_addr_stop - acq->mem_addr_next);

		acq->xfer_buf_out[0] = LWLA_WORD(CMD_READ_MEM32);
		acq->xfer_buf_out[1] = LWLA_WORD_0(acq->mem_addr_next);
		acq->xfer_buf_out[2] = LWLA_WORD_1(acq->mem_addr_next);
		acq->xfer_buf_out[3] = LWLA_WORD_0(count);
		acq->xfer_buf_out[4] = LWLA_WORD_1(count);
		acq->xfer_out->length = 5 * sizeof(acq->xfer_buf_out[0]);

		acq->mem_addr_next += count;
		break;
	default:
		sr_err("BUG: unhandled request state %d.", devc->state);
		return SR_ERR_BUG;
	}

	return SR_OK;
}

static int handle_response(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct acquisition_state *acq;
	int expect_len;

	devc = sdi->priv;
	acq  = devc->acquisition;

	switch (devc->state) {
	case STATE_STATUS_REQUEST:
		acq->status = acq->reg_sequence[0].val & 0x7F;
		acq->mem_addr_fill = acq->reg_sequence[1].val;
		acq->duration_now  = acq->reg_sequence[2].val;
		break;
	case STATE_LENGTH_REQUEST:
		acq->mem_addr_next = READ_START_ADDR;
		acq->mem_addr_stop = acq->reg_sequence[0].val + READ_START_ADDR - 1;
		break;
	case STATE_READ_REQUEST:
		expect_len = (acq->mem_addr_next - acq->mem_addr_done
				+ acq->in_index) * sizeof(acq->xfer_buf_in[0]);
		if (acq->xfer_in->actual_length != expect_len) {
			sr_err("Received size %d does not match expected size %d.",
			       acq->xfer_in->actual_length, expect_len);
			devc->transfer_error = TRUE;
			return SR_ERR;
		}
		if (acq->rle_enabled)
			read_response_rle(acq);
		else
			read_response(acq);
		break;
	default:
		sr_err("BUG: unhandled response state %d.", devc->state);
		return SR_ERR_BUG;
	}

	return SR_OK;
}

/* Model descriptor for the LWLA1016.
 */
SR_PRIV const struct model_info lwla1016_info = {
	.name = "LWLA1016",
	.num_channels = NUM_CHANNELS,

	.num_devopts = 5,
	.devopts = {
		SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
		SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
		SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
		SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
		SR_CONF_RLE | SR_CONF_GET | SR_CONF_SET,
	},
	.num_samplerates = 19,
	.samplerates = {
		SR_MHZ(100),
		SR_MHZ(50),  SR_MHZ(20),  SR_MHZ(10),
		SR_MHZ(5),   SR_MHZ(2),   SR_MHZ(1),
		SR_KHZ(500), SR_KHZ(200), SR_KHZ(100),
		SR_KHZ(50),  SR_KHZ(20),  SR_KHZ(10),
		SR_KHZ(5),   SR_KHZ(2),   SR_KHZ(1),
		SR_HZ(500),  SR_HZ(200),  SR_HZ(100),
	},

	.apply_fpga_config = &apply_fpga_config,
	.device_init_check = &device_init_check,
	.setup_acquisition = &setup_acquisition,

	.prepare_request = &prepare_request,
	.handle_response = &handle_response,
};
