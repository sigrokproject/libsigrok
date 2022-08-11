/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017-2021 Gerhard Sittig <gerhard.sittig@gmx.net>
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

/*
 * The STF input module supports reading "Sigma Test File" archives
 * which are created by the vendor application for Asix Sigma and Omega
 * devices. See the "SIGMAP01 - Reading STF File" Application Note for
 * details on the file format. Example data is available at the Asix
 * web site.
 *
 * http://asix.net/download/analyzers/sigmap01_reading_stf_file.pdf
 * http://asix.net/dwnld_sigma-omega_examples.htm
 *
 * TODO
 * - The current implementation only supports Sigma files. Support for
 *   Omega files is currently missing. The ZIP compressed input data
 *   requires local file I/O in the input module, which currently is
 *   not available in common infrastructure.
 * - The current implementation assumes 1-bit trace width, and accepts
 *   'Input' traces exclusively. No pseudo or decoder traces will be
 *   imported, neither are multi-bit wide traces supported ('Bus').
 * - This implementation derives the session feed unit size from the
 *   set of enabled channels, but assumes an upper limit of 16 channels
 *   total. Which is sufficient for Sigma, but may no longer be when a
 *   future version implements support for chained Omega devices. When
 *   the Omega chain length is limited (the AppNote suggests up to 256
 *   channels, the user manual lacks specs for synchronization skew
 *   beyond three Omega devices in a chain), we still might get away
 *   with simple integer variables, and need not switch to arbitrary
 *   length byte fields.
 * - The current implementation merely extracts the signal data from
 *   the archive (bit patterns, and their sample rate). Other information
 *   that may be available in the 'Settings' section is discarded (decoder
 *   configuration, assigned colours for traces, etc). This is acceptable
 *   because none of these details can get communicated to the session
 *   feed in useful ways.
 * - The STF file format involves the lzo1x method for data compression,
 *   see http://www.oberhumer.com/opensource/lzo/ for the project page.
 *   The vendor explicitly references the miniLZO library (under GPLv2+
 *   license). A future implementation might switch to a different lib
 *   which provides support to uncompress lzo1x content, which would
 *   eliminate the miniLZO dependency.
 * - Re-check the trigger marker position's correctness. It may be off
 *   in the current implementation when the file's first valid timestamp
 *   does not align with a cluster.
 */

/*
 * Implementor's notes on the input data:
 * - The input file contains: A magic literal for robust file type
 *   identification, a "header" section, and a "data" section. The
 *   input data either resides in a regular file (Sigma), or in a
 *   ZIP archive (Omega). Some of the Sigma file payload is LZO1x
 *   compressed, for Omega files ZIP's deflate is transparent.
 * - The textual header section either ends at its EOF (Omega) or is
 *   terminated by NUL (Sigma). Header lines are CR/LF terminated
 *   key=value pairs, where values can be semicolon separated lists
 *   of colon separated key=value pairs to form deeper nestings for
 *   complex settings. Unknown keys are non-fatal, their presence
 *   depends on the system including plugins. All numbers in the
 *   header section are kept in textual format, typically decimal.
 * - The (Sigma specific?) data section consists of "records" which
 *   have two u32 fields (length and checksum) followed by up to
 *   1MiB of compressed data. The last record has length -1 and a
 *   checksum value 0. The data is LZO1x compressed and decompresses
 *   to up to 1MiB. This 1MiB payload contains a number of chunks of
 *   1440 bytes length. Each chunk has 32 bytes information and 64
 *   clusters each, and a cluster has one 64bit timestamp and 7 16bit
 *   sample data items. A 16bit sample data item can carry 1 to 4
 *   sample sets, depending on the capture's samplerate. A record's
 *   content concentrates the chunks' info and the timestamps and the
 *   samples next to each other so that compression can take greater
 *   effect.
 * - The Omega specific data layout differs from Sigma, comes in
 *   different formats (streamable, legacy), and is kept in several
 *   ZIP member files. Omega Test Files are currently not covered by
 *   this sigrok input module.
 * - All numbers in binary data are kept in little endian format.
 * - All TS count in the units which correspond to the 16bit sample
 *   items in raw memory. When these 16bit items carry multiple 8bit
 *   or 4bit sample sets, the TS still counts them as one step.
 */

#include <config.h>

#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <libsigrok-internal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <zlib.h>

#include "minilzo/minilzo.h"

#define LOG_PREFIX "input/stf"

/*
 * Magic string literals which correspond to the file formats. Each
 * literal consists of 15 printables and the terminating NUL character.
 * Header lines are terminated by CRLF.
 */
#define STF_MAGIC_LENGTH	16
#define STF_MAGIC_SIGMA		"Sigma Test File"
#define STF_MAGIC_OMEGA		"Omega Test File"
#define STF_HEADER_EOL		"\r\n"

/*
 * Sample period is specified in "PU" units, where 15015 counts translate
 * to a period of 1ns. A value of 15016 signals the absence of a known
 * sample rate (externally clocked acquisition, timing unknown).
 */
#define CLK_TIME_PU_PER1NS	15015
#define CLK_TIME_PU_UNKNOWN	15016

/*
 * Data is organized in records, with up to 1MiB payload data that is
 * preceeded by two 32bit header fields.
 */
#define STF_DATA_REC_HDRLEN	(2 * sizeof(uint32_t))
#define STF_DATA_REC_PLMAX	(1 * 1024 * 1024)

/*
 * Accumulate chunks of sample data before submission to the session feed.
 */
#define CHUNKSIZE		(4 * 1024 * 1024)

/*
 * A chunk is associated with 32 bytes of information, and contains
 * 64 clusters with one 64bit timestamp and 7 sample data items of
 * 16bit width each. Which results in a chunk size of 1440 bytes. A
 * records contains several of these chunks (up to 1MiB total size).
 */
#define STF_CHUNK_TOTAL_SIZE	1440
#define STF_CHUNK_CLUSTER_COUNT	64
#define STF_CHUNK_INFO_SIZE	32
#define STF_CHUNK_STAMP_SIZE	8
#define STF_CHUNK_SAMPLE_SIZE	14

