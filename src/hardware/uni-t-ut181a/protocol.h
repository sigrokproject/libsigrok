/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019-2020 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#ifndef LIBSIGROK_HARDWARE_UNI_T_UT181A_PROTOCOL_H
#define LIBSIGROK_HARDWARE_UNI_T_UT181A_PROTOCOL_H

#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <stdint.h>

#include "libsigrok-internal.h"

#define LOG_PREFIX "uni-t-ut181a"

/*
 * Optional features. Tunables.
 */
#define UT181A_WITH_TIMESTAMP 0
#define UT181A_WITH_SER_ECHO 0

/*
 * The largest frame we expect to receive is chunked record data. Which
 * can span up to 256 items which each occupy 9 bytes, plus some header
 * before the items array. Be generous and prepare to receive several
 * frames in a row, e.g. when synchronizing to the packet stream at the
 * start of a session or after communication failure.
 *
 * The largest frame we expect to transmit is a "start record" command.
 * Which contains 18 bytes of payload (plus 6 bytes of frame envelope).
 */
#define RECV_BUFF_SIZE 4096
#define SEND_BUFF_SIZE 32
#define SEND_TO_MS 100

/*
 * The device can hold several recordings, their number is under the
 * user's control and dynamic at runtime. It's assumed that there is an
 * absolute upper bound of 20 recordings at any time. Names are under
 * user control, too (auto-preset, then editable), and a maximum label
 * length is assumed from the protocol description.
 *
 * Update 2020-03-17
 * Turns out that 20 is *not* the limit on the number of recordings.
 * Nor do I believe that 20K or 10K is the limit. It may be the total
 * of the number of recordings and their sample counts which may not
 * exceed 10K, while saved measurements can be up to 20K? This is just
 * a guess though, the "Operating Manual" does not specify a limit,
 * nor does it discuss a dependency beyond mentioning the 10K/20K
 * figures.
 */
#define MAX_REC_COUNT 20
#define MAX_REC_NAMELEN 12

#define MAX_RANGE_INDEX 8

/* Literals look weird as numbers. LE format makes them readable on the wire. */
#define FRAME_MAGIC 0xcdab /* Becomes the AB CD byte sequence. */
#define REPLY_CODE_OK 0x4b4f /* Becomes the "OK" text. */
#define REPLY_CODE_ERR 0x5245 /* Becomes the "ER" text. */

enum ut181a_channel_idx {
	UT181A_CH_MAIN,
	UT181A_CH_AUX1,
	UT181A_CH_AUX2,
	UT181A_CH_AUX3,
	UT181A_CH_BAR,
#if UT181A_WITH_TIMESTAMP
	UT181A_CH_TIME,
#endif
};

enum ut181_cmd_code {
	CMD_CODE_INVALID = 0x00,
	CMD_CODE_SET_MODE = 0x01,
	CMD_CODE_SET_RANGE = 0x02,
	CMD_CODE_SET_REFERENCE = 0x03,
	CMD_CODE_SET_MIN_MAX = 0x04,
	CMD_CODE_SET_MONITOR = 0x05,
	CMD_CODE_SAVE_MEAS = 0x06,
	CMD_CODE_GET_SAVED_MEAS = 0x07,
	CMD_CODE_GET_SAVED_COUNT = 0x08,
	CMD_CODE_DEL_SAVED_MEAS = 0x09,
	CMD_CODE_START_REC = 0x0a,
	CMD_CODE_STOP_REC = 0x0b,
	CMD_CODE_GET_REC_INFO = 0x0c,
	CMD_CODE_GET_REC_SAMPLES = 0x0d,
	CMD_CODE_GET_RECS_COUNT = 0x0e,
	CMD_CODE_BTN_PRESS = 0x12,
};

enum ut181_rsp_type {
	RSP_TYPE_REPLY_CODE = 0x01,
	RSP_TYPE_MEASUREMENT = 0x02,
	RSP_TYPE_SAVE = 0x03,
	RSP_TYPE_REC_INFO = 0x04,
	RSP_TYPE_REC_DATA = 0x05,
	RSP_TYPE_REPLY_DATA = 0x72, /* 'r' */
};

