/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Soeren Apel <soeren@apelpie.net>
 * Copyright (C) 2015 Bert Vermeulen <bert@biot.com>
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
 * Usage notes:
 * This input module reads .ad files created using the
 * following practice commands:
 *
 * I.SAVE <file> /NoCompress
 * IPROBE.SAVE <file> /NoCompress
 *
 * It currently cannot make use of files that have been
 * saved using /QuickCompress, /Compress or /ZIP.
 * As a workaround you may load the file in PowerView
 * using I.LOAD / IPROBE.LOAD and re-save using /NoCompress.
 */

#include <config.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "input/trace32_ad"

#define MAX_CHUNK_SIZE    4096
#define OUTBUF_FLUSH_SIZE 10240
#define MAX_POD_COUNT     12
#define HEADER_SIZE       80

#define TIMESTAMP_RESOLUTION ((double)0.000000000078125) /* 0.078125 ns */

/*
 * The resolution equals a sampling freq of 12.8 GHz. That's a bit high
 * for inter-record sample generation, so we scale it down to 200 MHz
 * for now. That way, the scaling factor becomes 32.
 */
#define DEFAULT_SAMPLERATE 200

enum {
	AD_FORMAT_BINHDR = 1, /* Binary header, binary data, textual setup info */
	AD_FORMAT_TXTHDR      /* Textual header, binary data */
};

enum {
	AD_DEVICE_PI = 1, /* Data recorded by LA-7940 PowerIntegrator or */
                          /* LA-394x PowerIntegrator II. */
	AD_DEVICE_IPROBE  /* Data recorded by LA-769x PowerTrace II IProbe. */
	/* Missing file format info for LA-793x ICD PowerProbe */
	/* Missing file format info for LA-4530 uTrace analog probe */
};

enum {
	AD_MODE_250MHZ = 0,
	AD_MODE_500MHZ = 1
};

enum {
	AD_COMPR_NONE  = 0, /* File created with /NOCOMPRESS */
	AD_COMPR_QCOMP = 6, /* File created with /COMPRESS or /QUICKCOMPRESS */
};

struct context {
	gboolean meta_sent;
	gboolean header_read, records_read, trigger_sent;
	char format, device, record_mode, compression;
	char pod_status[MAX_POD_COUNT];
	struct sr_channel *channels[MAX_POD_COUNT][17]; /* 16 + CLK */
	uint64_t trigger_timestamp;
	uint32_t record_size, record_count, cur_record;
	int32_t last_record;
	uint64_t samplerate;
	double timestamp_scale;
	GString *out_buf;
};

static int process_header(GString *buf, struct context *inc);
static void create_channels(struct sr_input *in);

static char get_pod_name_from_id(int id)
{
	switch (id) {
	case 0:  return 'A';
	case 1:  return 'B';
	case 2:  return 'C';
	case 3:  return 'D';
	case 4:  return 'E';
	case 5:  return 'F';
	case 6:  return 'J';
	case 7:  return 'K';
	case 8:  return 'L';
	case 9:  return 'M';
	case 10: return 'N';
	case 11: return 'O';
	default:
		sr_err("get_pod_name_from_id() called with invalid ID %d!", id);
	}
	return 'X';
}

static int init(struct sr_input *in, GHashTable *options)
{
	struct context *inc;
	int pod;
	char id[17];

	in->sdi = g_malloc0(sizeof(struct sr_dev_inst));
	in->priv = g_malloc0(sizeof(struct context));

	inc = in->priv;

	/* Calculate the desired timestamp scaling factor. */
	inc->samplerate = 1000000 *
		g_variant_get_uint32(g_hash_table_lookup(options, "samplerate"));

	inc->timestamp_scale = ((1 / TIMESTAMP_RESOLUTION) / (double)inc->samplerate);

	/* Enable the pods the user chose to see. */
	for (pod = 0; pod < MAX_POD_COUNT; pod++) {
		g_snprintf(id, sizeof(id), "pod%c", get_pod_name_from_id(pod));
		if (g_variant_get_boolean(g_hash_table_lookup(options, id)))
			inc->pod_status[pod] = 1;
	}

	create_channels(in);
	if (g_slist_length(in->sdi->channels) == 0) {
		sr_err("No pods were selected and thus no channels created, aborting.");
		g_free(in->priv);
		g_free(in->sdi);
		return SR_ERR;
	}

	inc->out_buf = g_string_sized_new(OUTBUF_FLUSH_SIZE);

	return SR_OK;
}

