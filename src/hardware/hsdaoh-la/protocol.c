/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Do Van Dung <do_van_dzung@yahoo.com>
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
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "protocol.h"

/* ------------------------------------------------------------- trigger */

/*
 * Build a 64K lookup table from the first trigger stage:
 * tbl[(previous sample << 8) | sample] is 1 where all matches are true.
 * One table lookup per sample keeps the trigger search fast enough for
 * the full stream rate. Only one stage is supported.
 */
SR_PRIV int hsdaoh_la_build_trigger(struct dev_context *devc, struct sr_trigger *trigger)
{
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	uint8_t must_zero = 0, must_one = 0, rising = 0, falling = 0, edge = 0;
	GSList *l;

	devc->have_trigger = FALSE;

	if (!trigger || !trigger->stages)
		return SR_OK;

	if (g_slist_length(trigger->stages) > 1)
		sr_warn("Only one trigger stage is supported, using the first.");

	stage = trigger->stages->data;
	for (l = stage->matches; l; l = l->next) {
		match = l->data;
		if (!match->channel->enabled)
			continue;
		if (match->channel->index < 0 || match->channel->index >= (int)devc->channels) {
			sr_err("Trigger on D%d, but the device has %u channels.",
				match->channel->index, devc->channels);
			return SR_ERR_ARG;
		}
		uint8_t m = 1 << match->channel->index;
		switch (match->match) {
		case SR_TRIGGER_ZERO:    must_zero |= m; break;
		case SR_TRIGGER_ONE:     must_one |= m; break;
		case SR_TRIGGER_RISING:  rising |= m; break;
		case SR_TRIGGER_FALLING: falling |= m; break;
		case SR_TRIGGER_EDGE:    edge |= m; break;
		default:
			sr_err("Unsupported trigger match %d.", match->match);
			return SR_ERR_ARG;
		}
	}

	if (!(must_zero | must_one | rising | falling | edge))
		return SR_OK;		/* nothing to wait for */

	devc->trig_zero = must_zero;
	devc->trig_one = must_one;
	devc->trig_rise = rising;
	devc->trig_fall = falling;
	devc->trig_edge = edge;
	devc->have_trigger = TRUE;
	return SR_OK;
}

/* ------------------------------------------------------------- capture */

static void nibble_tbl_init(void);

/* forget the pre-trigger history (after a gap in the stream) */
static void hist_reset(struct dev_context *devc)
{
	devc->hist_pos = 0;
	devc->hist_len = 0;
	devc->have_prev = FALSE;
}

static void hist_push(struct dev_context *devc, const uint8_t *s, size_t n)
{
	uint64_t cap = devc->pre;

	if (!cap)
		return;
	if (n >= cap) {
		memcpy(devc->hist, s + n - cap, (size_t)cap);
		devc->hist_pos = 0;
		devc->hist_len = cap;
		return;
	}
	while (n) {
		size_t k = MIN(n, cap - devc->hist_pos);
		memcpy(devc->hist + devc->hist_pos, s, k);
		devc->hist_pos = (devc->hist_pos + k) % cap;
		devc->hist_len = MIN(devc->hist_len + k, cap);
		s += k;
		n -= k;
	}
}

/* copy the newest 'want' history samples into the capture buffer */
static uint64_t hist_copy(struct dev_context *devc, uint8_t *dst, uint64_t want)
{
	uint64_t cap = devc->pre;
	uint64_t n = MIN(want, devc->hist_len);
	uint64_t start = (devc->hist_pos + cap - n) % cap;
	uint64_t first = MIN(n, cap - start);

	memcpy(dst, devc->hist + start, (size_t)first);
	memcpy(dst + first, devc->hist, (size_t)(n - first));
	return n;
}

SR_PRIV void hsdaoh_la_reset_capture(struct dev_context *devc)
{
	hist_reset(devc);
	devc->buf_fill = 0;
	devc->trigger_pos = 0;
	devc->cnt_valid = FALSE;
	devc->gaps = 0;
	devc->state = HL_ARMED;
	nibble_tbl_init();
}

