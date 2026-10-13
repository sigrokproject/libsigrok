/*
 * libslogic — device + capture engine on libusb and pthread.
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
 * Threads:
 *  - the event thread (one per open device) pumps libusb; other threads of
 *    the host may pump the same context too (all-logic's hotplug thread).
 *    libusb runs completions one at a time, and xfer_cb only touches
 *    atomics and the mutex-protected queue.
 *  - the host thread calling slogic_capture_poll() consumes the queue and
 *    issues every control transfer (the U3 firmware answers BUSY to control
 *    transfers issued from inside a bulk callback).
 * A transfer leaves the in-flight count as the LAST thing its callback does
 * (under the queue lock), so once poll sees 0 no callback can touch the ring
 * or the capture any more and both may be freed.
 */

#if !defined(_WIN32) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE /* clock_gettime/nanosleep under -std=c99 */
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#ifdef _WIN32
#include <windows.h>
#endif

#if defined(__has_include)
#if __has_include(<libusb.h>)
#include <libusb.h>
#else
#include <libusb-1.0/libusb.h>
#endif
#else
#include <libusb.h>
#endif

#include "slogic.h"

/* GCC/Clang builtins: libsigrok and all-logic compile as C99. */
#define SL_LOAD(p)     __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define SL_STORE(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)
#define SL_ADD(p, v)   __atomic_add_fetch((p), (v), __ATOMIC_ACQ_REL)

#define ALIGN             (32u * 1024u)
#define XFER_MIN          ALIGN
#define XFER_MAX          (64u * 1024u * 1024u)
#define PROBE_MS          250
#define TIMEOUT_FLOOR_MS  50
#define STOP_TRIES        10
#define DRAIN_LOOPS       32
#define DRAIN_CHUNK       (64u * 1024u)
#define DESTROY_WAIT_US   (2 * 1000 * 1000)
#define FREELIST_MAX      (SLOGIC_MAX_TRANSFERS + 8)

/* ---------------------------------------------------------------------- */
/* small platform helpers                                                  */
/* ---------------------------------------------------------------------- */

static int64_t now_us(void)
{
#ifdef _WIN32
	static LARGE_INTEGER f;
	LARGE_INTEGER c;
	if (!f.QuadPart)
		QueryPerformanceFrequency(&f);
	QueryPerformanceCounter(&c);
	return (int64_t)(c.QuadPart / f.QuadPart * 1000000 +
			 c.QuadPart % f.QuadPart * 1000000 / f.QuadPart);
#else
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}

static void sleep_ms(unsigned ms)
{
#ifdef _WIN32
	Sleep(ms);
#else
	struct timespec ts = { ms / 1000, (long)(ms % 1000) * 1000000L };
	nanosleep(&ts, NULL);
#endif
}

static void deadline_in(struct timespec *ts, unsigned ms)
{
	clock_gettime(CLOCK_REALTIME, ts);
	ts->tv_nsec += (long)(ms % 1000) * 1000000L;
	ts->tv_sec += ms / 1000 + ts->tv_nsec / 1000000000L;
	ts->tv_nsec %= 1000000000L;
}

/* ---------------------------------------------------------------------- */
/* device                                                                  */
/* ---------------------------------------------------------------------- */

struct slogic_dev {
	struct libusb_context *ctx;
	struct libusb_device *udev;
	struct libusb_device_handle *h;
	const slogic_model *m;
	slogic_link link;
	slogic_transport t;
	pthread_mutex_t ctrl_lock;
	pthread_t evt;
	int evt_started;
	int evt_run;
	slogic_log_fn log;
	void *log_user;
	int raw_io_max;               /* > 0 when RAW_IO is on (Windows) */
	uint32_t probed_size;         /* sizing probe cache */
	int probed_nch;
	uint64_t probed_rate;
	slogic_capture *live;
};

static void dlog(const slogic_dev *d, int level, const char *fmt, ...)
{
	char buf[512];
	va_list ap;

	if (!d || !d->log)
		return;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	d->log(d->log_user, level, buf);
}

static int t_write(void *ctx, uint8_t req, uint16_t val, uint16_t idx,
		   const uint8_t *data, uint16_t len, unsigned timeout_ms)
{
	slogic_dev *d = ctx;
	return libusb_control_transfer(d->h,
		LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT, req, val, idx,
		(unsigned char *)data, len, timeout_ms);
}

static int t_read(void *ctx, uint8_t req, uint16_t val, uint16_t idx,
		  uint8_t *data, uint16_t len, unsigned timeout_ms)
{
	slogic_dev *d = ctx;
	return libusb_control_transfer(d->h,
		LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN, req, val, idx,
		data, len, timeout_ms);
}

slogic_link slogic_usb_link(struct libusb_device *dev)
{
	int sp = libusb_get_device_speed(dev);

	switch (sp) {
	case LIBUSB_SPEED_LOW:
	case LIBUSB_SPEED_FULL: return SLOGIC_LINK_FULL;
	case LIBUSB_SPEED_HIGH: return SLOGIC_LINK_HIGH;
	case LIBUSB_SPEED_SUPER: return SLOGIC_LINK_SUPER;
	default: return sp > LIBUSB_SPEED_SUPER ? SLOGIC_LINK_SUPER_PLUS
						: SLOGIC_LINK_UNKNOWN;
	}
}