/*
 * TODO
 * - See if there is a pattern to these number codes.
 *   - [3:0] == 2 relative mode (when available)
 *   - [7:4] == 3 peak mode aka max/min (when available)
 *     (but there is command 4 set max/min on/off too)
 */

enum ut181a_mode_code {
	/* V AC */
	MODE_V_AC = 0x1111,
	MODE_V_AC_REL = 0x1112,
	MODE_V_AC_Hz = 0x1121,
	MODE_V_AC_PEAK = 0x1131,
	MODE_V_AC_LOWPASS = 0x1141,
	MODE_V_AC_LOWPASS_REL = 0x1142,
	MODE_V_AC_dBV = 0x1151,
	MODE_V_AC_dBV_REL = 0x1152,
	MODE_V_AC_dBm = 0x1161,
	MODE_V_AC_dBm_REL = 0x1162,
	/* mV AC */
	MODE_mV_AC = 0x2111,
	MODE_mV_AC_REL = 0x2112,
	MODE_mV_AC_Hz = 0x2121,
	MODE_mV_AC_PEAK = 0x2131,
	MODE_mV_AC_ACDC = 0x2141,
	MODE_mV_AC_ACDC_REL = 0x2142,
	/* V DC */
	MODE_V_DC = 0x3111,
	MODE_V_DC_REL = 0x3112,
	MODE_V_DC_ACDC = 0x3121,
	MODE_V_DC_ACDC_REL = 0x3122,
	MODE_V_DC_PEAK = 0x3131,
	/* mV DC */
	MODE_mV_DC = 0x4111,
	MODE_mV_DC_REL = 0x4112,
	MODE_mV_DC_PEAK = 0x4121,	/* TODO Check number code, is it 0x4131? */
	/* temperature Celsius */
	MODE_TEMP_C_T1_and_T2 = 0x4211,
	MODE_TEMP_C_T1_and_T2_REL = 0x4212,
	MODE_TEMP_C_T2_and_T1 = 0x4221,
	MODE_TEMP_C_T2_and_T1_REL = 0x4222,
	MODE_TEMP_C_T1_minus_T2 = 0x4231,	/* XXX exception, not PEAK */
	MODE_TEMP_C_T2_minus_T1 = 0x4241,
	/* temperature Farenheit */
	MODE_TEMP_F_T1_and_T2 = 0x4311,
	MODE_TEMP_F_T1_and_T2_REL = 0x4312,
	MODE_TEMP_F_T2_and_T1 = 0x4321,
	MODE_TEMP_F_T2_and_T1_REL = 0x4322,
	MODE_TEMP_F_T1_minus_T2 = 0x4331,
	MODE_TEMP_F_T2_minus_T1 = 0x4341,	/* XXX exception, not PEAK */
	/* resistance, continuity, conductivity */
	MODE_RES = 0x5111,
	MODE_RES_REL = 0x5112,
	MODE_CONT_SHORT = 0x5211,
	MODE_CONT_OPEN = 0x5212,
	MODE_COND = 0x5311,
	MODE_COND_REL = 0x5312,
	/* diode, capacitance */
	MODE_DIODE = 0x6111,
	MODE_DIODE_ALARM = 0x6112,	/* XXX exception, not REL */
	MODE_CAP = 0x6211,
	MODE_CAP_REL = 0x6212,
	/* frequency, duty cycle, pulse width */
	MODE_FREQ = 0x7111,
	MODE_FREQ_REL = 0x7112,
	MODE_DUTY = 0x7211,
	MODE_DUTY_REL = 0x7212,
	MODE_PULSEWIDTH = 0x7311,
	MODE_PULSEWIDTH_REL = 0x7312,
	/* uA DC */
	MODE_uA_DC = 0x8111,
	MODE_uA_DC_REL = 0x8112,
	MODE_uA_DC_ACDC = 0x8121,
	MODE_uA_DC_ACDC_REL = 0x8122,
	MODE_uA_DC_PEAK = 0x8131,
	/* uA AC */
	MODE_uA_AC = 0x8211,
	MODE_uA_AC_REL = 0x8212,
	MODE_uA_AC_Hz = 0x8221,
	MODE_uA_AC_PEAK = 0x8231,
	/* mA DC */
	MODE_mA_DC = 0x9111,
	MODE_mA_DC_REL = 0x9112,
	MODE_mA_DC_ACDC = 0x9121,
	MODE_mA_DC_ACDC_REL = 0x9122,
	MODE_mA_DC_ACDC_PEAK = 0x9131,
	/* mA AC */
	MODE_mA_AC = 0x9211,
	MODE_mA_AC_REL = 0x9212,
	MODE_mA_AC_Hz = 0x9221,
	MODE_mA_AC_PEAK = 0x9231,
	/* A DC */
	MODE_A_DC = 0xa111,
	MODE_A_DC_REL = 0xa112,
	MODE_A_DC_ACDC = 0xa121,
	MODE_A_DC_ACDC_REL = 0xa122,
	MODE_A_DC_PEAK = 0xa131,
	/* A AC */
	MODE_A_AC = 0xa211,
	MODE_A_AC_REL = 0xa212,
	MODE_A_AC_Hz = 0xa221,
	MODE_A_AC_PEAK = 0xa231,
};

