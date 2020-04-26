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
int sr_libm2k_context_get_all(struct CONTEXT_INFO ***info);


#ifdef __cplusplus
}
#endif
#endif //LIBSIGROK_HARDWARE_ADALM2000_LIBM2K_H
