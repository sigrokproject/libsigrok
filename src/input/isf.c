/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 Filip Kosecek <filip.kosecek@gmail.com>
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

#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "input/isf"

#define CHUNK_SIZE	(4 * 1024 * 1024)

/* Number of bytes required to process the header. */
#define MIN_INITIAL_OFFSET 1024

/* Number of items in the header. */
#define HEADER_ITEMS_PARAMETERS 10

/* Maximum length of a channel name. */
#define MAX_CHANNEL_NAME_SIZE 32

/* Maximum size of encoding and waveform strings. */
#define MAX_ENCODING_STRING_SIZE 10
#define MAX_WAVEFORM_STRING_SIZE 10

/* Byte order */
enum byteorder {
	LSB = 0,
	MSB = 1,
};

/* Format, i.e. RI (signed integer) or FP (floating point) */
enum format {
	RI = 0,
	FP = 1,
};

/* Waveform type, i.e. analog or radar frequency (RF) */
enum waveform_type {
	ANALOG = 0,
	RF_FD = 1,
};

union floating_point {
	float f;
	uint32_t i;
};

struct context {
	gboolean started;
	gboolean found_data_section;
	float yoff;
	float yzero;
	float ymult;
	float xincr;
	int bytnr;
	enum byteorder byte_order;
	enum format bn_fmt;
	enum waveform_type wfmtype;
	char channel_name[MAX_CHANNEL_NAME_SIZE];
};

/* Header items used to process the input file. */
enum header_items_enum {
	YOFF = 0,
	YZERO = 1,
	YMULT = 2,
	XINCR = 3,
	BYTNR = 4,
	BYTE_ORDER = 5,
	BN_FMT = 6,
	WFID = 7,
	WFMTYPE = 8,
	ENCODING = 9,
};

/* Strings searched for in the file header representing the header items. */
static const char *header_items[] = {
	[YOFF] = "YOFF ",
	[YZERO] = "YZERO ",
	[YMULT] = "YMULT ",
	[XINCR] = "XINCR ",
	[BYTNR] = "BYT_NR ",
	[BYTE_ORDER] = "BYT_OR ",
	[BN_FMT] = "BN_FMT ",
	[WFID] = "WFID ",
	[WFMTYPE] = "WFMTYPE ",
	[ENCODING] = "ENCDG ",
};

/* Find the header item in the header. */
static char *find_item(GString *buf, const char *item)
{
	return g_strstr_len(buf->str, buf->len, item);
}

/* Check if the header is loaded and can be processed. */
static gboolean has_header(GString *buf)
{
	return buf->len >= MIN_INITIAL_OFFSET;
}

/* Find curve which indicates the end of the header and the start of the data. */
static char *find_data_section(GString *buf)
{
	const char *curve = "CURVE #";

	char *data_ptr;
	size_t offset, metadata_length;

	data_ptr = g_strstr_len(buf->str, buf->len, curve);
	if (data_ptr == NULL)
		return NULL;

	data_ptr += strlen(curve);
	offset = data_ptr - buf->str;
	if (offset >= buf->len)
		return NULL;

	/* Curve metadata length is an ASCII byte, hence -48. */
	metadata_length = (size_t) *data_ptr - 48;
	data_ptr += 1 + metadata_length;
	offset = data_ptr - buf->str;

	if (offset >= buf->len)
		return NULL;

	return data_ptr;
}

/* Locate and extract the channel name in the header. */
static void extract_channel_name(struct context *inc, const char *beg)
{
	size_t i, channel_ix;

	channel_ix = 0;
	/* ISF WFID looks something like WFID "Ch1, ..."; hence we must skip character '"' */
	i = 1;
	while (beg[i] != ',' && beg[i] != '"' && channel_ix < MAX_CHANNEL_NAME_SIZE - 1)
		inc->channel_name[channel_ix++] = beg[i++];
}

/*
 * Parse and save string value from the string
 * starting at beg and ending with ';' character.
 */