struct context {
	enum stf_stage {
		STF_STAGE_MAGIC,
		STF_STAGE_HEADER,
		STF_STAGE_DATA,
		STF_STAGE_DONE,
	} file_stage;
	enum stf_format {
		STF_FORMAT_NONE,
		STF_FORMAT_SIGMA,
		STF_FORMAT_OMEGA,
	} file_format;
	gboolean header_sent;
	size_t channel_count;
	GSList *channels;
	struct {
		uint64_t first_ts;	/* First valid timestamp in the file. */
		uint64_t length_ts;	/* Last valid timestamp. */
		uint64_t trigger_ts;	/* Timestamp of trigger position. */
		uint64_t clk_pu;	/* Clock period, in PU units. */
		uint64_t clk_div;	/* Clock divider (when 50MHz). */
		char **sigma_clksrc;	/* ClockSource specs (50/100/200MHz). */
		char **sigma_inputs;	/* Input pin names. */
		size_t input_count;
		char **trace_specs;	/* Colon separated Trace description. */
		time_t c_date_time;	/* File creation time (Unix epoch). */
		char *omega_data_class;	/* Chunked or streamed, Omega only. */
	} header;
	struct stf_record {
		size_t len;		/* Payload length. */
		uint32_t crc;		/* Payload checksum. */
		uint8_t raw[STF_DATA_REC_PLMAX];	/* Payload data. */
	} record_data;
	struct keep_specs {
		uint64_t sample_rate;
		GSList *prev_sr_channels;
	} keep;
	struct {
		uint64_t sample_rate;	/* User specified or from header. */
		uint64_t sample_count;	/* Samples count as per header. */
		uint64_t submit_count;	/* Samples count submitted so far. */
		uint64_t samples_to_trigger;	/* Samples until trigger pos. */
		uint64_t last_submit_ts;	/* Last submitted timestamp. */
		size_t bits_per_sample;	/* 1x 16, 2x 8, or 4x 4 per 16bit. */
		size_t unit_size;
		uint16_t curr_data;	/* Current sample data. */
		struct feed_queue_logic *feed;	/* Session feed helper. */
	} submit;
};

static void keep_header_for_reread(const struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;

	g_slist_free_full(inc->keep.prev_sr_channels, sr_channel_free_cb);
	inc->keep.prev_sr_channels = in->sdi->channels;
	in->sdi->channels = NULL;
}

static gboolean check_header_in_reread(const struct sr_input *in)
{
	struct context *inc;
	GSList *prev, *curr;

	if (!in)
		return FALSE;
	inc = in->priv;
	if (!inc)
		return FALSE;
	if (!inc->keep.prev_sr_channels)
		return TRUE;

	prev = inc->keep.prev_sr_channels;
	curr = in->sdi->channels;
	if (sr_channel_lists_differ(prev, curr)) {
		sr_err("Channel list change not supported for file re-read.");
		return FALSE;
	}

	g_slist_free_full(curr, sr_channel_free_cb);
	in->sdi->channels = prev;
	inc->keep.prev_sr_channels = NULL;

	return TRUE;
}

struct stf_channel {
	char *name;
	size_t input_id;	/* Index in the Sigma inputs list. */
	size_t src_bitpos;	/* Bit position in the input file. */
	uint16_t src_bitmask;	/* Resulting input bit mask. */
	size_t dst_bitpos;	/* Bit position in the datafeed image. */
	uint16_t dst_bitmask;	/* Resulting datafeed bit mask. */
};

static void free_channel(void *data)
{
	struct stf_channel *ch;

	ch = data;
	g_free(ch->name);
	g_free(ch);
}

static int add_channel(const struct sr_input *in, char *name, size_t input_id)
{
	struct context *inc;
	char *perc;
	uint8_t conv_value;
	struct stf_channel *stf_ch;

	inc = in->priv;
	sr_dbg("Header: Adding channel, idx %zu, name %s, ID %zu.",
		inc->channel_count, name, input_id);

	/*
	 * Use Sigma pin names in the absence of user assigned
	 * GUI labels for traces.
	 */
	if (!name || !*name) {
		if (!inc->header.sigma_inputs)
			return SR_ERR_DATA;
		if (input_id >= inc->header.input_count)
			return SR_ERR_DATA;
		name = inc->header.sigma_inputs[input_id];
		if (!name || !*name)
			return SR_ERR_DATA;
	}

	/*
	 * Undo '%xx' style escapes in channel names. Failed conversion
	 * is non-fatal, the (non convertible part of the) channel name
	 * just won't get translated. No rollback is attempted. It's a
	 * mere cosmetics issue when input data is unexpected.
	 */
	perc = name;
	while ((perc = strchr(perc, '%')) != NULL) {
		if (!g_ascii_isxdigit(perc[1]) || !g_ascii_isxdigit(perc[2])) {
			sr_warn("Could not unescape channel name '%s'.", name);
			break;
		}
		conv_value = 0;
		conv_value <<= 4;
		conv_value |= g_ascii_xdigit_value(perc[1]);
		conv_value <<= 4;
		conv_value |= g_ascii_xdigit_value(perc[2]);
		perc[0] = conv_value;
		memmove(&perc[1], &perc[3], strlen(&perc[3]) + 1);
		perc = &perc[1];
	}

	stf_ch = g_malloc0(sizeof(*stf_ch));
	stf_ch->name = g_strdup(name);
	stf_ch->input_id = input_id;
	stf_ch->src_bitpos = input_id;
	stf_ch->src_bitmask = 1U << stf_ch->src_bitpos;
	stf_ch->dst_bitpos = inc->channel_count;
	stf_ch->dst_bitmask = 1U << stf_ch->dst_bitpos;
	inc->channels = g_slist_append(inc->channels, stf_ch);

	sr_channel_new(in->sdi, inc->channel_count,
		SR_CHANNEL_LOGIC, TRUE, name);
	inc->channel_count++;

	return SR_OK;
}