int slogic_port_path(struct libusb_device *dev, char *buf, size_t n)
{
	uint8_t ports[8];
	int i, np;
	size_t len;

#if defined(__FreeBSD__) || defined(__APPLE__)
	/* Port numbers need an open handle there (same as libsigrok). */
	struct libusb_device_handle *h;
	if (libusb_open(dev, &h) != 0)
		return SLOGIC_ERR_IO;
#endif
	np = libusb_get_port_numbers(dev, ports, sizeof(ports));
#if defined(__FreeBSD__) || defined(__APPLE__)
	libusb_close(h);
	if (np == 0) {
		ports[0] = libusb_get_device_address(dev);
		np = 1;
	}
#endif
	if (np < 1)
		return SLOGIC_ERR_IO;
	len = (size_t)snprintf(buf, n, "usb/%d-%d", libusb_get_bus_number(dev),
			       ports[0]);
	for (i = 1; i < np && len < n; i++)
		len += (size_t)snprintf(buf + len, n - len, ".%d", ports[i]);
	return SLOGIC_OK;
}

static int read_serial(struct libusb_device *dev, uint8_t idx, char *buf,
		       size_t n)
{
	struct libusb_device_handle *h;
	int r;

	if (!idx || libusb_open(dev, &h) != 0)
		return SLOGIC_ERR_IO;
	r = libusb_get_string_descriptor_ascii(h, idx, (unsigned char *)buf,
					       (int)n);
	libusb_close(h);
	return r > 0 ? SLOGIC_OK : SLOGIC_ERR_IO;
}

int slogic_find(struct libusb_context *ctx, uint16_t pid, const char *port_path,
		const char *serial, struct libusb_device **out)
{
	struct libusb_device **list, *match = NULL;
	struct libusb_device_descriptor des;
	char buf[64];
	ssize_t n, i;
	int pass;

	if (!out)
		return SLOGIC_ERR_ARG;
	*out = NULL;
	if ((n = libusb_get_device_list(ctx, &list)) < 0)
		return SLOGIC_ERR_IO;

	for (pass = 0; pass < 3 && !match; pass++) {
		for (i = 0; i < n && !match; i++) {
			if (libusb_get_device_descriptor(list[i], &des) != 0 ||
			    des.idVendor != SLOGIC_VID ||
			    !slogic_model_for_pid(des.idProduct) ||
			    (pid && des.idProduct != pid))
				continue;
			if (pass == 0 && port_path) {
				if (slogic_port_path(list[i], buf, sizeof(buf)) == SLOGIC_OK &&
				    !strcmp(buf, port_path))
					match = list[i];
			} else if (pass == 1 && serial) {
				if (read_serial(list[i], des.iSerialNumber, buf,
						sizeof(buf)) == SLOGIC_OK &&
				    !strcmp(buf, serial))
					match = list[i];
			} else if (pass == 2 && !port_path && !serial) {
				match = list[i];
			}
		}
	}
	if (match)
		*out = libusb_ref_device(match);
	libusb_free_device_list(list, 1);
	return match ? SLOGIC_OK : SLOGIC_ERR_NODEV;
}

static void *event_thread(void *arg)
{
	slogic_dev *d = arg;

	while (SL_LOAD(&d->evt_run)) {
		struct timeval tv = { 0, 100000 };
		libusb_handle_events_timeout_completed(d->ctx, &tv, NULL);
	}
	return NULL;
}

/* Read the bulk endpoint dry (Combo 8 stop, stale data after a kill). */
static void drain_ep(slogic_dev *d)
{
	uint8_t *buf = malloc(DRAIN_CHUNK);
	int got = 0, i;

	if (!buf)
		return;
	for (i = 0; i < DRAIN_LOOPS; i++) {
		got = 0;
		if (libusb_bulk_transfer(d->h, d->m->ep_in, buf, DRAIN_CHUNK, &got,
					 50) != 0 || got == 0)
			break;
	}
	free(buf);
}

/* Stop the device, retried: the FPGA keeps streaming into its buffer until it
 * is told to stop, so every teardown path must get this through. */
static int dev_stop(slogic_dev *d, int tries)
{
	int r = SLOGIC_ERR, i;

	if (d->m->proto == SLOGIC_PROTO_COMBO8) {
		drain_ep(d);
		return SLOGIC_OK;
	}
	for (i = 0; i < tries; i++) {
		pthread_mutex_lock(&d->ctrl_lock);
		r = slogic_stop(d->m, &d->t);
		pthread_mutex_unlock(&d->ctrl_lock);
		if (r == SLOGIC_OK)
			break;
		sleep_ms(10);
	}
	if (r != SLOGIC_OK)
		dlog(d, SLOGIC_LOG_WARN, "device STOP failed after %d tries", tries);
	return r;
}

