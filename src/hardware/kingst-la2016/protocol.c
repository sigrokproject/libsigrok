/*
 * This file is part of the libsigrok project.
 *
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
 * List of supported devices and their features. See @ref kingst_model
 * for the fields' type and meaning. Table is sorted by EEPROM magic.
 *
 * TODO
 * - Below LA1016 properties were guessed, need verification.
 * - Add LA5016 and LA5032 devices when their EEPROM magic is known.
 * - Does LA1010 fit the driver implementation? Samplerates vary with
 *   channel counts, lack of local sample memory. Most probably not.
 */
static const struct kingst_model models[] = {
	{ 2, "LA2016", "la2016", SR_MHZ(200), 16, 1, },
	{ 3, "LA1016", "la1016", SR_MHZ(100), 16, 1, },
	{ 8, "LA2016", "la2016a1", SR_MHZ(200), 16, 1, },
	{ 9, "LA1016", "la1016a1", SR_MHZ(100), 16, 1, },
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
#define REG_BULK	0x08	/* Write start addr, byte count to download samples. */
#define REG_SAMPLING	0x10	/* Write capture config, read capture SDRAM location. */
#define REG_TRIGGER	0x20	/* write level and edge trigger config. */
#define REG_THRESHOLD	0x68	/* Write PWM config to setup input threshold DAC. */
#define REG_PWM1	0x70	/* Write config for user PWM1. */
#define REG_PWM2	0x78	/* Write config for user PWM2. */

/* Bit patterns to write to REG_RUN, setup run mode. */
#define RUNMODE_HALT	0x00
#define RUNMODE_RUN	0x03

/* Bit patterns when reading from REG_RUN, get run state. */
#define RUNSTATE_IDLE_BIT	(1UL << 0)
#define RUNSTATE_DRAM_BIT	(1UL << 1)
#define RUNSTATE_TRGD_BIT	(1UL << 2)
#define RUNSTATE_POST_BIT	(1UL << 3)

/* Properties related to the layout of capture data downloads. */
#define NUM_PACKETS_IN_CHUNK	5
#define TRANSFER_PACKET_LENGTH	16

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
		return SR_ERR;
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
		return SR_ERR;
	}

	return SR_OK;
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
	int ret;
	uint16_t run_state;
	uint8_t pwm_en;
	size_t read_len;
	uint8_t buff[sizeof(run_state)];
	const uint8_t *rdptr;

	sr_dbg("Checking operation of the FPGA bitstream.");

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
				return SR_ERR;
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
			ret = SR_ERR;
			break;
		}
		if (act_len != len) {
			sr_dbg("Short write for FPGA bitstream, block %#x len %d: got %d.",
				pos, (int)len, act_len);
			ret = SR_ERR;
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
		return SR_ERR;
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
	struct dev_context *devc;
	int ret;
	uint16_t duty_R79, duty_R56;
	uint8_t buf[2 * sizeof(uint16_t)];
	uint8_t *wrptr;

	devc = sdi->priv;

	/* Clamp threshold setting to valid range for LA2016. */
	if (voltage > 4.0) {
		voltage = 4.0;
	} else if (voltage < -4.0) {
		voltage = -4.0;
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
	devc->threshold_voltage = voltage;

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

static uint16_t get_channels_mask(const struct sr_dev_inst *sdi)
{
	uint16_t channels;
	GSList *l;
	struct sr_channel *ch;

	channels = 0;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_LOGIC)
			continue;
		if (!ch->enabled)
			continue;
		channels |= 1UL << ch->index;
	}

	return channels;
}

static int set_trigger_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct trigger_cfg {
		uint32_t channels;
		uint32_t enabled;
		uint32_t level;
		uint32_t high_or_falling;
	} cfg;
	GSList *stages;
	GSList *channel;
	struct sr_trigger_stage *stage1;
	struct sr_trigger_match *match;
	uint16_t ch_mask;
	int ret;
	uint8_t buf[4 * sizeof(uint32_t)];
	uint8_t *wrptr;

	devc = sdi->priv;
	trigger = sr_session_trigger_get(sdi->session);

	memset(&cfg, 0, sizeof(cfg));

	cfg.channels = get_channels_mask(sdi);

	if (trigger && trigger->stages) {
		stages = trigger->stages;
		stage1 = stages->data;
		if (stages->next) {
			sr_err("Only one trigger stage supported for now.");
			return SR_ERR;
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
					return SR_ERR;
				}
				cfg.level &= ~ch_mask;
				cfg.high_or_falling &= ~ch_mask;
				break;
			case SR_TRIGGER_FALLING:
				if ((cfg.enabled & ~cfg.level)) {
					sr_err("Device only supports one edge trigger.");
					return SR_ERR;
				}
				cfg.level &= ~ch_mask;
				cfg.high_or_falling |= ch_mask;
				break;
			default:
				sr_err("Unknown trigger condition.");
				return SR_ERR;
			}
			cfg.enabled |= ch_mask;
			channel = channel->next;
		}
	}
	sr_dbg("Set trigger config: "
		"channels 0x%04x, trigger-enabled 0x%04x, "
		"level-triggered 0x%04x, high/falling 0x%04x.",
		cfg.channels, cfg.enabled, cfg.level, cfg.high_or_falling);

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

