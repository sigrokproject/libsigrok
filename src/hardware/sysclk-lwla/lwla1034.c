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
#define NUM_CHANNELS	34

/* Bit mask covering all logic channels.
 */
#define ALL_CHANNELS_MASK	((UINT64_C(1) << NUM_CHANNELS) - 1)

/* Unit size for the sigrok logic datafeed.
 */
#define UNIT_SIZE	((NUM_CHANNELS + 7) / 8)

/* Size of the acquisition buffer in device memory units.
 */
#define MEMORY_DEPTH	(256 * 1024)	/* 256k x 36 bit */

/* Capture memory read start address.
 */
#define READ_START_ADDR	4

/* Number of device memory units (36 bit) to read at a time. Slices of 8
 * consecutive 36-bit words are mapped to 9 32-bit words each, so the chunk
 * length should be a multiple of 8 to ensure alignment to slice boundaries.
 *
 * Experimentation has shown that reading chunks larger than about 1024 bytes
 * is unreliable. The threshold seems to relate to the buffer size on the FX2
 * USB chip: The configured endpoint buffer size is 512, and with double or
 * triple buffering enabled a multiple of 512 bytes can be kept in fly.
 *
 * The vendor software limits reads to 120 words (15 slices, 540 bytes) at
 * a time. So far, it appears safe to increase this to 224 words (28 slices,
 * 1008 bytes), thus making the most of two 512 byte buffers.
 */
#define READ_CHUNK_LEN	(28 * 8)

/* Bit mask for the RLE repeat-count-follows flag.
 */
#define RLE_FLAG_LEN_FOLLOWS	(UINT64_C(1) << 35)

/* Start index and count for bulk long register reads.
 * The first five long registers do not return useful values when read,
 * so skip over them to reduce the transfer size of status poll responses.
 */
#define READ_LREGS_START	LREG_MEM_FILL
#define READ_LREGS_COUNT	(LREG_STATUS + 1 - READ_LREGS_START)

/** LWLA1034 register addresses.
 */
enum reg_addr {
	REG_MEM_CTRL	= 0x1074, /* capture buffer control */
	REG_MEM_FILL	= 0x1078, /* capture buffer fill level */
	REG_MEM_START	= 0x107C, /* capture buffer start address */

	REG_CLK_BOOST	= 0x1094, /* logic clock boost flag */

	REG_LONG_STROBE	= 0x10B0, /* long register read/write strobe */
	REG_LONG_ADDR	= 0x10B4, /* long register address */
	REG_LONG_LOW	= 0x10B8, /* long register low word */
	REG_LONG_HIGH	= 0x10BC, /* long register high word */
};

/** Flag bits for REG_MEM_CTRL.
 */
enum mem_ctrl_flag {
	MEM_CTRL_WRITE   = 1 << 0, /* "wr1rd0" bit */
	MEM_CTRL_CLR_IDX = 1 << 1, /* "clr_idx" bit */
};

/* LWLA1034 long register addresses.
 */
enum long_reg_addr {
	LREG_CHAN_MASK	= 0,	/* channel enable mask */
	LREG_DIV_COUNT	= 1,	/* clock divider max count */
	LREG_TRG_VALUE	= 2,	/* trigger level/slope bits */
	LREG_TRG_TYPE	= 3,	/* trigger type bits (level or edge) */
	LREG_TRG_ENABLE	= 4,	/* trigger enable mask */
	LREG_MEM_FILL	= 5,	/* capture memory fill level or limit */

	LREG_DURATION	= 7,	/* elapsed time in ms (0.8 ms at 125 MS/s) */
	LREG_CHAN_STATE	= 8,	/* current logic levels at the inputs */
	LREG_STATUS	= 9,	/* capture status flags */

	LREG_CAP_CTRL	= 10,	/* capture control bits */
	LREG_TEST_ID	= 100,	/* constant test ID */
};

/** Flag bits for LREG_CAP_CTRL.
 */
