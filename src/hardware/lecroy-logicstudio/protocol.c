/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Tilman Sauerbeck <tilman@code-monkey.de>
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
#include <assert.h>
#include "protocol.h"

#define EP_INTR (LIBUSB_ENDPOINT_IN | 1)
#define EP_BULK (LIBUSB_ENDPOINT_IN | 2)
#define EP_BITSTREAM (LIBUSB_ENDPOINT_OUT | 6)

#define CTRL_IN (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN)
#define CTRL_OUT (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT)

#define USB_COMMAND_READ_WRITE_REGS 0xb1
#define USB_COMMAND_WRITE_STATUS_REG 0xb2
#define USB_COMMAND_START_UPLOAD 0xb3
#define USB_COMMAND_VERIFY_UPLOAD 0xb4

#define USB_TIMEOUT_MS 100

/* Firmware for acquisition on 8 channels. */
#define FPGA_FIRMWARE_8 "lecroy-logicstudio16-8.bitstream"
/* Firmware for acquisition on 16 channels. */
#define FPGA_FIRMWARE_16 "lecroy-logicstudio16-16.bitstream"

#define FPGA_FIRMWARE_SIZE 464196
#define FPGA_FIRMWARE_CHUNK_SIZE 2048

#define NUM_TRIGGER_STAGES 2
#define TRIGGER_CFG_SIZE 45

#define ALIGN2_DOWN(n, p) ((((n) > (p)) ? ((n) - (p)) : (n)) & ~((p) - 1))

#define TRIGGER_OP_A 0x1000
#define TRIGGER_OP_B 0x2000
#define TRIGGER_OP_A_OR_B 0x3000
#define TRIGGER_OP_A_AND_B 0x4000
#define TRIGGER_OP_A_THEN_B 0x8000

#define REG_ACQUISITION_ID 0x00
#define REG_SAMPLERATE 0x02
#define REG_PRETRIG_LO 0x03
#define REG_PRETRIG_HI 0x04
#define REG_POSTTRIG_LO 0x05
#define REG_POSTTRIG_HI 0x06
#define REG_ARM_TRIGGER 0x07
#define REG_FETCH_SAMPLES 0x08
#define REG_UNK1_LO 0x09
#define REG_UNK1_HI 0x0a
#define REG_UNK2_LO 0x0b
#define REG_UNK2_HI 0x0c
#define REG_UNK3_LO 0x0d
#define REG_UNK3_HI 0x0e
#define REG_UNK4_LO 0x0f
#define REG_UNK4_HI 0x10
#define REG_UNK5_LO 0x11
#define REG_UNK5_HI 0x12
#define REG_UNK6_LO 0x13
#define REG_UNK6_HI 0x14
#define REG_UNK0_LO 0x15
#define REG_UNK0_HI 0x16
#define REG_TRIGGER_CFG 0x18
#define REG_TRIGGER_COMBINE_OP 0x1b
#define REG_SELECT_CHANNELS 0x21
#define REG_VOLTAGE_THRESH_EXTERNAL 0x22
#define REG_VOLTAGE_THRESH_LOWER_CHANNELS 0x23
#define REG_VOLTAGE_THRESH_UPPER_CHANNELS 0x24

struct samplerate_info {
	/** The samplerate in Hz. */
	uint64_t samplerate;

	/**
	 * The offset to add to the sample offset for when the trigger fired.
	 *
	 * @note The value stored here only applies to 8 channel mode.
	 *       When acquiring 16 channels, subtract another 8 samples.
	 */
	int8_t trigger_sample_offset;

	uint8_t cfg;
};

struct trigger_config {
	uint16_t rising_edges;
	uint16_t falling_edges;
	uint16_t any_edges;

	uint16_t ones;
	uint16_t zeroes;
};

/** A register and its value. */
struct regval {
	uint8_t reg;
	uint16_t val;
};

static void handle_fetch_samples_done(struct libusb_transfer *xfer);
static void recv_bulk_transfer(struct libusb_transfer *xfer);

static const struct samplerate_info samplerates[] = {
	{ SR_GHZ(1),  -24, 0x1f },
	{ SR_MHZ(500), -6, 0x00 },
	{ SR_MHZ(250), -4, 0x01 },
	{ SR_MHZ(100),  2, 0x03 },
	{ SR_MHZ(50),   4, 0x04 },
	{ SR_MHZ(25),   8, 0x05 },
	{ SR_MHZ(10),   4, 0x07 },
	{ SR_MHZ(5),    8, 0x08 },
	{ SR_KHZ(2500), 8, 0x09 },
	{ SR_KHZ(1000), 8, 0x0b },
	{ SR_KHZ(500),  8, 0x0c },
	{ SR_KHZ(250),  8, 0x0d },
	{ SR_KHZ(100),  8, 0x0f },
	{ SR_KHZ(50),   8, 0x10 },
	{ SR_KHZ(25),   8, 0x11 },
	{ SR_KHZ(10),   8, 0x13 },
	{ SR_KHZ(5),    8, 0x14 },
	{ SR_HZ(2500),  8, 0x15 },
	{ SR_HZ(1000),  8, 0x17 },
};