static int set_sample_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint64_t min_samplerate, eff_samplerate;
	uint16_t divider_u16;
	uint64_t limit_samples;
	uint64_t pre_trigger_samples;
	uint64_t pre_trigger_memory;
	uint8_t buf[REG_TRIGGER - REG_SAMPLING]; /* Width of REG_SAMPLING. */
	uint8_t *wrptr;
	int ret;

	devc = sdi->priv;

	if (devc->cur_samplerate > devc->model->samplerate) {
		sr_err("Too high a sample rate: %" PRIu64 ".",
			devc->cur_samplerate);
		return SR_ERR_ARG;
	}
	min_samplerate = devc->model->samplerate;
	min_samplerate /= 65536;
	if (devc->cur_samplerate < min_samplerate) {
		sr_err("Too low a sample rate: %" PRIu64 ".",
			devc->cur_samplerate);
		return SR_ERR_ARG;
	}
	divider_u16 = devc->model->samplerate / devc->cur_samplerate;
	eff_samplerate = devc->model->samplerate / divider_u16;

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
	 *
	 * TODO Determine whether the pre-trigger memory size gets
	 * specified in samples or in bytes. A previous implementation
	 * suggests bytes but this is suspicious when every other spec
	 * is in terms of samples.
	 */
	if (devc->trigger_involved) {
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
		pre_trigger_samples = 1;
		pre_trigger_memory = 0;
	}
	/* Ensure non-zero value after LSB shift out in HW reg. */
	if (pre_trigger_memory < 0x100) {
		pre_trigger_memory = 0x100;
	}

	sr_dbg("Set sample config: %" PRIu64 "kHz, %" PRIu64 " samples.",
		eff_samplerate / SR_KHZ(1), limit_samples);
	sr_dbg("Capture ratio %" PRIu64 "%%, count %" PRIu64 ", mem %" PRIu64 ".",
		devc->capture_ratio, pre_trigger_samples, pre_trigger_memory);

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
	uint8_t buff[sizeof(state)];
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

static int la2016_is_idle(const struct sr_dev_inst *sdi)
{
	uint16_t state;

	state = run_state(sdi);
	if ((state & runstate_mask_idle) == runstate_patt_idle)
		return 1;

	return 0;
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
	uint8_t buf[3 * sizeof(uint32_t)];
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

	if (devc->info.n_rep_packets % NUM_PACKETS_IN_CHUNK) {
		sr_warn("Unexpected packets count %lu, not a multiple of %d.",
			(unsigned long)devc->info.n_rep_packets,
			NUM_PACKETS_IN_CHUNK);
	}

	return SR_OK;
}

SR_PRIV int la2016_upload_firmware(const struct sr_dev_inst *sdi,
	struct sr_context *sr_ctx, libusb_device *dev, uint16_t product_id)
{
	struct dev_context *devc;
	char *fw_file;
	int ret;

	devc = sdi ? sdi->priv : NULL;

	fw_file = g_strdup_printf(MCU_FWFILE_FMT, product_id);
	sr_info("USB PID %04hx, MCU firmware '%s'.", product_id, fw_file);

	ret = ezusb_upload_firmware(sr_ctx, dev, USB_CONFIGURATION, fw_file);
	if (ret != SR_OK) {
		g_free(fw_file);
		return ret;
	}

	if (devc) {
		devc->mcu_firmware = fw_file;
		fw_file = NULL;
	}
	g_free(fw_file);

	return SR_OK;
}

