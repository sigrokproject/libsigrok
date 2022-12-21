#include <config.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX 			"input/flipper"

#define FLIPPER_FILE_STR 		"Filetype: Flipper SubGhz RAW File"
#define FLIPPER_FILE_VERSION_STR 	"Version: 1"

/* Every line that starts with this string contains raw values */
#define RAW_DATA_STR 			"RAW_Data: "

#define CHUNK_SIZE 			(4 * 1024)

struct context
{
	gboolean started;
	gboolean got_header;
	struct feed_queue_logic *feed_logic;
	struct sr_channel *logic_channel;
};

/*
 * Checks whether we have buffered enough data to have a complete header.
 * @retval -1 if the header is incomplete.
 * @retval a positive value is returned representing the offset to the start of data (right after the header).
 */
static long header_complete(GString *buf)
{
	char *occurrence = g_strstr_len(buf->str, buf->len, RAW_DATA_STR);
	if (!occurrence)
		return -1;

	return occurrence - buf->str;
}

/*
 * Checks if the header matches the flipper SubGHz file format.
 * This function assumes that enough data is buffered in buf.
 */
static gboolean is_valid_file_type(GString *buf)
{
	char *occurrence = g_strstr_len(buf->str, buf->len, FLIPPER_FILE_STR);
	if (!occurrence)
		return FALSE;

	sr_dbg("Flipper SubGHz file format detected");
	return TRUE;
}

/*
 * Checks if the header matches the supported flipper SubGHz file format version.
 * This function assumes that enough data is buffered in buf.
 */
static gboolean is_valid_file_version(GString *buf)
{
	char *occurrence = g_strstr_len(buf->str, buf->len, FLIPPER_FILE_VERSION_STR);
	if (!occurrence)
		return FALSE;

	sr_dbg("Flipper SubGHz version 1 file format detected");
	return TRUE;
}

static gboolean parse_header(GString *buf)
{
	/*
	 * We actually ignore the header, we're just interested in finding the start of the raw data.
	 * However, this function will rewind the buffer forward to the start of the header.
	 */
	long data_offset = header_complete(buf);
	sr_dbg("Found header at offset %ld", data_offset);

	/* Discard header, erasing buffer so far up to but not including the first RAW_Data stanza. */
	g_string_erase(buf, 0, data_offset);
	return TRUE;
}

static int format_match(GHashTable *metadata, unsigned int *confidence)
{
	GString *buf;

	buf = g_hash_table_lookup(metadata,
				  GINT_TO_POINTER(SR_INPUT_META_HEADER));

	/* Make sure we have enough data buffered to recognise both file format and version (plus newline). */
	if (buf->len < strlen(FLIPPER_FILE_STR) + strlen(FLIPPER_FILE_VERSION_STR) + 1) {
		return SR_ERR_NA;
	}

	if (is_valid_file_type(buf)) {
		if (is_valid_file_version(buf)) {
			*confidence = 1;
			return SR_OK;
		}
		sr_dbg("Detected Flipper SubGHz file with an unknown version");
		*confidence = 100;
		return SR_ERR_DATA;
	}

	return SR_ERR;
}

static int process_value(struct context *inc, signed long value)
{
	signed long num_samples;
	uint8_t logic;
	int ret;

	if (value > 0) {
		/* queue value '1' samples */
		num_samples = value;
		logic = 1;
	} else {
		/* queue abs(value) '0' samples */
		num_samples = labs(value);
		logic = 0;
	}

	sr_dbg("logical %d duration %ld", logic, num_samples);
	ret = feed_queue_logic_submit(inc->feed_logic, &logic, num_samples);
	if (ret != SR_OK) {
		sr_dbg("Error buffering logic signal");
	}
	return ret;
}

static int parse_raw_values(struct context *inc, char *line)
{
	int ret;
	sr_dbg("Parsing line: '%s'", line);

	gchar **values = g_strsplit(line, " ", 0);
	int num_values = 0;
	if (values)
		num_values = g_strv_length(values);
	sr_dbg("Line contains %d raw values", num_values);
	for (int i = 0; i < num_values; i++) {
		signed long value = strtol(values[i], (char **)NULL, 10);
		if (value == 0) {
			/* Even if 0 is a valid output for strtol, a 0 value would not make sense in this file format. */
			g_strfreev(values);
			return SR_ERR;
		}
		ret = process_value(inc, value);
		if (ret != SR_OK)
			return ret;
	}
	g_strfreev(values);
	return SR_OK;
}

