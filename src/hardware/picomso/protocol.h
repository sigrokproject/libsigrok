/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026
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

#ifndef LIBSIGROK_HARDWARE_PICOMSO_PROTOCOL_H
#define LIBSIGROK_HARDWARE_PICOMSO_PROTOCOL_H

#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "picomso"

#define USB_INTERFACE                   0
#define USB_CONFIGURATION               1

#define NUM_CHANNELS                    16
#define NUM_ANALOG_CHANNELS             3

#define PICOMSO_USB_TIMEOUT_MS          500
#define PICOMSO_POLL_INTERVAL_MS        10
#define PICOMSO_PROTOCOL_IO_BUFFER_SIZE 256u
#define PICOMSO_PROTOCOL_ERROR_TEXT_MAX 64u

#define PICOMSO_BULK_EP_IN              0x86
#define PICOMSO_CTRL_REQUEST_OUT        0x01

#define PICOMSO_PROTOCOL_VERSION_MAJOR  0
#define PICOMSO_PROTOCOL_VERSION_MINOR  4

#define PICOMSO_PACKET_MAGIC            UINT16_C(0x4D53)
#define PICOMSO_PACKET_HEADER_SIZE      8u

#define PICOMSO_DEFAULT_LIMIT_SAMPLES   1000u

#define PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT 4u
#define PICOMSO_DATA_BLOCK_SIZE 64u

#define PICOMSO_CAP_LOGIC UINT32_C(1 << 0)
#define PICOMSO_CAP_SCOPE UINT32_C(1 << 1)

enum picomso_msg_type {
    PICOMSO_MSG_GET_INFO         = 0x01,
    PICOMSO_MSG_GET_CAPABILITIES = 0x02,
    PICOMSO_MSG_GET_STATUS       = 0x03,
    PICOMSO_MSG_SET_MODE         = 0x04,
    PICOMSO_MSG_REQUEST_CAPTURE  = 0x05,
    PICOMSO_MSG_READ_DATA_BLOCK  = 0x06,

    PICOMSO_MSG_ACK        = 0x80,
    PICOMSO_MSG_ERROR      = 0x81,
    PICOMSO_MSG_DATA_BLOCK = 0x82,
};

enum picomso_status_code {
    PICOMSO_STATUS_OK            = 0x00,
    PICOMSO_STATUS_ERR_UNKNOWN   = 0x01,
    PICOMSO_STATUS_ERR_BAD_MAGIC = 0x02,
    PICOMSO_STATUS_ERR_BAD_LEN   = 0x03,
    PICOMSO_STATUS_ERR_BAD_MODE  = 0x04,
    PICOMSO_STATUS_ERR_VERSION   = 0x05,
};

/*
 * Stream selection mask used by the control plane.
 *
 * 0x00: no streams enabled
 * 0x01: logic stream enabled
 * 0x02: scope stream enabled
 * 0x03: logic + scope enabled (mixed)
 */
enum picomso_stream_mask {
    PICOMSO_STREAM_NONE  = 0x00,
    PICOMSO_STREAM_LOGIC = 1u << 0,
    PICOMSO_STREAM_SCOPE = 1u << 1,
};

/*
 * Concrete stream identity for a single DATA_BLOCK packet.
 *
 * A transmitted block belongs to exactly one stream, even when the
 * device is configured with multiple enabled streams.
 */
enum picomso_stream_id {
    PICOMSO_STREAM_ID_NONE  = 0x00,
    PICOMSO_STREAM_ID_LOGIC = 0x01,
    PICOMSO_STREAM_ID_SCOPE = 0x02,
};

enum picomso_capture_state {
    PICOMSO_CAPTURE_IDLE    = 0x00,
    PICOMSO_CAPTURE_RUNNING = 0x01,
};

enum picomso_trigger_match {
    PICOMSO_TRIGGER_MATCH_LEVEL_LOW  = 0x00,
    PICOMSO_TRIGGER_MATCH_LEVEL_HIGH = 0x01,
    PICOMSO_TRIGGER_MATCH_EDGE_LOW   = 0x02,
    PICOMSO_TRIGGER_MATCH_EDGE_HIGH  = 0x03,
};

enum picomso_acq_state {
    PICOMSO_ACQ_IDLE,
    PICOMSO_ACQ_WAITING,
    PICOMSO_ACQ_READING,
};

struct picomso_profile {
    uint16_t vid;
    uint16_t pid;

    const char *vendor;
    const char *model;
    const char *model_version;

    const char *usb_manufacturer;
    const char *usb_product;
};

struct picomso_trigger_config {
    uint8_t is_enabled;
    uint8_t pin;
    uint8_t match;
};

struct picomso_request_capture {
    uint32_t total_samples;
    uint32_t rate;
    uint32_t pre_trigger_samples;
    struct picomso_trigger_config trigger[PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT];
    /*
     * Bitmask of selected analog channels to capture.  Bit N corresponds
     * to analog channel N (A0=bit0, A1=bit1, ...).  The firmware captures
     * only the selected channels and interleaves them in ascending channel
     * index order.  Set to 0 when the scope stream is not active.
     */
    uint8_t analog_channel_mask;
};

struct picomso_info {
    uint8_t protocol_version_major;
    uint8_t protocol_version_minor;
    char fw_id[32];
};

struct picomso_status {
    uint8_t streams;
    uint8_t capture_state;
};

struct picomso_data_block {
    uint8_t  stream_id;
    uint8_t  flags;
    uint16_t block_id;
    uint16_t data_len;
    uint8_t  data[PICOMSO_DATA_BLOCK_SIZE];
};

struct __attribute__((packed)) picomso_capabilities {
    uint8_t version;
    uint8_t size;

    uint8_t capabilities_flags;
    uint8_t max_logic_channels;
    uint8_t max_analog_channels;

    uint32_t max_samplerate_logic;
    uint32_t max_samplerate_scope;

    uint32_t max_samples_logic;
    uint32_t max_samples_scope;
};

struct dev_context {
    const struct picomso_profile *profile;
    char **channel_names;

    const uint64_t *samplerates;
    int num_samplerates;

    uint64_t cur_samplerate;
    uint64_t limit_samples;
    uint64_t capture_ratio;
    uint64_t sent_logic_samples;
    uint64_t sent_scope_samples;

    struct picomso_info info;
    struct picomso_capabilities fw_caps;

    uint8_t next_seq;
    uint8_t last_device_status;
    char last_error_text[PICOMSO_PROTOCOL_ERROR_TEXT_MAX];

    gboolean acq_aborted;
    enum picomso_acq_state acq_state;

    uint8_t enabled_streams;

    uint16_t expected_logic_block_id;
    uint16_t expected_scope_block_id;

    gint64 capture_deadline_us;

    GSList *enabled_analog_channels;
};

SR_PRIV int picomso_dev_open(struct sr_dev_inst *sdi, struct sr_dev_driver *di);
SR_PRIV struct dev_context *picomso_dev_new(void);
SR_PRIV int picomso_start_acquisition(const struct sr_dev_inst *sdi);
SR_PRIV void picomso_abort_acquisition(struct dev_context *devc);

#endif