/* End of header was seen. Postprocess previously accumulated data. */
static int eval_header(const struct sr_input *in)
{
	struct context *inc;
	uint64_t scale, large_num, p, q;
	char num_txt[24];
	int rc;
	size_t spec_idx, item_idx;
	char *spec, **items, *item, *sep;
	int scheme, period;
	char *type, *name, *id;
	gboolean is_input;

	inc = in->priv;

	/*
	 * Count the number of Sigma input pin names. This simplifies
	 * the name assignment logic in another location.
	 */
	if (!inc->header.sigma_inputs) {
		sr_err("Header: 'Inputs' information missing.");
		return SR_ERR_DATA;
	}
	inc->header.input_count = g_strv_length(inc->header.sigma_inputs);

	/*
	 * Derive the total sample count from the first/last timestamps,
	 * and determine the distance to an (optional) trigger location.
	 * Ignore out-of-range trigger positions (we have seen them in
	 * Sigma USB example captures).
	 */
	inc->submit.sample_count = inc->header.length_ts + 1;
	inc->submit.sample_count -= inc->header.first_ts;
	sr_dbg("Header: TS first %" PRIu64 ", last %" PRIu64 ", count %" PRIu64 ".",
		inc->header.first_ts, inc->header.length_ts,
		inc->submit.sample_count);
	if (inc->header.trigger_ts) {
		if (inc->header.trigger_ts < inc->header.first_ts)
			inc->header.trigger_ts = 0;
		if (inc->header.trigger_ts > inc->header.length_ts)
			inc->header.trigger_ts = 0;
		if (!inc->header.trigger_ts)
			sr_dbg("Header: ignoring out-of-range trigger TS.");
	}
	if (inc->header.trigger_ts) {
		inc->submit.samples_to_trigger = inc->header.trigger_ts;
		inc->submit.samples_to_trigger -= inc->header.first_ts;
		sr_dbg("Header: TS trigger %" PRIu64 ", samples to trigger %" PRIu64 ".",
			inc->header.trigger_ts, inc->submit.samples_to_trigger);
	}

	/*
	 * Inspect the ClockSource/ClockScheme header fields. Memory
	 * layout of sample data differs for 50/100/200MHz rates. As
	 * does the clock period calculation for some configurations.
	 * TestCLKTime specs only are applicable to externally clocked
	 * acquisition which gets tracked internally. 200/100MHz modes
	 * use fixed sample rates, as does 50MHz mode which supports
	 * an extra divider.
	 */
	if (!inc->header.sigma_clksrc) {
		sr_err("Header: Failed to parse 'ClockSource' information.");
		return SR_ERR_DATA;
	}
	scheme = -1;
	period = 1;
	for (spec_idx = 0; inc->header.sigma_clksrc[spec_idx]; spec_idx++) {
		spec = inc->header.sigma_clksrc[spec_idx];
		sep = strchr(spec, '=');
		if (!sep)
			continue;
		*sep++ = '\0';
		if (strcmp(spec, "ClockScheme") == 0) {
			scheme = strtoul(sep, NULL, 0);
		}
		if (strcmp(spec, "Period") == 0) {
			period = strtoul(sep, NULL, 0);
		}
	}
	if (scheme < 0) {
		sr_err("Header: Unsupported 'ClockSource' detail.");
		return SR_ERR_DATA;
	}
	sr_dbg("Header: ClockScheme %d, Period %d.", scheme, period);
	switch (scheme) {
	case 0:	/* 50MHz, 1x 16bits per sample, 20ns period and divider. */
		inc->header.clk_div = period;
		inc->header.clk_pu = 20 * CLK_TIME_PU_PER1NS;
		inc->header.clk_pu *= inc->header.clk_div;
		inc->submit.bits_per_sample = 16;
		break;
	case 1:	/* 100MHz, 2x 8bits per sample, 10ns period. */
		inc->header.clk_pu = 10 * CLK_TIME_PU_PER1NS;
		inc->submit.bits_per_sample = 8;
		scale = 16 / inc->submit.bits_per_sample;
		inc->submit.sample_count *= scale;
		sr_dbg("Header: 100MHz -> 2x sample count: %" PRIu64 ".",
			inc->submit.sample_count);
		inc->submit.samples_to_trigger *= scale;
		break;
	case 2:	/* 200MHz, 4x 4bits per sample, 5ns period. */
		inc->header.clk_pu = 5 * CLK_TIME_PU_PER1NS;
		inc->submit.bits_per_sample = 4;
		scale = 16 / inc->submit.bits_per_sample;
		inc->submit.sample_count *= scale;
		sr_dbg("Header: 200MHz -> 4x sample count: %" PRIu64 ".",
			inc->submit.sample_count);
		inc->submit.samples_to_trigger *= scale;
		break;
	default: /* "Async", not implemented. */
		sr_err("Header: Unsupported 'ClockSource' detail.");
		return SR_ERR_NA;
	}

	/*
	 * Prefer the externally provided samplerate when specified by
	 * the user. Use the input file's samplerate otherwise (when
	 * available and plausible).
	 *
	 * Highest sample rate is 50MHz which translates to 20ns period.
	 * We don't expect "odd" numbers that are not a multiple of 1ns.
	 * Special acquisition modes can provide data at 100MHz/200MHz
	 * rates, which still results in full 5ns periods.
	 * The detour via text buffer and parse routine is rather easy
	 * to verify, and leaves complex arith in common support code.
	 */
	do {
		inc->submit.sample_rate = inc->keep.sample_rate;
		if (inc->submit.sample_rate) {
			sr_dbg("Header: rate %" PRIu64 " (user).",
				inc->submit.sample_rate);
			break;
		}
		large_num = inc->header.clk_pu;
		if (!large_num)
			break;
		if (large_num == CLK_TIME_PU_UNKNOWN)
			break;
		large_num /= CLK_TIME_PU_PER1NS;
		snprintf(num_txt, sizeof(num_txt), "%" PRIu64 "ns", large_num);
		rc = sr_parse_period(num_txt, &p, &q);
		if (rc != SR_OK)
			return rc;
		inc->submit.sample_rate = q / p;
		sr_dbg("Header: period %s -> rate %" PRIu64 " (calc).",
			num_txt, inc->submit.sample_rate);
	} while (0);

	/*
	 * Scan "Trace" specs, filter for 'Input' types, determine
	 * trace names from input ID and Sigma input names.
	 *
	 * TODO Also support 'Bus' types which involve more 'Input<n>'
	 * references.
	 */
	if (!inc->header.trace_specs) {
		sr_err("Header: Failed to parse 'Trace' information.");
		return SR_ERR_DATA;
	}
	for (spec_idx = 0; inc->header.trace_specs[spec_idx]; spec_idx++) {
		spec = inc->header.trace_specs[spec_idx];
		items = g_strsplit_set(spec, ":", 0);
		type = name = id = NULL;
		for (item_idx = 0; items[item_idx]; item_idx++) {
			item = items[item_idx];
			sep = strchr(item, '=');
			if (!sep)
				continue;
			*sep++ = '\0';
			if (strcmp(item, "Type") == 0) {
				type = sep;
			} else if (strcmp(item, "Caption") == 0) {
				name = sep;
			} else if (strcmp(item, "Input0") == 0) {
				id = sep;
			}
		}
		if (!type) {
			g_strfreev(items);
			continue;
		}
		is_input = strcmp(type, "Input") == 0;
		is_input |= strcmp(type, "Digital") == 0;
		if (!is_input) {
			g_strfreev(items);
			continue;
		}
		if (!id || !*id) {
			g_strfreev(items);
			continue;
		}
		rc = add_channel(in, name, strtoul(id, NULL, 0));
		g_strfreev(items);
		if (rc != SR_OK)
			return rc;
	}

	if (!check_header_in_reread(in))
		return SR_ERR_DATA;

	return SR_OK;
}

