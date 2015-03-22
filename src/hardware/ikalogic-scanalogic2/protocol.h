/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Marc Schink <sigrok-dev@marcschink.de>
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

#ifndef LIBSIGROK_HARDWARE_IKALOGIC_SCANALOGIC2_PROTOCOL_H
#define LIBSIGROK_HARDWARE_IKALOGIC_SCANALOGIC2_PROTOCOL_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "ikalogic-scanalogic2"

#define VENDOR_NAME			"IKALOGIC"
#define MODEL_NAME			"Scanalogic-2"

#define USB_VID_PID			"20a0.4123"
#define USB_INTERFACE			0
#define USB_TIMEOUT_MS			(5 * 1000)

#define USB_REQUEST_TYPE_IN		(LIBUSB_REQUEST_TYPE_CLASS | \
	LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_IN)

#define USB_REQUEST_TYPE_OUT		(LIBUSB_REQUEST_TYPE_CLASS | \
	LIBUSB_RECIPIENT_INTERFACE | LIBUSB_ENDPOINT_OUT)

#define USB_HID_GET_REPORT		0x01
#define USB_HID_SET_REPORT		0x09
#define USB_HID_REPORT_TYPE_FEATURE	0x300

#define NUM_SAMPLERATES			11
#define NUM_CHANNELS			4

/*
 * Number of sample bytes and samples the device can acquire. Note that the
 * vendor software can acquire 32736 sample bytes only but the device is capable
 * to acquire up to 32766 sample bytes.
 */
#define MAX_DEV_SAMPLE_BYTES		32766
#define MAX_DEV_SAMPLES			(MAX_INT_SAMPLE_BYTES * 8)

/* Number of sample bytes and samples the driver can acquire. */
#define MAX_SAMPLE_BYTES		(MAX_DEV_SAMPLE_BYTES - 1)
#define MAX_SAMPLES			(MAX_SAMPLE_BYTES * 8)

/* Maximum time that the trigger can be delayed in milliseconds. */
#define MAX_AFTER_TRIGGER_DELAY		65000

#define PACKET_LENGTH			128

/* Number of sample bytes per packet where a sample byte contains 8 samples. */
#define PACKET_NUM_SAMPLE_BYTES		124

/* Number of samples per packet. */
#define PACKET_NUM_SAMPLES		(PACKET_NUM_SAMPLE_BYTES * 8)

#define DEFAULT_SAMPLERATE		SR_KHZ(1.25)

/*
 * Time interval between the last status of available data received and the
 * moment when the next status request will be sent in microseconds.
 */
#define WAIT_DATA_READY_INTERVAL	1500000

#define CMD_SAMPLE			0x01
#define CMD_RESET			0x02
#define CMD_IDLE			0x07
#define CMD_INFO			0x0a

#define TRIGGER_CHANNEL_ALL		0x00
#define TRIGGER_CHANNEL_0		0x01
#define TRIGGER_CHANNEL_1		0x02
#define TRIGGER_CHANNEL_2		0x03

#define TRIGGER_TYPE_NEGEDGE		0x00
#define TRIGGER_TYPE_POSEDGE		0x01
#define TRIGGER_TYPE_ANYEDGE		0x02
#define TRIGGER_TYPE_NONE		0x03

#define STATUS_DATA_READY		0x60
#define STATUS_WAITING_FOR_TRIGGER	0x61
#define STATUS_SAMPLING			0x62
#define STATUS_DEVICE_READY		0x63

struct device_info {
	/* Serial number of the device. */
	uint32_t serial;

	/* Major version of the firmware. */
	uint8_t fw_ver_major;

	/* Minor version of the firmware. */
	uint8_t fw_ver_minor;
};

enum {
	STATE_IDLE = 0,
	STATE_SAMPLE,
	STATE_WAIT_DATA_READY,
	STATE_RECEIVE_DATA,
	STATE_RESET_AND_IDLE,
	STATE_WAIT_DEVICE_READY
};

/** Private, per-device-instance driver context. */
struct dev_context {
	/* Current selected samplerate. */
	uint64_t samplerate;

