/*
 * This file is part of the libsigrok project.
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Options and their values:
 *
 * gnuplot: Write out a gnuplot interpreter script (.gpi file) to plot
 *          the datafile using the parameters given. It should be called
 *          from a gnuplot session with the data file name as a parameter
 *          after adjusting line styles, terminal, etc.
 *
 * scale:   The gnuplot graphs are scaled so they all have the same
 *          peak-to-peak distance. Defaults to TRUE.
 *
 * value:   The string used to separate values in a record. Defaults to ','.
 *
 * record:  The string to use to separate records. Default is newline. gnuplot
 *          files must use newline.
 *
 * frame:   The string to use when a frame ends. The default is a blank line.
 *          This may confuse some CSV parsers, but it makes gnuplot happy.
 *
 * comment: The string that starts a comment line. Defaults to ';'.
 *
 * header:  Print header comment with capture metadata. Defaults to TRUE.
 *
 * label:   What to use for channel labels as the first line of output.
 *          Values are "channel", "units", "off". Defaults to "units".
 *
 * time:    Whether or not the first column should include the time the sample
 *          was taken. Defaults to FALSE.
 *
 * trigger: Whether or not to add a "trigger" column as the last column.
 *          Defaults to FALSE.
 *
 * dedup:   Don't output duplicate rows. Defaults to FALSE. If time is off, then
 *          this is forced to be off.
 */

#include <config.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "output/csv"

struct ctx_channel {
	struct sr_channel *ch;
	char *label;
	float min, max;
};

enum time_value_e {
	TIME_VALUE_FALSE = 0x00,
	TIME_VALUE_TRUE = 0x01, /**< Compatibilits, resolves to sample_rate */
	TIME_VALUE_SAMPLE_RATE = 0x02, /**< Sample-Rate of device */
	TIME_VALUE_NOW_REL = 0x03, /**< Relative current timestamp */
	TIME_VALUE_NOW_ABS = 0x04, /**< Absolute current timestamp */
};

static const char *xlabels[] = {
	"samples",
	"milliseconds",
	"microseconds",
	"nanoseconds",
	"picoseconds",
	"femtoseconds",
	"attoseconds",
};

struct context {
	/* Options */
	const char *gnuplot;
	gboolean scale;
	const char *value;
	const char *record;
	const char *frame;
	const char *comment;
	gboolean header, did_header;
	gboolean label_do, label_did, label_names;
	enum time_value_e time;
	gboolean do_trigger;
	gboolean dedup;

	/* Plot data */
	unsigned int num_analog_channels;
	unsigned int num_logic_channels;
	struct ctx_channel *channels;

	/* Metadata */
	gboolean trigger;
	uint32_t num_samples;
	uint32_t channel_count, logic_channel_count;
	uint32_t channels_seen;
	uint64_t sample_rate;
	uint64_t sample_interval;
	uint64_t sample_scale;
	int64_t start_time;
	uint64_t out_sample_count;
	uint8_t *previous_sample;
	float *analog_samples;
	uint8_t *logic_samples;
	const char *xlabel;	/* Don't free: will point to a static string. */

	/* Input data constraints check. */
	gboolean have_checked;
	gboolean have_frames;
	uint64_t pkt_snums;
};

static enum time_value_e get_str2time_value(const char *arg_str)
{
	if (g_strcmp0("false", arg_str) == 0)
		return TIME_VALUE_FALSE;
	if (g_strcmp0("true", arg_str) == 0)
		return TIME_VALUE_TRUE;
	if (g_strcmp0("sample_rate", arg_str) == 0)
		return TIME_VALUE_SAMPLE_RATE;
	if (g_strcmp0("now_rel", arg_str) == 0)
		return TIME_VALUE_NOW_REL;
	if (g_strcmp0("now_abs", arg_str) == 0)
		return TIME_VALUE_NOW_ABS;
	return TIME_VALUE_FALSE;
}

/*
 * TODO:
 *  - Option to print comma-separated bits, or whole bytes/words (for 8/16
 *    channel LAs) as ASCII/hex etc. etc.
 */