/*
 * Bits of a sample 'c' (after 'p') that violate the trigger; 0 = match.
 * Works on one byte or, bytewise, on 8 samples packed in a uint64_t.
 */
#define TRIG_BAD(c, p, z, o, r, f, e) \
	(((c) & (z)) | ((o) & ~(c)) | ((r) & ((p) | ~(c))) | \
	 ((f) & (~(p) | (c))) | ((e) & ~((p) ^ (c))))

#define BYTES8(x)	((uint64_t)(x) * UINT64_C(0x0101010101010101))

static inline uint64_t load_u64(const uint8_t *p)
{
	uint64_t v;
	memcpy(&v, p, sizeof(v));	/* unaligned load, one instruction on x86 */
	return v;
}

static inline unsigned int ctz64(uint64_t v)
{
#if defined(__GNUC__) || defined(__clang__)
	return (unsigned int)__builtin_ctzll(v);
#else
	unsigned int n = 0;
	while (!(v & 1)) { v >>= 1; n++; }
	return n;
#endif
}

/*
 * Index of the first sample in s[from..n) that meets the trigger, or n.
 * 8 samples are tested at once (bytewise logic on 64-bit words, no SIMD
 * instructions needed): a sample matches when its 'bad' byte is 0, and the
 * lowest zero byte of a word is found with the usual (v - 0x01..) & ~v &
 * 0x80.. test, exact for the lowest byte (= earliest sample, little endian).
 */
static size_t find_trigger(struct dev_context *devc, const uint8_t *s, size_t from, size_t n)
{
	const unsigned int z = devc->trig_zero, o = devc->trig_one, r = devc->trig_rise,
		f = devc->trig_fall, e = devc->trig_edge;
	unsigned int p;
	size_t j;

	if (from >= n)
		return n;
	/* the sample before s[from]; without history the first sample cannot
	 * be an edge: use itself */
	if (from)
		p = s[from - 1];
	else
		p = devc->have_prev ? devc->prev : s[0];
	j = from;
	if (!(TRIG_BAD((unsigned int)s[j], p, z, o, r, f, e) & 0xff))
		return j;
	j++;

#if G_BYTE_ORDER == G_LITTLE_ENDIAN
	{
		const uint64_t Z = BYTES8(z), O = BYTES8(o), R = BYTES8(r),
			F = BYTES8(f), E = BYTES8(e);
		const uint64_t ONES = BYTES8(0x01), HIGHS = BYTES8(0x80);

		/* s[j - 1] exists from here on */
		for (; j + 8 <= n; j += 8) {
			uint64_t c = load_u64(s + j), pv = load_u64(s + j - 1);
			uint64_t bad = TRIG_BAD(c, pv, Z, O, R, F, E);
			uint64_t hit = (bad - ONES) & ~bad & HIGHS;
			if (hit)
				return j + (ctz64(hit) >> 3);
		}
	}
#endif
	for (; j < n; j++)
		if (!(TRIG_BAD((unsigned int)s[j], (unsigned int)s[j - 1], z, o, r, f, e) & 0xff))
			return j;
	return n;
}

/* la4 unpacking: byte -> its two nibbles, high nibble (earlier sample) first */
static uint16_t nibble_tbl[256];

static void nibble_tbl_init(void)
{
	if (nibble_tbl[0x10])
		return;
	for (unsigned int b = 0; b < 256; b++)
		nibble_tbl[b] = (uint16_t)((b >> 4) | ((b & 0x0f) << 8));	/* little endian */
}

/*
 * Unpack 'words' 16-bit link words (little endian) to one byte per sample:
 * spw 2 (la8): high byte = earlier sample; spw 4 (la4): bits 15:12 first.
 */
