/*
 * libslogic — shared USB core for Sipeed SLogic logic analyzers.
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
 * This is the single source of truth for how the host talks to the device.
 * It depends ONLY on <stdint.h>/<stddef.h> and the caller-supplied transport;
 * it never includes libsigrok or libusb headers, so both driver front ends can
 * vendor a byte-identical copy and adapt it with a thin porting layer.
 *
 * The wire protocol it implements is specified in build/docs/slogic-protocol.md.
 */

#ifndef SLOGIC_H
#define SLOGIC_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- return codes ---- */
enum {
	SLOGIC_OK = 0,
	SLOGIC_ERR = -1,
	SLOGIC_ERR_ARG = -2,
	SLOGIC_ERR_IO = -3,
	SLOGIC_ERR_TIMEOUT = -4,
	SLOGIC_ERR_NODEV = -5,
};

/* Samplerate/bandwidth helper — no libsigrok SR_MHZ dependency. */
#define SLOGIC_MHZ(n) ((uint64_t)(n) * UINT64_C(1000000))

/* ---- USB identity (protocol.md section 1) ---- */
#define SLOGIC_VID          UINT16_C(0x359f)
#define SLOGIC_PID_COMBO8   UINT16_C(0x0300)
#define SLOGIC_PID_16U3     UINT16_C(0x3031)
#define SLOGIC_PID_32U3     UINT16_C(0x3032)
#define SLOGIC_PID_DFU      UINT16_C(0x30f1) /* bootloader; not handled here */

/* ---- pattern modes (AUX cmd 5 payload) ---- */
enum {
	SLOGIC_PATTERN_NORMAL = 0,
	SLOGIC_PATTERN_USB_TEST = 1,   /* "USB connection test" max-speed pattern */
	SLOGIC_PATTERN_EMULATION = 2,  /* structured pattern generator */
	SLOGIC_PATTERN_COUNT = 3,
};

/* Display names indexed by SLOGIC_PATTERN_*, so both front ends show the same
 * strings (libsigrok SR_CONF_PATTERN_MODE, DSView pattern list). */
extern const char *const slogic_pattern_names[SLOGIC_PATTERN_COUNT];

/*
 * Transport: the host owns the USB device; libslogic never opens, claims, or
 * closes it, and issues no bulk transfers itself (the adapter owns the transfer
 * ring, so it can integrate with its own event loop). libslogic only drives the
 * vendor control endpoint through these two callbacks. A recorded/mock
 * transport makes the whole control path testable without hardware — that is
 * what the conformance vectors exercise.
 *
 * control_write/control_read move one 4-byte register word at a time; they
 * return the number of bytes transferred (>= 0) or a negative libusb-style
 * error. b_request is the register read/write code (0x00/0x01), w_value the
 * register address, w_index always 0. (Combo 8's start command reuses
 * control_write with b_request 0xb1 and a single 4-byte payload.)
 */
typedef struct slogic_transport {
	void *ctx;
	int (*control_write)(void *ctx, uint8_t b_request, uint16_t w_value,
			     uint16_t w_index, const uint8_t *data,
			     uint16_t len, unsigned timeout_ms);
	int (*control_read)(void *ctx, uint8_t b_request, uint16_t w_value,
			    uint16_t w_index, uint8_t *data, uint16_t len,
			    unsigned timeout_ms);
} slogic_transport;

/* ---- device model ---- */
typedef enum {
	SLOGIC_PROTO_COMBO8, /* small command protocol */
	SLOGIC_PROTO_U3,     /* register/AUX protocol, 16U3 and 32U3 */
} slogic_proto;

/*
 * The channel-mode ceiling is a physical, order-free mapping from channel count
 * to maximum samplerate (protocol.md section 1). Stored as pairs so the two
 * front ends can present them in whatever order their UI wants without the
 * table drifting.
 */
typedef struct slogic_rate_limit {
	int channels;
	uint64_t max_rate_hz;
} slogic_rate_limit;

typedef struct slogic_model {
	const char *name;
	uint16_t pid;
	uint8_t ep_in;              /* bulk IN endpoint (0x81 Combo8, 0x82 U3) */
	int physical_channels;
	slogic_proto proto;
	uint64_t max_bandwidth_hz;
	const uint64_t *rates;      /* advertised discrete rates, ascending */
	size_t rate_count;
	const slogic_rate_limit *limits;     /* native channel->max_rate table */
	size_t limit_count;
	uint8_t first_drop_bytes;   /* HW quirk: leading stream bytes to drop (0 = none) */
} slogic_model;

/* Registry (defined across slogic.c + slogic16u3.c + slogic32u3.c). */
const slogic_model *slogic_model_for_pid(uint16_t pid);
const slogic_model *const *slogic_models(size_t *count);

/*
 * Highest samplerate allowed for `channel_count` under this model, or 0 if the
 * channel count is not a supported mode. This is the native hardware ceiling;
 * a host that cannot sustain it at the top rate (e.g. the Windows USB stack)
 * leaves picking a lower rate to the user rather than capping it here.
 */
uint64_t slogic_max_rate(const slogic_model *m, int channel_count);

/* ---- capture configuration ---- */
typedef struct slogic_config {
	int channel_count;      /* 2/4/8/16/32 depending on model */
	uint64_t samplerate_hz;
	double threshold_v;
	int pattern_mode;       /* SLOGIC_PATTERN_* */
} slogic_config;