static int init(struct sr_output *o, GHashTable *options)
{
	unsigned int i, analog_channels, logic_channels;
	struct context *ctx;
	struct sr_channel *ch;
	const char *label_string;
	GSList *l;

	if (!o || !o->sdi)
		return SR_ERR_ARG;

	ctx = g_malloc0(sizeof(struct context));
	o->priv = ctx;

	/* Options */
	ctx->gnuplot = g_strdup(g_variant_get_string(
		g_hash_table_lookup(options, "gnuplot"), NULL));
	ctx->scale = g_variant_get_boolean(g_hash_table_lookup(options, "scale"));
	ctx->value = g_strdup(g_variant_get_string(
		g_hash_table_lookup(options, "value"), NULL));
	ctx->record = g_strdup(g_variant_get_string(
		g_hash_table_lookup(options, "record"), NULL));
	ctx->frame = g_strdup(g_variant_get_string(
		g_hash_table_lookup(options, "frame"), NULL));
	ctx->comment = g_strdup(g_variant_get_string(
		g_hash_table_lookup(options, "comment"), NULL));
	ctx->header = g_variant_get_boolean(g_hash_table_lookup(options, "header"));
	ctx->time = get_str2time_value(
		g_variant_get_string(g_hash_table_lookup(options, "time"), NULL));
	ctx->do_trigger = g_variant_get_boolean(g_hash_table_lookup(options, "trigger"));
	label_string = g_variant_get_string(
		g_hash_table_lookup(options, "label"), NULL);
	ctx->dedup = g_variant_get_boolean(g_hash_table_lookup(options, "dedup"));
	ctx->dedup &= (ctx->time > TIME_VALUE_FALSE);

	if (*ctx->gnuplot && g_strcmp0(ctx->record, "\n"))
		sr_warn("gnuplot record separator must be newline.");

	if (*ctx->gnuplot && strlen(ctx->value) > 1)
		sr_warn("gnuplot doesn't support multichar value separators.");

	if ((ctx->label_did = ctx->label_do = g_strcmp0(label_string, "off") != 0))
		ctx->label_names = g_strcmp0(label_string, "units") != 0;

	/* Default method for time value */
	if (ctx->time == TIME_VALUE_TRUE)
		ctx->time = TIME_VALUE_SAMPLE_RATE;

	ctx->start_time = 0;

	sr_dbg("gnuplot = '%s', scale = %d", ctx->gnuplot, ctx->scale);
	sr_dbg("value = '%s', record = '%s', frame = '%s', comment = '%s'",
	       ctx->value, ctx->record, ctx->frame, ctx->comment);
	sr_dbg("header = %d, time = %d, do_trigger = %d, dedup = %d",
	       ctx->header, ctx->time, ctx->do_trigger, ctx->dedup);
	sr_dbg("label_do = %d, label_names = %d", ctx->label_do, ctx->label_names);

	analog_channels = logic_channels = 0;
	/* Get the number of channels, and the unitsize. */
	for (l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type == SR_CHANNEL_LOGIC) {
			ctx->logic_channel_count++;
			if (ch->enabled)
				logic_channels++;
		}
		if (ch->type == SR_CHANNEL_ANALOG && ch->enabled)
			analog_channels++;
	}
	if (analog_channels) {
		sr_info("Outputting %d analog values", analog_channels);
		ctx->num_analog_channels = analog_channels;
	}
	if (logic_channels) {
		sr_info("Outputting %d logic values", logic_channels);
		ctx->num_logic_channels = logic_channels;
	}
	ctx->channels = g_malloc(sizeof(struct ctx_channel)
		* (ctx->num_analog_channels + ctx->num_logic_channels));

	/* Once more to map the enabled channels. */
	ctx->channel_count = g_slist_length(o->sdi->channels);
	for (i = 0, l = o->sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->enabled) {
			if (ch->type == SR_CHANNEL_ANALOG) {
				ctx->channels[i].min = FLT_MAX;
				ctx->channels[i].max = FLT_MIN;
			} else if (ch->type == SR_CHANNEL_LOGIC) {
				ctx->channels[i].min = 0;
				ctx->channels[i].max = 1;
			} else {
				sr_warn("Unknown channel type %d.", ch->type);
			}
			if (ctx->label_do && ctx->label_names)
				ctx->channels[i].label = g_strdup(ch->name);
			ctx->channels[i++].ch = ch;
		}
	}

	return SR_OK;
}

