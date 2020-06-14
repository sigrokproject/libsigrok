/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Gerhard Sittig <gerhard.sittig@gmx.net>
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
 * See the vendor's FAQ on file format details for exported files and
 * different software versions:
 *
 * https://support.saleae.com/faq/technical-faq/binary-data-export-format
 * https://support.saleae.com/faq/technical-faq/data-export-format-analog-binary
 * https://support.saleae.com/faq/technical-faq/binary-export-format-logic-2
 *
 * All data is in little endian representation, floating point values
 * in IEEE754 format. Recent versions add header information, while
 * previous versions tend to "raw" formats. This input module is about
 * digital and analog data in their "binary presentation". CSV and VCD
 * exports are handled by other input modules.
 *
 * Saleae Logic applications typically export one file per channel. The
 * sigrok input modules exclusively handle an individual file, existing
 * applications may not be prepared to handle a set of files, or handle
 * "special" file types like directories. Some of them will even actively
 * reject such input specs. Merging multiple exported channels into either
 * another input file or a sigrok session is supposed to be done outside
 * of this input module. Support for ZIP archives is currently missing.
 *
 * TODO
 * - Need to create a channel group in addition to channels?
 * - Check file re-load and channel references. See bug #1241.
 * - Fixup 'digits' use for analog data. The current implementation made
 *   an educated guess, assuming some 12bit resolution and logic levels
 *   which roughly results in the single digit mV range.
 * - Add support for "local I/O" in the input module when the common
 *   support code becomes available. The .sal save files of the Logic
 *   application appears to be a ZIP archive with *.bin files in it
 *   plus some meta.json dictionary. This will also introduce a new
 *   JSON reader dependency.
 * - When ZIP support gets added and .sal files become accepted, this
 *   import module needs to merge the content of several per-channel
 *   files, which may be of different types (mixed signal), and/or may
 *   even differ in their samplerate (which becomes complex, similar to
 *   VCD or CSV input). Given the .sal archive's layout this format may
 *   even only become attractive when common sigrok infrastructure has
 *   support for per-channel compression and rate(?).
 */

#include <config.h>
#include <glib.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "input/saleae"

/*
 * Saleae Logic "save files" (ZIP archives with .sal file extension)
 * could get detected, but are not yet supported. Usability would be
 * rather limited when the current development support gets enabled.
 * This compile time switch is strictly for internal developer use.
 */
#define SALEAE_WITH_SAL_SUPPORT 0

#define CHUNK_SIZE  (4 * 1024 * 1024)

#define LOGIC2_MAGIC "<SALEAE>"
#define LOGIC2_VERSION 0
#define LOGIC2_TYPE_DIGITAL 0
#define LOGIC2_TYPE_ANALOG 1

/* Simple header check approach. Assume minimum file size for all formats. */
#define LOGIC2_MIN_SIZE 0x30

enum logic_format {
	FMT_UNKNOWN,
	FMT_AUTO_DETECT,
	FMT_LOGIC1_DIGITAL,
	FMT_LOGIC1_ANALOG,
	FMT_LOGIC2_DIGITAL,
	FMT_LOGIC2_ANALOG,
	FMT_LOGIC2_ARCHIVE,
};

enum input_stage {
	STAGE_ALL_WAIT_HEADER,
	STAGE_ALL_DETECT_TYPE,
	STAGE_ALL_READ_HEADER,
	STAGE_L1D_EVERY_VALUE,
	STAGE_L1D_CHANGE_INIT,
	STAGE_L1D_CHANGE_VALUE,
	STAGE_L1A_NEW_CHANNEL,
	STAGE_L1A_SAMPLE,
	STAGE_L2D_CHANGE_VALUE,
	STAGE_L2A_FIRST_VALUE,
	STAGE_L2A_EVERY_VALUE,
};

struct context {
	struct context_options {
		enum logic_format format;
		gboolean when_changed;
		size_t word_size;
		size_t channel_count;
		uint64_t sample_rate;
	} options;
	struct {
		gboolean got_header;
		gboolean header_sent;
		gboolean rate_sent;
		GSList *prev_channels;
	} module_state;
	struct {
		enum logic_format format;
		gboolean when_changed;
		size_t word_size;
		size_t channel_count;
		uint64_t sample_rate;
		enum input_stage stage;
		struct {
			uint64_t samples_per_channel;
			uint64_t current_channel_idx;
			uint64_t current_per_channel;
		} l1a;
		struct {
			uint32_t init_state;
			double begin_time;
			double end_time;
			uint64_t transition_count;
			double sample_period;
			double min_time_step;
		} l2d;
		struct {
			double begin_time;
			uint64_t sample_rate;
			uint64_t down_sample;
			uint64_t sample_count;
		} l2a;
	} logic_state;
	struct {
		GSList *channels;
		gboolean is_analog;
		size_t unit_size;
		size_t samples_per_chunk;
		size_t samples_in_buffer;
		uint8_t *buffer_digital;
		float *buffer_analog;
		uint8_t *write_pos;
		struct {
			uint64_t stamp;
			double time;
			uint32_t digital;
			float analog;
		} last;
	} feed;
};