/* Preare datafeed submission in the DATA phase. */
static int data_enter(const struct sr_input *in)
{
	struct context *inc;
	GVariant *var;

	/*
	 * Send the datafeed header and meta packets. Get the unit size
	 * from the channel count, and create a buffer for sample data
	 * submission to the session feed.
	 *
	 * Cope with multiple invocations, only do the header transmission
	 * once during inspection of an input file.
	 */
	inc = in->priv;
	if (inc->header_sent)
		return SR_OK;
	sr_dbg("Data: entering data phase.");
	std_session_send_df_header(in->sdi);
	if (inc->submit.sample_rate) {
		var = g_variant_new_uint64(inc->submit.sample_rate);
		(void)sr_session_send_meta(in->sdi, SR_CONF_SAMPLERATE, var);
	}
	inc->header_sent = TRUE;

	/*
	 * Arrange for buffered submission of samples to the session feed.
	 */
	if (!inc->channel_count)
		return SR_ERR_DATA;
	inc->submit.unit_size = (inc->channel_count + 8 - 1) / 8;
	inc->submit.feed = feed_queue_logic_alloc(in->sdi,
		CHUNKSIZE, inc->submit.unit_size);
	if (!inc->submit.feed)
		return SR_ERR_MALLOC;

	return SR_OK;
}

/* Terminate datafeed submission of the DATA phase. */
static void data_leave(const struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;
	if (!inc->header_sent)
		return;

	sr_dbg("Data: leaving data phase.");
	(void)feed_queue_logic_flush(inc->submit.feed);
	feed_queue_logic_free(inc->submit.feed);
	inc->submit.feed = NULL;

	std_session_send_df_end(in->sdi);

	inc->header_sent = FALSE;
}

/* Forward (repetitions of) sample data, optionally mark trigger location. */
static void add_sample(const struct sr_input *in, uint16_t data, size_t count)
{
	struct context *inc;
	uint8_t unit_buffer[sizeof(data)];
	size_t send_first;

	inc = in->priv;

	if (!count)
		return;

	/* Also enforce the total sample count limit here. */
	if (inc->submit.submit_count + count > inc->submit.sample_count) {
		sr_dbg("Samples: large app submit count %zu, capping.", count);
		count = inc->submit.sample_count - inc->submit.submit_count;
		sr_dbg("Samples: capped to %zu.", count);
	}

	/*
	 * Convert the caller's logical information (C language variable)
	 * to its byte buffer presentation. Then send the caller specified
	 * number of that value's repetitions to the session feed. Track
	 * the number of forwarded samples, to skip remaining buffer content
	 * after a previously configured amount of payload got forwarded,
	 * and to emit the trigger location within the stream of sample
	 * values. Split the transmission when needed to insert the packet
	 * for a trigger location.
	 */
	write_u16le(unit_buffer, data);
	send_first = 0;
	if (!inc->submit.samples_to_trigger) {
		/* EMPTY */
	} else if (count >= inc->submit.samples_to_trigger) {
		send_first = inc->submit.samples_to_trigger;
		count -= inc->submit.samples_to_trigger;
	}
	if (send_first) {
		(void)feed_queue_logic_submit(inc->submit.feed,
			unit_buffer, send_first);
		inc->submit.submit_count += send_first;
		inc->submit.samples_to_trigger -= send_first;
		sr_dbg("Trigger: sending DF packet, at %" PRIu64 ".",
			inc->submit.submit_count);
		feed_queue_logic_send_trigger(inc->submit.feed);
	}
	if (count) {
		(void)feed_queue_logic_submit(inc->submit.feed,
			unit_buffer, count);
		inc->submit.submit_count += count;
		if (inc->submit.samples_to_trigger)
			inc->submit.samples_to_trigger -= count;
	}
}

static int match_magic(GString *buf)
{

	if (!buf || !buf->str)
		return SR_ERR;
	if (buf->len < STF_MAGIC_LENGTH)
		return SR_ERR;
	if (strncmp(buf->str, STF_MAGIC_SIGMA, STF_MAGIC_LENGTH) == 0)
		return SR_OK;
	if (strncmp(buf->str, STF_MAGIC_OMEGA, STF_MAGIC_LENGTH) == 0)
		return SR_OK;
	return SR_ERR;
}

/* Check the leading magic marker at the top of the file. */
static int parse_magic(struct sr_input *in)
{
	struct context *inc;

	/*
	 * Make sure the minimum amount of input data is available, to
	 * span the magic string literal. Check the magic and remove it
	 * from buffered receive data. Advance progress (or fail for
	 * unknown or yet unsupported formats).
	 */
	inc = in->priv;
	if (in->buf->len < STF_MAGIC_LENGTH)
		return SR_OK;
	if (strncmp(in->buf->str, STF_MAGIC_SIGMA, STF_MAGIC_LENGTH) == 0) {
		inc->file_format = STF_FORMAT_SIGMA;
		g_string_erase(in->buf, 0, STF_MAGIC_LENGTH);
		sr_dbg("Magic check: Detected SIGMA file format.");
		inc->file_stage = STF_STAGE_HEADER;
		return SR_OK;
	}
	if (strncmp(in->buf->str, STF_MAGIC_OMEGA, STF_MAGIC_LENGTH) == 0) {
		inc->file_format = STF_FORMAT_OMEGA;
		g_string_erase(in->buf, 0, STF_MAGIC_LENGTH);
		sr_dbg("Magic check: Detected OMEGA file format.");
		sr_err("OMEGA format not supported by STF input module.");
		inc->file_stage = STF_STAGE_DONE;
		return SR_ERR_NA;
	}
	sr_err("Could not identify STF input format.");
	return SR_ERR_NA;
}

