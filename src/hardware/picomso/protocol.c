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

#include <config.h>
#include <glib.h>
#include "protocol.h"

#define PICOMSO_RESPONSE_TYPE_INFO         PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_CAPABILITIES PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_STATUS       PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_SET_MODE     PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_REQUEST      PICOMSO_MSG_ACK
#define PICOMSO_RESPONSE_TYPE_DATA_BLOCK   PICOMSO_MSG_DATA_BLOCK

#define PICOMSO_READ_DONE 1

static uint16_t read_u16_le(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8);
}

static void write_u16_le(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void write_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value & 0xffu);
    data[1] = (uint8_t)((value >> 8) & 0xffu);
    data[2] = (uint8_t)((value >> 16) & 0xffu);
    data[3] = (uint8_t)((value >> 24) & 0xffu);
}

static void clear_error_state(struct dev_context *devc)
{
    devc->last_device_status = (uint8_t)PICOMSO_STATUS_OK;
    devc->last_error_text[0] = '\0';
}

static void set_error_text(struct dev_context *devc,
    const char *text, size_t length)
{
    size_t copy_len;

    copy_len = length;
    if (copy_len >= sizeof(devc->last_error_text))
        copy_len = sizeof(devc->last_error_text) - 1u;

    if (copy_len > 0u && text)
        memcpy(devc->last_error_text, text, copy_len);
    devc->last_error_text[copy_len] = '\0';
}