enum cap_ctrl_flag {
	CAP_CTRL_TRG_EN       = 1 << 0, /* "trg_en" bit */
	CAP_CTRL_CLR_TIMEBASE = 1 << 2, /* "do_clr_timebase" bit */
	CAP_CTRL_FLUSH_FIFO   = 1 << 4, /* "flush_fifo" bit */
	CAP_CTRL_CLR_FIFOFULL = 1 << 5, /* "clr_fifo32_ful" bit */
	CAP_CTRL_CLR_COUNTER  = 1 << 6, /* "clr_cntr0" bit */
};

/* Available FPGA configurations.
 */
enum fpga_config {
	FPGA_OFF = 0,	/* FPGA shutdown config */
	FPGA_INT,	/* internal clock config */
	FPGA_EXTPOS,	/* external clock, rising edge config */
	FPGA_EXTNEG,	/* external clock, falling edge config */
};

/* FPGA bitstream resource filenames.
 */
static const char bitstream_map[][32] = {
	[FPGA_OFF]	= "sysclk-lwla1034-off.rbf",
	[FPGA_INT]	= "sysclk-lwla1034-int.rbf",
	[FPGA_EXTPOS]	= "sysclk-lwla1034-extpos.rbf",
	[FPGA_EXTNEG]	= "sysclk-lwla1034-extneg.rbf",
};

/* Read 64-bit long register.
 */
static int read_long_reg(const struct sr_usb_dev_inst *usb,
			 uint32_t addr, uint64_t *value)
{
	uint32_t low, high, dummy;
	int ret;

	ret = lwla_write_reg(usb, REG_LONG_ADDR, addr);
	if (ret != SR_OK)
		return ret;

	ret = lwla_read_reg(usb, REG_LONG_STROBE, &dummy);
	if (ret != SR_OK)
		return ret;

	ret = lwla_read_reg(usb, REG_LONG_HIGH, &high);
	if (ret != SR_OK)
		return ret;

	ret = lwla_read_reg(usb, REG_LONG_LOW, &low);
	if (ret != SR_OK)
		return ret;

	*value = ((uint64_t)high << 32) | low;

	return SR_OK;
}

/* Queue access sequence for a long register write.
 */
static void queue_long_regval(struct acquisition_state *acq,
			      uint32_t addr, uint64_t value)
{
	lwla_queue_regval(acq, REG_LONG_ADDR, addr);
	lwla_queue_regval(acq, REG_LONG_LOW, value & 0xFFFFFFFF);
	lwla_queue_regval(acq, REG_LONG_HIGH, value >> 32);
	lwla_queue_regval(acq, REG_LONG_STROBE, 0);
}

/* Helper to fill in the long register bulk write command.
 */
static inline void bulk_long_set(struct acquisition_state *acq,
				 unsigned int idx, uint64_t value)
{
	acq->xfer_buf_out[4 * idx + 3] = LWLA_WORD_0(value);
	acq->xfer_buf_out[4 * idx + 4] = LWLA_WORD_1(value);
	acq->xfer_buf_out[4 * idx + 5] = LWLA_WORD_2(value);
	acq->xfer_buf_out[4 * idx + 6] = LWLA_WORD_3(value);
}

/* Helper for dissecting the response to a long register bulk read.
 */
static inline uint64_t bulk_long_get(const struct acquisition_state *acq,
				     unsigned int idx)
{
	uint64_t low, high;

	low  = LWLA_TO_UINT32(acq->xfer_buf_in[2 * (idx - READ_LREGS_START)]);
	high = LWLA_TO_UINT32(acq->xfer_buf_in[2 * (idx - READ_LREGS_START) + 1]);

	return (high << 32) | low;
}

/* Demangle and decompress incoming sample data from the transfer buffer.
 * The data chunk is taken from the acquisition state, and is expected to
 * contain a multiple of 8 packed 36-bit words.
 */