static const char *format_texts[] = {
	[FMT_UNKNOWN] = "unknown",
	[FMT_AUTO_DETECT] = "auto-detect",
	[FMT_LOGIC1_DIGITAL] = "logic1-digital",
	[FMT_LOGIC1_ANALOG] = "logic1-analog",
	[FMT_LOGIC2_DIGITAL] = "logic2-digital",
	[FMT_LOGIC2_ANALOG] = "logic2-analog",
#if SALEAE_WITH_SAL_SUPPORT
	[FMT_LOGIC2_ARCHIVE] = "logic2-archive",
#endif
};

static const char *get_format_text(enum logic_format fmt)
{
	const char *text;

	if (fmt >= ARRAY_SIZE(format_texts))
		return NULL;
	text = format_texts[fmt];
	if (!text || !*text)
		return NULL;
	return text;
}

static int create_channels(struct sr_input *in)
{
	struct context *inc;
	int type;
	size_t count, idx;
	char name[4];
	struct sr_channel *ch;

	inc = in->priv;

	if (in->sdi->channels)
		return SR_OK;

	count = inc->logic_state.channel_count;
	switch (inc->logic_state.format) {
	case FMT_LOGIC1_DIGITAL:
	case FMT_LOGIC2_DIGITAL:
		type = SR_CHANNEL_LOGIC;
		break;
	case FMT_LOGIC1_ANALOG:
	case FMT_LOGIC2_ANALOG:
		type = SR_CHANNEL_ANALOG;
		break;
	default:
		return SR_ERR_NA;
	}

	/* TODO Need to create a channel group? */
	for (idx = 0; idx < count; idx++) {
		snprintf(name, sizeof(name), "%zu", idx);
		ch = sr_channel_new(in->sdi, idx, type, TRUE, name);
		if (!ch)
			return SR_ERR_MALLOC;
	}

	return SR_OK;
}

static int alloc_feed_buffer(struct sr_input *in)
{
	struct context *inc;
	size_t alloc_size;

	inc = in->priv;

	inc->feed.is_analog = FALSE;
	alloc_size = CHUNK_SIZE;
	switch (inc->logic_state.format) {
	case FMT_LOGIC1_DIGITAL:
	case FMT_LOGIC2_DIGITAL:
		inc->feed.unit_size = sizeof(inc->feed.last.digital);
		alloc_size /= inc->feed.unit_size;
		inc->feed.samples_per_chunk = alloc_size;
		alloc_size *= inc->feed.unit_size;
		inc->feed.buffer_digital = g_try_malloc(alloc_size);
		if (!inc->feed.buffer_digital)
			return SR_ERR_MALLOC;
		inc->feed.write_pos = inc->feed.buffer_digital;
		break;
	case FMT_LOGIC1_ANALOG:
	case FMT_LOGIC2_ANALOG:
		inc->feed.is_analog = TRUE;
		alloc_size /= sizeof(inc->feed.last.analog);
		inc->feed.samples_per_chunk = alloc_size;
		alloc_size *= sizeof(inc->feed.last.analog);
		inc->feed.buffer_analog = g_try_malloc(alloc_size);
		if (!inc->feed.buffer_analog)
			return SR_ERR_MALLOC;
		inc->feed.write_pos = (void *)inc->feed.buffer_analog;
		break;
	default:
		return SR_ERR_NA;
	}
	inc->feed.samples_in_buffer = 0;

	return SR_OK;
}

static int relse_feed_buffer(struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;

	inc->feed.is_analog = FALSE;
	inc->feed.unit_size = 0;
	inc->feed.samples_per_chunk = 0;
	inc->feed.samples_in_buffer = 0;
	g_free(inc->feed.buffer_digital);
	inc->feed.buffer_digital = NULL;
	g_free(inc->feed.buffer_analog);
	inc->feed.buffer_analog = NULL;
	inc->feed.write_pos = NULL;

	return SR_OK;
}

static int setup_feed_buffer_channel(struct sr_input *in, size_t ch_idx)
{
	struct context *inc;
	struct sr_channel *ch;

	inc = in->priv;

	g_slist_free(inc->feed.channels);
	inc->feed.channels = NULL;
	if (ch_idx >= inc->logic_state.channel_count)
		return SR_OK;

	ch = g_slist_nth_data(in->sdi->channels, ch_idx);
	if (!ch)
		return SR_ERR_ARG;
	inc->feed.channels = g_slist_append(NULL, ch);
	return SR_OK;
}

