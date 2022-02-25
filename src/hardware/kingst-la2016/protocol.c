/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Gerhard Sittig <gerhard.sittig@gmx.net>
 * Copyright (C) 2020 Florian Schmidt <schmidt_florian@gmx.de>
 * Copyright (C) 2013 Marcus Comstedt <marcus@mc.pp.se>
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

#include <config.h>

#include <libsigrok/libsigrok.h>
#include <string.h>

#include "libsigrok-internal.h"
#include "protocol.h"

/* USB PID dependent MCU firmware. Model dependent FPGA bitstream. */
#define MCU_FWFILE_FMT	"kingst-la-%04x.fw"
#define FPGA_FWFILE_FMT	"kingst-%s-fpga.bitstream"

/*
 * List of known devices and their features. See @ref kingst_model
 * for the fields' type and meaning. Table is sorted by EEPROM magic.
 * More specific items need to go first (additional byte[2/6]). Not
 * all devices are covered by this driver implementation, but telling
 * users what was detected is considered useful.
 *
 * TODO Verify the identification of models that were not tested before.
 */
static const struct kingst_model models[] = {
	{  2, 1, "LA2016", "la2016a1", SR_MHZ(200), 16, 1, 0, },
	{  2, 0, "LA2016", "la2016",   SR_MHZ(200), 16, 1, 0, },
	{  3, 1, "LA1016", "la1016a1", SR_MHZ(100), 16, 1, 0, },
	{  3, 0, "LA1016", "la1016",   SR_MHZ(100), 16, 1, 0, },
	{  4, 0, "LA1010", "la1010a0", SR_MHZ(100), 16, 0, SR_MHZ(800), },
	{  5, 0, "LA5016", "la5016a1", SR_MHZ(500), 16, 2, SR_MHZ(800), },
	{  6, 0, "LA5032", "la5032a0", SR_MHZ(500), 32, 4, SR_MHZ(800), },
	{  7, 0, "LA1010", "la1010a1", SR_MHZ(100), 16, 0, SR_MHZ(800), },
	{  8, 0, "LA2016", "la2016a1", SR_MHZ(200), 16, 1, 0, },
	{  9, 0, "LA1016", "la1016a1", SR_MHZ(100), 16, 1, 0, },
	{ 10, 0, "LA1010", "la1010a2", SR_MHZ(100), 16, 0, SR_MHZ(800), },
	{ 65, 0, "LA5016", "la5016a1", SR_MHZ(500), 16, 2, SR_MHZ(800), },
};

/* USB vendor class control requests, executed by the Cypress FX2 MCU. */
#define CMD_FPGA_ENABLE	0x10
#define CMD_FPGA_SPI	0x20	/* R/W access to FPGA registers via SPI. */
#define CMD_BULK_START	0x30	/* Start sample data download via USB EP6 IN. */
#define CMD_BULK_RESET	0x38	/* Flush FIFO of FX2 USB EP6 IN. */
#define CMD_FPGA_INIT	0x50	/* Used before and after FPGA bitstream upload. */
#define CMD_KAUTH	0x60	/* Communicate to auth IC (U10). Not used. */
#define CMD_EEPROM	0xa2	/* R/W access to EEPROM content. */

/*
 * FPGA register addresses (base addresses when registers span multiple
 * bytes, in that case data is kept in little endian format). Passed to
 * CMD_FPGA_SPI requests. The FX2 MCU transparently handles the detail
 * of SPI transfers encoding the read (1) or write (0) direction in the
 * MSB of the address field. There are some 60 byte-wide FPGA registers.
 *
 * Unfortunately the FPGA registers change their meaning between the
 * read and write directions of access, or exclusively provide one of
 * these directions and not the other. This is an arbitrary vendor's
 * choice, there is nothing which the sigrok driver could do about it.
 * Values written to registers typically cannot get read back, neither
 * verified after writing a configuration, nor queried upon startup for
 * automatic detection of the current configuration. Neither appear to
 * be there echo registers for presence and communication checks, nor
 * version identifying registers, as far as we know.
 */
#define REG_RUN		0x00	/* Read capture status, write start capture. */
#define REG_PWM_EN	0x02	/* User PWM channels on/off. */
#define REG_CAPT_MODE	0x03	/* Write 0x00 capture to SDRAM, 0x01 streaming. */
#define REG_PIN_STATE	0x04	/* Read current pin state (real time display). */
#define REG_BULK	0x08	/* Write start addr, byte count to download samples. */
#define REG_SAMPLING	0x10	/* Write capture config, read capture SDRAM location. */
#define REG_TRIGGER	0x20	/* Write level and edge trigger config. */
#define REG_UNKNOWN_30	0x30
#define REG_THRESHOLD	0x68	/* Write PWM config to setup input threshold DAC. */
#define REG_PWM1	0x70	/* Write config for user PWM1. */
#define REG_PWM2	0x78	/* Write config for user PWM2. */

/* Bit patterns to write to REG_CAPT_MODE. */
#define CAPTMODE_TO_RAM	0x00
#define CAPTMODE_STREAM	0x01

/* Bit patterns to write to REG_RUN, setup run mode. */
#define RUNMODE_HALT	0x00
#define RUNMODE_RUN	0x03

/* Bit patterns when reading from REG_RUN, get run state. */
#define RUNSTATE_IDLE_BIT	(1UL << 0)
#define RUNSTATE_DRAM_BIT	(1UL << 1)
#define RUNSTATE_TRGD_BIT	(1UL << 2)
#define RUNSTATE_POST_BIT	(1UL << 3)

static int ctrl_in(const struct sr_dev_inst *sdi,
	uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
	void *data, uint16_t wLength)
{
	struct sr_usb_dev_inst *usb;
	int ret;

	usb = sdi->conn;

	ret = libusb_control_transfer(usb->devhdl,
		LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN,
		bRequest, wValue, wIndex, data, wLength,
		DEFAULT_TIMEOUT_MS);
	if (ret != wLength) {
		sr_dbg("USB ctrl in: %d bytes, req %d val %#x idx %d: %s.",
			wLength, bRequest, wValue, wIndex,
			libusb_error_name(ret));
		sr_err("Cannot read %d bytes from USB: %s.",
			wLength, libusb_error_name(ret));
		return SR_ERR_IO;
	}

	return SR_OK;
}

static int ctrl_out(const struct sr_dev_inst *sdi,
	uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
	void *data, uint16_t wLength)
{
	struct sr_usb_dev_inst *usb;
	int ret;

	usb = sdi->conn;

	ret = libusb_control_transfer(usb->devhdl,
		LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT,
		bRequest, wValue, wIndex, data, wLength,
		DEFAULT_TIMEOUT_MS);
	if (ret != wLength) {
		sr_dbg("USB ctrl out: %d bytes, req %d val %#x idx %d: %s.",
			wLength, bRequest, wValue, wIndex,
			libusb_error_name(ret));
		sr_err("Cannot write %d bytes to USB: %s.",
			wLength, libusb_error_name(ret));
		return SR_ERR_IO;
	}

	return SR_OK;
}

/* HACK Experiment to spot FPGA registers of interest. */
static void la2016_dump_fpga_registers(const struct sr_dev_inst *sdi,
	const char *caption, size_t reg_lower, size_t reg_upper)
{
	static const size_t dump_chunk_len = 16;

	size_t rdlen;
	uint8_t rdbuf[0x80 - 0x00];	/* Span all FPGA registers. */
	const uint8_t *rdptr;
	int ret;
	size_t dump_addr, indent, dump_len;
	GString *txt;

	if (sr_log_loglevel_get() < SR_LOG_SPEW)
		return;

	if (!reg_lower && !reg_upper) {
		reg_lower = 0;
		reg_upper = sizeof(rdbuf);
	}
	if (reg_upper - reg_lower > sizeof(rdbuf))
		reg_upper = sizeof(rdbuf) - reg_lower;

	rdlen = reg_upper - reg_lower;
	ret = ctrl_in(sdi, CMD_FPGA_SPI, reg_lower, 0, rdbuf, rdlen);
	if (ret != SR_OK) {
		sr_err("Cannot get registers space.");
		return;
	}
	rdptr = rdbuf;

	sr_spew("FPGA registers dump: %s", caption ? : "for fun");
	dump_addr = reg_lower;
	while (rdlen) {
		dump_len = rdlen;
		indent = dump_addr % dump_chunk_len;
		if (dump_len > dump_chunk_len)
			dump_len = dump_chunk_len;
		if (dump_len + indent > dump_chunk_len)
			dump_len = dump_chunk_len - indent;
		txt = sr_hexdump_new(rdptr, dump_len);
		sr_spew("  %04zx  %*s%s",
			dump_addr, (int)(3 * indent), "", txt->str);
		sr_hexdump_free(txt);
		dump_addr += dump_len;
		rdptr += dump_len;
		rdlen -= dump_len;
	}
}

