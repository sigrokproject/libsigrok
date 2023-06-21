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

#include "protocol.h"

#include <config.h>

#include "math.h"

/*
 * Developer notes on the UT8802E protocol:
 * - Serial communication over HID CP2110 converter USB-UART.
 * - UART frame format 8n1 at 9600 bps.
 * - DMM packet starts with a magic marker, functionality, value, etc.
 * - UT8802E only sends a packet with measurement. Example of frame:
 *   Frame | func | value    | comma | settings | checksum |
 *   ac    | 1b   | 45 01 00 | 33     | 04          | 44       |
 * - The frame to send a command to the multimeter is unknown.
 */

static const int8_t range_volt[]  = {-3,  0};
static const int8_t range_amp[]   = {-6, -3,  0};
static const int8_t range_ohm[]   = {0,   3,  6};
static const int8_t range_farad[] = {-9, -6, -3};
static const int8_t range_hz[]    = {0,   3,  6};

static int ut8802e_feedbuff_initialization(struct feed_buffer *buff) {
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

static int process_packet(struct sr_dev_inst *sdi, uint8_t *pkt, size_t len) {
    struct dev_context *devc;
    struct ut8802e_info *info;
    const uint8_t *payload;
    size_t checksum_dlen;

    uint8_t v8, got_magic_frame, got_checksum, want_checksum;
    uint16_t sum_of_bytes;
    uint32_t v32;
    uint8_t got_length, settings;
    float new_value;

    struct feed_buffer feedbuff;

    int ret;

    devc = sdi ? sdi->priv : NULL;
    info = devc ? &devc->info : NULL;

    if (len < 1 * sizeof(uint8_t)) {
        sr_spew("Wrong packet");
        return SR_ERR_DATA;
    }

    got_magic_frame = R8(&pkt[0]);

    if (got_magic_frame != FRAME_MAGIC) {
        sr_spew("Wrong frame packet");
        return SR_ERR_DATA;
    }

    got_length = 7;
    if (got_length != len - sizeof(uint8_t)) {
        sr_spew("Wrong lenght of packet");
        return SR_ERR_DATA;
    }

    payload = &pkt[sizeof(uint8_t)];

    sum_of_bytes = 0;
    checksum_dlen = got_length;
    for (uint32_t i = 0; i < checksum_dlen; i++) {
        sum_of_bytes = sum_of_bytes + pkt[i];
    }
    want_checksum = sum_of_bytes;
    want_checksum &= ~(1UL << 7);

    got_checksum = R8(&pkt[checksum_dlen]);
    sr_spew("Checksum: %d, Got: %d, sum of bytes: %d",
            want_checksum, got_checksum, sum_of_bytes);
    if (want_checksum != got_checksum)
        return SR_ERR_DATA;

    info->meas_head.mqflag = SR_MQFLAG_AC | SR_MQFLAG_DC;
    info->meas_data.main_prec = 0;
    info->meas_data.main_value = 0;

    v8 = R8(&payload[1]);  // 99
    v32 = (v8 & 0x0F) + ((v8 & 0xF0) >> 4) * 10;
    new_value = v32;

    v8 = R8(&payload[2]);  // 99--
    v32 = ((v8 & 0x0F) + ((v8 & 0xF0) >> 4) * 10) * 100;
    new_value = new_value + v32;

    v8 = R8(&payload[3]);  // 2----
    v32 = (v8 & 0x0F) * 10000;
    new_value = new_value + v32;

    v8 = R8(&payload[4]);
    info->meas_data.comma_position = -(v8 - 0x30);
    new_value = new_value * pow(10, info->meas_data.comma_position);

    settings = R8(&payload[5]);
    if ((settings & (1 << 7)))
        new_value = -new_value;

    v8 = R8(&payload[0]);
    switch (v8) {
        case MODE_V_AC_2V:
        case MODE_V_AC_20V:
        case MODE_V_AC_200V:
        case MODE_V_AC_750V:
            info->meas_head.mode = SR_MQ_VOLTAGE;
            info->meas_head.mqflag = SR_MQFLAG_AC;
            info->meas_data.main_unit = SR_UNIT_VOLT;
            info->meas_data.main_prec = range_volt[1];
            break;
        case MODE_V_DC_200mV:
            info->meas_head.mode = SR_MQ_VOLTAGE;
            info->meas_head.mqflag = SR_MQFLAG_DC;
            info->meas_data.main_unit = SR_UNIT_VOLT;
            info->meas_data.main_prec = range_volt[0];
            break;
        case MODE_V_DC_2V:
        case MODE_V_DC_20V:
        case MODE_V_DC_200V:
        case MODE_V_DC_1000V:
            info->meas_head.mode = SR_MQ_VOLTAGE;
            info->meas_head.mqflag = SR_MQFLAG_DC;
            info->meas_data.main_unit = SR_UNIT_VOLT;
            info->meas_data.main_prec = range_volt[1];
            break;

        case MODE_A_AC_2mA:
        case MODE_A_AC_20mA:
        case MODE_A_AC_200mA:
            info->meas_head.mode = SR_MQ_CURRENT;
            info->meas_head.mqflag = SR_MQFLAG_AC;
            info->meas_data.main_unit = SR_UNIT_AMPERE;
            info->meas_data.main_prec = range_amp[0];
            break;
        case MODE_A_AC_20A:
            info->meas_head.mode = SR_MQ_CURRENT;
            info->meas_head.mqflag = SR_MQFLAG_AC;
            info->meas_data.main_unit = SR_UNIT_AMPERE;
            info->meas_data.main_prec = range_amp[1];
            break;

        case MODE_A_DC_2mA:
        case MODE_A_DC_20mA:
        case MODE_A_DC_200mA:
            info->meas_head.mode = SR_MQ_CURRENT;
            info->meas_head.mqflag = SR_MQFLAG_DC;
            info->meas_data.main_unit = SR_UNIT_AMPERE;
            info->meas_data.main_prec = range_amp[0];
            break;
        case MODE_A_DC_20A:
            info->meas_head.mode = SR_MQ_CURRENT;
            info->meas_head.mqflag = SR_MQFLAG_DC;
            info->meas_data.main_unit = SR_UNIT_AMPERE;
            info->meas_data.main_prec = range_amp[1];
            break;

        case MODE_RES_200:
            info->meas_head.mode = SR_MQ_RESISTANCE;
            info->meas_data.main_unit = SR_UNIT_OHM;
            info->meas_data.main_prec = range_ohm[0];
            break;
        case MODE_RES_2k:
        case MODE_RES_20k:
        case MODE_RES_200k:
            info->meas_head.mode = SR_MQ_RESISTANCE;
            info->meas_data.main_unit = SR_UNIT_OHM;
            info->meas_data.main_prec = range_ohm[1];
            break;
        case MODE_RES_2M:
        case MODE_RES_200M:
            info->meas_head.mode = SR_MQ_RESISTANCE;
            info->meas_data.main_unit = SR_UNIT_OHM;
            info->meas_data.main_prec = range_ohm[2];
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

        case MODE_CAPACITANCE_nF:
            info->meas_head.mode = SR_MQ_CAPACITANCE;
            info->meas_data.main_unit = SR_UNIT_FARAD;
            info->meas_data.main_prec = range_farad[0];
            break;
        case MODE_CAPACITANCE_uF:
            info->meas_head.mode = SR_MQ_CAPACITANCE;
            info->meas_data.main_unit = SR_UNIT_FARAD;
            info->meas_data.main_prec = range_farad[1];
            break;
        case MODE_CAPACITANCE_mF:
            info->meas_head.mode = SR_MQ_CAPACITANCE;
            info->meas_data.main_unit = SR_UNIT_FARAD;
            info->meas_data.main_prec = range_farad[2];
            break;

        case MODE_TRIODE_HFE:
            info->meas_head.mode = SR_MQ_GAIN;
            info->meas_data.main_unit = SR_UNIT_UNITLESS;
            break;

        case MODE_THYRISTOR_SCR:
            info->meas_head.mode = SR_MQ_VOLTAGE;
            info->meas_data.main_unit = SR_UNIT_VOLT;
            break;

        case MODE_FREQ_Hz:
            info->meas_head.mode = SR_MQ_FREQUENCY;
            info->meas_data.main_unit = SR_UNIT_HERTZ;
            info->meas_data.main_prec = range_hz[0];
            break;
        case MODE_FREQ_KHz:
            info->meas_head.mode = SR_MQ_FREQUENCY;
            info->meas_data.main_unit = SR_UNIT_HERTZ;
            info->meas_data.main_prec = range_hz[1];
            break;
        case MODE_FREQ_MHz:
            info->meas_head.mode = SR_MQ_FREQUENCY;
            info->meas_data.main_unit = SR_UNIT_HERTZ;
            info->meas_data.main_prec = range_hz[2];
            break;

        case MODE_DUTY:
            info->meas_head.mode = SR_MQ_DUTY_CYCLE;
            info->meas_data.main_unit = SR_UNIT_PERCENTAGE;
            break;

        default:
            sr_spew("Unkwon functionality");
            return SR_ERR_DATA;
    }

    ret = ut8802e_feedbuff_initialization(&feedbuff);
    if (ret != SR_OK)
        return SR_ERR_DATA;

    g_slist_free(feedbuff.analog.meaning->channels);
    feedbuff.analog.meaning->channels = g_slist_append(
        NULL,
        g_slist_nth_data(sdi->channels, ut8802e_CH_MAIN));

    feedbuff.analog.meaning->mqflags = info->meas_head.mqflag;
    feedbuff.analog.meaning->mq = info->meas_head.mode;
    feedbuff.analog.meaning->unit = info->meas_data.main_unit;

    info->meas_data.main_value = new_value * pow(10, info->meas_data.main_prec);

    feedbuff.analog.encoding->digits = -info->meas_data.main_prec + 4;
    feedbuff.analog.spec->spec_digits = -info->meas_data.main_prec + 4;

    feedbuff.main_value = info->meas_data.main_value;
    if (sdi->status != SR_ST_ACTIVE)
        return SR_OK;

    ret = sr_session_send(sdi, &feedbuff.packet);
    if (ret != SR_OK)
        return SR_ERR_DATA;

    sr_sw_limits_update_samples_read(&devc->limits, 1);
    if (sr_sw_limits_check(&devc->limits))
        sr_dev_acquisition_stop(sdi);

    if (feedbuff.analog.meaning)
        g_slist_free(feedbuff.analog.meaning->channels);

    return SR_OK;
}

static int process_buffer(struct sr_dev_inst *sdi) {
    struct dev_context *devc;
    uint8_t *pkt;
    uint8_t v8;
    size_t pkt_len, remain, idx;
    int ret;
    GString *spew;

    devc = sdi->priv;
    pkt = &devc->packet[0];

    do {
        if (devc->packet_len < 1 * sizeof(uint8_t))
            return SR_OK;

        v8 = R8(&pkt[0]);
        if (v8 != FRAME_MAGIC)
            break;

        pkt_len = 8;
        sr_spew("Got a expected packet lenght %zu, have %zu",
                pkt_len, devc->packet_len);

        if (pkt_len > devc->packet_len)
            return SR_OK;

        spew = sr_hexdump_new(pkt, pkt_len);
        sr_spew("Packet to procceses, len %zu, bytes: %s", pkt_len, spew->str);
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

    if (devc->packet_len < 1 * sizeof(uint8_t))
        return SR_OK;

    for (idx = 1; idx < devc->packet_len; idx++) {
        if (devc->packet_len - idx < sizeof(uint8_t)) {
            pkt[0] = pkt[idx];
            devc->packet_len = 1;
            return SR_OK;
        }

        v8 = R8(&pkt[idx]);
        if (v8 != FRAME_MAGIC)
            continue;

        remain = devc->packet_len - idx;
        if (remain)
            memmove(&pkt[0], &pkt[idx], remain);
        devc->packet_len -= idx;
        break;
    }
    return SR_OK;
}

static int ut8802e_receive_data(struct sr_dev_inst *sdi) {
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

SR_PRIV int ut8802e_handle_events(int fd, int revents, void *cb_data) {
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
        (void)ut8802e_receive_data(sdi);

    if (sdi->status == SR_ST_STOPPING) {
        serial_source_remove(sdi->session, serial);
        std_session_send_df_end(sdi);
        sdi->status = SR_ST_INACTIVE;
    }
    return TRUE;
}