static int format_match(GHashTable *metadata)
{
	GString *buf;

	buf = g_hash_table_lookup(metadata, GINT_TO_POINTER(SR_INPUT_META_HEADER));

	return process_header(buf, NULL);
}

static int process_header(GString *buf, struct context *inc)
{
	char *format_name, *format_name_sig;
	int i, record_size, device_id;

	/*
	 * 00-31 (0x00-1F) file format name
	 * 32-39 (0x20-27) trigger timestamp       u64
	 * 40-47 (0x28-2F) unused
	 * 48    (0x30)    compression
	 * 49-53 (0x31-35) ??
	 *       50 (0x32) 0x00 (PI), 0x01 (iprobe)
	 * 54    (0x36)    0x08 (PI 250/500), 0x0A (iprobe 250)
	 * 55    (0x37)    0x00 (250), 0x01 (500)
	 * 56    (0x38)    record size             u8
	 * 57-59 (0x39-3B) const 0x00
	 * 60-63 (0x3C-3F) number of records       u32
	 * 64-67 (0x40-43) id of last record       s32
	 * 68-77 (0x44-4D) ??
	 *       71 (0x47) const 0x80=128
	 *       72 (0x48) const 0x01
	 * 78-79 (0x4E-4F) ??
	 */

	/* Note: inc is off-limits until we check whether it's a valid pointer. */

	format_name = g_strndup(buf->str, 32);

	/* File format name ends on 0x20/0x1A, let's remove both. */
	for (i = 1; i < 31; i++) {
		if (format_name[i] == 0x1A) {
			format_name[i - 1] = 0;
			format_name[i] = 0;
		}
	}
	g_strchomp(format_name); /* This is for additional padding spaces. */

	format_name_sig = g_strndup(format_name, 5);

	/* Desired file formats either start with digit+space or "trace32". */
	if (g_strcmp0(format_name_sig, "trace32")) {
		if (inc)
			inc->format = AD_FORMAT_BINHDR;
	} else if (g_ascii_isdigit(format_name[0]) && (format_name[1] == 0x20)) {
		if (inc)
			inc->format = AD_FORMAT_TXTHDR;
		g_free(format_name_sig);
		g_free(format_name);
		sr_err("This format isn't implemented yet, aborting.");
		return SR_ERR;
	} else {
		g_free(format_name_sig);
		g_free(format_name);
		sr_err("Don't know this file format, aborting.");
		return SR_ERR;
	}

	sr_dbg("File says it's \"%s\"", format_name);

	record_size = R8(buf->str + 56);
	device_id = 0;

	if (g_strcmp0(format_name, "trace32 power integrator data") == 0) {
		if (record_size == 28 || record_size == 45)
			device_id = AD_DEVICE_PI;
	} else if (g_strcmp0(format_name, "trace32 iprobe data") == 0) {
		if (record_size == 11)
			device_id = AD_DEVICE_IPROBE;
	}

	if (!device_id) {
		g_free(format_name_sig);
		g_free(format_name);
		sr_err("Don't know how to handle this file with record size %d.",
			record_size);
		return SR_ERR;
	}

	g_free(format_name_sig);
	g_free(format_name);

	/* Stop processing the header if we just want to identify the file. */
	if (!inc)
		return SR_OK;

	inc->device       = device_id;
	inc->trigger_timestamp = RL64(buf->str + 32);
	inc->compression  = R8(buf->str + 48); /* Maps to the enum. */
	inc->record_mode  = R8(buf->str + 55); /* Maps to the enum. */
	inc->record_size  = record_size;
	inc->record_count = RL32(buf->str + 60);
	inc->last_record  = RL32S(buf->str + 64);

	sr_dbg("Trigger occured at %lf s.",
		inc->trigger_timestamp * TIMESTAMP_RESOLUTION);
	sr_dbg("File contains %d records: first one is %d, last one is %d.",
		inc->record_count, (inc->last_record - inc->record_count + 1),
		inc->last_record);

	/* Check if we can work with this compression. */
	if (inc->compression != AD_COMPR_NONE) {
		sr_err("File uses unsupported compression (0x%02X), can't continue.",
			inc->compression);
		return SR_ERR;
	}

	inc->header_read = TRUE;

	return SR_OK;
}