/*
 * Check the necessity for FPGA bitstream upload, because another upload
 * would take some 600ms which is undesirable after program startup. Try
 * to access some FPGA registers and check the values' plausibility. The
 * check should fail on the safe side, request another upload when in
 * doubt. A positive response (the request to continue operation with the
 * currently active bitstream) should be conservative. Accessing multiple
 * registers is considered cheap compared to the cost of bitstream upload.
 *
 * It helps though that both the vendor software and the sigrok driver
 * use the same bundle of MCU firmware and FPGA bitstream for any of the
 * supported models. We don't expect to successfully communicate to the
 * device yet disagree on its protocol. Ideally we would access version
 * identifying registers for improved robustness, but are not aware of
 * any. A bitstream reload can always be forced by a power cycle.
 */
static int check_fpga_bitstream(const struct sr_dev_inst *sdi)
{
	uint8_t init_rsp;
	uint8_t buff[REG_PWM_EN - REG_RUN]; /* Larger of REG_RUN, REG_PWM_EN. */
	int ret;
	uint16_t run_state;
	uint8_t pwm_en;
	size_t read_len;
	const uint8_t *rdptr;

	sr_dbg("Checking operation of the FPGA bitstream.");
	la2016_dump_fpga_registers(sdi, "bitstream check", 0, 0);

	init_rsp = ~0;
	ret = ctrl_in(sdi, CMD_FPGA_INIT, 0x00, 0, &init_rsp, sizeof(init_rsp));
	if (ret != SR_OK || init_rsp != 0) {
		sr_dbg("FPGA init query failed, or unexpected response.");
		return SR_ERR_IO;
	}

	read_len = sizeof(run_state);
	ret = ctrl_in(sdi, CMD_FPGA_SPI, REG_RUN, 0, buff, read_len);
	if (ret != SR_OK) {
		sr_dbg("FPGA register access failed (run state).");
		return SR_ERR_IO;
	}
	rdptr = buff;
	run_state = read_u16le_inc(&rdptr);
	sr_spew("FPGA register: run state 0x%04x.", run_state);
	if (run_state && (run_state & 0x3) != 0x1) {
		sr_dbg("Unexpected FPGA register content (run state).");
		return SR_ERR_DATA;
	}
	if (run_state && (run_state & ~0xf) != 0x85e0) {
		sr_dbg("Unexpected FPGA register content (run state).");
		return SR_ERR_DATA;
	}

	read_len = sizeof(pwm_en);
	ret = ctrl_in(sdi, CMD_FPGA_SPI, REG_PWM_EN, 0, buff, read_len);
	if (ret != SR_OK) {
		sr_dbg("FPGA register access failed (PWM enable).");
		return SR_ERR_IO;
	}
	rdptr = buff;
	pwm_en = read_u8_inc(&rdptr);
	sr_spew("FPGA register: PWM enable 0x%02x.", pwm_en);
	if ((pwm_en & 0x3) != 0x0) {
		sr_dbg("Unexpected FPGA register content (PWM enable).");
		return SR_ERR_DATA;
	}

	sr_info("Could re-use current FPGA bitstream. No upload required.");
	return SR_OK;
}

static int upload_fpga_bitstream(const struct sr_dev_inst *sdi,
	const char *bitstream_fname)
{
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct sr_resource bitstream;
	uint32_t bitstream_size;
	uint8_t buffer[sizeof(uint32_t)];
	uint8_t *wrptr;
	uint8_t block[4096];
	int len, act_len;
	unsigned int pos;
	int ret;
	unsigned int zero_pad_to;

	drvc = sdi->driver->context;
	usb = sdi->conn;

	sr_info("Uploading FPGA bitstream '%s'.", bitstream_fname);

	ret = sr_resource_open(drvc->sr_ctx, &bitstream,
		SR_RESOURCE_FIRMWARE, bitstream_fname);
	if (ret != SR_OK) {
		sr_err("Cannot find FPGA bitstream %s.", bitstream_fname);
		return ret;
	}

	bitstream_size = (uint32_t)bitstream.size;
	wrptr = buffer;
	write_u32le_inc(&wrptr, bitstream_size);
	ret = ctrl_out(sdi, CMD_FPGA_INIT, 0x00, 0, buffer, wrptr - buffer);
	if (ret != SR_OK) {
		sr_err("Cannot initiate FPGA bitstream upload.");
		sr_resource_close(drvc->sr_ctx, &bitstream);
		return ret;
	}
	zero_pad_to = bitstream_size;
	zero_pad_to += LA2016_EP2_PADDING - 1;
	zero_pad_to /= LA2016_EP2_PADDING;
	zero_pad_to *= LA2016_EP2_PADDING;

	pos = 0;
	while (1) {
		if (pos < bitstream.size) {
			len = (int)sr_resource_read(drvc->sr_ctx, &bitstream,
				block, sizeof(block));
			if (len < 0) {
				sr_err("Cannot read FPGA bitstream.");
				sr_resource_close(drvc->sr_ctx, &bitstream);
				return SR_ERR_IO;
			}
		} else {
			/*  Zero-pad until 'zero_pad_to'. */
			len = zero_pad_to - pos;
			if ((unsigned)len > sizeof(block))
				len = sizeof(block);
			memset(&block, 0, len);
		}
		if (len == 0)
			break;

		ret = libusb_bulk_transfer(usb->devhdl, USB_EP_FPGA_BITSTREAM,
			&block[0], len, &act_len, DEFAULT_TIMEOUT_MS);
		if (ret != 0) {
			sr_dbg("Cannot write FPGA bitstream, block %#x len %d: %s.",
				pos, (int)len, libusb_error_name(ret));
			ret = SR_ERR_IO;
			break;
		}
		if (act_len != len) {
			sr_dbg("Short write for FPGA bitstream, block %#x len %d: got %d.",
				pos, (int)len, act_len);
			ret = SR_ERR_IO;
			break;
		}
		pos += len;
	}
	sr_resource_close(drvc->sr_ctx, &bitstream);
	if (ret != SR_OK)
		return ret;
	sr_info("FPGA bitstream upload (%" PRIu64 " bytes) done.",
		bitstream.size);

	return SR_OK;
}

static int enable_fpga_bitstream(const struct sr_dev_inst *sdi)
{
	int ret;
	uint8_t resp;

	ret = ctrl_in(sdi, CMD_FPGA_INIT, 0x00, 0, &resp, sizeof(resp));
	if (ret != SR_OK) {
		sr_err("Cannot read response after FPGA bitstream upload.");
		return ret;
	}
	if (resp != 0) {
		sr_err("Unexpected FPGA bitstream upload response, got 0x%02x, want 0.",
			resp);
		return SR_ERR_DATA;
	}
	g_usleep(30 * 1000);

	ret = ctrl_out(sdi, CMD_FPGA_ENABLE, 0x01, 0, NULL, 0);
	if (ret != SR_OK) {
		sr_err("Cannot enable FPGA after bitstream upload.");
		return ret;
	}
	g_usleep(40 * 1000);

	return SR_OK;
}

static int set_threshold_voltage(const struct sr_dev_inst *sdi, float voltage)
{
	int ret;
	uint16_t duty_R79, duty_R56;
	uint8_t buf[REG_PWM1 - REG_THRESHOLD]; /* Width of REG_THRESHOLD. */
	uint8_t *wrptr;

	/* Clamp threshold setting to valid range for LA2016. */
	if (voltage > LA2016_THR_VOLTAGE_MAX) {
		voltage = LA2016_THR_VOLTAGE_MAX;
	} else if (voltage < -LA2016_THR_VOLTAGE_MAX) {
		voltage = -LA2016_THR_VOLTAGE_MAX;
	}

	/*
	 * Two PWM output channels feed one DAC which generates a bias
	 * voltage, which offsets the input probe's voltage level, and
	 * in combination with the FPGA pins' fixed threshold result in
	 * a programmable input threshold from the user's perspective.
	 * The PWM outputs can be seen on R79 and R56 respectively, the
	 * frequency is 100kHz and the duty cycle varies. The R79 PWM
	 * uses three discrete settings. The R56 PWM varies with desired
	 * thresholds and depends on the R79 PWM configuration. See the
	 * schematics comments which discuss the formulae.
	 */
	if (voltage >= 2.9) {
		duty_R79 = 0;		/* PWM off (0V). */
		duty_R56 = (uint16_t)(302 * voltage - 363);
	} else if (voltage > -0.4) {
		duty_R79 = 0x00f2;	/* 25% duty cycle. */
		duty_R56 = (uint16_t)(302 * voltage + 121);
	} else {
		duty_R79 = 0x02d7;	/* 72% duty cycle. */
		duty_R56 = (uint16_t)(302 * voltage + 1090);
	}

	/* Clamp duty register values to sensible limits. */
	if (duty_R56 < 10) {
		duty_R56 = 10;
	} else if (duty_R56 > 1100) {
		duty_R56 = 1100;
	}

	sr_dbg("Set threshold voltage %.2fV.", voltage);
	sr_dbg("Duty cycle values: R56 0x%04x, R79 0x%04x.", duty_R56, duty_R79);

	wrptr = buf;
	write_u16le_inc(&wrptr, duty_R56);
	write_u16le_inc(&wrptr, duty_R79);

	ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_THRESHOLD, 0, buf, wrptr - buf);
	if (ret != SR_OK) {
		sr_err("Cannot set threshold voltage %.2fV.", voltage);
		return ret;
	}

	return SR_OK;
}

