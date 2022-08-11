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

#ifndef LIBSIGROK_HARDWARE_KINGST_LA2016_PROTOCOL_H
#define LIBSIGROK_HARDWARE_KINGST_LA2016_PROTOCOL_H

#include <libsigrok/libsigrok.h>
#include <stdint.h>

#define LOG_PREFIX	"kingst-la2016"

#define LA2016_VID		0x77a1
#define LA2016_PID		0x01a2
#define LA2016_IPRODUCT_INDEX	2
#define USB_INTERFACE		0
#define USB_CONFIGURATION	1
#define USB_EP_FPGA_BITSTREAM	2
#define USB_EP_CAPTURE_DATA	6

/*
 * On Windows sigrok uses WinUSB RAW_IO policy which requires the
 * USB transfer buffer size to be a multiple of the endpoint max packet
 * size, which is 512 bytes in this case. Also, the maximum allowed size
 * of the transfer buffer is normally read from WinUSB_GetPipePolicy API
 * but libusb does not expose this function. Typically, max size is 2MB.
 */
#define LA2016_EP6_PKTSZ	512 /* Max packet size of USB endpoint 6. */
#define LA2016_USB_BUFSZ	(512 * 1024) /* 512KiB buffer. */
#define LA2016_USB_XFER_COUNT	8 /* Size of USB bulk transfers pool. */

/* USB communication timeout during regular operation. */
#define DEFAULT_TIMEOUT_MS	200
#define CAPTURE_TIMEOUT_MS	500

/*
 * Check for MCU firmware to take effect after upload. Check the device
 * presence for a maximum period of time, delay between checks in that
 * phase. Allow for the device to vanish after upload and before checks,
 * to not mistake its earlier incarnation for the successful operation
 * of the most recently loaded firmware.
 */
#define RENUM_CHECK_PERIOD_MS	3000
#define RENUM_GONE_DELAY_MS	1800
#define RENUM_POLL_INTERVAL_MS	200

/*
 * The device expects some zero padding to follow the content of the
 * file which contains the FPGA bitstream. Specify the chunk size here.
 */
#define LA2016_EP2_PADDING	4096

/*
 * Whether the logic input threshold voltage is a config item of the
 * "Logic" channel group or a global config item of the device. Ideally
 * it would be the former (being strictly related to the Logic channels)
 * but mainline applications work better with the latter, and many other
 * device drivers implement it that way, too.
 */
#define WITH_THRESHOLD_DEVCFG	1

#define LA2016_THR_VOLTAGE_MIN	0.40
#define LA2016_THR_VOLTAGE_MAX	4.00

/* Properties related to the layout of capture data downloads. */
#define TRANSFER_PACKET_LENGTH	16
#define LA2016_NUM_SAMPLES_MAX	(UINT64_C(10 * 1000 * 1000 * 1000))

/* Maximum device capabilities. May differ between models. */
#define MAX_PWM_FREQ		SR_MHZ(20)
#define PWM_CLOCK		SR_MHZ(200)	/* 200MHz for both LA2016 and LA1016 */

#define LA2016_NUM_PWMCH_MAX	2

/* Streaming mode related thresholds. Not enforced, used for warnings. */
#define LA2016_STREAM_MBPS_MAX	200	/* In units of Mbps. */
#define LA2016_STREAM_PUSH_THR	16	/* In units of Mbps. */
#define LA2016_STREAM_PUSH_IVAL	250	/* In units of ms. */

/*
 * Whether to de-initialize the device hardware in the driver's close
 * callback. It is desirable to e.g. configure PWM channels and leave
 * the generator running after the application shuts down. Users can
 * always disable channels on their way out if they want to.
 */
#define WITH_DEINIT_IN_CLOSE	0

#define LA2016_CONVBUFFER_SIZE	(4 * 1024 * 1024)

struct kingst_model {
	uint8_t magic, magic2;	/* EEPROM magic byte values. */
	const char *name;	/* User perceived model name. */
	const char *fpga_stem;	/* Bitstream filename stem. */
	uint64_t samplerate;	/* Max samplerate in Hz. */
	size_t channel_count;	/* Max channel count (16, 32). */
	uint64_t memory_bits;	/* RAM capacity in Gbit (1, 2, 4). */
	uint64_t baseclock;	/* Base clock to derive samplerate from. */
};

struct dev_context {
	uint16_t usb_pid;
	char *mcu_firmware;
	char *fpga_bitstream;
	uint64_t fw_uploaded; /* Timestamp of most recent FW upload. */
	uint8_t identify_magic, identify_magic2;
	const struct kingst_model *model;
	struct sr_channel_group *cg_logic, *cg_pwm;

	/* User specified parameters. */
	struct pwm_setting {
		gboolean enabled;
		float freq;
		float duty;
	} pwm_setting[LA2016_NUM_PWMCH_MAX];
	size_t threshold_voltage_idx;
	uint64_t samplerate;
	struct sr_sw_limits sw_limits;
	uint64_t capture_ratio;
	gboolean continuous;

	/* Internal acquisition and download state. */
	gboolean trigger_involved;
	gboolean frame_begin_sent;
	gboolean completion_seen;
	gboolean download_finished;
	uint32_t packets_per_chunk;
	struct capture_info {
		uint32_t n_rep_packets;
		uint32_t n_rep_packets_before_trigger;
		uint32_t write_pos;
	} info;
	uint32_t n_transfer_packets_to_read; /* each with 5 acq packets */
	uint32_t n_bytes_to_read;
	uint32_t n_reps_until_trigger;
	gboolean trigger_marked;
	uint64_t total_samples;
	uint32_t read_pos;

	struct feed_queue_logic *feed_queue;
	GSList *transfers;
	size_t transfer_bufsize;
	struct stream_state_t {
		size_t enabled_count;
		uint32_t enabled_mask;
		uint32_t channel_masks[32];
		size_t channel_index;
		uint32_t sample_data[32];
		uint64_t flush_period_ms;
		uint64_t last_flushed;
	} stream;
};

SR_PRIV int la2016_upload_firmware(const struct sr_dev_inst *sdi,
	struct sr_context *sr_ctx, libusb_device *dev, gboolean skip_upload);
SR_PRIV int la2016_identify_device(const struct sr_dev_inst *sdi,
	gboolean show_message);
SR_PRIV int la2016_init_hardware(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_deinit_hardware(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_write_pwm_config(const struct sr_dev_inst *sdi, size_t idx);
SR_PRIV int la2016_setup_acquisition(const struct sr_dev_inst *sdi,
	double voltage);
SR_PRIV int la2016_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_abort_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_receive_data(int fd, int revents, void *cb_data);
SR_PRIV void la2016_release_resources(const struct sr_dev_inst *sdi);

#endif
