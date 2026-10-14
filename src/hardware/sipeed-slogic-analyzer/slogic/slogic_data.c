/*
 * libslogic — pure data layer: sample format, trigger, sample accounting,
 * throughput fit and the standard capture report.
 *
 * Copyright (C) 2023-2025 Shenzhen Sipeed Technology Co., Ltd.
 * (深圳市矽速科技有限公司) <support@sipeed.com>
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
 *
 * No libusb, no threads: everything here is driven by the capture engine
 * (slogic_usb.c) or by the unit tests with a fake producer.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slogic.h"

/* ---------------------------------------------------------------------- */
/* link speed and rate caps                                                */
/* ---------------------------------------------------------------------- */

const char *slogic_link_name(slogic_link link)
{
	switch (link) {
	case SLOGIC_LINK_FULL: return "FULL(USB1.1)";
	case SLOGIC_LINK_HIGH: return "HIGH(USB2.0)";
	case SLOGIC_LINK_SUPER: return "SUPER(USB3-5G)";
	case SLOGIC_LINK_SUPER_PLUS: return "SUPER+(USB3-10G)";
	case SLOGIC_LINK_UNKNOWN:
	default: return "unknown";
	}
}

const char *slogic_link_tag(slogic_link link)
{
	switch (link) {
	case SLOGIC_LINK_SUPER_PLUS: return "10G";
	case SLOGIC_LINK_SUPER: return "5G";
	case SLOGIC_LINK_HIGH: return "USB2";
	case SLOGIC_LINK_FULL: return "USB1";
	case SLOGIC_LINK_UNKNOWN:
	default: return NULL;
	}
}

/* Sustained payload a link carries for the U3 stream, in bits/s:
 * 5G ~ 400 MB/s, USB2 ~ 40 MB/s. 10G is not the limit (the device is). */
static uint64_t link_payload_bps(slogic_link link)
{
	switch (link) {
	case SLOGIC_LINK_SUPER: return SLOGIC_MHZ(3200);
	case SLOGIC_LINK_HIGH:
	case SLOGIC_LINK_FULL: return SLOGIC_MHZ(320);
	default: return 0;
	}
}

uint64_t slogic_link_max_rate(const slogic_model *m, int nch, slogic_link link)
{
	uint64_t lim = slogic_max_rate(m, nch);
	uint64_t bps = link_payload_bps(link);

	/* A U3 analyzer on a slower link than it was built for (32U3 on 5G,
	 * any U3 on USB2) is capped at what that link sustains for nch
	 * channels: 32U3 on 5G gives 4ch@800 8ch@400 16ch@200 32ch@100. */
	if (lim && m->proto == SLOGIC_PROTO_U3 && bps) {
		uint64_t cap = bps / (uint64_t)nch;
		if (lim > cap)
			lim = cap;
	}
	return lim;
}

uint64_t slogic_pick_rate(const slogic_model *m, int nch, slogic_link link,
			  uint64_t want, int exact)
{
	uint64_t cap = slogic_link_max_rate(m, nch, link);
	uint64_t best = 0, best_err = UINT64_MAX;
	size_t i;

	if (!cap)
		return 0;
	if (exact) {
		for (i = 0; i < m->rate_count; i++)
			if (m->rates[i] == want && want <= cap)
				return want;
		return 0;
	}
	if (want == 0 || want > cap)
		want = cap;
	for (i = 0; i < m->rate_count; i++) {
		uint64_t r = m->rates[i], err;
		if (r > cap)
			continue;
		err = r > want ? r - want : want - r;
		if (err < best_err) {
			best_err = err;
			best = r;
		}
	}
	return best ? best : m->rates[0];
}

size_t slogic_rate_list(const slogic_model *m, int nch, slogic_link link,
			uint64_t *out, size_t cap)
{
	uint64_t lim = slogic_link_max_rate(m, nch, link);
	size_t i, n = 0;

	for (i = 0; i < m->rate_count && n < cap; i++)
		if (m->rates[i] <= lim)
			out[n++] = m->rates[i];
	return n;
}

