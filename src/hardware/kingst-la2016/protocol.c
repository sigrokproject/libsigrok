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
#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <inttypes.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define UC_FIRMWARE	"kingst-la-%04x.fw"
#define FPGA_FW_LA2016	"kingst-la2016-fpga.bitstream"
#define FPGA_FW_LA2016A	"kingst-la2016a1-fpga.bitstream"
#define FPGA_FW_LA1016	"kingst-la1016-fpga.bitstream"
#define FPGA_FW_LA1016A	"kingst-la1016a1-fpga.bitstream"

#define MAX_SAMPLE_RATE_LA2016	SR_MHZ(200)
#define MAX_SAMPLE_RATE_LA1016	SR_MHZ(100)
#define MAX_SAMPLE_DEPTH 10e9
#define MAX_PWM_FREQ     SR_MHZ(20)
#define PWM_CLOCK        SR_MHZ(200)	/* this is 200MHz for both the LA2016 and LA1016 */

/* usb vendor class control requests to the cypress FX2 microcontroller */
#define CMD_FPGA_ENABLE	0x10
#define CMD_FPGA_SPI	0x20	/* access registers in the FPGA over SPI bus, ctrl_in reads, ctrl_out writes */
#define CMD_BULK_START	0x30	/* begin transfer of capture data via usb endpoint 6 IN */
#define CMD_BULK_RESET	0x38	/* flush FX2 usb endpoint 6 IN fifos */
#define CMD_FPGA_INIT	0x50	/* used before and after FPGA bitstream loading */
#define CMD_KAUTH	0x60	/* communicate with authentication ic U10, not used */
#define CMD_EEPROM	0xa2	/* ctrl_in reads, ctrl_out writes */

/*
 * fpga spi register addresses for control request CMD_FPGA_SPI:
 * There are around 60 byte-wide registers within the fpga and
 * these are the base addresses used for accessing them.
 * On the spi bus, the msb of the address byte is set for read
 * and cleared for write, but that is handled by the fx2 mcu
 * as appropriate. In this driver code just use IN transactions
 * to read, OUT to write.
 */
#define REG_RUN		0x00	/* read capture status, write capture start */
#define REG_PWM_EN	0x02	/* user pwm channels on/off */
#define REG_CAPT_MODE	0x03	/* set to 0x00 for capture to sdram, 0x01 bypass sdram for streaming */
#define REG_BULK	0x08	/* write start address and number of bytes for capture data bulk upload */
#define REG_SAMPLING	0x10	/* write capture config, read capture data location in sdram */
#define REG_TRIGGER	0x20	/* write level and edge trigger config */
#define REG_THRESHOLD	0x68	/* write two pwm configs to control input threshold dac */
#define REG_PWM1	0x70	/* write config for user pwm1 */
#define REG_PWM2	0x78	/* write config for user pwm2 */