static int read_register(const struct sr_dev_inst *sdi,
	uint8_t reg, uint16_t *value)
{
	struct sr_usb_dev_inst *usb;
	uint8_t data[2];
	int r;

	usb = sdi->conn;

	r = libusb_control_transfer(usb->devhdl, CTRL_IN,
		USB_COMMAND_READ_WRITE_REGS, reg, 5444,
		data, sizeof(data), USB_TIMEOUT_MS);

	if (r != sizeof(data)) {
		sr_err("CTRL_IN failed: %i.", r);
		return SR_ERR;
	}

	*value = RB16(data);

	return SR_OK;
}

static int write_registers_sync(const struct sr_dev_inst *sdi,
	unsigned int wValue, unsigned int wIndex,
	const struct regval *regs, size_t num_regs)
{
	struct sr_usb_dev_inst *usb;
	uint8_t *buf;
	size_t i, bufsiz;
	int r;

	usb = sdi->conn;

	/* Try to avoid overflowing the stack. */
	if (num_regs > 32)
		return SR_ERR;

	bufsiz = num_regs * 3;
	buf = alloca(bufsiz);

	for (i = 0; i < num_regs; i++) {
		W8(&buf[i * 3 + 0], regs[i].reg);
		WB16(&buf[i * 3 + 1], regs[i].val);
	}

	r = libusb_control_transfer(usb->devhdl, CTRL_OUT,
			USB_COMMAND_READ_WRITE_REGS, wValue, wIndex,
			buf, bufsiz, USB_TIMEOUT_MS);

	if (r != (int) bufsiz) {
		sr_err("write_registers_sync(%u/%u) failed.", wValue, wIndex);
		return SR_ERR;
	}

	return SR_OK;
}

static int write_registers_async(const struct sr_dev_inst *sdi,
	unsigned int wValue, unsigned int wIndex,
	const struct regval *regs, size_t num_regs,
	libusb_transfer_cb_fn callback)
{
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *xfer;
	uint8_t *buf, *xfer_buf;
	size_t i;

	usb = sdi->conn;

	xfer = libusb_alloc_transfer(0);
	xfer_buf = g_malloc(LIBUSB_CONTROL_SETUP_SIZE + (num_regs * 3));

	libusb_fill_control_setup(xfer_buf, CTRL_OUT,
		USB_COMMAND_READ_WRITE_REGS, wValue, wIndex, num_regs * 3);

	buf = xfer_buf + LIBUSB_CONTROL_SETUP_SIZE;

	for (i = 0; i < num_regs; i++) {
		W8(&buf[i * 3 + 0], regs[i].reg);
		WB16(&buf[i * 3 + 1], regs[i].val);
	}

	libusb_fill_control_transfer(xfer, usb->devhdl,
		xfer_buf, callback, (void *) sdi, USB_TIMEOUT_MS);

	if (libusb_submit_transfer(xfer) < 0) {
		g_free(xfer->buffer);
		xfer->buffer = NULL;
		libusb_free_transfer(xfer);
		return SR_ERR;
	}

	return SR_OK;
}

static void prep_regw(struct regval *regval, uint8_t reg, uint16_t val)
{
	regval->reg = reg;
	regval->val = val;
}

static void handle_fetch_samples_done(struct libusb_transfer *xfer)
{
	const struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;

	sdi = xfer->user_data;
	usb = sdi->conn;
	devc = sdi->priv;

	g_free(xfer->buffer);
	xfer->buffer = NULL;

	libusb_free_transfer(xfer);

	libusb_fill_bulk_transfer(devc->bulk_xfer, usb->devhdl, EP_BULK,
		devc->fetched_samples, 17 << 10,
		recv_bulk_transfer, (void *)sdi, USB_TIMEOUT_MS);

	libusb_submit_transfer(devc->bulk_xfer);
}

static void calc_unk0(uint32_t *a, uint32_t *b)
{
	uint32_t t;

	t = 20000 / 4;

	if (a)
		*a = (t + 63) | 63;
	if (b)
		*b = (t + 63) & ~63;
}