/* ---------------------------------------------------------------------- */
/* sample format                                                           */
/* ---------------------------------------------------------------------- */

unsigned slogic_unitsize(int nch)
{
	return nch <= 8 ? 1u : (unsigned)(nch / 8);
}

uint64_t slogic_bytes_for_samples(int nch, uint64_t samples)
{
	return (samples * (uint64_t)nch + 7) / 8;
}

uint64_t slogic_samples_in_bytes(int nch, uint64_t bytes)
{
	return bytes * 8 / (uint64_t)nch;
}

size_t slogic_unpack_range(int nch, const uint8_t *packed, uint64_t first,
			   size_t nsamples, uint8_t *out)
{
	unsigned spb, k;
	uint8_t mask;
	size_t i = 0;
	const uint8_t *p;

	if (nch >= 8) {
		unsigned unit = (unsigned)nch / 8;
		memcpy(out, packed + first * unit, nsamples * unit);
		return nsamples;
	}

	spb = 8u / (unsigned)nch;
	mask = (uint8_t)((1u << nch) - 1u);

	/* Leading samples up to a byte boundary. */
	while (i < nsamples && (first % spb)) {
		out[i++] = (packed[first / spb] >> ((first % spb) * nch)) & mask;
		first++;
	}
	/* Whole bytes. */
	p = packed + first / spb;
	if (nch == 4) {
		while (nsamples - i >= 2) {
			uint8_t b = *p++;
			out[i] = b & 0x0f;
			out[i + 1] = b >> 4;
			i += 2;
		}
	} else {
		while (nsamples - i >= spb) {
			uint8_t b = *p++;
			for (k = 0; k < spb; k++)
				out[i + k] = (b >> (k * nch)) & mask;
			i += spb;
		}
	}
	/* Trailing partial byte: only the samples asked for are read. */
	for (k = 0; i < nsamples; k++)
		out[i++] = (*p >> (k * nch)) & mask;
	return nsamples;
}

/* ---------------------------------------------------------------------- */
/* trigger                                                                 */
/* ---------------------------------------------------------------------- */

int slogic_trigger_spec_empty(const slogic_trigger_spec *spec)
{
	int i;

	if (!spec)
		return 1;
	for (i = 0; i < SLOGIC_MAX_CHANNELS; i++)
		if (spec->match[i] != SLOGIC_TRIG_X)
			return 0;
	return 1;
}

static uint64_t bcast(uint32_t v, unsigned bits)
{
	uint64_t r = 0;
	unsigned k;

	for (k = 0; k + bits <= 64; k += bits)
		r |= (uint64_t)v << k;
	return r;
}

int slogic_trigger_compile(slogic_trigger *t, const slogic_trigger_spec *spec,
			   int nch)
{
	int i;

	if (!t || !spec || nch < 1 || nch > SLOGIC_MAX_CHANNELS)
		return SLOGIC_ERR_ARG;
	memset(t, 0, sizeof(*t));
	t->nch = nch;
	t->bits = nch < 8 ? (unsigned)nch : 8u * slogic_unitsize(nch);
	t->smask = nch >= 32 ? 0xffffffffu : ((1u << nch) - 1u);
	for (i = 0; i < SLOGIC_MAX_CHANNELS; i++) {
		uint32_t b = (uint32_t)1 << i;
		if (spec->match[i] == SLOGIC_TRIG_X)
			continue;
		if (i >= nch)
			return SLOGIC_ERR_ARG; /* channel not captured in this mode */
		switch (spec->match[i]) {
		case SLOGIC_TRIG_ZERO: t->lvl_mask |= b; break;
		case SLOGIC_TRIG_ONE: t->lvl_mask |= b; t->lvl_val |= b; break;
		case SLOGIC_TRIG_RISING: t->rise |= b; break;
		case SLOGIC_TRIG_FALLING: t->fall |= b; break;
		case SLOGIC_TRIG_EDGE: t->any |= b; break;
		default: return SLOGIC_ERR_ARG;
		}
	}
	t->edge_all = t->rise | t->fall | t->any;
	/* A word in which none of these bits change cannot hold a match: for an
	 * edge trigger nothing toggled, for a level trigger every sample equals
	 * the previous (already rejected) one on the level channels. */
	t->edge_bcast = bcast(t->edge_all ? t->edge_all : t->lvl_mask, t->bits);
	return SLOGIC_OK;
}