/* Maximum number of UT181A modes which map to one MQ item. */
#define MODE_COUNT_PER_MQ_MQF	15

struct mqopt_item {
	enum sr_mq mq;
	enum sr_mqflag mqflags;
	enum ut181a_mode_code modes[MODE_COUNT_PER_MQ_MQF];
};

struct mq_scale_params {
	int scale;
	enum sr_mq mq;
	enum sr_mqflag mqflags;
	enum sr_unit unit;
};

struct value_params {
	float value;
	int digits;
	gboolean ol_neg, ol_pos;
};

struct feed_buffer {
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	int scale;
	float main_value; /* TODO double, for epoch timestamps */
};

struct ut181a_info {
	struct {
		enum ut181_rsp_type rsp_type;
	} rsp_head;
	struct {
		uint16_t code;
		gboolean ok;
	} reply_code;
	struct {
		uint32_t stamp;
		time_t epoch;
	} save_time;
	struct {
		uint8_t misc1, misc2, range;
		uint16_t mode;
		uint8_t is_type;
		gboolean is_norm, is_rel, is_minmax, is_peak;
		gboolean has_hold, has_aux1, has_aux2, has_bar;
		gboolean is_rec, is_comp, is_auto_range;
		gboolean has_lead_err, has_high_volt;
	} meas_head;
	union {
		struct {
			float main_value;
			uint8_t main_prec;
			char main_unit[8];
			float aux1_value;
			uint8_t aux1_prec;
			char aux1_unit[8];
			float aux2_value;
			uint8_t aux2_prec;
			char aux2_unit[8];
			float bar_value;
			char bar_unit[8];
		} norm;
		struct {
			enum {
				COMP_MODE_INNER = 0,
				COMP_MODE_OUTER = 1,
				COMP_MODE_BELOW = 2,
				COMP_MODE_ABOVE = 3,
			} mode;
			gboolean fail;
			int digits;
			float limit_high;
			float limit_low;
		} comp;
		struct {
			float rel_value;
			uint8_t rel_prec;
			char rel_unit[8];
			float ref_value;
			uint8_t ref_prec;
			char ref_unit[8];
			float abs_value;
			uint8_t abs_prec;
			char abs_unit[8];
			float bar_value;
			char bar_unit[8];
		} rel;
		struct {
			float curr_value;
			uint8_t curr_prec;
			float max_value;
			uint8_t max_prec;
			uint32_t max_stamp;
			float avg_value;
			uint8_t avg_prec;
			uint32_t avg_stamp;
			float min_value;
			uint8_t min_prec;
			uint32_t min_stamp;
			char all_unit[8];
		} minmax;
		struct {
			float max_value;
			uint8_t max_prec;
			char max_unit[8];
			float min_value;
			uint8_t min_prec;
			char min_unit[8];
		} peak;
	} meas_data;
	struct {
		size_t save_idx;
		size_t save_count;
	} save_info;
	struct {
		size_t rec_count;
		size_t rec_idx;
		gboolean auto_feed;
		gboolean auto_next;
		char name[12];
		char unit[8];
		uint16_t interval;
		uint32_t duration;
		uint32_t samples;
		float max_value, avg_value, min_value;
		uint8_t max_prec, avg_prec, min_prec;
		uint32_t start_stamp;
	} rec_info;
	struct {
		size_t rec_idx;
		size_t samples_total;
		size_t samples_curr;
		uint8_t samples_chunk;
	} rec_data;
	struct {
		enum ut181_cmd_code code;
		uint16_t data;
	} reply_data;
};