static int ctrl_in(const struct sr_dev_inst *sdi,
		   uint8_t bRequest, uint16_t wValue, uint16_t wIndex,
		   void *data, uint16_t wLength)
{
	struct sr_usb_dev_inst *usb;
	int ret;

	usb = sdi->conn;

	if ((ret = libusb_control_transfer(
		     usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN,
		     bRequest, wValue, wIndex, (unsigned char *)data, wLength,
		     DEFAULT_TIMEOUT_MS)) != wLength) {
		sr_err("failed to read %d bytes via ctrl-in %d %#x, %d: %s.",
		       wLength, bRequest, wValue, wIndex,
		       libusb_error_name(ret));
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

	if ((ret = libusb_control_transfer(
		     usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT,
		     bRequest, wValue, wIndex, (unsigned char*)data, wLength,
		     DEFAULT_TIMEOUT_MS)) != wLength) {
		sr_err("failed to write %d bytes via ctrl-out %d %#x, %d: %s.",
		       wLength, bRequest, wValue, wIndex,
		       libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int upload_fpga_bitstream(const struct sr_dev_inst *sdi, const char *bitstream_fname)
{
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct sr_resource bitstream;
	uint8_t buffer[sizeof(uint32_t)];
	uint8_t *wrptr;
	uint8_t cmd_resp;
	uint8_t block[4096];
	int len, act_len;
	unsigned int pos;
	int ret;
	unsigned int zero_pad_to = 0x2c000;

	devc = sdi->priv;
	drvc = sdi->driver->context;
	usb = sdi->conn;

	sr_info("Uploading FPGA bitstream '%s'.", bitstream_fname);

	ret = sr_resource_open(drvc->sr_ctx, &bitstream, SR_RESOURCE_FIRMWARE, bitstream_fname);
	if (ret != SR_OK) {
		sr_err("could not find fpga firmware %s!", bitstream_fname);
		return ret;
	}

	devc->bitstream_size = (uint32_t)bitstream.size;
	wrptr = buffer;
	write_u32le_inc(&wrptr, devc->bitstream_size);
	if ((ret = ctrl_out(sdi, CMD_FPGA_INIT, 0x00, 0, buffer, wrptr - buffer)) != SR_OK) {
		sr_err("failed to give upload init command");
		sr_resource_close(drvc->sr_ctx, &bitstream);
		return ret;
	}

	pos = 0;
	while (1) {
		if (pos < bitstream.size) {
			len = (int)sr_resource_read(drvc->sr_ctx, &bitstream, &block, sizeof(block));
			if (len < 0) {
				sr_err("failed to read from fpga bitstream!");
				sr_resource_close(drvc->sr_ctx, &bitstream);
				return SR_ERR;
			}
		} else {
			// fill with zero's until zero_pad_to
			len = zero_pad_to - pos;
			if ((unsigned)len > sizeof(block))
				len = sizeof(block);
			memset(&block, 0, len);
		}
		if (len == 0)
			break;

		ret = libusb_bulk_transfer(usb->devhdl, 2, (unsigned char*)&block[0], len, &act_len, DEFAULT_TIMEOUT_MS);
		if (ret != 0) {
			sr_dbg("failed to write fpga bitstream block at %#x len %d: %s.", pos, (int)len, libusb_error_name(ret));
			ret = SR_ERR;
			break;
		}
		if (act_len != len) {
			sr_dbg("failed to write fpga bitstream block at %#x len %d: act_len is %d.", pos, (int)len, act_len);
			ret = SR_ERR;
			break;
		}
		pos += len;
	}
	sr_resource_close(drvc->sr_ctx, &bitstream);
	if (ret != 0)
		return ret;
	sr_info("FPGA bitstream upload (%" PRIu64 " bytes) done.", bitstream.size);

	if ((ret = ctrl_in(sdi, CMD_FPGA_INIT, 0x00, 0, &cmd_resp, sizeof(cmd_resp))) != SR_OK) {
		sr_err("failed to read response after FPGA bitstream upload");
		return ret;
	}
	if (cmd_resp != 0) {
		sr_err("after fpga bitstream upload command response is 0x%02x, expect 0!", cmd_resp);
		return SR_ERR;
	}

	g_usleep(30000);

	if ((ret = ctrl_out(sdi, CMD_FPGA_ENABLE, 0x01, 0, NULL, 0)) != SR_OK) {
		sr_err("failed enable fpga");
		return ret;
	}

	g_usleep(40000);
	return SR_OK;
}

static int set_threshold_voltage(const struct sr_dev_inst *sdi, float voltage)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	uint16_t duty_R79,duty_R56;
	uint8_t buf[2 * sizeof(uint16_t)];
	uint8_t *wrptr;

	/* clamp threshold setting within valid range for LA2016 */
	if (voltage > 4.0) {
		voltage = 4.0;
	}
	else if (voltage < -4.0) {
		voltage = -4.0;
	}

	/*
	 * The fpga has two programmable pwm outputs which feed a dac that
	 * is used to adjust input offset. The dac changes the input
	 * swing around the fixed fpga input threshold.
	 * The two pwm outputs can be seen on R79 and R56 respectvely.
	 * Frequency is fixed at 100kHz and duty is varied.
	 * The R79 pwm uses just three settings.
	 * The R56 pwm varies with required threshold and its behaviour
	 * also changes depending on the setting of R79 PWM.
	 */

	/*
	 * calculate required pwm duty register values from requested threshold voltage
	 * see last page of schematic (on wiki) for an explanation of these numbers
	 */
	if (voltage >= 2.9) {
		duty_R79 = 0;		/* this pwm is off (0V)*/
		duty_R56 = (uint16_t)(302 * voltage - 363);
	}
	else if (voltage <= -0.4) {
		duty_R79 = 0x02D7;	/* 72% duty */
		duty_R56 = (uint16_t)(302 * voltage + 1090);
	}
	else {
		duty_R79 = 0x00f2;	/* 25% duty */
		duty_R56 = (uint16_t)(302 * voltage + 121);
	}

	/* clamp duty register values at sensible limits */
	if (duty_R56 < 10) {
		duty_R56 = 10;
	}
	else if (duty_R56 > 1100) {
		duty_R56 = 1100;
	}

	sr_dbg("set threshold voltage %.2fV", voltage);
	sr_dbg("duty_R56=0x%04x, duty_R79=0x%04x", duty_R56, duty_R79);

	wrptr = buf;
	write_u16le_inc(&wrptr, duty_R56);
	write_u16le_inc(&wrptr, duty_R79);

	ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_THRESHOLD, 0, buf, wrptr - buf);
	if (ret != SR_OK) {
		sr_err("error setting new threshold voltage of %.2fV", voltage);
		return ret;
	}
	devc->threshold_voltage = voltage;

	return SR_OK;
}

static int enable_pwm(const struct sr_dev_inst *sdi, uint8_t p1, uint8_t p2)
{
	struct dev_context *devc;
	uint8_t cfg;
	int ret;

	devc = sdi->priv;
	cfg = 0;

	if (p1) cfg |= 1 << 0;
	if (p2) cfg |= 1 << 1;

	sr_dbg("set pwm enable %d %d", p1, p2);
	ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_PWM_EN, 0, &cfg, sizeof(cfg));
	if (ret != SR_OK) {
		sr_err("error setting new pwm enable 0x%02x", cfg);
		return ret;
	}
	devc->pwm_setting[0].enabled = (p1) ? 1 : 0;
	devc->pwm_setting[1].enabled = (p2) ? 1 : 0;

	return SR_OK;
}

static int set_pwm(const struct sr_dev_inst *sdi, uint8_t which, float freq, float duty)
{
	int CTRL_PWM[] = { REG_PWM1, REG_PWM2 };
	struct dev_context *devc;
	pwm_setting_dev_t cfg;
	pwm_setting_t *setting;
	int ret;
	uint8_t buf[2 * sizeof(uint32_t)];
	uint8_t *wrptr;

	devc = sdi->priv;

	if (which < 1 || which > 2) {
		sr_err("invalid pwm channel: %d", which);
		return SR_ERR;
	}
	if (freq > MAX_PWM_FREQ) {
		sr_err("pwm frequency too high: %.1f", freq);
		return SR_ERR;
	}
	if (duty > 100 || duty < 0) {
		sr_err("invalid pwm percentage: %f", duty);
		return SR_ERR;
	}

	cfg.period = (uint32_t)(PWM_CLOCK / freq);
	cfg.duty = (uint32_t)(0.5f + (cfg.period * duty / 100.));
	sr_dbg("set pwm%d period %d, duty %d", which, cfg.period, cfg.duty);

	wrptr = buf;
	write_u32le_inc(&wrptr, cfg.period);
	write_u32le_inc(&wrptr, cfg.duty);
	ret = ctrl_out(sdi, CMD_FPGA_SPI, CTRL_PWM[which - 1], 0, buf, wrptr - buf);
	if (ret != SR_OK) {
		sr_err("error setting new pwm%d config %d %d", which, cfg.period, cfg.duty);
		return ret;
	}
	setting = &devc->pwm_setting[which - 1];
	setting->freq = freq;
	setting->duty = duty;

	return SR_OK;
}

static int set_defaults(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	devc->capture_ratio = 5; /* percent */
	devc->cur_channels = 0xffff;
	devc->limit_samples = 5000000;
	devc->cur_samplerate = SR_MHZ(100);

	ret = set_threshold_voltage(sdi, devc->threshold_voltage);
	if (ret)
		return ret;

	ret = enable_pwm(sdi, 0, 0);
	if (ret)
		return ret;

	ret = set_pwm(sdi, 1, 1e3, 50);
	if (ret)
		return ret;

	ret = set_pwm(sdi, 2, 100e3, 50);
	if (ret)
		return ret;

	ret = enable_pwm(sdi, 1, 1);
	if (ret)
		return ret;

	return SR_OK;
}

static int set_trigger_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	trigger_cfg_t cfg;
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

	cfg.channels = devc->cur_channels;

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
			ch_mask = 1 << match->channel->index;

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
					sr_err("Only one trigger signal with falling-/rising-edge allowed.");
					return SR_ERR;
				}
				cfg.level &= ~ch_mask;
				cfg.high_or_falling &= ~ch_mask;
				break;
			case SR_TRIGGER_FALLING:
				if ((cfg.enabled & ~cfg.level)) {
					sr_err("Only one trigger signal with falling-/rising-edge allowed.");
					return SR_ERR;
				}
				cfg.level &= ~ch_mask;
				cfg.high_or_falling |= ch_mask;
				break;
			default:
				sr_err("Unknown trigger value.");
				return SR_ERR;
			}
			cfg.enabled |= ch_mask;
			channel = channel->next;
		}
	}
	sr_dbg("set trigger configuration channels: 0x%04x, "
	       "trigger-enabled 0x%04x, level-triggered 0x%04x, "
	       "high/falling 0x%04x", cfg.channels, cfg.enabled, cfg.level,
	       cfg.high_or_falling);

	devc->had_triggers_configured = cfg.enabled != 0;

	wrptr = buf;
	write_u32le_inc(&wrptr, cfg.channels);
	write_u32le_inc(&wrptr, cfg.enabled);
	write_u32le_inc(&wrptr, cfg.level);
	write_u32le_inc(&wrptr, cfg.high_or_falling);
	ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_TRIGGER, 16, buf, wrptr - buf);
	if (ret != SR_OK) {
		sr_err("error setting trigger config!");
		return ret;
	}

	return SR_OK;
}