void slogic_trigger_reset(slogic_trigger *t)
{
	t->prev = 0;
	t->have_prev = 0;
}

static inline int trig_match(const slogic_trigger *t, uint32_t p, uint32_t s)
{
	return ((s ^ t->lvl_val) & t->lvl_mask) == 0 &&
	       (t->rise & ~(~p & s)) == 0 &&
	       (t->fall & ~(p & ~s)) == 0 &&
	       (t->any & ~(p ^ s)) == 0;
}

static inline uint32_t get_sample(const slogic_trigger *t, const uint8_t *p,
				  uint64_t idx)
{
	if (t->nch < 8) {
		unsigned spb = 8u / (unsigned)t->nch;
		return (p[idx / spb] >> ((idx % spb) * t->nch)) & t->smask;
	} else {
		unsigned unit = t->bits / 8, k;
		uint32_t v = 0;
		const uint8_t *q = p + idx * unit;
		for (k = 0; k < unit; k++)
			v |= (uint32_t)q[k] << (8 * k);
		return v;
	}
}

int64_t slogic_trigger_scan(slogic_trigger *t, const uint8_t *packed, size_t len,
			    uint64_t first)
{
	uint64_t n, i = first, spw;
	unsigned w = t->bits;
	uint64_t lane = w >= 32 ? 0xffffffffull : ((1ull << w) - 1ull);

	n = t->nch < 8 ? (uint64_t)len * (8u / (unsigned)t->nch)
		       : (uint64_t)len / (w / 8);
	if (i >= n)
		return -1;

	if (!t->have_prev) {
		uint32_t s0 = get_sample(t, packed, i);
		/* No previous sample: only a pure level trigger can fire here. */
		if (!t->edge_all && ((s0 ^ t->lvl_val) & t->lvl_mask) == 0)
			return (int64_t)i;
		t->prev = s0;
		t->have_prev = 1;
		i++;
	}

	spw = 64 / w; /* samples per u64 word */
	/* Single samples up to a word boundary. */
	for (; i < n && (i % spw); i++) {
		uint32_t s = get_sample(t, packed, i);
		if (trig_match(t, t->prev, s))
			return (int64_t)i;
		t->prev = s;
	}
	/* Whole words: skip any word where the relevant bits never change. */
	for (; i + spw <= n; i += spw) {
		uint64_t x, y;
		unsigned k;
		memcpy(&x, packed + i * w / 8, 8); /* little-endian hosts */
		y = (x << w) | t->prev;
		if (((x ^ y) & t->edge_bcast) == 0) {
			t->prev = (uint32_t)((x >> (64 - w)) & lane);
			continue;
		}
		for (k = 0; k < spw; k++) {
			uint32_t s = (uint32_t)((x >> (k * w)) & lane);
			if (trig_match(t, t->prev, s))
				return (int64_t)(i + k);
			t->prev = s;
		}
	}
	/* Tail. */
	for (; i < n; i++) {
		uint32_t s = get_sample(t, packed, i);
		if (trig_match(t, t->prev, s))
			return (int64_t)i;
		t->prev = s;
	}
	return -1;
}

/* ---------------------------------------------------------------------- */
/* sample accounting                                                       */
/* ---------------------------------------------------------------------- */

#define ACC_SCRATCH_SAMPLES (1u << 20)
#define ACC_PRE_MAX_BYTES   (UINT64_C(1) << 30)

