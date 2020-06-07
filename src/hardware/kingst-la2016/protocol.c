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

#define FPGA_FIRMWARE	"kingst-la2016a-fpga.bitstream"
#define UC_FIRMWARE	"kingst-la-%04x.fw"

#define MAX_SAMPLE_RATE  SR_MHZ(200)
#define MAX_SAMPLE_DEPTH 10e9
#define MAX_PWM_FREQ     SR_MHZ(20)
#define PWM_CLOCK        SR_MHZ(200)

/* registers for control request 32: */
#define CTRL_RUN         0x00
#define CTRL_PWM_EN      0x02
#define CTRL_BULK        0x10 /* can be read to get 12 byte sampling_info (III) */
#define CTRL_SAMPLING    0x20
#define CTRL_TRIGGER     0x30
#define CTRL_THRESHOLD   0x48
#define CTRL_PWM1        0x70
#define CTRL_PWM2        0x78

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

static int upload_fpga_bitstream(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct drv_context *drvc;
	struct sr_usb_dev_inst *usb;
	struct sr_resource bitstream;
	uint32_t cmd;
	uint8_t cmd_resp;
	uint8_t block[4096];
	int len, act_len;
	unsigned int pos;
	int ret;
	unsigned int zero_pad_to = 0x2c000;

	devc = sdi->priv;
	drvc = sdi->driver->context;
	usb = sdi->conn;

	sr_info("Uploading FPGA bitstream '%s'.", FPGA_FIRMWARE);

	ret = sr_resource_open(drvc->sr_ctx, &bitstream, SR_RESOURCE_FIRMWARE, FPGA_FIRMWARE);
	if (ret != SR_OK) {
		sr_err("could not find la2016 firmware %s!", FPGA_FIRMWARE);
		return ret;
	}

	devc->bitstream_size = (uint32_t)bitstream.size;
	WL32(&cmd, devc->bitstream_size);
	if ((ret = ctrl_out(sdi, 80, 0x00, 0, &cmd, sizeof(cmd))) != SR_OK) {
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

	if ((ret = ctrl_in(sdi, 80, 0x00, 0, &cmd_resp, sizeof(cmd_resp))) != SR_OK) {
		sr_err("failed to read response after FPGA bitstream upload");
		return ret;
	}
	if (cmd_resp != 0) {
		sr_err("after fpga bitstream upload command response is 0x%02x, expect 0!", cmd_resp);
		return SR_ERR;
	}

	g_usleep(30000);

	if ((ret = ctrl_out(sdi, 16, 0x01, 0, NULL, 0)) != SR_OK) {
		sr_err("failed enable fpga");
		return ret;
	}

	g_usleep(40000);
	return SR_OK;
}

static int set_threshold_voltage(const struct sr_dev_inst *sdi, float voltage)
{
	struct dev_context *devc;
	float o1, o2, v1, v2, f;
	uint32_t cfg;
	int ret;

	devc = sdi->priv;
	o1 = 15859969; v1 = 0.45;
	o2 = 15860333; v2 = 1.65;
	f = (o2 - o1) / (v2 - v1);
	WL32(&cfg, (uint32_t)(o1 + (voltage - v1) * f));

	sr_dbg("set threshold voltage %.2fV", voltage);
	ret = ctrl_out(sdi, 32, CTRL_THRESHOLD, 0, &cfg, sizeof(cfg));
	if (ret != SR_OK) {
		sr_err("error setting new threshold voltage of %.2fV (%d)", voltage, RL16(&cfg));
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
	ret = ctrl_out(sdi, 32, CTRL_PWM_EN, 0, &cfg, sizeof(cfg));
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
	int CTRL_PWM[] = { CTRL_PWM1, CTRL_PWM2 };
	struct dev_context *devc;
	pwm_setting_dev_t cfg;
	pwm_setting_t *setting;
	int ret;

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

	pwm_setting_dev_le(cfg);
	ret = ctrl_out(sdi, 32, CTRL_PWM[which - 1], 0, &cfg, sizeof(cfg));
	if (ret != SR_OK) {
		sr_err("error setting new pwm%d config %d %d", which, cfg.period, cfg.duty);
		return ret;
	}
	setting = &devc->pwm_setting[which - 1];
	setting->freq = freq;
	setting->duty = duty;
	setting->dev = cfg;

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
	devc->cur_samplerate = 200000000;

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

	trigger_cfg_le(cfg);
	ret = ctrl_out(sdi, 32, CTRL_TRIGGER, 16, &cfg, sizeof(cfg));
	if (ret != SR_OK) {
		sr_err("error setting trigger config!");
		return ret;
	}

	return SR_OK;
}

static int set_sample_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	sample_config_t cfg;
	double clock_divisor;
	uint64_t psa;
	uint64_t total;
	int ret;

	devc = sdi->priv;
	total = 128 * 1024 * 1024;

	if (devc->cur_samplerate > MAX_SAMPLE_RATE) {
		sr_err("too high sample rate: %" PRIu64, devc->cur_samplerate);
		return SR_ERR;
	}

	clock_divisor = MAX_SAMPLE_RATE / (double)devc->cur_samplerate;
	if (clock_divisor > 0xffff)
		clock_divisor = 0xffff;
	cfg.clock_divisor = (uint16_t)(clock_divisor + 0.5);
	devc->cur_samplerate = MAX_SAMPLE_RATE / cfg.clock_divisor;

	if (devc->limit_samples > MAX_SAMPLE_DEPTH) {
		sr_err("too high sample depth: %" PRIu64, devc->limit_samples);
		return SR_ERR;
	}
	cfg.sample_depth = devc->limit_samples;

	devc->pre_trigger_size = (devc->capture_ratio * devc->limit_samples) / 100;

	psa = devc->pre_trigger_size * 256;
	cfg.psa = (uint32_t)(psa & 0xffffffff);
	cfg.u1  = (uint16_t)((psa >> 32) & 0xffff);
	cfg.u2 = (uint32_t)((total * devc->capture_ratio) / 100);

	sr_dbg("set sampling configuration %.0fkHz, %d samples, trigger-pos %d%%",
	       devc->cur_samplerate/1e3, (unsigned int)cfg.sample_depth, (unsigned int)devc->capture_ratio);

	sample_config_le(cfg);
	ret = ctrl_out(sdi, 32, CTRL_SAMPLING, 0, &cfg, sizeof(cfg));
	if (ret != SR_OK) {
		sr_err("error setting sample config!");
		return ret;
	}

	return SR_OK;
}

/**
 * lowest 2 bit are probably:
 * 2: recording
 * 1: finished
 * next 2 bit indicate whether we are still waiting for triggering
 * 0: waiting
 * 3: triggered
 */
static uint16_t run_state(const struct sr_dev_inst *sdi)
{
	uint16_t state;
	int ret;

	if ((ret = ctrl_in(sdi, 32, CTRL_RUN, 0, &state, sizeof(state))) != SR_OK) {
		sr_err("failed to read run state!");
		return ret;
	}
	sr_dbg("run_state: 0x%04x", state);

	return state;
}

static int set_run_mode(const struct sr_dev_inst *sdi, uint8_t fast_blinking)
{
	int ret;

	if ((ret = ctrl_out(sdi, 32, CTRL_RUN, 0, &fast_blinking, sizeof(fast_blinking))) != SR_OK) {
		sr_err("failed to send set-run-mode command %d", fast_blinking);
		return ret;
	}

	return SR_OK;
}

static int get_capture_info(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	if ((ret = ctrl_in(sdi, 32, CTRL_BULK, 0, &devc->info, sizeof(devc->info))) != SR_OK) {
		sr_err("failed to read capture info!");
		return ret;
	}
	capture_info_host(devc->info);

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
	return ezusb_upload_firmware(sr_ctx, dev, 0, fw_file);
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
	if ((ret = ctrl_out(sdi, 32, 0x03, 0, &cmd, sizeof(cmd))) != SR_OK) {
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
	uint32_t bulk_cfg[2];
	uint32_t to_read;
	uint8_t *buffer;

	devc = sdi->priv;
	usb = sdi->conn;

	if ((ret = get_capture_info(sdi)) != SR_OK)
		return ret;

	devc->n_transfer_packets_to_read = devc->info.n_rep_packets / 5;
	devc->n_bytes_to_read = devc->n_transfer_packets_to_read * sizeof(transfer_packet_t);
	devc->read_pos = devc->info.write_pos - devc->n_bytes_to_read;
	devc->n_reps_until_trigger = devc->info.n_rep_packets_before_trigger;

	sr_dbg("want to read %d tfer-packets starting from pos %d",
	       devc->n_transfer_packets_to_read, devc->read_pos);

	if ((ret = ctrl_out(sdi, 56, 0x00, 0, NULL, 0)) != SR_OK) {
		sr_err("failed to reset bulk state");
		return ret;
	}
	WL32(&bulk_cfg[0], devc->read_pos);
	WL32(&bulk_cfg[1], devc->n_bytes_to_read);
	sr_dbg("will read from 0x%08x, 0x%08x bytes", devc->read_pos, devc->n_bytes_to_read);
	if ((ret = ctrl_out(sdi, 32, CTRL_BULK, 0, &bulk_cfg, sizeof(bulk_cfg))) != SR_OK) {
		sr_err("failed to send bulk config");
		return ret;
	}
	if ((ret = ctrl_out(sdi, 48, 0x00, 0, NULL, 0)) != SR_OK) {
		sr_err("failed to unblock bulk transfers");
		return ret;
	}

	to_read = devc->n_bytes_to_read;
	if (to_read > LA2016_BULK_MAX)
		to_read = LA2016_BULK_MAX;

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
	int ret;
	uint32_t i1;
	uint32_t i2[2];
	uint16_t state;

	/* this unknown_cmd1 seems to depend on the FPGA bitstream */
	uint8_t unknown_cmd1_340[] = { 0xa3, 0x09, 0xc9, 0x8d, 0xe7, 0xad, 0x7a, 0x62, 0xb6, 0xd1, 0xbf };
	uint8_t unknown_cmd1_342[] = { 0xa3, 0x09, 0xc9, 0xf4, 0x32, 0x4c, 0x4d, 0xee, 0xab, 0xa0, 0xdd };
	uint8_t expected_unknown_resp1_340[] = { 0xa3, 0x10, 0xda, 0x66, 0x6b, 0x93, 0x5c, 0x55, 0x38, 0x50, 0x39, 0x51, 0x98, 0x86, 0x5d, 0x06, 0x7c, 0xea };
	uint8_t expected_unknown_resp1_342[] = { 0xa3, 0x10, 0xb3, 0x92, 0x7b, 0xd8, 0x6b, 0xca, 0xa5, 0xab, 0x42, 0x6e, 0xda, 0xcd, 0x9d, 0xf1, 0x31, 0x2f };
	uint8_t unknown_resp1[sizeof(expected_unknown_resp1_340)];
	uint8_t *expected_unknown_resp1;
	uint8_t *unknown_cmd1;

	uint8_t unknown_cmd2[] = { 0xa3, 0x01, 0xca };
	uint8_t expected_unknown_resp2[] = { 0xa3, 0x08, 0x06, 0x83, 0x96, 0x29, 0x15, 0xe1, 0x92, 0x74, 0x00, 0x00 };
	uint8_t unknown_resp2[sizeof(expected_unknown_resp2)];

	devc = sdi->priv;

	if ((ret = ctrl_in(sdi, 162, 0x20, 0, &i1, sizeof(i1))) != SR_OK) {
		sr_err("failed to read i1");
		return ret;
	}
	sr_dbg("i1: 0x%08x", i1);

	if ((ret = ctrl_in(sdi, 162, 0x08, 0, &i2, sizeof(i2))) != SR_OK) {
		sr_err("failed to read i2");
		return ret;
	}
	sr_dbg("i2: 0x%08x, 0x%08x", i2[0], i2[1]);

	if ((ret = upload_fpga_bitstream(sdi)) != SR_OK) {
		sr_err("failed to upload fpga bitstream");
		return ret;
	}

	if (run_state(sdi) == 0xffff) {
		sr_err("run_state after fpga bitstream upload is 0xffff!");
		return SR_ERR;
	}

	if (devc->bitstream_size == 0x2b602) {
		// v3.4.0
		unknown_cmd1 = unknown_cmd1_340;
		expected_unknown_resp1 = expected_unknown_resp1_340;
	} else {
		// v3.4.2
		if (devc->bitstream_size != 0x2b839)
			sr_warn("the FPGA bitstream size %d is unknown. tested bistreams from vendor's version 3.4.0 and 3.4.2\n", devc->bitstream_size);
		unknown_cmd1 = unknown_cmd1_342;
		expected_unknown_resp1 = expected_unknown_resp1_342;
	}
	if ((ret = ctrl_out(sdi, 96, 0x00, 0, unknown_cmd1, sizeof(unknown_cmd1_340))) != SR_OK) {
		sr_err("failed to send unknown_cmd1");
		return ret;
	}
	g_usleep(80 * 1000);
	if ((ret = ctrl_in(sdi, 96, 0x00, 0, unknown_resp1, sizeof(unknown_resp1))) != SR_OK) {
		sr_err("failed to read unknown_resp1");
		return ret;
	}
	if (memcmp(unknown_resp1, expected_unknown_resp1, sizeof(unknown_resp1)))
		sr_dbg("unknown_cmd1 response is not as expected, this is to be expected...");

	state = run_state(sdi);
	if (state != 0x85e9)
		sr_warn("expect run state to be 0x85e9, but it reads 0x%04x", state);

	if ((ret = ctrl_out(sdi, 96, 0x00, 0, unknown_cmd2, sizeof(unknown_cmd2))) != SR_OK) {
		sr_err("failed to send unknown_cmd2");
		return ret;
	}
	g_usleep(80 * 1000);
	if ((ret = ctrl_in(sdi, 96, 0x00, 0, unknown_resp2, sizeof(unknown_resp2))) != SR_OK) {
		sr_err("failed to read unknown_resp2");
		return ret;
	}
	if (memcmp(unknown_resp2, expected_unknown_resp2, sizeof(unknown_resp2)))
		sr_dbg("unknown_cmd2 response is not as expected!");

	if ((ret = ctrl_out(sdi, 56, 0x00, 0, NULL, 0)) != SR_OK) {
		sr_err("failed to send unknown_cmd3");
		return ret;
	}
	sr_dbg("device should be initialized");

	return set_defaults(sdi);
}

SR_PRIV int la2016_deinit_device(const struct sr_dev_inst *sdi)
{
	int ret;

	if ((ret = ctrl_out(sdi, 16, 0x00, 0, NULL, 0)) != SR_OK) {
		sr_err("failed to send deinit command");
		return ret;
	}

	return SR_OK;
}
