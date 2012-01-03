/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "sigrok.h"
#include "sigrok-internal.h"

struct context {
	unsigned int num_enabled_probes;
	unsigned int unitsize;
	char *probelist[SR_MAX_NUM_PROBES + 1];
	uint64_t trigger_point;
	uint64_t samplerate;
};

/**
 * Check if the given samplerate is supported by the LA8 hardware.
 *
 * @param samplerate The samplerate (in Hz) to check.
 * @return 1 if the samplerate is supported/valid, 0 otherwise.
 */
static int is_valid_samplerate(uint64_t samplerate)
{
	unsigned int i;

	for (i = 0; i < 255; i++) {
		if (samplerate == (SR_MHZ(100) / (i + 1)))
			return 1;
	}

	sr_warn("la8 out: %s: invalid samplerate (%" PRIu64 "Hz)",
		__func__, samplerate);

	return 0;
}

/**
 * Convert a samplerate (in Hz) to the 'divcount' value the LA8 wants.
 *
 * LA8 hardware: sample period = (divcount + 1) * 10ns.
 * Min. value for divcount: 0x00 (10ns sample period, 100MHz samplerate).
 * Max. value for divcount: 0xfe (2550ns sample period, 392.15kHz samplerate).
 *
 * @param samplerate The samplerate in Hz.
 * @return The divcount value as needed by the hardware, or 0xff upon errors.
 */
static uint8_t samplerate_to_divcount(uint64_t samplerate)
{
	if (samplerate == 0) {
		sr_warn("la8 out: %s: samplerate was 0", __func__);
		return 0xff;
	}

	if (!is_valid_samplerate(samplerate)) {
		sr_warn("la8 out: %s: can't get divcount, samplerate invalid",
			__func__);
		return 0xff;
	}

	return (SR_MHZ(100) / samplerate) - 1;
}

static int init(struct sr_output *o)
{
	struct context *ctx;
	struct sr_probe *probe;
	GSList *l;
	int num_probes;
	uint64_t samplerate;

	if (!o) {
		sr_warn("la8 out: %s: o was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!o->device) {
		sr_warn("la8 out: %s: o->device was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!o->device->plugin) {
		sr_warn("la8 out: %s: o->device->plugin was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!(ctx = calloc(1, sizeof(struct context)))) {
		sr_warn("la8 out: %s: ctx calloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	o->internal = ctx;

	/* Get the number of probes, their names, and the unitsize. */
	/* TODO: Error handling. */
	for (l = o->device->probes; l; l = l->next) {
		probe = l->data;
		if (!probe->enabled)
			continue;
		ctx->probelist[ctx->num_enabled_probes++] = probe->name;
	}
	ctx->probelist[ctx->num_enabled_probes] = 0;
	ctx->unitsize = (ctx->num_enabled_probes + 7) / 8;

	num_probes = g_slist_length(o->device->probes);

	if (sr_device_has_hwcap(o->device, SR_HWCAP_SAMPLERATE)) {
		samplerate = *((uint64_t *) o->device->plugin->get_device_info(
				o->device->plugin_index, SR_DI_CUR_SAMPLERATE));
		/* TODO: Error checks. */
	} else {
		samplerate = 0; /* TODO: Error or set some value? */
	}
	ctx->samplerate = samplerate;

	return 0; /* TODO: SR_OK? */
}

static int event(struct sr_output *o, int event_type, char **data_out,
		 uint64_t *length_out)
{
	struct context *ctx;
	char *outbuf;

	if (!o) {
		sr_warn("la8 out: %s: o was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!(ctx = o->internal)) {
		sr_warn("la8 out: %s: o->internal was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!data_out) {
		sr_warn("la8 out: %s: data_out was NULL", __func__);
		return SR_ERR_ARG;
	}

	switch (event_type) {
	case SR_DF_TRIGGER:
		sr_dbg("la8 out: %s: SR_DF_TRIGGER event", __func__);
		/* Save the trigger point for later (SR_DF_END). */
		ctx->trigger_point = 0; /* TODO: Store _actual_ value. */
		break;
	case SR_DF_END:
		sr_dbg("la8 out: %s: SR_DF_END event", __func__);
		if (!(outbuf = malloc(4 + 1))) {
			sr_warn("la8 out: %s: outbuf malloc failed", __func__);
			return SR_ERR_MALLOC;
		}

		/* One byte for the 'divcount' value. */
		outbuf[0] = samplerate_to_divcount(ctx->samplerate);
		// if (outbuf[0] == 0xff) {
		// 	sr_warn("la8 out: %s: invalid divcount", __func__);
		// 	return SR_ERR;
		// }

		/* Four bytes (little endian) for the trigger point. */
		outbuf[1] = (ctx->trigger_point >>  0) & 0xff;
		outbuf[2] = (ctx->trigger_point >>  8) & 0xff;
		outbuf[3] = (ctx->trigger_point >> 16) & 0xff;
		outbuf[4] = (ctx->trigger_point >> 24) & 0xff;

		*data_out = outbuf;
		*length_out = 4 + 1;
		free(o->internal);
		o->internal = NULL;
		break;
	default:
		sr_warn("la8 out: %s: unsupported event type: %d", __func__,
			event_type);
		*data_out = NULL;
		*length_out = 0;
		break;
	}

	return SR_OK;
}

static int data(struct sr_output *o, const char *data_in, uint64_t length_in,
		char **data_out, uint64_t *length_out)
{
	struct context *ctx;
	char *outbuf;

	if (!o) {
		sr_warn("la8 out: %s: o was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!(ctx = o->internal)) {
		sr_warn("la8 out: %s: o->internal was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!data_in) {
		sr_warn("la8 out: %s: data_in was NULL", __func__);
		return SR_ERR_ARG;
	}

	if (!(outbuf = calloc(1, length_in))) {
		sr_warn("la8 out: %s: outbuf calloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	memcpy(outbuf, data_in, length_in);

	*data_out = outbuf;
	*length_out = length_in;

	return SR_OK;
}

struct sr_output_format output_chronovu_la8 = {
	.id = "chronovu-la8",
	.description = "ChronoVu LA8",
	.df_type = SR_DF_LOGIC,
	.init = init,
	.data = data,
	.event = event,
};