/*
 * Communicates a channel's configuration to the device after the
 * parameters may have changed. Configuration of one channel may
 * interfere with other channels since they share FPGA registers.
 */
static int set_pwm_config(const struct sr_dev_inst *sdi, size_t idx)
{
	static uint8_t reg_bases[] = { REG_PWM1, REG_PWM2, };

	struct dev_context *devc;
	struct pwm_setting *params;
	uint8_t reg_base;
	double val_f;
	uint32_t val_u;
	uint32_t period, duty;
	size_t ch;
	int ret;
	uint8_t enable_all, enable_cfg, reg_val;
	uint8_t buf[REG_PWM2 - REG_PWM1]; /* Width of one REG_PWMx. */
	uint8_t *wrptr;

	devc = sdi->priv;
	if (idx >= ARRAY_SIZE(devc->pwm_setting))
		return SR_ERR_ARG;
	params = &devc->pwm_setting[idx];
	if (idx >= ARRAY_SIZE(reg_bases))
		return SR_ERR_ARG;
	reg_base = reg_bases[idx];

	/*
	 * Map application's specs to hardware register values. Do math
	 * in floating point initially, but convert to u32 eventually.
	 */
	sr_dbg("PWM config, app spec, ch %zu, en %d, freq %.1f, duty %.1f.",
		idx, params->enabled ? 1 : 0, params->freq, params->duty);
	val_f = PWM_CLOCK;
	val_f /= params->freq;
	val_u = val_f;
	period = val_u;
	val_f = period;
	val_f *= params->duty;
	val_f /= 100.0;
	val_f += 0.5;
	val_u = val_f;
	duty = val_u;
	sr_dbg("PWM config, reg 0x%04x, freq %u, duty %u.",
		(unsigned)reg_base, (unsigned)period, (unsigned)duty);

	/* Get the "enabled" state of all supported PWM channels. */
	enable_all = 0;
	for (ch = 0; ch < ARRAY_SIZE(devc->pwm_setting); ch++) {
		if (!devc->pwm_setting[ch].enabled)
			continue;
		enable_all |= 1U << ch;
	}
	enable_cfg = 1U << idx;
	sr_spew("PWM config, enable all 0x%02hhx, cfg 0x%02hhx.",
		enable_all, enable_cfg);

	/*
	 * Disable the to-get-configured channel before its parameters
	 * will change. Or disable and exit when the channel is supposed
	 * to get turned off.
	 */
	sr_spew("PWM config, disabling before param change.");
	reg_val = enable_all & ~enable_cfg;
	ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_PWM_EN, 0,
		&reg_val, sizeof(reg_val));
	if (ret != SR_OK) {
		sr_err("Cannot adjust PWM enabled state.");
		return ret;
	}
	if (!params->enabled)
		return SR_OK;

	/* Write register values to device. */
	sr_spew("PWM config, sending new parameters.");
	wrptr = buf;
	write_u32le_inc(&wrptr, period);
	write_u32le_inc(&wrptr, duty);
	ret = ctrl_out(sdi, CMD_FPGA_SPI, reg_base, 0, buf, wrptr - buf);
	if (ret != SR_OK) {
		sr_err("Cannot change PWM parameters.");
		return ret;
	}

	/* Enable configured channel after write completion. */
	sr_spew("PWM config, enabling after param change.");
	reg_val = enable_all | enable_cfg;
	ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_PWM_EN, 0,
		&reg_val, sizeof(reg_val));
	if (ret != SR_OK) {
		sr_err("Cannot adjust PWM enabled state.");
		return ret;
	}

	return SR_OK;
}

/*
 * Determine the number of enabled channels as well as their bitmask
 * representation. Derive data here which later simplifies processing
 * of raw capture data memory content in streaming mode.
 */
static void la2016_prepare_stream(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct stream_state_t *stream;
	size_t channel_mask;
	GSList *l;
	struct sr_channel *ch;

	devc = sdi->priv;
	stream = &devc->stream;
	memset(stream, 0, sizeof(*stream));

	stream->enabled_count = 0;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_LOGIC)
			continue;
		if (!ch->enabled)
			continue;
		channel_mask = 1UL << ch->index;
		stream->enabled_mask |= channel_mask;
		stream->channel_masks[stream->enabled_count++] = channel_mask;
	}
	stream->channel_index = 0;
}

/*
 * This routine configures the set of enabled channels, as well as the
 * trigger condition (if one was specified). Also prepares the capture
 * data processing in stream mode, where the memory layout dramatically
 * differs from normal mode.
 */
static int set_trigger_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct trigger_cfg {
		uint32_t channels;	/* Actually: Enabled channels? */
		uint32_t enabled;	/* Actually: Triggering channels? */
		uint32_t level;
		uint32_t high_or_falling;
	} cfg;
	GSList *stages;
	GSList *channel;
	struct sr_trigger_stage *stage1;
	struct sr_trigger_match *match;
	uint32_t ch_mask;
	int ret;
	uint8_t buf[REG_UNKNOWN_30 - REG_TRIGGER]; /* Width of REG_TRIGGER. */
	uint8_t *wrptr;

	devc = sdi->priv;

	la2016_prepare_stream(sdi);

	memset(&cfg, 0, sizeof(cfg));
	cfg.channels = devc->stream.enabled_mask;
	if (!cfg.channels) {
		sr_err("Need at least one enabled logic channel.");
		return SR_ERR_ARG;
	}
	trigger = sr_session_trigger_get(sdi->session);
	if (trigger && trigger->stages) {
		stages = trigger->stages;
		stage1 = stages->data;
		if (stages->next) {
			sr_err("Only one trigger stage supported for now.");
			return SR_ERR_ARG;
		}
		channel = stage1->matches;
		while (channel) {
			match = channel->data;
			ch_mask = 1UL << match->channel->index;

			switch (match->match) {
			case SR_TRIGGER_ZERO:
				cfg.level |= ch_mask;
				cfg.high_or_falling &= ~ch_mask;
				break;
			case SR_TRIGGER_ONE:
				cfg.level |= ch_mask;
				cfg.high_or_falling |= ch_mask;
				break;
			case SR_TRIGGER_RISING:
				if ((cfg.enabled & ~cfg.level)) {
					sr_err("Device only supports one edge trigger.");
					return SR_ERR_ARG;
				}
				cfg.level &= ~ch_mask;
				cfg.high_or_falling &= ~ch_mask;
				break;
			case SR_TRIGGER_FALLING:
				if ((cfg.enabled & ~cfg.level)) {
					sr_err("Device only supports one edge trigger.");
					return SR_ERR_ARG;
				}
				cfg.level &= ~ch_mask;
				cfg.high_or_falling |= ch_mask;
				break;
			default:
				sr_err("Unknown trigger condition.");
				return SR_ERR_ARG;
			}
			cfg.enabled |= ch_mask;
			channel = channel->next;
		}
	}
	sr_dbg("Set trigger config: "
		"enabled-channels 0x%04x, triggering-channels 0x%04x, "
		"level-triggered 0x%04x, high/falling 0x%04x.",
		cfg.channels, cfg.enabled, cfg.level, cfg.high_or_falling);

	/*
	 * Don't configure hardware trigger parameters in streaming mode
	 * or when the device lacks local memory. Yet the above dump of
	 * derived parameters from user specs is considered valueable.
	 *
	 * TODO Add support for soft triggers when hardware triggers in
	 * the device are not used or are not available at all.
	 */
	if (!devc->model->memory_bits || devc->continuous) {
		if (!devc->model->memory_bits)
			sr_dbg("Device without memory. No hardware triggers.");
		else if (devc->continuous)
			sr_dbg("Streaming mode. No hardware triggers.");
		cfg.enabled = 0;
		cfg.level = 0;
		cfg.high_or_falling = 0;
	}

	devc->trigger_involved = cfg.enabled != 0;

	wrptr = buf;
	write_u32le_inc(&wrptr, cfg.channels);
	write_u32le_inc(&wrptr, cfg.enabled);
	write_u32le_inc(&wrptr, cfg.level);
	write_u32le_inc(&wrptr, cfg.high_or_falling);
	/* TODO
	 * Comment on this literal 16. Origin, meaning? Cannot be the
	 * register offset, nor the transfer length. Is it a channels
	 * count that is relevant for 16 and 32 channel models? Is it
	 * an obsolete experiment?
	 */
	ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_TRIGGER, 16, buf, wrptr - buf);
	if (ret != SR_OK) {
		sr_err("Cannot setup trigger configuration.");
		return ret;
	}

	return SR_OK;
}