SR_PRIV int la2016_setup_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;
	uint8_t cmd;

	devc = sdi->priv;

	ret = set_threshold_voltage(sdi, devc->threshold_voltage);
	if (ret != SR_OK)
		return ret;

	cmd = 0;
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
	int ret;

	ret = set_run_mode(sdi, RUNMODE_RUN);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static int la2016_stop_acquisition(const struct sr_dev_inst *sdi)
{
	int ret;

	ret = set_run_mode(sdi, RUNMODE_HALT);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

SR_PRIV int la2016_abort_acquisition(const struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;

	ret = la2016_stop_acquisition(sdi);
	if (ret != SR_OK)
		return ret;

	devc = sdi ? sdi->priv : NULL;
	if (devc && devc->transfer)
		libusb_cancel_transfer(devc->transfer);

	return SR_OK;
}

static int la2016_start_download(const struct sr_dev_inst *sdi,
	libusb_transfer_cb_fn cb)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int ret;
	uint8_t wrbuf[2 * sizeof(uint32_t)];
	uint8_t *wrptr;
	uint32_t to_read;
	uint8_t *buffer;

	devc = sdi->priv;
	usb = sdi->conn;

	ret = get_capture_info(sdi);
	if (ret != SR_OK)
		return ret;

	devc->n_transfer_packets_to_read = devc->info.n_rep_packets / NUM_PACKETS_IN_CHUNK;
	devc->n_bytes_to_read = devc->n_transfer_packets_to_read * TRANSFER_PACKET_LENGTH;
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
	ret = ctrl_out(sdi, CMD_BULK_START, 0x00, 0, NULL, 0);
	if (ret != SR_OK) {
		sr_err("Cannot unblock USB bulk transfers.");
		return ret;
	}

	/*
	 * Pick a buffer size for all USB transfers. The buffer size
	 * must be a multiple of the endpoint packet size. And cannot
	 * exceed a maximum value.
	 */
	to_read = devc->n_bytes_to_read;
	if (to_read >= LA2016_USB_BUFSZ) /* Multiple transfers. */
		to_read = LA2016_USB_BUFSZ;
	else /* One transfer. */
		to_read = (to_read + (LA2016_EP6_PKTSZ-1)) & ~(LA2016_EP6_PKTSZ-1);
	buffer = g_try_malloc(to_read);
	if (!buffer) {
		sr_dbg("USB bulk transfer size %d bytes.", (int)to_read);
		sr_err("Cannot allocate buffer for USB bulk transfer.");
		return SR_ERR_MALLOC;
	}

	devc->transfer = libusb_alloc_transfer(0);
	libusb_fill_bulk_transfer(devc->transfer,
		usb->devhdl, USB_EP_CAPTURE_DATA | LIBUSB_ENDPOINT_IN,
		buffer, to_read, cb, (void *)sdi, DEFAULT_TIMEOUT_MS);

	ret = libusb_submit_transfer(devc->transfer);
	if (ret != 0) {
		sr_err("Cannot submit USB transfer: %s.", libusb_error_name(ret));
		libusb_free_transfer(devc->transfer);
		devc->transfer = NULL;
		g_free(buffer);
		return SR_ERR;
	}

	return SR_OK;
}

/*
 * A chunk (received via USB) contains a number of transfers (USB length
 * divided by 16) which contain a number of packets (5 per transfer) which
 * contain a number of samples (8bit repeat count per 16bit sample data).
 */