static int flush_feed_buffer(struct sr_input *in)
{
	struct context *inc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	int rc;

	inc = in->priv;

	if (!inc->feed.samples_in_buffer)
		return SR_OK;

	/* Automatically send a datafeed header before meta and samples. */
	if (!inc->module_state.header_sent) {
		rc = std_session_send_df_header(in->sdi);
		if (rc)
			return rc;
		inc->module_state.header_sent = TRUE;
	}

	/* Automatically send the samplerate (when available). */
	if (inc->logic_state.sample_rate && !inc->module_state.rate_sent) {
		rc = sr_session_send_meta(in->sdi, SR_CONF_SAMPLERATE,
			g_variant_new_uint64(inc->logic_state.sample_rate));
		inc->module_state.rate_sent = TRUE;
	}

	/*
	 * Create a packet with either logic or analog payload. Rewind
	 * the caller's write position.
	 */
	memset(&packet, 0, sizeof(packet));
	if (inc->feed.is_analog) {
		/* TODO: Use proper 'digits' value for this input module. */
		sr_analog_init(&analog, &encoding, &meaning, &spec, 3);
		analog.data = inc->feed.buffer_analog;
		analog.num_samples = inc->feed.samples_in_buffer;
		analog.meaning->channels = inc->feed.channels;
		analog.meaning->mq = SR_MQ_VOLTAGE;
		analog.meaning->mqflags |= SR_MQFLAG_DC;
		analog.meaning->unit = SR_UNIT_VOLT;
		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		inc->feed.write_pos = (void *)inc->feed.buffer_analog;
	} else {
		memset(&logic, 0, sizeof(logic));
		logic.length = inc->feed.samples_in_buffer;
		logic.length *= inc->feed.unit_size;
		logic.unitsize = inc->feed.unit_size;
		logic.data = inc->feed.buffer_digital;
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		inc->feed.write_pos = inc->feed.buffer_digital;
	}
	inc->feed.samples_in_buffer = 0;

	/* Send the packet to the session feed. */
	return sr_session_send(in->sdi, &packet);
}

static int addto_feed_buffer_logic(struct sr_input *in,
	uint64_t data, size_t count)
{
	struct context *inc;

	inc = in->priv;

	if (inc->feed.is_analog)
		return SR_ERR_ARG;

	while (count--) {
		if (inc->feed.unit_size == sizeof(uint64_t))
			write_u64le_inc(&inc->feed.write_pos, data);
		else if (inc->feed.unit_size == sizeof(uint32_t))
			write_u32le_inc(&inc->feed.write_pos, data);
		else if (inc->feed.unit_size == sizeof(uint16_t))
			write_u16le_inc(&inc->feed.write_pos, data);
		else if (inc->feed.unit_size == sizeof(uint8_t))
			write_u8_inc(&inc->feed.write_pos, data);
		else
			return SR_ERR_BUG;
		inc->feed.samples_in_buffer++;
		if (inc->feed.samples_in_buffer == inc->feed.samples_per_chunk)
			flush_feed_buffer(in);
	}

	return SR_OK;
}

static int addto_feed_buffer_analog(struct sr_input *in,
	float data, size_t count)
{
	struct context *inc;

	inc = in->priv;

	if (!inc->feed.is_analog)
		return SR_ERR_ARG;

	while (count--) {
		if (sizeof(inc->feed.buffer_analog[0]) == sizeof(float))
			write_fltle_inc(&inc->feed.write_pos, data);
		else if (sizeof(inc->feed.buffer_analog[0]) == sizeof(double))
			write_dblle_inc(&inc->feed.write_pos, data);
		else
			return SR_ERR_BUG;
		inc->feed.samples_in_buffer++;
		if (inc->feed.samples_in_buffer == inc->feed.samples_per_chunk)
			flush_feed_buffer(in);
	}

	return SR_OK;
}

static enum logic_format check_format(const uint8_t *data, size_t dlen)
{
	const char *s;
	uint32_t v, t;

	/* TODO
	 * Can we check ZIP content here in useful ways? Probably only
	 * when the input module got extended to optionally handle local
	 * file I/O, and passes some archive handle to this routine.
	 */

	/* Check for the magic literal. */
	s = (void *)data;
	if (dlen < strlen(LOGIC2_MAGIC))
		return FMT_UNKNOWN;
	if (strncmp(s, LOGIC2_MAGIC, strlen(LOGIC2_MAGIC)) != 0)
		return FMT_UNKNOWN;
	data += strlen(LOGIC2_MAGIC);
	dlen -= strlen(LOGIC2_MAGIC);

	/* Get the version and type fields. */
	if (dlen < 2 * sizeof(uint32_t))
		return FMT_UNKNOWN;
	v = read_u32le_inc(&data);
	t = read_u32le_inc(&data);
	if (v != LOGIC2_VERSION)
		return FMT_UNKNOWN;
	switch (t) {
	case LOGIC2_TYPE_DIGITAL:
		return FMT_LOGIC2_DIGITAL;
	case LOGIC2_TYPE_ANALOG:
		return FMT_LOGIC2_ANALOG;
	default:
		return FMT_UNKNOWN;
	}

	return FMT_UNKNOWN;
}

/* Check for availability of required header data. */
static gboolean have_header(struct context *inc, GString *buf)
{

	/*
	 * The amount of required data depends on the file format. Which
	 * either was specified before, or is yet to get determined. The
	 * input module ideally would apply a sequence of checks for the
	 * currently available (partial) data, access a few first header
	 * fields, before checking for a little more receive data, before
	 * accessing more fields, until the input file's type was found,
	 * and its header length is known, and can get checked.
	 *
	 * This simple implementation just assumes that any input file
	 * has at least a given number of bytes, which should not be an
	 * issue for typical use cases. Only extremely short yet valid
	 * input files with just a few individual samples may fail this
	 * check. It's assumed that these files are very rare, and may
	 * be of types which are covered by other input modules (raw
	 * binary).
	 */
	(void)inc;
	return buf->len >= LOGIC2_MIN_SIZE;
}