static int apply_meta(const struct sr_output *o,
	const struct sr_datafeed_meta *meta)
{
	int retr;
	struct context *ctx;
	struct sr_config *config;
	GList *it;
	unsigned int i;

	retr = SR_OK;
	ctx = o->priv;

	if (o == NULL
		|| meta == NULL)
		return SR_ERR_BUG;

	if (meta->config == NULL)
		return SR_ERR_NA;

	for (it = (GList *)meta->config; it; it = it->next) {
		config = it->data;
		if (config->data == NULL)
			continue;
		switch (config->key) {
		case SR_CONF_SAMPLE_INTERVAL:
			if(g_variant_is_of_type(config->data,
				G_VARIANT_TYPE_UINT64)) {
				ctx->sample_interval = g_variant_get_uint64(config->data);
			}
			break;
		case SR_CONF_SAMPLERATE:
			if(g_variant_is_of_type(config->data,
				G_VARIANT_TYPE_UINT64)) {
				ctx->sample_rate = g_variant_get_uint64(config->data);

				i = 0;
				ctx->sample_scale = 1;
				while (ctx->sample_scale < ctx->sample_rate) {
					i++;
					ctx->sample_scale *= 1000;
				}
			}
			break;
		default:
			break;
		}
	}

	return retr;
}

static GString *gen_header(const struct sr_output *o,
			   const struct sr_datafeed_header *hdr)
{
	struct context *ctx;
	struct sr_channel *ch;
	GVariant *gvar;
	GString *header;
	GString *title;
	GSList *channels, *l;
	unsigned int num_channels, i;
	char *sample_rate_interval_s;

	ctx = o->priv;
	header = g_string_sized_new(512);
	title = g_string_sized_new(160);

	switch (ctx->time) {
	default:
		/* do nothing */
		break;
	case TIME_VALUE_SAMPLE_RATE:
		if (ctx->sample_rate == 0) {
			if (sr_config_get(o->sdi->driver, o->sdi, NULL,
					 SR_CONF_SAMPLERATE, &gvar) == SR_OK) {
				ctx->sample_rate = g_variant_get_uint64(gvar);
				g_variant_unref(gvar);
			}

			i = 0;
			ctx->sample_scale = 1;
			while (ctx->sample_scale < ctx->sample_rate) {
				i++;
				ctx->sample_scale *= 1000;
			}
			if (i < ARRAY_SIZE(xlabels))
				ctx->xlabel = xlabels[i];
			sr_info("Set sample rate, scale to %"
				PRIu64 ", %" PRIu64 " %s",
				ctx->sample_rate,
				ctx->sample_scale,
				ctx->xlabel);
		}

		if (ctx->sample_rate == 0
			&& ctx->sample_interval == 0) {
			if (sr_config_get(o->sdi->driver, o->sdi, NULL,
				SR_CONF_SAMPLE_INTERVAL, &gvar) == SR_OK) {
				ctx->sample_interval =
					g_variant_get_uint64(gvar);
				g_variant_unref(gvar);
			}
			ctx->xlabel = "seconds";
		}

		if (!ctx->sample_rate && !ctx->sample_interval) {
			ctx->xlabel = "N/A";
		}
		break;
	case TIME_VALUE_NOW_REL:
		ctx->start_time = g_get_real_time();
		ctx->xlabel = "seconds";
		break;
	case TIME_VALUE_NOW_ABS:
		ctx->start_time = 0;
		ctx->xlabel = "seconds";
		break;
	}

	if (o->sdi != NULL) {
		if ((o->sdi->vendor == NULL)
			&& (o->sdi->model == NULL))
			g_string_append_printf(title, "%s ",
				(o->sdi->driver ?
				o->sdi->driver->longname
				: "N/A "));
		if (o->sdi->vendor != NULL && o->sdi->vendor[0] != 0) {
			g_string_append_printf(title, "%s ",
				o->sdi->vendor);
		}
		if (o->sdi->model != NULL && o->sdi->model[0] != 0) {
			g_string_append_printf(title, "%s ",
				o->sdi->model);
		}
		if (o->sdi->version != NULL && o->sdi->version[0] != 0) {
			g_string_append_printf(title, "%s ",
				o->sdi->version);
		}
		if (o->sdi->serial_num != NULL && o->sdi->serial_num[0] != 0) {
			g_string_append_printf(title, "[S/N: %s] ",
				o->sdi->serial_num);
		}
	}

	if (title->len < 1) {
		g_string_printf(title, "N/A ");
	}

	/* Some metadata */
	if (ctx->header && !ctx->did_header) {
		/* save_gnuplot knows how many lines we print. */
		g_string_append_printf(header,
			"%s CSV generated by %s %s\n%s from %son %s",
			ctx->comment, PACKAGE_NAME,
			sr_package_version_string_get(), ctx->comment,
			title->str, ctime(&hdr->starttime.tv_sec));

		/* Columns / channels */
		channels = o->sdi ? o->sdi->channels : NULL;
		num_channels = g_slist_length(channels);
		g_string_append_printf(header, "%s Channels (%d/%d):",
			ctx->comment, ctx->num_analog_channels +
			ctx->num_logic_channels, num_channels);
		for (l = channels; l; l = l->next) {
			ch = l->data;
			if (ch->enabled)
				g_string_append_printf(header, " %s,",
					ch->name);
		}
		if (channels) {
			/* Drop last separator. */
			g_string_truncate(header, header->len - 1);
		}
		g_string_append_printf(header, "\n");
		if (ctx->sample_rate != 0) {
			sample_rate_interval_s =
				sr_samplerate_string(ctx->sample_rate);
			g_string_append_printf(header, "%s Samplerate: %s\n",
					       ctx->comment,
				sample_rate_interval_s);
			g_free(sample_rate_interval_s);
		}
		if (ctx->sample_interval != 0) {
			sample_rate_interval_s =
				sr_period_string(ctx->sample_interval, 1000);
			g_string_append_printf(header,
				"%s Sample interval: %s\n",
				ctx->comment, sample_rate_interval_s);
			g_free(sample_rate_interval_s);
		}
		ctx->did_header = TRUE;
	}

	g_string_free(title, TRUE);

	return header;
}

