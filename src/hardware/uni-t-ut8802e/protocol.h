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

#ifndef LIBSIGROK_HARDWARE_UNI_T_UT8802E_PROTOCOL_H
#define LIBSIGROK_HARDWARE_UNI_T_UT8802E_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "uni-t-ut8802e"

#define PACKET_SIZE    32

#define FRAME_MAGIC 0xac /* AC */

enum ut8802e_mode_code {
    /* V AC */
    MODE_V_AC_2V = 0x09,
    MODE_V_AC_20V = 0x0A,
    MODE_V_AC_200V = 0x0B,
    MODE_V_AC_750V = 0x0C,
    /* V DC */
    MODE_V_DC_200mV = 0x01,
    MODE_V_DC_2V = 0x03,
    MODE_V_DC_20V = 0x04,
    MODE_V_DC_200V = 0x05,
    MODE_V_DC_1000V = 0x06,
    /* A AC */
    MODE_A_AC_2mA = 0x10,
    MODE_A_AC_20mA = 0x13,
    MODE_A_AC_200mA = 0x14,
    MODE_A_AC_20A = 0x18,
    /* A DC */
    MODE_A_DC_200uA = 0x0D,
    MODE_A_DC_2mA = 0x0E,
    MODE_A_DC_20mA = 0x11,
    MODE_A_DC_200mA = 0x12,
    MODE_A_DC_20A = 0x16,
    /* Resistance */
    MODE_RES_200 = 0x19,
    MODE_RES_2k = 0x1A,
    MODE_RES_20k = 0x1B,
    MODE_RES_200k = 0x1C,
    MODE_RES_2M = 0x1D,
    MODE_RES_200M = 0x1F,
    /* Resistance */
    MODE_CIRCUIT_CONTINUITY = 0x24,
    /* Diode  */
    MODE_DIODE = 0x23,
    /* Capacitance */
    MODE_CAPACITANCE_nF = 0x27,
    MODE_CAPACITANCE_uF = 0x28,
    MODE_CAPACITANCE_mF = 0x29,
    /* Triode hFe */
    MODE_TRIODE_HFE = 0x25,
    /* THYRISTOR SCR */
    MODE_THYRISTOR_SCR = 0x2A,
    /* frequency, duty cycle, pulse width */
    MODE_FREQ_Hz = 0x2B,
    MODE_FREQ_KHz = 0x2C,
    MODE_FREQ_MHz = 0x2D,
    MODE_DUTY = 0x22,
};

enum ut8802e_rsp_type {
    RSP_TYPE_INFO = 0x00,
    RSP_TYPE_MEASUREMENT = 0x02,
    RSP_TYPE_REC_INFO = 0x04,
};

enum ut8802e_channel_idx {
    ut8802e_CH_MAIN,
};
struct ut8802e_info {
    struct {
        enum ut8802e_rsp_type rsp_type;
    } rsp_head;

    struct {
        uint8_t range;
        uint16_t  mode;
        uint8_t is_type;
        uint8_t mqflag;
    } meas_head;

    struct {
        uint16_t main_unit;
        float main_value;
        int8_t main_prec;
        int8_t comma_position;
    } meas_data;
};

struct feed_buffer {
    struct sr_datafeed_packet packet;
    struct sr_datafeed_analog analog;
    struct sr_analog_encoding encoding;
    struct sr_analog_meaning meaning;
    struct sr_analog_spec spec;
    int scale;
    float main_value;
};

struct dev_context {
    struct sr_sw_limits limits;
    struct ut8802e_info info;
    uint8_t packet[PACKET_SIZE];
    size_t packet_len;
};

SR_PRIV int ut8802e_handle_events(int fd, int revents, void *cb_data);

#endif
