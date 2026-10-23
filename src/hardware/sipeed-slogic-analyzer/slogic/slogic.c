/*
 * libslogic — shared USB core for Sipeed SLogic logic analyzers.
 *
 * Copyright (C) 2023-2025 Shenzhen Sipeed Technology Co., Ltd.
 * (深圳市矽速科技有限公司) <support@sipeed.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.  See <http://www.gnu.org/licenses/>.
 *
 * Model registry, channel-mode ceilings, transfer planning, and the shared
 * per-transfer post-processing (first-byte drop and the stall watchdog). The
 * register/AUX control path (slogic_reset/configure/run/stop) is implemented in
 * a following commit, verified against build/bench/slogic_control_vectors.py.
 * Behaviour is specified in build/docs/slogic-protocol.md.
 */

#include "slogic.h"

#include <string.h>

/* -------------------- model registry -------------------- */

extern const slogic_model slogic_model_combo8; /* slogic8u2.c */
extern const slogic_model slogic_model_16u3;   /* slogic16u3.c */
extern const slogic_model slogic_model_32u3;   /* slogic32u3.c */

static const slogic_model *const registry[] = {
	&slogic_model_combo8,
	&slogic_model_16u3,
	&slogic_model_32u3,
};

const slogic_model *const *slogic_models(size_t *count)
{
	if (count)
		*count = sizeof(registry) / sizeof(registry[0]);
	return registry;
}

const char *const slogic_pattern_names[SLOGIC_PATTERN_COUNT] = {
	[SLOGIC_PATTERN_NORMAL] = "Normal",
	[SLOGIC_PATTERN_USB_TEST] = "USB connection test",
	[SLOGIC_PATTERN_EMULATION] = "Emulation",
};

const slogic_model *slogic_model_for_pid(uint16_t pid)
{
	size_t i;

	for (i = 0; i < sizeof(registry) / sizeof(registry[0]); i++) {
		if (registry[i]->pid == pid)
			return registry[i];
	}
	return NULL;
}

uint64_t slogic_max_rate(const slogic_model *m, int channel_count)
{
	size_t i;

	if (!m)
		return 0;
	for (i = 0; i < m->limit_count; i++) {
		if (m->limits[i].channels == channel_count)
			return m->limits[i].max_rate_hz;
	}
	return 0;
}

/* -------------------- transfer planning -------------------- */

#define SLOGIC_ALIGN     (32u * 1024u)
#define SLOGIC_SIZE_MIN  (32u * 1024u)
#define SLOGIC_SIZE_MAX  (3u * 1024u * 1024u)
#define SLOGIC_TOLERANCE 0.30

static uint32_t align_up(uint32_t v, uint32_t a)
{
	return (v + (a - 1)) & ~(a - 1);
}

int slogic_plan_transfers(const slogic_config *c, slogic_sizing sizing,
			  slogic_transfer_plan *out)
{
	uint64_t rate_bytes;
	uint32_t size;
	uint64_t duration_ms;

	if (!c || !out || c->channel_count <= 0 || c->samplerate_hz == 0)
		return SLOGIC_ERR_ARG;

	rate_bytes = c->samplerate_hz * (uint64_t)c->channel_count / 8u;
	if (rate_bytes == 0)
		return SLOGIC_ERR_ARG;

	if (sizing == SLOGIC_SIZING_LIBSIGROK) {
		/* 250 ms target, 32 KiB aligned, quartered so >= 4 are in flight
		 * (protocol.md section 3). No upper cap in the reference; we keep
		 * the floor so a tiny/very-slow capture still submits. */
		uint64_t bytes_250ms = rate_bytes / 4u; /* 250 ms */
		size = align_up((uint32_t)(bytes_250ms > 0xffffffffu ?
					   0xffffffffu : bytes_250ms),
				SLOGIC_ALIGN);
		size >>= 2;
		if (size < SLOGIC_SIZE_MIN)
			size = SLOGIC_SIZE_MIN;
	} else {
		/* ~4 ms target, clamp [32 KiB, 3 MiB] (protocol.md section 3). */
		uint64_t bytes_4ms = rate_bytes * 4u / 1000u;
		if (bytes_4ms < SLOGIC_SIZE_MIN)
			bytes_4ms = SLOGIC_SIZE_MIN;
		if (bytes_4ms > SLOGIC_SIZE_MAX)
			bytes_4ms = SLOGIC_SIZE_MAX;
		size = align_up((uint32_t)bytes_4ms, SLOGIC_ALIGN);
		if (size > SLOGIC_SIZE_MAX)
			size = SLOGIC_SIZE_MAX;
	}

	duration_ms = (uint64_t)size * 1000u / rate_bytes;
	if (duration_ms == 0)
		duration_ms = 1;

	out->size_bytes = size;
	out->ring_count = SLOGIC_MAX_TRANSFERS;
	out->expected_rate_bytes = rate_bytes;
	/* Match the resubmit timeout: (tolerance+1) * duration * 4, floored. */
	out->timeout_ms = (unsigned)((SLOGIC_TOLERANCE + 1.0) * duration_ms * 4.0);
	if (out->timeout_ms < 10)
		out->timeout_ms = 10;
	return SLOGIC_OK;
}