static void find_string_value(const char *beg, char *value, size_t value_len)
{
	size_t i, value_idx;

	value_idx = 0;
	i = 0;
	while (beg[i] != ';' && value_idx < value_len)
		value[value_idx++] = beg[i++];
	value[value_idx] = 0;
}

/* Extract enconding type from the header. */
static int find_encoding(const char *beg)
{
	char value[MAX_ENCODING_STRING_SIZE];

	find_string_value(beg, value, MAX_ENCODING_STRING_SIZE - 1);

	/* TODO: Use strncmp instead. */
	/* "BIN" and "BINARY" are accepted as suggested in a pull request comment. */
	if (strcmp(value, "BINARY") != 0 && strcmp(value, "BIN") != 0) {
		sr_err("Only binary encoding supported.");
		return SR_ERR_NA;
	}

	return SR_OK;
}

/* Extract waveform type from the header. */
static int find_waveform_type(struct context *inc, const char *beg)
{
	char value[MAX_WAVEFORM_STRING_SIZE];

	find_string_value(beg, value, MAX_WAVEFORM_STRING_SIZE - 1);

	if (strcmp(value, "ANALOG") == 0)
		inc->wfmtype = ANALOG;
	else if (strcmp(value, "RF_FD") == 0)
		inc->wfmtype = RF_FD;
	else
		return SR_ERR_DATA;

	return SR_OK;
}

/* Parse header items. */
static int process_header_item(const char *beg, struct context *inc, enum header_items_enum item)
{
	int ret;

	switch (item) {
	case YOFF:
		inc->yoff = (float) g_ascii_strtod(beg, NULL);
		break;

	case YZERO:
		inc->yzero = (float) g_ascii_strtod(beg, NULL);
		break;

	case YMULT:
		inc->ymult = (float) g_ascii_strtod(beg, NULL);
		break;

	case XINCR:
		inc->xincr = (float) g_ascii_strtod(beg, NULL);
		break;

	case BYTNR:
		inc->bytnr = (int) g_ascii_strtoll(beg, NULL, 10);
		break;

	case BYTE_ORDER:
		if (strncmp(beg, "LSB", 3) == 0)
			inc->byte_order = LSB;
		else if (strncmp(beg, "MSB", 3) == 0)
			inc->byte_order = MSB;
		else
			return SR_ERR_DATA;
		break;

	case BN_FMT:
		if (strncmp(beg, "RI", 2) == 0)
			inc->bn_fmt = RI;
		else if (strncmp(beg, "FP", 2) == 0)
			inc->bn_fmt = FP;
		else
			return SR_ERR_DATA;
		break;

	case WFID:
		extract_channel_name(inc, beg);
		break;

	case WFMTYPE:
		ret = find_waveform_type(inc, beg);
		if (ret != SR_OK)
			return ret;
		break;

	case ENCODING:
		ret = find_encoding(beg);
		if (ret != SR_OK)
			return ret;
		break;
	default:
		return SR_ERR_ARG;
	}
	
	return SR_OK;
}

/* Parse the input file header. */
static int parse_isf_header(GString *buf, struct context *inc)
{
	char *pattern, *data_section;
	int ret, i;

	if (inc == NULL)
		return SR_ERR_ARG;

	/* Search for all header items. */
	for (i = 0; i < HEADER_ITEMS_PARAMETERS; ++i) {
		pattern = find_item(buf, header_items[i]);
		if (pattern == NULL) {
			/* WFID is not required. */
			if (i == WFID)
				continue;
			return SR_ERR_DATA;
		}
		
		ret = process_header_item(pattern + strlen(header_items[i]), inc, i);
		if (ret != SR_OK)
			return ret;
	}

	data_section = find_data_section(buf);
	if (data_section == NULL)
		return SR_ERR_DATA;

	return SR_OK;
}