static int fetch_samples_async(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct regval cmd[12];
	uint32_t unk1, unk2, unk3;
	int i;

	devc = sdi->priv;

	unk1 = devc->earliest_sample % (devc->num_thousand_samples << 10);
	unk1 = (unk1 * devc->num_enabled_channel_groups) / 8;

	calc_unk0(&unk2, &unk3);

	i = 0;

	prep_regw(&cmd[i++], REG_UNK1_LO, (unk1 >>  0) & 0xffff);
	prep_regw(&cmd[i++], REG_UNK1_HI, (unk1 >> 16) & 0xffff);

	prep_regw(&cmd[i++], REG_FETCH_SAMPLES, devc->magic_fetch_samples);
	prep_regw(&cmd[i++], REG_FETCH_SAMPLES, devc->magic_fetch_samples | 0x02);

	prep_regw(&cmd[i++], REG_UNK1_LO, 0x0000);
	prep_regw(&cmd[i++], REG_UNK1_HI, 0x0000);

	prep_regw(&cmd[i++], REG_UNK2_LO, (unk2 >>  0) & 0xffff);
	prep_regw(&cmd[i++], REG_UNK2_HI, (unk2 >> 16) & 0xffff);

	prep_regw(&cmd[i++], REG_UNK3_LO, (unk3 >>  0) & 0xffff);
	prep_regw(&cmd[i++], REG_UNK3_HI, (unk3 >> 16) & 0xffff);

	devc->magic_fetch_samples = 0x01;
	prep_regw(&cmd[i++], REG_FETCH_SAMPLES, devc->magic_fetch_samples + 0x01);
	prep_regw(&cmd[i++], REG_FETCH_SAMPLES, devc->magic_fetch_samples | 0x02);

	return write_registers_async(sdi, 0x12, 5444, cmd, ARRAY_SIZE(cmd),
			handle_fetch_samples_done);
}

static int handle_intr_data(const struct sr_dev_inst *sdi, uint8_t *buffer)
{
	struct dev_context *devc;
	gboolean resubmit_intr_xfer;
	uint64_t time_latest, time_trigger, sample_latest, sample_trigger;
	uint32_t samplerate_divider;

	resubmit_intr_xfer = TRUE;

	if (!sdi)
		goto out;

	devc = sdi->priv;

	if (!devc->want_trigger)
		goto out;

	/* Does this packet refer to our newly programmed trigger yet? */
	if (RB16(&buffer[0x02]) != devc->acquisition_id)
		goto out;

	switch (buffer[0x1f]) {
	case 0x09:
		/* Storing pre-trigger samples. */
		break;
	case 0x0a:
		/* Trigger armed? */
		break;
	case 0x0b:
		/* Storing post-trigger samples. */
		break;
	case 0x04:
		/* Acquisition complete. */
		devc->total_received_sample_bytes = 0;

		samplerate_divider = SR_GHZ(1) / devc->samplerate_info->samplerate;

		/*
		 * These timestamps seem to be in units of eight nanoseconds.
		 * The first one refers to the time when the latest sample
		 * was written to the device's sample buffer, and the second
		 * one refers to the time when the trigger fired.
		 *
		 * They are stored as 48 bit integers in the packet and we
		 * shift it to the right by 16 to make up for that.
		 */
		time_latest = RB64(&buffer[0x6]) >> 16;
		time_trigger = RB64(&buffer[0xc]) >> 16;

		/* Convert timestamps to sample offsets. */
		sample_latest = time_latest * 8;
		sample_latest /= samplerate_divider;

		sample_latest = ALIGN2_DOWN(sample_latest,
			8 / devc->num_enabled_channel_groups);

		devc->earliest_sample = sample_latest -
			(devc->num_thousand_samples * 1000);

		sample_trigger = time_trigger * 8;

		/* Fill the zero bits on the right. */
		sample_trigger |= RB16(&buffer[0x12]) & 7;

		sample_trigger += devc->samplerate_info->trigger_sample_offset;

		if (devc->num_enabled_channel_groups > 1)
			sample_trigger -= 8;

		sample_trigger -= 0x18;

		if (samplerate_divider > 1) {
			/* FIXME: Underflow. */
			sample_trigger -= samplerate_divider;
			sample_trigger /= samplerate_divider;
		}

		/*
		 * Seems the hardware reports one sample too early,
		 * so make up for that.
		 */
		sample_trigger++;

		devc->trigger_sample = sample_trigger;

		fetch_samples_async(sdi);

		/*
		 * Don't re-submit the interrupt transfer;
		 * we need to get the samples instead.
		 */
		resubmit_intr_xfer = FALSE;

		break;
	default:
		break;
	}

out:
	return resubmit_intr_xfer;
}