/* Parse a single text line of the header section. */
static void parse_header_line(struct context *inc, char *line, size_t len)
{
	char *key, *value;

	/*
	 * Split keys and values. Convert the simple types. Store the
	 * more complex types here, only evaluate their content later.
	 * Some of the fields might reference each other. Check limits
	 * and apply scaling factors later as well.
	 */
	(void)len;
	key = line;
	value = strchr(line, '=');
	if (!value)
		return;
	*value++ = '\0';

	if (strcmp(key, "TestFirstTS") == 0) {
		inc->header.first_ts = strtoull(value, NULL, 0);
	} else if (strcmp(key, "TestLengthTS") == 0) {
		inc->header.length_ts = strtoull(value, NULL, 0);
	} else if (strcmp(key, "TestTriggerTS") == 0) {
		inc->header.trigger_ts = strtoull(value, NULL, 0);
		sr_dbg("Trigger: text '%s' -> num %." PRIu64,
			value, inc->header.trigger_ts);
	} else if (strcmp(key, "TestCLKTime") == 0) {
		inc->header.clk_pu = strtoull(value, NULL, 0);
	} else if (strcmp(key, "Sigma.ClockSource") == 0) {
		inc->header.sigma_clksrc = g_strsplit_set(value, ";", 0);
	} else if (strcmp(key, "Sigma.SigmaInputs") == 0) {
		inc->header.sigma_inputs = g_strsplit_set(value, ";", 0);
	} else if (strcmp(key, "Traces.Traces") == 0) {
		inc->header.trace_specs = g_strsplit_set(value, ";", 0);
	} else if (strcmp(key, "DateTime") == 0) {
		inc->header.c_date_time = strtoull(value, NULL, 0);
	} else if (strcmp(key, "DataClass") == 0) {
		inc->header.omega_data_class = g_strdup(value);
	}
}

/* Parse the content of the "settings" section of the file. */
static int parse_header(struct sr_input *in)
{
	struct context *inc;
	int rc;
	char *line, *eol;
	size_t len;

	/*
	 * Process those text lines which have completed (which have
	 * their line termination present). A NUL character signals the
	 * end of the header section and the start of the data section.
	 *
	 * Implementor's note: The Omega file will _not_ include the NUL
	 * termination. Instead the un-zipped configuration data will
	 * see its EOF. Either the post-processing needs to get factored
	 * out, or the caller needs to send a NUL containing buffer in
	 * the Omega case, too.
	 */
	inc = in->priv;
	while (in->buf->len) {
		if (in->buf->str[0] == '\0') {
			g_string_erase(in->buf, 0, 1);
			sr_dbg("Header: End of section seen.");
			rc = eval_header(in);
			if (rc != SR_OK)
				return rc;
			inc->file_stage = STF_STAGE_DATA;
			return SR_OK;
		}

		line = in->buf->str;
		len = in->buf->len;
		eol = g_strstr_len(line, len, STF_HEADER_EOL);
		if (!eol) {
			sr_dbg("Header: Need more receive data.");
			return SR_OK;
		}
		*eol = '\0';		/* Trim off EOL. */
		len = eol - line;	/* Excludes EOL from parse call. */
		sr_spew("Header: Got a line, len %zd, text: %s.", len, line);

		parse_header_line(inc, line, len);
		g_string_erase(in->buf, 0, len + strlen(STF_HEADER_EOL));
	}
	return SR_OK;
}

/*
 * Get one or several sample sets from a 16bit raw sample memory item.
 * Ideally would be shared with the asix-sigma driver source files. But
 * is kept private to each of them so that the compiler can optimize the
 * hot code path to a maximum extent.
 */
static uint16_t get_sample_bits_16(uint16_t indata)
{
	return indata;
}

static uint16_t get_sample_bits_8(uint16_t indata, int idx)
{
	uint16_t outdata;

	indata >>= idx;
	outdata = 0;
	outdata |= (indata >> (0 * 2 - 0)) & (1 << 0);
	outdata |= (indata >> (1 * 2 - 1)) & (1 << 1);
	outdata |= (indata >> (2 * 2 - 2)) & (1 << 2);
	outdata |= (indata >> (3 * 2 - 3)) & (1 << 3);
	outdata |= (indata >> (4 * 2 - 4)) & (1 << 4);
	outdata |= (indata >> (5 * 2 - 5)) & (1 << 5);
	outdata |= (indata >> (6 * 2 - 6)) & (1 << 6);
	outdata |= (indata >> (7 * 2 - 7)) & (1 << 7);
	return outdata;
}

static uint16_t get_sample_bits_4(uint16_t indata, int idx)
{
	uint16_t outdata;

	indata >>= idx;
	outdata = 0;
	outdata |= (indata >> (0 * 4 - 0)) & (1 << 0);
	outdata |= (indata >> (1 * 4 - 1)) & (1 << 1);
	outdata |= (indata >> (2 * 4 - 2)) & (1 << 2);
	outdata |= (indata >> (3 * 4 - 3)) & (1 << 3);
	return outdata;
}

/* Map from Sigma file bit position to sigrok channel bit position. */
static uint16_t map_input_chans(struct sr_input *in, uint16_t bits)
{
	struct context *inc;
	uint16_t data;
	GSList *l;
	struct stf_channel *ch;

	inc = in->priv;
	data = 0;
	for (l = inc->channels; l; l = l->next) {
		ch = l->data;
		if (bits & ch->src_bitmask)
			data |= ch->dst_bitmask;
	}
	return data;
}