/* Check if the format matches ISF format. */
static int format_match(GHashTable *metadata, unsigned int *confidence)
{
	const char *default_extension = ".isf";
	const char *nr_pt = "NR_PT";

	GString *buf;
	char *fn;
	size_t fn_len;

	/* If the extension is '.isf', it is likely the format matches. */
	fn = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_FILENAME));
	if (fn != NULL && (fn_len = strlen(fn)) >= strlen(default_extension)) {
		if (strcmp(fn + fn_len - strlen(default_extension), default_extension) == 0) {
			*confidence = 10;
			return SR_OK;
		}
	}

	buf = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_HEADER));
	/* Check if the header contains NR_PT item. */
	/* TODO: The header could be searched for more items to increase the confidence. */
	if (buf == NULL || g_strstr_len(buf->str, buf->len, nr_pt) == NULL) {
		/* TODO: Maybe confidence shouldn't be modified. */
		*confidence = 0;
		return SR_ERR;
	}

	/* The header contains NR_PT item, the confidence is high. */
	*confidence = 50;
	return SR_OK;
}

/* Initialize the ISF module. */
static int init(struct sr_input *in, GHashTable *options)
{
	struct context *inc;

	(void) options;

	in->sdi = g_malloc0(sizeof(struct sr_dev_inst));
	in->priv = g_malloc0(sizeof(struct context));

	inc = in->priv;
	inc->found_data_section = FALSE;
	memset(inc->channel_name, 0, MAX_CHANNEL_NAME_SIZE);

	return SR_OK;
}

/*
 * Read an integer value from the data buffer.
 * The number of bytes per sample may vary and a sample is stored
 * in a signed 64-bit integer. Therefore a negative integer extension
 * might be needed.
 */
static int64_t read_int_value(struct sr_input *in, size_t offset)
{
	struct context *inc;
	int bytnr, i;
	int64_t value;
	uint8_t data[8];

	inc = in->priv;
	bytnr = inc->bytnr;
	/* TODO: Perform proper bounds checking. */
	g_assert(bytnr <= 8 && bytnr <= sizeof(data));
	memcpy(data, in->buf->str + offset, bytnr);
	value = 0;
	if (inc->byte_order == MSB) {
		for (i = 0; i < bytnr; ++i) {
			value = value << 8;
			value |= data[i];
		}
	} else {
		for (i = bytnr - 1; i >= 0; --i) {
			value = value << 8;
			value |= data[i];
		}
	}

	/* Check if the loaded value is negative. */
	if ((value & (1 << (8*bytnr - 1))) != 0) {
		/* Extend the 64-bit integer if the value is negative.  */
		i = ~((1 << (8*bytnr - 1)) - 1);
		value |= i;
	}

	return value;
}

/* Read a float value from the data buffer. */
static float read_float_value(struct sr_input *in, size_t offset)
{
	struct context *inc;
	union floating_point fp;
	int bytnr, i;
	uint8_t data[4];

	inc = in->priv;
	bytnr = inc->bytnr;
	/* TODO: Check single-precision format properly. */
	g_assert(sizeof(float) == 4 && bytnr == 4);
	fp.i = 0;
	memcpy(data, in->buf->str + offset, sizeof(data));

	if (inc->byte_order == MSB) {
		for (i = 0; i < bytnr; ++i) {
			fp.i = fp.i << 8;
			fp.i |= data[i];
		}
	} else {
		for (i = bytnr - 1; i >= 0; --i) {
			fp.i = fp.i << 8;
			fp.i |= data[i];
		}
	}

	return fp.f;
}

/* Send a sample chunk to the sigrok session. */
static void send_chunk(struct sr_input *in, size_t initial_offset, size_t num_samples)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct context *inc;
	float *fdata;
	size_t offset, i;

	inc = in->priv;
	offset = initial_offset;
	fdata = g_malloc0(sizeof(float) * num_samples);
	for (i = 0; i < num_samples; ++i) {
		if (inc->bn_fmt == RI) {
			fdata[i] = ((float) read_int_value(in, offset) - inc->yoff) * inc->ymult + inc->yzero;
		} else if (inc->bn_fmt == FP) {
			fdata[i] = (read_float_value(in, offset) - inc->yoff) * inc->ymult + inc->yzero;
		}
		offset += inc->bytnr;

		/* Convert W to dBm if the sample is RF. */
		if (inc->wfmtype == RF_FD)
			fdata[i] = 10 * log10f(1000 * fdata[i]);
	}

	sr_analog_init(&analog, &encoding, &meaning, &spec, 2);
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.num_samples = num_samples;
	analog.data = fdata;
	analog.meaning->channels = in->sdi->channels;
	analog.meaning->mq = 0;
	analog.meaning->mqflags = 0;
	analog.meaning->unit = 0;

	sr_session_send(in->sdi, &packet);
	g_free(fdata);
}