/* Process/inspect previously received input data. Get header parameters. */
static int parse_header(struct sr_input *in)
{
	struct context *inc;
	const uint8_t *read_pos, *start_pos;
	size_t read_len, want_len;
	uint64_t samples_per_channel;
	size_t channel_count;
	double sample_period;
	uint64_t sample_rate;

	inc = in->priv;
	read_pos = (const uint8_t *)in->buf->str;
	read_len = in->buf->len;

	/*
	 * Clear internal state. Normalize user specified option values
	 * before amending them from the input file's header information.
	 */
	memset(&inc->logic_state, 0, sizeof(inc->logic_state));
	inc->logic_state.format = inc->options.format;
	inc->logic_state.when_changed = inc->options.when_changed;
	inc->logic_state.word_size = inc->options.word_size;
	if (!inc->logic_state.word_size) {
		sr_err("Need a word size.");
		return SR_ERR_ARG;
	}
	inc->logic_state.word_size += 8 - 1;
	inc->logic_state.word_size /= 8; /* Sample width in bytes. */
	if (inc->logic_state.word_size > sizeof(inc->feed.last.digital)) {
		sr_err("Excessive word size %zu.", inc->logic_state.word_size);
		return SR_ERR_ARG;
	}
	inc->logic_state.channel_count = inc->options.channel_count;
	inc->logic_state.sample_rate = inc->options.sample_rate;
	if (inc->logic_state.format == FMT_AUTO_DETECT)
		inc->logic_state.stage = STAGE_ALL_DETECT_TYPE;
	else
		inc->logic_state.stage = STAGE_ALL_READ_HEADER;

	/*
	 * Optionally auto-detect the format if none was specified yet.
	 * This only works for some of the supported formats. ZIP support
	 * requires local I/O in the input module (won't work on memory
	 * buffers).
	 */
	if (inc->logic_state.stage == STAGE_ALL_DETECT_TYPE) {
		inc->logic_state.format = check_format(read_pos, read_len);
		if (inc->logic_state.format == FMT_UNKNOWN) {
			sr_err("Unknown or unsupported file format.");
			return SR_ERR_DATA;
		}
		sr_info("Detected file format: '%s'.",
			get_format_text(inc->logic_state.format));
		inc->logic_state.stage = STAGE_ALL_READ_HEADER;
	}

	/*
	 * Read the header fields, depending on the specific file format.
	 * Arrange for the subsequent inspection of sample data items.
	 */
	start_pos = read_pos;
	switch (inc->logic_state.format) {
	case FMT_LOGIC1_DIGITAL:
		channel_count = inc->logic_state.channel_count;
		if (!channel_count) {
			channel_count = inc->logic_state.word_size;
			channel_count *= 8;
			inc->logic_state.channel_count = channel_count;
		}
		/* EMPTY */ /* No header fields to read here. */
		sr_dbg("L1D, empty header, changed %d.",
			inc->logic_state.when_changed ? 1 : 0);
		if (inc->logic_state.when_changed)
			inc->logic_state.stage = STAGE_L1D_CHANGE_INIT;
		else
			inc->logic_state.stage = STAGE_L1D_EVERY_VALUE;
		break;
	case FMT_LOGIC1_ANALOG:
		want_len = sizeof(uint64_t) + sizeof(uint32_t) + sizeof(double);
		if (read_len < want_len)
			return SR_ERR_DATA;
		samples_per_channel = read_u64le_inc(&read_pos);
		channel_count = read_u32le_inc(&read_pos);
		sample_period = read_dblle_inc(&read_pos);
		inc->logic_state.l1a.samples_per_channel = samples_per_channel;
		inc->logic_state.channel_count = channel_count;
		sample_rate = 0;
		if (sample_period) {
			sample_period = 1.0 / sample_period;
			sample_period += 0.5;
			sample_rate = (uint64_t)sample_period;
			inc->logic_state.sample_rate = sample_rate;
		}
		sr_dbg("L1A header, smpls %zu, chans %zu, per %lf, rate %zu.",
			(size_t)samples_per_channel, (size_t)channel_count,
			sample_period, (size_t)sample_rate);
		inc->logic_state.stage = STAGE_L1A_NEW_CHANNEL;
		inc->logic_state.l1a.current_channel_idx = 0;
		inc->logic_state.l1a.current_per_channel = 0;
		break;
	case FMT_LOGIC2_DIGITAL:
		inc->logic_state.channel_count = 1;
		want_len = sizeof(uint64_t); /* magic */
		want_len += 2 * sizeof(uint32_t); /* version, type */
		want_len += sizeof(uint32_t); /* initial state */
		want_len += 2 * sizeof(double); /* begin time, end time */
		want_len += sizeof(uint64_t); /* transition count */
		if (read_len < want_len)
			return SR_ERR_DATA;
		if (check_format(read_pos, read_len) != FMT_LOGIC2_DIGITAL)
			return SR_ERR_DATA;
		(void)read_u64le_inc(&read_pos);
		(void)read_u32le_inc(&read_pos);
		(void)read_u32le_inc(&read_pos);
		inc->logic_state.l2d.init_state = read_u32le_inc(&read_pos);
		inc->logic_state.l2d.begin_time = read_dblle_inc(&read_pos);
		inc->logic_state.l2d.end_time = read_dblle_inc(&read_pos);
		inc->logic_state.l2d.transition_count = read_u64le_inc(&read_pos);
		sr_dbg("L2D header, init %u, begin %lf, end %lf, transitions %" PRIu64 ".",
			(unsigned)inc->logic_state.l2d.init_state,
			inc->logic_state.l2d.begin_time,
			inc->logic_state.l2d.end_time,
			inc->logic_state.l2d.transition_count);
		if (!inc->logic_state.sample_rate) {
			sr_err("Need a samplerate.");
			return SR_ERR_ARG;
		}
		inc->feed.last.time = inc->logic_state.l2d.begin_time;
		inc->feed.last.digital = inc->logic_state.l2d.init_state ? 1 : 0;
		inc->logic_state.l2d.sample_period = inc->logic_state.sample_rate;
		inc->logic_state.l2d.sample_period = 1.0 / inc->logic_state.l2d.sample_period;
		inc->logic_state.l2d.min_time_step = inc->logic_state.l2d.end_time;
		inc->logic_state.l2d.min_time_step -= inc->logic_state.l2d.begin_time;
		inc->logic_state.stage = STAGE_L2D_CHANGE_VALUE;
		break;
	case FMT_LOGIC2_ANALOG:
		inc->logic_state.channel_count = 1;
		want_len = sizeof(uint64_t); /* magic */
		want_len += 2 * sizeof(uint32_t); /* version, type */
		want_len += sizeof(double); /* begin time */
		want_len += 2 * sizeof(uint64_t); /* sample rate, down sample */
		want_len += sizeof(uint64_t); /* sample count */
		if (read_len < want_len)
			return SR_ERR_DATA;
		if (check_format(read_pos, read_len) != FMT_LOGIC2_ANALOG)
			return SR_ERR_DATA;
		(void)read_u64le_inc(&read_pos);
		(void)read_u32le_inc(&read_pos);
		(void)read_u32le_inc(&read_pos);
		inc->logic_state.l2a.begin_time = read_dblle_inc(&read_pos);
		inc->logic_state.l2a.sample_rate = read_u64le_inc(&read_pos);
		inc->logic_state.l2a.down_sample = read_u64le_inc(&read_pos);
		inc->logic_state.l2a.sample_count = read_u64le_inc(&read_pos);
		if (!inc->logic_state.sample_rate)
			inc->logic_state.sample_rate = inc->logic_state.l2a.sample_rate;
		sr_dbg("L2A header, begin %lf, rate %" PRIu64 ", down %" PRIu64 ", samples %" PRIu64 ".",
			inc->logic_state.l2a.begin_time,
			inc->logic_state.l2a.sample_rate,
			inc->logic_state.l2a.down_sample,
			inc->logic_state.l2a.sample_count);
		inc->feed.last.time = inc->logic_state.l2a.begin_time;
		inc->logic_state.stage = STAGE_L2A_FIRST_VALUE;
		break;
	case FMT_LOGIC2_ARCHIVE:
		sr_err("Support for .sal archives not implemented yet.");
		return SR_ERR_NA;
	default:
		sr_err("Unknown or unsupported file format.");
		return SR_ERR_NA;
	}

	/* Remove the consumed header fields from the receive buffer. */
	read_len = read_pos - start_pos;
	g_string_erase(in->buf, 0, read_len);

	return SR_OK;
}

