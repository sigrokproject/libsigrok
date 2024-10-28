/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 Konrad Tagowski <konrad.tagowski@grinn-global.com>
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
#include "math.h"

#include "protocol.h"

/*
 * Developer notes on the UT8803E protocol:
 * - Serial communication over HID CP2110 converter USB-UART.
 * - UART frame format 8n1 at 9600 bps.
 * - DMM packet starts with a magic marker, followed by the length, packet indentifity
 * - Example of measurement packet:
 * | HEADER | Lenght | Packet type | Func | RANGE | +/- | Value 6 digit     | *     | Settings    | Checksum |
 * | ab cd  | 12     | 02          | 01   | 31    | 2b  | 30 2e 30 30 30 30 | 30 31 | 30 3c 30 30 | 04 34    |
 * TO DO:
 * - add handler to packet with ID,
 * - add way to change manually range
 * - more...
*/

static const int8_t range_volt[]  = {-3,  0,  0,  0,  0};
static const int8_t range_amp[]   = {-6, -3, -3, -3,  0};
static const int8_t range_ohm[]   = {0,   3,  3,  3,  6,  6};
static const int8_t range_F[]     = {-9, -9, -9, -6, -6, -6, -6};
static const int8_t range_hz[]    = {0,   3,  3,  3,  6,  6};
static const int8_t range_henry[] = {-6, -3, -3, -3,  0,  0,  0};

static int ut8803e_feedbuff_initialization(struct feed_buffer *buff) {
    memset(buff, 0, sizeof(*buff));

    memset(&buff->packet, 0, sizeof(buff->packet));
    sr_analog_init(&buff->analog, &buff->encoding,
                   &buff->meaning, &buff->spec, 0);
    buff->analog.meaning->mq = 0;
    buff->analog.meaning->mqflags = 0;
    buff->analog.meaning->unit = 0;
    buff->analog.meaning->channels = 0;
    buff->analog.encoding->unitsize = sizeof(buff->main_value);
    buff->analog.encoding->digits = 4;
    buff->analog.spec->spec_digits = 4;
    buff->analog.num_samples = 1;
    buff->analog.data = &buff->main_value;
    buff->packet.type = SR_DF_ANALOG;
    buff->packet.payload = &buff->analog;
    buff->analog.encoding->is_float = TRUE;

    return SR_OK;
}

