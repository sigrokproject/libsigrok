/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>
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

#ifndef LIBSIGROK_HARDWARE_ADALM_2000_M2K_WRAPPER_H
#define LIBSIGROK_HARDWARE_ADALM_2000_M2K_WRAPPER_H

#ifdef __cplusplus /* if C++, specify external linkage to C functions */
extern "C" {
#endif

/* wrapper between C and C++
 */
struct m2k_wrapper {
	void *ctx; /* m2k context */
	void *dig; /* digital context */
	void *trig; /* hardware trigger */
};

typedef struct m2k_wrapper m2k_wrapper_t;

/**
 * struct used to store device information
 */
struct m2k_infos {
	char *name;
	char *vendor;
	char *id_product;
	char *id_vendor;
	char *serial_number;
	char *uri;
};

/* m2k_wrapper handling */
m2k_wrapper_t *m2k_open(char *uri);
int m2k_close(m2k_wrapper_t *m2k);

/* devices information */
int m2k_get_specific_info(char *uri, GSList **infos);
void m2k_list_all(GSList **infos);

/* samplerate configuration */
double m2k_set_rate(m2k_wrapper_t *m2k, double rate);
double m2k_get_rate(m2k_wrapper_t *m2k);

/* channel configuration */
int m2k_enable_channel(m2k_wrapper_t *m2k, unsigned short channels);

/* trigger configuration */
int m2k_configure_trigg(m2k_wrapper_t *m2k, uint16_t channel, uint8_t cond);
int m2k_disable_trigg(m2k_wrapper_t *m2k);
int m2k_pre_trigger_delay(m2k_wrapper_t *m2k, int delay);

/* acquisition */
int m2k_start_acquisition(m2k_wrapper_t *m2k, int nb_sample);
int m2k_stop_acquisition(m2k_wrapper_t *m2k);
int m2k_get_sample(m2k_wrapper_t *m2k, unsigned short *samples, int nb_sample);

#ifdef __cplusplus
}
#endif

#endif  // LIBSIGROK_HARDWARE_ADALM_2000_M2K_WRAPPER_H
