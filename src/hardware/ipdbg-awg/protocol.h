/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Daniel Anselmi <danselmi@gmx.ch>
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

#ifndef LIBSIGROK_HARDWARE_IPDBG_AWG_PROTOCOL_H
#define LIBSIGROK_HARDWARE_IPDBG_AWG_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "ipdbg-awg"

struct ipdbg_awg_tcp {
	char *address;
	char *port;
	int socket;
};

enum {
	IPDBG_AWG_WAVEFORM_DC = 0,
	IPDBG_AWG_WAVEFORM_SINE,
	IPDBG_AWG_WAVEFORM_RECTANGLE,
	IPDBG_AWG_WAVEFORM_TRIANGLE,
	//IPDBG_AWG_WAVEFORM_NOISE,
	IPDBG_AWG_WAVEFORM_ARB,
	IPDBG_AWG_NUM_WAVEFORM_TYPES
};

struct dev_context {
	uint64_t sample_rate;
	uint64_t center_freq;
	double frequency;
	//double frequency_act;
	double amplitude;
	double phase;
	double offset;
	double dutycycle;
	int64_t *wave_buffer;
	size_t limit_samples_max;
	size_t limit_samples;
	uint32_t data_width;
	uint32_t addr_width;
	uint32_t data_width_bytes;
	uint32_t addr_width_bytes;
	int waveform;
	gboolean is_running;
	gboolean complex_part_parallel;
	uint8_t periods;
};

SR_PRIV struct ipdbg_awg_tcp *ipdbg_awg_tcp_new(void);
SR_PRIV void ipdbg_awg_tcp_free(struct ipdbg_awg_tcp *tcp);
SR_PRIV int ipdbg_awg_tcp_open(struct ipdbg_awg_tcp *tcp);
SR_PRIV int ipdbg_awg_tcp_close(struct ipdbg_awg_tcp *tcp);
SR_PRIV int ipdbg_awg_tcp_receive(struct ipdbg_awg_tcp *tcp,
		uint8_t *buf, size_t bufsize);
SR_PRIV int ipdbg_awg_send_reset(struct ipdbg_awg_tcp *tcp);
SR_PRIV void ipdbg_awg_abort_acquisition(const struct sr_dev_inst *sdi);

SR_PRIV void ipdbg_awg_get_addrwidth_and_datawidth(
		struct ipdbg_awg_tcp *tcp, struct dev_context *devc);
SR_PRIV void ipdbg_awg_get_isrunning(
		struct ipdbg_awg_tcp *tcp, struct dev_context *devc);
SR_PRIV void ipdbg_awg_init_waveform(struct dev_context *devc);
SR_PRIV int ipdbg_awg_update_waveform(const struct sr_dev_inst *sdi);
SR_PRIV int ipdbg_awg_set_frequency(
		const struct sr_dev_inst *sdi, double f_value);
SR_PRIV double ipdbg_awg_get_frequency(struct dev_context *devc);
SR_PRIV int ipdbg_awg_set_amplitude(
		const struct sr_dev_inst *sdi, double a_value);
SR_PRIV double ipdbg_awg_get_amplitude(struct dev_context *devc);
SR_PRIV int ipdbg_awg_set_phase(const struct sr_dev_inst *sdi, double a_value);
SR_PRIV double ipdbg_awg_get_phase(struct dev_context *devc);
SR_PRIV int ipdbg_awg_set_offset(const struct sr_dev_inst *sdi,	double o_value);
SR_PRIV double ipdbg_awg_get_offset(struct dev_context *devc);
SR_PRIV int ipdbg_awg_set_dutycycle(
		const struct sr_dev_inst *sdi, double d_value);
SR_PRIV double ipdbg_awg_get_dutycycle(struct dev_context *devc);
SR_PRIV int ipdbg_awg_set_waveform(const struct sr_dev_inst *sdi, int wf_value);
SR_PRIV const char *ipdbg_awg_waveform_to_string(int waveform);
SR_PRIV uint64_t ipdbg_awg_get_sample_rate(struct dev_context *devc);
SR_PRIV int ipdbg_awg_set_sample_rate(
		const struct sr_dev_inst *sdi, uint64_t rate);
SR_PRIV uint64_t ipdbg_awg_get_center_freq(struct dev_context *devc);
SR_PRIV int ipdbg_awg_set_center_freq(
		const struct sr_dev_inst *sdi, uint64_t center_freq);
SR_PRIV int ipdbg_awg_set_enable(const struct sr_dev_inst *sdi,	int en);
SR_PRIV int ipdbg_awg_start(const struct sr_dev_inst *sdi);
SR_PRIV int ipdbg_awg_stop(const struct sr_dev_inst *sdi);

#endif