/* Check availablity of the next sample data item. */
static gboolean have_next_item(struct sr_input *in,
	const uint8_t *buff, size_t blen,
	const uint8_t **curr, const uint8_t **next)
{
	struct context *inc;
	size_t want_len;
	const uint8_t *pos;

	inc = in->priv;
	if (curr)
		*curr = NULL;
	if (next)
		*next = NULL;

	/*
	 * The amount of required data depends on the file format and
	 * the current state. Wait for the availabilty of the desired
	 * data before processing it (to simplify data inspection
	 * code paths).
	 */
	switch (inc->logic_state.stage) {
	case STAGE_L1D_EVERY_VALUE:
		want_len = inc->logic_state.word_size;
		break;
	case STAGE_L1D_CHANGE_INIT:
	case STAGE_L1D_CHANGE_VALUE:
		want_len = sizeof(uint64_t);
		want_len += inc->logic_state.word_size;
		break;
	case STAGE_L1A_NEW_CHANNEL:
		want_len = 0;
		break;
	case STAGE_L1A_SAMPLE:
		want_len = sizeof(float);
		break;
	case STAGE_L2D_CHANGE_VALUE:
		want_len = sizeof(double);
		break;
	case STAGE_L2A_FIRST_VALUE:
	case STAGE_L2A_EVERY_VALUE:
		want_len = sizeof(float);
		break;
	default:
		return FALSE;
	}
	if (blen < want_len)
		return FALSE;

	/* Provide references to the next item, and the position after it. */
	pos = buff;
	if (curr)
		*curr = pos;
	pos += want_len;
	if (next)
		*next = pos;
	return TRUE;
}