static void create_channels(struct sr_input *in)
{
	struct context *inc;
	int pod, channel, chan_id;
	char name[8];

	inc = in->priv;
	chan_id = 0;

	for (pod = 0; pod < MAX_POD_COUNT; pod++) {
		if (!inc->pod_status[pod])
			continue;

		for (channel = 0; channel < 16; channel++) {
			snprintf(name, sizeof(name), "%c%d", get_pod_name_from_id(pod), channel);
			inc->channels[pod][channel] =
				sr_channel_new(in->sdi, chan_id, SR_CHANNEL_LOGIC, TRUE, name);
			chan_id++;
		}

		snprintf(name, sizeof(name), "CLK%c", get_pod_name_from_id(pod));
		inc->channels[pod][16] =
			sr_channel_new(in->sdi, chan_id, SR_CHANNEL_LOGIC, TRUE, name);
		chan_id++;
	}
}

static void send_metadata(struct sr_input *in)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_config *src;
	struct context *inc;

	inc = in->priv;

	packet.type = SR_DF_META;
	packet.payload = &meta;
	src = sr_config_new(SR_CONF_SAMPLERATE, g_variant_new_uint64(inc->samplerate));
	meta.config = g_slist_append(NULL, src);
	sr_session_send(in->sdi, &packet);
	g_slist_free(meta.config);
	sr_config_free(src);

	inc->meta_sent = TRUE;
}

static void flush_output_buffer(struct sr_input *in)
{
	struct context *inc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	inc = in->priv;

	if (inc->out_buf->len) {
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.unitsize = (g_slist_length(in->sdi->channels) + 7) / 8;
		logic.data = inc->out_buf->str;
		logic.length = inc->out_buf->len;
		sr_session_send(in->sdi, &packet);

		g_string_truncate(inc->out_buf, 0);
	}
}