	/* Device specific identifier for the current samplerate. */
	uint8_t samplerate_id;

	/* Current sampling limit. */
	uint64_t limit_samples;

	/* Calculated number of pre-trigger samples. */
	uint64_t pre_trigger_samples;

	/* Number of pre- and post-trigger sample bytes to acquire. */
	uint16_t pre_trigger_bytes;
	uint16_t post_trigger_bytes;

	/* Device specific settings for the trigger. */
	uint8_t trigger_channel;
	uint8_t trigger_type;

	unsigned int capture_ratio;

	/* Time that the trigger will be delayed in milliseconds. */
	uint16_t after_trigger_delay;

	void *cb_data;

	/* Array to provide an index based access to all channels. */
	const struct sr_channel *channels[NUM_CHANNELS];

	struct libusb_transfer *xfer_in, *xfer_out;

	/*
	 * Buffer to store setup and payload data for incoming and outgoing
	 * transfers.
	 */
	uint8_t xfer_buf_in[LIBUSB_CONTROL_SETUP_SIZE + PACKET_LENGTH];
	uint8_t xfer_buf_out[LIBUSB_CONTROL_SETUP_SIZE + PACKET_LENGTH];

	/* Pointers to the payload of incoming and outgoing transfers. */
	uint8_t *xfer_data_in, *xfer_data_out;

	/* Current state of the state machine */
	unsigned int state;

	/* Next state of the state machine. */
	unsigned int next_state;

	/*
	 * Locking variable to ensure that no status about available data will
	 * be requested until the last status was received.
	 */
	gboolean wait_data_ready_locked;

	/*
	 * Time when the last response about the status of available data was
	 * received.
	 */
	int64_t wait_data_ready_time;

	/*
	 * Indicates that stopping of the acquisition is currently in progress.
	 */
	gboolean stopping_in_progress;

	/*
	 * Buffer which contains the samples received from the device for each
	 * channel except the last one. The samples of the last channel will be
	 * processed directly after they will be received.
	 */
	uint8_t sample_buffer[NUM_CHANNELS - 1][MAX_DEV_SAMPLE_BYTES];

	/* Expected number of sample packets for each channel. */
	uint16_t num_sample_packets;

	/* Number of samples already processed. */
	uint64_t samples_processed;

	/* Sample packet number that is currently processed. */
	uint16_t sample_packet;

	/* Channel number that is currently processed. */
	uint8_t channel;

	/* Number of enabled channels. */
	unsigned int num_enabled_channels;

	/* Array to provide a sequential access to all enabled channel indices. */
	uint8_t channel_map[NUM_CHANNELS];

	/* Indicates whether a transfer failed. */
	gboolean transfer_error;
};

SR_PRIV int ikalogic_scanalogic2_receive_data(int fd, int revents, void *cb_data);
SR_PRIV void sl2_receive_transfer_in(struct libusb_transfer *transfer);
SR_PRIV void sl2_receive_transfer_out(struct libusb_transfer *transfer);
SR_PRIV int sl2_set_samplerate(const struct sr_dev_inst *sdi,
		uint64_t samplerate);
SR_PRIV int sl2_set_limit_samples(const struct sr_dev_inst *sdi,
				  uint64_t limit_samples);
SR_PRIV int sl2_convert_trigger(const struct sr_dev_inst *sdi);
SR_PRIV int sl2_set_capture_ratio(const struct sr_dev_inst *sdi,
				  uint64_t capture_ratio);
SR_PRIV int sl2_set_after_trigger_delay(const struct sr_dev_inst *sdi,
					uint64_t after_trigger_delay);
SR_PRIV void sl2_calculate_trigger_samples(const struct sr_dev_inst *sdi);
SR_PRIV int sl2_get_device_info(struct sr_usb_dev_inst usb,
		struct device_info *dev_info);
SR_PRIV int sl2_transfer_in(libusb_device_handle *dev_handle, uint8_t *data);
SR_PRIV int sl2_transfer_out(libusb_device_handle *dev_handle, uint8_t *data);

#endif