/* Forward one 16bit entity to the session feed. */
static void xlat_send_sample_data(struct sr_input *in, uint16_t indata)
{
	struct context *inc;
	uint16_t bits, data;

	/*
	 * Depending on the sample rate the memory layout for sample
	 * data varies. Get one, two, or four samples of 16, 8, or 4
	 * bits each from one 16bit entity. Get a "dense" mapping of
	 * the enabled channels from the "spread" input data. Forward
	 * the dense logic data for datafeed submission to the session,
	 * increment the timestamp for each submitted sample, and keep
	 * the last submitted pattern since it must be repeated when
	 * the next sample's timestamp is not adjacent to the current.
	 */
	inc = in->priv;
	switch (inc->submit.bits_per_sample) {
	case 16:
		bits = get_sample_bits_16(indata);
		data = map_input_chans(in, bits);
		add_sample(in, data, 1);
		inc->submit.last_submit_ts++;
		inc->submit.curr_data = data;
		break;
	case 8:
		bits = get_sample_bits_8(indata, 0);
		data = map_input_chans(in, bits);
		add_sample(in, data, 1);
		bits = get_sample_bits_8(indata, 1);
		data = map_input_chans(in, bits);
		add_sample(in, data, 1);
		inc->submit.last_submit_ts++;
		inc->submit.curr_data = data;
		break;
	case 4:
		bits = get_sample_bits_4(indata, 0);
		data = map_input_chans(in, bits);
		add_sample(in, data, 1);
		bits = get_sample_bits_4(indata, 1);
		data = map_input_chans(in, bits);
		add_sample(in, data, 1);
		bits = get_sample_bits_4(indata, 2);
		data = map_input_chans(in, bits);
		add_sample(in, data, 1);
		bits = get_sample_bits_4(indata, 3);
		data = map_input_chans(in, bits);
		add_sample(in, data, 1);
		inc->submit.last_submit_ts++;
		inc->submit.curr_data = data;
		break;
	}
}

/* Parse one "chunk" of a "record" of the file. */
static int stf_parse_data_chunk(struct sr_input *in,
	const uint8_t *info, const uint8_t *stamps, const uint8_t *samples)
{
	struct context *inc;
	uint32_t chunk_id;
	uint64_t first_ts, last_ts, chunk_len;
	uint64_t ts, ts_diff;
	size_t cluster, sample_count, sample;
	uint16_t sample_data;

	inc = in->priv;

	chunk_id = read_u32le(&info[4]);
	first_ts = read_u64le(&info[8]);
	last_ts = read_u64le(&info[16]);
	chunk_len = read_u64le(&info[24]);
	sr_spew("Chunk info: id %08x, first %" PRIu64 ", last %" PRIu64 ", len %." PRIu64,
		chunk_id, first_ts, last_ts, chunk_len);

	if (first_ts < inc->submit.last_submit_ts) {
		/* Leap backwards? Cannot be valid input data. */
		sr_dbg("Chunk: TS %" PRIu64 " before last submit TS %" PRIu64 ", stopping.",
			first_ts, inc->submit.last_submit_ts);
		return SR_ERR_DATA;
	}

	if (!inc->submit.last_submit_ts) {
		sr_dbg("Chunk: First seen TS %" PRIu64 ".", first_ts);
		inc->submit.last_submit_ts = first_ts;
	}
	if (inc->submit.submit_count >= inc->submit.sample_count) {
		sr_dbg("Chunk: Sample count reached, stopping.");
		return SR_OK;
	}
	for (cluster = 0; cluster < STF_CHUNK_CLUSTER_COUNT; cluster++) {
		ts = read_u64le_inc(&stamps);

		if (ts > inc->header.length_ts) {
			/*
			 * This cluster is beyond the file's valid TS
			 * range. Cease processing after submitting the
			 * last seen sample up to the last valid TS.
			 */
			sr_dbg("Data: Cluster TS %" PRIu64 " past header's last, flushing.", ts);
			ts_diff = inc->header.length_ts;
			ts_diff -= inc->submit.last_submit_ts;
			if (!ts_diff)
				return SR_OK;
			ts_diff *= 16 / inc->submit.bits_per_sample;
			add_sample(in, inc->submit.curr_data, ts_diff);
			return SR_OK;
		}
		if (ts < inc->submit.last_submit_ts) {
			sr_dbg("Data: Cluster TS %" PRIu64 " before last submit TS, stopping.", ts);
			return SR_OK;
		}
		sample_count = STF_CHUNK_SAMPLE_SIZE / sizeof(uint16_t);
		if (ts + sample_count < inc->header.first_ts) {
			/*
			 * The file may contain data which is located
			 * _before_ the "first valid timestamp". We need
			 * to avoid feeding these samples to the session,
			 * yet track their most recent value.
			 */
			inc->submit.last_submit_ts = ts;
			for (sample = 0; sample < sample_count; sample++) {
				sample_data = read_u16le_inc(&samples);
				inc->submit.last_submit_ts++;
				inc->submit.curr_data = sample_data;
			}
			continue;
		}
		ts_diff = ts - inc->submit.last_submit_ts;
		if (ts_diff) {
			sr_spew("Cluster: TS %" PRIu64 ", need to skip %" PRIu64 ".",
				ts, ts_diff);
			ts_diff *= 16 / inc->submit.bits_per_sample;
			add_sample(in, inc->submit.curr_data, ts_diff);
		}
		inc->submit.last_submit_ts = ts;
		for (sample = 0; sample < sample_count; sample++) {
			sample_data = read_u16le_inc(&samples);
			xlat_send_sample_data(in, sample_data);
		}
		if (inc->submit.submit_count >= inc->submit.sample_count) {
			sr_dbg("Cluster: Sample count reached, stopping.");
			return SR_OK;
		}
	}
	sr_spew("Chunk done.");

	return SR_OK;
}