static int get_measurment_type_from_packet(uint8_t type, struct ut8803e_info *info) {
    switch (type) {
        case MODE_V_AC:
            info->meas_head.mode = SR_MQ_VOLTAGE;
            info->meas_head.mqflag = SR_MQFLAG_AC;
            info->meas_data.main_unit = SR_UNIT_VOLT;
            info->meas_data.main_prec = range_volt[info->meas_head.range];
            break;
        case MODE_V_DC:
            info->meas_head.mode = SR_MQ_VOLTAGE;
            info->meas_head.mqflag = SR_MQFLAG_DC;
            info->meas_data.main_unit = SR_UNIT_VOLT;
            info->meas_data.main_prec = range_volt[info->meas_head.range];
            break;

        case MODE_uA_AC:
        case MODE_mA_AC:
        case MODE_A_AC:
            info->meas_head.mode = SR_MQ_CURRENT;
            info->meas_head.mqflag = SR_MQFLAG_AC;
            info->meas_data.main_unit = SR_UNIT_AMPERE;
            info->meas_data.main_prec = range_amp[info->meas_head.range];
            break;

        case MODE_uA_DC:
        case MODE_mA_DC:
        case MODE_A_DC:
            info->meas_head.mode = SR_MQ_CURRENT;
            info->meas_head.mqflag = SR_MQFLAG_DC;
            info->meas_data.main_unit = SR_UNIT_AMPERE;
            info->meas_data.main_prec = range_amp[info->meas_head.range];
            break;

        case MODE_RES:
            info->meas_head.mode = SR_MQ_RESISTANCE;
            info->meas_data.main_unit = SR_UNIT_OHM;
            info->meas_data.main_prec = range_ohm[info->meas_head.range];
            break;

        case MODE_CIRCUIT_CONTINUITY:
            info->meas_head.mode = SR_MQ_CONTINUITY;
            info->meas_head.mqflag = SR_MQFLAG_AUTORANGE;
            info->meas_data.main_unit = SR_UNIT_OHM;
            break;

        case MODE_DIODE:
            info->meas_head.mode = SR_MQ_VOLTAGE;
            info->meas_head.mqflag = SR_MQFLAG_DIODE | SR_MQFLAG_DC;
            info->meas_data.main_unit = SR_MQ_VOLTAGE;
            break;

        case MODE_INDUCTANCE_L:
            info->meas_head.mode = SR_MQ_SERIES_INDUCTANCE;
            info->meas_data.main_unit = SR_UNIT_HENRY;
            info->meas_data.main_prec = range_henry[info->meas_head.range];
            break;

        case MODE_INDUCTANCE_Q:
            info->meas_head.mode = SR_MQ_QUALITY_FACTOR;
            info->meas_data.main_unit = SR_UNIT_UNITLESS;
            break;

        case MODE_INDUCTANCE_R:
            info->meas_head.mode = SR_MQ_RESISTANCE;
            info->meas_data.main_unit = SR_UNIT_OHM;
            break;

        case MODE_CAPACITANCE_C:
            info->meas_head.mode = SR_MQ_CAPACITANCE;
            info->meas_data.main_unit = SR_UNIT_FARAD;
            info->meas_data.main_prec = range_F[info->meas_head.range];
            break;

        case MODE_CAPACITANCE_D:
            info->meas_head.mode = SR_MQ_DISSIPATION_FACTOR;
            info->meas_data.main_unit = SR_UNIT_UNITLESS;
            break;

        case MODE_CAPACITANCE_R:
            info->meas_head.mode = SR_MQ_RESISTANCE;
            info->meas_data.main_unit = SR_UNIT_OHM;
            break;

        case MODE_TRIODE_HFE:
            info->meas_head.mode = SR_MQ_GAIN;
            info->meas_data.main_unit = SR_UNIT_UNITLESS;
            break;

        case MODE_THYRISTOR_SCR:
            info->meas_head.mode = SR_MQ_VOLTAGE;
            info->meas_data.main_unit = SR_UNIT_VOLT;
            break;

        case MODE_TEMP_C:
            info->meas_head.mode = SR_MQ_TEMPERATURE;
            info->meas_data.main_unit = SR_UNIT_CELSIUS;
            break;

        case MODE_TEMP_F:
            info->meas_head.mode = SR_MQ_TEMPERATURE;
            info->meas_data.main_unit = SR_UNIT_FAHRENHEIT;
            break;

        case MODE_FREQ:
            info->meas_head.mode = SR_MQ_FREQUENCY;
            info->meas_data.main_unit = SR_UNIT_HERTZ;
            info->meas_data.main_prec = range_hz[info->meas_head.range];
            break;

        case MODE_DUTY:
            info->meas_head.mode = SR_MQ_DUTY_CYCLE;
            info->meas_data.main_unit = SR_UNIT_PERCENTAGE;
            break;
        default:
            sr_spew("Unkwon functionality");
            return SR_ERR_DATA;
    }
    return SR_OK;
}

