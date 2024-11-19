/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Peter Skarpetis <peters@skarpetis.com>
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

#ifndef LIBSIGROK_HARDWARE_MHINSTEK_MHS5200A_PROTOCOL_H
#define LIBSIGROK_HARDWARE_MHINSTEK_MHS5200A_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "mhinstek-mhs-5200a"

#define PROTOCOL_LEN_MAX 32	/**< Max. line length for requests */
#define SERIAL_READ_TIMEOUT_MS 50
#define SERIAL_WRITE_TIMEOUT_MS 50

// don't change the values, these are returned by the function generator
enum attenuation_type {
	ATTENUATION_MINUS_20DB = 0,
	ATTENUATION_0DB        = 1,
};

// don't change the values, these are returned by the function generator
enum waveform_type {
	WAVEFORM_SINE = 0,
	WAVEFORM_SQUARE,
	WAVEFORM_TRIANGLE,
	WAVEFORM_RISING_SAWTOOTH,
	WAVEFORM_FALLING_SAWTOOTH,
	WAVEFORM_UNKNOWN = 1000,
};

enum waveform_options {
	WFO_FREQUENCY = 1,
	WFO_AMPLITUDE = 2,
	WFO_OFFSET = 4,
	WFO_PHASE = 8,
	WFO_DUTY_CYCLE = 16,
};

enum counter_function {
	COUNTER_MEASURE_FREQUENCY = 0,
	COUNTER_MEASURE_COUNT,
	COUNTER_MEASURE_PERIOD,
	COUNTER_MEASURE_PULSE_WIDTH,
	COUNTER_MEASURE_NEGATIVE_PULSE_WIDTH,
	COUNTER_MEASURE_DUTY_CYCLE,
};

enum gate_time {
	COUNTER_GATE_TIME_1_SEC = 0,
	COUNTER_GATE_TIME_10_SEC,
	COUNTER_GATE_TIME_10_MSEC,
	COUNTER_GATE_TIME_100_MSEC,
};

struct waveform_spec {
	enum waveform_type waveform;
	double freq_min;
	double freq_max;
	double freq_step;
	uint32_t opts;
};

struct channel_spec {
	const char *name;
	const struct waveform_spec *waveforms;
	uint32_t num_waveforms;
};

struct dev_context {
	struct sr_sw_limits limits;
	size_t buflen;
	double max_frequency; // for sine wave, all others are 6MHz
	uint8_t buf[PROTOCOL_LEN_MAX];

};


SR_PRIV int mhs5200a_receive_data(int fd, int revents, void *cb_data);
SR_PRIV const char *mhs5200a_waveform_to_string(enum waveform_type type);
SR_PRIV enum waveform_type mhs5200a_string_to_waveform(const char *type);
SR_PRIV int mhs5200a_frequency_limits(enum waveform_type wtype, double *freq_min, double *freq_max);
SR_PRIV int mhs5200a_get_model(struct sr_serial_dev_inst *serial, char *model, int modelsize);
SR_PRIV int mhs5200a_get_waveform(const struct sr_dev_inst *sdi, int ch, long *val);
SR_PRIV int mhs5200a_get_attenuation(const struct sr_dev_inst *sdi, int ch, long *val);
SR_PRIV int mhs5200a_get_onoff(const struct sr_dev_inst *sdi, long *val);
SR_PRIV int mhs5200a_get_frequency(const struct sr_dev_inst *sdi, int ch, double *val);
SR_PRIV int mhs5200a_get_amplitude(const struct sr_dev_inst *sdi, int ch, double *val);
SR_PRIV int mhs5200a_get_duty_cycle(const struct sr_dev_inst *sdi, int ch, double *val);
SR_PRIV int mhs5200a_get_offset(const struct sr_dev_inst *sdi, int ch, double *val);
SR_PRIV int mhs5200a_get_phase(const struct sr_dev_inst *sdi, int ch, double *val);
SR_PRIV int mhs5200a_set_frequency(const struct sr_dev_inst *sdi, int ch, double val);
SR_PRIV int mhs5200a_set_waveform(const struct sr_dev_inst *sdi, int ch, long val);
SR_PRIV int mhs5200a_set_waveform_string(const struct sr_dev_inst *sdi, int ch, const char *val);
SR_PRIV int mhs5200a_set_amplitude(const struct sr_dev_inst *sdi, int ch, double val);
SR_PRIV int mhs5200a_set_duty_cycle(const struct sr_dev_inst *sdi, int ch, double val);
SR_PRIV int mhs5200a_set_offset(const struct sr_dev_inst *sdi, int ch, double val);
SR_PRIV int mhs5200a_set_phase(const struct sr_dev_inst *sdi, int ch, double val);
SR_PRIV int mhs5200a_set_attenuation(const struct sr_dev_inst *sdi, int ch, long val);
SR_PRIV int mhs5200a_set_onoff(const struct sr_dev_inst *sdi, long val);
SR_PRIV int mhs5200a_set_counter_onoff(const struct sr_dev_inst *sdi, long val);
SR_PRIV int mhs5200a_set_counter_function(const struct sr_dev_inst *sdi, enum counter_function val);
SR_PRIV int mhs5200a_set_counter_gate_time(const struct sr_dev_inst *sdi, enum gate_time val);
SR_PRIV int mhs5200a_get_counter_value(const struct sr_dev_inst *sdi, double *val);
SR_PRIV int mhs5200a_get_counter_frequency(const struct sr_dev_inst *sdi, double *val);
SR_PRIV int mhs5200a_get_counter_period(const struct sr_dev_inst *sdi, double *val);
SR_PRIV int mhs5200a_get_counter_pulse_width(const struct sr_dev_inst *sdi, double *val);
SR_PRIV int mhs5200a_get_counter_duty_cycle(const struct sr_dev_inst *sdi, double *val);

#endif

/* Local Variables: */
/* mode: c */
/* indent-tabs-mode: t */
/* c-basic-offset: 8 */
/* tab-width: 8 */
/* End: */
