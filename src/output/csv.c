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
 *          was taken. Defaults to TRUE.
 *
 * trigger: Whether or not to add a "trigger" column as the last column.
 *          Defaults to FALSE.
 *
 * dedup:   Don't output duplicate rows. Defaults to TRUE. If time is off, then
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
	gboolean time;
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
	uint64_t period;
	uint64_t sample_time;
	uint8_t *previous_sample;
	float *analog_samples;
	uint8_t *logic_samples;
	const char *xlabel;	/* Don't free: will point to a static string. */
	const char *title;	/* Don't free: will point into the driver struct. */
};

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
	ctx->time = g_variant_get_boolean(g_hash_table_lookup(options, "time"));
	ctx->do_trigger = g_variant_get_boolean(g_hash_table_lookup(options, "trigger"));
	label_string = g_variant_get_string(
		g_hash_table_lookup(options, "label"), NULL);
	ctx->dedup = g_variant_get_boolean(g_hash_table_lookup(options, "dedup"));
	ctx->dedup &= ctx->time;

	if (*ctx->gnuplot && g_strcmp0(ctx->record, "\n"))
		sr_warn("gnuplot record separator must be newline.");

	if (*ctx->gnuplot && strlen(ctx->value) > 1)
		sr_warn("gnuplot doesn't support multichar value separators.");

	if ((ctx->label_did = ctx->label_do = g_strcmp0(label_string, "off") != 0))
		ctx->label_names = g_strcmp0(label_string, "units") != 0;

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
				ctx->channels[i].label = ch->name;
			ctx->channels[i++].ch = ch;
		}
	}

	return SR_OK;
}

static const char *xlabels[] = {
	"samples", "milliseconds", "microseconds", "nanoseconds", "picoseconds",
	"femtoseconds", "attoseconds",
};

static GString *gen_header(const struct sr_output *o,
			   const struct sr_datafeed_header *hdr)
{
	struct context *ctx;
	struct sr_channel *ch;
	GVariant *gvar;
	GString *header;
	GSList *channels, *l;
	unsigned int num_channels, i;
	uint64_t samplerate = 0, sr;
	char *samplerate_s;

	ctx = o->priv;
	header = g_string_sized_new(512);

	if (ctx->period == 0) {
		if (sr_config_get(o->sdi->driver, o->sdi, NULL,
				  SR_CONF_SAMPLERATE, &gvar) == SR_OK) {
			samplerate = g_variant_get_uint64(gvar);
			g_variant_unref(gvar);
		}

		i = 0;
		sr = 1;
		while (sr < samplerate) {
			i++;
			sr *= 1000;
		}
		if (samplerate)
			ctx->period = sr / samplerate;
		if (i < ARRAY_SIZE(xlabels))
			ctx->xlabel = xlabels[i];
		sr_info("Set sample period to %" PRIu64 " %s",
			ctx->period, ctx->xlabel);
	}
	ctx->title = (o->sdi && o->sdi->driver) ? o->sdi->driver->longname : "unknown";

	/* Some metadata */
	if (ctx->header && !ctx->did_header) {
		/* save_gnuplot knows how many lines we print. */
		g_string_append_printf(header,
			"%s CSV generated by %s %s\n%s from %s on %s",
			ctx->comment, PACKAGE_NAME,
			SR_PACKAGE_VERSION_STRING, ctx->comment,
			ctx->title, ctime(&hdr->starttime.tv_sec));

		/* Columns / channels */
		channels = o->sdi ? o->sdi->channels : NULL;
		num_channels = g_slist_length(channels);
		g_string_append_printf(header, "%s Channels (%d/%d):",
			ctx->comment, ctx->num_analog_channels +
			ctx->num_logic_channels, num_channels);
		for (l = channels; l; l = l->next) {
			ch = l->data;
			if (ch->enabled)
				g_string_append_printf(header, " %s,", ch->name);
		}
		if (channels) {
			/* Drop last separator. */
			g_string_truncate(header, header->len - 1);
		}
		g_string_append_printf(header, "\n");
		if (samplerate != 0) {
			samplerate_s = sr_samplerate_string(samplerate);
			g_string_append_printf(header, "%s Samplerate: %s\n",
					       ctx->comment, samplerate_s);
			g_free(samplerate_s);
		}
		ctx->did_header = TRUE;
	}

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
	struct sr_analog_meaning *meaning;
	GSList *l;
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
				sr_analog_unit_to_string(analog,
					&ctx->channels[idx_have].label);
			}
			for (idx_smpl = 0; idx_smpl < analog->num_samples; idx_smpl++)
				ctx->analog_samples[idx_smpl * ctx->num_analog_channels + idx_have] = fdata[idx_smpl * num_rcvd_ch + idx_rcvd];
			break;
		}
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
	float *analog_sample, value;
	uint8_t *logic_sample;

	/* If we haven't seen samples we're expecting, skip them. */
	if ((ctx->num_analog_channels && !ctx->analog_samples) ||
	    (ctx->num_logic_channels && !ctx->logic_samples)) {
		sr_warn("Discarding partial packet");
	} else {
		sr_info("Dumping %u samples", ctx->num_samples);

		*out = g_string_sized_new(512);
		num_channels =
		    ctx->num_logic_channels + ctx->num_analog_channels;

		if (ctx->label_do) {
			if (ctx->time)
				g_string_append_printf(*out, "%s%s",
					ctx->label_names ? "Time" :
					ctx->xlabel, ctx->value);
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
			ctx->previous_sample = g_malloc0(analog_size + ctx->num_logic_channels);

		for (i = 0; i < ctx->num_samples; i++) {
			ctx->sample_time += ctx->period;
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

			if (ctx->time)
				g_string_append_printf(*out, "%" PRIu64 "%s",
					ctx->sample_time, ctx->value);

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
	if (ctx->xlabel && ctx->time)
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
			ctx->time, i + 1 + ctx->time, ctx->scale ?
			max / ctx->channels[i].max : 1, ctx->channels[i].min);
		offset += 1.1 * (ctx->channels[i].max - ctx->channels[i].min);
	}
	g_string_truncate(script, script->len - 2);
	g_file_set_contents(ctx->gnuplot, script->str, script->len, NULL);
	g_string_free(script, TRUE);
}