/* Process the next sample data item after it became available. */
static int parse_next_item(struct sr_input *in,
	const uint8_t *curr, size_t len)
{
	struct context *inc;
	uint64_t next_stamp, count;
	uint64_t digital;
	float analog;
	double next_time, diff_time;
	int rc;

	inc = in->priv;
	(void)len;

	/*
	 * The specific item to get processed next depends on the file
	 * format and current state.
	 */
	switch (inc->logic_state.stage) {
	case STAGE_L1D_CHANGE_INIT:
	case STAGE_L1D_CHANGE_VALUE:
		next_stamp = read_u64le_inc(&curr);
		if (inc->logic_state.stage == STAGE_L1D_CHANGE_INIT) {
			inc->feed.last.stamp = next_stamp;
			inc->logic_state.stage = STAGE_L1D_CHANGE_VALUE;
		}
		count = next_stamp - inc->feed.last.stamp;
		digital = inc->feed.last.digital;
		rc = addto_feed_buffer_logic(in, digital, count);
		if (rc)
			return rc;
		inc->feed.last.stamp = next_stamp - 1;
		/* FALLTHROUGH */
	case STAGE_L1D_EVERY_VALUE:
		if (inc->logic_state.word_size == sizeof(uint8_t)) {
			digital = read_u8_inc(&curr);
		} else if (inc->logic_state.word_size == sizeof(uint16_t)) {
			digital = read_u16le_inc(&curr);
		} else if (inc->logic_state.word_size == sizeof(uint32_t)) {
			digital = read_u32le_inc(&curr);
		} else if (inc->logic_state.word_size == sizeof(uint64_t)) {
			digital = read_u64le_inc(&curr);
		} else {
			/*
			 * In theory the sigrok input module could support
			 * arbitrary word sizes, but the Saleae exporter
			 * only provides the 8/16/32/64 choices anyway.
			 */
			sr_err("Unsupported word size %zu.", inc->logic_state.word_size);
			return SR_ERR_ARG;
		}
		rc = addto_feed_buffer_logic(in, digital, 1);
		if (rc)
			return rc;
		inc->feed.last.digital = digital;
		inc->feed.last.stamp++;
		return SR_OK;
	case STAGE_L1A_NEW_CHANNEL:
		/* Just select the channel. Don't consume any data. */
		rc = setup_feed_buffer_channel(in, inc->logic_state.l1a.current_channel_idx);
		if (rc)
			return rc;
		inc->logic_state.l1a.current_channel_idx++;
		inc->logic_state.l1a.current_per_channel = 0;
		inc->logic_state.stage = STAGE_L1A_SAMPLE;
		return SR_OK;
	case STAGE_L1A_SAMPLE:
		analog = read_fltle_inc(&curr);
		rc = addto_feed_buffer_analog(in, analog, 1);
		if (rc)
			return rc;
		inc->logic_state.l1a.current_per_channel++;
		if (inc->logic_state.l1a.current_channel_idx == inc->logic_state.l1a.samples_per_channel)
			inc->logic_state.stage = STAGE_L1A_NEW_CHANNEL;
		return SR_OK;
	case STAGE_L2D_CHANGE_VALUE:
		next_time = read_dblle_inc(&curr);
		diff_time = next_time - inc->feed.last.time;
		if (inc->logic_state.l2d.min_time_step > diff_time)
			inc->logic_state.l2d.min_time_step = diff_time;
		diff_time /= inc->logic_state.l2d.sample_period;
		diff_time += 0.5;
		count = (uint64_t)diff_time;
		digital = inc->feed.last.digital;
		rc = addto_feed_buffer_logic(in, digital, count);
		if (rc)
			return rc;
		inc->feed.last.time = next_time;
		inc->feed.last.digital = 1 - inc->feed.last.digital;
		return SR_OK;
	case STAGE_L2A_FIRST_VALUE:
	case STAGE_L2A_EVERY_VALUE:
		analog = read_fltle_inc(&curr);
		if (inc->logic_state.stage == STAGE_L2A_FIRST_VALUE) {
			rc = setup_feed_buffer_channel(in, 0);
			if (rc)
				return rc;
			count = 1;
		} else {
			count = inc->logic_state.l2a.down_sample;
		}
		rc = addto_feed_buffer_analog(in, analog, 1);
		if (rc)
			return rc;
		return SR_OK;

	default:
		(void)analog;
		return SR_ERR_NA;
	}
	/* UNREACH */
}

static int parse_samples(struct sr_input *in)
{
	const uint8_t *buff, *start;
	size_t blen;

	const uint8_t *curr, *next;
	size_t len;
	int rc;

	start = (const uint8_t *)in->buf->str;
	buff = start;
	blen = in->buf->len;
	while (have_next_item(in, buff, blen, &curr, &next)) {
		len = next - curr;
		rc = parse_next_item(in, curr, len);
		if (rc)
			return rc;
		buff += len;
		blen -= len;
	}
	len = buff - start;
	g_string_erase(in->buf, 0, len);

	return SR_OK;
}

/*
 * Try to auto detect an input's file format. Mismatch is non-fatal.
 * Silent operation by design. Not all details need to be available.
 * Get the strongest possible match in a best-effort manner.
 *
 * TODO Extend the .sal check when local file I/O becomes available.
 * File extensions can lie, and need not be available. Check for a
 * ZIP archive and the meta.json member in it.
 */