/*
 * Analog devices can have samples of different types. Since each
 * packet has only one meaning, it is restricted to having at most one
 * type of data. So they can send multiple packets for a single sample.
 * To further complicate things, they can send multiple samples in a
 * single packet.
 *
 * So we need to pull any channels of interest out of a packet and save
 * them until we have complete samples to output. Some devices make this
 * simple by sending DF_FRAME_BEGIN/DF_FRAME_END packets, the latter of which
 * signals the end of a set of samples, so we can dump things there.
 *
 * At least one driver (the demo driver) sends packets that contain parts of
 * multiple samples without wrapping them in DF_FRAME. Possibly this driver
 * is buggy, but it's also the standard for testing, so it has to be supported
 * as is.
 *
 * Many assumptions about the "shape" of the data here:
 *
 * All of the data for a channel is assumed to be in one frame;
 * otherwise the data in the second packet will overwrite the data in
 * the first packet.
 */
static void process_analog(struct context *ctx,
			   const struct sr_datafeed_analog *analog)
{
	int ret;
	size_t num_rcvd_ch, num_have_ch;
	size_t idx_have, idx_smpl, idx_rcvd;
	size_t idx_send;
	struct sr_analog_meaning *meaning;
	GSList *l;
	GString *strval;
	float *fdata = NULL;
	struct sr_channel *ch;

	if (!ctx->analog_samples) {
		ctx->analog_samples = g_malloc(analog->num_samples
			* sizeof(float) * ctx->num_analog_channels);
		if (!ctx->num_samples)
			ctx->num_samples = analog->num_samples;
	}
	if (ctx->num_samples != analog->num_samples)
		sr_warn("Expecting %u analog samples, got %u.",
			ctx->num_samples, analog->num_samples);

	meaning = analog->meaning;
	num_rcvd_ch = g_slist_length(meaning->channels);
	ctx->channels_seen += num_rcvd_ch;
	sr_dbg("Processing packet of %zu analog channels", num_rcvd_ch);
	fdata = g_malloc(analog->num_samples * num_rcvd_ch * sizeof(float));
	if ((ret = sr_analog_to_float(analog, fdata)) != SR_OK)
		sr_warn("Problems converting data to floating point values.");

	num_have_ch = ctx->num_analog_channels + ctx->num_logic_channels;
	idx_send = 0;
	for (idx_have = 0; idx_have < num_have_ch; idx_have++) {
		if (ctx->channels[idx_have].ch->type != SR_CHANNEL_ANALOG)
			continue;
		sr_dbg("Looking for channel %s",
		       ctx->channels[idx_have].ch->name);
		for (l = meaning->channels, idx_rcvd = 0; l; l = l->next, idx_rcvd++) {
			ch = l->data;
			sr_dbg("Checking %s", ch->name);
			if (ctx->channels[idx_have].ch != ch)
				continue;
			if (ctx->label_do && !ctx->label_names) {
				if (analog->meaning->mq == SR_MQ_COUNT
					&& analog->meaning->unit
					== SR_UNIT_UNITLESS) {
					strval = g_string_new("count");
					ctx->channels[idx_have].label
						= strval->str;
					g_string_free(strval, FALSE);
				} else {
					sr_analog_unit_to_string(analog,
						&ctx->channels[idx_have].label);
				}
			}
			for (idx_smpl = 0; idx_smpl < analog->num_samples; idx_smpl++)
				ctx->analog_samples[idx_smpl * ctx->num_analog_channels + idx_send] = fdata[idx_smpl * num_rcvd_ch + idx_rcvd];
			break;
		}
		idx_send++;
	}
	g_free(fdata);
}