static int receive(const struct sr_output *o,
		   const struct sr_datafeed_packet *packet, GString **out)
{
	struct context *ctx;

	*out = NULL;
	if (!o || !o->sdi)
		return SR_ERR_ARG;
	if (!(ctx = o->priv))
		return SR_ERR_ARG;

	sr_dbg("Got packet of type %d", packet->type);
	switch (packet->type) {
	case SR_DF_HEADER:
		*out = gen_header(o, packet->payload);
		break;
	case SR_DF_TRIGGER:
		ctx->trigger = TRUE;
		break;
	case SR_DF_LOGIC:
		process_logic(ctx, packet->payload);
		break;
	case SR_DF_ANALOG:
		process_analog(ctx, packet->payload);
		break;
	case SR_DF_FRAME_BEGIN:
		*out = g_string_new(ctx->frame);
		/* Fallthrough */
	case SR_DF_END:
		/* Got to end of frame/session with part of the data. */
		if (ctx->channels_seen)
			ctx->channels_seen = ctx->channel_count;
		if (*ctx->gnuplot)
			save_gnuplot(ctx);
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
	{"time", "Time column", "Output sample time as column 1", NULL, NULL},
	{"trigger", "Trigger column", "Output trigger indicator as last column ", NULL, NULL},
	{"dedup", "Dedup rows", "Set to false to output duplicate rows", NULL, NULL},
	ALL_ZERO
};

static const struct sr_option *get_options(void)
{
	if (!options[0].def) {
		options[0].def = g_variant_ref_sink(g_variant_new_string(""));
		options[1].def = g_variant_ref_sink(g_variant_new_boolean(TRUE));
		options[2].def = g_variant_ref_sink(g_variant_new_string(","));
		options[3].def = g_variant_ref_sink(g_variant_new_string("\n"));
		options[4].def = g_variant_ref_sink(g_variant_new_string("\n"));
		options[5].def = g_variant_ref_sink(g_variant_new_string(";"));
		options[6].def = g_variant_ref_sink(g_variant_new_boolean(TRUE));
		options[7].def = g_variant_ref_sink(g_variant_new_string("units"));
		options[8].def = g_variant_ref_sink(g_variant_new_boolean(TRUE));
		options[9].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[10].def = g_variant_ref_sink(g_variant_new_boolean(TRUE));
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