static void send_chunk(struct sr_dev_inst *sdi,
	const uint8_t *packets, size_t num_xfers)
{
	struct dev_context *devc;
	size_t num_pkts;
	const uint8_t *rp;
	uint16_t sample_value;
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

	rp = packets;
	while (num_xfers--) {
		num_pkts = NUM_PACKETS_IN_CHUNK;
		while (num_pkts--) {

			sample_value = read_u16le_inc(&rp);
			repetitions = read_u8_inc(&rp);

			devc->total_samples += repetitions;

			write_u16le(sample_buff, sample_value);
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
						(double)devc->total_samples / devc->cur_samplerate * 1e3);
				}
			}
		}
		(void)read_u8_inc(&rp); /* Skip sequence number. */
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

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	size_t num_xfers;
	int ret;

	sdi = transfer->user_data;
	devc = sdi->priv;
	usb = sdi->conn;

	sr_dbg("receive_transfer(): status %s received %d bytes.",
		libusb_error_name(transfer->status), transfer->actual_length);
	/*
	 * Implementation detail: A USB transfer timeout is not fatal
	 * here. We just process whatever was received, empty input is
	 * perfectly acceptable. Reaching (or exceeding) the sw limits
	 * or exhausting the device's captured data will complete the
	 * sample data download.
	 */
	num_xfers = transfer->actual_length / TRANSFER_PACKET_LENGTH;
	send_chunk(sdi, transfer->buffer, num_xfers);

	devc->n_bytes_to_read -= transfer->actual_length;
	if (devc->n_bytes_to_read) {
		uint32_t to_read = devc->n_bytes_to_read;
		/*
		 * Determine read size for the next USB transfer. Make
		 * the buffer size a multiple of the endpoint packet
		 * size. Don't exceed a maximum value.
		 */
		if (to_read >= LA2016_USB_BUFSZ)
			to_read = LA2016_USB_BUFSZ;
		else
			to_read = (to_read + (LA2016_EP6_PKTSZ-1)) & ~(LA2016_EP6_PKTSZ-1);
		libusb_fill_bulk_transfer(transfer,
			usb->devhdl, USB_EP_CAPTURE_DATA | LIBUSB_ENDPOINT_IN,
			transfer->buffer, to_read,
			receive_transfer, (void *)sdi, DEFAULT_TIMEOUT_MS);

		ret = libusb_submit_transfer(transfer);
		if (ret == 0)
			return;
		sr_err("Cannot submit another USB transfer: %s.",
			libusb_error_name(ret));
	}

	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
	devc->download_finished = TRUE;
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

	/*
	 * Wait for the acquisition to complete in hardware.
	 * Periodically check a potentially configured msecs timeout.
	 */
	if (!devc->completion_seen) {
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

		/* Initiate the download of acquired sample data. */
		std_session_send_df_frame_begin(sdi);
		ret = la2016_start_download(sdi, receive_transfer);
		if (ret != SR_OK) {
			sr_err("Cannot start acquisition data download.");
			return FALSE;
		}
		sr_dbg("Acquisition data download started.");

		return TRUE;
	}

	/* Handle USB reception. Drives sample data download. */
	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	/* Postprocess completion of sample data download. */
	if (devc->download_finished) {
		sr_dbg("Download finished, post processing.");

		la2016_stop_acquisition(sdi);
		usb_source_remove(sdi->session, drvc->sr_ctx);
		devc->transfer = NULL;

		feed_queue_logic_flush(devc->feed_queue);
		feed_queue_logic_free(devc->feed_queue);
		devc->feed_queue = NULL;
		std_session_send_df_frame_end(sdi);
		std_session_send_df_end(sdi);

		sr_dbg("Download finished, done post processing.");
	}

	return TRUE;
}

SR_PRIV int la2016_identify_device(const struct sr_dev_inst *sdi,
	gboolean show_message)
{
	struct dev_context *devc;
	uint8_t buf[8];
	size_t rdoff, rdlen;
	const uint8_t *rdptr;
	uint8_t date_yy, date_mm;
	uint8_t dinv_yy, dinv_mm;
	uint8_t magic;
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
	if ((buf[0] ^ buf[1]) == 0xff) {
		/* Primary copy of magic passes complement check. */
		magic = buf[0];
		sr_dbg("Using primary magic, value %d.", (int)magic);
	} else if ((buf[4] ^ buf[5]) == 0xff) {
		/* Backup copy of magic passes complement check. */
		magic = buf[4];
		sr_dbg("Using backup magic, value %d.", (int)magic);
	} else {
		sr_err("Cannot find consistent device type identification.");
		magic = 0;
	}
	devc->identify_magic = magic;

	devc->model = NULL;
	for (model_idx = 0; model_idx < ARRAY_SIZE(models); model_idx++) {
		model = &models[model_idx];
		if (model->magic != magic)
			continue;
		devc->model = model;
		sr_info("Model '%s', %zu channels, max %" PRIu64 "MHz.",
			model->name, model->channel_count,
			model->samplerate / SR_MHZ(1));
		devc->fpga_bitstream = g_strdup_printf(FPGA_FWFILE_FMT,
			model->fpga_stem);
		sr_info("FPGA bitstream file '%s'.", devc->fpga_bitstream);
		break;
	}
	if (!devc->model) {
		sr_err("Cannot identify as one of the supported models.");
		return SR_ERR;
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
	if (state != 0x85e9) {
		sr_warn("Unexpected run state, want 0x85e9, got 0x%04x.", state);
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

SR_PRIV int la2016_write_pwm_config(const struct sr_dev_inst *sdi, size_t idx)
{
	return set_pwm_config(sdi, idx);
}