static int upload_fpga_bitstream(const struct sr_dev_inst *sdi,
	const char *firmware_name)
{
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct sr_resource firmware;
	uint8_t firmware_chunk[FPGA_FIRMWARE_CHUNK_SIZE];
	uint8_t upload_succeeded;
	ssize_t chunk_size;
	int i, r, ret, actual_length;

	drvc = sdi->driver->context;
	usb = sdi->conn;

	ret = sr_resource_open(drvc->sr_ctx, &firmware,
			SR_RESOURCE_FIRMWARE, firmware_name);

	if (ret != SR_OK)
		return ret;

	ret = SR_ERR;

	if (firmware.size != FPGA_FIRMWARE_SIZE) {
		sr_err("Invalid FPGA firmware file size: %" PRIu64 " bytes.",
			firmware.size);
		goto out;
	}

	/* Initiate upload. */
	r = libusb_control_transfer(usb->devhdl, CTRL_OUT,
		USB_COMMAND_START_UPLOAD, 0x07, 5444,
		NULL, 0, USB_TIMEOUT_MS);

	if (r != 0) {
		sr_err("Failed to initiate firmware upload: %s.",
				libusb_error_name(ret));
		goto out;
	}

	for (;;) {
		chunk_size = sr_resource_read(drvc->sr_ctx, &firmware,
			firmware_chunk, sizeof(firmware_chunk));

		if (chunk_size < 0)
			goto out;

		if (chunk_size == 0)
			break;

		actual_length = chunk_size;

		r = libusb_bulk_transfer(usb->devhdl, EP_BITSTREAM,
			firmware_chunk, chunk_size, &actual_length, USB_TIMEOUT_MS);

		if (r != 0 || (ssize_t)actual_length != chunk_size) {
			sr_err("FPGA firmware upload failed.");
			goto out;
		}
	}

	/* Verify upload. */
	for (i = 0; i < 4; i++) {
		g_usleep(250000);

		upload_succeeded = 0x00;

		r = libusb_control_transfer(usb->devhdl, CTRL_IN,
			USB_COMMAND_VERIFY_UPLOAD, 0x07, 5444,
			&upload_succeeded, sizeof(upload_succeeded),
			USB_TIMEOUT_MS);

		if (r != sizeof(upload_succeeded)) {
			sr_err("CTRL_IN failed: %i.", r);
			return SR_ERR;
		}

		if (upload_succeeded == 0x01) {
			ret = SR_OK;
			break;
		}
	}

out:
	sr_resource_close(drvc->sr_ctx, &firmware);

	return ret;
}

static int upload_trigger(const struct sr_dev_inst *sdi,
	uint8_t reg_values[TRIGGER_CFG_SIZE], uint8_t reg_offset)
{
	struct regval regs[3 * 5];
	uint16_t value;
	int i, j, k;

	for (i = 0; i < TRIGGER_CFG_SIZE; i += 5) {
		k = 0;

		for (j = 0; j < 5; j++) {
			value = ((reg_offset + i + j) << 8) | reg_values[i + j];

			prep_regw(&regs[k++], REG_TRIGGER_CFG, value);
			prep_regw(&regs[k++], REG_TRIGGER_CFG, value | 0x8000);
			prep_regw(&regs[k++], REG_TRIGGER_CFG, value);
		}

		if (write_registers_sync(sdi, 0x12, 5444, regs, ARRAY_SIZE(regs))) {
			sr_err("Failed to upload trigger config.");
			return SR_ERR;
		}
	}

	return SR_OK;
}

static int program_trigger(const struct sr_dev_inst *sdi,
	struct trigger_config *stages, int num_filled_stages)
{
	struct trigger_config *block;
	struct regval combine_op;
	uint8_t buf[TRIGGER_CFG_SIZE];
	const uint8_t reg_offsets[] = { 0x00, 0x40 };
	int i;

	for (i = 0; i < NUM_TRIGGER_STAGES; i++) {
		block = &stages[i];

		memset(buf, 0, sizeof(buf));

		WL16(&buf[0x00], ~(block->rising_edges | block->falling_edges));
		WL16(&buf[0x05], block->rising_edges | block->falling_edges | block->any_edges);

		if (block->ones | block->zeroes)
			buf[0x09] = 0x10;

		WL16(&buf[0x0a], block->rising_edges);
		WL16(&buf[0x0f], block->ones | block->zeroes);
		buf[0x13] = 0x10;
		WL16(&buf[0x14], block->ones | 0x8000);

		if (block->ones == 0x01)
			WL16(&buf[0x19], block->ones << 1);
		else
			WL16(&buf[0x19], block->ones | 0x0001);

		/*
		 * The final trigger has some special stuff.
		 * Not sure of the meaning yet.
		 */
		if (i == NUM_TRIGGER_STAGES - 1) {
			buf[0x09] = 0x10; /* This is most likely wrong. */

			buf[0x28] = 0xff;
			buf[0x29] = 0xff;
			buf[0x2a] = 0xff;
			buf[0x2b] = 0xff;
			buf[0x2c] = 0x80;
		}

		if (upload_trigger(sdi, buf, reg_offsets[i]) < 0)
			return SR_ERR;
	}