static void process_record_pi(struct sr_input *in, gsize start)
{
	struct sr_datafeed_packet packet;
	struct context *inc;
	uint64_t timestamp, next_timestamp;
	uint32_t pod_data;
	char single_payload[12 * 3];
	GString *buf;
	int i, pod_count, clk_offset, packet_count, pod;
	int payload_bit, payload_len, value;

	inc = in->priv;
	buf = in->buf;

	/*
	 * 00-07 timestamp
	 * 08-09 A15..0
	 * 10-11 B15..0
	 * 12-13 C15..0
	 * 14-15 D15..0
	 * 16-17 E15..0
	 * 18-19 F15..0
	 * 20-23 ??
	 * 24-25 J15..0                         Not present in 500MHz mode
	 * 26-27 K15..0                         Not present in 500MHz mode
	 * 28-29 L15..0                         Not present in 500MHz mode
	 * 30-31 M15..0                         Not present in 500MHz mode
	 * 32-33 N15..0                         Not present in 500MHz mode
	 * 34-35 O15..0                         Not present in 500MHz mode
	 * 36-39 ??                             Not present in 500MHz mode
	 * 40/24 CLKF..A (32=CLKF, .., 1=CLKA)
	 * 41    CLKO..J (32=CLKO, .., 1=CLKJ)  Not present in 500MHz mode
	 * 42/25    ??
	 * 43/26    ??
	 * 44/27    ??
	 */

	timestamp = RL64(buf->str + start);

	if (inc->record_mode == AD_MODE_500MHZ) {
		pod_count = 6;
		clk_offset = 24;
	} else {
		pod_count = 12;
		clk_offset = 40;
	}

	payload_bit = 0;
	payload_len = 0;
	single_payload[0] = 0;

	for (pod = 0; pod < pod_count; pod++) {
		if (!inc->pod_status[pod])
			continue;

		switch (pod) {
		case 0: /* A */
			pod_data = RL16(buf->str + start + 8);
			pod_data |= (RL16(buf->str + start + clk_offset) & 1) << 16;
			break;
		case 1: /* B */
			pod_data = RL16(buf->str + start + 10);
			pod_data |= (RL16(buf->str + start + clk_offset) & 2) << 15;
			break;
		case 2: /* C */
			pod_data = RL16(buf->str + start + 12);
			pod_data |= (RL16(buf->str + start + clk_offset) & 4) << 14;
			break;
		case 3: /* D */
			pod_data = RL16(buf->str + start + 14);
			pod_data |= (RL16(buf->str + start + clk_offset) & 8) << 13;
			break;
		case 4: /* E */
			pod_data = RL16(buf->str + start + 16);
			pod_data |= (RL16(buf->str + start + clk_offset) & 16) << 12;
			break;
		case 5: /* F */
			pod_data = RL16(buf->str + start + 18);
			pod_data |= (RL16(buf->str + start + clk_offset) & 32) << 11;
			break;
		case 6: /* J */
			pod_data = RL16(buf->str + start + 24);
			pod_data |= (RL16(buf->str + start + 41) & 1) << 16;
			break;
		case 7: /* K */
			pod_data = RL16(buf->str + start + 26);
			pod_data |= (RL16(buf->str + start + 41) & 2) << 15;
			break;
		case 8: /* L */
			pod_data = RL16(buf->str + start + 28);
			pod_data |= (RL16(buf->str + start + 41) & 4) << 14;
			break;
		case 9: /* M */
			pod_data = RL16(buf->str + start + 30);
			pod_data |= (RL16(buf->str + start + 41) & 8) << 13;
			break;
		case 10: /* N */
			pod_data = RL16(buf->str + start + 32);
			pod_data |= (RL16(buf->str + start + 41) & 16) << 12;
			break;
		case 11: /* O */
			pod_data = RL16(buf->str + start + 34);
			pod_data |= (RL16(buf->str + start + 41) & 32) << 11;
			break;
		default:
			sr_err("Don't know how to obtain data for pod %d.", pod);
		}

		for (i = 0; i < 17; i++) {
			value = (pod_data >> i) & 1;
			single_payload[payload_len] |= value << payload_bit;

			payload_bit++;
			if (payload_bit > 7) {
				payload_bit = 0;
				payload_len++;
				single_payload[payload_len] = 0;
			}
		}
	}

	/* Make sure that payload_len accounts for any incomplete bytes used. */
	if (payload_bit)
		payload_len++;

	i = (g_slist_length(in->sdi->channels) + 7) / 8;
	if (payload_len != i) {
		sr_err("Payload unit size is %d but should be %d!", payload_len, i);
		return;
	}

	if (timestamp == inc->trigger_timestamp && !inc->trigger_sent) {
		sr_dbg("Trigger @%lf s, record #%d.",
			timestamp * TIMESTAMP_RESOLUTION, inc->cur_record);

		packet.type = SR_DF_TRIGGER;
		packet.payload = NULL;
		sr_session_send(in->sdi, &packet);
		inc->trigger_sent = TRUE;
	}

	/* Is this the last record in the file? */
	if (inc->cur_record == inc->record_count - 1) {
		/* It is, so send the last sample data only once. */
		g_string_append_len(inc->out_buf, single_payload, payload_len);
	} else {
		/* It's not, so fill the time gap by sending lots of data. */
		next_timestamp = RL64(buf->str + start + inc->record_size);
		packet_count = (int)(next_timestamp - timestamp) / inc->timestamp_scale;

		/* Make sure we send at least one data set. */
		if (packet_count == 0)
			packet_count = 1;

		for (i = 0; i < packet_count; i++)
			g_string_append_len(inc->out_buf, single_payload, payload_len);
	}

	if (inc->out_buf->len >= OUTBUF_FLUSH_SIZE)
		flush_output_buffer(in);
}

static void process_record_iprobe(struct sr_input *in, gsize start)
{
	struct sr_datafeed_packet packet;
	struct context *inc;
	uint64_t timestamp, next_timestamp;
	char single_payload[3];
	int i, payload_len, packet_count;

	inc = in->priv;

	/*
	 * 00-07 timestamp
	 * 08-09 IP15..0
	 * 10    CLK
	 */

	timestamp = RL64(in->buf->str + start);
	single_payload[0] = R8(in->buf->str + start + 8);
	single_payload[1] = R8(in->buf->str + start + 9);
	single_payload[2] = R8(in->buf->str + start + 10) & 1;
	payload_len = 3;

	if (timestamp == inc->trigger_timestamp && !inc->trigger_sent) {
		sr_dbg("Trigger @%lf s, record #%d.",
			timestamp * TIMESTAMP_RESOLUTION, inc->cur_record);

		packet.type = SR_DF_TRIGGER;
		packet.payload = NULL;
		sr_session_send(in->sdi, &packet);
		inc->trigger_sent = TRUE;
	}

	/* Is this the last record in the file? */
	if (inc->cur_record == inc->record_count - 1) {
		/* It is, so send the last sample data only once. */
		g_string_append_len(inc->out_buf, single_payload, payload_len);
	} else {
		/* It's not, so fill the time gap by sending lots of data. */
		next_timestamp = RL64(in->buf->str + start + inc->record_size);
		packet_count = (int)(next_timestamp - timestamp) / inc->timestamp_scale;

		/* Make sure we send at least one data set. */
		if (packet_count == 0)
			packet_count = 1;

		for (i = 0; i < packet_count; i++)
			g_string_append_len(inc->out_buf, single_payload, payload_len);
	}

	if (inc->out_buf->len >= OUTBUF_FLUSH_SIZE)
		flush_output_buffer(in);
}

