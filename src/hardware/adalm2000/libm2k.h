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


/* Analog */
double sr_libm2k_analog_samplerate_get(struct M2k *m2k);

double sr_libm2k_analog_samplerate_set(struct M2k *m2k, double samplerate);

int sr_libm2k_analog_oversampling_ratio_get(struct M2k *m2k);

void sr_libm2k_analog_oversampling_ratio_set(struct M2k *m2k, int oversampling);

/* Digital */
double sr_libm2k_digital_samplerate_get(struct M2k *m2k);

double sr_libm2k_digital_samplerate_set(struct M2k *m2k, double samplerate);

#ifdef __cplusplus
}
#endif
#endif //LIBSIGROK_HARDWARE_ADALM2000_LIBM2K_H