static int set_sample_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	double clock_divisor;
	uint64_t total;
	int ret;
	uint16_t divisor;
	uint8_t buf[2 * sizeof(uint32_t) + 48 / 8 + sizeof(uint16_t)];
	uint8_t *wrptr;

	devc = sdi->priv;
	total = 128 * 1024 * 1024;

	if (devc->cur_samplerate > devc->max_samplerate) {
		sr_err("too high sample rate: %" PRIu64, devc->cur_samplerate);
		return SR_ERR;
	}

	clock_divisor = devc->max_samplerate / (double)devc->cur_samplerate;
	if (clock_divisor > 0xffff)
		clock_divisor = 0xffff;
	divisor = (uint16_t)(clock_divisor + 0.5);
	devc->cur_samplerate = devc->max_samplerate / divisor;

	if (devc->limit_samples > MAX_SAMPLE_DEPTH) {
		sr_err("too high sample depth: %" PRIu64, devc->limit_samples);
		return SR_ERR;
	}

	devc->pre_trigger_size = (devc->capture_ratio * devc->limit_samples) / 100;

	sr_dbg("set sampling configuration %.0fkHz, %d samples, trigger-pos %d%%",
	       devc->cur_samplerate / 1e3, (unsigned int)devc->limit_samples, (unsigned int)devc->capture_ratio);

	wrptr = buf;
	write_u32le_inc(&wrptr, devc->limit_samples);
	write_u8_inc(&wrptr, 0);
	write_u32le_inc(&wrptr, devc->pre_trigger_size);
	write_u32le_inc(&wrptr, ((total * devc->capture_ratio) / 100) & 0xFFFFFF00);
	write_u16le_inc(&wrptr, divisor);
	write_u8_inc(&wrptr, 0);

	ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_SAMPLING, 0, buf, wrptr - buf);
	if (ret != SR_OK) {
		sr_err("error setting sample config!");
		return ret;
	}

	return SR_OK;
}