/*
 * This routine communicates the sample configuration to the device:
 * Total samples count and samplerate, pre-trigger configuration.
 */
static int set_sample_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint64_t baseclock;
	uint64_t min_samplerate, eff_samplerate;
	uint64_t stream_bandwidth;
	uint16_t divider_u16;
	uint64_t limit_samples;
	uint64_t pre_trigger_samples;
	uint64_t pre_trigger_memory;
	uint8_t buf[REG_TRIGGER - REG_SAMPLING]; /* Width of REG_SAMPLING. */
	uint8_t *wrptr;
	int ret;

	devc = sdi->priv;

	/*
	 * The base clock need not be identical to the maximum samplerate,
	 * and differs between models. The 500MHz devices even use a base
	 * clock of 800MHz, and communicate divider 1 to the hardware to
	 * configure the 500MHz samplerate. This allows them to operate at
	 * a 200MHz samplerate which uses divider 4.
	 */
	if (devc->samplerate > devc->model->samplerate) {
		sr_err("Too high a sample rate: %" PRIu64 ".",
			devc->samplerate);
		return SR_ERR_ARG;
	}
	baseclock = devc->model->baseclock;
	if (!baseclock)
		baseclock = devc->model->samplerate;
	min_samplerate = baseclock;
	min_samplerate /= 65536;
	if (devc->samplerate < min_samplerate) {
		sr_err("Too low a sample rate: %" PRIu64 ".",
			devc->samplerate);
		return SR_ERR_ARG;
	}
	divider_u16 = baseclock / devc->samplerate;
	eff_samplerate = baseclock / divider_u16;
	if (eff_samplerate > devc->model->samplerate)
		eff_samplerate = devc->model->samplerate;

	ret = sr_sw_limits_get_remain(&devc->sw_limits,
		&limit_samples, NULL, NULL, NULL);
	if (ret != SR_OK) {
		sr_err("Cannot get acquisition limits.");
		return ret;
	}
	if (limit_samples > LA2016_NUM_SAMPLES_MAX) {
		sr_warn("Too high a sample depth: %" PRIu64 ", capping.",
			limit_samples);
		limit_samples = LA2016_NUM_SAMPLES_MAX;
	}
	if (limit_samples == 0) {
		limit_samples = LA2016_NUM_SAMPLES_MAX;
		sr_dbg("Passing %" PRIu64 " to HW for unlimited samples.",
			limit_samples);
	}

	/*
	 * The acquisition configuration communicates "pre-trigger"
	 * specs in several formats. sigrok users provide a percentage
	 * (0-100%), which translates to a pre-trigger samples count
	 * (assuming that a total samples count limit was specified).
	 * The device supports hardware compression, which depends on
	 * slowly changing input data to be effective. Fast changing
	 * input data may occupy more space in sample memory than its
	 * uncompressed form would. This is why a third parameter can
	 * limit the amount of sample memory to use for pre-trigger
	 * data. Only the upper 24 bits of that memory size spec get
	 * communicated to the device (written to its FPGA register).
	 */
	if (!devc->model->memory_bits) {
		sr_dbg("Memory-less device, skipping pre-trigger config.");
		pre_trigger_samples = 0;
		pre_trigger_memory = 0;
	} else if (devc->trigger_involved) {
		pre_trigger_samples = limit_samples;
		pre_trigger_samples *= devc->capture_ratio;
		pre_trigger_samples /= 100;
		pre_trigger_memory = devc->model->memory_bits;
		pre_trigger_memory *= UINT64_C(1024 * 1024 * 1024);
		pre_trigger_memory /= 8; /* devc->model->channel_count ? */
		pre_trigger_memory *= devc->capture_ratio;
		pre_trigger_memory /= 100;
	} else {
		sr_dbg("No trigger setup, skipping pre-trigger config.");
		pre_trigger_samples = 0;
		pre_trigger_memory = 0;
	}
	/* Ensure non-zero value after LSB shift out in HW reg. */
	if (pre_trigger_memory < 0x100)
		pre_trigger_memory = 0x100;

	sr_dbg("Set sample config: %" PRIu64 "kHz (div %" PRIu16 "), %" PRIu64 " samples.",
		eff_samplerate / SR_KHZ(1), divider_u16, limit_samples);
	sr_dbg("Capture ratio %" PRIu64 "%%, count %" PRIu64 ", mem %" PRIu64 ".",
		devc->capture_ratio, pre_trigger_samples, pre_trigger_memory);

	if (devc->continuous) {
		stream_bandwidth = eff_samplerate;
		stream_bandwidth *= devc->stream.enabled_count;
		sr_dbg("Streaming: channel count %zu, product %" PRIu64 ".",
			devc->stream.enabled_count, stream_bandwidth);
		stream_bandwidth /= 1000 * 1000;
		if (stream_bandwidth >= LA2016_STREAM_MBPS_MAX) {
			sr_warn("High USB stream bandwidth: %" PRIu64 "Mbps.",
				stream_bandwidth);
		}
		if (stream_bandwidth < LA2016_STREAM_PUSH_THR) {
			sr_dbg("Streaming: low Mbps, suggest periodic flush.");
			devc->stream.flush_period_ms = LA2016_STREAM_PUSH_IVAL;
		}
	}

	/*
	 * The acquisition configuration occupies a total of 16 bytes:
	 * - A 34bit total samples count limit (up to 10 billions) that
	 *   is kept in a 40bit register.
	 * - A 34bit pre-trigger samples count limit (up to 10 billions)
	 *   in another 40bit register.
	 * - A 32bit pre-trigger memory space limit (in bytes) of which
	 *   the upper 24bits are kept in an FPGA register.
	 * - A 16bit clock divider which gets applied to the maximum
	 *   samplerate of the device.
	 * - An 8bit register of unknown meaning. Currently always 0.
	 */
	wrptr = buf;
	write_u40le_inc(&wrptr, limit_samples);
	write_u40le_inc(&wrptr, pre_trigger_samples);
	write_u24le_inc(&wrptr, pre_trigger_memory >> 8);
	write_u16le_inc(&wrptr, divider_u16);
	write_u8_inc(&wrptr, 0);
	ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_SAMPLING, 0, buf, wrptr - buf);
	if (ret != SR_OK) {
		sr_err("Cannot setup acquisition configuration.");
		return ret;
	}

	return SR_OK;
}

/*
 * FPGA register REG_RUN holds the run state (u16le format). Bit fields
 * of interest:
 *   bit 0: value 1 = idle
 *   bit 1: value 1 = writing to SDRAM
 *   bit 2: value 0 = waiting for trigger, 1 = trigger seen
 *   bit 3: value 0 = pretrigger sampling, 1 = posttrigger sampling
 * The meaning of other bit fields is unknown.
 *
 * Typical values in order of appearance during execution:
 *   0x85e1: idle, no acquisition pending
 *     IDLE set, TRGD don't care, POST don't care; DRAM don't care
 *     "In idle state." Takes precedence over all others.
 *   0x85e2: pre-sampling, samples before the trigger position,
 *     when capture ratio > 0%
 *     IDLE clear, TRGD clear, POST clear; DRAM don't care
 *     "Not idle any more, no post yet, not triggered yet."
 *   0x85ea: pre-sampling complete, now waiting for the trigger
 *     (whilst sampling continuously)
 *     IDLE clear, TRGD clear, POST set; DRAM don't care
 *     "Post set thus after pre, not triggered yet"
 *   0x85ee: trigger seen, capturing post-trigger samples, running
 *     IDLE clear, TRGD set, POST set; DRAM don't care
 *     "Triggered and in post, not idle yet."
 *   0x85ed: idle
 *     IDLE set, TRGD don't care, POST don't care; DRAM don't care
 *     "In idle state." TRGD/POST don't care, same meaning as above.
 */
