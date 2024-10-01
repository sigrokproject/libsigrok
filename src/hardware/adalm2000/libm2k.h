/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Analog Devices Inc.
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

#ifndef LIBSIGROK_HARDWARE_ADALM2000_LIBM2K_H
#define LIBSIGROK_HARDWARE_ADALM2000_LIBM2K_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SR_NO_TRIGGER                -1

enum DIGITAL_TRIGGER_SOURCE {
	SRC_TRIGGER_IN = 0,
	SRC_ANALOG_IN = 1,
	SRC_NONE = 2,
};

enum M2K_TRIGGER_CONDITION_DIGITAL {
	RISING_EDGE_DIGITAL = 0,
	FALLING_EDGE_DIGITAL = 1,
	LOW_LEVEL_DIGITAL = 2,
	HIGH_LEVEL_DIGITAL = 3,
	ANY_EDGE_DIGITAL = 4,
	NO_TRIGGER_DIGITAL = 5
};

enum M2K_RANGE {
	PLUS_MINUS_25V = 0,
	PLUS_MINUS_2_5V = 1,
};

enum ANALOG_TRIGGER_SOURCE {
	CH_1 = 0,
	CH_2 = 1,
	CH_1_OR_CH_2 = 2,
	CH_1_AND_CH_2 = 3,
	CH_1_XOR_CH_2 = 4,
	SRC_DIGITAL_IN = 5,
};

enum ANALOG_TRIGGER_MODE {
	ALWAYS = 0,
	ANALOG = 1,
};

enum ANALOG_TRIGGER_CONDITION {
	RISING = 0,
	FALLING = 1,
	LOW = 2,
	HIGH = 3,
};

struct CONTEXT_INFO {
	const char *id_vendor;
	const char *id_product;
	const char *manufacturer;
	const char *product;
	const char *serial;
	const char *uri;
};

/* Context */
struct M2k *sr_libm2k_context_open(const char *uri);

int sr_libm2k_context_close(struct M2k **m2k);

void sr_libm2k_context_adc_calibrate(struct M2k *m2k);

int sr_libm2k_context_get_all(struct CONTEXT_INFO ***info);

int sr_libm2k_has_mixed_signal(struct M2k *m2k);

void sr_libm2k_mixed_signal_acquisition_start(struct M2k *m2k, unsigned int nb_samples);

void sr_libm2k_mixed_signal_acquisition_stop(struct M2k *m2k);


/* Analog */
void sr_libm2k_analog_channel_enable(struct M2k *m2k, unsigned int chnIdx, int enable);

double sr_libm2k_analog_samplerate_get(struct M2k *m2k);

double sr_libm2k_analog_samplerate_set(struct M2k *m2k, double samplerate);

int sr_libm2k_analog_oversampling_ratio_get(struct M2k *m2k);

void sr_libm2k_analog_oversampling_ratio_set(struct M2k *m2k, int oversampling);

enum M2K_RANGE sr_libm2k_analog_range_get(struct M2k *m2k, unsigned int channel);

void sr_libm2k_analog_range_set(struct M2k *m2k, unsigned int channel, enum M2K_RANGE range);

void sr_libm2k_analog_acquisition_start(struct M2k *m2k, unsigned int buffer_size);

float **sr_libm2k_analog_samples_get(struct M2k *m2k, uint64_t nb_samples);

void sr_libm2k_analog_acquisition_cancel(struct M2k *m2k);

void sr_libm2k_analog_acquisition_stop(struct M2k *m2k);

void sr_libm2k_analog_kernel_buffers_count_set(struct M2k *m2k, unsigned int count);


/* Analog trigger */
enum ANALOG_TRIGGER_SOURCE sr_libm2k_analog_trigger_source_get(struct M2k *m2k);

void sr_libm2k_analog_trigger_source_set(struct M2k *m2k, enum ANALOG_TRIGGER_SOURCE source);

enum ANALOG_TRIGGER_MODE sr_libm2k_analog_trigger_mode_get(struct M2k *m2k, unsigned int chnIdx);

void sr_libm2k_analog_trigger_mode_set(struct M2k *m2k, unsigned int chnIdx,
				       enum ANALOG_TRIGGER_MODE mode);

enum ANALOG_TRIGGER_CONDITION sr_libm2k_analog_trigger_condition_get(struct M2k *m2k, unsigned int chnIdx);

void sr_libm2k_analog_trigger_condition_set(struct M2k *m2k, unsigned int chnIdx,
					    enum ANALOG_TRIGGER_CONDITION condition);

float sr_libm2k_analog_trigger_level_get(struct M2k *m2k, unsigned int chnIdx);

void sr_libm2k_analog_trigger_level_set(struct M2k *m2k, unsigned int chnIdx, float level);

int sr_libm2k_analog_trigger_delay_get(struct M2k *m2k);

void sr_libm2k_analog_trigger_delay_set(struct M2k *m2k, int delay);

void sr_libm2k_analog_streaming_flag_set(struct M2k *m2k, int flag);


/* Digital */
double sr_libm2k_digital_samplerate_get(struct M2k *m2k);

double sr_libm2k_digital_samplerate_set(struct M2k *m2k, double samplerate);

void sr_libm2k_digital_acquisition_start(struct M2k *m2k, unsigned int buffer_size);

uint32_t *sr_libm2k_digital_samples_get(struct M2k *m2k, uint64_t nb_samples);

void sr_libm2k_digital_acquisition_cancel(struct M2k *m2k);

void sr_libm2k_digital_acquisition_stop(struct M2k *m2k);

void sr_libm2k_digital_kernel_buffers_count_set(struct M2k *m2k, unsigned int count);


/* Digital trigger*/
void sr_libm2k_digital_trigger_source_set(struct M2k *m2k, enum DIGITAL_TRIGGER_SOURCE source);

enum M2K_TRIGGER_CONDITION_DIGITAL sr_libm2k_digital_trigger_condition_get(struct M2k *m2k, unsigned int chnIdx);

void sr_libm2k_digital_trigger_condition_set(struct M2k *m2k, unsigned int chnIdx, uint32_t cond);

int sr_libm2k_digital_trigger_delay_get(struct M2k *m2k);

void sr_libm2k_digital_trigger_delay_set(struct M2k *m2k, int delay);

void sr_libm2k_digital_streaming_flag_set(struct M2k *m2k, int flag);


#ifdef __cplusplus
}
#endif
#endif //LIBSIGROK_HARDWARE_ADALM2000_LIBM2K_H