/* Parse a "record" of the file which contains several "chunks". */
static int stf_parse_data_record(struct sr_input *in, struct stf_record *rec)
{
	size_t chunk_count, chunk_idx;
	const uint8_t *rdpos, *info, *stamps, *samples;
	size_t rec_len;
	int ret;

	chunk_count = rec->len / STF_CHUNK_TOTAL_SIZE;
	if (chunk_count * STF_CHUNK_TOTAL_SIZE != rec->len) {
		sr_err("Unexpected record length, not a multiple of chunks.");
		return SR_ERR_DATA;
	}
	sr_dbg("Data: Processing record, len %zu, chunks %zu, remain %zu.",
		rec->len, chunk_count, rec->len % STF_CHUNK_TOTAL_SIZE);
	rdpos = &rec->raw[0];
	info = rdpos;
	rdpos += chunk_count * STF_CHUNK_INFO_SIZE;
	stamps = rdpos;
	rdpos += chunk_count * STF_CHUNK_CLUSTER_COUNT * STF_CHUNK_STAMP_SIZE;
	samples = rdpos;
	rdpos += chunk_count * STF_CHUNK_CLUSTER_COUNT * STF_CHUNK_SAMPLE_SIZE;
	rec_len = rdpos - &rec->raw[0];
	if (rec_len != rec->len) {
		sr_err("Unexpected record length, info/stamp/samples sizes.");
		return SR_ERR_DATA;
	}

	for (chunk_idx = 0; chunk_idx < chunk_count; chunk_idx++) {
		ret = stf_parse_data_chunk(in, info, stamps, samples);
		if (ret != SR_OK)
			return ret;
		info += STF_CHUNK_INFO_SIZE;
		stamps += STF_CHUNK_CLUSTER_COUNT * STF_CHUNK_STAMP_SIZE;
		samples += STF_CHUNK_CLUSTER_COUNT * STF_CHUNK_SAMPLE_SIZE;
	}

	return SR_OK;
}

/* Parse the "data" section of the file (sample data). */
static int parse_file_data(struct sr_input *in)
{
	struct context *inc;
	size_t len, final_len;
	uint32_t crc, crc_calc;
	size_t have_len, want_len;
	const uint8_t *read_ptr;
	void *compressed;
	lzo_uint raw_len;
	int rc;

	inc = in->priv;

	rc = data_enter(in);
	if (rc != SR_OK)
		return rc;

	/*
	 * Make sure enough receive data is available for the
	 * interpretation of the record header, and for the record's
	 * respective payload data. Uncompress the payload data, have
	 * the record processed, and remove its content from the
	 * receive buffer.
	 *
	 * Implementator's note: Cope with the fact that receive data
	 * is gathered in arbitrary pieces across arbitrary numbers of
	 * routine calls. Insufficient amounts of receive data in one
	 * or several iterations is non-fatal. Make sure to only "take"
	 * input data when it's complete and got processed. Keep the
	 * current read position when input data is incomplete.
	 */
	final_len = (uint32_t)~0ul;
	while (in->buf->len) {
		/*
		 * Wait for record data to become available. Check for
		 * the availability of a header, get the payload size
		 * from the header, check for the data's availability.
		 * Check the CRC of the (compressed) payload data.
		 */
		have_len = in->buf->len;
		if (have_len < STF_DATA_REC_HDRLEN) {
			sr_dbg("Data: Need more receive data (header).");
			return SR_OK;
		}
		read_ptr = (const uint8_t *)in->buf->str;
		len = read_u32le_inc(&read_ptr);
		crc = read_u32le_inc(&read_ptr);
		if (len == final_len && !crc) {
			sr_dbg("Data: Last record seen.");
			g_string_erase(in->buf, 0, STF_DATA_REC_HDRLEN);
			inc->file_stage = STF_STAGE_DONE;
			return SR_OK;
		}
		sr_dbg("Data: Record header, len %zu, crc 0x%08lx.",
			len, (unsigned long)crc);
		if (len > STF_DATA_REC_PLMAX) {
			sr_err("Data: Illegal record length %zu.", len);
			return SR_ERR_DATA;
		}
		inc->record_data.len = len;
		inc->record_data.crc = crc;
		want_len = inc->record_data.len;
		if (have_len < STF_DATA_REC_HDRLEN + want_len) {
			sr_dbg("Data: Need more receive data (payload).");
			return SR_OK;
		}
		crc_calc = crc32(0, read_ptr, want_len);
		sr_spew("DBG: CRC32 calc comp 0x%08lx.",
			(unsigned long)crc_calc);
		if (crc_calc != inc->record_data.crc) {
			sr_err("Data: Record payload CRC mismatch.");
			return SR_ERR_DATA;
		}

		/*
		 * Uncompress the payload data, have the record processed.
		 * Drop the compressed receive data from the input buffer.
		 */
		compressed = (void *)read_ptr;
		raw_len = sizeof(inc->record_data.raw);
		memset(&inc->record_data.raw, 0, sizeof(inc->record_data.raw));
		rc = lzo1x_decompress_safe(compressed, want_len,
			inc->record_data.raw, &raw_len, NULL);
		g_string_erase(in->buf, 0, STF_DATA_REC_HDRLEN + want_len);
		if (rc) {
			sr_err("Data: Decompression error %d.", rc);
			return SR_ERR_DATA;
		}
		if (raw_len > sizeof(inc->record_data.raw)) {
			sr_err("Data: Excessive decompressed size %zu.",
				(size_t)raw_len);
			return SR_ERR_DATA;
		}
		inc->record_data.len = raw_len;
		sr_spew("Data: Uncompressed record, len %zu.",
			inc->record_data.len);
		rc = stf_parse_data_record(in, &inc->record_data);
		if (rc != SR_OK)
			return rc;
	}
	return SR_OK;
}

/* Process previously queued file content, invoked from receive() and end(). */
static int process_data(struct sr_input *in)
{
	struct context *inc;
	int ret;

	/*
	 * Have data which was received so far inspected, depending on
	 * the current internal state of the input module. Have
	 * information extracted, and/or internal state advanced to the
	 * next phase when a section has completed.
	 *
	 * BEWARE! A switch() statement would be inappropriate, as it
	 * would not allow for the timely processing of receive chunks
	 * that span multiple input file sections. It's essential that
	 * stage updates result in the continued inspection of received
	 * but not yet processed input data. Yet it's desirable to bail
	 * out upon errors as they are encountered.
	 *
	 * Note that it's essential to set sdi_ready and return from
	 * receive() after the channels got created, and before data
	 * gets submitted to the sigrok session.
	 */
	inc = in->priv;
	if (inc->file_stage == STF_STAGE_MAGIC) {
		ret = parse_magic(in);
		if (ret != SR_OK)
			return ret;
	}
	if (inc->file_stage == STF_STAGE_HEADER) {
		ret = parse_header(in);
		if (ret != SR_OK)
			return ret;
		if (inc->file_stage == STF_STAGE_DATA && !in->sdi_ready) {
			in->sdi_ready = TRUE;
			return SR_OK;
		}
	}
	if (inc->file_stage == STF_STAGE_DATA) {
		ret = parse_file_data(in);
		if (ret != SR_OK)
			return ret;
	}
	/* Nothing to be done for STF_STAGE_DONE. */
	return SR_OK;
}