/* -------------------- per-transfer post-processing -------------------- */

void slogic_stream_init(slogic_stream *s, const slogic_transfer_plan *p,
			uint64_t need_bytes)
{
	if (!s)
		return;
	memset(s, 0, sizeof(*s));
	s->drop_left = 0; /* no drop by default; the adapter seeds model->first_drop_bytes */
	s->need_bytes = need_bytes;
	if (p) {
		s->slow_limit = (unsigned)(p->ring_count > 0 ? p->ring_count : 1);
		s->expected_rate_bytes = (uint32_t)p->expected_rate_bytes;
		s->transfer_size = p->size_bytes;
	} else {
		s->slow_limit = 1;
	}
}

size_t slogic_apply_first_drop(slogic_stream *s, uint8_t *buf, size_t len)
{
	size_t drop;

	if (!s || s->drop_left <= 0 || !buf || len == 0)
		return len;
	drop = ((size_t)s->drop_left < len) ? (size_t)s->drop_left : len;
	memmove(buf, buf + drop, len - drop);
	s->drop_left -= (int)drop;
	return len - drop;
}

double slogic_avg_mbps(uint64_t received_bytes, int64_t first_data_us,
		       int64_t now_us)
{
	int64_t span_us = now_us - first_data_us;

	if (received_bytes == 0 || first_data_us == 0 || span_us <= 0)
		return 0.0;
	return (double)received_bytes / (double)span_us; /* bytes/us == MB/s */
}

slogic_verdict slogic_stream_watch(slogic_stream *s, size_t got_bytes,
				   int64_t now_us, int64_t since_last_us)
{
	double expected_us, actual_rate;
	int slow;

	if (!s)
		return SLOGIC_STREAM_ABORT;

	if (s->time_start_us == 0)
		s->time_start_us = now_us;
	if (got_bytes > 0) {
		if (s->time_first_data_us == 0)
			s->time_first_data_us = now_us; /* start the rate clock at first byte */
		s->received_bytes += got_bytes;
		s->time_last_data_us = now_us;
	}

	if (s->need_bytes && s->received_bytes >= s->need_bytes)
		return SLOGIC_STREAM_DONE;

	/* Not enough history to judge speed yet. */
	if (since_last_us <= 0 || s->expected_rate_bytes == 0 ||
	    s->transfer_size == 0)
		return SLOGIC_STREAM_OK;

	expected_us = (double)s->transfer_size * 1000000.0 /
		      (double)s->expected_rate_bytes;
	actual_rate = (double)got_bytes * 1000000.0 / (double)since_last_us;
	slow = ((double)since_last_us > (SLOGIC_TOLERANCE + 1.0) * expected_us) ||
	       (actual_rate < (1.0 - SLOGIC_TOLERANCE) *
				      (double)s->expected_rate_bytes);

	if (s->received_bytes == 0) {
		/* Stream has not started: the rate watchdog is authoritative. */
		if (slow)
			s->slow_count++;
		else
			s->slow_count = 0;
		if (s->slow_count >= s->slow_limit) {
			if (!s->run_retried) {
				/* Firmware swallowed RUN — re-arm exactly once. */
				s->run_retried = 1;
				s->slow_count = 0;
				return SLOGIC_STREAM_RETRY_RUN;
			}
			return SLOGIC_STREAM_ABORT;
		}
		return SLOGIC_STREAM_OK;
	}

	/* Data is flowing: only total silence is fatal; slow-but-alive is
	 * legitimate host backpressure and warns once (protocol.md 6.2). */
	{
		int64_t idle_us = (s->time_last_data_us ? s->time_last_data_us
						       : s->time_start_us);
		idle_us = now_us - idle_us;
		if (idle_us > SLOGIC_STREAM_IDLE_US)
			return SLOGIC_STREAM_ABORT;
	}
	if (slow && !s->slow_warned) {
		s->slow_warned = 1;
		return SLOGIC_STREAM_WARN_SLOW;
	}
	return SLOGIC_STREAM_OK;
}