/* reset -> clear halt -> configure, the start of every (re)run. */
static int dev_prepare(slogic_dev *d, const slogic_config *c)
{
	int r;

	pthread_mutex_lock(&d->ctrl_lock);
	r = slogic_reset(d->m, &d->t);
	if (r == SLOGIC_OK) {
#ifndef SLOGIC_NO_CLEAR_HALT
		int ch = libusb_clear_halt(d->h, d->m->ep_in);
#else
		int ch = 0;
#endif
		if (ch)
			dlog(d, SLOGIC_LOG_DBG, "clear_halt: %s", libusb_error_name(ch));
		r = slogic_configure(d->m, &d->t, c);
	}
	pthread_mutex_unlock(&d->ctrl_lock);
	return r;
}

static int dev_run(slogic_dev *d, const slogic_config *c)
{
	int r;

	pthread_mutex_lock(&d->ctrl_lock);
	r = slogic_run(d->m, &d->t, c);
	pthread_mutex_unlock(&d->ctrl_lock);
	return r;
}

int slogic_dev_open(struct libusb_context *ctx, struct libusb_device *dev,
		    slogic_log_fn log, void *log_user, slogic_dev **out)
{
	struct libusb_device_descriptor des;
	slogic_dev *d;
	int r;

	if (!dev || !out)
		return SLOGIC_ERR_ARG;
	*out = NULL;
	if (libusb_get_device_descriptor(dev, &des) != 0)
		return SLOGIC_ERR_IO;
	d = calloc(1, sizeof(*d));
	if (!d)
		return SLOGIC_ERR;
	d->ctx = ctx;
	d->log = log;
	d->log_user = log_user;
	d->m = slogic_model_for_pid(des.idProduct);
	if (!d->m) {
		free(d);
		return SLOGIC_ERR_NODEV;
	}
	r = libusb_open(dev, &d->h);
	if (r) {
		dlog(d, SLOGIC_LOG_ERR, "open %s: %s", d->m->name, libusb_error_name(r));
		free(d);
		return r == LIBUSB_ERROR_NO_DEVICE ? SLOGIC_ERR_NODEV : SLOGIC_ERR_IO;
	}
	libusb_set_auto_detach_kernel_driver(d->h, 1);
	r = libusb_claim_interface(d->h, 0);
	if (r) {
		if (r == LIBUSB_ERROR_BUSY)
			dlog(d, SLOGIC_LOG_ERR, "Unable to claim USB interface. "
			     "Another program or driver has already claimed it.");
		else
			dlog(d, SLOGIC_LOG_ERR, "Unable to claim USB interface: %s.",
			     libusb_error_name(r));
		libusb_close(d->h);
		free(d);
		return r == LIBUSB_ERROR_BUSY ? SLOGIC_ERR_BUSY :
		       r == LIBUSB_ERROR_NO_DEVICE ? SLOGIC_ERR_NODEV : SLOGIC_ERR_IO;
	}
	d->udev = libusb_ref_device(dev);
	d->link = slogic_usb_link(dev);
	d->t.ctx = d;
	d->t.control_write = t_write;
	d->t.control_read = t_read;
	pthread_mutex_init(&d->ctrl_lock, NULL);

#if defined(LIBUSB_API_VERSION) && (LIBUSB_API_VERSION >= 0x0100010C)
	/* WinUSB keeps a single bulk-IN in flight without RAW_IO (libusb #490);
	 * a no-op error elsewhere. Transfers are sized under the RAW_IO max. */
	if (libusb_endpoint_set_raw_io(d->h, d->m->ep_in, 1) == LIBUSB_SUCCESS) {
		d->raw_io_max = libusb_get_max_raw_io_transfer_size(d->h, d->m->ep_in);
		dlog(d, SLOGIC_LOG_INFO, "RAW_IO enabled on ep 0x%02x, max_raw_io=%d",
		     d->m->ep_in, d->raw_io_max);
	}
#endif

	SL_STORE(&d->evt_run, 1);
	if (pthread_create(&d->evt, NULL, event_thread, d) != 0) {
		dlog(d, SLOGIC_LOG_ERR, "cannot start the USB event thread");
		libusb_release_interface(d->h, 0);
		libusb_close(d->h);
		libusb_unref_device(d->udev);
		pthread_mutex_destroy(&d->ctrl_lock);
		free(d);
		return SLOGIC_ERR;
	}
	d->evt_started = 1;

	/* Known idle state, also after a killed capture: stop, drain, reset. */
	dev_stop(d, 2);
	if (d->m->proto != SLOGIC_PROTO_COMBO8) {
#ifndef SLOGIC_NO_CLEAR_HALT
		libusb_clear_halt(d->h, d->m->ep_in);
#endif
		drain_ep(d);
	}
	pthread_mutex_lock(&d->ctrl_lock);
	slogic_reset(d->m, &d->t);
	pthread_mutex_unlock(&d->ctrl_lock);

	dlog(d, SLOGIC_LOG_INFO, "opened %s on %s link", d->m->name,
	     slogic_link_name(d->link));
	*out = d;
	return SLOGIC_OK;
}

void slogic_dev_close(slogic_dev *d)
{
	if (!d)
		return;
	if (d->live)
		slogic_capture_destroy(d->live);
	if (d->evt_started) {
		SL_STORE(&d->evt_run, 0);
		libusb_interrupt_event_handler(d->ctx);
		pthread_join(d->evt, NULL);
	}
	libusb_release_interface(d->h, 0);
	libusb_close(d->h);
	libusb_unref_device(d->udev);
	pthread_mutex_destroy(&d->ctrl_lock);
	free(d);
}