	/*
	 * If both available stages are used, AND them in the trigger
	 * criteria.
	 *
	 * Once sigrok learns to teach devices about the combination
	 * that the user wants, this seems to be the best default since
	 * edge triggers cannot be AND'ed otherwise
	 * (they are always OR'd within a single stage).
	 */
	prep_regw(&combine_op, REG_TRIGGER_COMBINE_OP,
		num_filled_stages > 1 ? TRIGGER_OP_A_AND_B : TRIGGER_OP_A);

	return write_registers_sync(sdi, 0x12, 5444, &combine_op, 1);
}

static gboolean transform_trigger(struct sr_trigger_stage *stage,
	struct trigger_config *config)
{
	GSList *l;
	struct sr_trigger_match *match;
	uint32_t channel_mask;
	gboolean ret;

	ret = FALSE;

	for (l = stage->matches; l; l = l->next) {
		match = l->data;

		if (!match)
			continue;

		/* Ignore disabled channels. */
		if (!match->channel->enabled)
			continue;

		channel_mask = 1 << match->channel->index;

		switch (match->match) {
		case SR_TRIGGER_RISING:
			config->rising_edges |= channel_mask;
			break;
		case SR_TRIGGER_FALLING:
			config->falling_edges |= channel_mask;
			break;
		case SR_TRIGGER_EDGE:
			config->any_edges |= channel_mask;
			break;
		case SR_TRIGGER_ONE:
			config->ones |= channel_mask;
			break;
		case SR_TRIGGER_ZERO:
			config->zeroes |= channel_mask;
			break;
		}

		ret = TRUE;
	}

	return ret;
}

static int configure_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	struct trigger_config blocks[NUM_TRIGGER_STAGES];
	gboolean stage_has_matches;
	int num_filled_stages;
	GSList *l, *ll;

	devc = sdi->priv;

	trigger = sr_session_trigger_get(sdi->session);

	num_filled_stages = 0;

	memset(blocks, 0, sizeof(blocks));

	for (l = trigger ? trigger->stages : NULL; l; l = l->next) {
		stage = l->data;
		stage_has_matches = FALSE;

		/* Check if this stage has any interesting matches. */
		for (ll = stage->matches; ll; ll = ll->next) {
			match = ll->data;

			if (!match)
				continue;

			/* Ignore disabled channels. */
			if (match->channel->enabled) {
				stage_has_matches = TRUE;
				break;
			}
		}

		if (stage_has_matches == FALSE)
			continue;

		if (num_filled_stages == NUM_TRIGGER_STAGES)
			return SR_ERR;

		if (transform_trigger(stage, &blocks[num_filled_stages]))
			num_filled_stages++;
	}

	devc->want_trigger = num_filled_stages > 0;

	return program_trigger(sdi, blocks, num_filled_stages);
}

/** Update the bit mask of enabled channels. */
SR_PRIV void lls_update_channel_mask(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_channel *channel;
	GSList *l;

	devc = sdi->priv;

	devc->channel_mask = 0;

	for (l = sdi->channels; l; l = l->next) {
		channel = l->data;
		if (channel->enabled)
			devc->channel_mask |= 1 << channel->index;
	}
}

SR_PRIV int lls_set_samplerate(const struct sr_dev_inst *sdi,
	uint64_t samplerate)
{
	struct dev_context *devc;
	size_t i;

	devc = sdi->priv;

	for (i = 0; i < ARRAY_SIZE(samplerates); i++) {
		if (samplerates[i].samplerate == samplerate) {
			devc->samplerate_info = &samplerates[i];
			return SR_OK;
		}
	}

	return SR_ERR;
}

SR_PRIV uint64_t lls_get_samplerate(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	return devc->samplerate_info->samplerate;
}

static int read_0f12(const struct sr_dev_inst *sdi, uint64_t *value)
{
	uint64_t u64;
	uint16_t u16;
	int r, reg;

	u64 = 0;

	/*
	 * Read the 64 bit register spread over 4 16 bit registers.
	 *
	 * Note that these don't seem to be the same registers we're writing
	 * when arming the trigger (ie REG_UNK4 and REG_UNK5).
	 * Seems there's multiple register spaces?
	 */
	for (reg = 0x0f; reg <= 0x12; reg++) {
		r = read_register(sdi, reg, &u16);
		if (r != SR_OK)
			return r;
		u64 <<= 16;
		u64 |= u16;
	}

	*value = u64;

	return SR_OK;
}

