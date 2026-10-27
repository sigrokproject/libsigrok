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
 * The header needs only <stdint.h>/<stddef.h>. slogic.c and slogic_data.c are
 * pure C; slogic_usb.c owns the capture engine on libusb + pthread. It never
 * includes libsigrok headers, so every front end vendors a byte-identical copy
 * and adapts it with a thin porting layer.
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
	SLOGIC_ERR_BUSY = -6,   /* interface claimed elsewhere / capture already live */
};

/* Samplerate/bandwidth helper — no libsigrok SR_MHZ dependency. */
#define SLOGIC_MHZ(n) ((uint64_t)(n) * UINT64_C(1000000))

/* ---- USB identity (protocol.md section 1) ---- */
#define SLOGIC_VID          UINT16_C(0x359f)
#define SLOGIC_PID_8U2      UINT16_C(0x3000) /* SLogic8U2 (Combo 8 protocol) */
#define SLOGIC_PID_COMBO8   UINT16_C(0x0300) /* same, old firmware: "(old)" */
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
	const char *product;        /* USB product string, the name shown in the apps */
	const char *name_tag;       /* device name prefix instead of the link tag (NULL: link) */
} slogic_model;

/* Registry (defined across slogic.c + slogic16u3.c + slogic32u3.c). */
const slogic_model *slogic_model_for_pid(uint16_t pid);
/* Whether a USB device (VID:PID) is an SLogic analyzer the core drives. Front
 * ends use this instead of their own PID lists (scan, hotplug, reconnect). */
int slogic_is_analyzer(uint16_t vid, uint16_t pid);
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
	/*
	 * Sample clock source. The U3 models derive the sample clock from one of
	 * several PLL bases (SLogic32U3: slot 0 = 1400 MHz, slot 1 = 800 MHz).
	 * 0 = prefer the 800 MHz base and fall back to another base only for a
	 * rate 800 MHz cannot make (1400 MHz); otherwise only this base (in MHz)
	 * is used, and slogic_configure fails if the rate does not divide it.
	 */
	unsigned clock_base_mhz;
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

/* ======================================================================
 * Pure data layer (slogic_data.c): no libusb, no threads. Everything that
 * decides WHICH samples reach the host lives here, so it is unit-testable
 * with a fake producer.
 * ====================================================================== */

/* ---- logging (optional) ---- */
enum {
	SLOGIC_LOG_ERR = 1,
	SLOGIC_LOG_WARN,
	SLOGIC_LOG_INFO,
	SLOGIC_LOG_DBG,
	SLOGIC_LOG_SPEW,
};
typedef void (*slogic_log_fn)(void *user, int level, const char *msg);

/* ---- link speed and link-aware rate caps ---- */
typedef enum {
	SLOGIC_LINK_UNKNOWN = 0,
	SLOGIC_LINK_FULL,       /* USB 1.1 */
	SLOGIC_LINK_HIGH,       /* USB 2.0 */
	SLOGIC_LINK_SUPER,      /* USB 3 5 Gbps */
	SLOGIC_LINK_SUPER_PLUS, /* USB 3 10 Gbps */
} slogic_link;

const char *slogic_link_name(slogic_link link);
/* Link speed for device names: "10G", "5G", "480M", "12M"; NULL if unknown. */
const char *slogic_link_tag(slogic_link link);
/* Device name prefix: "old" for firmware to update, else the link speed;
 * NULL: no prefix. */
const char *slogic_name_tag(const slogic_model *m, slogic_link link);
/* The name every app shows, "(10G) SLogic32 U3", "(480M) SLogic8 U2",
 * "(old) SLogic8 U2": prefix, then the USB product string. Returns `buf`. */
char *slogic_display_name(const slogic_model *m, slogic_link link, char *buf, size_t n);
/* Native ceiling for `nch`, capped for the U3 models to what the link
 * sustains: 5G ~400 MB/s (32U3 on 5G: 4ch@800 ... 32ch@100), USB2 ~40 MB/s. */
uint64_t slogic_link_max_rate(const slogic_model *m, int nch, slogic_link link);
/* exact=1: `want` if it is an advertised rate within the cap, else 0.
 * exact=0: nearest advertised rate within the cap (0 picks the cap). */
uint64_t slogic_pick_rate(const slogic_model *m, int nch, slogic_link link,
			  uint64_t want, int exact);
/* Advertised rates within the cap, ascending. Returns the count written. */
size_t slogic_rate_list(const slogic_model *m, int nch, slogic_link link,
			uint64_t *out, size_t cap);

/* ---- sample format ----
 * Wire: sample-major, LSB = D0. Below 8 channels a byte packs 8/nch samples,
 * earliest sample in the low bits. Delivered samples are one byte each below
 * 8 channels, else nch/8 bytes (unitsize 1/1/1/2/4 for 2/4/8/16/32 ch). */
unsigned slogic_unitsize(int nch);
uint64_t slogic_bytes_for_samples(int nch, uint64_t samples); /* rounds UP */
uint64_t slogic_samples_in_bytes(int nch, uint64_t bytes);    /* whole samples */
/* Unpack `nsamples` samples starting at sample `first` of the packed buffer
 * into `out` (unitsize bytes each). Never touches a byte past the last sample
 * read, so a partial trailing group is safe. Returns nsamples. */