const slogic_model *slogic_dev_model(const slogic_dev *d) { return d->m; }
slogic_link slogic_dev_link(const slogic_dev *d) { return d->link; }
struct libusb_device_handle *slogic_dev_handle(const slogic_dev *d) { return d->h; }

/* ---------------------------------------------------------------------- */
/* capture                                                                 */
/* ---------------------------------------------------------------------- */

struct qnode {
	struct qnode *next;
	uint8_t *buf;
	size_t len;
};

enum { ST_RUN, ST_REARM, ST_TEARDOWN, ST_STOPDEV, ST_DONE };

struct slogic_capture {
	slogic_dev *d;
	slogic_capture_config cfg;
	uint64_t need_raw;            /* raw bytes for an untriggered finite capture */
	uint64_t rate_bytes;          /* bytes/s on the wire */

	struct libusb_transfer *xfer[SLOGIC_MAX_TRANSFERS];
	int nxfer;
	uint32_t size;
	unsigned dur_ms;

	/* shared with the callback (atomics) */
	int in_flight;
	int aborted;
	int rearm_req;
	int stop_req;
	const char *reason;           /* first error wins */

	/* callback-only (completions are serialised by libusb) */
	slogic_stream watch;
	slogic_ratefit fit;
	uint64_t raw_bytes;
	int64_t t_prev;

	/* queue */
	pthread_mutex_t qmx;
	pthread_cond_t qcv;
	struct qnode *qh, *qt;
	uint64_t qbytes, qpeak;
	uint8_t *freelist[FREELIST_MAX];
	int nfree;

	/* consumer-only */
	slogic_acc acc;
	int st;
	int dev_stopped;              /* STOP already sent for this capture */
	unsigned rearms;
	int64_t t_run;
	slogic_end end;
};