/*
 * We treat logic packets the same as analog packets, though it's not
 * strictly required. This allows us to process mixed signals properly.
 */
static void process_logic(struct context *ctx,
			  const struct sr_datafeed_logic *logic)
{
	unsigned int i, j, ch, num_samples;
	int idx;
	uint8_t *sample;

	num_samples = logic->length / logic->unitsize;
	ctx->channels_seen += ctx->logic_channel_count;
	sr_dbg("Logic packet had %d channels", logic->unitsize * 8);
	if (!ctx->logic_samples) {
		ctx->logic_samples = g_malloc(num_samples * ctx->num_logic_channels);
		if (!ctx->num_samples)
			ctx->num_samples = num_samples;
	}
	if (ctx->num_samples != num_samples)
		sr_warn("Expecting %u samples, got %u",
			ctx->num_samples, num_samples);

	for (j = ch = 0; ch < ctx->num_logic_channels; j++) {
		if (ctx->channels[j].ch->type == SR_CHANNEL_LOGIC) {
			for (i = 0; i < num_samples; i++) {
				sample = logic->data + i * logic->unitsize;
				idx = ctx->channels[j].ch->index;
				if (ctx->label_do && !ctx->label_names)
					ctx->channels[j].label = "logic";
				ctx->logic_samples[i * ctx->num_logic_channels + ch] = sample[idx / 8] & (1 << (idx % 8));
			}
			ch++;
		}
	}
}