static const uint16_t runstate_mask_idle = RUNSTATE_IDLE_BIT;
static const uint16_t runstate_patt_idle = RUNSTATE_IDLE_BIT;
static const uint16_t runstate_mask_step =
	RUNSTATE_IDLE_BIT | RUNSTATE_TRGD_BIT | RUNSTATE_POST_BIT;
static const uint16_t runstate_patt_pre_trig = 0;
static const uint16_t runstate_patt_wait_trig = RUNSTATE_POST_BIT;
static const uint16_t runstate_patt_post_trig =
	RUNSTATE_TRGD_BIT | RUNSTATE_POST_BIT;

static uint16_t run_state(const struct sr_dev_inst *sdi)
{
	static uint16_t previous_state;

	int ret;
	uint16_t state;
	uint8_t buff[REG_PWM_EN - REG_RUN]; /* Width of REG_RUN. */
	const uint8_t *rdptr;
	const char *label;

	ret = ctrl_in(sdi, CMD_FPGA_SPI, REG_RUN, 0, buff, sizeof(state));
	if (ret != SR_OK) {
		sr_err("Cannot read run state.");
		return ret;
	}
	rdptr = buff;
	state = read_u16le_inc(&rdptr);

	/*
	 * Avoid flooding the log, only dump values as they change.
	 * The routine is called about every 50ms.
	 */
	if (state == previous_state)
		return state;

	previous_state = state;
	label = NULL;
	if ((state & runstate_mask_idle) == runstate_patt_idle)
		label = "idle";
	if ((state & runstate_mask_step) == runstate_patt_pre_trig)
		label = "pre-trigger sampling";
	if ((state & runstate_mask_step) == runstate_patt_wait_trig)
		label = "sampling, waiting for trigger";
	if ((state & runstate_mask_step) == runstate_patt_post_trig)
		label = "post-trigger sampling";
	if (label && *label)
		sr_dbg("Run state: 0x%04x (%s).", state, label);
	else
		sr_dbg("Run state: 0x%04x.", state);

	return state;
}

static gboolean la2016_is_idle(const struct sr_dev_inst *sdi)
{
	uint16_t state;

	state = run_state(sdi);
	if ((state & runstate_mask_idle) == runstate_patt_idle)
		return TRUE;

	return FALSE;
}

static int set_run_mode(const struct sr_dev_inst *sdi, uint8_t mode)
{
	int ret;

	ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_RUN, 0, &mode, sizeof(mode));
	if (ret != SR_OK) {
		sr_err("Cannot configure run mode %d.", mode);
		return ret;
	}

	return SR_OK;
}

static int get_capture_info(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	uint8_t buf[REG_TRIGGER - REG_SAMPLING]; /* Width of REG_SAMPLING. */
	const uint8_t *rdptr;

	devc = sdi->priv;

	ret = ctrl_in(sdi, CMD_FPGA_SPI, REG_SAMPLING, 0, buf, sizeof(buf));
	if (ret != SR_OK) {
		sr_err("Cannot read capture info.");
		return ret;
	}

	rdptr = buf;
	devc->info.n_rep_packets = read_u32le_inc(&rdptr);
	devc->info.n_rep_packets_before_trigger = read_u32le_inc(&rdptr);
	devc->info.write_pos = read_u32le_inc(&rdptr);

	sr_dbg("Capture info: n_rep_packets: 0x%08x/%d, before_trigger: 0x%08x/%d, write_pos: 0x%08x/%d.",
		devc->info.n_rep_packets, devc->info.n_rep_packets,
		devc->info.n_rep_packets_before_trigger,
		devc->info.n_rep_packets_before_trigger,
		devc->info.write_pos, devc->info.write_pos);

	if (devc->info.n_rep_packets % devc->packets_per_chunk) {
		sr_warn("Unexpected packets count %lu, not a multiple of %lu.",
			(unsigned long)devc->info.n_rep_packets,
			(unsigned long)devc->packets_per_chunk);
	}

	return SR_OK;
}

SR_PRIV int la2016_upload_firmware(const struct sr_dev_inst *sdi,
	struct sr_context *sr_ctx, libusb_device *dev, gboolean skip_upload)
{
	struct dev_context *devc;
	uint16_t pid;
	char *fw;
	int ret;

	devc = sdi ? sdi->priv : NULL;
	if (!devc || !devc->usb_pid)
		return SR_ERR_ARG;
	pid = devc->usb_pid;

	fw = g_strdup_printf(MCU_FWFILE_FMT, pid);
	sr_info("USB PID %04hx, MCU firmware '%s'.", pid, fw);
	devc->mcu_firmware = g_strdup(fw);

	if (skip_upload)
		ret = SR_OK;
	else
		ret = ezusb_upload_firmware(sr_ctx, dev, USB_CONFIGURATION, fw);
	g_free(fw);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *xfer);

static void la2016_usbxfer_release_cb(gpointer p)
{
	struct libusb_transfer *xfer;

	xfer = p;
	g_free(xfer->buffer);
	libusb_free_transfer(xfer);
}

static int la2016_usbxfer_release(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi ? sdi->priv : NULL;
	if (!devc)
		return SR_ERR_ARG;

	/* Release all USB transfers. */
	g_slist_free_full(devc->transfers, la2016_usbxfer_release_cb);
	devc->transfers = NULL;

	return SR_OK;
}

static int la2016_usbxfer_allocate(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	size_t bufsize, xfercount;
	uint8_t *buffer;
	struct libusb_transfer *xfer;

	devc = sdi ? sdi->priv : NULL;
	if (!devc)
		return SR_ERR_ARG;

	/* Transfers were already allocated before? */
	if (devc->transfers)
		return SR_OK;

	/*
	 * Allocate all USB transfers and their buffers. Arrange for a
	 * buffer size which is within the device's capabilities, and
	 * is a multiple of the USB endpoint's size, to make use of the
	 * RAW_IO performance feature.
	 *
	 * Implementation detail: The LA2016_USB_BUFSZ value happens
	 * to match all those constraints. No additional arithmetics is
	 * required in this location.
	 */
	bufsize = LA2016_USB_BUFSZ;
	xfercount = LA2016_USB_XFER_COUNT;
	while (xfercount--) {
		buffer = g_try_malloc(bufsize);
		if (!buffer) {
			sr_err("Cannot allocate USB transfer buffer.");
			return SR_ERR_MALLOC;
		}
		xfer = libusb_alloc_transfer(0);
		if (!xfer) {
			sr_err("Cannot allocate USB transfer.");
			g_free(buffer);
			return SR_ERR_MALLOC;
		}
		xfer->buffer = buffer;
		devc->transfers = g_slist_append(devc->transfers, xfer);
	}
	devc->transfer_bufsize = bufsize;

	return SR_OK;
}

static int la2016_usbxfer_cancel_all(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	GSList *l;
	struct libusb_transfer *xfer;

	devc = sdi ? sdi->priv : NULL;
	if (!devc)
		return SR_ERR_ARG;

	/* Unconditionally cancel the transfer. Ignore errors. */
	for (l = devc->transfers; l; l = l->next) {
		xfer = l->data;
		if (!xfer)
			continue;
		libusb_cancel_transfer(xfer);
	}

	return SR_OK;
}

static int la2016_usbxfer_resubmit(const struct sr_dev_inst *sdi,
	struct libusb_transfer *xfer)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	libusb_transfer_cb_fn cb;
	int ret;

	devc = sdi ? sdi->priv : NULL;
	usb = sdi ? sdi->conn : NULL;
	if (!devc || !usb)
		return SR_ERR_ARG;

	if (!xfer)
		return SR_ERR_ARG;

	cb = receive_transfer;
	libusb_fill_bulk_transfer(xfer, usb->devhdl,
		USB_EP_CAPTURE_DATA | LIBUSB_ENDPOINT_IN,
		xfer->buffer, devc->transfer_bufsize,
		cb, (void *)sdi, CAPTURE_TIMEOUT_MS);
	ret = libusb_submit_transfer(xfer);
	if (ret != 0) {
		sr_err("Cannot submit USB transfer: %s.",
			libusb_error_name(ret));
		return SR_ERR_IO;
	}

	return SR_OK;
}