static int send_request(const struct sr_dev_inst *sdi,
    uint8_t msg_type, const uint8_t *payload, uint16_t payload_len,
    uint8_t expected_response_type, uint8_t *response,
    size_t *response_len)
{
    struct dev_context *devc;
    struct sr_usb_dev_inst *usb;
    uint8_t request[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    uint8_t seq;
    uint16_t wire_length;
    int actual_length, ret;
    const uint8_t *payload_ptr;
    uint8_t msg_len;

    devc = sdi->priv;
    usb = sdi->conn;

    if (!response || !response_len || !usb || !usb->devhdl)
        return SR_ERR_ARG;

    if ((size_t)PICOMSO_PACKET_HEADER_SIZE + payload_len > sizeof(request))
        return SR_ERR_ARG;

    clear_error_state(devc);
    seq = devc->next_seq++;

    write_u16_le(request, PICOMSO_PACKET_MAGIC);
    request[2] = PICOMSO_PROTOCOL_VERSION_MAJOR;
    request[3] = PICOMSO_PROTOCOL_VERSION_MINOR;
    request[4] = msg_type;
    request[5] = seq;
    write_u16_le(request + 6, payload_len);

    if (payload_len > 0u && payload)
        memcpy(request + PICOMSO_PACKET_HEADER_SIZE, payload, payload_len);

    wire_length = (uint16_t)(PICOMSO_PACKET_HEADER_SIZE + payload_len);

    ret = libusb_control_transfer(usb->devhdl,
        LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT,
        PICOMSO_CTRL_REQUEST_OUT, 0x0000, 0x0000,
        request, wire_length, PICOMSO_USB_TIMEOUT_MS);
    if (ret < 0 || ret != wire_length) {
        sr_err("Unable to send request 0x%02x: %s.", msg_type,
            ret < 0 ? libusb_error_name(ret) : "short control write");
        return SR_ERR;
    }

    ret = libusb_bulk_transfer(usb->devhdl, PICOMSO_BULK_EP_IN, response,
        PICOMSO_PROTOCOL_IO_BUFFER_SIZE, &actual_length,
        PICOMSO_USB_TIMEOUT_MS);
    if (ret < 0) {
        sr_err("Unable to read response for request 0x%02x: %s.", msg_type,
            libusb_error_name(ret));
        return SR_ERR;
    }

    if ((size_t)actual_length < PICOMSO_PACKET_HEADER_SIZE)
        return SR_ERR;
    if (read_u16_le(response) != PICOMSO_PACKET_MAGIC)
        return SR_ERR;
    if (response[2] != PICOMSO_PROTOCOL_VERSION_MAJOR)
        return SR_ERR;
    if (response[5] != seq)
        return SR_ERR;

    *response_len = (size_t)actual_length;
    wire_length = read_u16_le(response + 6);

    if ((size_t)PICOMSO_PACKET_HEADER_SIZE + wire_length > (size_t)actual_length)
        return SR_ERR;

    if (response[4] == PICOMSO_MSG_ERROR) {
        payload_ptr = response + PICOMSO_PACKET_HEADER_SIZE;

        if (wire_length < 2u)
            return SR_ERR;

        devc->last_device_status = payload_ptr[0];
        msg_len = payload_ptr[1];

        if ((uint16_t)(2u + msg_len) > wire_length)
            return SR_ERR;

        set_error_text(devc, (const char *)(payload_ptr + 2), msg_len);
        return SR_ERR;
    }

    if (response[4] != expected_response_type)
        return SR_ERR;

    return SR_OK;
}

static int parse_ack_status(const uint8_t *response, size_t response_len)
{
    size_t payload_len;
    const uint8_t *payload;

    if (!response || response_len < PICOMSO_PACKET_HEADER_SIZE)
        return SR_ERR;

    payload_len = read_u16_le(response + 6);
    payload = response + PICOMSO_PACKET_HEADER_SIZE;

    if (payload_len == 0u)
        return SR_OK;
    if (payload_len < 1u)
        return SR_ERR;
    if (response_len < PICOMSO_PACKET_HEADER_SIZE + payload_len)
        return SR_ERR;
    if (payload[0] != (uint8_t)PICOMSO_STATUS_OK)
        return SR_ERR;

    return SR_OK;
}

static gboolean stream_mask_is_valid(uint8_t streams)
{
    const uint8_t valid_mask = PICOMSO_STREAM_LOGIC | PICOMSO_STREAM_SCOPE;
    return (streams & (uint8_t)~valid_mask) == 0u;
}

static gboolean stream_id_is_valid(uint8_t stream_id)
{
    return stream_id == PICOMSO_STREAM_ID_LOGIC
        || stream_id == PICOMSO_STREAM_ID_SCOPE;
}

static int command_get_info(const struct sr_dev_inst *sdi,
    struct picomso_info *info)
{
    uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    size_t response_len;
    size_t payload_len;
    const uint8_t *payload;
    int ret;

    if (!info)
        return SR_ERR_ARG;

    ret = send_request(sdi, PICOMSO_MSG_GET_INFO, NULL, 0u,
        PICOMSO_RESPONSE_TYPE_INFO, response, &response_len);
    if (ret != SR_OK)
        return ret;

    payload_len = read_u16_le(response + 6);
    if (payload_len < sizeof(*info))
        return SR_ERR;

    payload = response + PICOMSO_PACKET_HEADER_SIZE;
    info->protocol_version_major = payload[0];
    info->protocol_version_minor = payload[1];
    memset(info->fw_id, 0, sizeof(info->fw_id));
    memcpy(info->fw_id, payload + 2, sizeof(info->fw_id));
    info->fw_id[sizeof(info->fw_id) - 1u] = '\0';

    return SR_OK;
}

static int command_get_capabilities(const struct sr_dev_inst *sdi,
    struct picomso_capabilities *caps)
{
    uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    size_t response_len;
    const uint8_t *payload;
    uint16_t payload_len;

    int ret;

    if (!caps)
        return SR_ERR_ARG;

    ret = send_request(sdi, PICOMSO_MSG_GET_CAPABILITIES, NULL, 0u,
        PICOMSO_RESPONSE_TYPE_CAPABILITIES, response, &response_len);
    if (ret != SR_OK)
        return ret;

    payload_len = read_u16_le(response + 6);
    if (payload_len < sizeof(*caps))
        return SR_ERR_DATA;

    payload = response + PICOMSO_PACKET_HEADER_SIZE;
    memcpy(caps, payload, sizeof(*caps));

    if (caps->version != 1u)
        return SR_ERR_DATA;

    if (caps->size != sizeof(*caps))
        return SR_ERR_DATA;

    return SR_OK;
}

static int command_get_status(const struct sr_dev_inst *sdi,
    struct picomso_status *status)
{
    uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    size_t response_len;
    const uint8_t *payload;
    int ret;

    if (!status)
        return SR_ERR_ARG;

    ret = send_request(sdi, PICOMSO_MSG_GET_STATUS, NULL, 0u,
        PICOMSO_RESPONSE_TYPE_STATUS, response, &response_len);
    if (ret != SR_OK)
        return ret;

    if (read_u16_le(response + 6) < 2u)
        return SR_ERR;

    payload = response + PICOMSO_PACKET_HEADER_SIZE;
    status->streams = payload[0];
    status->capture_state = payload[1];

    if (!stream_mask_is_valid(status->streams))
        return SR_ERR;

    return SR_OK;
}

static int command_set_mode(const struct sr_dev_inst *sdi,
    uint8_t streams)
{
    uint8_t payload[1];
    uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    size_t response_len;
    int ret;

    if (!stream_mask_is_valid(streams))
        return SR_ERR_ARG;

    payload[0] = streams;

    ret = send_request(sdi, PICOMSO_MSG_SET_MODE, payload, sizeof(payload),
        PICOMSO_RESPONSE_TYPE_SET_MODE, response, &response_len);
    if (ret != SR_OK)
        return ret;

    return parse_ack_status(response, response_len);
}

static int command_request_capture(const struct sr_dev_inst *sdi,
    const struct picomso_request_capture *request)
{
    uint8_t payload[12u + PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT * 3u + 1u];
    uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    size_t response_len;
    size_t offset;
    unsigned int i;
    int ret;

    if (!request)
        return SR_ERR_ARG;

    write_u32_le(payload, request->total_samples);
    write_u32_le(payload + 4, request->rate);
    write_u32_le(payload + 8, request->pre_trigger_samples);

    for (i = 0; i < PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT; i++) {
        offset = 12u + (size_t)i * 3u;
        payload[offset] = request->trigger[i].is_enabled;
        payload[offset + 1u] = request->trigger[i].pin;
        payload[offset + 2u] = request->trigger[i].match;
    }

    /* Byte 24: analog channel selection mask (A0=bit0, A1=bit1, ...). */
    payload[12u + PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT * 3u] =
        request->analog_channel_mask;

    ret = send_request(sdi, PICOMSO_MSG_REQUEST_CAPTURE, payload,
        (uint16_t)sizeof(payload), PICOMSO_RESPONSE_TYPE_REQUEST,
        response, &response_len);
    if (ret != SR_OK)
        return ret;

    return parse_ack_status(response, response_len);
}

static int command_read_data_block(const struct sr_dev_inst *sdi,
    struct picomso_data_block *block)
{
    struct dev_context *devc;
    uint8_t response[PICOMSO_PROTOCOL_IO_BUFFER_SIZE];
    size_t response_len;
    uint16_t payload_len;
    const uint8_t *payload;
    int ret;

    devc = sdi->priv;

    if (!block)
        return SR_ERR_ARG;

    memset(block, 0, sizeof(*block));

    ret = send_request(sdi, PICOMSO_MSG_READ_DATA_BLOCK, NULL, 0u,
        PICOMSO_RESPONSE_TYPE_DATA_BLOCK, response, &response_len);
    if (ret != SR_OK
        && devc->last_device_status == (uint8_t)PICOMSO_STATUS_ERR_UNKNOWN
        && !strcmp(devc->last_error_text, "no finalized capture data"))
        return PICOMSO_READ_DONE;
    if (ret != SR_OK)
        return ret;

    payload_len = read_u16_le(response + 6);
    if (payload_len < 6u)
        return SR_ERR;

    payload = response + PICOMSO_PACKET_HEADER_SIZE;

    block->stream_id = payload[0];
    block->flags = payload[1];
    block->block_id = read_u16_le(payload + 2);
    block->data_len = read_u16_le(payload + 4);

    if (!stream_id_is_valid(block->stream_id))
        return SR_ERR;
    if (block->data_len > PICOMSO_DATA_BLOCK_SIZE)
        return SR_ERR;
    if ((uint16_t)(6u + block->data_len) > payload_len)
        return SR_ERR;

    if (block->data_len > 0u)
        memcpy(block->data, payload + 6, block->data_len);

    return SR_OK;
}

static int trigger_match_to_picomso(enum sr_trigger_matches match,
    uint8_t *picomso_match)
{
    switch (match) {
    case SR_TRIGGER_ZERO:
        *picomso_match = PICOMSO_TRIGGER_MATCH_LEVEL_LOW;
        break;
    case SR_TRIGGER_ONE:
        *picomso_match = PICOMSO_TRIGGER_MATCH_LEVEL_HIGH;
        break;
    case SR_TRIGGER_RISING:
        *picomso_match = PICOMSO_TRIGGER_MATCH_EDGE_HIGH;
        break;
    case SR_TRIGGER_FALLING:
        *picomso_match = PICOMSO_TRIGGER_MATCH_EDGE_LOW;
        break;
    default:
        return SR_ERR_NA;
    }

    return SR_OK;
}

static gint compare_channel_index(gconstpointer a, gconstpointer b)
{
    const struct sr_channel *cha = a;
    const struct sr_channel *chb = b;

    if (cha->index < chb->index)
        return -1;
    if (cha->index > chb->index)
        return 1;
    return 0;
}

static int configure_capture_streams(const struct sr_dev_inst *sdi,
    uint8_t *streams)
{
    struct dev_context *devc;
    const GSList *l;
    struct sr_channel *ch;
    unsigned int enabled_logic;
    unsigned int enabled_analog;

    if (!streams)
        return SR_ERR_ARG;

    devc = sdi->priv;

    g_slist_free(devc->enabled_analog_channels);
    devc->enabled_analog_channels = NULL;

    enabled_logic = 0;
    enabled_analog = 0;
    *streams = PICOMSO_STREAM_NONE;

    for (l = sdi->channels; l; l = l->next) {
        ch = l->data;

        if (!ch->enabled)
            continue;

        if (ch->type == SR_CHANNEL_ANALOG) {
            enabled_analog++;
            devc->enabled_analog_channels =
                g_slist_append(devc->enabled_analog_channels, ch);
        } else if (ch->type == SR_CHANNEL_LOGIC) {
            enabled_logic++;
        }
    }

    devc->enabled_analog_channels =
        g_slist_sort(devc->enabled_analog_channels, compare_channel_index);

    if (enabled_logic > 0)
        *streams |= PICOMSO_STREAM_LOGIC;
    if (enabled_analog > 0)
        *streams |= PICOMSO_STREAM_SCOPE;

    if (*streams == PICOMSO_STREAM_NONE) {
        sr_err("No enabled PicoMSO channels found for acquisition.");
        return SR_ERR;
    }

    return SR_OK;
}

static int build_capture_request(const struct sr_dev_inst *sdi,
    struct picomso_request_capture *request)
{
    struct dev_context *devc;
    struct sr_trigger *trigger;
    struct sr_trigger_stage *stage;
    struct sr_trigger_match *match;
    struct sr_channel *ch;
    const GSList *l;
    uint64_t total_samples;
    unsigned int trigger_index;
    int ret;
    uint64_t max_samples;

    devc = sdi->priv;
    memset(request, 0, sizeof(*request));

    total_samples = devc->limit_samples ?
        devc->limit_samples : PICOMSO_DEFAULT_LIMIT_SAMPLES;

    if (devc->cur_samplerate == 0)
        return SR_ERR_ARG;
    if (devc->capture_ratio > 100)
        return SR_ERR_ARG;
    max_samples = devc->fw_caps.max_samples_logic;
    if (total_samples > max_samples)
        return SR_ERR_ARG;

    request->total_samples = (uint32_t)total_samples;
    request->rate = (uint32_t)devc->cur_samplerate;
    request->pre_trigger_samples = request->total_samples * devc->capture_ratio / 100u;

    /*
     * Build the analog channel mask from the enabled analog channel list.
     * Bit N = analog channel N (A0=bit0, A1=bit1, ...).  Each channel's
     * hardware analog index is its global index minus NUM_CHANNELS (the
     * number of logic channels that precede the analog channels).
     */
    request->analog_channel_mask = 0u;
    for (l = devc->enabled_analog_channels; l; l = l->next) {
        ch = l->data;
        if (ch->index >= NUM_CHANNELS)
            request->analog_channel_mask |=
                (uint8_t)(1u << (ch->index - NUM_CHANNELS));
    }

    trigger = sr_session_trigger_get(sdi->session);
    if (!trigger)
        return SR_OK;
    if (g_slist_length(trigger->stages) > 1)
        return SR_ERR_NA;

    stage = g_slist_nth_data(trigger->stages, 0);
    if (!stage)
        return SR_ERR_ARG;

    trigger_index = 0;
    for (l = stage->matches; l; l = l->next) {
        match = l->data;

        if (!match->match || !match->channel || !match->channel->enabled)
            continue;
        if (match->channel->type != SR_CHANNEL_LOGIC)
            continue;
        if (match->channel->index >= NUM_CHANNELS)
            return SR_ERR_ARG;
        if (trigger_index >= PICOMSO_REQUEST_CAPTURE_TRIGGER_COUNT)
            return SR_ERR_NA;

        ret = trigger_match_to_picomso(match->match,
            &request->trigger[trigger_index].match);
        if (ret != SR_OK)
            return ret;

        request->trigger[trigger_index].is_enabled = 1u;
        request->trigger[trigger_index].pin = match->channel->index;
        trigger_index++;
    }

    return SR_OK;
}

static int send_logic_data(struct sr_dev_inst *sdi,
    const struct picomso_data_block *block)
{
    struct dev_context *devc;
    struct sr_datafeed_logic logic;
    struct sr_datafeed_packet packet;
    size_t sample_count;

    devc = sdi->priv;

    if ((block->data_len % sizeof(uint16_t)) != 0u)
        return SR_ERR;

    sample_count = block->data_len / sizeof(uint16_t);

    if (devc->limit_samples &&
        devc->sent_logic_samples + sample_count > devc->limit_samples) {
        sample_count = (size_t)(devc->limit_samples - devc->sent_logic_samples);
    }

    if (sample_count == 0u)
        return SR_OK;

    logic.length = sample_count * sizeof(uint16_t);
    logic.unitsize = sizeof(uint16_t);
    logic.data = (uint8_t *)block->data;

    packet.type = SR_DF_LOGIC;
    packet.payload = &logic;

    devc->sent_logic_samples += sample_count;

    return sr_session_send(sdi, &packet);
}

static int send_scope_analog_data(struct sr_dev_inst *sdi,
    const struct picomso_data_block *block)
{
    struct dev_context *devc;
    struct sr_datafeed_packet packet;
    struct sr_datafeed_analog analog;
    struct sr_analog_encoding encoding;
    struct sr_analog_meaning meaning;
    struct sr_analog_spec spec;
    float samples[PICOMSO_DATA_BLOCK_SIZE / 2];
    size_t sample_count, num_channels, per_channel, ch_idx, raw_idx, i;
    uint16_t raw;
    const GSList *l;
    GSList ch_node = {0};
    int ret;

    devc = sdi->priv;

    /*
     * Scope payload format:
     *
     * - The firmware always sends scope samples as little-endian 16-bit words.
     * - Single-channel analog mode uses native 12-bit ADC samples in bits 11:0.
     * - Dual-channel analog mode may internally capture 8-bit samples, but the
     *   firmware expands them to a 12-bit-equivalent range before transmission.
     *
     * Therefore the host always consumes 16-bit little-endian samples and
     * scales them as 12-bit values after masking with 0x0FFF.
     */
    if ((block->data_len % 2u) != 0u)
        return SR_ERR;

    sample_count = block->data_len / 2u;

    /*
     * N = number of enabled analog channels; incoming samples are interleaved:
     * channel k receives raw samples at indices k, k+N, k+2N, ...
     * Truncate to a whole number of N-sample frames.
     */
    num_channels = (size_t)g_slist_length(devc->enabled_analog_channels);
    if (num_channels == 0u)
        return SR_OK;

    sample_count -= sample_count % num_channels;

    /*
     * limit_samples is a per-channel budget. The total number of interleaved
     * samples the driver should forward is therefore limit_samples * num_channels.
     */
    if (devc->limit_samples &&
        devc->sent_scope_samples + sample_count >
            devc->limit_samples * (uint64_t)num_channels) {
        sample_count = (size_t)(
            devc->limit_samples * (uint64_t)num_channels
            - devc->sent_scope_samples);
        sample_count -= sample_count % num_channels;
    }

    if (sample_count == 0u)
        return SR_OK;

    per_channel = sample_count / num_channels;

    /* Emit one analog packet per enabled channel with demultiplexed samples. */
    for (ch_idx = 0, l = devc->enabled_analog_channels; l;
            l = l->next, ch_idx++) {
        for (i = 0; i < per_channel; i++) {
            raw_idx = i * num_channels + ch_idx;
            raw = (uint16_t)block->data[2u * raw_idx]
                | ((uint16_t)block->data[2u * raw_idx + 1u] << 8);

            /* Scope samples are always transported as 12-bit-scaled values. */
            raw &= 0x0FFFu;
            samples[i] = (3.3f * (float)raw) / 4095.0f;
        }

        ch_node.data = l->data;
        ch_node.next = NULL;

        sr_analog_init(&analog, &encoding, &meaning, &spec, 2);
        analog.meaning->channels = &ch_node;
        analog.meaning->mq = SR_MQ_VOLTAGE;
        analog.meaning->unit = SR_UNIT_VOLT;
        analog.meaning->mqflags = 0;
        analog.num_samples = per_channel;
        analog.data = samples;

        packet.type = SR_DF_ANALOG;
        packet.payload = &analog;

        ret = sr_session_send(sdi, &packet);
        if (ret != SR_OK)
            return ret;
    }

    devc->sent_scope_samples += sample_count;

    return SR_OK;
}

static void finish_acquisition(struct sr_dev_inst *sdi)
{
    struct dev_context *devc;

    devc = sdi->priv;
    if (devc->acq_state == PICOMSO_ACQ_IDLE)
        return;

    if (command_set_mode(sdi, PICOMSO_STREAM_NONE) != SR_OK)
        sr_dbg("Unable to switch device back to stream mask 0 after acquisition.");

    sr_session_source_remove(sdi->session, -1);
    std_session_send_df_end(sdi);

    devc->acq_aborted = FALSE;
    devc->acq_state = PICOMSO_ACQ_IDLE;
    devc->enabled_streams = PICOMSO_STREAM_NONE;
    devc->expected_logic_block_id = 0;
    devc->expected_scope_block_id = 0;
    devc->capture_deadline_us = 0;
    devc->sent_logic_samples = 0;
    devc->sent_scope_samples = 0;

    g_slist_free(devc->enabled_analog_channels);
    devc->enabled_analog_channels = NULL;
}

static int receive_data(int fd, int revents, void *cb_data)
{
    struct sr_dev_inst *sdi;
    struct dev_context *devc;
    struct picomso_status status;
    struct picomso_data_block block;
    int ret;

    (void)fd;
    (void)revents;

    sdi = cb_data;
    devc = sdi->priv;

    if (devc->acq_aborted) {
        finish_acquisition(sdi);
        return FALSE;
    }

    if (devc->acq_state == PICOMSO_ACQ_WAITING) {
        ret = command_get_status(sdi, &status);
        if (ret != SR_OK) {
            sr_err("Failed to query PicoMSO capture status.");
            finish_acquisition(sdi);
            return FALSE;
        }

        if (status.streams != devc->enabled_streams) {
            sr_err("Device left expected stream configuration while acquisition was running.");
            finish_acquisition(sdi);
            return FALSE;
        }

        if (status.capture_state == PICOMSO_CAPTURE_RUNNING) {
            if (devc->capture_deadline_us > 0 &&
                g_get_monotonic_time() > devc->capture_deadline_us) {
                sr_err("Timed out waiting for PicoMSO capture completion.");
                finish_acquisition(sdi);
                return FALSE;
            }
            return TRUE;
        }

        if (status.capture_state != PICOMSO_CAPTURE_IDLE) {
            sr_err("Unexpected PicoMSO capture state 0x%02x.",
                status.capture_state);
            finish_acquisition(sdi);
            return FALSE;
        }

        devc->acq_state = PICOMSO_ACQ_READING;
    }

    while (!devc->acq_aborted) {
        ret = command_read_data_block(sdi, &block);
        if (ret == PICOMSO_READ_DONE) {
            finish_acquisition(sdi);
            return FALSE;
        }
        if (ret != SR_OK) {
            sr_err("Failed to read finalized PicoMSO capture data.");
            finish_acquisition(sdi);
            return FALSE;
        }

        switch (block.stream_id) {
        case PICOMSO_STREAM_ID_LOGIC:
            if (block.block_id != devc->expected_logic_block_id) {
                sr_err("Unexpected PicoMSO logic block id %u, expected %u.",
                    block.block_id, devc->expected_logic_block_id);
                finish_acquisition(sdi);
                return FALSE;
            }

            ret = send_logic_data(sdi, &block);
            if (ret != SR_OK) {
                sr_err("Failed to forward PicoMSO logic capture data.");
                finish_acquisition(sdi);
                return FALSE;
            }

            devc->expected_logic_block_id++;
            break;

        case PICOMSO_STREAM_ID_SCOPE:
            if (block.block_id != devc->expected_scope_block_id) {
                sr_err("Unexpected PicoMSO scope block id %u, expected %u.",
                    block.block_id, devc->expected_scope_block_id);
                finish_acquisition(sdi);
                return FALSE;
            }

            ret = send_scope_analog_data(sdi, &block);
            if (ret != SR_OK) {
                sr_err("Failed to forward PicoMSO scope capture data.");
                finish_acquisition(sdi);
                return FALSE;
            }

            devc->expected_scope_block_id++;
            break;

        default:
            sr_err("Received PicoMSO data block with unknown stream id 0x%02x.",
                block.stream_id);
            finish_acquisition(sdi);
            return FALSE;
        }
    }

    finish_acquisition(sdi);
    return FALSE;
}

SR_PRIV int picomso_dev_open(struct sr_dev_inst *sdi, struct sr_dev_driver *di)
{
    libusb_device **devlist;
    struct sr_usb_dev_inst *usb;
    struct libusb_device_descriptor des;
    struct dev_context *devc;
    struct drv_context *drvc;
    int ret, i, device_count;
    char connection_id[64];

    drvc = di->context;
    devc = sdi->priv;
    usb = sdi->conn;

    device_count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
    if (device_count < 0) {
        sr_err("Failed to get device list: %s.",
            libusb_error_name(device_count));
        return SR_ERR;
    }

    ret = SR_ERR;
    for (i = 0; i < device_count; i++) {
        libusb_get_device_descriptor(devlist[i], &des);

        if (des.idVendor != devc->profile->vid ||
            des.idProduct != devc->profile->pid)
            continue;

        if (usb_get_port_path(devlist[i], connection_id,
            sizeof(connection_id)) < 0)
            continue;

        if (strcmp(sdi->connection_id, connection_id))
            continue;

        ret = libusb_open(devlist[i], &usb->devhdl);
        if (ret < 0) {
            sr_err("Failed to open device: %s.", libusb_error_name(ret));
            ret = SR_ERR;
            break;
        }

        if (libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER) &&
            libusb_kernel_driver_active(usb->devhdl, USB_INTERFACE) == 1) {
            ret = libusb_detach_kernel_driver(usb->devhdl, USB_INTERFACE);
            if (ret < 0) {
                sr_err("Failed to detach kernel driver: %s.",
                    libusb_error_name(ret));
                libusb_close(usb->devhdl);
                usb->devhdl = NULL;
                ret = SR_ERR;
                break;
            }
        }

        ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE);
        if (ret != 0) {
            sr_err("Unable to claim interface: %s.",
                libusb_error_name(ret));
            libusb_close(usb->devhdl);
            usb->devhdl = NULL;
            ret = SR_ERR;
            break;
        }

        ret = command_get_info(sdi, &devc->info);
        if (ret != SR_OK) {
            sr_err("Failed to query PicoMSO device information.");
            libusb_release_interface(usb->devhdl, USB_INTERFACE);
            libusb_close(usb->devhdl);
            usb->devhdl = NULL;
            break;
        }

        ret = command_get_capabilities(sdi, &devc->fw_caps);
        if (ret != SR_OK) {
            sr_err("Failed to query PicoMSO device capabilities.");
            libusb_release_interface(usb->devhdl, USB_INTERFACE);
            libusb_close(usb->devhdl);
            usb->devhdl = NULL;
            break;
        }

        sr_info("Opened PicoMSO device on %d.%d / %s, firmware %u.%u (%s).",
            usb->bus, usb->address, connection_id,
            devc->info.protocol_version_major,
            devc->info.protocol_version_minor,
            devc->info.fw_id);
        ret = SR_OK;
        break;
    }

    libusb_free_device_list(devlist, 1);

    return ret;
}