static void unpack_words(uint8_t *dst, const uint8_t *src, size_t words, size_t spw)
{
	size_t w = 0;

	if (spw == 4) {
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
		/* 2 table lookups and one 32-bit store per word */
		for (; w < words; w++) {
			uint32_t v = (uint32_t)nibble_tbl[src[2 * w + 1]] |
				((uint32_t)nibble_tbl[src[2 * w]] << 16);
			memcpy(dst + 4 * w, &v, 4);
		}
#else
		for (; w < words; w++) {
			uint8_t hi = src[2 * w + 1], lo = src[2 * w];
			dst[4 * w] = hi >> 4;
			dst[4 * w + 1] = hi & 0x0f;
			dst[4 * w + 2] = lo >> 4;
			dst[4 * w + 3] = lo & 0x0f;
		}
#endif
		return;
	}
	/* la8: swap the two bytes of every word, 4 words at a time (the swap
	 * within 16-bit lanes does not depend on the byte order) */
	for (; w + 4 <= words; w += 4) {
		uint64_t v = load_u64(src + 2 * w);
		v = ((v >> 8) & UINT64_C(0x00ff00ff00ff00ff)) |
		    ((v << 8) & UINT64_C(0xff00ff00ff00ff00));
		memcpy(dst + 2 * w, &v, 8);
	}
	for (; w < words; w++) {
		dst[2 * w] = src[2 * w + 1];
		dst[2 * w + 1] = src[2 * w];
	}
}

static void start_capture(struct dev_context *devc, const uint8_t *s, size_t at)
{
	/* pre-trigger: history from earlier buffers plus this buffer up to 'at' */
	uint64_t want = devc->pre;
	uint64_t from_buf = MIN((uint64_t)at, want);
	uint64_t from_hist = 0;

	if (want > from_buf)
		from_hist = hist_copy(devc, devc->buf, want - from_buf);
	memcpy(devc->buf + from_hist, s + at - from_buf, (size_t)from_buf);
	devc->buf_fill = from_hist + from_buf;
	devc->trigger_pos = devc->buf_fill;
	devc->state = HL_CAPTURING;
}

static void process(struct dev_context *devc, const uint8_t *s, size_t n)
{
	size_t i = 0;

	if (devc->state == HL_ARMED) {
		size_t at = 0;
		if (devc->have_trigger) {
			/*
			 * Like other logic analyzers: a trigger only counts once
			 * the pre-trigger part is complete, i.e. at least 'pre'
			 * samples were received (since Run, or since lost data).
			 * Else a busy signal triggers at once with (almost) no
			 * samples before the trigger.
			 */
			size_t from = 0;
			if (devc->hist_len < devc->pre) {
				uint64_t need = devc->pre - devc->hist_len;
				from = need < n ? (size_t)need : n;
			}
			at = find_trigger(devc, s, from, n);
		}
		if (at >= n) {
			hist_push(devc, s, n);
			devc->prev = s[n - 1];
			devc->have_prev = TRUE;
			return;
		}
		start_capture(devc, s, at);
		i = at;
	}

	if (devc->state == HL_CAPTURING) {
		uint64_t room = devc->pre + devc->post - devc->buf_fill;
		/* never take more pre-trigger samples than exist: the capture
		 * ends post samples after the trigger */
		uint64_t until_end = devc->trigger_pos + devc->post - devc->buf_fill;
		uint64_t k = MIN(MIN(room, until_end), (uint64_t)(n - i));
		memcpy(devc->buf + devc->buf_fill, s + i, (size_t)k);
		devc->buf_fill += k;
		if (devc->buf_fill >= devc->trigger_pos + devc->post)
			devc->state = HL_DONE;
	}
}

/*
 * Called by libhsdaoh from its own thread with each received buffer.
 * The firmware packs two samples per 16-bit word, the earlier sample in
 * the high byte; the words arrive little endian.
 */