int slogic_acc_init(slogic_acc *a, int nch, uint64_t limit,
		    const slogic_trigger_spec *trig, uint64_t pretrigger,
		    unsigned first_drop_bytes, const slogic_sink *sink)
{
	if (!a || (nch != 1 && nch != 2 && nch != 4 && nch != 8 && nch != 16 &&
		   nch != 32))
		return SLOGIC_ERR_ARG;
	memset(a, 0, sizeof(*a));
	a->nch = nch;
	a->unit = slogic_unitsize(nch);
	a->wire_unit = nch >= 8 ? (unsigned)nch / 8 : 0;
	a->limit = limit;
	a->drop_init = a->drop_left = first_drop_bytes;
	if (sink)
		a->sink = *sink;

	if (trig && !slogic_trigger_spec_empty(trig)) {
		int ret = slogic_trigger_compile(&a->trig, trig, nch);
		if (ret != SLOGIC_OK)
			return ret;
		a->has_trig = 1;
		if (limit && pretrigger > limit)
			pretrigger = limit;
		if (pretrigger * a->unit > ACC_PRE_MAX_BYTES)
			pretrigger = ACC_PRE_MAX_BYTES / a->unit;
		a->pre_cap = pretrigger;
		a->armed = a->pre_cap == 0;
		if (a->pre_cap) {
			a->pre = malloc((size_t)(a->pre_cap * a->unit));
			if (!a->pre)
				return SLOGIC_ERR;
		}
	}
	if (nch < 8) {
		a->scratch_samples = ACC_SCRATCH_SAMPLES;
		a->scratch = malloc(a->scratch_samples);
		if (!a->scratch) {
			free(a->pre);
			a->pre = NULL;
			return SLOGIC_ERR;
		}
	}
	return SLOGIC_OK;
}

void slogic_acc_free(slogic_acc *a)
{
	if (!a)
		return;
	free(a->pre);
	free(a->scratch);
	a->pre = a->scratch = NULL;
}

void slogic_acc_rearm(slogic_acc *a)
{
	a->drop_left = a->drop_init;
	a->carry_len = 0;
	a->armed = a->pre_cap == 0;
	a->triggered = 0;
	a->pre_head = a->pre_fill = 0;
	a->delivered = 0;
	a->sink_stop = 0;
	slogic_trigger_reset(&a->trig);
}

static void acc_emit(slogic_acc *a, const uint8_t *s, uint64_t n)
{
	if (!n)
		return;
	if (a->sink.data && !a->sink_stop &&
	    a->sink.data(a->sink.user, s, (size_t)n, a->unit))
		a->sink_stop = 1;
	a->delivered += n;
}

static int acc_done(const slogic_acc *a)
{
	return a->sink_stop || (a->limit && a->delivered >= a->limit);
}

/* Deliver samples [first, first+n) of a sample-aligned packed buffer. */
static void acc_deliver(slogic_acc *a, const uint8_t *p, uint64_t first,
			uint64_t n)
{
	if (a->limit && n > a->limit - a->delivered)
		n = a->limit - a->delivered;
	if (a->wire_unit) {
		acc_emit(a, p + first * a->wire_unit, n);
		return;
	}
	while (n && !a->sink_stop) {
		uint64_t c = n < a->scratch_samples ? n : a->scratch_samples;
		slogic_unpack_range(a->nch, p, first, (size_t)c, a->scratch);
		acc_emit(a, a->scratch, c);
		first += c;
		n -= c;
	}
}

/* Keep the last pre_cap samples of [first, first+n) in the pre-trigger ring. */
static void acc_pre_push(slogic_acc *a, const uint8_t *p, uint64_t first,
			 uint64_t n)
{
	if (!a->pre_cap)
		return;
	if (n > a->pre_cap) {
		first += n - a->pre_cap;
		n = a->pre_cap;
	}
	while (n) {
		uint64_t seg = a->pre_cap - a->pre_head;
		if (seg > n)
			seg = n;
		slogic_unpack_range(a->nch, p, first, (size_t)seg,
				    a->pre + a->pre_head * a->unit);
		a->pre_head = (a->pre_head + seg) % a->pre_cap;
		a->pre_fill = a->pre_fill + seg > a->pre_cap ? a->pre_cap
							     : a->pre_fill + seg;
		first += seg;
		n -= seg;
	}
}

