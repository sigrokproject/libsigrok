/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef LIBSIGROK_TESTS_LIB_H
#define LIBSIGROK_TESTS_LIB_H

#include "../include/libsigrok/libsigrok.h"

struct sr_dev_driver *srtest_driver_get(const char *drivername);
struct sr_input_format *srtest_input_get(const char *id);

void srtest_driver_init(struct sr_context *sr_ctx, struct sr_dev_driver *driver);
void srtest_driver_init_all(struct sr_context *sr_ctx);

void srtest_input_init(struct sr_context *sr_ctx, struct sr_input_format *input);
void srtest_input_init_all(struct sr_context *sr_ctx);

void srtest_set_samplerate(struct sr_dev_driver *driver, uint64_t samplerate);
uint64_t srtest_get_samplerate(struct sr_dev_driver *driver);
void srtest_check_samplerate(struct sr_context *sr_ctx, const char *drivername,
			     uint64_t samplerate);

void srtest_buf_to_file(const char *filename, const uint8_t *buf, uint64_t len);
GArray *srtest_get_enabled_logic_channels(const struct sr_dev_inst *sdi);

Suite *suite_core(void);
Suite *suite_driver_all(void);
Suite *suite_input_all(void);
Suite *suite_input_binary(void);
Suite *suite_output_all(void);
Suite *suite_strutil(void);
Suite *suite_version(void);

#endif