static void dump_saved_values(struct context *ctx, GString **out)
{
	unsigned int i, j, analog_size, num_channels;
	double sample_time_dbl;
	uint64_t sample_time_u64;
	float *analog_sample, value;
	uint8_t *logic_sample;

	/* If we haven't seen samples we're expecting, skip them. */
	if ((ctx->num_analog_channels && !ctx->analog_samples) ||
	    (ctx->num_logic_channels && !ctx->logic_samples)) {
		sr_warn("Discarding partial packet");
	} else {
		sr_info("Dumping %u samples", ctx->num_samples);

		if (!*out)
			*out = g_string_sized_new(512);
		num_channels =
		    ctx->num_logic_channels + ctx->num_analog_channels;

		if (ctx->label_do) {
			switch (ctx->time) {
			default:
				/* do nothing */
				break;
			case TIME_VALUE_SAMPLE_RATE:
				if (ctx->sample_rate > 0) {
					g_string_append_printf(*out, "%s%s",
						ctx->label_names ? "Time"
						: ctx->xlabel,
						ctx->value);
				} else if (ctx->sample_interval > 0) {
					g_string_append_printf(*out, "%s%s",
						ctx->label_names ? "Time"
						: ctx->xlabel,
						ctx->value);
				} else {
					g_string_append_printf(*out, "%s%s",
						ctx->label_names ? "Invalid"
						: ctx->xlabel,
						ctx->value);
				}
				break;
			case TIME_VALUE_NOW_ABS:
				g_string_append_printf(*out, "%s%s",
					ctx->label_names ? "Time" : ctx->xlabel,
					ctx->value);
				break;
			case TIME_VALUE_NOW_REL:
				g_string_append_printf(*out, "%s%s",
					ctx->label_names ? "Time" : ctx->xlabel,
					ctx->value);
				break;
			}
			for (i = 0; i < num_channels; i++) {
				g_string_append_printf(*out, "%s%s",
					ctx->channels[i].label, ctx->value);
				if (ctx->channels[i].ch->type == SR_CHANNEL_ANALOG
						&& ctx->label_names)
					g_free(ctx->channels[i].label);
			}
			if (ctx->do_trigger)
				g_string_append_printf(*out, "Trigger%s",
						       ctx->value);
			/* Drop last separator. */
			g_string_truncate(*out, (*out)->len - 1);
			g_string_append(*out, ctx->record);

			ctx->label_do = FALSE;
		}

		analog_size = ctx->num_analog_channels * sizeof(float);
		if (ctx->dedup && !ctx->previous_sample)
			ctx->previous_sample = g_malloc0(analog_size
				+ ctx->num_logic_channels);

		for (i = 0; i < ctx->num_samples; i++) {
			analog_sample =
			    &ctx->analog_samples[i * ctx->num_analog_channels];
			logic_sample =
			    &ctx->logic_samples[i * ctx->num_logic_channels];

			if (ctx->dedup) {
				if (i > 0 && i < ctx->num_samples - 1 &&
				    !memcmp(logic_sample, ctx->previous_sample,
					    ctx->num_logic_channels) &&
				    !memcmp(analog_sample,
					    ctx->previous_sample +
					    ctx->num_logic_channels,
					    analog_size))
					continue;
				memcpy(ctx->previous_sample, logic_sample,
				       ctx->num_logic_channels);
				memcpy(ctx->previous_sample
				       + ctx->num_logic_channels,
				       analog_sample, analog_size);
			}

			switch (ctx->time) {
			default:
				/* do nothing */
				break;
			case TIME_VALUE_SAMPLE_RATE:
				if (ctx->sample_rate && ctx->time) {
					sample_time_dbl =
						ctx->out_sample_count++;
					sample_time_dbl /= ctx->sample_rate;
					sample_time_dbl *= ctx->sample_scale;
					sample_time_u64 = sample_time_dbl;
					g_string_append_printf(*out,
						"%" PRIu64 "%s",
						sample_time_u64, ctx->value);
				} else if (ctx->sample_interval && ctx->time) {
					if (!ctx->sample_interval) {
						g_string_append_printf(*out,
							"0%s", ctx->value);
					} else if (ctx->time) {
						sample_time_dbl =
							ctx->out_sample_count++;
						sample_time_dbl *=
							ctx->sample_interval;
						sample_time_dbl /= 1000;
						g_string_append_printf(*out,
							"%f%s",
							sample_time_dbl,
							ctx->value);
					}
				} else {
					g_string_append_printf(*out, "0%s",
						ctx->value);
				}
				break;
			case TIME_VALUE_NOW_ABS:
			case TIME_VALUE_NOW_REL:
				sample_time_dbl = g_get_real_time()
					- ctx->start_time;
				sample_time_dbl /= 1000000;
				g_string_append_printf(*out, "%f%s",
					sample_time_dbl, ctx->value);
				break;
			}

			for (j = 0; j < num_channels; j++) {
				if (ctx->channels[j].ch->type == SR_CHANNEL_ANALOG) {
					value = ctx->analog_samples[i * ctx->num_analog_channels + j];
					ctx->channels[j].max =
					    fmax(value, ctx->channels[j].max);
					ctx->channels[j].min =
					    fmin(value, ctx->channels[j].min);
					g_string_append_printf(*out, "%g%s",
						value, ctx->value);
				} else if (ctx->channels[j].ch->type == SR_CHANNEL_LOGIC) {
					g_string_append_printf(*out, "%c%s",
							       ctx->logic_samples[i * ctx->num_logic_channels + j] ? '1' : '0', ctx->value);
				} else {
					sr_warn("Unexpected channel type: %d",
						ctx->channels[i].ch->type);
				}
			}

			if (ctx->do_trigger) {
				g_string_append_printf(*out, "%d%s",
					ctx->trigger, ctx->value);
				ctx->trigger = FALSE;
			}
			g_string_truncate(*out, (*out)->len - 1);
			g_string_append(*out, ctx->record);
		}
	}

	/* Discard all of the working space. */
	g_free(ctx->previous_sample);
	g_free(ctx->analog_samples);
	g_free(ctx->logic_samples);
	ctx->channels_seen = 0;
	ctx->num_samples = 0;
	ctx->previous_sample = NULL;
	ctx->analog_samples = NULL;
	ctx->logic_samples = NULL;
}