static int process_packet(struct sr_dev_inst *sdi, uint8_t *pkt, size_t len) {
    struct dev_context *devc;
    struct ut8803e_info *info;
    const uint8_t *payload;
    size_t checksum_dlen;

    uint8_t v8;
    uint16_t got_magic_frame, got_checksum, want_checksum;
    uint8_t got_length, response_type;
    float new_value;

    struct feed_buffer feedbuff;

    int ret;

    devc = sdi ? sdi->priv : NULL;
    info = devc ? &devc->info : NULL;

    if (len < 3 * sizeof(uint8_t)) {
        sr_spew("Wrong packet");
        return SR_ERR_DATA;
    }

    got_magic_frame = RL16(&pkt[0]);

    if (got_magic_frame != FRAME_MAGIC) {
        sr_spew("Wrong frame packet");
        return SR_ERR_DATA;
    }

    got_length = R8(&pkt[sizeof(uint16_t)]);
    if (got_length != len - 1 * sizeof(uint16_t) - sizeof(uint8_t)) {
        sr_spew("Wrong lenght of packet");
        return SR_ERR_DATA;
    }

    payload = &pkt[sizeof(uint16_t) + sizeof(uint8_t)];

    want_checksum = 0;
    checksum_dlen = len - sizeof(uint16_t);
    for (unsigned i = 0; i < checksum_dlen; i++) {
        want_checksum = want_checksum + pkt[i];
    }
    got_checksum = RB16(&pkt[checksum_dlen]);
    sr_spew("Checksum: %d, Got: %d", want_checksum, got_checksum);
    if (want_checksum != got_checksum)
        return SR_ERR_DATA;

    v8 = R8(&payload[0]);
    response_type = v8;
    switch (response_type) {
        case RSP_TYPE_MEASUREMENT:
            info->meas_head.mqflag = 0;
            info->meas_data.main_prec = 0;

            v8 = R8(&payload[2 * sizeof(uint8_t)]);
            info->meas_head.range = v8 - 0x30;
            v8 = R8(&payload[sizeof(uint8_t)]);

            ret = get_measurment_type_from_packet(v8, info);
            if (ret != SR_OK)
                return SR_ERR_DATA;

            char subbuff[8];
            memcpy(subbuff, &payload[3 * sizeof(uint8_t)], 6);
            new_value = atof(subbuff);

            sr_spew("Received value: %f, from bytes: %s",
                    new_value, subbuff);

            ret = ut8803e_feedbuff_initialization(&feedbuff);
            if (ret != SR_OK)
                return SR_ERR_DATA;

            g_slist_free(feedbuff.analog.meaning->channels);
            feedbuff.analog.meaning->channels = g_slist_append(
                NULL,
                g_slist_nth_data(sdi->channels, UT8803E_CH_MAIN));

            feedbuff.analog.meaning->mqflags = info->meas_head.mqflag;
            feedbuff.analog.meaning->mq = info->meas_head.mode;
            feedbuff.analog.meaning->unit = info->meas_data.main_unit;

            info->meas_data.main_value = new_value *
                                         pow(10, info->meas_data.main_prec);

            feedbuff.analog.encoding->digits = -info->meas_data.main_prec + 3;
            feedbuff.analog.spec->spec_digits = -info->meas_data.main_prec + 3;

            feedbuff.main_value = info->meas_data.main_value;

            if (sdi->status != SR_ST_ACTIVE)
                return SR_OK;

            ret = sr_session_send(sdi, &feedbuff.packet);
            if (ret != SR_OK)
                return SR_ERR_DATA;

            sr_sw_limits_update_samples_read(&devc->limits, 1);
            if (sr_sw_limits_check(&devc->limits)) {
                sr_dev_acquisition_stop(sdi);
            }
            break;
        /* To Do: Add more packet type */
        default:
            sr_spew("Unkwon packet type");
            return SR_ERR_DATA;
    }

    if (feedbuff.analog.meaning)
        g_slist_free(feedbuff.analog.meaning->channels);

    return SR_OK;
}

