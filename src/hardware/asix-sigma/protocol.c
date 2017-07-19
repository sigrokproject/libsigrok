/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2010-2012 Håvard Espeland <gus@ping.uio.no>,
 * Copyright (C) 2010 Martin Stensgård <mastensg@ping.uio.no>
 * Copyright (C) 2010 Carl Henrik Lunde <chlunde@ping.uio.no>
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

/*
 * ASIX SIGMA/SIGMA2 logic analyzer driver
 */

#include <config.h>
#include "protocol.h"

/*
 * The ASIX Sigma supports arbitrary integer frequency divider in
 * the 50MHz mode. The divider is in range 1...256 , allowing for
 * very precise sampling rate selection. This driver supports only
 * a subset of the sampling rates.
 */
SR_PRIV const uint64_t samplerates[] = {
	SR_KHZ(200),	/* div=250 */
	SR_KHZ(250),	/* div=200 */
	SR_KHZ(500),	/* div=100 */
	SR_MHZ(1),	/* div=50  */
	SR_MHZ(5),	/* div=10  */
	SR_MHZ(10),	/* div=5   */
	SR_MHZ(25),	/* div=2   */
	SR_MHZ(50),	/* div=1   */
	SR_MHZ(100),	/* Special FW needed */
	SR_MHZ(200),	/* Special FW needed */
};

SR_PRIV const size_t samplerates_count = ARRAY_SIZE(samplerates);

static const char firmware_files[][24] = {
	/* 50 MHz, supports 8 bit fractions */
	"asix-sigma-50.fw",
	/* 100 MHz */
	"asix-sigma-100.fw",
	/* 200 MHz */
	"asix-sigma-200.fw",
	/* Synchronous clock from pin */
	"asix-sigma-50sync.fw",
	/* Frequency counter */
	"asix-sigma-phasor.fw",
};

