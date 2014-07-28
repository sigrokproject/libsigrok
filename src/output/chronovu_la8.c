/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/chronovu-la8"

struct context {
	unsigned int num_enabled_channels;
	gboolean triggered;
	uint64_t samplerate;
	uint64_t samplecount;
	int *channel_index;
	GString *pretrig_buf;
};

/**
 * Check if the given samplerate is supported by the LA8 hardware.
 *
 * @param samplerate The samplerate (in Hz) to check.
 *
 * @return 1 if the samplerate is supported/valid, 0 otherwise.
 */
static gboolean is_valid_samplerate(uint64_t samplerate)
{
	unsigned int i;

	for (i = 0; i < 255; i++) {
		if (samplerate == (SR_MHZ(100) / (i + 1)))
			return TRUE;
	}

	return FALSE;
}

/**
 * Convert a samplerate (in Hz) to the 'divcount' value the LA8 wants.
 *
 * LA8 hardware: sample period = (divcount + 1) * 10ns.
 * Min. value for divcount: 0x00 (10ns sample period, 100MHz samplerate).
 * Max. value for divcount: 0xfe (2550ns sample period, 392.15kHz samplerate).
 *
 * @param samplerate The samplerate in Hz.
 *
 * @return The divcount value as needed by the hardware, or 0xff upon errors.
 */
static uint8_t samplerate_to_divcount(uint64_t samplerate)
{
	if (samplerate == 0 || !is_valid_samplerate(samplerate)) {
		sr_warn("Invalid samplerate (%" PRIu64 "Hz)", samplerate);
		return 0xff;
	}

	return (SR_MHZ(100) / samplerate) - 1;
}

static int init(struct sr_output *o, GHashTable *options)
{
	struct context *ctx;
	struct sr_channel *ch;
	GSList *l;

	(void)options;

	if (!o || !o->sdi)
		return SR_ERR_ARG;

	ctx = g_malloc0(sizeof(struct context));
	o->priv = ctx;

	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_LOGIC)
			continue;
		if (!ch->enabled)
			continue;
		ctx->num_enabled_channels++;
	}
	ctx->channel_index = g_malloc(sizeof(int) * ctx->num_enabled_channels);
	ctx->pretrig_buf = g_string_sized_new(1024);

	return SR_OK;
}

static int receive(const struct sr_output *o, const struct sr_datafeed_packet *packet,
		GString **out)
{
	const struct sr_datafeed_logic *logic;
	struct context *ctx;
	GVariant *gvar;
	uint64_t samplerate;
	gchar c[4];

	*out = NULL;
	if (!o || !o->sdi)
		return SR_ERR_ARG;
	if (!(ctx = o->priv))
		return SR_ERR_ARG;

	switch (packet->type) {
	case SR_DF_HEADER:
		/* One byte for the 'divcount' value. */
		if (sr_config_get(o->sdi->driver, o->sdi, NULL, SR_CONF_SAMPLERATE,
				&gvar) == SR_OK) {
			samplerate = g_variant_get_uint64(gvar);
			g_variant_unref(gvar);
		} else
			samplerate = 0;
		c[0] = samplerate_to_divcount(samplerate);
		*out = g_string_new_len(c, 1);
		ctx->triggered = FALSE;
		break;
	case SR_DF_TRIGGER:
		/* Four bytes (little endian) for the trigger point. */
		c[0] = ctx->samplecount & 0xff;
		c[1] = (ctx->samplecount >> 8) & 0xff;
		c[2] = (ctx->samplecount >> 16) & 0xff;
		c[3] = (ctx->samplecount >> 24) & 0xff;
		*out = g_string_new_len(c, 4);
		/* Flush the pre-trigger buffer. */
		if (ctx->pretrig_buf->len)
			g_string_append_len(*out, ctx->pretrig_buf->str,
					ctx->pretrig_buf->len);
		ctx->triggered = TRUE;
		break;
	case SR_DF_LOGIC:
		logic = packet->payload;
		if (!ctx->triggered)
			g_string_append_len(ctx->pretrig_buf, logic->data, logic->length);
		else
			*out = g_string_new_len(logic->data, logic->length);
		ctx->samplecount += logic->length / logic->unitsize;
		break;
	case SR_DF_END:
		if (!ctx->triggered && ctx->pretrig_buf->len) {
			/* We never got a trigger, submit an empty one. */
			*out = g_string_sized_new(ctx->pretrig_buf->len + 4);
			g_string_append_len(*out, "\x00\x00\x00\x00", 4);
			g_string_append_len(*out, ctx->pretrig_buf->str, ctx->pretrig_buf->len);
		}
		break;
	}

	return SR_OK;
}

static int cleanup(struct sr_output *o)
{
	struct context *ctx;

	if (!o || !o->sdi)
		return SR_ERR_ARG;

	if (o->priv) {
		ctx = o->priv;
		g_string_free(ctx->pretrig_buf, TRUE);
		g_free(ctx->channel_index);
		g_free(o->priv);
		o->priv = NULL;
	}

	return SR_OK;
}

SR_PRIV struct sr_output_module output_chronovu_la8 = {
	.id = "chronovu-la8",
	.name = "ChronoVu LA8",
	.desc = "ChronoVu LA8 native file format",
	.options = NULL,
	.init = init,
	.receive = receive,
	.cleanup = cleanup,
};