static int la2016_usbxfer_submit_all(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	GSList *l;
	struct libusb_transfer *xfer;
	int ret;

	devc = sdi ? sdi->priv : NULL;
	if (!devc)
		return SR_ERR_ARG;

	for (l = devc->transfers; l; l = l->next) {
		xfer = l->data;
		if (!xfer)
			return SR_ERR_ARG;
		ret = la2016_usbxfer_resubmit(sdi, xfer);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

SR_PRIV int la2016_setup_acquisition(const struct sr_dev_inst *sdi,
	double voltage)
{
	struct dev_context *devc;
	int ret;
	uint8_t cmd;

	devc = sdi->priv;

	ret = set_threshold_voltage(sdi, voltage);
	if (ret != SR_OK)
		return ret;

	cmd = devc->continuous ? CAPTMODE_STREAM : CAPTMODE_TO_RAM;
	ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_CAPT_MODE, 0, &cmd, sizeof(cmd));
	if (ret != SR_OK) {
		sr_err("Cannot send command to stop sampling.");
		return ret;
	}

	ret = set_trigger_config(sdi);
	if (ret != SR_OK)
		return ret;

	ret = set_sample_config(sdi);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

SR_PRIV int la2016_start_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	ret = la2016_usbxfer_allocate(sdi);
	if (ret != SR_OK)
		return ret;

	if (devc->continuous) {
		ret = ctrl_out(sdi, CMD_BULK_RESET, 0x00, 0, NULL, 0);
		if (ret != SR_OK)
			return ret;

		ret = la2016_usbxfer_submit_all(sdi);
		if (ret != SR_OK)
			return ret;

		/*
		 * Periodic receive callback will set runmode. This
		 * activity MUST be close to data reception, a pause
		 * between these steps breaks the stream's operation.
		 */
	} else {
		ret = set_run_mode(sdi, RUNMODE_RUN);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

static int la2016_stop_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	ret = set_run_mode(sdi, RUNMODE_HALT);
	if (ret != SR_OK)
		return ret;

	devc = sdi->priv;
	if (devc->continuous)
		devc->download_finished = TRUE;

	return SR_OK;
}

SR_PRIV int la2016_abort_acquisition(const struct sr_dev_inst *sdi)
{
	int ret;

	ret = la2016_stop_acquisition(sdi);
	if (ret != SR_OK)
		return ret;

	(void)la2016_usbxfer_cancel_all(sdi);

	return SR_OK;
}

static int la2016_start_download(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	uint8_t wrbuf[REG_SAMPLING - REG_BULK]; /* Width of REG_BULK. */
	uint8_t *wrptr;

	devc = sdi->priv;

	ret = get_capture_info(sdi);
	if (ret != SR_OK)
		return ret;

	devc->n_transfer_packets_to_read = devc->info.n_rep_packets;
	devc->n_transfer_packets_to_read /= devc->packets_per_chunk;
	devc->n_bytes_to_read = devc->n_transfer_packets_to_read;
	devc->n_bytes_to_read *= TRANSFER_PACKET_LENGTH;
	devc->read_pos = devc->info.write_pos - devc->n_bytes_to_read;
	devc->n_reps_until_trigger = devc->info.n_rep_packets_before_trigger;

	sr_dbg("Want to read %u xfer-packets starting from pos %" PRIu32 ".",
		devc->n_transfer_packets_to_read, devc->read_pos);

	ret = ctrl_out(sdi, CMD_BULK_RESET, 0x00, 0, NULL, 0);
	if (ret != SR_OK) {
		sr_err("Cannot reset USB bulk state.");
		return ret;
	}
	sr_dbg("Will read from 0x%08lx, 0x%08x bytes.",
		(unsigned long)devc->read_pos, devc->n_bytes_to_read);
	wrptr = wrbuf;
	write_u32le_inc(&wrptr, devc->read_pos);
	write_u32le_inc(&wrptr, devc->n_bytes_to_read);
	ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_BULK, 0, wrbuf, wrptr - wrbuf);
	if (ret != SR_OK) {
		sr_err("Cannot send USB bulk config.");
		return ret;
	}

	ret = la2016_usbxfer_submit_all(sdi);
	if (ret != SR_OK) {
		sr_err("Cannot submit USB bulk transfers.");
		return ret;
	}

	ret = ctrl_out(sdi, CMD_BULK_START, 0x00, 0, NULL, 0);
	if (ret != SR_OK) {
		sr_err("Cannot start USB bulk transfers.");
		return ret;
	}

	return SR_OK;
}

/*
 * A chunk (received via USB) contains a number of transfers (USB length
 * divided by 16) which contain a number of packets (5 per transfer) which
 * contain a number of samples (8bit repeat count per 16bit sample data).
 */
static void send_chunk(struct sr_dev_inst *sdi,
	const uint8_t *data_buffer, size_t data_length)
{
	struct dev_context *devc;
	size_t num_xfers, num_pkts;
	const uint8_t *rp;
	uint32_t sample_value;
	size_t repetitions;
	uint8_t sample_buff[sizeof(sample_value)];

	devc = sdi->priv;

	/* Ignore incoming USB data after complete sample data download. */
	if (devc->download_finished)
		return;

	if (devc->trigger_involved && !devc->trigger_marked && devc->info.n_rep_packets_before_trigger == 0) {
		feed_queue_logic_send_trigger(devc->feed_queue);
		devc->trigger_marked = TRUE;
	}

	/*
	 * Adjust the number of remaining bytes to read from the device
	 * before the processing of the currently received chunk affects
	 * the variable which holds the number of received bytes.
	 */
	if (data_length > devc->n_bytes_to_read)
		devc->n_bytes_to_read = 0;
	else
		devc->n_bytes_to_read -= data_length;

	/* Process the received chunk of capture data. */
	sample_value = 0;
	rp = data_buffer;
	num_xfers = data_length / TRANSFER_PACKET_LENGTH;
	while (num_xfers--) {
		num_pkts = devc->packets_per_chunk;
		while (num_pkts--) {

			/* TODO Verify 32channel layout. */
			if (devc->model->channel_count == 32)
				sample_value = read_u32le_inc(&rp);
			else if (devc->model->channel_count == 16)
				sample_value = read_u16le_inc(&rp);
			repetitions = read_u8_inc(&rp);

			devc->total_samples += repetitions;

			write_u32le(sample_buff, sample_value);
			feed_queue_logic_submit(devc->feed_queue,
				sample_buff, repetitions);
			sr_sw_limits_update_samples_read(&devc->sw_limits,
				repetitions);

			if (devc->trigger_involved && !devc->trigger_marked) {
				if (!--devc->n_reps_until_trigger) {
					feed_queue_logic_send_trigger(devc->feed_queue);
					devc->trigger_marked = TRUE;
					sr_dbg("Trigger position after %" PRIu64 " samples, %.6fms.",
						devc->total_samples,
						(double)devc->total_samples / devc->samplerate * 1e3);
				}
			}
		}
		(void)read_u8_inc(&rp); /* Skip sequence number. */
	}

	/*
	 * Check for several conditions which shall terminate the
	 * capture data download: When the amount of capture data in
	 * the device is exhausted. When the user specified samples
	 * count limit is reached.
	 */
	if (!devc->n_bytes_to_read) {
		devc->download_finished = TRUE;
	} else {
		sr_dbg("%" PRIu32 " more bytes to download from the device.",
			devc->n_bytes_to_read);
	}
	if (!devc->download_finished && sr_sw_limits_check(&devc->sw_limits)) {
		sr_dbg("Acquisition limit reached.");
		devc->download_finished = TRUE;
	}
	if (devc->download_finished) {
		sr_dbg("Download finished, flushing session feed queue.");
		feed_queue_logic_flush(devc->feed_queue);
	}
	sr_dbg("Total samples after chunk: %" PRIu64 ".", devc->total_samples);
}

/*
 * Process a chunk of capture data in streaming mode. The memory layout
 * is rather different from "normal mode" (see the send_chunk() routine
 * above). In streaming mode data is not compressed, and memory cells
 * neither contain raw sampled pin values at a given point in time. The
 * memory content needs transformation.
 * - The memory content can be seen as a sequence of memory cells.
 * - Each cell contains samples that correspond to the same channel.
 *   The next cell contains samples for the next channel, etc.
 * - Only enabled channels occupy memory cells. Disabled channels are
 *   not part of the capture data memory layout.
 * - The LSB bit position in a cell is the sample which was taken first
 *   for this channel. Upper bit positions were taken later.
 *
 * Implementor's note: This routine is inspired by convert_sample_data()
 * in the https://github.com/AlexUg/sigrok implementation. Which in turn
 * appears to have been derived from the saleae-logic16 sigrok driver.
 * The code is phrased conservatively to verify the layout as discussed
 * above, performance was not a priority. Operation was verified with an
 * LA2016 device. The memory layout of 32 channel models is yet to get
 * determined.
 */
static void stream_data(struct sr_dev_inst *sdi,
	const uint8_t *data_buffer, size_t data_length)
{
	struct dev_context *devc;
	struct stream_state_t *stream;
	size_t bit_count;
	const uint8_t *rp;
	uint32_t sample_value;
	uint8_t sample_buff[sizeof(sample_value)];
	size_t bit_idx;
	uint32_t ch_mask;

	devc = sdi->priv;
	stream = &devc->stream;

	/* Ignore incoming USB data after complete sample data download. */
	if (devc->download_finished)
		return;
	sr_dbg("Stream mode, got another chunk: %p, length %zu.",
		data_buffer, data_length);

	/* TODO Add soft trigger support when in stream mode? */

	/*
	 * TODO Are memory cells always as wide as the channel count?
	 * Are they always 16bits wide? Verify for 32 channel devices.
	 */
	bit_count = devc->model->channel_count;
	if (bit_count == 32) {
		data_length /= sizeof(uint32_t);
	} else if (bit_count == 16) {
		data_length /= sizeof(uint16_t);
	} else {
		/*
		 * Unhandled case. Acquisition should not start.
		 * The statement silences the compiler.
		 */
		return;
	}
	rp = data_buffer;
	sample_value = 0;
	while (data_length--) {
		/* Get another entity. */
		if (bit_count == 32)
			sample_value = read_u32le_inc(&rp);
		else if (bit_count == 16)
			sample_value = read_u16le_inc(&rp);

		/* Map the entity's bits to a channel's samples. */
		ch_mask = stream->channel_masks[stream->channel_index];
		for (bit_idx = 0; bit_idx < bit_count; bit_idx++) {
			if (sample_value & (1UL << bit_idx))
				stream->sample_data[bit_idx] |= ch_mask;
		}

		/*
		 * Advance to the next channel. Submit a block of
		 * samples when all channels' data was seen.
		 */
		stream->channel_index++;
		if (stream->channel_index != stream->enabled_count)
			continue;
		for (bit_idx = 0; bit_idx < bit_count; bit_idx++) {
			sample_value = stream->sample_data[bit_idx];
			write_u32le(sample_buff, sample_value);
			feed_queue_logic_submit(devc->feed_queue, sample_buff, 1);
		}
		sr_sw_limits_update_samples_read(&devc->sw_limits, bit_count);
		devc->total_samples += bit_count;
		memset(stream->sample_data, 0, sizeof(stream->sample_data));
		stream->channel_index = 0;
	}

	/*
	 * Need we count empty or failed USB transfers? This version
	 * doesn't, assumes that timeouts are perfectly legal because
	 * transfers are started early, and slow samplerates or trigger
	 * support in hardware are plausible causes for empty transfers.
	 *
	 * TODO Maybe a good condition would be (rather large) a timeout
	 * after a previous capture data chunk was seen? So that stalled
	 * streaming gets detected which _is_ an exceptional condition.
	 * We have observed these when "runmode" is set early but bulk
	 * transfers start late with a pause after setting the runmode.
	 */
	if (sr_sw_limits_check(&devc->sw_limits)) {
		sr_dbg("Acquisition end reached (sw limits).");
		devc->download_finished = TRUE;
	}
	if (devc->download_finished) {
		sr_dbg("Stream receive done, flushing session feed queue.");
		feed_queue_logic_flush(devc->feed_queue);
	}
	sr_dbg("Total samples after chunk: %" PRIu64 ".", devc->total_samples);
}

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	gboolean was_cancelled, device_gone;
	int ret;

	sdi = transfer->user_data;
	devc = sdi->priv;

	was_cancelled = transfer->status == LIBUSB_TRANSFER_CANCELLED;
	device_gone = transfer->status == LIBUSB_TRANSFER_NO_DEVICE;
	sr_dbg("receive_transfer(): status %s received %d bytes.",
		libusb_error_name(transfer->status), transfer->actual_length);
	if (device_gone) {
		sr_warn("Lost communication to USB device.");
		devc->download_finished = TRUE;
		return;
	}

	/*
	 * Implementation detail: A USB transfer timeout is not fatal
	 * here. We just process whatever was received, empty input is
	 * perfectly acceptable. Reaching (or exceeding) the sw limits
	 * or exhausting the device's captured data will complete the
	 * sample data download.
	 */
	if (devc->continuous)
		stream_data(sdi, transfer->buffer, transfer->actual_length);
	else
		send_chunk(sdi, transfer->buffer, transfer->actual_length);

	/*
	 * Re-submit completed transfers (regardless of timeout or
	 * data reception), unless the transfer was cancelled when
	 * the acquisition was terminated or has completed.
	 */
	if (!was_cancelled && !devc->download_finished) {
		ret = la2016_usbxfer_resubmit(sdi, transfer);
		if (ret == SR_OK)
			return;
		devc->download_finished = TRUE;
	}
}