static void save_gnuplot(struct context *ctx)
{
	float offset, max, sum;
	unsigned int i, num_channels;
	GString *script;

	script = g_string_sized_new(512);
	g_string_append_printf(script, "set datafile separator '%s'\n",
			       ctx->value);
	if (ctx->label_did)
		g_string_append(script, "set key autotitle columnhead\n");
	if (ctx->xlabel && ctx->time > TIME_VALUE_FALSE)
		g_string_append_printf(script, "set xlabel '%s'\n",
				       ctx->xlabel);

	g_string_append(script, "plot ");

	num_channels = ctx->num_analog_channels + ctx->num_logic_channels;

	/* Graph position and scaling. */
	max = FLT_MIN;
	sum = 0;
	for (i = 0; i < num_channels; i++) {
		ctx->channels[i].max =
		    ctx->channels[i].max - ctx->channels[i].min;
		max = fmax(max, ctx->channels[i].max);
		sum += ctx->channels[i].max;
	}
	sum = (ctx->scale ? max : sum / num_channels) / 4;
	offset = sum;
	for (i = num_channels; i > 0;) {
		i--;
		ctx->channels[i].min = offset - ctx->channels[i].min;
		offset += sum + (ctx->scale ? max : ctx->channels[i].max);
	}

	for (i = 0; i < num_channels; i++) {
		sr_spew("Channel %d, min %g, max %g", i, ctx->channels[i].min,
			ctx->channels[i].max);
		g_string_append(script, "ARG1 ");
		if (ctx->did_header)
			g_string_append(script, "skip 4 ");
		g_string_append_printf(script, "using %u:($%u * %g + %g), ",
			(ctx->time > TIME_VALUE_FALSE),
			i + 1 + (ctx->time > TIME_VALUE_FALSE),
			ctx->scale ?
			max / ctx->channels[i].max : 1, ctx->channels[i].min);
		offset += 1.1 * (ctx->channels[i].max - ctx->channels[i].min);
	}
	g_string_truncate(script, script->len - 2);
	g_file_set_contents(ctx->gnuplot, script->str, script->len, NULL);
	g_string_free(script, TRUE);
}

static void check_input_constraints(struct context *ctx)
{
	size_t snum_count, snum_warn_limit;
	size_t logic, analog;
	gboolean has_frames, is_short, is_mixed, is_multi_analog;
	gboolean do_warn;

	/*
	 * Check and conditionally warn exactly once during execution
	 * of the output module on a set of input data.
	 */
	if (ctx->have_checked)
		return;
	ctx->have_checked = TRUE;

	/*
	 * This implementation of the CSV output module assumes some
	 * constraints which need not be met in reality. Emit warnings
	 * until a better version becomes available. Letting users know
	 * that their request may not get processed correctly is the
	 * only thing we can do for now except for complete refusal to
	 * process the input data.
	 *
	 * What the implementation appears to assume (unverified, this
	 * interpretation may be incorrect and/or incomplete):
	 * - Multi-channel analog data, or mixed signal input, always
	 *   is enclosed in frame markers.
	 * - Data which gets received across several packets spans a
	 *   consistent sample number range. All samples of one frame
	 *   and channel number or data type fit into a single packet.
	 *   Arbitrary chunking seems to not be supported.
	 * - A specific order of analog data packets is assumed.
	 *
	 * With these assumptions encoded in the implementation, and
	 * not being met at runtime, incorrect and unexpected results
	 * were seen for these configurations:
	 * - More than one analog channel.
	 * - The combination of logic and analog channel types.
	 *
	 * The condition of frames with large sample counts is a wild
	 * guess, the limit is a totally arbitrary choice. It assumes
	 * typical scope frames with at most a few thousand samples per
	 * frame, and assumes that a channel's data gets sent in large
	 * enough packets. The absence of a warning message does not
	 * necessarily translate to correct output, it's more of a rate
	 * limiting approach to not scare users too much.
	 */
	snum_count = ctx->pkt_snums;
	snum_warn_limit = 1 * 1000 * 1000;
	logic = ctx->num_logic_channels;
	analog = ctx->num_analog_channels;
	has_frames = ctx->have_frames;
	is_short = snum_count < snum_warn_limit;
	is_mixed = logic && analog;
	is_multi_analog = analog > 1;

	if (has_frames && is_short) {
		sr_info("Assuming consistent framed input data.");
		return;
	}

	do_warn = FALSE;
	if (has_frames) {
		sr_warn("Untested configuration: large frame content.");
		do_warn = TRUE;
	}
	if (is_mixed) {
		sr_warn("Untested configuration: mixed signal input data.");
		do_warn = TRUE;
	}
	if (is_multi_analog) {
		sr_warn("Untested configuration: multi-channel analog data.");
		do_warn = TRUE;
	}
	if (!do_warn)
		return;
	sr_warn("Resulting CSV output data may be incomplete or incorrect.");
}