static int sigma_read(void *buf, size_t size, struct dev_context *devc)
{
	int ret;

	ret = ftdi_read_data(&devc->ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		sr_err("ftdi_read_data failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
	}

	return ret;
}

static int sigma_write(void *buf, size_t size, struct dev_context *devc)
{
	int ret;

	ret = ftdi_write_data(&devc->ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		sr_err("ftdi_write_data failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
	} else if ((size_t) ret != size) {
		sr_err("ftdi_write_data did not complete write.");
	}

	return ret;
}

/*
 * NOTE: We chose the buffer size to be large enough to hold any write to the
 * device. We still print a message just in case.
 */
SR_PRIV int sigma_write_register(uint8_t reg, uint8_t *data, size_t len,
				 struct dev_context *devc)
{
	size_t i;
	uint8_t buf[80];
	int idx = 0;

	if ((2 * len + 2) > sizeof(buf)) {
		sr_err("Attempted to write %zu bytes, but buffer is too small.",
		       len);
		return SR_ERR_BUG;
	}

	buf[idx++] = REG_ADDR_LOW | (reg & 0xf);
	buf[idx++] = REG_ADDR_HIGH | (reg >> 4);

	for (i = 0; i < len; i++) {
		buf[idx++] = REG_DATA_LOW | (data[i] & 0xf);
		buf[idx++] = REG_DATA_HIGH_WRITE | (data[i] >> 4);
	}

	return sigma_write(buf, idx, devc);
}

SR_PRIV int sigma_set_register(uint8_t reg, uint8_t value, struct dev_context *devc)
{
	return sigma_write_register(reg, &value, 1, devc);
}

static int sigma_read_register(uint8_t reg, uint8_t *data, size_t len,
			       struct dev_context *devc)
{
	uint8_t buf[3];

	buf[0] = REG_ADDR_LOW | (reg & 0xf);
	buf[1] = REG_ADDR_HIGH | (reg >> 4);
	buf[2] = REG_READ_ADDR;

	sigma_write(buf, sizeof(buf), devc);

	return sigma_read(data, len, devc);
}

static uint8_t sigma_get_register(uint8_t reg, struct dev_context *devc)
{
	uint8_t value;

	if (1 != sigma_read_register(reg, &value, 1, devc)) {
		sr_err("sigma_get_register: 1 byte expected");
		return 0;
	}

	return value;
}

static int sigma_read_pos(uint32_t *stoppos, uint32_t *triggerpos,
			  struct dev_context *devc)
{
	uint8_t buf[] = {
		REG_ADDR_LOW | READ_TRIGGER_POS_LOW,

		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
	};
	uint8_t result[6];

	sigma_write(buf, sizeof(buf), devc);

	sigma_read(result, sizeof(result), devc);

	*triggerpos = result[0] | (result[1] << 8) | (result[2] << 16);
	*stoppos = result[3] | (result[4] << 8) | (result[5] << 16);

	/* Not really sure why this must be done, but according to spec. */
	if ((--*stoppos & 0x1ff) == 0x1ff)
		*stoppos -= 64;

	if ((*--triggerpos & 0x1ff) == 0x1ff)
		*triggerpos -= 64;

	return 1;
}

static int sigma_read_dram(uint16_t startchunk, size_t numchunks,
			   uint8_t *data, struct dev_context *devc)
{
	size_t i;
	uint8_t buf[4096];
	int idx;

	/* Send the startchunk. Index start with 1. */
	idx = 0;
	buf[idx++] = startchunk >> 8;
	buf[idx++] = startchunk & 0xff;
	sigma_write_register(WRITE_MEMROW, buf, idx, devc);

	/* Read the DRAM. */
	idx = 0;
	buf[idx++] = REG_DRAM_BLOCK;
	buf[idx++] = REG_DRAM_WAIT_ACK;

	for (i = 0; i < numchunks; i++) {
		/* Alternate bit to copy from DRAM to cache. */
		if (i != (numchunks - 1))
			buf[idx++] = REG_DRAM_BLOCK | (((i + 1) % 2) << 4);

		buf[idx++] = REG_DRAM_BLOCK_DATA | ((i % 2) << 4);

		if (i != (numchunks - 1))
			buf[idx++] = REG_DRAM_WAIT_ACK;
	}

	sigma_write(buf, idx, devc);

	return sigma_read(data, numchunks * CHUNK_SIZE, devc);
}

/* Upload trigger look-up tables to Sigma. */
SR_PRIV int sigma_write_trigger_lut(struct triggerlut *lut, struct dev_context *devc)
{
	int i;
	uint8_t tmp[2];
	uint16_t bit;

	/* Transpose the table and send to Sigma. */
	for (i = 0; i < 16; i++) {
		bit = 1 << i;

		tmp[0] = tmp[1] = 0;

		if (lut->m2d[0] & bit)
			tmp[0] |= 0x01;
		if (lut->m2d[1] & bit)
			tmp[0] |= 0x02;
		if (lut->m2d[2] & bit)
			tmp[0] |= 0x04;
		if (lut->m2d[3] & bit)
			tmp[0] |= 0x08;

		if (lut->m3 & bit)
			tmp[0] |= 0x10;
		if (lut->m3s & bit)
			tmp[0] |= 0x20;
		if (lut->m4 & bit)
			tmp[0] |= 0x40;

		if (lut->m0d[0] & bit)
			tmp[1] |= 0x01;
		if (lut->m0d[1] & bit)
			tmp[1] |= 0x02;
		if (lut->m0d[2] & bit)
			tmp[1] |= 0x04;
		if (lut->m0d[3] & bit)
			tmp[1] |= 0x08;

		if (lut->m1d[0] & bit)
			tmp[1] |= 0x10;
		if (lut->m1d[1] & bit)
			tmp[1] |= 0x20;
		if (lut->m1d[2] & bit)
			tmp[1] |= 0x40;
		if (lut->m1d[3] & bit)
			tmp[1] |= 0x80;

		sigma_write_register(WRITE_TRIGGER_SELECT0, tmp, sizeof(tmp),
				     devc);
		sigma_set_register(WRITE_TRIGGER_SELECT1, 0x30 | i, devc);
	}

	/* Send the parameters */
	sigma_write_register(WRITE_TRIGGER_SELECT0, (uint8_t *) &lut->params,
			     sizeof(lut->params), devc);

	return SR_OK;
}

/*
 * Configure the FPGA for bitbang mode.
 * This sequence is documented in section 2. of the ASIX Sigma programming
 * manual. This sequence is necessary to configure the FPGA in the Sigma
 * into Bitbang mode, in which it can be programmed with the firmware.
 */
static int sigma_fpga_init_bitbang(struct dev_context *devc)
{
	uint8_t suicide[] = {
		0x84, 0x84, 0x88, 0x84, 0x88, 0x84, 0x88, 0x84,
	};
	uint8_t init_array[] = {
		0x01, 0x03, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01,
	};
	int i, ret, timeout = (10 * 1000);
	uint8_t data;

	/* Section 2. part 1), do the FPGA suicide. */
	sigma_write(suicide, sizeof(suicide), devc);
	sigma_write(suicide, sizeof(suicide), devc);
	sigma_write(suicide, sizeof(suicide), devc);
	sigma_write(suicide, sizeof(suicide), devc);

	/* Section 2. part 2), do pulse on D1. */
	sigma_write(init_array, sizeof(init_array), devc);
	ftdi_usb_purge_buffers(&devc->ftdic);

	/* Wait until the FPGA asserts D6/INIT_B. */
	for (i = 0; i < timeout; i++) {
		ret = sigma_read(&data, 1, devc);
		if (ret < 0)
			return ret;
		/* Test if pin D6 got asserted. */
		if (data & (1 << 5))
			return 0;
		/* The D6 was not asserted yet, wait a bit. */
		g_usleep(10 * 1000);
	}

	return SR_ERR_TIMEOUT;
}

/*
 * Configure the FPGA for logic-analyzer mode.
 */
static int sigma_fpga_init_la(struct dev_context *devc)
{
	/* Initialize the logic analyzer mode. */
	uint8_t mode_regval = WMR_SDRAMINIT;
	uint8_t logic_mode_start[] = {
		REG_ADDR_LOW  | (READ_ID & 0xf),
		REG_ADDR_HIGH | (READ_ID >> 4),
		REG_READ_ADDR,	/* Read ID register. */

		REG_ADDR_LOW | (WRITE_TEST & 0xf),
		REG_DATA_LOW | 0x5,
		REG_DATA_HIGH_WRITE | 0x5,
		REG_READ_ADDR,	/* Read scratch register. */

		REG_DATA_LOW | 0xa,
		REG_DATA_HIGH_WRITE | 0xa,
		REG_READ_ADDR,	/* Read scratch register. */

		REG_ADDR_LOW | (WRITE_MODE & 0xf),
		REG_DATA_LOW | (mode_regval & 0xf),
		REG_DATA_HIGH_WRITE | (mode_regval >> 4),
	};

	uint8_t result[3];
	int ret;

	/* Initialize the logic analyzer mode. */
	sigma_write(logic_mode_start, sizeof(logic_mode_start), devc);

	/* Expect a 3 byte reply since we issued three READ requests. */
	ret = sigma_read(result, 3, devc);
	if (ret != 3)
		goto err;

	if (result[0] != 0xa6 || result[1] != 0x55 || result[2] != 0xaa)
		goto err;

	return SR_OK;
err:
	sr_err("Configuration failed. Invalid reply received.");
	return SR_ERR;
}

/*
 * Read the firmware from a file and transform it into a series of bitbang
 * pulses used to program the FPGA. Note that the *bb_cmd must be free()'d
 * by the caller of this function.
 */
static int sigma_fw_2_bitbang(struct sr_context *ctx, const char *name,
			      uint8_t **bb_cmd, gsize *bb_cmd_size)
{
	size_t i, file_size, bb_size;
	char *firmware;
	uint8_t *bb_stream, *bbs;
	uint32_t imm;
	int bit, v;
	int ret = SR_OK;

	/* Retrieve the on-disk firmware file content. */
	firmware = sr_resource_load(ctx, SR_RESOURCE_FIRMWARE,
			name, &file_size, 256 * 1024);
	if (!firmware)
		return SR_ERR;

	/* Unscramble the file content (XOR with "random" sequence). */
	imm = 0x3f6df2ab;
	for (i = 0; i < file_size; i++) {
		imm = (imm + 0xa853753) % 177 + (imm * 0x8034052);
		firmware[i] ^= imm & 0xff;
	}

	/*
	 * Generate a sequence of bitbang samples. With two samples per
	 * FPGA configuration bit, providing the level for the DIN signal
	 * as well as two edges for CCLK. See Xilinx UG332 for details
	 * ("slave serial" mode).
	 *
	 * Note that CCLK is inverted in hardware. That's why the
	 * respective bit is first set and then cleared in the bitbang
	 * sample sets. So that the DIN level will be stable when the
	 * data gets sampled at the rising CCLK edge, and the signals'
	 * setup time constraint will be met.
	 *
	 * The caller will put the FPGA into download mode, will send
	 * the bitbang samples, and release the allocated memory.
	 */
	bb_size = file_size * 8 * 2;
	bb_stream = (uint8_t *)g_try_malloc(bb_size);
	if (!bb_stream) {
		sr_err("%s: Failed to allocate bitbang stream", __func__);
		ret = SR_ERR_MALLOC;
		goto exit;
	}
	bbs = bb_stream;
	for (i = 0; i < file_size; i++) {
		for (bit = 7; bit >= 0; bit--) {
			v = (firmware[i] & (1 << bit)) ? 0x40 : 0x00;
			*bbs++ = v | 0x01;
			*bbs++ = v;
		}
	}

	/* The transformation completed successfully, return the result. */
	*bb_cmd = bb_stream;
	*bb_cmd_size = bb_size;

exit:
	g_free(firmware);
	return ret;
}

static int upload_firmware(struct sr_context *ctx,
		int firmware_idx, struct dev_context *devc)
{
	int ret;
	unsigned char *buf;
	unsigned char pins;
	size_t buf_size;
	const char *firmware;

	/* Avoid downloading the same firmware multiple times. */
	firmware = firmware_files[firmware_idx];
	if (devc->cur_firmware == firmware_idx) {
		sr_info("Not uploading firmware file '%s' again.", firmware);
		return SR_OK;
	}

	ret = ftdi_set_bitmode(&devc->ftdic, 0xdf, BITMODE_BITBANG);
	if (ret < 0) {
		sr_err("ftdi_set_bitmode failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
		return SR_ERR;
	}

	/* Four times the speed of sigmalogan - Works well. */
	ret = ftdi_set_baudrate(&devc->ftdic, 750 * 1000);
	if (ret < 0) {
		sr_err("ftdi_set_baudrate failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
		return SR_ERR;
	}

	/* Initialize the FPGA for firmware upload. */
	ret = sigma_fpga_init_bitbang(devc);
	if (ret)
		return ret;

	/* Prepare firmware. */
	ret = sigma_fw_2_bitbang(ctx, firmware, &buf, &buf_size);
	if (ret != SR_OK) {
		sr_err("An error occurred while reading the firmware: %s",
		       firmware);
		return ret;
	}

	/* Upload firmware. */
	sr_info("Uploading firmware file '%s'.", firmware);
	sigma_write(buf, buf_size, devc);

	g_free(buf);

	ret = ftdi_set_bitmode(&devc->ftdic, 0x00, BITMODE_RESET);
	if (ret < 0) {
		sr_err("ftdi_set_bitmode failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
		return SR_ERR;
	}

	ftdi_usb_purge_buffers(&devc->ftdic);

	/* Discard garbage. */
	while (sigma_read(&pins, 1, devc) == 1)
		;

	/* Initialize the FPGA for logic-analyzer mode. */
	ret = sigma_fpga_init_la(devc);
	if (ret != SR_OK)
		return ret;

	devc->cur_firmware = firmware_idx;

	sr_info("Firmware uploaded.");

	return SR_OK;
}

/*
 * Sigma doesn't support limiting the number of samples, so we have to
 * translate the number and the samplerate to an elapsed time.
 *
 * In addition we need to ensure that the last data cluster has passed
 * the hardware pipeline, and became available to the PC side. With RLE
 * compression up to 327ms could pass before another cluster accumulates
 * at 200kHz samplerate when input pins don't change.
 */
SR_PRIV uint64_t sigma_limit_samples_to_msec(const struct dev_context *devc,
					     uint64_t limit_samples)
{
	uint64_t limit_msec;
	uint64_t worst_cluster_time_ms;

	limit_msec = limit_samples * 1000 / devc->cur_samplerate;
	worst_cluster_time_ms = 65536 * 1000 / devc->cur_samplerate;
	/*
	 * One cluster time is not enough to flush pipeline when sampling
	 * grounded pins with 1 sample limit at 200kHz. Hence the 2* fix.
	 */
	return limit_msec + 2 * worst_cluster_time_ms;
}

SR_PRIV int sigma_set_samplerate(const struct sr_dev_inst *sdi, uint64_t samplerate)
{
	struct dev_context *devc;
	struct drv_context *drvc;
	size_t i;
	int ret;
	int num_channels;

	devc = sdi->priv;
	drvc = sdi->driver->context;
	ret = SR_OK;

	/* Reject rates that are not in the list of supported rates. */
	for (i = 0; i < samplerates_count; i++) {
		if (samplerates[i] == samplerate)
			break;
	}
	if (i >= samplerates_count || samplerates[i] == 0)
		return SR_ERR_SAMPLERATE;

	/*
	 * Depending on the samplerates of 200/100/50- MHz, specific
	 * firmware is required and higher rates might limit the set
	 * of available channels.
	 */
	num_channels = devc->num_channels;
	if (samplerate <= SR_MHZ(50)) {
		ret = upload_firmware(drvc->sr_ctx, 0, devc);
		num_channels = 16;
	} else if (samplerate == SR_MHZ(100)) {
		ret = upload_firmware(drvc->sr_ctx, 1, devc);
		num_channels = 8;
	} else if (samplerate == SR_MHZ(200)) {
		ret = upload_firmware(drvc->sr_ctx, 2, devc);
		num_channels = 4;
	}

	/*
	 * Derive the sample period from the sample rate as well as the
	 * number of samples that the device will communicate within
	 * an "event" (memory organization internal to the device).
	 */
	if (ret == SR_OK) {
		devc->num_channels = num_channels;
		devc->cur_samplerate = samplerate;
		devc->samples_per_event = 16 / devc->num_channels;
		devc->state.state = SIGMA_IDLE;
	}

	/*
	 * Support for "limit_samples" is implemented by stopping
	 * acquisition after a corresponding period of time.
	 * Re-calculate that period of time, in case the limit is
	 * set first and the samplerate gets (re-)configured later.
	 */
	if (ret == SR_OK && devc->limit_samples) {
		uint64_t msecs;
		msecs = sigma_limit_samples_to_msec(devc, devc->limit_samples);
		devc->limit_msec = msecs;
	}

	return ret;
}

/*
 * In 100 and 200 MHz mode, only a single pin rising/falling can be
 * set as trigger. In other modes, two rising/falling triggers can be set,
 * in addition to value/mask trigger for any number of channels.
 *
 * The Sigma supports complex triggers using boolean expressions, but this
 * has not been implemented yet.
 */
SR_PRIV int sigma_convert_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	const GSList *l, *m;
	int channelbit, trigger_set;

	devc = sdi->priv;
	memset(&devc->trigger, 0, sizeof(struct sigma_trigger));
	if (!(trigger = sr_session_trigger_get(sdi->session)))
		return SR_OK;

	trigger_set = 0;
	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			if (!match->channel->enabled)
				/* Ignore disabled channels with a trigger. */
				continue;
			channelbit = 1 << (match->channel->index);
			if (devc->cur_samplerate >= SR_MHZ(100)) {
				/* Fast trigger support. */
				if (trigger_set) {
					sr_err("Only a single pin trigger is "
							"supported in 100 and 200MHz mode.");
					return SR_ERR;
				}
				if (match->match == SR_TRIGGER_FALLING)
					devc->trigger.fallingmask |= channelbit;
				else if (match->match == SR_TRIGGER_RISING)
					devc->trigger.risingmask |= channelbit;
				else {
					sr_err("Only rising/falling trigger is "
							"supported in 100 and 200MHz mode.");
					return SR_ERR;
				}

				trigger_set++;
			} else {
				/* Simple trigger support (event). */
				if (match->match == SR_TRIGGER_ONE) {
					devc->trigger.simplevalue |= channelbit;
					devc->trigger.simplemask |= channelbit;
				}
				else if (match->match == SR_TRIGGER_ZERO) {
					devc->trigger.simplevalue &= ~channelbit;
					devc->trigger.simplemask |= channelbit;
				}
				else if (match->match == SR_TRIGGER_FALLING) {
					devc->trigger.fallingmask |= channelbit;
					trigger_set++;
				}
				else if (match->match == SR_TRIGGER_RISING) {
					devc->trigger.risingmask |= channelbit;
					trigger_set++;
				}

				/*
				 * Actually, Sigma supports 2 rising/falling triggers,
				 * but they are ORed and the current trigger syntax
				 * does not permit ORed triggers.
				 */
				if (trigger_set > 1) {
					sr_err("Only 1 rising/falling trigger "
						   "is supported.");
					return SR_ERR;
				}
			}
		}
	}

	return SR_OK;
}

/* Software trigger to determine exact trigger position. */
static int get_trigger_offset(uint8_t *samples, uint16_t last_sample,
			      struct sigma_trigger *t)
{
	int i;
	uint16_t sample = 0;

	for (i = 0; i < 8; i++) {
		if (i > 0)
			last_sample = sample;
		sample = samples[2 * i] | (samples[2 * i + 1] << 8);

		/* Simple triggers. */
		if ((sample & t->simplemask) != t->simplevalue)
			continue;

		/* Rising edge. */
		if (((last_sample & t->risingmask) != 0) ||
		    ((sample & t->risingmask) != t->risingmask))
			continue;

		/* Falling edge. */
		if ((last_sample & t->fallingmask) != t->fallingmask ||
		    (sample & t->fallingmask) != 0)
			continue;

		break;
	}

	/* If we did not match, return original trigger pos. */
	return i & 0x7;
}

/*
 * Return the timestamp of "DRAM cluster".
 */
static uint16_t sigma_dram_cluster_ts(struct sigma_dram_cluster *cluster)
{
	return (cluster->timestamp_hi << 8) | cluster->timestamp_lo;
}

/*
 * Return one 16bit data entity of a DRAM cluster at the specified index.
 */
static uint16_t sigma_dram_cluster_data(struct sigma_dram_cluster *cl, int idx)
{
	uint16_t sample;

	sample = 0;
	sample |= cl->samples[idx].sample_lo << 0;
	sample |= cl->samples[idx].sample_hi << 8;
	sample = (sample >> 8) | (sample << 8);
	return sample;
}

/*
 * Deinterlace sample data that was retrieved at 100MHz samplerate.
 * One 16bit item contains two samples of 8bits each. The bits of
 * multiple samples are interleaved.
 */
static uint16_t sigma_deinterlace_100mhz_data(uint16_t indata, int idx)
{
	uint16_t outdata;

	indata >>= idx;
	outdata = 0;
	outdata |= (indata >> (0 * 2 - 0)) & (1 << 0);
	outdata |= (indata >> (1 * 2 - 1)) & (1 << 1);
	outdata |= (indata >> (2 * 2 - 2)) & (1 << 2);
	outdata |= (indata >> (3 * 2 - 3)) & (1 << 3);
	outdata |= (indata >> (4 * 2 - 4)) & (1 << 4);
	outdata |= (indata >> (5 * 2 - 5)) & (1 << 5);
	outdata |= (indata >> (6 * 2 - 6)) & (1 << 6);
	outdata |= (indata >> (7 * 2 - 7)) & (1 << 7);
	return outdata;
}

/*
 * Deinterlace sample data that was retrieved at 200MHz samplerate.
 * One 16bit item contains four samples of 4bits each. The bits of
 * multiple samples are interleaved.
 */
static uint16_t sigma_deinterlace_200mhz_data(uint16_t indata, int idx)
{
	uint16_t outdata;

	indata >>= idx;
	outdata = 0;
	outdata |= (indata >> (0 * 4 - 0)) & (1 << 0);
	outdata |= (indata >> (1 * 4 - 1)) & (1 << 1);
	outdata |= (indata >> (2 * 4 - 2)) & (1 << 2);
	outdata |= (indata >> (3 * 4 - 3)) & (1 << 3);
	return outdata;
}

static void store_sr_sample(uint8_t *samples, int idx, uint16_t data)
{
	samples[2 * idx + 0] = (data >> 0) & 0xff;
	samples[2 * idx + 1] = (data >> 8) & 0xff;
}

/*
 * Local wrapper around sr_session_send() calls. Make sure to not send
 * more samples to the session's datafeed than what was requested by a
 * previously configured (optional) sample count.
 */
static void sigma_session_send(struct sr_dev_inst *sdi,
				struct sr_datafeed_packet *packet)
{
	struct dev_context *devc;
	struct sr_datafeed_logic *logic;
	uint64_t send_now;

	devc = sdi->priv;
	if (devc->limit_samples) {
		logic = (void *)packet->payload;
		send_now = logic->length / logic->unitsize;
		if (devc->sent_samples + send_now > devc->limit_samples) {
			send_now = devc->limit_samples - devc->sent_samples;
			logic->length = send_now * logic->unitsize;
		}
		if (!send_now)
			return;
		devc->sent_samples += send_now;
	}

	sr_session_send(sdi, packet);
}

/*
 * This size translates to: event count (1K events per cluster), times
 * the sample width (unitsize, 16bits per event), times the maximum
 * number of samples per event.
 */
#define SAMPLES_BUFFER_SIZE	(1024 * 2 * 4)

static void sigma_decode_dram_cluster(struct sigma_dram_cluster *dram_cluster,
				      unsigned int events_in_cluster,
				      unsigned int triggered,
				      struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sigma_state *ss = &devc->state;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint16_t tsdiff, ts, sample, item16;
	uint8_t samples[SAMPLES_BUFFER_SIZE];
	uint8_t *send_ptr;
	size_t send_count, trig_count;
	unsigned int i;
	int j;

	ts = sigma_dram_cluster_ts(dram_cluster);
	tsdiff = ts - ss->lastts;
	ss->lastts = ts + EVENTS_PER_CLUSTER;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = 2;
	logic.data = samples;

	/*
	 * If this cluster is not adjacent to the previously received
	 * cluster, then send the appropriate number of samples with the
	 * previous values to the sigrok session. This "decodes RLE".
	 */
	for (ts = 0; ts < tsdiff; ts++) {
		i = ts % 1024;
		store_sr_sample(samples, i, ss->lastsample);

		/*
		 * If we have 1024 samples ready or we're at the
		 * end of submitting the padding samples, submit
		 * the packet to Sigrok. Since constant data is
		 * sent, duplication of data for rates above 50MHz
		 * is simple.
		 */
		if ((i == 1023) || (ts == tsdiff - 1)) {
			logic.length = (i + 1) * logic.unitsize;
			for (j = 0; j < devc->samples_per_event; j++)
				sigma_session_send(sdi, &packet);
		}
	}

	/*
	 * Parse the samples in current cluster and prepare them
	 * to be submitted to Sigrok. Cope with memory layouts that
	 * vary with the samplerate.
	 */
	send_ptr = &samples[0];
	send_count = 0;
	sample = 0;
	for (i = 0; i < events_in_cluster; i++) {
		item16 = sigma_dram_cluster_data(dram_cluster, i);
		if (devc->cur_samplerate == SR_MHZ(200)) {
			sample = sigma_deinterlace_200mhz_data(item16, 0);
			store_sr_sample(samples, send_count++, sample);
			sample = sigma_deinterlace_200mhz_data(item16, 1);
			store_sr_sample(samples, send_count++, sample);
			sample = sigma_deinterlace_200mhz_data(item16, 2);
			store_sr_sample(samples, send_count++, sample);
			sample = sigma_deinterlace_200mhz_data(item16, 3);
			store_sr_sample(samples, send_count++, sample);
		} else if (devc->cur_samplerate == SR_MHZ(100)) {
			sample = sigma_deinterlace_100mhz_data(item16, 0);
			store_sr_sample(samples, send_count++, sample);
			sample = sigma_deinterlace_100mhz_data(item16, 1);
			store_sr_sample(samples, send_count++, sample);
		} else {
			sample = item16;
			store_sr_sample(samples, send_count++, sample);
		}
	}

	/*
	 * If a trigger position applies, then provide the datafeed with
	 * the first part of data up to that position, then send the
	 * trigger marker.
	 */
	int trigger_offset = 0;
	if (triggered) {
		/*
		 * Trigger is not always accurate to sample because of
		 * pipeline delay. However, it always triggers before
		 * the actual event. We therefore look at the next
		 * samples to pinpoint the exact position of the trigger.
		 */
		trigger_offset = get_trigger_offset(samples,
					ss->lastsample, &devc->trigger);

		if (trigger_offset > 0) {
			trig_count = trigger_offset * devc->samples_per_event;
			packet.type = SR_DF_LOGIC;
			logic.length = trig_count * logic.unitsize;
			sigma_session_send(sdi, &packet);
			send_ptr += trig_count * logic.unitsize;
			send_count -= trig_count;
		}

		/* Only send trigger if explicitly enabled. */
		if (devc->use_triggers) {
			packet.type = SR_DF_TRIGGER;
			sr_session_send(sdi, &packet);
		}
	}

	/*
	 * Send the data after the trigger, or all of the received data
	 * if no trigger position applies.
	 */
	if (send_count) {
		packet.type = SR_DF_LOGIC;
		logic.length = send_count * logic.unitsize;
		logic.data = send_ptr;
		sigma_session_send(sdi, &packet);
	}

	ss->lastsample = sample;
}

/*
 * Decode chunk of 1024 bytes, 64 clusters, 7 events per cluster.
 * Each event is 20ns apart, and can contain multiple samples.
 *
 * For 200 MHz, events contain 4 samples for each channel, spread 5 ns apart.
 * For 100 MHz, events contain 2 samples for each channel, spread 10 ns apart.
 * For 50 MHz and below, events contain one sample for each channel,
 * spread 20 ns apart.
 */
static int decode_chunk_ts(struct sigma_dram_line *dram_line,
			   uint16_t events_in_line,
			   uint32_t trigger_event,
			   struct sr_dev_inst *sdi)
{
	struct sigma_dram_cluster *dram_cluster;
	struct dev_context *devc;
	unsigned int clusters_in_line;
	unsigned int events_in_cluster;
	unsigned int i;
	uint32_t trigger_cluster, triggered;

	devc = sdi->priv;
	clusters_in_line = events_in_line;
	clusters_in_line += EVENTS_PER_CLUSTER - 1;
	clusters_in_line /= EVENTS_PER_CLUSTER;
	trigger_cluster = ~0;
	triggered = 0;

	/* Check if trigger is in this chunk. */
	if (trigger_event < (64 * 7)) {
		if (devc->cur_samplerate <= SR_MHZ(50)) {
			trigger_event -= MIN(EVENTS_PER_CLUSTER - 1,
					     trigger_event);
		}

		/* Find in which cluster the trigger occurred. */
		trigger_cluster = trigger_event / EVENTS_PER_CLUSTER;
	}

	/* For each full DRAM cluster. */
	for (i = 0; i < clusters_in_line; i++) {
		dram_cluster = &dram_line->cluster[i];

		/* The last cluster might not be full. */
		if ((i == clusters_in_line - 1) &&
		    (events_in_line % EVENTS_PER_CLUSTER)) {
			events_in_cluster = events_in_line % EVENTS_PER_CLUSTER;
		} else {
			events_in_cluster = EVENTS_PER_CLUSTER;
		}

		triggered = (i == trigger_cluster);
		sigma_decode_dram_cluster(dram_cluster, events_in_cluster,
					  triggered, sdi);
	}

	return SR_OK;
}

static int download_capture(struct sr_dev_inst *sdi)
{
	const uint32_t chunks_per_read = 32;

	struct dev_context *devc;
	struct sigma_dram_line *dram_line;
	int bufsz;
	uint32_t stoppos, triggerpos;
	uint8_t modestatus;
	uint32_t i;
	uint32_t dl_lines_total, dl_lines_curr, dl_lines_done;
	uint32_t dl_first_line, dl_line;
	uint32_t dl_events_in_line;
	uint32_t trg_line, trg_event;

	devc = sdi->priv;
	dl_events_in_line = 64 * 7;
	trg_line = ~0;
	trg_event = ~0;

	dram_line = g_try_malloc0(chunks_per_read * sizeof(*dram_line));
	if (!dram_line)
		return FALSE;

	sr_info("Downloading sample data.");

	/*
	 * Ask the hardware to stop data acquisition. Reception of the
	 * FORCESTOP request makes the hardware "disable RLE" (store
	 * clusters to DRAM regardless of whether pin state changes) and
	 * raise the POSTTRIGGERED flag.
	 */
	sigma_set_register(WRITE_MODE, WMR_FORCESTOP | WMR_SDRAMWRITEEN, devc);
	do {
		modestatus = sigma_get_register(READ_MODE, devc);
	} while (!(modestatus & RMR_POSTTRIGGERED));

	/* Set SDRAM Read Enable. */
	sigma_set_register(WRITE_MODE, WMR_SDRAMREADEN, devc);

	/* Get the current position. */
	sigma_read_pos(&stoppos, &triggerpos, devc);

	/* Check if trigger has fired. */
	modestatus = sigma_get_register(READ_MODE, devc);
	if (modestatus & RMR_TRIGGERED) {
		trg_line = triggerpos >> 9;
		trg_event = triggerpos & 0x1ff;
	}

	devc->sent_samples = 0;

	/*
	 * Determine how many "DRAM lines" of 1024 bytes each we need to
	 * retrieve from the Sigma hardware, so that we have a complete
	 * set of samples. Note that the last line need not contain 64
	 * clusters, it might be partially filled only.
	 *
	 * When RMR_ROUND is set, the circular buffer in DRAM has wrapped
	 * around. Since the status of the very next line is uncertain in
	 * that case, we skip it and start reading from the next line. The
	 * circular buffer has 32K lines (0x8000).
	 */
	dl_lines_total = (stoppos >> 9) + 1;
	if (modestatus & RMR_ROUND) {
		dl_first_line = dl_lines_total + 1;
		dl_lines_total = 0x8000 - 2;
	} else {
		dl_first_line = 0;
	}
	dl_lines_done = 0;
	while (dl_lines_total > dl_lines_done) {
		/* We can download only up-to 32 DRAM lines in one go! */
		dl_lines_curr = MIN(chunks_per_read, dl_lines_total - dl_lines_done);

		dl_line = dl_first_line + dl_lines_done;
		dl_line %= 0x8000;
		bufsz = sigma_read_dram(dl_line, dl_lines_curr,
					(uint8_t *)dram_line, devc);
		/* TODO: Check bufsz. For now, just avoid compiler warnings. */
		(void)bufsz;

		/* This is the first DRAM line, so find the initial timestamp. */
		if (dl_lines_done == 0) {
			devc->state.lastts =
				sigma_dram_cluster_ts(&dram_line[0].cluster[0]);
			devc->state.lastsample = 0;
		}

		for (i = 0; i < dl_lines_curr; i++) {
			uint32_t trigger_event = ~0;
			/* The last "DRAM line" can be only partially full. */
			if (dl_lines_done + i == dl_lines_total - 1)
				dl_events_in_line = stoppos & 0x1ff;

			/* Test if the trigger happened on this line. */
			if (dl_lines_done + i == trg_line)
				trigger_event = trg_event;

			decode_chunk_ts(dram_line + i, dl_events_in_line,
					trigger_event, sdi);
		}

		dl_lines_done += dl_lines_curr;
	}

	std_session_send_df_end(sdi);

	sr_dev_acquisition_stop(sdi);

	g_free(dram_line);

	return TRUE;
}

/*
 * Periodically check the Sigma status when in CAPTURE mode. This routine
 * checks whether the configured sample count or sample time have passed,
 * and will stop acquisition and download the acquired samples.
 */
static int sigma_capture_mode(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint64_t running_msec;
	uint64_t current_time;

	devc = sdi->priv;

	/*
	 * Check if the selected sampling duration passed. Sample count
	 * limits are covered by this enforced timeout as well.
	 */
	current_time = g_get_monotonic_time();
	running_msec = (current_time - devc->start_time) / 1000;
	if (running_msec >= devc->limit_msec)
		return download_capture(sdi);

	return TRUE;
}

SR_PRIV int sigma_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;

	if (devc->state.state == SIGMA_IDLE)
		return TRUE;

	if (devc->state.state == SIGMA_CAPTURE)
		return sigma_capture_mode(sdi);

	return TRUE;
}

/* Build a LUT entry used by the trigger functions. */
static void build_lut_entry(uint16_t value, uint16_t mask, uint16_t *entry)
{
	int i, j, k, bit;

	/* For each quad channel. */
	for (i = 0; i < 4; i++) {
		entry[i] = 0xffff;

		/* For each bit in LUT. */
		for (j = 0; j < 16; j++)

			/* For each channel in quad. */
			for (k = 0; k < 4; k++) {
				bit = 1 << (i * 4 + k);

				/* Set bit in entry */
				if ((mask & bit) && ((!(value & bit)) !=
							(!(j & (1 << k)))))
					entry[i] &= ~(1 << j);
			}
	}
}

/* Add a logical function to LUT mask. */
static void add_trigger_function(enum triggerop oper, enum triggerfunc func,
				 int index, int neg, uint16_t *mask)
{
	int i, j;
	int x[2][2], tmp, a, b, aset, bset, rset;

	memset(x, 0, 4 * sizeof(int));

	/* Trigger detect condition. */
	switch (oper) {
	case OP_LEVEL:
		x[0][1] = 1;
		x[1][1] = 1;
		break;
	case OP_NOT:
		x[0][0] = 1;
		x[1][0] = 1;
		break;
	case OP_RISE:
		x[0][1] = 1;
		break;
	case OP_FALL:
		x[1][0] = 1;
		break;
	case OP_RISEFALL:
		x[0][1] = 1;
		x[1][0] = 1;
		break;
	case OP_NOTRISE:
		x[1][1] = 1;
		x[0][0] = 1;
		x[1][0] = 1;
		break;
	case OP_NOTFALL:
		x[1][1] = 1;
		x[0][0] = 1;
		x[0][1] = 1;
		break;
	case OP_NOTRISEFALL:
		x[1][1] = 1;
		x[0][0] = 1;
		break;
	}

	/* Transpose if neg is set. */
	if (neg) {
		for (i = 0; i < 2; i++) {
			for (j = 0; j < 2; j++) {
				tmp = x[i][j];
				x[i][j] = x[1 - i][1 - j];
				x[1 - i][1 - j] = tmp;
			}
		}
	}

	/* Update mask with function. */
	for (i = 0; i < 16; i++) {
		a = (i >> (2 * index + 0)) & 1;
		b = (i >> (2 * index + 1)) & 1;

		aset = (*mask >> i) & 1;
		bset = x[b][a];

		rset = 0;
		if (func == FUNC_AND || func == FUNC_NAND)
			rset = aset & bset;
		else if (func == FUNC_OR || func == FUNC_NOR)
			rset = aset | bset;
		else if (func == FUNC_XOR || func == FUNC_NXOR)
			rset = aset ^ bset;

		if (func == FUNC_NAND || func == FUNC_NOR || func == FUNC_NXOR)
			rset = !rset;

		*mask &= ~(1 << i);

		if (rset)
			*mask |= 1 << i;
	}
}

/*
 * Build trigger LUTs used by 50 MHz and lower sample rates for supporting
 * simple pin change and state triggers. Only two transitions (rise/fall) can be
 * set at any time, but a full mask and value can be set (0/1).
 */
SR_PRIV int sigma_build_basic_trigger(struct triggerlut *lut, struct dev_context *devc)
{
	int i,j;
	uint16_t masks[2] = { 0, 0 };

	memset(lut, 0, sizeof(struct triggerlut));

	/* Constant for simple triggers. */
	lut->m4 = 0xa000;

	/* Value/mask trigger support. */
	build_lut_entry(devc->trigger.simplevalue, devc->trigger.simplemask,
			lut->m2d);

	/* Rise/fall trigger support. */
	for (i = 0, j = 0; i < 16; i++) {
		if (devc->trigger.risingmask & (1 << i) ||
		    devc->trigger.fallingmask & (1 << i))
			masks[j++] = 1 << i;
	}

	build_lut_entry(masks[0], masks[0], lut->m0d);
	build_lut_entry(masks[1], masks[1], lut->m1d);

	/* Add glue logic */
	if (masks[0] || masks[1]) {
		/* Transition trigger. */
		if (masks[0] & devc->trigger.risingmask)
			add_trigger_function(OP_RISE, FUNC_OR, 0, 0, &lut->m3);
		if (masks[0] & devc->trigger.fallingmask)
			add_trigger_function(OP_FALL, FUNC_OR, 0, 0, &lut->m3);
		if (masks[1] & devc->trigger.risingmask)
			add_trigger_function(OP_RISE, FUNC_OR, 1, 0, &lut->m3);
		if (masks[1] & devc->trigger.fallingmask)
			add_trigger_function(OP_FALL, FUNC_OR, 1, 0, &lut->m3);
	} else {
		/* Only value/mask trigger. */
		lut->m3 = 0xffff;
	}

	/* Triggertype: event. */
	lut->params.selres = 3;

	return SR_OK;
}