static int process_buffer(struct sr_dev_inst *sdi) {
    struct dev_context *devc;
    uint8_t *pkt;
    uint8_t v8;
    uint16_t v16;
    size_t pkt_len, remain, idx;
    int ret;
    GString *spew;

    devc = sdi->priv;
    pkt = &devc->packet[0];

    do {
        if (devc->packet_len < 3 * sizeof(uint8_t))
            return SR_OK;

        v16 = RL16(&pkt[0]);
        if (v16 != FRAME_MAGIC)
            break;

        v8 = R8(&pkt[sizeof(uint16_t)]);
        if (v8 < sizeof(uint8_t))
            break;

        pkt_len = sizeof(uint8_t) + sizeof(uint16_t) + v8;
        sr_spew("Got a expected packet lenght %zu, have %zu",
                pkt_len, devc->packet_len);

        if (pkt_len > devc->packet_len) {
            return SR_OK;
        }

        spew = sr_hexdump_new(pkt, pkt_len);
        sr_spew("Packet, len %zu, bytes: %s", pkt_len, spew->str);
        sr_hexdump_free(spew);

        ret = process_packet(sdi, pkt, pkt_len);
        if (ret == SR_ERR_DATA) {
            /* Verification failed */
            break;
        }

        remain = devc->packet_len - pkt_len;
        if (remain)
            memmove(&pkt[0], &pkt[pkt_len], remain);
        devc->packet_len -= pkt_len;
    } while (1);

    if (devc->packet_len < 3 * sizeof(uint8_t))
        return SR_OK;

    for (idx = 1; idx < devc->packet_len; idx++) {
        if (devc->packet_len - idx < sizeof(uint16_t)) {
            pkt[0] = pkt[idx];
            devc->packet_len = 1;
            return SR_OK;
        }

        v16 = RL16(&pkt[idx]);
        if (v16 != FRAME_MAGIC)
            continue;

        remain = devc->packet_len - idx;
        if (remain)
            memmove(&pkt[0], &pkt[idx], remain);
        devc->packet_len -= idx;
        break;
    }
    return SR_OK;
}

static int ut8803e_receive_data(struct sr_dev_inst *sdi) {
    struct dev_context *devc;
    struct sr_serial_dev_inst *serial;
    size_t len;
    uint8_t *data;

    devc = sdi->priv;
    serial = sdi->conn;

    if (devc->packet_len == sizeof(devc->packet)) {
        (void)process_packet(sdi, &devc->packet[0], devc->packet_len);
        devc->packet_len = 0;
    }

    len = sizeof(devc->packet) - devc->packet_len;
    data = &devc->packet[devc->packet_len];
    len = serial_read_nonblocking(serial,
                                  data, len);
    if (!len)
        return 0;

    devc->packet_len += len;
    process_buffer(sdi);

    return 0;
}

SR_PRIV int ut8803e_handle_events(int fd, int revents, void *cb_data) {
    struct sr_dev_inst *sdi;
    struct sr_serial_dev_inst *serial;

    (void)fd;

    sdi = cb_data;
    if (!sdi)
        return TRUE;
    serial = sdi->conn;
    if (!serial)
        return TRUE;

    if (revents & G_IO_IN)
        (void)ut8803e_receive_data(sdi);

    if (sdi->status == SR_ST_STOPPING) {
        sdi->status = SR_ST_INACTIVE;
        serial_source_remove(sdi->session, serial);
        std_session_send_df_end(sdi);
    }
    return TRUE;
}

/* Construct and transmit command. */
SR_PRIV int ut8803e_send_cmd(const struct sr_dev_inst *sdi, uint8_t mode) {
    size_t dlen;
    GString *spew;

    uint8_t frame_buff[SEND_BUFF_SIZE];
    size_t frame_off;
    uint16_t cs_value;
    int ret;

    dlen = 2 * sizeof(uint16_t);

    frame_off = 0;
    WL16(&frame_buff[frame_off], FRAME_MAGIC);
    frame_off += sizeof(uint16_t);
    W8(&frame_buff[frame_off], dlen);
    frame_off += sizeof(uint8_t);

    W8(&frame_buff[frame_off], mode);
    frame_off += sizeof(uint8_t);
    W8(&frame_buff[frame_off], 0x00);
    frame_off += sizeof(uint8_t);

    cs_value = 0;
    for (unsigned i = 0; i < frame_off; i++) {
        cs_value = cs_value + frame_buff[i];
    }

    WB16(&frame_buff[frame_off], cs_value);
    frame_off += sizeof(uint16_t);

    spew = sr_hexdump_new(frame_buff, frame_off);
    sr_spew("TX frame, %zu bytes: %s", frame_off, spew->str);
    sr_hexdump_free(spew);

    ret = serial_write_blocking(sdi->conn, frame_buff, frame_off, SEND_TO_MS);
    if (ret < 0)
        return SR_ERR_IO;

    return SR_OK;
}