/* The run state is read from FPGA registers 1[hi-byte] and 0[lo-byte]
 * and the bits are interpreted as follows:
 *
 * register 0:
 *	bit0 1=	idle
 *	bit1 1=	writing to sdram
 *	bit2 0=	waiting_for_trigger 1=been_triggered
 *	bit3 0=	pretrigger_sampling 1=posttrigger_sampling
 * 	...unknown...
 * register 1:
 *	meaning of bits unknown (but vendor software reads this, so just do the same)
 *
 * The run state values occur in this order:
 * 0x85E2: pre-sampling (for samples before trigger position, capture ratio > 0%)
 * 0x85EA: pre-sampling complete, now waiting for trigger (whilst sampling continuously)
 * 0x85EE: running
 * 0x85ED: idle
 */
static uint16_t run_state(const struct sr_dev_inst *sdi)
{
	uint16_t state;
	static uint16_t previous_state = 0;
	int ret;

	if ((ret = ctrl_in(sdi, CMD_FPGA_SPI, REG_RUN, 0, &state, sizeof(state))) != SR_OK) {
		sr_err("failed to read run state!");
		return ret;
	}

	/* This function is called about every 50ms.
	 * To avoid filling the log file with redundant information during long captures,
	 * just print a log message if status has changed.
	 */

	if (state != previous_state) {
		previous_state = state;
		if ((state & 0x0003) == 0x01) {
			sr_dbg("run_state: 0x%04x (%s)", state, "idle");
		}
		else if ((state & 0x000f) == 0x02) {
			sr_dbg("run_state: 0x%04x (%s)", state, "pre-trigger sampling");
		}
		else if ((state & 0x000f) == 0x0a) {
			sr_dbg("run_state: 0x%04x (%s)", state, "sampling, waiting for trigger");
		}
		else if ((state & 0x000f) == 0x0e) {
			sr_dbg("run_state: 0x%04x (%s)", state, "post-trigger sampling");
		}
		else {
			sr_dbg("run_state: 0x%04x", state);
		}
	}

	return state;
}