static void read_response(struct acquisition_state *acq)
{
	uint64_t sample, high_nibbles, word;
	uint32_t *slice;
	uint8_t *out_p;
	unsigned int words_left;
	unsigned int max_samples, run_samples;
	unsigned int wi, ri, si;

	/* Number of 36-bit words remaining in the transfer buffer. */
	words_left = MIN(acq->mem_addr_next, acq->mem_addr_stop)
			- acq->mem_addr_done;

	for (wi = 0;; wi++) {
		/* Calculate number of samples to write into packet. */
		max_samples = MIN(acq->samples_max - acq->samples_done,
				  PACKET_SIZE / UNIT_SIZE - acq->out_index);
		run_samples = MIN(max_samples, acq->run_len);

		/* Expand run-length samples into session packet. */
		sample = acq->sample;
		out_p = &acq->out_packet[acq->out_index * UNIT_SIZE];

		for (ri = 0; ri < run_samples; ri++) {
			out_p[0] =  sample        & 0xFF;
			out_p[1] = (sample >>  8) & 0xFF;
			out_p[2] = (sample >> 16) & 0xFF;
			out_p[3] = (sample >> 24) & 0xFF;
			out_p[4] = (sample >> 32) & 0xFF;
			out_p += UNIT_SIZE;
		}
		acq->run_len -= run_samples;
		acq->out_index += run_samples;
		acq->samples_done += run_samples;

		if (run_samples == max_samples)
			break; /* packet full or sample limit reached */
		if (wi >= words_left)
			break; /* done with current transfer */

		/* Get the current slice of 8 packed 36-bit words. */
		slice = &acq->xfer_buf_in[(acq->in_index + wi) / 8 * 9];
		si = (acq->in_index + wi) % 8; /* word index within slice */

		/* Extract the next 36-bit word. */
		high_nibbles = LWLA_TO_UINT32(slice[8]);
		word = LWLA_TO_UINT32(slice[si]);
		word |= (high_nibbles << (4 * si + 4)) & (UINT64_C(0xF) << 32);

		if (acq->rle == RLE_STATE_DATA) {
			acq->sample = word & ALL_CHANNELS_MASK;
			acq->run_len = ((word >> NUM_CHANNELS) & 1) + 1;
			acq->rle = ((word & RLE_FLAG_LEN_FOLLOWS) != 0)
					? RLE_STATE_LEN : RLE_STATE_DATA;
		} else {
			acq->run_len += word << 1;
			acq->rle = RLE_STATE_DATA;
		}
	}
	acq->in_index += wi;
	acq->mem_addr_done += wi;
}

/* Check whether we can receive responses of more than 64 bytes.
 * The FX2 firmware of the LWLA1034 has a bug in the reset logic which
 * sometimes causes the response endpoint to be limited to transfers of
 * 64 bytes at a time, instead of the expected 2*512 bytes. The problem
 * can be worked around by never requesting more than 64 bytes.
 * This quirk manifests itself only under certain conditions, and some
 * users seem to see it more frequently than others. Detect it here in
 * order to avoid paying the penalty unnecessarily.
 */
static int detect_short_transfer_quirk(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int xfer_len;
	int ret;
	uint16_t command[3];
	unsigned char buf[512];

	const int lreg_count = 10;

	devc = sdi->priv;
	usb  = sdi->conn;

	command[0] = LWLA_WORD(CMD_READ_LREGS);
	command[1] = LWLA_WORD(0);
	command[2] = LWLA_WORD(lreg_count);

	ret = lwla_send_command(usb, command, ARRAY_SIZE(command));
	if (ret != SR_OK)
		return ret;

	ret = lwla_receive_reply(usb, buf, sizeof(buf), &xfer_len);
	if (ret != SR_OK)
		return ret;

	devc->short_transfer_quirk = (xfer_len == 64);

	if (xfer_len == 8 * lreg_count)
		return SR_OK;

	if (xfer_len == 64) {
		/* Drain the tailing portion of the split transfer. */
		ret = lwla_receive_reply(usb, buf, sizeof(buf), &xfer_len);
		if (ret != SR_OK)
			return ret;

		if (xfer_len == 8 * lreg_count - 64)
			return SR_OK;
	}
	sr_err("Received response of unexpected length %d.", xfer_len);

	return SR_ERR;
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
		config = FPGA_OFF;
	else if (devc->cfg_clock_source == CLOCK_INTERNAL)
		config = FPGA_INT;
	else if (devc->cfg_clock_edge == EDGE_POSITIVE)
		config = FPGA_EXTPOS;
	else
		config = FPGA_EXTNEG;

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
	uint64_t value;
	int ret;

	read_long_reg(sdi->conn, LREG_TEST_ID, &value);

	/* Ignore the value returned by the first read. */
	ret = read_long_reg(sdi->conn, LREG_TEST_ID, &value);
	if (ret != SR_OK)
		return ret;

	if (value != UINT64_C(0x1234567887654321)) {
		sr_err("Received invalid test word 0x%016" PRIX64 ".", value);
		return SR_ERR;
	}

	return detect_short_transfer_quirk(sdi);
}