static int wait_for_dev_to_settle(const struct sr_dev_inst *sdi)
{
	uint64_t old_value, new_value;
	int r, i;

	/* Get the initial value. */
	r = read_0f12(sdi, &old_value);

	if (r != SR_OK)
		return r;

	/*
	 * We are looking for two consecutive reads that yield the
	 * same value. Try a couple of times.
	 */
	for (i = 0; i < 100; i++) {
		r = read_0f12(sdi, &new_value);
		if (r != SR_OK)
			return r;

		if (old_value == new_value)
			return SR_OK;

		old_value = new_value;
	}

	return SR_ERR;
}

SR_PRIV int lls_setup_acquisition(const struct sr_dev_inst *sdi)
{
	uint8_t status_reg_value[] = {
		0x1, 0x0, 0x7, 0x8, 0x9, 0xa, 0xb, 0xc,
		0xd, 0xe, 0x1, 0x2, 0x3, 0x4, 0x5, 0x6
	};
	struct regval threshold[3];
	struct regval channels;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	gboolean lower_enabled, upper_enabled, upload_bitstream;
	uint32_t num_thousand_samples, num_enabled_channel_groups;
	int i, r;

	usb = sdi->conn;
	devc = sdi->priv;

	prep_regw(&threshold[0], REG_VOLTAGE_THRESH_LOWER_CHANNELS, 0x00c3);
	prep_regw(&threshold[1], REG_VOLTAGE_THRESH_UPPER_CHANNELS, 0x00c2);
	prep_regw(&threshold[2], REG_VOLTAGE_THRESH_EXTERNAL, 0x003e);

	lls_update_channel_mask(sdi);

	lower_enabled = (devc->channel_mask & 0x00ff) != 0x00;
	upper_enabled = (devc->channel_mask & 0xff00) != 0x00;

	num_thousand_samples = 20;
	num_enabled_channel_groups = 2;

	if (lower_enabled != upper_enabled) {
		num_thousand_samples <<= 1;
		num_enabled_channel_groups >>= 1;
	}

	if (upper_enabled && !lower_enabled)
		prep_regw(&channels, REG_SELECT_CHANNELS, 0x01);
	else
		prep_regw(&channels, REG_SELECT_CHANNELS, 0x00);

	/*
	 * If the number of enabled channel groups changed since
	 * the last acquisition, we need to switch FPGA bitstreams.
	 * This works for the initial bitstream upload because
	 * devc->num_enabled_channel_groups is initialized to zero.
	 */
	upload_bitstream =
		devc->num_enabled_channel_groups != num_enabled_channel_groups;

	if (upload_bitstream) {
		if (lls_stop_acquisition(sdi)) {
			sr_err("Cannot stop acquisition for FPGA bitstream upload.");
			return SR_ERR;
		}

		for (i = 0; i < 3; i++)
			if (write_registers_sync(sdi, 0x0, 0x0, &threshold[i], 1))
				return SR_ERR;

		if (num_enabled_channel_groups == 1)
			r = upload_fpga_bitstream(sdi, FPGA_FIRMWARE_8);
		else
			r = upload_fpga_bitstream(sdi, FPGA_FIRMWARE_16);

		if (r != SR_OK) {
			sr_err("Firmware not accepted by device.");
			return SR_ERR;
		}

		r = wait_for_dev_to_settle(sdi);

		if (r != SR_OK) {
			sr_err("Device did not settle in time.");
			return SR_ERR;
		}

		for (i = 0; i < 3; i++)
			if (write_registers_sync(sdi, 0x12, 5444, &threshold[i], 1))
				return SR_ERR;

		devc->magic_arm_trigger = 0x00;
		devc->magic_fetch_samples = 0x00;
	}

	if (write_registers_sync(sdi, 0x12, 5444, &channels, 1))
		return SR_ERR;

	if (configure_trigger(sdi) < 0)
		return SR_ERR;

	if (write_registers_sync(sdi, 0x12, 5444, &threshold[0], 1))
		return SR_ERR;

	if (write_registers_sync(sdi, 0x12, 5444, &threshold[1], 1))
		return SR_ERR;

	if (upload_bitstream) {
		r = libusb_control_transfer(usb->devhdl, CTRL_OUT,
			USB_COMMAND_WRITE_STATUS_REG, 0x12, 5444,
			status_reg_value, sizeof(status_reg_value), USB_TIMEOUT_MS);

		if (r != sizeof(status_reg_value)) {
			sr_err("Failed to write status register: %s.",
				libusb_error_name(r));
			return SR_ERR;
		}
	}

	devc->num_thousand_samples = num_thousand_samples;
	devc->num_enabled_channel_groups = num_enabled_channel_groups;

	return SR_OK;
}