static int set_run_mode(const struct sr_dev_inst *sdi, uint8_t fast_blinking)
{
	int ret;

	if ((ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_RUN, 0, &fast_blinking, sizeof(fast_blinking))) != SR_OK) {
		sr_err("failed to send set-run-mode command %d", fast_blinking);
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

	if ((ret = ctrl_in(sdi, CMD_FPGA_SPI, REG_SAMPLING, 0, buf, sizeof(buf))) != SR_OK) {
		sr_err("failed to read capture info!");
		return ret;
	}

	rdptr = buf;
	devc->info.n_rep_packets = read_u32le_inc(&rdptr);
	devc->info.n_rep_packets_before_trigger = read_u32le_inc(&rdptr);
	devc->info.write_pos = read_u32le_inc(&rdptr);

	sr_dbg("capture info: n_rep_packets: 0x%08x/%d, before_trigger: 0x%08x/%d, write_pos: 0x%08x%d",
	       devc->info.n_rep_packets, devc->info.n_rep_packets,
	       devc->info.n_rep_packets_before_trigger, devc->info.n_rep_packets_before_trigger,
	       devc->info.write_pos, devc->info.write_pos);

	if (devc->info.n_rep_packets % 5)
		sr_warn("number of packets is not as expected multiples of 5: %d", devc->info.n_rep_packets);

	return SR_OK;
}

SR_PRIV int la2016_upload_firmware(struct sr_context *sr_ctx, libusb_device *dev, uint16_t product_id)
{
	char fw_file[1024];
	snprintf(fw_file, sizeof(fw_file) - 1, UC_FIRMWARE, product_id);
	return ezusb_upload_firmware(sr_ctx, dev, USB_CONFIGURATION, fw_file);
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
	if ((ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_CAPT_MODE, 0, &cmd, sizeof(cmd))) != SR_OK) {
		sr_err("failed to send stop sampling command");
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
	return set_run_mode(sdi, 3);
}

SR_PRIV int la2016_stop_acquisition(const struct sr_dev_inst *sdi)
{
	return set_run_mode(sdi, 0);
}

SR_PRIV int la2016_abort_acquisition(const struct sr_dev_inst *sdi)
{
	return la2016_stop_acquisition(sdi);
}