static void process_practice_token(struct sr_input *in, char *cmd_token)
{
	struct context *inc;
	char **tokens;
	char chan_suffix[2], chan_name[33];
	char *s1, *s2;
	int pod, ch;
	struct sr_channel *channel;

	inc = in->priv;

	/*
	 * Commands of interest (I may also be IPROBE):
	 *
	 * I.TWIDTH
	 * I.TPREDELAY
	 * I.TDELAY
	 * I.TYSNC.SELECT I.A0 HIGH
	 * NAME.SET <port.chan> <name> <+/-> ...
	 */

	if (!cmd_token)
		return;

	if (cmd_token[0] == 0)
		return;

	tokens = g_strsplit(cmd_token, " ", 0);

	if (!tokens)
		return;

	if (g_strcmp0(tokens[0], "NAME.SET") == 0) {
		/* Let the user know when the channel has been inverted. */
		/* This *should* be token #3 but there's an additonal space, making it #4. */
		chan_suffix[0] = 0;
		chan_suffix[1] = 0;
		if (tokens[4]) {
			if (tokens[4][0] == '-')
				chan_suffix[0] = '-'; /* This is the way PowerView shows it. */
		}

		/*
		 * Command is using structure "NAME.SET I.A00 I.XYZ" or
		 * "NAME.SET IP.00 IP.XYZ", depending on the device used.
		 * Let's get strings with the I./IP. from both tokens removed.
		 */
		s1 = g_strstr_len(tokens[1], -1, ".") + 1;
		s2 = g_strstr_len(tokens[2], -1, ".") + 1;

		if (g_strcmp0(s1, "CLK") == 0) {
			/* CLK for iprobe */
			pod = 0;
			ch = 16;
		} else if ((strlen(s1) == 4) && g_ascii_isupper(s1[3])) {
			/* CLKA/B/J/K for PowerIntegrator */
			pod = s1[3] - (char)'A';
			ch = 16;
		} else if (g_ascii_isupper(s1[0])) {
			/* A00 for PowerIntegrator */
			pod = s1[0] - (char)'A';
			ch = atoi(s1 + 1);
		} else {
			/* 00 for iprobe */
			pod = 0;
			ch = atoi(s1);
		}

		channel = inc->channels[pod][ch];
		g_snprintf(chan_name, sizeof(chan_name), "%s%s", s2, chan_suffix);

		sr_dbg("Changing channel name for %s to %s.", s1, chan_name);
		sr_dev_channel_name_set(channel, chan_name);
	}

	g_strfreev(tokens);
}

static void process_practice(struct sr_input *in)
{
	char delimiter[3];
	char **tokens, *token;
	int i;

	/* Gather all input data until we see the end marker. */
	if (in->buf->str[in->buf->len - 1] != 0x29)
		return;

	delimiter[0] = 0x0A;
	delimiter[1] = ' ';
	delimiter[2] = 0;

	tokens = g_strsplit(in->buf->str, delimiter, 0);

	/* Special case: first token contains the start marker, too. Skip it. */
	token = tokens[0];
	for (i = 0; token[i]; i++) {
		if (token[i] == ' ')
			process_practice_token(in, token + i + 1);
	}

	for (i = 1; tokens[i]; i++)
		process_practice_token(in, tokens[i]);

	g_strfreev(tokens);

	g_string_erase(in->buf, 0, in->buf->len);
}