enum ut181a_data_source {
	DATA_SOURCE_LIVE,
	DATA_SOURCE_SAVE,
	DATA_SOURCE_REC_FIRST,
	DATA_SOURCE_MAX = DATA_SOURCE_REC_FIRST + MAX_REC_COUNT,
};

struct dev_context {
	struct sr_sw_limits limits;
	enum ut181a_data_source data_source;
	size_t data_source_count;
	const char *data_source_names[DATA_SOURCE_MAX + 1];
	size_t record_count;
	char record_names[MAX_REC_COUNT][MAX_REC_NAMELEN];
	gboolean is_monitoring;
	gboolean is_recording;

	/* Reception of serial communication data. */
	uint8_t recv_buff[RECV_BUFF_SIZE];
	size_t recv_count;

	/* Meter's internal state tracking. */
	int disable_feed;
	gboolean frame_started;
	struct ut181a_info info;

	/* Management for request/response pairs. */
	struct wait_state {
		gboolean want_code, got_code;
		enum ut181_cmd_code want_data; gboolean got_data;
		enum ut181_rsp_type want_rsp_type;
		gboolean got_rsp_type;
		gboolean want_measure, got_measure;
		gboolean got_rec_count;
		gboolean got_save_count;
		gboolean got_sample_count;
		size_t response_count;
		gboolean code_ok;
		size_t data_value;
	} wait_state;
	struct {
		char unit_text[12];
	} last_data;
};

SR_PRIV const struct mqopt_item *ut181a_get_mqitem_from_mode(uint16_t mode);
SR_PRIV uint16_t ut181a_get_mode_from_mq_flags(enum sr_mq mq, enum sr_mqflag mqflags);
SR_PRIV GVariant *ut181a_get_mq_flags_list_item(enum sr_mq mq, enum sr_mqflag mqflag);
SR_PRIV GVariant *ut181a_get_mq_flags_list(void);

SR_PRIV int ut181a_send_cmd_monitor(struct sr_serial_dev_inst *serial, gboolean on);
SR_PRIV int ut181a_send_cmd_setmode(struct sr_serial_dev_inst *serial, uint16_t mode);
SR_PRIV int ut181a_send_cmd_setrange(struct sr_serial_dev_inst *serial, uint8_t range);
SR_PRIV int ut181a_send_cmd_get_save_count(struct sr_serial_dev_inst *serial);
SR_PRIV int ut181a_send_cmd_get_saved_value(struct sr_serial_dev_inst *serial, size_t idx);
SR_PRIV int ut181a_send_cmd_get_recs_count(struct sr_serial_dev_inst *serial);
SR_PRIV int ut181a_send_cmd_get_rec_info(struct sr_serial_dev_inst *serial, size_t idx);
SR_PRIV int ut181a_send_cmd_get_rec_samples(struct sr_serial_dev_inst *serial, size_t idx, size_t off);

SR_PRIV int ut181a_configure_waitfor(struct dev_context *devc,
	gboolean want_code, enum ut181_cmd_code want_data,
	enum ut181_rsp_type want_rsp_type,
	gboolean want_measure, gboolean want_rec_count,
	gboolean want_save_count, gboolean want_sample_count);
SR_PRIV int ut181a_waitfor_response(const struct sr_dev_inst *sdi, int timeout_ms);

SR_PRIV int ut181a_handle_events(int fd, int revents, void *cb_data);

SR_PRIV GVariant *ut181a_get_ranges_list(void);
SR_PRIV const char *ut181a_get_range_from_packet_bytes(struct dev_context *devc);
SR_PRIV int ut181a_set_range_from_text(const struct sr_dev_inst *sdi, const char *text);

#endif
