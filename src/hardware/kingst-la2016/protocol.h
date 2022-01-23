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
#define LA2016_USB_BUFSZ	(256 * 2 * LA2016_EP6_PKTSZ) /* 256KiB buffer. */

/* USB communication timeout during regular operation. */
#define DEFAULT_TIMEOUT_MS	200

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
#define LA2016_EP2_PADDING	2048

#define LA2016_THR_VOLTAGE_MIN	0.40
#define LA2016_THR_VOLTAGE_MAX	4.00

#define LA2016_NUM_SAMPLES_MIN	256
#define LA2016_NUM_SAMPLES_MAX	(10UL * 1000 * 1000 * 1000)

#define LA2016_NUM_PWMCH_MAX	2

#define LA2016_CONVBUFFER_SIZE	(4 * 1024 * 1024)

struct pwm_setting_dev {
	uint32_t period;
	uint32_t duty;
};

struct trigger_cfg {
	uint32_t channels;
	uint32_t enabled;
	uint32_t level;
	uint32_t high_or_falling;
};

struct capture_info {
	uint32_t n_rep_packets;
	uint32_t n_rep_packets_before_trigger;
	uint32_t write_pos;
};

#define NUM_PACKETS_IN_CHUNK	5
#define TRANSFER_PACKET_LENGTH	16

struct pwm_setting {
	gboolean enabled;
	float freq;
	float duty;
};

struct dev_context {
	uint64_t fw_uploaded; /* Timestamp of most recent FW upload. */

	/* User specified parameters. */
	struct pwm_setting pwm_setting[LA2016_NUM_PWMCH_MAX];
	unsigned int threshold_voltage_idx;
	float threshold_voltage;
	uint64_t max_samplerate;
	uint64_t cur_samplerate;
	uint64_t limit_samples;
	uint64_t capture_ratio;
	uint16_t cur_channels;

	/* Internal acquisition and download state. */
	gboolean trigger_involved;
	gboolean completion_seen;
	gboolean download_finished;
	struct capture_info info;
	uint32_t n_transfer_packets_to_read; /* each with 5 acq packets */
	uint32_t n_bytes_to_read;
	uint32_t n_reps_until_trigger;
	gboolean trigger_marked;
	uint64_t total_samples;
	uint32_t read_pos;

	size_t convbuffer_size;
	uint8_t *convbuffer;
	struct libusb_transfer *transfer;
};

SR_PRIV int la2016_upload_firmware(struct sr_context *sr_ctx,
	libusb_device *dev, uint16_t product_id);
SR_PRIV int la2016_setup_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_abort_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_receive_data(int fd, int revents, void *cb_data);
SR_PRIV int la2016_init_device(const struct sr_dev_inst *sdi);
SR_PRIV int la2016_deinit_device(const struct sr_dev_inst *sdi);

#endif