/* -------------------- register / AUX control path -------------------- */
/*
 * Verified against build/bench/slogic_control_vectors.py: the canonical
 * configure sequence is CTRL=STOP, then AUX channel/samplerate/vref/pattern in
 * that fixed order each with a confirm read-back (section 6.7), then CTRL=RUN.
 */

#define REQ_REG_READ     0x00
#define REQ_REG_WRITE    0x01
#define R_CTRL           0x0004
#define R_AUX            0x000c
#define R_AUX_PAYLOAD    0x0010 /* R_AUX + 4 */
#define CTRL_STOP        UINT32_C(0x00000000)
#define CTRL_RUN         UINT32_C(0x00000001)
#define CTRL_RST         UINT32_C(0x00000002)
#define AUX_CMD_CHANNEL  UINT32_C(0x00000001)
#define AUX_CMD_RATE     UINT32_C(0x00000002)
#define AUX_CMD_VREF     UINT32_C(0x00000003)
#define AUX_CMD_TEST     UINT32_C(0x00000005)
#define COMBO8_CMD_START 0xb1
#define CTRL_TIMEOUT_MS  500
#define AUX_POLL_RETRIES 8

static int ctrl_write(const slogic_transport *t, uint16_t addr,
		      const uint8_t *data, size_t len)
{
	size_t i;

	len = (len + 3) & ~(size_t)3;
	for (i = 0; i < len; i += 4) {
		int r = t->control_write(t->ctx, REQ_REG_WRITE,
					 (uint16_t)(addr + i), 0, data + i, 4,
					 CTRL_TIMEOUT_MS);
		if (r < 0)
			return SLOGIC_ERR_IO;
	}
	return SLOGIC_OK;
}

static int ctrl_read(const slogic_transport *t, uint16_t addr, uint8_t *data,
		     size_t len)
{
	size_t i;

	len = (len + 3) & ~(size_t)3;
	for (i = 0; i < len; i += 4) {
		int r = t->control_read(t->ctx, REQ_REG_READ,
					(uint16_t)(addr + i), 0, data + i, 4,
					CTRL_TIMEOUT_MS);
		if (r < 0)
			return SLOGIC_ERR_IO;
	}
	return SLOGIC_OK;
}

static int wr32(const slogic_transport *t, uint16_t addr, uint32_t v)
{
	uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16),
			 (uint8_t)(v >> 24) };
	return ctrl_write(t, addr, b, 4);
}

/*
 * Begin one AUX transaction: write the command word, poll the header until its
 * ready bit sets, and read the payload. Returns the payload byte length in *n.
 */
static int aux_begin(const slogic_transport *t, uint32_t cmd, uint8_t *pay,
		     size_t cap, size_t *n)
{
	uint32_t h = 0;
	int retry, r;
	size_t len;
	uint8_t hb[4];

	r = wr32(t, R_AUX, cmd);
	if (r)
		return r;
	for (retry = 0; retry < AUX_POLL_RETRIES; retry++) {
		r = ctrl_read(t, R_AUX, hb, 4);
		if (r)
			return r;
		h = (uint32_t)hb[0] | ((uint32_t)hb[1] << 8) |
		    ((uint32_t)hb[2] << 16) | ((uint32_t)hb[3] << 24);
		if ((h >> 16) & 1u)
			break;
	}
	if (!((h >> 16) & 1u))
		return SLOGIC_ERR_TIMEOUT;

	len = (size_t)((h & 0xffffu) >> 9);
	if (len & 3)
		len = (len + 3) & ~(size_t)3;
	if (len == 0)
		len = 4;
	if (len > cap)
		len = cap & ~(size_t)3;
	memset(pay, 0, cap);
	r = ctrl_read(t, R_AUX_PAYLOAD, pay, len);
	if (r)
		return r;
	if (n)
		*n = len;
	return SLOGIC_OK;
}