SR_PRIV void hsdaoh_la_data_cb(hsdaoh_data_info_t *info)
{
	struct sr_dev_inst *sdi = info->ctx;
	struct dev_context *devc = sdi->priv;
	size_t words, n, spw;
	uint64_t cnt;

	if (info->stream_id != 0)
		return;

	g_mutex_lock(&devc->mutex);

	if (info->device_error) {
		devc->stream_error = TRUE;
		if (devc->state == HL_ARMED || devc->state == HL_CAPTURING)
			devc->state = HL_FAILED;
		goto out;
	}
	/* just after a sample rate change: these may still hold old-rate samples */
	if (devc->skip_bufs > 0) {
		devc->skip_bufs--;
		devc->cnt_valid = FALSE;
		goto out;
	}
	if (devc->state != HL_ARMED && devc->state != HL_CAPTURING)
		goto out;

	if (info->srate)
		devc->stream_srate = info->srate;

	words = info->len / 2;
	if (!words)
		goto out;

#if HSDAOH_LA_HAVE_FORMAT
	spw = info->format == HSDAOH_FORMAT_PIO_4BIT ? 4 : 2;
#else
	spw = 2;
#endif

	/* lost data: the device's word counter does not continue */
	cnt = info->data_cnt;
	if (devc->cnt_valid && cnt != devc->next_cnt) {
		devc->gaps++;
		sr_warn("Data lost in the HDMI stream (%" PRIu64 " words).",
			(uint64_t)(cnt - devc->next_cnt));
		if (devc->state == HL_CAPTURING) {
			/* the capture would contain a hole: start over */
			sr_warn("Discarding the capture, waiting for the trigger again.");
			devc->state = HL_ARMED;
			devc->buf_fill = 0;
		}
		hist_reset(devc);
	}
	devc->next_cnt = cnt + words;
	devc->cnt_valid = TRUE;

	/*
	 * Capturing (after the trigger): unpack straight into the capture
	 * buffer, no intermediate copy. Whole words only; a last partial word
	 * goes through the scratch buffer below.
	 */
	if (devc->state == HL_CAPTURING) {
		uint64_t need = devc->trigger_pos + devc->post - devc->buf_fill;
		size_t direct = (size_t)MIN((uint64_t)words, need / spw);
		unpack_words(devc->buf + devc->buf_fill, info->buf, direct, spw);
		devc->buf_fill += (uint64_t)direct * spw;
		if (devc->buf_fill >= devc->trigger_pos + devc->post) {
			devc->state = HL_DONE;
			goto out;
		}
		if (direct == words)
			goto out;
		/* the capture ends inside the next word */
		uint8_t last[4];
		unpack_words(last, info->buf + 2 * direct, 1, spw);
		need = devc->trigger_pos + devc->post - devc->buf_fill;
		memcpy(devc->buf + devc->buf_fill, last, (size_t)need);
		devc->buf_fill += need;
		devc->state = HL_DONE;
		goto out;
	}

	n = words * spw;
	if (n > devc->unpack_size) {
		g_free(devc->unpack);
		devc->unpack = g_try_malloc(n);
		devc->unpack_size = devc->unpack ? n : 0;
		if (!devc->unpack) {
			devc->state = HL_FAILED;
			goto out;
		}
	}
	unpack_words(devc->unpack, info->buf, words, spw);
	process(devc, devc->unpack, n);

out:
	g_mutex_unlock(&devc->mutex);
}

/* ---------------------------------------------------------- send to session */

static void send_logic(const struct sr_dev_inst *sdi, const uint8_t *data, uint64_t len)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	while (len) {
		uint64_t k = MIN(len, (uint64_t)HSDAOH_LA_SEND_CHUNK);
		logic.length = k;
		logic.unitsize = 1;
		logic.data = (void *)data;
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		sr_session_send(sdi, &packet);
		data += k;
		len -= k;
	}
}

static void send_samplerate(const struct sr_dev_inst *sdi, uint64_t rate)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_meta meta;
	struct sr_config *src;

	src = sr_config_new(SR_CONF_SAMPLERATE, g_variant_new_uint64(rate));
	meta.config = g_slist_append(NULL, src);
	packet.type = SR_DF_META;
	packet.payload = &meta;
	sr_session_send(sdi, &packet);
	g_slist_free(meta.config);
	sr_config_free(src);
}

