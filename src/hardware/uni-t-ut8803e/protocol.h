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

#ifndef LIBSIGROK_HARDWARE_UNI_T_UT8803E_PROTOCOL_H
#define LIBSIGROK_HARDWARE_UNI_T_UT8803E_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "uni-t-ut8803e"

#define PACKET_SIZE    256
#define SEND_BUFF_SIZE 32
#define SEND_TO_MS 100

#define FRAME_MAGIC 0xcdab /* AB CD */

enum ut8803e_mode_code {
    /* V AC */
    MODE_V_AC = 0,
    /* V DC */
    MODE_V_DC = 1,
    /* A AC */
    MODE_uA_AC = 2,
    MODE_mA_AC = 3,
    MODE_A_AC = 4,
    /* A DC */
    MODE_uA_DC = 5,
    MODE_mA_DC = 6,
    MODE_A_DC = 7,
    /* Resistance */
    MODE_RES = 8,
    /* Resistance */
    MODE_CIRCUIT_CONTINUITY = 9,
    /* Diode  */
    MODE_DIODE = 10,
    /* Inductance_measurement,  */
    MODE_INDUCTANCE_L = 11,
    MODE_INDUCTANCE_Q = 12,
    MODE_INDUCTANCE_R = 13,
    /* Capacitance */
    MODE_CAPACITANCE_C = 14,
    MODE_CAPACITANCE_D = 15,
    MODE_CAPACITANCE_R = 16,
    /* Triode hFe */
    MODE_TRIODE_HFE = 17,
    /* THYRISTOR SCR */
    MODE_THYRISTOR_SCR = 18,
    /* Temperature Celsius */
    MODE_TEMP_C = 19,
    /* Temperature Farenheit */
    MODE_TEMP_F = 20,
    /* Frequency, duty cycle */
    MODE_FREQ = 21,
    MODE_DUTY = 22,
};

enum ut8803e_cmd_code {
    CMD_CODE_HOLD = 0x46,
    CMD_CODE_BACKLIGHT = 0x47,
    CMD_CODE_SELECT = 0x48,
    CMD_CODE_MANUAL_RANGE = 0x49,
    CMD_CODE_AUTO_RANGE = 0x4a,
    CMD_CODE_SET_MIN_MAX = 0x4b,
    CMD_CODE_UNSET_MIN_MAX = 0x4c,
    CMD_CODE_SET_REFERENCE = 0x4e,
    CMD_CODE_D_VALUE = 0x4e,
    CMD_CODE_Q_VALUE = 0x4f,
    CMD_CODE_R_VALUE = 0x51,
    CMD_CODE_DEVICE_ID= 0x58,
};

enum ut8803e_rsp_type {
    RSP_TYPE_INFO = 0x00,
    RSP_TYPE_MEASUREMENT = 0x02,
};

enum ut8803e_channel_idx {
    UT8803E_CH_MAIN,
};
struct ut8803e_info {
    struct {
        enum ut8803e_rsp_type rsp_type;
    } rsp_head;

    struct {
        uint8_t range;
        uint16_t  mode;
        uint8_t mqflag;
    } meas_head;

    struct {
        uint16_t main_unit;
        float main_value;
        int8_t main_prec;
    } meas_data;
};

struct feed_buffer {
    struct sr_datafeed_packet packet;
    struct sr_datafeed_analog analog;
    struct sr_analog_encoding encoding;
    struct sr_analog_meaning meaning;
    struct sr_analog_spec spec;
    float main_value;
};

struct dev_context {
    struct sr_sw_limits limits;
    struct ut8803e_info info;
    uint8_t packet[PACKET_SIZE];
    size_t packet_len;
};

SR_PRIV int ut8803e_handle_events(int fd, int revents, void *cb_data);
SR_PRIV int ut8803e_send_cmd(const struct sr_dev_inst *sdi, uint8_t mode);
#endif
