/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 poljar (Damir Jelić) <poljarinho@gmail.com>
 * Copyright (C) 2018 Guido Trentalancia <guido@trentalancia.com>
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

#ifndef LIBSIGROK_HARDWARE_ROHDE_SCHWARZ_PROTOCOL_H
#define LIBSIGROK_HARDWARE_ROHDE_SCHWARZ_PROTOCOL_H

#include <glib.h>
#include <stdint.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "rohde-schwarz-hameg"

#define DIGITAL_CHANNELS_PER_POD	8
#define DIGITAL_CHANNELS_PER_NIBBLE	4

#define MAX_INSTRUMENT_VERSIONS		10
#define MAX_COMMAND_SIZE		256
#define MAX_ANALOG_CHANNEL_COUNT	4
#define MAX_DIGITAL_CHANNEL_COUNT	16
#define MAX_DIGITAL_GROUP_COUNT		2
#define MAX_TRIGGER_PATTERN_LENGTH	(MAX_ANALOG_CHANNEL_COUNT + MAX_DIGITAL_CHANNEL_COUNT)

/* Unequivocally a byte is 8 bits. */
#define BYTES_PER_POD			DIGITAL_CHANNELS_PER_POD / 8

/*
 * Set the FFT sample rate at its maximum value when
 * performing the Fast Fourier Transform (FFT).
 *
 * Only available on models that support a dedicated
 * option for setting the maximum sample rate (i.e.
 * not available on the RT series, except the RTO1000).
 *
 * When this feature is disabled, the FFT sample rate is
 * set adaptively according to the selected FFT frequency
 * span.
 *
 * Comment out the #define statement to disable this feature.
 */
#define FFT_SET_MAX_SAMPLING_RATE

/*
 * Digital Down Converter (DDC) low-pass filter factor.
 * This is used for minimum FFT sample rate calculation.
 *
 * Official value is not known. At the moment the
 * recommended empirical value is 1.5.
 */
#define FFT_DDC_LP_FILTER_FACTOR	1.5

/*
 * The Math Expression used to calculate the Fast Fourier
 * Transform (FFT).
 *
 * On most oscilloscope models this is "FFTMAG".
 */
#define FFT_MATH_EXPRESSION		"FFTMAG"

/*
 * The Math Expression used to exit from the Fast Fourier
 * Transform (FFT) mode.
 *
 * On most oscilloscope models the safest choice is "INV(CH1)".
 */
#define FFT_EXIT_MATH_EXPRESSION	"INV(CH1)"

/*
 * The Math Waveform to use for Fast Fourier Transform (FFT).
 *
 * Most oscilloscope models support five (5) Math Waveforms,
 * therefore using an index greater than five (5) here might
 * break the support for most models !
 *
 * An index greater than one (1) is normally used by default
 * to avoid overwriting user-defined Math Expressions.
 */
#define MATH_WAVEFORM_INDEX		5

/*
 * Maximum Sample Rate option array index (for all models).
 *
 * IMPORTANT: Always place the Maximum Sample Rate option
 *            (usually named "MSAM") at the following array
 *            index (see model_desc.h) !
 */
#define MAXIMUM_SAMPLE_RATE_INDEX	2

/*
 * Logic (Pattern) Trigger match encodings.
 */
#define LOGIC_TRIGGER_ZERO		'0'
#define LOGIC_TRIGGER_ONE		'1'
#define LOGIC_TRIGGER_DONTCARE		'X'

/*
 * The SCPI command string prefix used to set the data
 * length used in the data format.
 *
 * These are used to detect how many bytes are used by
 * a given dialect for digital data.
 * See, for example, SCPI_CMD_GET_DIG_DATA for the RTO
 * series.
 *
 * At the moment, this is the same for all dialects.
 */
#define SCPI_CMD_FORM_REAL		"FORM REAL,"
#define SCPI_CMD_FORM_INT		"FORM INT,"
#define SCPI_CMD_FORM_UINT		"FORM UINT,"

struct scope_config {
	const char *name[MAX_INSTRUMENT_VERSIONS];
	uint8_t analog_channels;
	const uint8_t digital_channels;
	uint8_t digital_pods;

	const gboolean digital_data_pod_index; /* Index based on POD instead of digital channel */
	uint8_t digital_data_byte_len; /* The length of digital data in bytes (UINT,8 = 1; REAL,32 = 4) */

	const char *(*analog_names)[];
	const char *(*digital_names)[];

	const uint32_t (*devopts)[];
	const uint8_t num_devopts;