SR_PRIV struct dev_context *picomso_dev_new(void)
{
    struct dev_context *devc;

    devc = g_malloc0(sizeof(struct dev_context));
    devc->profile = NULL;
    devc->channel_names = NULL;
    devc->cur_samplerate = 0;
    devc->limit_samples = PICOMSO_DEFAULT_LIMIT_SAMPLES;
    devc->capture_ratio = 0;
    devc->sent_logic_samples = 0;
    devc->sent_scope_samples = 0;
    devc->next_seq = 1u;
    devc->last_device_status = PICOMSO_STATUS_OK;
    devc->acq_state = PICOMSO_ACQ_IDLE;
    devc->enabled_streams = PICOMSO_STREAM_NONE;
    devc->expected_logic_block_id = 0;
    devc->expected_scope_block_id = 0;
    devc->capture_deadline_us = 0;
    devc->enabled_analog_channels = NULL;
    clear_error_state(devc);

    return devc;
}

SR_PRIV void picomso_abort_acquisition(struct dev_context *devc)
{
    devc->acq_aborted = TRUE;
}

SR_PRIV int picomso_start_acquisition(const struct sr_dev_inst *sdi)
{
    struct dev_context *devc;
    struct picomso_request_capture request;
    struct picomso_status status;
    uint8_t streams;
    gint64 capture_time_us;
    int ret;

    devc = sdi->priv;

    if (devc->acq_state != PICOMSO_ACQ_IDLE)
        return SR_ERR;

    ret = configure_capture_streams(sdi, &streams);
    if (ret != SR_OK)
        return ret;

    guint analog_channels = g_slist_length(devc->enabled_analog_channels);
    if ((streams & PICOMSO_STREAM_SCOPE) 
        && ((devc->cur_samplerate > SR_MHZ(2) && analog_channels == 1)
        || (devc->cur_samplerate > SR_MHZ(1) && analog_channels == 2))) {
        sr_err("Analog stream requested with samplerate above 2 MHz.");
        return SR_ERR_ARG;
    }

    ret = build_capture_request(sdi, &request);
    if (ret != SR_OK)
        return ret;

    ret = command_set_mode(sdi, streams);
    if (ret != SR_OK)
        return ret;

    ret = command_get_status(sdi, &status);
    if (ret != SR_OK)
        return ret;

    if (status.streams != streams || status.capture_state == PICOMSO_CAPTURE_RUNNING)
        return SR_ERR;

    ret = command_request_capture(sdi, &request);
    if (ret != SR_OK)
        return ret;

    devc->acq_aborted = FALSE;
    devc->acq_state = PICOMSO_ACQ_WAITING;
    devc->enabled_streams = streams;
    devc->expected_logic_block_id = 0;
    devc->expected_scope_block_id = 0;
    devc->sent_logic_samples = 0;
    devc->sent_scope_samples = 0;

    capture_time_us = ((gint64)request.total_samples * G_USEC_PER_SEC)
        / request.rate;
    devc->capture_deadline_us = g_get_monotonic_time()
        + capture_time_us + (600 * G_USEC_PER_SEC);

    ret = sr_session_source_add(sdi->session, -1, 0,
        PICOMSO_POLL_INTERVAL_MS, receive_data, (void *)sdi);
    if (ret != SR_OK) {
        devc->acq_state = PICOMSO_ACQ_IDLE;
        devc->enabled_streams = PICOMSO_STREAM_NONE;
        command_set_mode(sdi, PICOMSO_STREAM_NONE);
        return ret;
    }

    ret = std_session_send_df_header(sdi);
    if (ret != SR_OK) {
        sr_session_source_remove(sdi->session, -1);
        devc->acq_state = PICOMSO_ACQ_IDLE;
        devc->enabled_streams = PICOMSO_STREAM_NONE;
        command_set_mode(sdi, PICOMSO_STREAM_NONE);
    }

    return ret;
}