SR_PRIV int la2016_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct timeval tv;
	int ret;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;
	drvc = sdi->driver->context;

	/* Arrange for the start of stream mode when requested. */
	if (devc->continuous && !devc->frame_begin_sent) {
		sr_dbg("First receive callback in stream mode.");
		devc->download_finished = FALSE;
		devc->trigger_marked = FALSE;
		devc->total_samples = 0;

		std_session_send_df_frame_begin(sdi);
		devc->frame_begin_sent = TRUE;

		ret = set_run_mode(sdi, RUNMODE_RUN);
		if (ret != SR_OK) {
			sr_err("Cannot set 'runmode' to 'run'.");
			return FALSE;
		}

		ret = ctrl_out(sdi, CMD_BULK_START, 0x00, 0, NULL, 0);
		if (ret != SR_OK) {
			sr_err("Cannot start USB bulk transfers.");
			return FALSE;
		}
		sr_dbg("Stream data reception initiated.");
	}

	/*
	 * Wait for the acquisition to complete in hardware.
	 * Periodically check a potentially configured msecs timeout.
	 */
	if (!devc->continuous && !devc->completion_seen) {
		if (!la2016_is_idle(sdi)) {
			if (sr_sw_limits_check(&devc->sw_limits)) {
				devc->sw_limits.limit_msec = 0;
				sr_dbg("Limit reached. Stopping acquisition.");
				la2016_stop_acquisition(sdi);
			}
			/* Not yet ready for sample data download. */
			return TRUE;
		}
		sr_dbg("Acquisition completion seen (hardware).");
		devc->sw_limits.limit_msec = 0;
		devc->completion_seen = TRUE;
		devc->download_finished = FALSE;
		devc->trigger_marked = FALSE;
		devc->total_samples = 0;

		la2016_dump_fpga_registers(sdi, "acquisition complete", 0, 0);

		/* Initiate the download of acquired sample data. */
		std_session_send_df_frame_begin(sdi);
		devc->frame_begin_sent = TRUE;
		ret = la2016_start_download(sdi);
		if (ret != SR_OK) {
			sr_err("Cannot start acquisition data download.");
			return FALSE;
		}
		sr_dbg("Acquisition data download started.");

		return TRUE;
	}

	/* Handle USB reception. Drives sample data download. */
	memset(&tv, 0, sizeof(tv));
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	/*
	 * Periodically flush acquisition data in streaming mode.
	 * Without this nudge, previously received and accumulated data
	 * keeps sitting in queues and is not seen by applications.
	 */
	if (devc->continuous && devc->stream.flush_period_ms) {
		uint64_t now, elapsed;
		now = g_get_monotonic_time();
		if (!devc->stream.last_flushed)
			devc->stream.last_flushed = now;
		elapsed = now - devc->stream.last_flushed;
		elapsed /= 1000;
		if (elapsed >= devc->stream.flush_period_ms) {
			sr_dbg("Stream mode, flushing.");
			feed_queue_logic_flush(devc->feed_queue);
			devc->stream.last_flushed = now;
		}
	}

	/* Postprocess completion of sample data download. */
	if (devc->download_finished) {
		sr_dbg("Download finished, post processing.");

		la2016_stop_acquisition(sdi);
		usb_source_remove(sdi->session, drvc->sr_ctx);

		la2016_usbxfer_cancel_all(sdi);
		memset(&tv, 0, sizeof(tv));
		libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

		feed_queue_logic_flush(devc->feed_queue);
		feed_queue_logic_free(devc->feed_queue);
		devc->feed_queue = NULL;
		if (devc->frame_begin_sent) {
			std_session_send_df_frame_end(sdi);
			devc->frame_begin_sent = FALSE;
		}
		std_session_send_df_end(sdi);

		sr_dbg("Download finished, done post processing.");
	}

	return TRUE;
}