static void recv_intr_transfer(struct libusb_transfer *xfer)
{
	const struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_datafeed_packet packet;

	sdi = xfer->user_data;
	drvc = sdi->driver->context;
	devc = sdi->priv;

	if (devc->abort_acquisition) {
		packet.type = SR_DF_END;
		sr_session_send(sdi, &packet);
		usb_source_remove(sdi->session, drvc->sr_ctx);
		return;
	}

	if (xfer->status == LIBUSB_TRANSFER_COMPLETED) {
		if (xfer->actual_length != INTR_BUF_SIZE)
			sr_err("Invalid size of interrupt transfer: %u.",
				xfer->actual_length);
		else if (handle_intr_data(sdi, xfer->buffer)) {
			if (libusb_submit_transfer(xfer) < 0)
				sr_err("Failed to submit interrupt transfer.");
		}
	}
}

static void send_samples(const struct sr_dev_inst *sdi,
	uint8_t *samples, uint32_t length)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	gboolean lower_enabled, upper_enabled;
	uint32_t shift, i;

	devc = sdi->priv;

	lower_enabled = (devc->channel_mask & 0x00ff) != 0x00;
	upper_enabled = (devc->channel_mask & 0xff00) != 0x00;

	logic.unitsize = 2;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;

	if (lower_enabled && upper_enabled) {
		logic.length = length;
		logic.data = samples;

		sr_session_send(sdi, &packet);
	} else {
		/* Which channel group is enabled? */
		shift = (lower_enabled) ? 0 : 8;

		while (length >= (CONV_8TO16_BUF_SIZE / 2)) {
			for (i = 0; i < (CONV_8TO16_BUF_SIZE / 2); i++) {
				devc->conv8to16[i] = samples[i];
				devc->conv8to16[i] <<= shift;
			}

			logic.length = CONV_8TO16_BUF_SIZE;
			logic.data = devc->conv8to16;

			sr_session_send(sdi, &packet);

			samples += CONV_8TO16_BUF_SIZE / 2;
			length -= CONV_8TO16_BUF_SIZE / 2;
		}

		/* Handle the remaining samples. */
		for (i = 0; i < length; i++) {
			devc->conv8to16[i] = samples[i];
			devc->conv8to16[i] <<= shift;
		}

		logic.length = length * 2;
		logic.data = devc->conv8to16;

		sr_session_send(sdi, &packet);
	}
}

static uint16_t sample_to_byte_offset(struct dev_context *devc, uint64_t o)
{
	o %= devc->num_thousand_samples << 10;

	/* We have 8 bit per channel group, so this gets us a byte offset. */
	return o * devc->num_enabled_channel_groups;
}

static void recv_bulk_transfer(struct libusb_transfer *xfer)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_datafeed_packet packet;
	uint32_t bytes_left, length;
	uint16_t read_offset, trigger_offset;

	sdi = xfer->user_data;

	if (!sdi)
		return;

	drvc = sdi->driver->context;
	devc = sdi->priv;

	devc->total_received_sample_bytes += xfer->actual_length;

	if (devc->total_received_sample_bytes < SAMPLE_BUF_SIZE) {
		xfer->buffer = devc->fetched_samples
			+ devc->total_received_sample_bytes;

		xfer->length = MIN(16 << 10,
			SAMPLE_BUF_SIZE - devc->total_received_sample_bytes);

		libusb_submit_transfer(xfer);
		return;
	}

	usb_source_remove(sdi->session, drvc->sr_ctx);

	read_offset = sample_to_byte_offset(devc, devc->earliest_sample);
	trigger_offset = sample_to_byte_offset(devc, devc->trigger_sample);

	/*
	 * The last few bytes seem to contain garbage data,
	 * so ignore them.
	 */
	bytes_left = (SAMPLE_BUF_SIZE >> 10) * 1000;

	sr_spew("Start reading at offset 0x%04hx.", read_offset);
	sr_spew("Trigger offset 0x%04hx.", trigger_offset);

	if (trigger_offset < read_offset) {
		length = MIN(bytes_left, SAMPLE_BUF_SIZE - read_offset);

		sr_spew("Sending %u pre-trigger bytes starting at 0x%04hx.",
			length, read_offset);

		send_samples(sdi, &devc->fetched_samples[read_offset], length);

		bytes_left -= length;
		read_offset = 0;
	}

	{
		length = MIN(bytes_left, (uint32_t)(trigger_offset - read_offset));

		sr_spew("Sending %u pre-trigger bytes starting at 0x%04hx.",
			length, read_offset);

		send_samples(sdi, &devc->fetched_samples[read_offset], length);

		bytes_left -= length;

		read_offset += length;
		read_offset %= SAMPLE_BUF_SIZE;
	}

	/* Here comes the trigger. */
	packet.type = SR_DF_TRIGGER;
	packet.payload = NULL;

	sr_session_send(sdi, &packet);

	/* Send post-trigger samples. */
	while (bytes_left > 0) {
		length = MIN(bytes_left, SAMPLE_BUF_SIZE - read_offset);

		sr_spew("Sending %u post-trigger bytes starting at 0x%04hx.",
			length, read_offset);

		send_samples(sdi, &devc->fetched_samples[read_offset], length);

		bytes_left -= length;

		read_offset += length;
		read_offset %= SAMPLE_BUF_SIZE;
	}

	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);
}