static void acc_pre_flush(slogic_acc *a)
{
	uint64_t start, seg;

	if (!a->pre_fill)
		return;
	start = (a->pre_head + a->pre_cap - a->pre_fill) % a->pre_cap;
	seg = a->pre_cap - start;
	if (seg > a->pre_fill)
		seg = a->pre_fill;
	acc_emit(a, a->pre + start * a->unit, seg);
	acc_emit(a, a->pre, a->pre_fill - seg);
	a->pre_fill = 0;
}

/* One sample-aligned span of wire bytes. */
static int acc_process(slogic_acc *a, const uint8_t *p, size_t len)
{
	uint64_t n = a->wire_unit ? len / a->wire_unit
				  : (uint64_t)len * (8u / (unsigned)a->nch);

	if (a->has_trig && !a->triggered) {
		uint64_t from = 0;
		int64_t off;

		if (!a->armed) {
			/* Fill the pre-trigger window before arming. */
			from = a->pre_cap - a->pre_fill;
			if (from > n)
				from = n;
			acc_pre_push(a, p, 0, from);
			if (a->pre_fill < a->pre_cap)
				return SLOGIC_ACC_MORE;
			a->armed = 1;
			/* The scan starts after the window: seed it with the
			 * window's last sample so an edge right there counts. */
			if (from) {
				a->trig.prev = get_sample(&a->trig, p, from - 1);
				a->trig.have_prev = 1;
			}
		}
		off = slogic_trigger_scan(&a->trig, p, len, from);
		if (off < 0) {
			acc_pre_push(a, p, from, n - from);
			return SLOGIC_ACC_MORE;
		}
		acc_pre_push(a, p, from, (uint64_t)off - from);
		a->triggered = 1;
		acc_pre_flush(a);
		a->trig_index = a->delivered;
		if (a->sink.trigger)
			a->sink.trigger(a->sink.user, a->trig_index);
		acc_deliver(a, p, (uint64_t)off, n - (uint64_t)off);
	} else {
		acc_deliver(a, p, 0, n);
	}
	return acc_done(a) ? SLOGIC_ACC_DONE : SLOGIC_ACC_MORE;
}

int slogic_acc_feed(slogic_acc *a, uint8_t *packed, size_t len)
{
	int r;

	if (acc_done(a))
		return SLOGIC_ACC_DONE;

	if (a->drop_left && len) {
		size_t d = a->drop_left < len ? a->drop_left : len;
		packed += d;
		len -= d;
		a->drop_left -= (unsigned)d;
	}
	if (!len)
		return SLOGIC_ACC_MORE;

	if (!a->wire_unit)
		return acc_process(a, packed, len);

	/* >= 8 ch: complete a sample split across buffers first. */
	if (a->carry_len) {
		size_t take = a->wire_unit - a->carry_len;
		if (take > len)
			take = len;
		memcpy(a->carry + a->carry_len, packed, take);
		a->carry_len += (unsigned)take;
		packed += take;
		len -= take;
		if (a->carry_len < a->wire_unit)
			return SLOGIC_ACC_MORE;
		a->carry_len = 0;
		r = acc_process(a, a->carry, a->wire_unit);
		if (r != SLOGIC_ACC_MORE)
			return r;
	}
	{
		size_t whole = len - len % a->wire_unit;
		if (whole) {
			r = acc_process(a, packed, whole);
			if (r != SLOGIC_ACC_MORE)
				return r;
		}
		a->carry_len = (unsigned)(len - whole);
		memcpy(a->carry, packed + whole, a->carry_len);
	}
	return SLOGIC_ACC_MORE;
}

/* ---------------------------------------------------------------------- */
/* throughput fit                                                          */
/* ---------------------------------------------------------------------- */