/* Set up the device in preparation for an acquisition session.
 */
static int setup_acquisition(const struct sr_dev_inst *sdi)
{
	static const struct regval capture_init[] = {
		{REG_MEM_CTRL,    MEM_CTRL_CLR_IDX},
		{REG_MEM_CTRL,    MEM_CTRL_WRITE},
		{REG_LONG_ADDR,   LREG_CAP_CTRL},
		{REG_LONG_LOW,    CAP_CTRL_CLR_TIMEBASE | CAP_CTRL_FLUSH_FIFO |
				  CAP_CTRL_CLR_FIFOFULL | CAP_CTRL_CLR_COUNTER},
		{REG_LONG_HIGH,   0},
		{REG_LONG_STROBE, 0},
	};
	uint64_t divider_count;
	uint64_t trigger_mask;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct acquisition_state *acq;
	int ret;

	devc = sdi->priv;
	usb  = sdi->conn;
	acq  = devc->acquisition;

	ret = lwla_write_regs(usb, capture_init, ARRAY_SIZE(capture_init));
	if (ret != SR_OK)
		return ret;

	ret = lwla_write_reg(usb, REG_CLK_BOOST, acq->clock_boost);
	if (ret != SR_OK)
		return ret;

	acq->xfer_buf_out[0] = LWLA_WORD(CMD_WRITE_LREGS);
	acq->xfer_buf_out[1] = LWLA_WORD(0);
	acq->xfer_buf_out[2] = LWLA_WORD(LREG_STATUS + 1);

	bulk_long_set(acq, LREG_CHAN_MASK, devc->channel_mask);

	if (devc->samplerate > 0 && devc->samplerate <= SR_MHZ(100)
			&& !acq->clock_boost)
		divider_count = SR_MHZ(100) / devc->samplerate - 1;
	else
		divider_count = 0;

	bulk_long_set(acq, LREG_DIV_COUNT, divider_count);
	bulk_long_set(acq, LREG_TRG_VALUE, devc->trigger_values);
	bulk_long_set(acq, LREG_TRG_TYPE,  devc->trigger_edge_mask);

	trigger_mask = devc->trigger_mask;

	/* Set bits to select external TRG input edge. */
	if (devc->cfg_trigger_source == TRIGGER_EXT_TRG)
		switch (devc->cfg_trigger_slope) {
		case EDGE_POSITIVE:
			trigger_mask |= UINT64_C(1) << 35;
			break;
		case EDGE_NEGATIVE:
			trigger_mask |= UINT64_C(1) << 34;
			break;
		}

	bulk_long_set(acq, LREG_TRG_ENABLE, trigger_mask);

	/* Set the capture memory full threshold. This is slightly less
	 * than the actual maximum, most likely in order to compensate for
	 * pipeline latency.
	 */
	bulk_long_set(acq, LREG_MEM_FILL, MEMORY_DEPTH - 16);

	/* Fill remaining words with zeroes. */
	bulk_long_set(acq, 6, 0);
	bulk_long_set(acq, LREG_DURATION, 0);
	bulk_long_set(acq, LREG_CHAN_STATE, 0);
	bulk_long_set(acq, LREG_STATUS, 0);

	return lwla_send_command(sdi->conn, acq->xfer_buf_out,
				 3 + (LREG_STATUS + 1) * 4);
}