size_t slogic_unpack_range(int nch, const uint8_t *packed, uint64_t first,
			   size_t nsamples, uint8_t *out);

/* ---- single-stage trigger (per channel, ANDed) ---- */
enum {
	SLOGIC_TRIG_X = 0, /* don't care */
	SLOGIC_TRIG_ZERO,
	SLOGIC_TRIG_ONE,
	SLOGIC_TRIG_RISING,
	SLOGIC_TRIG_FALLING,
	SLOGIC_TRIG_EDGE,  /* either edge */
};
#define SLOGIC_MAX_CHANNELS 32
typedef struct slogic_trigger_spec {
	uint8_t match[SLOGIC_MAX_CHANNELS]; /* SLOGIC_TRIG_* by channel index */
} slogic_trigger_spec;
int slogic_trigger_spec_empty(const slogic_trigger_spec *spec);

typedef struct slogic_trigger {
	int nch;
	unsigned bits;          /* sample width in bits on the wire: nch or 8*unit */
	uint32_t smask;         /* (1 << nch) - 1 */
	uint32_t lvl_mask, lvl_val, rise, fall, any, edge_all;
	uint64_t edge_bcast;    /* edge_all repeated in every lane of a u64 */
	uint32_t prev;
	int have_prev;
} slogic_trigger;
int slogic_trigger_compile(slogic_trigger *t, const slogic_trigger_spec *spec,
			   int nch);
void slogic_trigger_reset(slogic_trigger *t); /* forget the previous sample */
/* Index (within this packed buffer) of the first matching sample at or after
 * sample `first`, or -1. For >= 8 channels `len` must be a whole number of
 * samples. The previous sample is carried across calls, so edges spanning a
 * buffer boundary are found; the very first sample of a capture never fires
 * an edge. */
int64_t slogic_trigger_scan(slogic_trigger *t, const uint8_t *packed, size_t len,
			    uint64_t first);

/* ---- sample accounting: drop, trigger, pre-trigger, exact limit ---- */
typedef struct slogic_sink {
	void *user;
	/* Delivered samples (unitsize bytes each). Return non-zero to end. */
	int (*data)(void *user, const uint8_t *samples, size_t nsamples,
		    unsigned unitsize);
	/* Trigger point: index of the trigger sample in the delivered stream
	 * (= number of pre-trigger samples delivered before it). Optional. */
	void (*trigger)(void *user, uint64_t sample_index);
} slogic_sink;

typedef struct slogic_acc {
	int nch;
	unsigned unit;           /* delivered bytes per sample */
	unsigned wire_unit;      /* wire bytes per sample (>= 8 ch), else 0 */
	uint64_t limit;          /* 0 = continuous */
	uint64_t delivered;
	unsigned drop_init, drop_left;
	uint8_t carry[4];        /* partial wire sample across buffers (>= 8 ch) */
	unsigned carry_len;
	int has_trig, armed, triggered;
	slogic_trigger trig;
	uint64_t trig_index;
	uint8_t *pre;            /* ring of delivered-format samples */
	uint64_t pre_cap, pre_head, pre_fill;
	uint8_t *scratch;        /* unpack chunk */
	size_t scratch_samples;
	slogic_sink sink;
	int sink_stop;
} slogic_acc;

enum { SLOGIC_ACC_MORE = 0, SLOGIC_ACC_DONE = 1 };

/* trig may be NULL or empty for an untriggered capture. pretrigger is clamped
 * to the limit (continuous: kept as given). The trigger arms only once the
 * pre-trigger window has filled, so a fired trigger always sits exactly at
 * sample index `pretrigger` of the delivered stream. Returns SLOGIC_OK or an
 * error. */
int slogic_acc_init(slogic_acc *a, int nch, uint64_t limit,
		    const slogic_trigger_spec *trig, uint64_t pretrigger,
		    unsigned first_drop_bytes, const slogic_sink *sink);
/* Feed one raw bulk buffer (may be modified). Returns SLOGIC_ACC_MORE,
 * SLOGIC_ACC_DONE (limit reached or the sink asked to stop) or < 0. */
int slogic_acc_feed(slogic_acc *a, uint8_t *packed, size_t len);
/* Forget everything received (stall re-arm before anything was delivered). */
void slogic_acc_rearm(slogic_acc *a);
void slogic_acc_free(slogic_acc *a);

/* ---- throughput: least-squares fit of cumulative bytes vs time ----
 * Feed every data-bearing transfer completion. The slope is the link rate,
 * independent of transfer size and of how long the first transfer took; the
 * line's zero-byte crossing gives when data started, so cold start = that
 * minus the RUN time. Needs >= 2 points at distinct times. */
typedef struct slogic_ratefit {
	uint32_t n;
	double mx, my, ctt, ctb;  /* Welford: x = t - t_first (us), y = bytes */
	int64_t t_run, t_first, t_last;
	uint64_t bytes;
} slogic_ratefit;
void slogic_ratefit_init(slogic_ratefit *f, int64_t t_run_us);
void slogic_ratefit_add(slogic_ratefit *f, int64_t t_us, uint64_t bytes);
int slogic_ratefit_get(const slogic_ratefit *f, double *mbps,
		       double *cold_start_ms);