static int format_match(GHashTable *metadata, unsigned int *confidence)
{
	static const char *zip_ext = ".sal";
	static const char *bin_ext = ".bin";

	gboolean matched;
	const char *fn;
	size_t fn_len, ext_len;
	const char *ext_pos;
	GString *buf;

	matched = FALSE;

	/* Weak match on the filename (when available). */
	fn = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_FILENAME));
	if (fn && *fn) {
		fn_len = strlen(fn);
		ext_len = strlen(zip_ext);
		ext_pos = &fn[fn_len - ext_len];
		if (fn_len >= ext_len && g_ascii_strcasecmp(ext_pos, zip_ext) == 0) {
			if (SALEAE_WITH_SAL_SUPPORT) {
				*confidence = 10;
				matched = TRUE;
			}
		}
		ext_len = strlen(bin_ext);
		ext_pos = &fn[fn_len - ext_len];
		if (fn_len >= ext_len && g_ascii_strcasecmp(ext_pos, bin_ext) == 0) {
			*confidence = 50;
			matched = TRUE;
		}
	}

	/* Stronger match when magic literals are found in file content. */
	buf = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_HEADER));
	if (!buf || !buf->len || !buf->str)
		return SR_ERR_ARG;
	switch (check_format((const uint8_t *)buf->str, buf->len)) {
	case FMT_LOGIC2_DIGITAL:
	case FMT_LOGIC2_ANALOG:
		*confidence = 1;
		matched = TRUE;
		break;
	default:
		/* EMPTY */
		break;
	}

	return matched ? SR_OK : SR_ERR_DATA;
}

static int init(struct sr_input *in, GHashTable *options)
{
	struct context *inc;
	const char *type, *fmt_text;
	enum logic_format format, fmt_idx;
	gboolean changed;
	size_t size, count;
	uint64_t rate;

	/* Allocate resources. */
	in->sdi = g_malloc0(sizeof(*in->sdi));
	inc = g_malloc0(sizeof(*inc));
	in->priv = inc;

	/* Get caller provided specs, dump before check. */
	type = g_variant_get_string(g_hash_table_lookup(options, "format"), NULL);
	changed = g_variant_get_boolean(g_hash_table_lookup(options, "changed"));
	size = g_variant_get_uint32(g_hash_table_lookup(options, "wordsize"));
	count = g_variant_get_uint32(g_hash_table_lookup(options, "logic_channels"));
	rate = g_variant_get_uint64(g_hash_table_lookup(options, "samplerate"));
	sr_dbg("Caller options: type '%s', changed %d, wordsize %zu, channels %zu, rate %" PRIu64 ".",
		type, changed ? 1 : 0, size, count, rate);

	/* Run a few simple checks. Normalization is done in .init(). */
	format = FMT_UNKNOWN;
	for (fmt_idx = FMT_AUTO_DETECT; fmt_idx < ARRAY_SIZE(format_texts); fmt_idx++) {
		fmt_text = format_texts[fmt_idx];
		if (!fmt_text || !*fmt_text)
			continue;
		if (g_ascii_strcasecmp(type, fmt_text) != 0)
			continue;
		format = fmt_idx;
		break;
	}
	if (format == FMT_UNKNOWN) {
		sr_err("Unknown file type name: '%s'.", type);
		return SR_ERR_ARG;
	}
	if (!size) {
		sr_err("Need a word size.");
		return SR_ERR_ARG;
	}

	/*
	 * Keep input specs around. We never get back to .init() even
	 * when input files are re-read later.
	 */
	inc->options.format = format;
	inc->options.when_changed = !!changed;
	inc->options.word_size = size;
	inc->options.channel_count = count;
	inc->options.sample_rate = rate;
	sr_dbg("Resulting options: type '%s', changed %d",
		get_format_text(format), changed ? 1 : 0);

	return SR_OK;
}

static int receive(struct sr_input *in, GString *buf)
{
	struct context *inc;
	int rc;
	const char *text;

	inc = in->priv;

	/* Accumulate another chunk of input data. */
	g_string_append_len(in->buf, buf->str, buf->len);

	/*
	 * Wait for the full header's availability, then process it in
	 * a single call, and set the "ready" flag. Make sure sample data
	 * and the header get processed in disjoint receive() calls, the
	 * backend requires those separate phases.
	 */
	if (!inc->module_state.got_header) {
		if (!have_header(inc, in->buf))
			return SR_OK;
		rc = parse_header(in);
		if (rc)
			return rc;
		inc->module_state.got_header = TRUE;
		text = get_format_text(inc->logic_state.format) ? : "<unknown>";
		sr_info("Using file format: '%s'.", text);
		rc = create_channels(in);
		if (rc)
			return rc;
		rc = alloc_feed_buffer(in);
		if (rc)
			return rc;
		in->sdi_ready = TRUE;
		return SR_OK;
	}

	/* Process sample data, after the header got processed. */
	return parse_samples(in);
}