static int process_buffer(struct sr_input *in)
{
	struct context *inc;
	int i, chunk_size, res;

	inc = in->priv;

	if (!inc->header_read) {
		res = process_header(in->buf, inc);
		g_string_erase(in->buf, 0, HEADER_SIZE);
		if (res != SR_OK)
			return res;
	}

	if (!inc->meta_sent) {
		std_session_send_df_header(in->sdi);
		send_metadata(in);
	}

	if (!inc->records_read) {
		/* Cut off at a multiple of the record size. */
		chunk_size = ((in->buf->len) / inc->record_size) * inc->record_size;

		/* There needs to be at least one more record process_record() can peek into. */
		chunk_size -= inc->record_size;

		for (i = 0; (i < chunk_size) && (!inc->records_read); i += inc->record_size) {
			switch (inc->device) {
			case AD_DEVICE_PI:
				process_record_pi(in, i);
				break;
			case AD_DEVICE_IPROBE:
				process_record_iprobe(in, i);
				break;
			default:
				sr_err("Trying to process records for unknown device!");
				return SR_ERR;
			}

			inc->cur_record++;
			if (inc->cur_record == inc->record_count)
				inc->records_read = TRUE;
		}

		g_string_erase(in->buf, 0, i);
	}

	if (inc->records_read) {
		/* Read practice commands that configure the setup. */
		process_practice(in);
	}

	return SR_OK;
}

static int receive(struct sr_input *in, GString *buf)
{
	g_string_append_len(in->buf, buf->str, buf->len);

	if (!in->sdi_ready) {
		/* sdi is ready, notify frontend. */
		in->sdi_ready = TRUE;
		return SR_OK;
	}

	return process_buffer(in);
}

static int end(struct sr_input *in)
{
	struct context *inc;
	int ret;

	inc = in->priv;

	if (in->sdi_ready)
		ret = process_buffer(in);
	else
		ret = SR_OK;

	flush_output_buffer(in);

	if (inc->meta_sent)
		std_session_send_df_end(in->sdi);

	return ret;
}

static int reset(struct sr_input *in)
{
	struct context *inc = in->priv;

	inc->meta_sent = FALSE;
	inc->header_read = FALSE;
	inc->records_read = FALSE;
	inc->trigger_sent = FALSE;
	inc->cur_record = 0;

	g_string_truncate(in->buf, 0);

	return SR_OK;
}

static struct sr_option options[] = {
	{ "podA", "Import pod A / iprobe",
		"Create channels and data for pod A / iprobe", NULL, NULL },

	{ "podB", "Import pod B", "Create channels and data for pod B", NULL, NULL },
	{ "podC", "Import pod C", "Create channels and data for pod C", NULL, NULL },
	{ "podD", "Import pod D", "Create channels and data for pod D", NULL, NULL },
	{ "podE", "Import pod E", "Create channels and data for pod E", NULL, NULL },
	{ "podF", "Import pod F", "Create channels and data for pod F", NULL, NULL },
	{ "podJ", "Import pod J", "Create channels and data for pod J", NULL, NULL },
	{ "podK", "Import pod K", "Create channels and data for pod K", NULL, NULL },
	{ "podL", "Import pod L", "Create channels and data for pod L", NULL, NULL },
	{ "podM", "Import pod M", "Create channels and data for pod M", NULL, NULL },
	{ "podN", "Import pod N", "Create channels and data for pod N", NULL, NULL },
	{ "podO", "Import pod O", "Create channels and data for pod O", NULL, NULL },

	{ "samplerate", "Reduced sample rate (MHz)", "Reduce the original sample rate of 12.8 GHz to the specified sample rate in MHz", NULL, NULL },

	ALL_ZERO
};

static const struct sr_option *get_options(void)
{
	if (!options[0].def) {
		options[0].def = g_variant_ref_sink(g_variant_new_boolean(TRUE));
		options[1].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[2].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[3].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[4].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[5].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[6].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[7].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[8].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[9].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[10].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[11].def = g_variant_ref_sink(g_variant_new_boolean(FALSE));
		options[12].def = g_variant_ref_sink(g_variant_new_uint32(DEFAULT_SAMPLERATE));
	}

	return options;
}

SR_PRIV struct sr_input_module input_trace32_ad = {
	.id = "trace32_ad",
	.name = "Trace32_ad",
	.desc = "Lauterbach Trace32 logic analyzer data",
	.exts = (const char*[]){"ad", NULL},
	.options = get_options,
	.metadata = { SR_INPUT_META_HEADER | SR_INPUT_META_REQUIRED },
	.format_match = format_match,
	.init = init,
	.receive = receive,
	.end = end,
	.reset = reset,
};