static void set_abort(slogic_capture *c, const char *reason)
{
	if (reason) {
		const char *expected = NULL;
		__atomic_compare_exchange_n(&c->reason, &expected, reason, 0,
					    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
	}
	SL_STORE(&c->aborted, 1);
}

static uint8_t *take_buf(slogic_capture *c)
{
	uint8_t *b = NULL;

	pthread_mutex_lock(&c->qmx);
	if (c->nfree)
		b = c->freelist[--c->nfree];
	pthread_mutex_unlock(&c->qmx);
	return b ? b : malloc(c->size);
}

static void give_buf(slogic_capture *c, uint8_t *b)
{
	pthread_mutex_lock(&c->qmx);
	if (c->nfree < FREELIST_MAX) {
		c->freelist[c->nfree++] = b;
		b = NULL;
	}
	pthread_mutex_unlock(&c->qmx);
	free(b);
}

static void enqueue(slogic_capture *c, uint8_t *buf, size_t len)
{
	struct qnode *n = malloc(sizeof(*n));
	int over;

	if (!n) {
		free(buf);
		set_abort(c, "oom");
		return;
	}
	n->next = NULL;
	n->buf = buf;
	n->len = len;
	pthread_mutex_lock(&c->qmx);
	if (c->qt)
		c->qt->next = n;
	else
		c->qh = n;
	c->qt = n;
	c->qbytes += len;
	if (c->qbytes > c->qpeak)
		c->qpeak = c->qbytes;
	over = c->cfg.max_queue_bytes && c->qbytes > c->cfg.max_queue_bytes;
	pthread_cond_broadcast(&c->qcv);
	pthread_mutex_unlock(&c->qmx);
	if (over)
		set_abort(c, "host-too-slow");
}

static struct qnode *dequeue(slogic_capture *c, unsigned wait_ms)
{
	struct qnode *n;

	pthread_mutex_lock(&c->qmx);
	if (!c->qh && wait_ms && SL_LOAD(&c->in_flight)) {
		struct timespec ts;
		deadline_in(&ts, wait_ms);
		pthread_cond_timedwait(&c->qcv, &c->qmx, &ts);
	}
	n = c->qh;
	if (n) {
		c->qh = n->next;
		if (!c->qh)
			c->qt = NULL;
		c->qbytes -= n->len;
	}
	pthread_mutex_unlock(&c->qmx);
	return n;
}

static unsigned xfer_timeout(const slogic_capture *c, int others)
{
	unsigned t = (unsigned)(1.3 * c->dur_ms * (others + 2));
	return t < TIMEOUT_FLOOR_MS ? TIMEOUT_FLOOR_MS : t;
}

static void LIBUSB_CALL xfer_cb(struct libusb_transfer *x)
{
	slogic_capture *c = x->user_data;
	slogic_dev *d = c->d;
	int64_t now = now_us(), since = now - c->t_prev;
	int resubmitted = 0;
	int others = SL_LOAD(&c->in_flight) - 1;
	uint8_t *filled = NULL;
	size_t len = 0;

	c->t_prev = now;
	switch (x->status) {
	case LIBUSB_TRANSFER_COMPLETED:
	case LIBUSB_TRANSFER_TIMED_OUT:
		len = (size_t)x->actual_length;
		if (len && !SL_LOAD(&c->aborted) && !SL_LOAD(&c->rearm_req)) {
			c->raw_bytes += len;
			slogic_ratefit_add(&c->fit, now, len);
		}
		switch (slogic_stream_watch(&c->watch, len, now, since)) {
		case SLOGIC_STREAM_WARN_SLOW:
			dlog(d, SLOGIC_LOG_WARN, "USB link slower than the analyzer; "
			     "draining at the host's pace.");
			break;
		case SLOGIC_STREAM_RETRY_RUN:
			dlog(d, SLOGIC_LOG_WARN, "USB stream did not start; re-arming.");
			SL_STORE(&c->rearm_req, 1);
			break;
		case SLOGIC_STREAM_ABORT:
			dlog(d, SLOGIC_LOG_ERR, "acquisition stall: aborting capture.");
			set_abort(c, "stall");
			break;
		default:
			break;
		}
		dlog(d, SLOGIC_LOG_SPEW, "slogic-xfer t_ms=%.3f len=%zu cum=%llu st=%d",
		     (double)(now - c->t_run) / 1000.0, len,
		     (unsigned long long)c->raw_bytes, (int)x->status);

		/* Swap in a fresh buffer and resubmit BEFORE handing the filled
		 * one over, so the bulk pipe never idles on host processing. */
		if (len) {
			uint8_t *fresh = take_buf(c);
			if (!fresh) {
				set_abort(c, "oom");
			} else {
				filled = x->buffer;
				x->buffer = fresh;
			}
		}
		if (!SL_LOAD(&c->aborted) && !SL_LOAD(&c->rearm_req) &&
		    (!c->need_raw ||
		     c->raw_bytes + (uint64_t)(others > 0 ? others : 0) * c->size <
			     c->need_raw)) {
			int r;
			x->length = (int)c->size;
			x->timeout = xfer_timeout(c, others);
			r = libusb_submit_transfer(x);
			if (r == 0)
				resubmitted = 1;
			else
				set_abort(c, r == LIBUSB_ERROR_NO_DEVICE ? "no-device"
									  : "usb-submit");
		}
		if (filled)
			enqueue(c, filled, len);
		break;
	case LIBUSB_TRANSFER_CANCELLED:
		if (!SL_LOAD(&c->rearm_req))
			set_abort(c, NULL);
		break;
	case LIBUSB_TRANSFER_NO_DEVICE:
		set_abort(c, "no-device");
		break;
	default:
		set_abort(c, "usb-error");
		break;
	}

	/* Last touch of the transfer and of the capture. */
	if (!resubmitted) {
		pthread_mutex_lock(&c->qmx);
		if (SL_ADD(&c->in_flight, -1) == 0) {
			if (!SL_LOAD(&c->rearm_req))
				SL_STORE(&c->aborted, 1); /* ring done */
			pthread_cond_broadcast(&c->qcv);
		}
		pthread_mutex_unlock(&c->qmx);
	}
}

/* 250 ms of data, halved while the OS refuses it (usbfs memory limit), under
 * the RAW_IO maximum on Windows, then a quarter so several are in flight. */
static uint32_t probe_size(slogic_capture *c)
{
	slogic_dev *d = c->d;
	uint64_t bpms = c->rate_bytes / 1000, sz;

	if (!bpms)
		bpms = 1;
	if (d->probed_size && d->probed_nch == c->cfg.dev.channel_count &&
	    d->probed_rate == c->cfg.dev.samplerate_hz)
		return d->probed_size;

	sz = (PROBE_MS * bpms + ALIGN - 1) / ALIGN * ALIGN;
	if (d->raw_io_max > 0 && sz > (uint64_t)d->raw_io_max)
		sz = (uint64_t)d->raw_io_max & ~(uint64_t)(ALIGN - 1);
	if (sz > (uint64_t)XFER_MAX * 4)
		sz = (uint64_t)XFER_MAX * 4;
	while (sz > XFER_MIN) {
		struct libusb_transfer *x = libusb_alloc_transfer(0);
		uint8_t *b = malloc(sz);
		int r;
		if (!x || !b) {
			libusb_free_transfer(x);
			free(b);
			sz /= 2;
			continue;
		}
		libusb_fill_bulk_transfer(x, d->h, d->m->ep_in, b, (int)sz, NULL,
					  NULL, 0);
		x->flags |= LIBUSB_TRANSFER_FREE_BUFFER | LIBUSB_TRANSFER_FREE_TRANSFER;
		r = libusb_submit_transfer(x);
		if (r == LIBUSB_ERROR_NO_MEM) {
			libusb_free_transfer(x); /* frees b too */
			sz /= 2;
			continue;
		}
		if (r) {
			libusb_free_transfer(x);
			break;
		}
		libusb_cancel_transfer(x); /* freed by libusb on completion */
		break;
	}
	sz /= 4;
	sz = sz / ALIGN * ALIGN;
	if (sz < XFER_MIN)
		sz = XFER_MIN;
	if (sz > XFER_MAX)
		sz = XFER_MAX;
	d->probed_size = (uint32_t)sz;
	d->probed_nch = c->cfg.dev.channel_count;
	d->probed_rate = c->cfg.dev.samplerate_hz;
	return d->probed_size;
}

static void free_ring(slogic_capture *c)
{
	int i;

	for (i = 0; i < SLOGIC_MAX_TRANSFERS; i++) {
		if (c->xfer[i]) {
			free(c->xfer[i]->buffer);
			libusb_free_transfer(c->xfer[i]);
			c->xfer[i] = NULL;
		}
	}
}

static void cancel_ring(slogic_capture *c)
{
	int i;

	for (i = 0; i < SLOGIC_MAX_TRANSFERS; i++)
		if (c->xfer[i])
			libusb_cancel_transfer(c->xfer[i]);
}

static int submit_ring(slogic_capture *c)
{
	slogic_dev *d = c->d;
	int i, want = SLOGIC_MAX_TRANSFERS;

	if (c->need_raw) {
		uint64_t k = (c->need_raw + c->size - 1) / c->size;
		if (k < (uint64_t)want)
			want = (int)k;
	}
	for (i = 0; i < want; i++) {
		struct libusb_transfer *x = libusb_alloc_transfer(0);
		uint8_t *b = malloc(c->size);
		int r;
		if (!x || !b) {
			libusb_free_transfer(x);
			free(b);
			break;
		}
		libusb_fill_bulk_transfer(x, d->h, d->m->ep_in, b, (int)c->size,
					  xfer_cb, c, xfer_timeout(c, i));
		c->xfer[i] = x;
		SL_ADD(&c->in_flight, 1); /* before submit: it may complete at once */
		r = libusb_submit_transfer(x);
		if (r) {
			dlog(d, SLOGIC_LOG_DBG, "submit transfer %d: %s", i,
			     libusb_error_name(r));
			SL_ADD(&c->in_flight, -1);
			c->xfer[i] = NULL;
			free(b);
			libusb_free_transfer(x);
			break;
		}
	}
	c->nxfer = i;
	return i ? SLOGIC_OK : SLOGIC_ERR_IO;
}

/* Cancel and wait for the ring from the host thread; leaks on timeout. */
static int drain_ring_sync(slogic_capture *c, int64_t budget_us)
{
	int64_t deadline = now_us() + budget_us;

	while (SL_LOAD(&c->in_flight) && now_us() < deadline) {
		cancel_ring(c);
		sleep_ms(5);
	}
	if (SL_LOAD(&c->in_flight))
		return SLOGIC_ERR_TIMEOUT;
	free_ring(c);
	return SLOGIC_OK;
}

static void stream_rearm_reset(slogic_capture *c)
{
	slogic_transfer_plan plan = {
		.size_bytes = c->size,
		.ring_count = c->nxfer ? c->nxfer : 1,
		.expected_rate_bytes = c->rate_bytes,
		.timeout_ms = xfer_timeout(c, 0),
	};
	int retried = c->watch.run_retried;

	/* need_bytes 0: DONE would switch idle detection off, and the limit is
	 * enforced by the accounting, not by the watchdog. */
	slogic_stream_init(&c->watch, &plan, 0);
	c->watch.run_retried = retried;
	c->raw_bytes = 0;
}

static void capture_free(slogic_capture *c)
{
	struct qnode *n;
	int i;

	while ((n = c->qh)) {
		c->qh = n->next;
		free(n->buf);
		free(n);
	}
	for (i = 0; i < c->nfree; i++)
		free(c->freelist[i]);
	slogic_acc_free(&c->acc);
	pthread_cond_destroy(&c->qcv);
	pthread_mutex_destroy(&c->qmx);
	free(c);
}

int slogic_capture_start(slogic_dev *d, const slogic_capture_config *cfg,
			 slogic_capture **out)
{
	slogic_capture *c;
	int nch, r;

	if (!d || !cfg || !out)
		return SLOGIC_ERR_ARG;
	*out = NULL;
	if (d->live)
		return SLOGIC_ERR_BUSY;
	nch = cfg->dev.channel_count;
	if (!slogic_max_rate(d->m, nch) || !cfg->dev.samplerate_hz)
		return SLOGIC_ERR_ARG;

	c = calloc(1, sizeof(*c));
	if (!c)
		return SLOGIC_ERR;
	c->d = d;
	c->cfg = *cfg;
	pthread_mutex_init(&c->qmx, NULL);
	pthread_cond_init(&c->qcv, NULL);
	r = slogic_acc_init(&c->acc, nch, cfg->limit_samples, &cfg->trigger,
			    cfg->pretrigger_samples, d->m->first_drop_bytes,
			    &cfg->sink);
	if (r != SLOGIC_OK) {
		capture_free(c);
		return r;
	}
	c->rate_bytes = cfg->dev.samplerate_hz * (uint64_t)nch / 8;
	if (cfg->limit_samples && !c->acc.has_trig)
		c->need_raw = slogic_bytes_for_samples(nch, cfg->limit_samples) +
			      d->m->first_drop_bytes;

	dev_stop(d, 2);
	r = dev_prepare(d, &cfg->dev);
	if (r != SLOGIC_OK) {
		dlog(d, SLOGIC_LOG_ERR, "configure failed (%d)", r);
		capture_free(c);
		return r == SLOGIC_ERR_ARG ? r : SLOGIC_ERR_IO;
	}

	c->size = probe_size(c);
	if (c->need_raw && c->size > c->need_raw)
		c->size = (uint32_t)((c->need_raw + ALIGN - 1) / ALIGN * ALIGN);
	c->dur_ms = (unsigned)(c->size / (c->rate_bytes / 1000 ? c->rate_bytes / 1000 : 1));
	if (!c->dur_ms)
		c->dur_ms = 1;

	c->t_prev = now_us();
	slogic_ratefit_init(&c->fit, 0);
	if (submit_ring(c) != SLOGIC_OK) {
		dlog(d, SLOGIC_LOG_ERR, "cannot submit any USB transfer");
		capture_free(c);
		return SLOGIC_ERR_IO;
	}
	stream_rearm_reset(c);

	r = dev_run(d, &cfg->dev);
	c->t_run = now_us();
	c->fit.t_run = c->t_run;
	if (r != SLOGIC_OK) {
		dlog(d, SLOGIC_LOG_ERR, "RUN failed (%d)", r);
		set_abort(c, "run");
		if (drain_ring_sync(c, DESTROY_WAIT_US) != SLOGIC_OK) {
			dlog(d, SLOGIC_LOG_ERR, "transfers did not drain; leaking them");
			return SLOGIC_ERR_IO;
		}
		dev_stop(d, STOP_TRIES);
		capture_free(c);
		return SLOGIC_ERR_IO;
	}

	dlog(d, SLOGIC_LOG_DBG, "capture: %dch@%gMHz limit=%llu xfer=%ux%d (%ums)%s",
	     nch, (double)cfg->dev.samplerate_hz / 1e6,
	     (unsigned long long)cfg->limit_samples, c->size, c->nxfer, c->dur_ms,
	     c->acc.has_trig ? " triggered" : "");
	c->st = ST_RUN;
	d->live = c;
	*out = c;
	return SLOGIC_OK;
}

void slogic_capture_stop(slogic_capture *c)
{
	if (!c)
		return;
	SL_STORE(&c->stop_req, 1);
	pthread_mutex_lock(&c->qmx);
	pthread_cond_broadcast(&c->qcv);
	pthread_mutex_unlock(&c->qmx);
}

/* Feed a dequeued buffer if the capture still wants data, then recycle it. */
static void consume(slogic_capture *c, struct qnode *n, int live)
{
	slogic_acc *a = &c->acc;
	int wants = live || (!(a->has_trig && !a->triggered) &&
			     !(a->limit && a->delivered >= a->limit) &&
			     !a->sink_stop);

	if (wants && slogic_acc_feed(a, n->buf, n->len) == SLOGIC_ACC_DONE &&
	    !SL_LOAD(&c->aborted))
		set_abort(c, NULL); /* limit reached / sink asked to stop */
	give_buf(c, n->buf);
	free(n);
}

static void build_report(const slogic_capture *c, slogic_report *r)
{
	const slogic_dev *d = c->d;
	double mbps = 0, cold = 0;

	memset(r, 0, sizeof(*r));
	r->end = c->end;
	r->reason = __atomic_load_n(&c->reason, __ATOMIC_ACQUIRE);
	r->model = d->m->name;
	r->nch = c->cfg.dev.channel_count;
	r->samplerate_hz = c->cfg.dev.samplerate_hz;
	r->samples = c->acc.delivered;
	r->limit = c->cfg.limit_samples;
	r->bytes = c->fit.bytes;
	r->exp_mbps = (double)c->rate_bytes / 1e6;
	r->fit_n = c->fit.n;
	if (slogic_ratefit_get(&c->fit, &mbps, &cold) == SLOGIC_OK) {
		r->mbps = mbps;
		r->cold_ms = cold;
	}
	r->span_ms = c->fit.n ? (double)(c->fit.t_last - c->fit.t_first) / 1000.0 : 0;
	r->xfer_size = c->size;
	r->ring = c->nxfer;
	r->rearms = c->rearms;
	r->triggered = c->acc.triggered;
	r->trig_index = c->acc.trig_index;
	r->pre = c->acc.trig_index;
	r->q_peak_bytes = c->qpeak;
}

void slogic_capture_report(const slogic_capture *c, slogic_report *r)
{
	build_report(c, r);
}

static int rearm(slogic_capture *c)
{
	slogic_dev *d = c->d;
	struct qnode *n;

	free_ring(c);
	while ((n = dequeue(c, 0))) { /* nothing was delivered before a re-arm */
		give_buf(c, n->buf);
		free(n);
	}
	slogic_acc_rearm(&c->acc);
	dev_stop(d, 2);
	if (dev_prepare(d, &c->cfg.dev) != SLOGIC_OK)
		return SLOGIC_ERR_IO;
	SL_STORE(&c->rearm_req, 0);
	c->t_prev = now_us();
	slogic_ratefit_init(&c->fit, 0);
	if (submit_ring(c) != SLOGIC_OK)
		return SLOGIC_ERR_IO;
	stream_rearm_reset(c);
	if (dev_run(d, &c->cfg.dev) != SLOGIC_OK)
		return SLOGIC_ERR_IO;
	c->t_run = now_us();
	c->fit.t_run = c->t_run;
	c->rearms++;
	dlog(d, SLOGIC_LOG_INFO, "acquisition stall recovery: restarted capture.");
	return SLOGIC_OK;
}

int slogic_capture_poll(slogic_capture *c, unsigned max_buffers, unsigned wait_ms)
{
	struct qnode *n;
	unsigned i;

	if (!c)
		return SLOGIC_CAP_DONE;

	if (c->st == ST_RUN) {
		if (SL_LOAD(&c->rearm_req) && !SL_LOAD(&c->aborted)) {
			c->st = ST_REARM;
		} else if (SL_LOAD(&c->stop_req) || SL_LOAD(&c->aborted)) {
			set_abort(c, NULL);
			c->st = ST_TEARDOWN;
		} else {
			for (i = 0; i < max_buffers; i++) {
				if (!(n = dequeue(c, i ? 0 : wait_ms)))
					break;
				consume(c, n, 1);
				if (SL_LOAD(&c->aborted))
					break;
			}
			return SLOGIC_CAP_RUNNING;
		}
	}

	if (c->st == ST_REARM) {
		cancel_ring(c);
		if (SL_LOAD(&c->in_flight))
			return SLOGIC_CAP_RUNNING;
		if (rearm(c) != SLOGIC_OK) {
			set_abort(c, "rearm");
			c->st = ST_TEARDOWN;
		} else {
			c->st = ST_RUN;
			return SLOGIC_CAP_RUNNING;
		}
	}

	if (c->st == ST_TEARDOWN) {
		/* Stop the device BEFORE cancelling the ring: the transfers still
		 * in flight keep draining the device while it stops. Cancelling
		 * first leaves the FPGA streaming into an endpoint nobody reads,
		 * and current firmware then resets itself off the bus. */
		if (!c->dev_stopped) {
			const char *why = __atomic_load_n(&c->reason, __ATOMIC_ACQUIRE);
			c->dev_stopped = 1;
			if (c->d->m->proto != SLOGIC_PROTO_COMBO8 &&
			    !(why && !strcmp(why, "no-device")))
				dev_stop(c->d, STOP_TRIES);
		}
		if (SL_LOAD(&c->in_flight))
			cancel_ring(c);
		/* Deliver what the capture still wants; drop the rest (an
		 * untriggered backlog is not trigger-scanned after a stop). */
		while ((n = dequeue(c, 0)))
			consume(c, n, 0);
		if (SL_LOAD(&c->in_flight)) {
			pthread_mutex_lock(&c->qmx);
			if (SL_LOAD(&c->in_flight) && !c->qh) {
				struct timespec ts;
				deadline_in(&ts, 5);
				pthread_cond_timedwait(&c->qcv, &c->qmx, &ts);
			}
			pthread_mutex_unlock(&c->qmx);
			return SLOGIC_CAP_RUNNING;
		}
		free_ring(c);
		c->st = ST_STOPDEV;
	}

	if (c->st == ST_STOPDEV) {
		slogic_acc *a = &c->acc;
		const char *reason;
		slogic_report rep;
		char line[512];

		if (c->d->m->proto == SLOGIC_PROTO_COMBO8)
			drain_ep(c->d); /* Combo 8: draining the EP is the stop */
		while ((n = dequeue(c, 0)))
			consume(c, n, 0);

		reason = __atomic_load_n(&c->reason, __ATOMIC_ACQUIRE);
		if (reason)
			c->end = SLOGIC_END_ERROR;
		else if (a->has_trig && !a->triggered)
			c->end = SLOGIC_END_NOTRIG;
		else if (!a->limit || a->delivered >= a->limit || a->sink_stop)
			c->end = SLOGIC_END_OK;
		else if (SL_LOAD(&c->stop_req))
			c->end = SLOGIC_END_STOPPED;
		else {
			c->end = SLOGIC_END_ERROR;
			__atomic_store_n(&c->reason, "short", __ATOMIC_RELEASE);
		}
		build_report(c, &rep);
		slogic_report_format(&rep, line, sizeof(line));
		dlog(c->d, c->end == SLOGIC_END_ERROR ? SLOGIC_LOG_ERR : SLOGIC_LOG_INFO,
		     "%s", line);
		c->st = ST_DONE;
	}
	return SLOGIC_CAP_DONE;
}

void slogic_capture_destroy(slogic_capture *c)
{
	slogic_dev *d;
	int64_t deadline;

	if (!c)
		return;
	d = c->d;
	/* No sink callback once destroy has started. */
	c->acc.sink.data = NULL;
	c->acc.sink.trigger = NULL;
	if (c->st != ST_DONE) {
		slogic_capture_stop(c);
		deadline = now_us() + DESTROY_WAIT_US;
		while (slogic_capture_poll(c, 16, 5) != SLOGIC_CAP_DONE) {
			if (now_us() > deadline) {
				dlog(d, SLOGIC_LOG_ERR,
				     "transfers did not drain; leaking the capture");
				d->live = NULL;
				dev_stop(d, STOP_TRIES);
				return;
			}
		}
	}
	d->live = NULL;
	capture_free(c);
}