/*
 * Control path. Each issues the register/AUX transactions of protocol.md
 * sections 2/2b through the transport. slogic_configure pre-arms with CTRL=STOP
 * then applies channel mask, samplerate, threshold, and pattern in the
 * canonical fixed order (section 6.7), each block confirmed with a read-back;
 * it does NOT write RUN. slogic_run starts streaming — it takes the config
 * because Combo 8 carries rate and channel count in its start command; the U3
 * models ignore it and just write CTRL=RUN. slogic_stop stops the U3 models
 * with CTRL=STOP; Combo 8 has no reliable stop command, so its adapter drains
 * the bulk endpoint instead, and slogic_configure is a no-op on Combo 8.
 */
int slogic_reset(const slogic_model *m, const slogic_transport *t);
int slogic_configure(const slogic_model *m, const slogic_transport *t,
		     const slogic_config *c);
int slogic_run(const slogic_model *m, const slogic_transport *t,
	       const slogic_config *c);
int slogic_stop(const slogic_model *m, const slogic_transport *t);

/* ---- transfer planning (protocol.md section 3) ---- */

/*
 * Per-transfer sizing strategy. The two front ends disagree (protocol.md
 * section 6.5, still an open decision to settle against the Phase 0 baseline),
 * so it is a parameter rather than a baked-in choice.
 */
typedef enum {
	SLOGIC_SIZING_LIBSIGROK, /* 250 ms probe, quarter, no upper cap */
	SLOGIC_SIZING_ALLLOGIC,  /* ~4 ms target, clamp [32 KiB, 3 MiB] */
} slogic_sizing;

typedef struct slogic_transfer_plan {
	uint32_t size_bytes;
	int ring_count;             /* number of transfers in flight, <= 16 */
	uint64_t expected_rate_bytes; /* samplerate * channel_count / 8 */
	unsigned timeout_ms;        /* per-transfer libusb timeout */
} slogic_transfer_plan;

#define SLOGIC_MAX_TRANSFERS 16

int slogic_plan_transfers(const slogic_config *c, slogic_sizing sizing,
			  slogic_transfer_plan *out);

/* ---- shared per-transfer post-processing ---- */

/*
 * Stream bookkeeping the adapter carries across a capture. The adapter owns the
 * libusb ring and calls the two helpers below on each completed transfer; the
 * raw (buf, len) it works with is still sample-major — the adapter reshapes it
 * (SR_DF_LOGIC or LA_CROSS_DATA). Fields are exposed deliberately: this is a
 * vendored source copy, not a shared ABI.
 */
typedef struct slogic_stream {
	int drop_left;               /* bytes still to drop at stream start (model quirk, section 6.3) */
	uint64_t need_bytes;         /* target byte count; 0 = continuous */
	uint64_t received_bytes;
	/* stall watchdog (protocol.md section 6.2, all-logic canonical) */
	int64_t time_start_us;
	int64_t time_first_data_us; /* first byte arrival; excludes arm/cold-start from the rate */
	int64_t time_last_data_us;
	unsigned slow_count;
	unsigned slow_limit;         /* = ring_count */
	int run_retried;             /* RUN re-arm is one-shot */
	int slow_warned;             /* backpressure warning is one-shot */
	uint32_t expected_rate_bytes;
	uint32_t transfer_size;
} slogic_stream;

void slogic_stream_init(slogic_stream *s, const slogic_transfer_plan *p,
			uint64_t need_bytes);

/*
 * Drop drop_left leading bytes of the stream across however many transfers it
 * takes (a per-model hardware quirk, section 6.3; seed drop_left from the
 * model's first_drop_bytes). Returns the number of bytes to keep and shifts buf
 * if any bytes were dropped.
 */
size_t slogic_apply_first_drop(slogic_stream *s, uint8_t *buf, size_t len);

/*
 * Standard average throughput in MB/s over the data-transfer window only:
 * received_bytes / (now_us - first_data_us). Starting the clock at the first
 * byte adaptively excludes the arm/cold-start dead time (the firmware can stall
 * many ms before the first sample), so the figure reflects real link
 * throughput. Returns 0 until data has flowed. (bytes/us == MB/s.)
 */
double slogic_avg_mbps(uint64_t received_bytes, int64_t first_data_us,
		       int64_t now_us);

typedef enum {
	SLOGIC_STREAM_OK,        /* keep going */
	SLOGIC_STREAM_WARN_SLOW, /* one-shot: host backpressure, not fatal */
	SLOGIC_STREAM_RETRY_RUN, /* never started: re-issue RUN once */
	SLOGIC_STREAM_ABORT,     /* fatal stall / error */
	SLOGIC_STREAM_DONE,      /* need_bytes reached */
} slogic_verdict;

/*
 * Fold one completed transfer into the stall watchdog. `got_bytes` is this
 * transfer's kept length, `now_us` a monotonic microsecond clock,
 * `since_last_us` the gap since the previous completion. Implements the
 * canonical policy: rate-watchdog gates only the never-started case (+ one RUN
 * re-arm); once bytes flow, only SLOGIC_STREAM_IDLE_US of complete silence is
 * fatal.
 */
slogic_verdict slogic_stream_watch(slogic_stream *s, size_t got_bytes,
				   int64_t now_us, int64_t since_last_us);

#define SLOGIC_STREAM_IDLE_US 1000000 /* mid-stream fatal silence (section 3) */

#ifdef __cplusplus
}
#endif

#endif /* SLOGIC_H */