static uint32_t transform_sample_count(struct dev_context *devc,
	uint32_t samples)
{
	uint32_t d = 8 / devc->num_enabled_channel_groups;

	return (samples + 0x1c + d + d - 1) / d;
}

SR_PRIV int lls_start_acquisition(const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	struct dev_context *devc;
	struct regval cmd[17];
	uint32_t unk0, total_samples, pre_trigger_samples, post_trigger_samples;
	uint32_t pre_trigger_tr, post_trigger_tr;
	int i;

	usb = sdi->conn;
	devc = sdi->priv;

	devc->abort_acquisition = FALSE;

	libusb_fill_interrupt_transfer(devc->intr_xfer, usb->devhdl, EP_INTR,
		devc->intr_buf, INTR_BUF_SIZE,
		recv_intr_transfer, (void *) sdi, USB_TIMEOUT_MS);

	libusb_submit_transfer(devc->intr_xfer);

	if (devc->want_trigger == FALSE)
		return SR_OK;

	calc_unk0(&unk0, NULL);

	total_samples = devc->num_thousand_samples * 1000;

	pre_trigger_samples = total_samples * devc->capture_ratio / 100;
	post_trigger_samples = total_samples - pre_trigger_samples;

	pre_trigger_tr = transform_sample_count(devc, pre_trigger_samples);
	post_trigger_tr = transform_sample_count(devc, post_trigger_samples);

	i = 0;

	prep_regw(&cmd[i++], REG_ARM_TRIGGER, devc->magic_arm_trigger);
	prep_regw(&cmd[i++], REG_ARM_TRIGGER, devc->magic_arm_trigger | 0x02);

	prep_regw(&cmd[i++], REG_UNK6_LO, 0x0000);
	prep_regw(&cmd[i++], REG_UNK6_HI, 0x0000);

	prep_regw(&cmd[i++], REG_UNK0_LO, (unk0 >>  0) & 0xffff);
	prep_regw(&cmd[i++], REG_UNK0_HI, (unk0 >> 16) & 0xffff);

	prep_regw(&cmd[i++], REG_UNK4_LO, 0x0000);
	prep_regw(&cmd[i++], REG_UNK4_HI, 0x0000);

	prep_regw(&cmd[i++], REG_UNK5_LO, 0x0000);
	prep_regw(&cmd[i++], REG_UNK5_HI, 0x0000);

	prep_regw(&cmd[i++], REG_ACQUISITION_ID, ++devc->acquisition_id);
	prep_regw(&cmd[i++], REG_SAMPLERATE, devc->samplerate_info->cfg);

	prep_regw(&cmd[i++], REG_PRETRIG_LO, (pre_trigger_tr >>  0) & 0xffff);
	prep_regw(&cmd[i++], REG_PRETRIG_HI, (pre_trigger_tr >> 16) & 0xffff);

	prep_regw(&cmd[i++], REG_POSTTRIG_LO, (post_trigger_tr >>  0) & 0xffff);
	prep_regw(&cmd[i++], REG_POSTTRIG_HI, (post_trigger_tr >> 16) & 0xffff);

	devc->magic_arm_trigger = 0x0c;
	prep_regw(&cmd[i++], REG_ARM_TRIGGER, devc->magic_arm_trigger | 0x01);

	return write_registers_sync(sdi, 0x12, 5444, cmd, ARRAY_SIZE(cmd));
}

SR_PRIV int lls_stop_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct regval cmd[2];
	int i;

	devc = sdi->priv;

	devc->abort_acquisition = TRUE;

	i = 0;

	prep_regw(&cmd[i++], REG_ARM_TRIGGER, devc->magic_arm_trigger);
	prep_regw(&cmd[i++], REG_ARM_TRIGGER, devc->magic_arm_trigger | 0x02);

	assert(i == ARRAY_SIZE(cmd));

	return write_registers_sync(sdi, 0x12, 5444, cmd, ARRAY_SIZE(cmd));
}