	const uint32_t (*devopts_cg_analog)[];
	const uint8_t num_devopts_cg_analog;

	const uint32_t (*devopts_cg_digital)[];
	const uint8_t num_devopts_cg_digital;

	const char *(*waveform_sample_rate)[];
	const uint8_t num_waveform_sample_rate;

	const char *(*random_sampling)[];
	const uint8_t num_random_sampling;

	const char *(*acquisition_mode)[];
	const uint8_t num_acquisition_mode;

	const char *(*arithmetics_type)[];
	const uint8_t num_arithmetics_type;

	const char *(*interpolation_mode)[];
	const uint8_t num_interpolation_mode;

	const char *(*coupling_options)[];
	const uint8_t num_coupling_options;

	const char *(*logic_threshold)[];
	const uint8_t num_logic_threshold;
	const gboolean logic_threshold_pod_index; /* Index based on POD instead of nibble channel */

	const char *(*trigger_sources)[];
	const uint8_t num_trigger_sources;

	const char *(*edge_trigger_slopes)[];
	const uint8_t num_edge_trigger_slopes;

	const char *(*edge_trigger_coupling)[];
	const uint8_t num_edge_trigger_coupling;

	const char *(*fft_window_types)[];
	const uint8_t num_fft_window_types;

	const char *(*bandwidth_limit)[];
	const uint8_t num_bandwidth_limit;

	const uint64_t (*timebases)[][2];
	const uint8_t num_timebases;

	const uint64_t (*vscale)[][2];
	const uint8_t num_vscale;

	unsigned int num_xdivs;
	const unsigned int num_ydivs;

	const char *(*scpi_dialect)[];
};

struct analog_channel_state {
	gboolean state;

	unsigned int coupling;

	unsigned int vscale;
	float vertical_offset;

	char probe_unit;

	float user_threshold;

	unsigned int bandwidth_limit;
};

struct digital_pod_state {
	gboolean state;

	unsigned int threshold;
	float user_threshold;
};

struct scope_state {
	struct analog_channel_state *analog_channels;
	gboolean *digital_channels;
	struct digital_pod_state *digital_pods;

	int timebase;

	uint64_t sample_rate;
	unsigned int waveform_sample_rate;
	gboolean auto_record_length;

	unsigned int random_sampling;

	unsigned int acquisition_mode;
	unsigned int arithmetics_type;

	unsigned int interpolation_mode;

	float horiz_triggerpos;

	unsigned int trigger_source;
	unsigned int edge_trigger_slope;
	unsigned int edge_trigger_coupling;
	gboolean edge_trigger_lowpass;
	gboolean edge_trigger_noise_rej;
	char trigger_pattern[MAX_TRIGGER_PATTERN_LENGTH];

	gboolean high_resolution;
	gboolean peak_detection;

	float fft_sample_rate;
	unsigned int fft_window_type;
	float fft_freq_start;
	float fft_freq_stop;
	float fft_freq_span;
	float fft_freq_center;
	float fft_rbw;
	gboolean fft_span_rbw_coupling;
	unsigned int fft_span_rbw_ratio;
	char restore_math_expr[MAX_COMMAND_SIZE];
	unsigned int restore_waveform_sample_rate;
	gboolean restore_auto_record_length;

	gboolean beep_on_trigger;
	gboolean beep_on_error;
};

struct dev_context {
	void *model_config;
	void *model_state;

	struct sr_channel_group **analog_groups;
	struct sr_channel_group **digital_groups;

	GSList *enabled_channels;
	GSList *current_channel;
	uint64_t num_samples;
	uint64_t num_frames;

	uint64_t samples_limit;
	uint64_t frame_limit;

	size_t pod_count;
	GByteArray *logic_data;
};

/* Supported trigger states (sigrok native trigger definition format). */
static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

SR_PRIV int rs_init_device(struct sr_dev_inst *sdi);
SR_PRIV int rs_check_esr(const struct sr_dev_inst *sdi);
SR_PRIV int rs_request_data(const struct sr_dev_inst *sdi);
SR_PRIV int rs_receive_data(int fd, int revents, void *cb_data);

SR_PRIV struct scope_state *rs_scope_state_new(struct scope_config *config);
SR_PRIV void rs_scope_state_free(struct scope_state *state);
SR_PRIV int rs_scope_state_get(const struct sr_dev_inst *sdi);
SR_PRIV int rs_update_sample_rate(const struct sr_dev_inst *sdi);

#endif