static int receive(const struct sr_output *o,
		   const struct sr_datafeed_packet *packet, GString **out)
{
	struct context *ctx;
	const struct sr_datafeed_logic *logic;
	const struct sr_datafeed_analog *analog;

	*out = NULL;
	if (!o || !o->sdi)
		return SR_ERR_ARG;
	if (!(ctx = o->priv))
		return SR_ERR_ARG;

	sr_dbg("Got packet of type %d", packet->type);
	switch (packet->type) {
	case SR_DF_HEADER:
		ctx->have_checked = FALSE;
		ctx->have_frames = FALSE;
		ctx->pkt_snums = FALSE;
		*out = gen_header(o, packet->payload);
		break;
	case SR_DF_META:
		apply_meta(o, packet->payload);
		*out = g_string_sized_new(0);
		break;
	case SR_DF_TRIGGER:
		ctx->trigger = TRUE;
		break;
	case SR_DF_LOGIC:
		*out = g_string_sized_new(512);
		logic = packet->payload;
		ctx->pkt_snums = logic->length;
		ctx->pkt_snums /= logic->length;
		check_input_constraints(ctx);
		process_logic(ctx, logic);
		break;
	case SR_DF_ANALOG:
		*out = g_string_sized_new(512);
		analog = packet->payload;
		ctx->pkt_snums = analog->num_samples;
		ctx->pkt_snums /= g_slist_length(analog->meaning->channels);
		check_input_constraints(ctx);
		process_analog(ctx, analog);
		break;
	case SR_DF_FRAME_BEGIN:
		*out = g_string_sized_new(0);
		ctx->have_frames = TRUE;
		/* Fallthrough */
	case SR_DF_END:
		/* Got to end of frame/session with part of the data. */
		if (ctx->channels_seen)
			ctx->channels_seen = ctx->channel_count;
		if (*ctx->gnuplot)
			save_gnuplot(ctx);
		break;
	case SR_DF_FRAME_END:
		*out = g_string_sized_new(0);
		break;
	}

	/* If we've got them all, dump the values. */
	if (ctx->channels_seen >= ctx->channel_count)
		dump_saved_values(ctx, out);

	return SR_OK;
}

static int cleanup(struct sr_output *o)
{
	struct context *ctx;

	if (!o || !o->sdi)
		return SR_ERR_ARG;

	if (o->priv) {
		ctx = o->priv;
		g_free((gpointer)ctx->record);
		g_free((gpointer)ctx->frame);
		g_free((gpointer)ctx->comment);
		g_free((gpointer)ctx->gnuplot);
		g_free((gpointer)ctx->value);
		g_free(ctx->previous_sample);
		g_free(ctx->channels);
		g_free(o->priv);
		o->priv = NULL;
	}

	return SR_OK;
}

static struct sr_option options[] = {
	{"gnuplot", "gnuplot", "gnuplot script file name", NULL, NULL},
	{"scale", "scale", "Scale gnuplot graphs", NULL, NULL},
	{"value", "Value separator", "Character to print between values", NULL, NULL},
	{"record", "Record separator", "String to print between records", NULL, NULL},
	{"frame", "Frame separator", "String to print between frames", NULL, NULL},
	{"comment", "Comment start string", "String used at start of comment lines", NULL, NULL},
	{"header", "Output header", "Output header comment with capture metdata", NULL, NULL},
	{"label", "Label values", "Type of column labels", NULL, NULL},
	{"time", "Time column", "Output time as column 1", NULL, NULL},
	{"trigger", "Trigger column", "Output trigger indicator as last column ", NULL, NULL},
	{"dedup", "Dedup rows", "Set to false to output duplicate rows", NULL, NULL},
	ALL_ZERO
};

static const struct sr_option *get_options(void)
{
	GSList *l;

	if (!options[0].def) {
		options[0].def = g_variant_ref_sink(g_variant_new_string(""));
		options[1].def = g_variant_ref_sink(g_variant_new_boolean(TRUE));
		options[2].def = g_variant_ref_sink(g_variant_new_string(","));
		options[3].def = g_variant_ref_sink(g_variant_new_string("\n"));
		options[4].def = g_variant_ref_sink(g_variant_new_string("\n"));
		options[5].def = g_variant_ref_sink(g_variant_new_string(";"));
		options[6].def = g_variant_ref_sink(g_variant_new_boolean(TRUE));
		options[7].def = g_variant_ref_sink(g_variant_new_string("units"));
		l = NULL;
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("units")));
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("channel")));
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("off")));
		options[7].values = l;
		options[8].def = g_variant_ref_sink(g_variant_new_string("false"));
		l = NULL;
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("false")));
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("true")));
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("sample_rate")));
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("now_abs")));
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string("now_rel")));
		options[8].values = l;
		options[9].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[10].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
	}

	return options;
}

SR_PRIV struct sr_output_module output_csv = {
	.id = "csv",
	.name = "CSV",
	.desc = "Comma-separated values",
	.exts = (const char *[]){"csv", NULL},
	.flags = 0,
	.options = get_options,
	.init = init,
	.receive = receive,
	.cleanup = cleanup,
};
