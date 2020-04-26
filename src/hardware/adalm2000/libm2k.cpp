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

#include "libm2k.h"
#include <libm2k/m2k.hpp>
#include <libm2k/contextbuilder.hpp>
#include <string.h>

extern "C" {
/* Context */
M2k *sr_libm2k_context_open(const char *uri)
{
	libm2k::context::M2k *ctx;
	if (strlen(uri) == 0) {
		ctx = libm2k::context::m2kOpen();
	} else {
		ctx = libm2k::context::m2kOpen(uri);
	}
	return (M2k *) ctx;
}

void sr_libm2k_context_adc_calibrate(struct M2k *m2k)
{
	libm2k::context::M2k *ctx = (libm2k::context::M2k *) m2k;
	ctx->calibrateADC();
}

int sr_libm2k_context_get_all(struct CONTEXT_INFO ***info)
{
	auto ctxs = libm2k::context::getContextsInfo();

	struct CONTEXT_INFO **ctxs_info = (struct CONTEXT_INFO **) malloc(
		ctxs.size() * sizeof(struct CONTEXT_INFO *));
	for (unsigned int i = 0; i < ctxs.size(); ++i) {
		ctxs_info[i] = (struct CONTEXT_INFO *) malloc(sizeof(struct CONTEXT_INFO));
		ctxs_info[i]->id_vendor = ctxs[i]->id_vendor.c_str();
		ctxs_info[i]->id_product = ctxs[i]->id_product.c_str();
		ctxs_info[i]->manufacturer = ctxs[i]->manufacturer.c_str();
		ctxs_info[i]->product = ctxs[i]->product.c_str();
		ctxs_info[i]->serial = ctxs[i]->serial.c_str();
		ctxs_info[i]->uri = ctxs[i]->uri.c_str();
	}
	*info = ctxs_info;
	return ctxs.size();
}

}