SR_PRIV int la2016_identify_device(const struct sr_dev_inst *sdi,
	gboolean show_message)
{
	struct dev_context *devc;
	uint8_t buf[8]; /* Larger size of manuf date and device type magic. */
	size_t rdoff, rdlen;
	const uint8_t *rdptr;
	uint8_t date_yy, date_mm;
	uint8_t dinv_yy, dinv_mm;
	uint8_t magic, magic2;
	size_t model_idx;
	const struct kingst_model *model;
	int ret;

	devc = sdi->priv;

	/*
	 * Four EEPROM bytes at offset 0x20 are the manufacturing date,
	 * year and month in BCD format, followed by inverted values for
	 * consistency checks. For example bytes 20 04 df fb translate
	 * to 2020-04. This information can help identify the vintage of
	 * devices when unknown magic numbers are seen.
	 */
	rdoff = 0x20;
	rdlen = 4 * sizeof(uint8_t);
	ret = ctrl_in(sdi, CMD_EEPROM, rdoff, 0, buf, rdlen);
	if (ret != SR_OK && !show_message) {
		/* Non-fatal weak attempt during probe. Not worth logging. */
		sr_dbg("Cannot access EEPROM.");
		return SR_ERR_IO;
	} else if (ret != SR_OK) {
		/* Failed attempt in regular use. Non-fatal. Worth logging. */
		sr_err("Cannot read manufacture date in EEPROM.");
	} else {
		if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
			GString *txt;
			txt = sr_hexdump_new(buf, rdlen);
			sr_spew("Manufacture date bytes %s.", txt->str);
			sr_hexdump_free(txt);
		}
		rdptr = &buf[0];
		date_yy = read_u8_inc(&rdptr);
		date_mm = read_u8_inc(&rdptr);
		dinv_yy = read_u8_inc(&rdptr);
		dinv_mm = read_u8_inc(&rdptr);
		sr_info("Manufacture date: 20%02hx-%02hx.", date_yy, date_mm);
		if ((date_mm ^ dinv_mm) != 0xff || (date_yy ^ dinv_yy) != 0xff)
			sr_warn("Manufacture date fails checksum test.");
	}

	/*
	 * Several Kingst logic analyzer devices share the same USB VID
	 * and PID. The product ID determines which MCU firmware to load.
	 * The MCU firmware provides access to EEPROM content which then
	 * allows to identify the device model. Which in turn determines
	 * which FPGA bitstream to load. Eight bytes at offset 0x08 are
	 * to get inspected.
	 *
	 * EEPROM content for model identification is kept redundantly
	 * in memory. The values are stored in verbatim and in inverted
	 * form, multiple copies are kept at different offsets. Example
	 * data:
	 *
	 *   magic 0x08
	 *    | ~magic 0xf7
	 *    | |
	 *   08f7000008f710ef
	 *            | |
	 *            | ~magic backup
	 *            magic backup
	 *
	 * Exclusively inspecting the magic byte appears to be sufficient,
	 * other fields seem to be 'don't care'.
	 *
	 *   magic 2 == LA2016 using "kingst-la2016-fpga.bitstream"
	 *   magic 3 == LA1016 using "kingst-la1016-fpga.bitstream"
	 *   magic 8 == LA2016a using "kingst-la2016a1-fpga.bitstream"
	 *              (latest v1.3.0 PCB, perhaps others)
	 *   magic 9 == LA1016a using "kingst-la1016a1-fpga.bitstream"
	 *              (latest v1.3.0 PCB, perhaps others)
	 *
	 * When EEPROM content does not match the hardware configuration
	 * (the board layout), the software may load but yield incorrect
	 * results (like swapped channels). The FPGA bitstream itself
	 * will authenticate with IC U10 and fail when its capabilities
	 * do not match the hardware model. An LA1016 won't become a
	 * LA2016 by faking its EEPROM content.
	 */
	devc->identify_magic = 0;
	rdoff = 0x08;
	rdlen = 8 * sizeof(uint8_t);
	ret = ctrl_in(sdi, CMD_EEPROM, rdoff, 0, &buf, rdlen);
	if (ret != SR_OK) {
		sr_err("Cannot read EEPROM device identifier bytes.");
		return ret;
	}
	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		GString *txt;
		txt = sr_hexdump_new(buf, rdlen);
		sr_spew("EEPROM magic bytes %s.", txt->str);
		sr_hexdump_free(txt);
	}
	magic = 0;
	magic2 = 0;
	if ((buf[0] ^ buf[1]) == 0xff && (buf[2] ^ buf[3]) == 0xff) {
		/* Primary copy of magic passes complement check (4 bytes). */
		magic = buf[0];
		magic2 = buf[2];
		sr_dbg("Using primary magic %hhu (%hhu).", magic, magic2);
	} else if ((buf[4] ^ buf[5]) == 0xff && (buf[6] ^ buf[7]) == 0xff) {
		/* Backup copy of magic passes complement check (4 bytes). */
		magic = buf[4];
		magic2 = buf[6];
		sr_dbg("Using secondary magic %hhu (%hhu).", magic, magic2);
	} else if ((buf[0] ^ buf[1]) == 0xff) {
		/* Primary copy of magic passes complement check (2 bytes). */
		magic = buf[0];
		sr_dbg("Using primary magic %hhu.", magic);
	} else if ((buf[4] ^ buf[5]) == 0xff) {
		/* Backup copy of magic passes complement check (2 bytes). */
		magic = buf[4];
		sr_dbg("Using secondary magic %hhu.", magic);
	} else {
		sr_err("Cannot find consistent device type identification.");
	}
	devc->identify_magic = magic;
	devc->identify_magic2 = magic2;

	devc->model = NULL;
	for (model_idx = 0; model_idx < ARRAY_SIZE(models); model_idx++) {
		model = &models[model_idx];
		if (model->magic != magic)
			continue;
		if (model->magic2 && model->magic2 != magic2)
			continue;
		devc->model = model;
		sr_info("Model '%s', %zu channels, max %" PRIu64 "MHz.",
			model->name, model->channel_count,
			model->samplerate / SR_MHZ(1));
		devc->fpga_bitstream = g_strdup_printf(FPGA_FWFILE_FMT,
			model->fpga_stem);
		sr_info("FPGA bitstream file '%s'.", devc->fpga_bitstream);
		if (!model->channel_count) {
			sr_warn("Device lacks logic channels. Not supported.");
			devc->model = NULL;
		}
		break;
	}
	if (!devc->model) {
		sr_err("Cannot identify as one of the supported models.");
		return SR_ERR_DATA;
	}

	return SR_OK;
}

SR_PRIV int la2016_init_hardware(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	const char *bitstream_fn;
	int ret;
	uint16_t state;

	devc = sdi->priv;
	bitstream_fn = devc ? devc->fpga_bitstream : "";

	ret = check_fpga_bitstream(sdi);
	if (ret != SR_OK) {
		ret = upload_fpga_bitstream(sdi, bitstream_fn);
		if (ret != SR_OK) {
			sr_err("Cannot upload FPGA bitstream.");
			return ret;
		}
	}
	ret = enable_fpga_bitstream(sdi);
	if (ret != SR_OK) {
		sr_err("Cannot enable FPGA bitstream after upload.");
		return ret;
	}

	state = run_state(sdi);
	if ((state & 0xfff0) != 0x85e0) {
		sr_warn("Unexpected run state, want 0x85eX, got 0x%04x.", state);
	}

	ret = ctrl_out(sdi, CMD_BULK_RESET, 0x00, 0, NULL, 0);
	if (ret != SR_OK) {
		sr_err("Cannot reset USB bulk transfer.");
		return ret;
	}

	sr_dbg("Device should be initialized.");

	return SR_OK;
}

SR_PRIV int la2016_deinit_hardware(const struct sr_dev_inst *sdi)
{
	int ret;

	ret = ctrl_out(sdi, CMD_FPGA_ENABLE, 0x00, 0, NULL, 0);
	if (ret != SR_OK) {
		sr_err("Cannot deinitialize device's FPGA.");
		return ret;
	}

	return SR_OK;
}

SR_PRIV void la2016_release_resources(const struct sr_dev_inst *sdi)
{
	(void)la2016_usbxfer_release(sdi);
}

SR_PRIV int la2016_write_pwm_config(const struct sr_dev_inst *sdi, size_t idx)
{
	return set_pwm_config(sdi, idx);
}