SR_PRIV int la2016_has_triggered(const struct sr_dev_inst *sdi)
{
	uint16_t state;

	state = run_state(sdi);

	return (state & 0x3) == 1;
}

SR_PRIV int la2016_start_retrieval(const struct sr_dev_inst *sdi, libusb_transfer_cb_fn cb)
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

	if ((ret = get_capture_info(sdi)) != SR_OK)
		return ret;

	devc->n_transfer_packets_to_read = devc->info.n_rep_packets / NUM_PACKETS_IN_CHUNK;
	devc->n_bytes_to_read = devc->n_transfer_packets_to_read * TRANSFER_PACKET_LENGTH;
	devc->read_pos = devc->info.write_pos - devc->n_bytes_to_read;
	devc->n_reps_until_trigger = devc->info.n_rep_packets_before_trigger;

	sr_dbg("want to read %d tfer-packets starting from pos %d",
	       devc->n_transfer_packets_to_read, devc->read_pos);

	if ((ret = ctrl_out(sdi, CMD_BULK_RESET, 0x00, 0, NULL, 0)) != SR_OK) {
		sr_err("failed to reset bulk state");
		return ret;
	}
	sr_dbg("will read from 0x%08x, 0x%08x bytes", devc->read_pos, devc->n_bytes_to_read);
	wrptr = wrbuf;
	write_u32le_inc(&wrptr, devc->read_pos);
	write_u32le_inc(&wrptr, devc->n_bytes_to_read);
	if ((ret = ctrl_out(sdi, CMD_FPGA_SPI, REG_BULK, 0, wrbuf, wrptr - wrbuf)) != SR_OK) {
		sr_err("failed to send bulk config");
		return ret;
	}
	if ((ret = ctrl_out(sdi, CMD_BULK_START, 0x00, 0, NULL, 0)) != SR_OK) {
		sr_err("failed to unblock bulk transfers");
		return ret;
	}

	to_read = devc->n_bytes_to_read;
	/* choose a buffer size for all of the usb transfers */
	if (to_read >= LA2016_USB_BUFSZ)
		to_read = LA2016_USB_BUFSZ; /* multiple transfers */
	else /* one transfer, make buffer size some multiple of LA2016_EP6_PKTSZ */
		to_read = (to_read + (LA2016_EP6_PKTSZ-1)) & ~(LA2016_EP6_PKTSZ-1);
	buffer = g_try_malloc(to_read);
	if (!buffer) {
		sr_err("Failed to allocate %d bytes for bulk transfer", to_read);
		return SR_ERR_MALLOC;
	}

	devc->transfer = libusb_alloc_transfer(0);
	libusb_fill_bulk_transfer(
		devc->transfer, usb->devhdl,
		0x86, buffer, to_read,
		cb, (void *)sdi, DEFAULT_TIMEOUT_MS);

	if ((ret = libusb_submit_transfer(devc->transfer)) != 0) {
		sr_err("Failed to submit transfer: %s.", libusb_error_name(ret));
		libusb_free_transfer(devc->transfer);
		devc->transfer = NULL;
		g_free(buffer);
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int la2016_init_device(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint16_t state;
	uint8_t buf[8];
	int16_t purchase_date_bcd[2];
	uint8_t magic;
	int ret;

	devc = sdi->priv;

	/* Four bytes of eeprom at 0x20 are purchase year & month in BCD format, with 16bit
	 * complemented checksum; e.g. 2004DFFB = 2020-April.
	 * This helps to identify the age of devices if unknown magic numbers occur.
	 */
	if ((ret = ctrl_in(sdi, CMD_EEPROM, 0x20, 0, purchase_date_bcd, sizeof(purchase_date_bcd))) != SR_OK) {
		sr_err("failed to read eeprom purchase_date_bcd");
	}
	else {
		sr_dbg("purchase date: 20%02hx-%02hx", (purchase_date_bcd[0]) & 0x00ff, (purchase_date_bcd[0] >> 8) & 0x00ff);
		if (purchase_date_bcd[0] != (0x0ffff & ~purchase_date_bcd[1])) {
			sr_err("purchase date: checksum failure");
		}
	}

	/*
	 * There are four known kingst logic analyser devices which use this same usb vid and pid:
	 * LA2016, LA1016 and the older revision of each of these. They all use the same hardware
	 * and the same FX2 mcu firmware but each requires a different fpga bitstream. They are
	 * differentiated by a 'magic' byte within the 8 bytes of EEPROM from address 0x08.
	 * For example;
	 *
	 * magic=0x08
	 *  | ~magic=0xf7
	 *  | |
	 * 08F7000008F710EF
	 *          | |
	 *          | ~magic-backup
	 *          magic-backup
	 *
	 * It seems that only these magic bytes are used, other bytes shown above are 'don't care'.
	 * Changing the magic byte on newer device to older magic causes OEM software to load
	 * the older fpga bitstream. The device then functions but has channels out of order.
	 * It's likely the bitstreams were changed to move input channel pins due to PCB changes.
	 *
	 * magic 9 == LA1016a using "kingst-la1016a1-fpga.bitstream" (latest v1.3.0 PCB, perhaps others)
	 * magic 8 == LA2016a using "kingst-la2016a1-fpga.bitstream" (latest v1.3.0 PCB, perhaps others)
	 * magic 3 == LA1016 using "kingst-la1016-fpga.bitstream"
	 * magic 2 == LA2016 using "kingst-la2016-fpga.bitstream"
	 *
	 * This was all determined by altering the eeprom contents of an LA2016 and LA1016 and observing
	 * the vendor software actions, either raising errors or loading specific bitstreams.
	 *
	 * Note:
	 * An LA1016 cannot be converted to an LA2016 by changing the magic number - the bitstream
	 * will not authenticate with ic U10, which has different security coding for each device type.
	 */

	if ((ret = ctrl_in(sdi, CMD_EEPROM, 0x08, 0, &buf, sizeof(buf))) != SR_OK) {
		sr_err("failed to read eeprom device identifier bytes");
		return ret;
	}

	magic = 0;
	if (buf[0] == (0x0ff & ~buf[1])) {
		/* primary copy of magic passes complement check */
		magic = buf[0];
	}
	else if (buf[4] == (0x0ff & ~buf[5])) {
		/* backup copy of magic passes complement check */
		sr_dbg("device_type: using backup copy of magic number");
		magic = buf[4];
	}

	sr_dbg("device_type: magic number is %hhu", magic);

	/* select the correct fpga bitstream for this device */
	switch (magic) {
	case 2:
		ret = upload_fpga_bitstream(sdi, FPGA_FW_LA2016);
		devc->max_samplerate = MAX_SAMPLE_RATE_LA2016;
		break;
	case 3:
		ret = upload_fpga_bitstream(sdi, FPGA_FW_LA1016);
		devc->max_samplerate = MAX_SAMPLE_RATE_LA1016;
		break;
	case 8:
		ret = upload_fpga_bitstream(sdi, FPGA_FW_LA2016A);
		devc->max_samplerate = MAX_SAMPLE_RATE_LA2016;
		break;
	case 9:
		ret = upload_fpga_bitstream(sdi, FPGA_FW_LA1016A);
		devc->max_samplerate = MAX_SAMPLE_RATE_LA1016;
		break;
	default:
		sr_err("device_type: device not supported; magic number indicates this is not a LA2016 or LA1016");
		return SR_ERR;
	}

	if (ret != SR_OK) {
		sr_err("failed to upload fpga bitstream");
		return ret;
	}

	state = run_state(sdi);
	if (state != 0x85e9) {
		sr_warn("expect run state to be 0x85e9, but it reads 0x%04x", state);
	}

	if ((ret = ctrl_out(sdi, CMD_BULK_RESET, 0x00, 0, NULL, 0)) != SR_OK) {
		sr_err("failed to send CMD_BULK_RESET");
		return ret;
	}

	sr_dbg("device should be initialized");

	return set_defaults(sdi);
}

SR_PRIV int la2016_deinit_device(const struct sr_dev_inst *sdi)
{
	int ret;

	if ((ret = ctrl_out(sdi, CMD_FPGA_ENABLE, 0x00, 0, NULL, 0)) != SR_OK) {
		sr_err("failed to send deinit command");
		return ret;
	}

	return SR_OK;
}
