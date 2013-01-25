/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2011 Håvard Espeland <gus@ping.uio.no>
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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <glib.h>
#include "config.h" /* Needed for PACKAGE_STRING and others. */
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "text.h"

/* Message logging helpers with driver-specific prefix string. */
#define DRIVER_LOG_DOMAIN "output/text: "
#define sr_log(l, s, args...) sr_log(l, DRIVER_LOG_DOMAIN s, ## args)
#define sr_spew(s, args...) sr_spew(DRIVER_LOG_DOMAIN s, ## args)
#define sr_dbg(s, args...) sr_dbg(DRIVER_LOG_DOMAIN s, ## args)
#define sr_info(s, args...) sr_info(DRIVER_LOG_DOMAIN s, ## args)
#define sr_warn(s, args...) sr_warn(DRIVER_LOG_DOMAIN s, ## args)
#define sr_err(s, args...) sr_err(DRIVER_LOG_DOMAIN s, ## args)

SR_PRIV void flush_linebufs(struct context *ctx, uint8_t *outbuf)
{
	static int max_probename_len = 0;
	int len, i;

	if (ctx->linebuf[0] == 0)
		return;

	if (max_probename_len == 0) {
		/* First time through... */
		for (i = 0; ctx->probelist[i]; i++) {
			len = strlen(ctx->probelist[i]);
			if (len > max_probename_len)
				max_probename_len = len;
		}
	}

	for (i = 0; ctx->probelist[i]; i++) {
		sprintf((char *)outbuf + strlen((const char *)outbuf),
			"%*s:%s\n", max_probename_len,
			ctx->probelist[i], ctx->linebuf + i * ctx->linebuf_len);
	}

	/* Mark trigger with a ^ character. */
	if (ctx->mark_trigger != -1)
	{
		int space_offset = ctx->mark_trigger / 8;

		if (ctx->mode == MODE_ASCII)
			space_offset = 0;

		sprintf((char *)outbuf + strlen((const char *)outbuf),
			"T:%*s^\n", ctx->mark_trigger + space_offset, "");
	}

	memset(ctx->linebuf, 0, i * ctx->linebuf_len);
}

SR_PRIV int init(struct sr_output *o, int default_spl, enum outputmode mode)
{
	struct context *ctx;
	struct sr_probe *probe;
	GSList *l;
	uint64_t *samplerate;
	int num_probes, ret;
	char *samplerate_s;

	if (!(ctx = g_try_malloc0(sizeof(struct context)))) {
		sr_err("%s: ctx malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	o->internal = ctx;
	ctx->num_enabled_probes = 0;

	for (l = o->sdi->probes; l; l = l->next) {
		probe = l->data;
		if (!probe->enabled)
			continue;
		ctx->probelist[ctx->num_enabled_probes++] = probe->name;
	}

	ctx->probelist[ctx->num_enabled_probes] = 0;
	ctx->unitsize = (ctx->num_enabled_probes + 7) / 8;
	ctx->line_offset = 0;
	ctx->spl_cnt = 0;
	ctx->mark_trigger = -1;
	ctx->mode = mode;

	ret = SR_OK;
	if (o->param && o->param[0]) {
		ctx->samples_per_line = strtoul(o->param, NULL, 10);
		if (ctx->samples_per_line < 1) {
			ret = SR_ERR;
			goto err;
		}
	} else
		ctx->samples_per_line = default_spl;

	if (!(ctx->header = g_try_malloc0(512))) {
		sr_err("%s: ctx->header malloc failed", __func__);
		ret = SR_ERR_MALLOC;
		goto err;
	}

	snprintf(ctx->header, 511, "%s\n", PACKAGE_STRING);
	num_probes = g_slist_length(o->sdi->probes);
	if (o->sdi->driver || sr_dev_has_hwcap(o->sdi, SR_CONF_SAMPLERATE)) {
		ret = o->sdi->driver->config_get(SR_CONF_SAMPLERATE,
				(const void **)&samplerate, o->sdi);
		if (ret != SR_OK)
			goto err;
		if (!(samplerate_s = sr_samplerate_string(*samplerate))) {
			ret = SR_ERR;
			goto err;
		}
		snprintf(ctx->header + strlen(ctx->header),
			 511 - strlen(ctx->header),
			 "Acquisition with %d/%d probes at %s\n",
			 ctx->num_enabled_probes, num_probes, samplerate_s);
		g_free(samplerate_s);
	}

	ctx->linebuf_len = ctx->samples_per_line * 2 + 4;
	if (!(ctx->linebuf = g_try_malloc0(num_probes * ctx->linebuf_len))) {
		sr_err("%s: ctx->linebuf malloc failed", __func__);
		ret = SR_ERR_MALLOC;
		goto err;
	}

	if (!(ctx->linevalues = g_try_malloc0(num_probes))) {
		sr_err("%s: ctx->linevalues malloc failed", __func__);
		ret = SR_ERR_MALLOC;
	}

err:
	if (ret != SR_OK) {
		g_free(ctx->header);
		g_free(ctx);
	}

	return ret;
}

SR_PRIV int event(struct sr_output *o, int event_type, uint8_t **data_out,
		  uint64_t *length_out)
{
	struct context *ctx;
	int outsize;
	uint8_t *outbuf;

	ctx = o->internal;
	switch (event_type) {
	case SR_DF_TRIGGER:
		ctx->mark_trigger = ctx->spl_cnt;
		*data_out = NULL;
		*length_out = 0;
		break;
	case SR_DF_END:
		outsize = ctx->num_enabled_probes
				* (ctx->samples_per_line + 20) + 512;
		if (!(outbuf = g_try_malloc0(outsize))) {
			sr_err("%s: outbuf malloc failed", __func__);
			return SR_ERR_MALLOC;
		}
		flush_linebufs(ctx, outbuf);
		*data_out = outbuf;
		*length_out = strlen((const char *)outbuf);
		g_free(o->internal);
		o->internal = NULL;
		break;
	default:
		*data_out = NULL;
		*length_out = 0;
		break;
	}

	return SR_OK;
}