static int prepare_request(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct acquisition_state *acq;
	unsigned int chunk_len, remaining, count;

	devc = sdi->priv;
	acq  = devc->acquisition;

	acq->xfer_out->length = 0;
	acq->reg_seq_pos = 0;
	acq->reg_seq_len = 0;

	switch (devc->state) {
	case STATE_START_CAPTURE:
		queue_long_regval(acq, LREG_CAP_CTRL, CAP_CTRL_TRG_EN);
		break;
	case STATE_STOP_CAPTURE:
		queue_long_regval(acq, LREG_CAP_CTRL, 0);
		lwla_queue_regval(acq, REG_CLK_BOOST, 0);
		break;
	case STATE_READ_PREPARE:
		lwla_queue_regval(acq, REG_CLK_BOOST, 1);
		lwla_queue_regval(acq, REG_MEM_CTRL, MEM_CTRL_CLR_IDX);
		lwla_queue_regval(acq, REG_MEM_START, READ_START_ADDR);
		break;
	case STATE_READ_FINISH:
		lwla_queue_regval(acq, REG_CLK_BOOST, 0);
		break;
	case STATE_STATUS_REQUEST:
		acq->xfer_buf_out[0] = LWLA_WORD(CMD_READ_LREGS);
		acq->xfer_buf_out[1] = LWLA_WORD(READ_LREGS_START);
		acq->xfer_buf_out[2] = LWLA_WORD(READ_LREGS_COUNT);
		acq->xfer_out->length = 3 * sizeof(acq->xfer_buf_out[0]);
		break;
	case STATE_LENGTH_REQUEST:
		lwla_queue_regval(acq, REG_MEM_FILL, 0);
		break;
	case STATE_READ_REQUEST:
		/* Limit reads to 8 device words (36 bytes) at a time if the
		 * device firmware has the short transfer quirk. */
		chunk_len = (devc->short_transfer_quirk) ? 8 : READ_CHUNK_LEN;
		/* Always read a multiple of 8 device words. */
		remaining = (acq->mem_addr_stop - acq->mem_addr_next + 7) / 8 * 8;
		count = MIN(chunk_len, remaining);

		acq->xfer_buf_out[0] = LWLA_WORD(CMD_READ_MEM36);
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
		if (acq->xfer_in->actual_length != READ_LREGS_COUNT * 8) {
			sr_err("Received size %d doesn't match expected size %d.",
			       acq->xfer_in->actual_length, READ_LREGS_COUNT * 8);
			return SR_ERR;
		}
		acq->mem_addr_fill = bulk_long_get(acq, LREG_MEM_FILL) & 0xFFFFFFFF;
		acq->duration_now  = bulk_long_get(acq, LREG_DURATION);
		/* Shift left by one so the bit positions match the LWLA1016. */
		acq->status = (bulk_long_get(acq, LREG_STATUS) & 0x3F) << 1;
		/*
		 * It seems that the 125 MS/s mode is implemented simply by
		 * running the FPGA logic at a 25% higher clock rate. As a
		 * result, the millisecond counter for the capture duration
		 * is also off by 25%, and thus needs to be corrected here.
		 */
		if (acq->clock_boost)
			acq->duration_now = acq->duration_now * 4 / 5;
		break;
	case STATE_LENGTH_REQUEST:
		acq->mem_addr_next = READ_START_ADDR;
		acq->mem_addr_stop = acq->reg_sequence[0].val;
		break;
	case STATE_READ_REQUEST:
		/* Expect a multiple of 8 36-bit words packed into 9 32-bit
		 * words. */
		expect_len = (acq->mem_addr_next - acq->mem_addr_done
			+ acq->in_index + 7) / 8 * 9 * sizeof(acq->xfer_buf_in[0]);

		if (acq->xfer_in->actual_length != expect_len) {
			sr_err("Received size %d does not match expected size %d.",
			       acq->xfer_in->actual_length, expect_len);
			devc->transfer_error = TRUE;
			return SR_ERR;
		}
		read_response(acq);
		break;
	default:
		sr_err("BUG: unhandled response state %d.", devc->state);
		return SR_ERR_BUG;
	}

	return SR_OK;
}

/** Model descriptor for the LWLA1034.
 */
SR_PRIV const struct model_info lwla1034_info = {
	.name = "LWLA1034",
	.num_channels = NUM_CHANNELS,

	.num_devopts = 8,
	.devopts = {
		SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
		SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
		SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
		SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
		SR_CONF_EXTERNAL_CLOCK | SR_CONF_GET | SR_CONF_SET,
		SR_CONF_CLOCK_EDGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
		SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
		SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	},
	.num_samplerates = 20,
	.samplerates = {
		SR_MHZ(125), SR_MHZ(100),
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