/* Process the buffer data. */
static int process_buffer(struct sr_input *in)
{
	struct context *inc;
	char *data;
	size_t offset, chunk_samples, total_samples, processed, max_chunk_samples, num_samples;

	inc = in->priv;
	/* Initialize the session. */
	if (!inc->started) {
		std_session_send_df_header(in->sdi);
		/* Send samplerate. */
		(void) sr_session_send_meta(in->sdi, SR_CONF_SAMPLERATE, g_variant_new_uint64((uint64_t) (1 / inc->xincr)));
		inc->started = TRUE;
	}

	/* Set offset to the data section beginning. */
	if (!inc->found_data_section) {
		data = find_data_section(in->buf);
		if (data == NULL) {
			sr_err("Couldn't find data section.");
			return SR_ERR;
		}
		offset = data - in->buf->str;
		inc->found_data_section = TRUE;
	} else {
		offset = 0;
	}

	/* Slice the chunk into samples and clear the input buffer. */
	processed = 0;
	chunk_samples = (in->buf->len - offset)/inc->bytnr;
	max_chunk_samples = CHUNK_SIZE/inc->bytnr;
	total_samples = chunk_samples;
	
	while (processed < total_samples) {
		if (chunk_samples > max_chunk_samples)
			num_samples = max_chunk_samples;
		else
			num_samples = chunk_samples;

		send_chunk(in, offset, num_samples);
		offset += num_samples * inc->bytnr;
		chunk_samples -= num_samples;
		processed += num_samples;
	}

	if (offset < in->buf->len)
		g_string_erase(in->buf, 0, offset);
	else
		g_string_truncate(in->buf, 0);

	return SR_OK;
}

/* Process received data. */
static int receive(struct sr_input *in, GString *buf)
{
	struct context *inc;
	int ret;

	inc = in->priv;
	g_string_append_len(in->buf, buf->str, buf->len);

	if (!in->sdi_ready) {
		if (!has_header(in->buf))
			return SR_OK;

		ret = parse_isf_header(in->buf, inc);
		if (ret != SR_OK)
			return ret;

		if (inc->bytnr > 8) {
			sr_err("Byte number > 8 is not supported.");
			return SR_ERR_NA;
		}

		/* Set default channel name. */
		if (strlen(inc->channel_name) == 0)
			snprintf(inc->channel_name, MAX_CHANNEL_NAME_SIZE, "CH");
		sr_channel_new(in->sdi, 0, SR_CHANNEL_ANALOG, TRUE, inc->channel_name);

		in->sdi_ready = TRUE;
		return SR_OK;
	}

	return process_buffer(in);
}

/* Finish the processing. */
static int end(struct sr_input *in)
{
	struct context *inc;
	int ret;

	if (in->sdi_ready)
		ret = process_buffer(in);
	else
		ret = SR_OK;

	inc = in->priv;
	if (inc->started)
		std_session_send_df_end(in->sdi);

	return ret;
}

/* Clear the buffer and metadata. */
static int reset(struct sr_input *in) {
	memset(in->priv, 0, sizeof(struct context));

	g_string_truncate(in->buf, 0);
	return SR_OK;
}

SR_PRIV struct sr_input_module input_isf = {
		.id = "isf",
		.name = "ISF",
		.desc = "Tektronix isf format",
		.exts = (const char *[]) {"isf", NULL},
		.metadata = {SR_INPUT_META_FILENAME, SR_INPUT_META_HEADER | SR_INPUT_META_REQUIRED},
		.format_match = format_match,
		.init = init,
		.receive = receive,
		.end = end,
		.reset = reset
};