/* Write the payload back and confirm with a read-back (canonical, section 6.7). */
static int aux_write_confirm(const slogic_transport *t, uint8_t *pay, size_t n)
{
	int r;

	if (n & 3)
		n = (n + 3) & ~(size_t)3;
	r = ctrl_write(t, R_AUX_PAYLOAD, pay, n);
	if (r)
		return r;
	return ctrl_read(t, R_AUX_PAYLOAD, pay, n);
}

static uint32_t vth_to_dac(double v)
{
	if (v < 0.0)
		v = 0.0;
	if (v > 6.0)
		v = 6.0;
	/* Canonical rounds (section 6.6): dac = V / 3.33 / 2 * 1024, rounded. */
	return (uint32_t)(v / 3.33 / 2.0 * 1024.0 + 0.5);
}

static int aux_channel(const slogic_transport *t, int nch)
{
	uint8_t pay[16];
	size_t n = 0;
	uint32_t mask;
	int r;

	mask = (nch >= 32) ? UINT32_C(0xffffffff) :
	       (nch <= 0)  ? 0u : ((UINT32_C(1) << nch) - 1u);
	r = aux_begin(t, AUX_CMD_CHANNEL, pay, sizeof(pay), &n);
	if (r)
		return r;
	pay[0] = (uint8_t)mask;
	pay[1] = (uint8_t)(mask >> 8);
	pay[2] = (uint8_t)(mask >> 16);
	pay[3] = (uint8_t)(mask >> 24);
	if (n < 4)
		n = 4;
	return aux_write_confirm(t, pay, n);
}

#define PREFERRED_BASE_MHZ 800u

/*
 * Select the clock base and divider for `want`. The device reports one base
 * per index (SLogic32U3: 0 = 1400 MHz, 1 = 800 MHz, then empty slots) and
 * keeps the selected index until CTRL=RST, which selects index 0; the walk
 * starts at the reported index and wraps past an empty slot.
 *
 * `base_mhz_req` != 0 accepts only that base. Otherwise the first lap looks
 * for the 800 MHz base, and only a rate it cannot divide down to (1400 MHz)
 * takes another base on the second lap, so e.g. 200 MHz is always 800/4
 * rather than 1400/7.
 */
static int aux_rate(const slogic_transport *t, uint64_t want, unsigned base_mhz_req)
{
	uint8_t pay[16];
	size_t n = 0;
	int tries, r;
	unsigned pref;

	if (want == 0)
		return SLOGIC_ERR_ARG;
	if (base_mhz_req && (uint64_t)base_mhz_req * UINT64_C(1000000) % want != 0)
		return SLOGIC_ERR_ARG;
	pref = base_mhz_req ? base_mhz_req
	       : (UINT64_C(1000000) * PREFERRED_BASE_MHZ % want == 0 ? PREFERRED_BASE_MHZ : 0);
	r = aux_begin(t, AUX_CMD_RATE, pay, sizeof(pay), &n);
	if (r)
		return r;
	if (n < 8)
		n = 8;
	/* Two laps over the (at most 4) slots: the preferred base, then, without a
	 * required base, any base that divides the rate. */
	for (tries = 0; tries < 16; tries++) {
		uint16_t idx = (uint16_t)(pay[0] | (pay[1] << 8));
		uint16_t base_mhz = (uint16_t)(pay[2] | (pay[3] << 8));
		uint64_t base = (uint64_t)base_mhz * UINT64_C(1000000);
		uint32_t divm1;

		if (tries == 8 && !base_mhz_req)
			pref = 0; /* preferred base not offered: take any that divides */
		if (base == 0 || base % want != 0 || (pref && base_mhz != pref)) {
			/* Wrong or empty slot: next index, wrapping to 0 after an empty one. */
			idx = base == 0 ? 0 : (uint16_t)(idx + 1);
			pay[0] = (uint8_t)idx;
			pay[1] = (uint8_t)(idx >> 8);
			r = ctrl_write(t, R_AUX_PAYLOAD, pay, 4);
			if (r)
				return r;
			r = ctrl_read(t, R_AUX_PAYLOAD, pay, n);
			if (r)
				return r;
			continue;
		}
		divm1 = (uint32_t)(base / want - 1);
		pay[4] = (uint8_t)divm1;
		pay[5] = (uint8_t)(divm1 >> 8);
		pay[6] = (uint8_t)(divm1 >> 16);
		pay[7] = (uint8_t)(divm1 >> 24);
		return aux_write_confirm(t, pay, n);
	}
	return SLOGIC_ERR;
}