/*
 * Send the collected samples to the session: pre-trigger part, trigger
 * marker, rest. With complete == FALSE (stopped by the user) whatever was
 * collected after the trigger is sent.
 */
SR_PRIV void hsdaoh_la_send_capture(const struct sr_dev_inst *sdi, gboolean complete)
{
	struct dev_context *devc = sdi->priv;

	if (devc->stream_srate && devc->stream_srate != devc->samplerate) {
		devc->samplerate = devc->stream_srate;
		send_samplerate(sdi, devc->samplerate);
	}

	if (!devc->buf_fill) {
		sr_info("No samples captured.");
		return;
	}
	if (!complete)
		sr_info("Acquisition stopped, sending %" PRIu64 " samples.", devc->buf_fill);

	send_logic(sdi, devc->buf, devc->trigger_pos);
	if (devc->have_trigger)
		std_session_send_df_trigger(sdi);
	send_logic(sdi, devc->buf + devc->trigger_pos, devc->buf_fill - devc->trigger_pos);
}

/*
 * End the acquisition: send what was captured if send is set, close the
 * data feed. The HDMI stream keeps running (it is stopped in dev_close());
 * once the state is HL_IDLE the libhsdaoh thread no longer touches the
 * capture buffers. Also used by dev_acquisition_stop() (Stop button).
 */
SR_PRIV void hsdaoh_la_end_acquisition(const struct sr_dev_inst *sdi, gboolean send)
{
	struct dev_context *devc = sdi->priv;
	gboolean complete;
	int state;

	sr_session_source_remove(sdi->session, -1);

	g_mutex_lock(&devc->mutex);
	state = devc->state;
	devc->state = HL_IDLE;
	g_mutex_unlock(&devc->mutex);

	complete = (state == HL_DONE);
	if (send && (complete || state == HL_CAPTURING))
		hsdaoh_la_send_capture(sdi, complete);
	if (devc->gaps)
		sr_info("%" PRIu64 " gap(s) in the HDMI stream during the acquisition.",
			devc->gaps);

	std_session_send_df_end(sdi);

	g_mutex_lock(&devc->mutex);
	g_free(devc->buf);
	devc->buf = NULL;
	g_free(devc->hist);
	devc->hist = NULL;
	g_free(devc->unpack);
	devc->unpack = NULL;
	devc->unpack_size = 0;
	g_mutex_unlock(&devc->mutex);
}

/*
 * Session callback (timer): wait for the libhsdaoh thread to finish the
 * capture, then send everything and end the acquisition.
 */
SR_PRIV int hsdaoh_la_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc;
	int state;

	(void)fd;
	(void)revents;

	if (!sdi || !(devc = sdi->priv))
		return TRUE;

	g_mutex_lock(&devc->mutex);
	state = devc->state;
	g_mutex_unlock(&devc->mutex);

	if (state == HL_DONE) {
		hsdaoh_la_end_acquisition(sdi, TRUE);
	} else if (state == HL_FAILED) {
		sr_err("hsdaoh device error, acquisition aborted.");
		hsdaoh_la_end_acquisition(sdi, FALSE);
	}

	return TRUE;
}

/* ------------------------------------------------------- Pico serial port */

/*
 * Send one command to the la8 firmware and collect its answer. The status
 * answer ends with the "commands:" help line; a refused divider is a
 * single "divider must be ..." line.
 */