static int end(struct sr_input *in)
{
	struct context *inc;
	int rc;

	/* Nothing to do here if we never started feeding the session. */
	if (!in->sdi_ready)
		return SR_OK;

	/*
	 * Process input data which may not have been inspected before.
	 * Flush any potentially queued samples.
	 */
	rc = parse_samples(in);
	if (rc)
		return rc;
	rc = flush_feed_buffer(in);
	if (rc)
		return rc;

	/* End the session feed if one was started. */
	inc = in->priv;
	if (inc->module_state.header_sent) {
		rc = std_session_send_df_end(in->sdi);
		if (rc)
			return rc;
		inc->module_state.header_sent = FALSE;
	}

	/* Input data shall be exhausted by now. Non-fatal condition. */
	if (in->buf->len)
		sr_warn("Unprocessed remaining input: %zu bytes.", in->buf->len);

	return SR_OK;
}

static void cleanup(struct sr_input *in)
{
	struct context *inc;
	struct context_options save_opts;

	if (!in)
		return;
	inc = in->priv;
	if (!inc)
		return;

	/* Keep references to previously created channels. */
	g_slist_free_full(inc->module_state.prev_channels, sr_channel_free_cb);
	inc->module_state.prev_channels = in->sdi->channels;
	in->sdi->channels = NULL;

	/* Release dynamically allocated resources. */
	relse_feed_buffer(in);

	/* Clear internal state, but keep what .init() has provided. */
	save_opts = inc->options;
	memset(inc, 0, sizeof(*inc));
	inc->options = save_opts;
}

static int reset(struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;

	/*
	 * The input module's .reset() routine clears the 'inc' context.
	 * But 'in' is kept which contains channel groups which reference
	 * channels. We cannot re-create the channels, since applications
	 * still reference them and expect us to keep them. The .cleanup()
	 * routine also keeps the user specified option values, the module
	 * will derive internal state again when the input gets re-read.
	 */
	cleanup(in);
	in->sdi->channels = inc->module_state.prev_channels;

	inc->module_state.got_header = FALSE;
	inc->module_state.header_sent = FALSE;
	inc->module_state.rate_sent = FALSE;
	g_string_truncate(in->buf, 0);

	return SR_OK;
}

enum option_index {
	OPT_FMT_TYPE,
	OPT_CHANGE,
	OPT_WORD_SIZE,
	OPT_NUM_LOGIC,
	OPT_SAMPLERATE,
	OPT_MAX,
};

static struct sr_option options[] = {
	[OPT_FMT_TYPE] = {
		"format", "File format.",
		"Type of input file format. Not all types can get auto-detected.",
		NULL, NULL,
	},
	[OPT_CHANGE] = {
		"changed", "Save when changed.",
		"Sample value was saved when changed (in contrast to: every sample).",
		NULL, NULL,
	},
	[OPT_WORD_SIZE] = {
		"wordsize", "Word size.",
		"The number of bits per set of samples for digital data.",
		NULL, NULL,
	},
	[OPT_NUM_LOGIC] = {
		"logic_channels", "Channel count.",
		"The number of digital channels. Word size is used when not specified.",
		NULL, NULL,
	},
	[OPT_SAMPLERATE] = {
		"samplerate", "Samplerate.",
		"The samplerate. Needed when the file content lacks this information.",
		NULL, NULL,
	},
	[OPT_MAX] = ALL_ZERO,
};

static const struct sr_option *get_options(void)
{
	enum logic_format fmt_idx;
	const char *fmt_text;
	size_t word_size;
	GSList *l;

	/* Been here before? Already assigned default values? */
	if (options[0].def)
		return options;

	/* Assign default values, and list choices to select from. */
	fmt_text = format_texts[FMT_AUTO_DETECT];
	options[OPT_FMT_TYPE].def = g_variant_ref_sink(g_variant_new_string(fmt_text));
	l = NULL;
	for (fmt_idx = FMT_AUTO_DETECT; fmt_idx < ARRAY_SIZE(format_texts); fmt_idx++) {
		fmt_text = format_texts[fmt_idx];
		if (!fmt_text || !*fmt_text)
			continue;
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_string(fmt_text)));
	}
	options[OPT_FMT_TYPE].values = l;
	options[OPT_CHANGE].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
	options[OPT_WORD_SIZE].def = g_variant_ref_sink(g_variant_new_uint32(8));
	l = NULL;
	for (word_size = sizeof(uint8_t); word_size <= sizeof(uint64_t); word_size *= 2)
		l = g_slist_append(l, g_variant_ref_sink(g_variant_new_uint32(8 * word_size)));
	options[OPT_WORD_SIZE].values = l;
	options[OPT_NUM_LOGIC].def = g_variant_ref_sink(g_variant_new_uint32(0));
	options[OPT_SAMPLERATE].def = g_variant_ref_sink(g_variant_new_uint64(0));

	return options;
}

SR_PRIV struct sr_input_module input_saleae = {
	.id = "saleae",
	.name = "Saleae",
#if SALEAE_WITH_SAL_SUPPORT
	.desc = "Saleae Logic software export/save files",
	.exts = (const char *[]){"bin", "sal", NULL},
#else
	.desc = "Saleae Logic software export files",
	.exts = (const char *[]){"bin", NULL},
#endif
	.metadata = {
		SR_INPUT_META_FILENAME,
		SR_INPUT_META_HEADER | SR_INPUT_META_REQUIRED
	},
	.options = get_options,
	.format_match = format_match,
	.init = init,
	.receive = receive,
	.end = end,
	.cleanup = cleanup,
	.reset = reset,
};