static const char *stf_extensions[] = { "stf", NULL, };

/* Check if filename ends in one of STF format's extensions. */
static gboolean is_stf_extension(const char *fn)
{
	size_t fn_len, ext_len, ext_idx, dot_idx;
	const char *ext;

	if (!fn || !*fn)
		return FALSE;
	fn_len = strlen(fn);

	for (ext_idx = 0; /* EMPTY */; ext_idx++) {
		ext = stf_extensions[ext_idx];
		if (!ext || !*ext)
			break;
		ext_len = strlen(ext);
		if (fn_len < 1 + ext_len)
			continue;
		dot_idx = fn_len - 1 - ext_len;
		if (fn[dot_idx] != '.')
			continue;
		if (strcasecmp(&fn[dot_idx + 1], ext) != 0)
			continue;
		return TRUE;
	}

	return FALSE;
}

/* Try to auto-detect an input module for a given file. */
static int format_match(GHashTable *metadata, unsigned int *confidence)
{
	gboolean found;
	const char *fn;
	GString *buf;

	found = FALSE;

	/* Check the filename (its extension). */
	fn = (const char *)g_hash_table_lookup(metadata,
		GINT_TO_POINTER(SR_INPUT_META_FILENAME));
	sr_dbg("Format Match: filename %s.", fn);
	if (is_stf_extension(fn)) {
		*confidence = 100;
		found = TRUE;
		sr_dbg("Format Match: weak match found (filename).");
	}

	/* Check the part of the file content (leading magic). */
	buf = (GString *)g_hash_table_lookup(metadata,
		GINT_TO_POINTER(SR_INPUT_META_HEADER));
	if (match_magic(buf) == SR_OK) {
		*confidence = 10;
		found = TRUE;
		sr_dbg("Format Match: strong match found (magic).");
	}

	if (found)
		return SR_OK;
	return SR_ERR;
}

/* Initialize the input module. Inspect user specified options. */
static int init(struct sr_input *in, GHashTable *options)
{
	GVariant *var;
	struct context *inc;
	uint64_t sample_rate;

	/* Allocate input module context. */
	inc = g_malloc0(sizeof(*inc));
	if (!inc)
		return SR_ERR_MALLOC;
	in->priv = inc;

	/* Allocate input device instance data. */
	in->sdi = g_malloc0(sizeof(*in->sdi));
	if (!in->sdi)
		return SR_ERR_MALLOC;

	/* Preset values from caller specified options. */
	var = g_hash_table_lookup(options, "samplerate");
	sample_rate = g_variant_get_uint64(var);
	inc->keep.sample_rate = sample_rate;

	return SR_OK;
}

/* Process another chunk of the input stream (file content). */
static int receive(struct sr_input *in, GString *buf)
{

	/*
	 * Unconditionally buffer the most recently received piece of
	 * file content. Run another process() routine that is shared
	 * with end(), to make sure pending data gets processed, even
	 * when receive() is only invoked exactly once for short input.
	 */
	g_string_append_len(in->buf, buf->str, buf->len);
	return process_data(in);
}

/* Process the end of the input stream (file content). */
static int end(struct sr_input *in)
{
	int ret;

	/*
	 * Process any previously queued receive data. Flush any queued
	 * sample data that wasn't submitted before. Send the datafeed
	 * session end packet if a session start was sent before.
	 */
	ret = process_data(in);
	if (ret != SR_OK)
		return ret;

	data_leave(in);

	return SR_OK;
}

/* Release previously allocated resources. */
static void cleanup(struct sr_input *in)
{
	struct context *inc;

	/* Keep channel references between file re-imports. */
	keep_header_for_reread(in);

	/* Release dynamically allocated resources. */
	inc = in->priv;

	g_slist_free_full(inc->channels, free_channel);
	feed_queue_logic_free(inc->submit.feed);
	inc->submit.feed = NULL;
	g_strfreev(inc->header.sigma_clksrc);
	inc->header.sigma_clksrc = NULL;
	g_strfreev(inc->header.sigma_inputs);
	inc->header.sigma_inputs = NULL;
	g_strfreev(inc->header.trace_specs);
	inc->header.trace_specs = NULL;
}

static int reset(struct sr_input *in)
{
	struct context *inc;
	struct keep_specs keep;

	inc = in->priv;

	cleanup(in);
	keep = inc->keep;
	memset(inc, 0, sizeof(*inc));
	g_string_truncate(in->buf, 0);
	inc->keep = keep;

	return SR_OK;
}

enum option_index {
	OPT_SAMPLERATE,
	OPT_MAX,
};

static struct sr_option options[] = {
	[OPT_SAMPLERATE] = {
		"samplerate", "Samplerate (Hz)",
		"The input data's sample rate in Hz. No default value.",
		NULL, NULL,
	},
	ALL_ZERO,
};

static const struct sr_option *get_options(void)
{
	GVariant *var;

	if (!options[0].def) {
		var = g_variant_new_uint64(0);
		options[OPT_SAMPLERATE].def = g_variant_ref_sink(var);
	}

	return options;
}

SR_PRIV struct sr_input_module input_stf = {
	.id = "stf",
	.name = "STF",
	.desc = "Sigma Test File (Asix Sigma/Omega)",
	.exts = stf_extensions,
	.metadata = {
		SR_INPUT_META_FILENAME | SR_INPUT_META_REQUIRED,
		SR_INPUT_META_HEADER | SR_INPUT_META_REQUIRED,
	},
	.options = get_options,
	.format_match = format_match,
	.init = init,
	.receive = receive,
	.end = end,
	.cleanup = cleanup,
	.reset = reset,
};