SR_PRIV int hsdaoh_la_pico_command(struct dev_context *devc, const char *cmd,
		char *reply, size_t reply_size)
{
	struct sr_serial_dev_inst *serial = devc->serial;
	char line[64];
	size_t len = 0;
	gint64 deadline;
	int ret = SR_ERR;

	if (!serial || reply_size < 2)
		return SR_ERR_NA;

	if (serial_open(serial, SERIAL_RDWR) != SR_OK) {
		sr_err("Cannot open the Pico's serial port %s.", serial->port);
		return SR_ERR_IO;
	}

	/* drop anything printed earlier */
	while (serial_read_blocking(serial, reply, reply_size - 1, 100) > 0)
		;

	snprintf(line, sizeof(line), "%s\r", cmd);
	if (serial_write_blocking(serial, line, strlen(line), 500) < (int)strlen(line)) {
		sr_err("Writing to %s failed.", serial->port);
		ret = SR_ERR_IO;
		goto out;
	}

	deadline = g_get_monotonic_time() + 2 * G_USEC_PER_SEC;
	while (g_get_monotonic_time() < deadline && len < reply_size - 1) {
		int n = serial_read_blocking(serial, reply + len, reply_size - 1 - len, 50);
		if (n < 0)
			break;
		len += (size_t)n;
		reply[len] = 0;
		if (strstr(reply, "commands:")) {
			ret = (strstr(reply, "divider must be") || strstr(reply, "unknown command"))
				? SR_ERR_ARG : SR_OK;
			break;
		}
		if (strstr(reply, "divider must be") && strchr(strstr(reply, "divider must be"), '\n')) {
			ret = SR_ERR_ARG;
			break;
		}
		if (strstr(reply, "channels must be") && strchr(strstr(reply, "channels must be"), '\n')) {
			ret = SR_ERR_ARG;
			break;
		}
	}
	reply[len] = 0;
	if (!len)
		sr_err("No answer from the Pico on %s (is la8 or la4 running?).", serial->port);
	else
		sr_dbg("Pico answer to '%s': %s", cmd, reply);

out:
	serial_close(serial);
	return ret;
}

/*
 * Parse the la8 / la4 / la48 status, e.g.
 * "la8: GP0..GP7, 140.000 MS/s (sys 280 MHz / 2), minimum divider 2, HDMI 384 MHz (USB PLL)"
 * "la4: GP0..GP3, 280.000 MS/s (sys 280 MHz / 1), minimum divider 1, HDMI 384 MHz (USB PLL)"
 * la48 prints one of these for its current mode, then
 * "la48 switchable: ch 8 (GP0..GP7, minimum divider 2) or ch 4 (GP0..GP3, minimum divider 1)"
 */
SR_PRIV int hsdaoh_la_pico_parse_status(struct dev_context *devc, const char *reply)
{
	const char *p;
	unsigned int sys_mhz, div, min_div;

	p = strstr(reply, "(sys ");
	if (!p || sscanf(p, "(sys %u MHz / %u)", &sys_mhz, &div) != 2 || !sys_mhz || !div)
		return SR_ERR_DATA;
	p = strstr(reply, "minimum divider ");
	if (!p || sscanf(p, "minimum divider %u", &min_div) != 1 || !min_div)
		return SR_ERR_DATA;

	devc->sys_clock = (uint64_t)sys_mhz * 1000000;
	devc->cur_div = div;
	devc->min_div = min_div;
	devc->fw_channels = strstr(reply, "la4:") ? 4 : 8;

	/* la48: "la48 switchable: ch 8 (GP0..GP7, minimum divider 2) or
	 * ch 4 (GP0..GP3, minimum divider 1)" */
	p = strstr(reply, "la48 switchable:");
	if (p) {
		unsigned int m8, m4;
		const char *p8 = strstr(p, "ch 8 ("), *p4 = strstr(p, "ch 4 (");
		if (p8 && p4 && (p8 = strstr(p8, "minimum divider ")) && (p4 = strstr(p4, "minimum divider ")) &&
		    sscanf(p8, "minimum divider %u", &m8) == 1 && sscanf(p4, "minimum divider %u", &m4) == 1 &&
		    m8 && m4) {
			devc->switchable = TRUE;
			devc->min_div8 = m8;
			devc->min_div4 = m4;
			devc->min_div = MIN(m8, m4);	/* rates down from the 4 channel maximum */
		}
	}
	devc->samplerate = hsdaoh_la_rate(devc->sys_clock, div);
	return SR_OK;
}