static int process_buffer(struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;

	if (!inc->started) {
		inc->logic_channel = sr_channel_new(in->sdi, 0, SR_CHANNEL_LOGIC, TRUE, "Raw Signal");
		std_session_send_df_header(in->sdi);
		sr_session_send_meta(in->sdi, SR_CONF_SAMPLERATE, g_variant_new_uint64(1000000));
		inc->started = TRUE;
	}

	/* Process only up to the last complete to avoid processing lines where a value might be chopped/incomplete */
	int consumed = 0;
	gchar *last_newline = g_strrstr(in->buf->str, "\n");
	if (last_newline) {
		*last_newline = '\0';
		consumed = last_newline - in->buf->str + 1;
	}
	/*
	 * For each line, remove any "RAW_Data:" strings. At this point, assuming that the
	 * data was correctly formatted, all that should be remaining would be the raw integer values.
	 */
	gchar **lines = g_strsplit(in->buf->str, "\n", 0);
	int num_lines = 0;
	if (lines)
		num_lines = g_strv_length(lines);
	for (int i = 0; i < num_lines; i++) {
		gchar *occurrence = g_strstr_len(lines[i], strlen(lines[i]), RAW_DATA_STR);
		if (occurrence) {
			memset(occurrence, ' ', strlen(RAW_DATA_STR));
		}
		int ret = parse_raw_values(inc, g_strstrip(lines[i]));
		if (ret != SR_OK) {
			g_strfreev(lines);
			return ret;
		}
	}
	g_strfreev(lines);

	/* chop off up to the point we consumed */
	g_string_erase(in->buf, 0, consumed);
	return SR_OK;
}

static int receive(struct sr_input *in, GString *buf)
{
	struct context *inc;

	inc = in->priv;

	sr_dbg("receive %lu bytes", buf->len);
	g_string_append_len(in->buf, buf->str, buf->len);

	if (!inc->got_header) {
		if (!parse_header(in->buf))
			return SR_OK;
		sr_dbg("parsed header");
		inc->got_header = TRUE;
		in->sdi_ready = TRUE;
		return SR_OK;
	}

	return process_buffer(in);
}

static int end(struct sr_input *in)
{
	struct context *inc;
	int ret;

	sr_dbg("end() called, frontend notified there's no more input coming");
	inc = in->priv;

	/* Finish processing any buffered data */
	if (in->sdi_ready) {
		ret = process_buffer(in);
		if (ret != SR_OK) {
			sr_dbg("process_buffer ERROR");
			return ret;
		}
		sr_dbg("flushing");
		feed_queue_logic_flush(inc->feed_logic);
	}

	/* Send DF_END when DF_HEADER was sent before */
	if (inc->started)
		std_session_send_df_end(in->sdi);

	return SR_OK;
}

static void cleanup(struct sr_input *in)
{
	struct context *inc;

	inc = in->priv;

	feed_queue_logic_free(inc->feed_logic);
	inc->feed_logic = NULL;
}

static int init(struct sr_input *in, GHashTable *options)
{
	struct context *inc;
	(void)options; /* no options used */

	inc = g_malloc0(sizeof(*inc));
	in->sdi = g_malloc0(sizeof(*in->sdi));
	in->priv = inc;
	inc->feed_logic = feed_queue_logic_alloc(in->sdi, CHUNK_SIZE, 1);

	return SR_OK;
}

SR_PRIV struct sr_input_module input_flipper_sub = {
	.id = "flipper",
	.name = "flipper",
	.desc = "Flipper Sub-GHz v1",
	.exts = (const char *[]){"sub", NULL},
	.options = NULL,
	.init = init,
	.receive = receive,
	.end = end,
	.cleanup = cleanup,
	.format_match = format_match,
	.metadata = { SR_INPUT_META_HEADER | SR_INPUT_META_REQUIRED },
	.reset = NULL,
};