static int aux_vref(const slogic_transport *t, double v)
{
	uint8_t pay[16];
	size_t n = 0;
	uint32_t dac = vth_to_dac(v);
	int r;

	r = aux_begin(t, AUX_CMD_VREF, pay, sizeof(pay), &n);
	if (r)
		return r;
	pay[0] = (uint8_t)dac;
	pay[1] = (uint8_t)(dac >> 8);
	pay[2] = (uint8_t)(dac >> 16);
	pay[3] = (uint8_t)(dac >> 24);
	if (n < 4)
		n = 4;
	return aux_write_confirm(t, pay, n);
}

static int aux_test(const slogic_transport *t, uint32_t mode)
{
	uint8_t pay[16];
	size_t n = 0;
	int r;

	r = aux_begin(t, AUX_CMD_TEST, pay, sizeof(pay), &n);
	if (r)
		return r;
	pay[0] = (uint8_t)mode;
	pay[1] = (uint8_t)(mode >> 8);
	pay[2] = (uint8_t)(mode >> 16);
	pay[3] = (uint8_t)(mode >> 24);
	if (n < 4)
		n = 4;
	return aux_write_confirm(t, pay, n);
}

int slogic_reset(const slogic_model *m, const slogic_transport *t)
{
	int r;

	if (!m || !t)
		return SLOGIC_ERR_ARG;
	if (m->proto == SLOGIC_PROTO_COMBO8)
		return SLOGIC_OK;
	r = wr32(t, R_CTRL, CTRL_RST);
	if (r)
		return r;
	return wr32(t, R_CTRL, CTRL_STOP);
}

int slogic_configure(const slogic_model *m, const slogic_transport *t,
		     const slogic_config *c)
{
	int r;

	if (!m || !t || !c)
		return SLOGIC_ERR_ARG;
	if (m->proto == SLOGIC_PROTO_COMBO8)
		return SLOGIC_OK; /* config carried in the CMD_START at run */
	/* Pre-arm stopped, then the four config blocks in canonical order. */
	r = wr32(t, R_CTRL, CTRL_STOP);
	if (r)
		return r;
	if ((r = aux_channel(t, c->channel_count)))
		return r;
	if ((r = aux_rate(t, c->samplerate_hz, c->clock_base_mhz)))
		return r;
	if ((r = aux_vref(t, c->threshold_v)))
		return r;
	if ((r = aux_test(t, (uint32_t)c->pattern_mode)))
		return r;
	return SLOGIC_OK;
}

int slogic_run(const slogic_model *m, const slogic_transport *t,
	       const slogic_config *c)
{
	if (!m || !t || !c)
		return SLOGIC_ERR_ARG;
	if (m->proto == SLOGIC_PROTO_COMBO8) {
		/* Byte 3 carries the pattern mode (SLOGIC_PATTERN_*). Firmware
		 * without pattern support ignores it (it was never used). */
		uint16_t mhz = (uint16_t)(c->samplerate_hz / UINT64_C(1000000));
		uint8_t cmd[4] = { (uint8_t)mhz, (uint8_t)(mhz >> 8),
				   (uint8_t)c->channel_count,
				   (uint8_t)c->pattern_mode };
		int r = t->control_write(t->ctx, COMBO8_CMD_START, 0, 0, cmd,
					 (uint16_t)sizeof(cmd), CTRL_TIMEOUT_MS);
		return (r < 0) ? SLOGIC_ERR_IO : SLOGIC_OK;
	}
	return wr32(t, R_CTRL, CTRL_RUN);
}

int slogic_stop(const slogic_model *m, const slogic_transport *t)
{
	if (!m || !t)
		return SLOGIC_ERR_ARG;
	if (m->proto == SLOGIC_PROTO_COMBO8)
		return SLOGIC_OK; /* no reliable stop; adapter drains the EP */
	return wr32(t, R_CTRL, CTRL_STOP);
}