void slogic_ratefit_init(slogic_ratefit *f, int64_t t_run_us)
{
	memset(f, 0, sizeof(*f));
	f->t_run = t_run_us;
}

void slogic_ratefit_add(slogic_ratefit *f, int64_t t_us, uint64_t bytes)
{
	double x, y, dx, dy;

	if (!bytes)
		return;
	if (!f->n)
		f->t_first = t_us;
	f->bytes += bytes;
	f->t_last = t_us;
	x = (double)(t_us - f->t_first);
	y = (double)f->bytes;
	f->n++;
	dx = x - f->mx;
	f->mx += dx / f->n;
	dy = y - f->my;
	f->my += dy / f->n;
	f->ctt += dx * (x - f->mx);
	f->ctb += dx * (y - f->my);
}

int slogic_ratefit_get(const slogic_ratefit *f, double *mbps,
		       double *cold_start_ms)
{
	double slope, t0;

	if (f->n < 2 || f->ctt <= 0.0)
		return SLOGIC_ERR;
	slope = f->ctb / f->ctt; /* bytes/us == MB/s */
	if (slope <= 0.0)
		return SLOGIC_ERR;
	if (mbps)
		*mbps = slope;
	if (cold_start_ms) {
		/* Where the fitted line reaches zero bytes = data start. */
		t0 = (double)f->t_first + f->mx - f->my / slope;
		*cold_start_ms = f->t_run ? (t0 - (double)f->t_run) / 1000.0 : 0.0;
		if (*cold_start_ms < 0.0)
			*cold_start_ms = 0.0;
	}
	return SLOGIC_OK;
}

/* ---------------------------------------------------------------------- */
/* report                                                                  */
/* ---------------------------------------------------------------------- */

int slogic_report_format(const slogic_report *r, char *buf, size_t n)
{
	char model[48], end[48], mbps[24], cold[24], trig[64];
	size_t i;

	snprintf(model, sizeof(model), "%s", r->model ? r->model : "unknown");
	for (i = 0; model[i]; i++)
		if (model[i] == ' ')
			model[i] = '_';

	switch (r->end) {
	case SLOGIC_END_OK: snprintf(end, sizeof(end), "ok"); break;
	case SLOGIC_END_STOPPED: snprintf(end, sizeof(end), "stopped"); break;
	case SLOGIC_END_NOTRIG: snprintf(end, sizeof(end), "notrig"); break;
	case SLOGIC_END_ERROR:
	default:
		snprintf(end, sizeof(end), "error:%s",
			 r->reason ? r->reason : "unknown");
		break;
	}
	if (r->fit_n >= 2 && r->mbps > 0.0) {
		snprintf(mbps, sizeof(mbps), "%.1f", r->mbps);
		snprintf(cold, sizeof(cold), "%.1f", r->cold_ms);
	} else {
		snprintf(mbps, sizeof(mbps), "nan");
		snprintf(cold, sizeof(cold), "nan");
	}
	if (r->triggered)
		snprintf(trig, sizeof(trig), "at:%llu,pre:%llu",
			 (unsigned long long)r->trig_index,
			 (unsigned long long)r->pre);
	else
		snprintf(trig, sizeof(trig), "none");

	return snprintf(buf, n,
		"slogic-report v1 model=%s ch=%d rate_mhz=%g end=%s "
		"samples=%llu/%llu bytes=%llu mbps=%s exp_mbps=%.1f fit_n=%u "
		"span_ms=%.1f cold_ms=%s xfer=%ux%d rearms=%u trig=%s q_peak_mb=%.1f",
		model, r->nch, (double)r->samplerate_hz / 1e6, end,
		(unsigned long long)r->samples, (unsigned long long)r->limit,
		(unsigned long long)r->bytes, mbps, r->exp_mbps, r->fit_n,
		r->span_ms, cold, r->xfer_size, r->ring, r->rearms, trig,
		(double)r->q_peak_bytes / 1e6);
}