/* ---- one standard capture report, identical for every host ---- */
typedef enum {
	SLOGIC_END_OK,       /* limit reached, or continuous capture stopped */
	SLOGIC_END_STOPPED,  /* stopped before the limit */
	SLOGIC_END_NOTRIG,   /* stopped while waiting for the trigger */
	SLOGIC_END_ERROR,
} slogic_end;

typedef struct slogic_report {
	slogic_end end;
	const char *reason;      /* static string for SLOGIC_END_ERROR */
	const char *model;
	int nch;
	uint64_t samplerate_hz;
	uint64_t samples, limit, bytes;
	double mbps, exp_mbps, cold_ms, span_ms;
	uint32_t fit_n;
	uint32_t xfer_size;
	int ring;
	unsigned rearms;
	int triggered;
	uint64_t trig_index, pre;
	uint64_t q_peak_bytes;
} slogic_report;

/* "slogic-report v1 key=value ..." on one line (build/docs/slogic-protocol.md
 * section 9). Returns the snprintf length. */
int slogic_report_format(const slogic_report *r, char *buf, size_t n);

/* ======================================================================
 * Device + capture engine (slogic_usb.c): libusb + pthread.
 *
 * The core owns the device handle, one libusb event thread per open device,
 * the bulk transfer ring and the producer->consumer queue. The host only
 * calls slogic_capture_poll() from a thread of its own choosing (session
 * source, acquisition loop, ...) and receives exact, trimmed samples through
 * its sink. Every control transfer (stop, re-arm, run) happens inside poll /
 * start / destroy on that thread, never in a USB callback.
 *
 * Threading: call everything for one device from one host thread, except
 * slogic_capture_stop(), which may be called from any thread.
 * ====================================================================== */

struct libusb_context;
struct libusb_device;
struct libusb_device_handle;

/* "usb/<bus>-<port>[.<port>...]" — same format as libsigrok's
 * usb_get_port_path(), i.e. sdi->connection_id. */
int slogic_port_path(struct libusb_device *dev, char *buf, size_t n);
/* First SLogic (pid 0 = any known model) matching port_path, else serial;
 * both NULL takes the first one. Finds the analyzer again after it
 * re-enumerated with a new address. *out is referenced (libusb_unref_device). */
int slogic_find(struct libusb_context *ctx, uint16_t pid, const char *port_path,
		const char *serial, struct libusb_device **out);
/* Negotiated link of a (not necessarily open) device. */
slogic_link slogic_usb_link(struct libusb_device *dev);

typedef struct slogic_dev slogic_dev;
/* Open, claim interface 0, enable RAW_IO where libusb supports it, start the
 * event thread, then bring the device to a known idle state (stop, drain,
 * reset) so a capture left running by a killed process does not leak into
 * the next one. */
int slogic_dev_open(struct libusb_context *ctx, struct libusb_device *dev,
		    slogic_log_fn log, void *log_user, slogic_dev **out);
/* Destroys a live capture first. */
void slogic_dev_close(slogic_dev *d);
const slogic_model *slogic_dev_model(const slogic_dev *d);
slogic_link slogic_dev_link(const slogic_dev *d);
struct libusb_device_handle *slogic_dev_handle(const slogic_dev *d);

typedef struct slogic_capture slogic_capture;
typedef struct slogic_capture_config {
	slogic_config dev;             /* channels, samplerate, threshold, pattern */
	uint64_t limit_samples;        /* 0 = continuous until stopped */
	slogic_trigger_spec trigger;   /* all SLOGIC_TRIG_X = untriggered */
	uint64_t pretrigger_samples;   /* host computes, e.g. limit * ratio / 100 */
	slogic_sink sink;
	uint64_t max_queue_bytes;      /* 0 = unbounded */
} slogic_capture_config;

enum { SLOGIC_CAP_RUNNING = 0, SLOGIC_CAP_DONE = 1 };

int slogic_capture_start(slogic_dev *d, const slogic_capture_config *c,
			 slogic_capture **out);
/* Request the end of the capture; any thread, idempotent. */
void slogic_capture_stop(slogic_capture *c);
/* Consumer step: delivers up to max_buffers queued buffers (waiting up to
 * wait_ms for one) and runs re-arm / teardown. Returns SLOGIC_CAP_RUNNING
 * until the capture has fully ended (ring idle, device stopped, report logged
 * at INFO), then SLOGIC_CAP_DONE. */
int slogic_capture_poll(slogic_capture *c, unsigned max_buffers, unsigned wait_ms);
void slogic_capture_report(const slogic_capture *c, slogic_report *r);
/* Stops if needed, waits up to 2 s for the ring to go idle, frees. If the ring
 * never drains the memory is deliberately leaked (a late callback stays
 * memory-safe). No sink callback runs once destroy has started. */
void slogic_capture_destroy(slogic_capture *c);

#ifdef __cplusplus
}
#endif

#endif /* SLOGIC_H */
