// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (C) 2026 whatshu <shussm@qq.com>
 *
 * FNIRSI DLA-32 experimental driver. Protocol reference:
 * https://github.com/TechBirdCompany/PXView-DLA32-overlay (GPL-3.0).
 * Command framing and CRC checked against factory DLA-Logic 1.0.3.
 */

#include <config.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <libsigrok/libsigrok.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#define O_BINARY 0
#endif
#include "libsigrok-internal.h"
#include "protocol.h"

#define LOG_PREFIX "fnirsi-dla32"
#define NUM_TRANSFERS 8
#define TRANSFER_SIZE (1024 * 1024)
#define TRANSFER_TIMEOUT_MS 1000
#define USB_POLL_MS 10
#define WCH_POLL_MS 1
#define BUFFER_MEMORY_BITS (UINT64_C(4) * 1024 * 1024 * 1024)
#define DEFAULT_LIMIT_SAMPLES 50000
#define LOGIC_ONLY_MAX_LIMIT_SAMPLES UINT64_C(100000000)
#define CONTINUOUS_WINDOW_MSEC 1000
#define STOP_DRAIN_USEC (3 * G_TIME_SPAN_SECOND)
#define STOP_DRAIN_BYTES (UINT64_C(64) * 1024 * 1024)
#define STOP_EMPTY_READS 2
#define DLA_PREARM_EMPTY_READS 1
#define DATA_STALL_USEC (3 * G_TIME_SPAN_SECOND)

#include "transport-wch.h"
#include "worker-wch.h"

struct pwm_setting {
	uint32_t frequency;
	unsigned int duty;
	gboolean enabled;
	/* This is commanded state, not hardware readback. */
	gboolean known;
};

#include "stream-settle-policy.h"

struct dev_context {
	gint64 stream_retired_us;
	gboolean run_was_stream, run_settle_enabled;
#ifdef _WIN32
	struct dla_wch wch;
	gboolean use_wch;
	/* Hardware STOP ownership is independent of the optional SDK queue.
	 * Keep the existing internal name; survives END in both SDK0 and SDK1. */
	gboolean upload_stop_pending;
	uint8_t *wch_buffer;
#if DLA_WCH_WORKER
	struct dla_wch_worker *worker;
	HANDLE worker_ready;
	GPollFD worker_pollfd;
	gboolean wch_disconnected;
	uint64_t worker_generation, worker_unconsumed_bytes;
	gboolean worker_dispatching;
	FILE *worker_unconsumed_trace;
#endif
#endif
	struct sr_sw_limits limits;
	uint64_t samplerate;
	uint64_t capture_ratio;
	int threshold;
	gboolean stream, continuous, usb3, logic_only;
	struct pwm_setting pwm[4];
	struct dla_decoder decoder;
	uint32_t channel_mask;
	uint64_t run_limit_samples;
	/* Passive host outcome, independent of cached waveform/PWM settings. */
	const char *capture_status, *capture_error;
	struct sr_context *usb_ctx;
	struct libusb_transfer *transfers[NUM_TRANSFERS];
	gboolean transfer_submitted[NUM_TRANSFERS];
	unsigned int submitted_transfers;
	gboolean acquiring, starting, stopping, stop_sent, cancel_requested;
	gboolean source_added, header_sent;
	gboolean data_started, has_trigger;
	int64_t last_data_us, first_data_timeout_us;
	size_t pending;
	uint8_t wire_tail[DLA_CHANNELS];
	uint8_t *decoded;
	size_t decoded_size;
	/* Optional diagnostic: the exact concatenated inputs to the decoder.
	 * Drain bytes are logged separately and never mixed into this file. */
	FILE *wire_trace;
	uint64_t wire_trace_bytes;
	int64_t wire_trace_started_us;
	gboolean wire_trace_failed;
};

#include "capture-status.h"

static gboolean has_application_resources(const struct dev_context *devc)
{
	return devc->acquiring || devc->source_added || devc->submitted_transfers
#if defined(_WIN32) && DLA_WCH_WORKER
		|| devc->worker || devc->worker_ready || devc->worker_dispatching
#endif
		;
}

#ifdef _WIN32
static gboolean has_wch_stop_ownership(const struct dev_context *devc)
{
	return devc->upload_stop_pending
		;
}
#endif

static gboolean has_acquisition_resources(const struct dev_context *devc)
{
	return has_application_resources(devc)
#ifdef _WIN32
		|| (devc->use_wch && has_wch_stop_ownership(devc))
#endif
		;
}

static void wire_trace_close(struct dev_context *devc)
{
	FILE *trace = devc->wire_trace;

#if defined(_WIN32) && DLA_WCH_WORKER
	if (devc->worker_unconsumed_trace) {
		if (fclose(devc->worker_unconsumed_trace)) {
			devc->wire_trace_failed = TRUE;
			sr_err("WCH worker unconsumed trace close failed.");
		}
		devc->worker_unconsumed_trace = NULL;
		sr_info("WCH WORKER unconsumed trace closed: %" PRIu64
			" bytes, failed %d; these bytes were not decoded.",
			devc->worker_unconsumed_bytes, devc->wire_trace_failed);
	}
#endif

	if (!trace)
		return;
	devc->wire_trace = NULL;
	if (fclose(trace)) {
		devc->wire_trace_failed = TRUE;
		sr_err("WIRE trace close failed: %s.", g_strerror(errno));
	}
	sr_info("WIRE trace closed: %" PRIu64 " decoder-input bytes, tail %u, failed %d.",
		devc->wire_trace_bytes,
		(unsigned int)(devc->wire_trace_bytes % devc->decoder.wire_channels),
		devc->wire_trace_failed);
}

static int wire_trace_open(struct dev_context *devc)
{
	const char *path = g_getenv("FNIRSI_DLA32_WIRE_TRACE");
	int fd, saved_errno;

	if (!path || !*path)
		return SR_OK;
	if (devc->wire_trace)
		return SR_ERR_BUG;
	fd = g_open(path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
	if (fd < 0) {
		sr_err("Cannot create exclusive WIRE trace '%s': %s.", path,
			g_strerror(errno));
		return SR_ERR_IO;
	}
	devc->wire_trace = fdopen(fd, "wb");
	if (!devc->wire_trace) {
		saved_errno = errno;
		close(fd);
		sr_err("Cannot open WIRE trace stream: %s.", g_strerror(saved_errno));
		return SR_ERR_IO;
	}
	devc->wire_trace_bytes = 0;
	devc->wire_trace_failed = FALSE;
	devc->wire_trace_started_us = g_get_monotonic_time();
	/* Report disk failures at the corresponding decoder input, not at a
	 * later buffered flush. The unchanged payload is written once per read. */
	if (setvbuf(devc->wire_trace, NULL, _IONBF, 0)) {
		devc->wire_trace_failed = TRUE;
		sr_err("Cannot configure unbuffered WIRE trace output.");
		wire_trace_close(devc);
		return SR_ERR_IO;
	}
#if defined(_WIN32) && DLA_WCH_WORKER
	if (devc->use_wch) {
		char *unconsumed_path = g_strconcat(path, ".worker-unconsumed.bin", NULL);
		fd = g_open(unconsumed_path, O_WRONLY | O_CREAT | O_EXCL | O_BINARY, 0600);
		g_free(unconsumed_path);
		if (fd < 0) {
			sr_err("Cannot create exclusive WCH worker unconsumed trace.");
			wire_trace_close(devc);
			return SR_ERR_IO;
		}
		devc->worker_unconsumed_trace = fdopen(fd, "wb");
		if (!devc->worker_unconsumed_trace) {
			close(fd);
			wire_trace_close(devc);
			return SR_ERR_IO;
		}
		if (setvbuf(devc->worker_unconsumed_trace, NULL, _IONBF, 0)) {
			devc->wire_trace_failed = TRUE;
			wire_trace_close(devc);
			return SR_ERR_IO;
		}
	}
#endif
	sr_info("WIRE trace enabled: '%s', frame %u bytes, mask %08x, %s, %" PRIu64
		" Hz. Payload is unchanged; disk I/O changes timing.", path,
		devc->decoder.wire_channels, devc->channel_mask,
		devc->stream ? "Stream" : "Buffer", devc->samplerate);
	return SR_OK;
}

static void wire_trace_prefix(char *out, const uint8_t *bytes, size_t length)
{
	size_t i, n = MIN(length, (size_t)32);

	for (i = 0; i < n; i++)
		g_snprintf(out + 2 * i, 3, "%02x", bytes[i]);
	out[2 * n] = '\0';
}

static int wire_trace_write(struct dev_context *devc,
	const uint8_t *bytes, size_t length)
{
	char prefix[65];
	size_t written;

	if (!devc->wire_trace)
		return SR_OK;
	wire_trace_prefix(prefix, bytes, length);
	sr_dbg("WIRE payload: offset %" PRIu64 ", count %zu, offset_mod %u, count_mod %u,"
		" pending %zu, elapsed_us %" PRId64 ", prefix32 %s.",
		devc->wire_trace_bytes, length,
		(unsigned int)(devc->wire_trace_bytes % devc->decoder.wire_channels),
		(unsigned int)(length % devc->decoder.wire_channels), devc->pending,
		g_get_monotonic_time() - devc->wire_trace_started_us, prefix);
	written = fwrite(bytes, 1, length, devc->wire_trace);
	devc->wire_trace_bytes += written;
	if (written != length || ferror(devc->wire_trace)) {
		devc->wire_trace_failed = TRUE;
		sr_err("WIRE trace write failed after %" PRIu64 " bytes: %s.",
			devc->wire_trace_bytes, g_strerror(errno));
		return SR_ERR_IO;
	}
	return SR_OK;
}

static const uint32_t scanopts[] = { SR_CONF_CONN };
static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_SIGNAL_GENERATOR,
};
static const uint32_t logic_drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};
static const uint32_t devopts[] = {
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DEVICE_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CONTINUOUS | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_CAPTURE_STATUS | SR_CONF_GET,
	SR_CONF_CAPTURE_ERROR | SR_CONF_GET,
};
static const uint32_t logic_devopts[] = {
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
#if DLA_WCH_WORKER
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
#endif
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
#if DLA_WCH_WORKER
	SR_CONF_CONTINUOUS | SR_CONF_GET | SR_CONF_SET,
#endif
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_CAPTURE_STATUS | SR_CONF_GET,
	SR_CONF_CAPTURE_ERROR | SR_CONF_GET,
};
static const uint32_t pwmopts[] = {
	SR_CONF_ENABLED | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_OUTPUT_FREQUENCY | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DUTY_CYCLE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};
static const uint64_t samplerates[] = {
	SR_MHZ(1), SR_MHZ(2), SR_MHZ(4), SR_MHZ(5), SR_MHZ(10),
	SR_MHZ(20), SR_MHZ(25), SR_MHZ(40), SR_MHZ(50), SR_MHZ(100),
	SR_MHZ(125), SR_MHZ(200), SR_MHZ(250), SR_MHZ(500), SR_GHZ(1),
};
static const double thresholds[][2] = { {0.6, 0.6}, {1.6, 1.6}, {2.5, 2.5} };
static const char *device_modes[] = { "Buffer", "Stream" };
static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO, SR_TRIGGER_ONE, SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING, SR_TRIGGER_EDGE,
};

static gboolean logic_only_profile(void)
{
	return TRUE;
}

static uint32_t enabled_channel_mask(const struct sr_dev_inst *sdi)
{
	const struct sr_channel *channel;
	GSList *l;
	uint32_t mask = 0;

	for (l = sdi->channels; l; l = l->next) {
		channel = l->data;
		if (channel->type == SR_CHANNEL_LOGIC && channel->enabled &&
				channel->index < DLA_CHANNELS)
			mask |= 1U << channel->index;
	}
	return mask;
}

static uint64_t maximum_samplerate(const struct dev_context *devc,
	uint32_t mask)
{
	unsigned int channels = __builtin_popcount(mask);

	if (!channels)
		return 0;
	if (!devc->stream)
		return channels <= 8 ? SR_GHZ(1) : channels <= 16 ? SR_MHZ(500) : SR_MHZ(250);
	if (!devc->usb3)
		return 0;
	if (channels <= 2)
		return SR_GHZ(1);
	if (channels <= 4)
		return SR_MHZ(500);
	if (channels <= 8)
		return SR_MHZ(250);
	return channels <= 16 ? SR_MHZ(125) : SR_MHZ(50);
}

static int samplerate_index(uint64_t samplerate)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(samplerates); i++)
		if (samplerates[i] == samplerate)
			return i;
	return -1;
}

static uint64_t maximum_samples(const struct dev_context *devc,
	uint32_t mask)
{
	struct dla_decoder decoder;

	if (devc->stream)
		return UINT64_MAX;
	if (dla_decoder_init(&decoder, mask, FALSE) < 0)
		return 0;
	return BUFFER_MEMORY_BITS / decoder.wire_channels;
}

static int pwm_group_index(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg)
{
	int index;

	if (!cg)
		return -1;
	index = g_slist_index(sdi->channel_groups, cg);
	return index >= 0 && index < 4 ? index : -1;
}

#ifdef _WIN32
static GSList *scan_wch(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc = di->context;
	libusb_device **list;
	struct libusb_device_descriptor des;
	struct dla_wch wch = {0};
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_config *cfg;
	GSList *devices = NULL, *l;
	const char *conn = NULL;
	unsigned int index, ch, matches = 0, bus = 0, address = 0;
	int speed = LIBUSB_SPEED_UNKNOWN;
	ssize_t count, i;
	char name[16], connection[32];

	for (l = options; l; l = l->next) {
		cfg = l->data;
		if (cfg->key == SR_CONF_CONN)
			conn = g_variant_get_string(cfg->data, NULL);
	}
	count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &list);
	if (count >= 0) {
		for (i = 0; i < count; i++) {
			if (libusb_get_device_descriptor(list[i], &des) ||
				des.idVendor != 0x1a86 || des.idProduct != 0x5537)
				continue;
			matches++;
			speed = libusb_get_device_speed(list[i]);
			bus = libusb_get_bus_number(list[i]);
			address = libusb_get_device_address(list[i]);
		}
		libusb_free_device_list(list, 1);
	}
	if (!dla_wch_load())
		return NULL;
	for (index = 0; index < 16; index++) {
		g_snprintf(connection, sizeof(connection), "wch:%u", index);
		if (conn && strcmp(conn, connection))
			continue;
		wch.index = index;
		if (dla_wch_open(&wch) != SR_OK)
			continue;
		if (!dla_wch_model32(&wch)) {
			dla_wch_close(&wch);
			continue;
		}
		dla_wch_close(&wch);
		sdi = g_malloc0(sizeof(*sdi));
		sdi->vendor = g_strdup("FNIRSI");
		sdi->model = g_strdup("DLA-32 Plus (experimental)");
		sdi->connection_id = g_strdup(connection);
		sdi->status = SR_ST_INACTIVE;
		sdi->inst_type = SR_INST_USB;
		sdi->conn = sr_usb_dev_inst_new(bus, address, NULL);
		devc = sdi->priv = g_malloc0(sizeof(*devc));
		devc->use_wch = TRUE;
		devc->wch.index = index;
		devc->samplerate = SR_MHZ(50);
		devc->capture_ratio = 50;
		devc->threshold = 1;
		devc->logic_only = TRUE;
		/* With multiple matching WCH nodes, the index-to-libusb association
		 * needs a path match; never guess which node negotiated SuperSpeed. */
		devc->usb3 = matches == 1 && speed >= LIBUSB_SPEED_SUPER;
		sr_sw_limits_init(&devc->limits);
		devc->limits.limit_samples = DEFAULT_LIMIT_SAMPLES;
		for (ch = 0; ch < DLA_CHANNELS; ch++) {
			g_snprintf(name, sizeof(name), "D%u", ch);
			sr_channel_new(sdi, ch, SR_CHANNEL_LOGIC, TRUE, name);
		}
		for (ch = 0; ch < ARRAY_SIZE(devc->pwm); ch++) {
			g_snprintf(name, sizeof(name), "PWM%u", ch);
			sr_channel_group_new(sdi, name, NULL);
			devc->pwm[ch].frequency = 1000;
			devc->pwm[ch].duty = 50;
		}
		sr_info("Confirmed DL32 at %s; negotiated speed %s.", connection,
			devc->usb3 ? "SuperSpeed" : "USB 2 or unassociated");
		devices = g_slist_append(devices, sdi);
	}
	return devices;
}
#endif

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc = di->context;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct libusb_device_descriptor des;
	libusb_device **list;
	GSList *devices = NULL, *l;
	const char *conn = NULL;
	struct sr_channel_group *cg;
	struct sr_channel *channel;
	struct sr_config *cfg;
	char bus_addr[32], name[8];
	ssize_t count, i;
	unsigned int ch;

#ifdef _WIN32
	devices = scan_wch(di, options);
	if (devices)
		return std_scan_complete(di, devices);
#endif
	for (l = options; l; l = l->next) {
		cfg = l->data;
		if (cfg->key == SR_CONF_CONN)
			conn = g_variant_get_string(cfg->data, NULL);
	}
	/* The WCH VID/PID is shared; require an explicit bus/address. */
	if (!conn || !*conn)
		return NULL;
	count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &list);
	if (count < 0)
		return NULL;
	for (i = 0; i < count; i++) {
		if (libusb_get_device_descriptor(list[i], &des) ||
			des.idVendor != 0x1a86 || des.idProduct != 0x5537)
			continue;
		g_snprintf(bus_addr, sizeof(bus_addr), "%u.%u",
			libusb_get_bus_number(list[i]), libusb_get_device_address(list[i]));
		if (strcmp(conn, bus_addr))
			continue;
		sdi = g_malloc0(sizeof(*sdi));
		sdi->vendor = g_strdup("FNIRSI");
		sdi->model = g_strdup("DLA-32 (experimental)");
		sdi->connection_id = g_strdup(bus_addr);
		sdi->status = SR_ST_INACTIVE;
		sdi->inst_type = SR_INST_USB;
		sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(list[i]),
			libusb_get_device_address(list[i]), NULL);
		devc = sdi->priv = g_malloc0(sizeof(*devc));
		sr_sw_limits_init(&devc->limits);
		devc->limits.limit_samples = DEFAULT_LIMIT_SAMPLES;
		devc->samplerate = SR_MHZ(50);
		devc->capture_ratio = 50;
		devc->threshold = 1;
		devc->usb3 = libusb_get_device_speed(list[i]) >= LIBUSB_SPEED_SUPER;
		devc->logic_only = logic_only_profile();
		for (ch = 0; ch < ARRAY_SIZE(devc->pwm); ch++) {
			devc->pwm[ch].frequency = SR_KHZ(1);
			devc->pwm[ch].duty = 50;
		}
		for (ch = 0; ch < DLA_CHANNELS; ch++) {
			g_snprintf(name, sizeof(name), "D%u", ch);
			sr_channel_new(sdi, ch, SR_CHANNEL_LOGIC, TRUE, name);
		}
		if (!devc->logic_only) {
			for (ch = 0; ch < ARRAY_SIZE(devc->pwm); ch++) {
				g_snprintf(name, sizeof(name), "PWM%u", ch);
				cg = sr_channel_group_new(sdi, name, NULL);
				channel = sr_channel_new(sdi, DLA_CHANNELS + ch,
					SR_CHANNEL_ANALOG, FALSE, name);
				cg->channels = g_slist_append(cg->channels, channel);
			}
		}
		devices = g_slist_append(devices, sdi);
	}
	libusb_free_device_list(list, 1);
	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *open_devc = sdi->priv;
	unsigned int pwm_i;
	/* Another application or a reconnect may change persistent outputs. */
	for (pwm_i = 0; pwm_i < ARRAY_SIZE(open_devc->pwm); pwm_i++)
		open_devc->pwm[pwm_i].known = FALSE;
#ifdef _WIN32
	struct dev_context *devc = sdi->priv;
	if (devc->use_wch) {
#if DLA_WCH_WORKER
		if (has_acquisition_resources(devc))
			return SR_ERR;
#endif
		if (dla_wch_open(&devc->wch) != SR_OK)
			return SR_ERR_IO;
		if (!dla_wch_model32(&devc->wch)) {
			dla_wch_close(&devc->wch);
			return SR_ERR_NA;
		}
		return SR_OK;
	}
#endif
	struct drv_context *drvc = sdi->driver->context;
	struct sr_usb_dev_inst *usb = sdi->conn;
	int ret = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);

	if (ret != SR_OK)
		return ret;
	ret = libusb_claim_interface(usb->devhdl, 0);
	if (ret) {
		sr_err("Cannot claim interface: %s", libusb_error_name(ret));
		libusb_close(usb->devhdl);
		usb->devhdl = NULL;
		return SR_ERR;
	}
	return SR_OK;
}

static int send_command(const struct sr_dev_inst *sdi, const uint8_t *packet)
{
	struct dev_context *trace_devc = sdi->priv;
	char prefix[109];
	unsigned int trace_i;

#if defined(_WIN32) && DLA_WCH_WORKER
	if (trace_devc->use_wch && trace_devc->worker && trace_devc->worker->thread) {
		sr_err("Main-thread WCH command refused while worker owns I/O.");
		return SR_ERR;
	}
#endif

	if (trace_devc->wire_trace) {
		for (trace_i = 0; trace_i < 54; trace_i++)
			g_snprintf(prefix + 2 * trace_i, 3, "%02x", packet[trace_i]);
		sr_dbg("WIRE command: opcode %02x, elapsed_us %" PRId64 ", prefix54 %s.",
			packet[9], g_get_monotonic_time() - trace_devc->wire_trace_started_us,
			prefix);
	}
#ifdef _WIN32
	struct dev_context *devc = sdi->priv;
	if (devc->use_wch)
		return dla_wch_write(&devc->wch, packet, DLA_COMMAND_SIZE);
#endif
	struct sr_usb_dev_inst *usb = sdi->conn;
	int count = 0, ret;

	ret = libusb_bulk_transfer(usb->devhdl, 0x02, (uint8_t *)packet,
		DLA_COMMAND_SIZE, &count, 1000);
	if (ret || count != DLA_COMMAND_SIZE) {
		sr_err("Command 0x%02x failed: %s (%d bytes)", packet[9],
			libusb_error_name(ret), count);
		return SR_ERR_IO;
	}
	return SR_OK;
}

#ifdef _WIN32
static void drain_stopped_endpoint(const struct sr_dev_inst *sdi);

static int stop_wch_capture(const struct sr_dev_inst *sdi, const char *phase)
{
	struct dev_context *devc = sdi->priv;
	uint8_t packet[DLA_COMMAND_SIZE];
	int ret;

	/* An attempted disable or STOP may leave device capture state unknown.
	 * This ownership is separate from SDK queue and application resources. */
	devc->upload_stop_pending = TRUE;

	dla_stop(packet);
	ret = send_command(sdi, packet);
	if (ret != SR_OK) {
		sr_err("WCH capture STOP failed: phase %s; pending ownership retained.", phase);
		return ret;
	}
	devc->upload_stop_pending = FALSE;
	sr_info("WCH capture STOP complete: phase %s; command accepted.", phase);
	return SR_OK;
}
#endif

static int send_pwm_config(const struct sr_dev_inst *sdi, unsigned int index,
	const struct pwm_setting *setting)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	uint8_t packet[DLA_COMMAND_SIZE];

	if (!usb->devhdl
#ifdef _WIN32
		&& !((struct dev_context *)sdi->priv)->wch.opened
#endif
	)
		return SR_ERR_DEV_CLOSED;
	if (dla_pwm(packet, index, setting->frequency, setting->duty,
			setting->enabled) < 0)
		return SR_ERR_ARG;
	return send_command(sdi, packet);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	unsigned int pwm_i;

	if (has_application_resources(devc)) {
		sr_err("Cannot close while asynchronous transfers are active; stop acquisition first.");
		/* sr_dev_close() changes status before invoking this callback. */
		sdi->status = SR_ST_ACTIVE;
		return SR_ERR;
	}
	for (pwm_i = 0; pwm_i < ARRAY_SIZE(devc->pwm); pwm_i++)
		devc->pwm[pwm_i].known = FALSE;
#ifdef _WIN32
	if (devc->use_wch) {
#if DLA_WCH_WORKER
		if (devc->wch_disconnected) {
			/* The OS explicitly reported device loss. The worker has exited;
			 * release the stale host handle without issuing further USB I/O. */
			dla_wch_close(&devc->wch);
			devc->upload_stop_pending = FALSE;
			devc->wch_disconnected = FALSE;
			return SR_OK;
		}
#endif
		if (devc->upload_stop_pending) {
			int ret = stop_wch_capture(sdi, "close");
			if (ret != SR_OK) {
				sr_err("WCH close refused: pending capture STOP retirement failed.");
				sdi->status = SR_ST_ACTIVE;
				return ret;
			}
			drain_stopped_endpoint(sdi);
		}
		dla_wch_close(&devc->wch);
		return SR_OK;
	}
#endif
	if (usb->devhdl) {
		libusb_release_interface(usb->devhdl, 0);
		libusb_close(usb->devhdl);
		usb->devhdl = NULL;
	}
	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	int pwm_index;

	if (cg) {
		pwm_index = pwm_group_index(sdi, cg);
		if (pwm_index < 0)
			return SR_ERR_ARG;
		switch (key) {
		case SR_CONF_ENABLED:
			if (!devc->pwm[pwm_index].known)
				return SR_ERR_NA;
			*data = g_variant_new_boolean(devc->pwm[pwm_index].enabled);
			break;
		case SR_CONF_OUTPUT_FREQUENCY:
			*data = g_variant_new_double(devc->pwm[pwm_index].frequency);
			break;
		case SR_CONF_DUTY_CYCLE:
			*data = g_variant_new_double(devc->pwm[pwm_index].duty);
			break;
		default:
			return SR_ERR_NA;
		}
		return SR_OK;
	}
	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->samplerate);
		break;
	case SR_CONF_CAPTURE_STATUS:
		*data = g_variant_new_string(devc->capture_status ? devc->capture_status : "unknown");
		break;
	case SR_CONF_CAPTURE_ERROR:
		*data = g_variant_new_string(devc->capture_error ? devc->capture_error : "");
		break;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_CAPTURE_RATIO:
		if (devc->stream)
			return SR_ERR_NA;
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	case SR_CONF_CONN:
		*data = g_variant_new_string(sdi->connection_id);
		break;
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = g_variant_new("(dd)", thresholds[devc->threshold][0],
			thresholds[devc->threshold][1]);
		break;
	case SR_CONF_DEVICE_MODE:
	case SR_CONF_DATA_SOURCE:
		*data = g_variant_new_string(device_modes[devc->stream]);
		break;
	case SR_CONF_CONTINUOUS:
		*data = g_variant_new_boolean(devc->continuous);
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	struct pwm_setting candidate;
	uint64_t value;
	double low, high, number;
	const char *mode;
	int pwm_index, ret;
	unsigned int i;
	gboolean enabled;

#if defined(_WIN32) && DLA_WCH_WORKER
	if (devc->wch_disconnected)
		return SR_ERR_DEV_CLOSED;
#endif
	if (devc->acquiring
#if defined(_WIN32) && DLA_WCH_WORKER
			|| devc->worker || devc->worker_ready || devc->worker_dispatching
#endif
#ifdef _WIN32
			|| (devc->use_wch && has_wch_stop_ownership(devc))
#endif
			)
		return SR_ERR;
	if (cg) {
		pwm_index = pwm_group_index(sdi, cg);
		if (pwm_index < 0)
			return SR_ERR_ARG;
		candidate = devc->pwm[pwm_index];
		switch (key) {
		case SR_CONF_ENABLED:
			candidate.enabled = g_variant_get_boolean(data);
			break;
		case SR_CONF_OUTPUT_FREQUENCY:
			number = g_variant_get_double(data);
			if (!isfinite(number) || number < 1 || number > 20000000 ||
					number != (uint32_t)number)
				return SR_ERR_ARG;
			candidate.frequency = number;
			break;
		case SR_CONF_DUTY_CYCLE:
			number = g_variant_get_double(data);
			if (!isfinite(number) || number < 1 || number > 99 ||
					number != (unsigned int)number)
				return SR_ERR_ARG;
			candidate.duty = number;
			break;
		default:
			return SR_ERR_NA;
		}
		/* Editing defaults must not disable an output whose state we cannot
		 * read. Stage parameters until the user explicitly selects ON/OFF. */
		if (!candidate.known && key != SR_CONF_ENABLED) {
			devc->pwm[pwm_index] = candidate;
			return SR_OK;
		}
		ret = send_pwm_config(sdi, pwm_index, &candidate);
		if (ret == SR_OK) {
			candidate.known = TRUE;
			devc->pwm[pwm_index] = candidate;
		} else {
			/* A failed write does not establish the remaining hardware state. */
			devc->pwm[pwm_index].known = FALSE;
		}
		return ret;
	}
	switch (key) {
	case SR_CONF_SAMPLERATE:
		value = g_variant_get_uint64(data);
		if (samplerate_index(value) < 0)
			return SR_ERR_ARG;
		devc->samplerate = value;
		return SR_OK;
	case SR_CONF_LIMIT_SAMPLES:
		value = g_variant_get_uint64(data);
		if ((!value && !devc->continuous) ||
				(!devc->stream && value > BUFFER_MEMORY_BITS))
			return SR_ERR_ARG;
		devc->limits.limit_samples = value;
		if (value) {
			devc->continuous = FALSE;
			devc->limits.limit_msec = 0;
		}
		return SR_OK;
	case SR_CONF_LIMIT_MSEC:
		value = g_variant_get_uint64(data);
		if ((!value && !devc->continuous) || value > UINT64_MAX / 1000)
			return SR_ERR_ARG;
		devc->limits.limit_msec = value * 1000;
		if (value) {
			devc->continuous = FALSE;
			devc->limits.limit_samples = 0;
		}
		return SR_OK;
	case SR_CONF_CAPTURE_RATIO:
		value = g_variant_get_uint64(data);
		/* Factory UI supports 1..90%; zero does not capture the tested event. */
		if (value < 1 || value > 90)
			return SR_ERR_ARG;
		if (devc->stream)
			return SR_ERR_NA;
		devc->capture_ratio = value;
		return SR_OK;
	case SR_CONF_VOLTAGE_THRESHOLD:
		g_variant_get(data, "(dd)", &low, &high);
		for (i = 0; i < ARRAY_SIZE(thresholds); i++) {
			if (low == thresholds[i][0] && high == thresholds[i][1]) {
				devc->threshold = i;
				return SR_OK;
			}
		}
		return SR_ERR_ARG;
	case SR_CONF_DEVICE_MODE:
	case SR_CONF_DATA_SOURCE:
		mode = g_variant_get_string(data, NULL);
		if (!strcmp(mode, device_modes[0])) {
			if (devc->continuous)
				return SR_ERR_ARG;
			devc->stream = FALSE;
			return SR_OK;
		}
		if (!strcmp(mode, device_modes[1])) {
			if (!devc->usb3) {
				sr_err("Experimental Stream mode requires a USB 3 connection.");
				return SR_ERR_NA;
			}
			devc->stream = TRUE;
			return SR_OK;
		}
		return SR_ERR_ARG;
	case SR_CONF_CONTINUOUS:
		enabled = g_variant_get_boolean(data);
		if (enabled && !devc->usb3) {
			sr_err("Continuous Stream mode requires a USB 3 connection.");
			return SR_ERR_NA;
		}
		devc->continuous = enabled;
		if (enabled) {
			devc->stream = TRUE;
			devc->limits.limit_samples = 0;
			devc->limits.limit_msec = 0;
		} else if (!devc->limits.limit_samples && !devc->limits.limit_msec) {
			devc->limits.limit_samples = DEFAULT_LIMIT_SAMPLES;
		}
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	const struct dev_context *devc = sdi ? sdi->priv : NULL;
	uint64_t maximum, maximum_msec;
	uint32_t mask;
	unsigned int count;
	int pwm_index;
	static const double frequency_range[] = {1, 20000000, 1};
	static const double duty_range[] = {1, 99, 1};

	if (cg) {
		if (!sdi)
			return SR_ERR_ARG;
		pwm_index = pwm_group_index(sdi, cg);
		if (pwm_index < 0)
			return SR_ERR_ARG;
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(pwmopts));
			return SR_OK;
		case SR_CONF_OUTPUT_FREQUENCY:
			*data = std_gvar_min_max_step_array(frequency_range);
			return SR_OK;
		case SR_CONF_DUTY_CYCLE:
			*data = std_gvar_min_max_step_array(duty_range);
			return SR_OK;
		default:
			return SR_ERR_NA;
		}
	}
	switch (key) {
	case SR_CONF_SAMPLERATE:
		if (!sdi)
			return SR_ERR_ARG;
		mask = enabled_channel_mask(sdi);
		maximum = maximum_samplerate(devc, mask);
		for (count = 0; count < ARRAY_SIZE(samplerates) &&
				samplerates[count] <= maximum; count++)
			;
		if (!count)
			return SR_ERR_NA;
		*data = std_gvar_samplerates(samplerates, count);
		return SR_OK;
	case SR_CONF_LIMIT_SAMPLES:
		if (!sdi)
			return SR_ERR_ARG;
		maximum = maximum_samples(devc, enabled_channel_mask(sdi));
		if (devc->logic_only)
			maximum = MIN(maximum, LOGIC_ONLY_MAX_LIMIT_SAMPLES);
		*data = std_gvar_tuple_u64(1, maximum);
		return SR_OK;
	case SR_CONF_LIMIT_MSEC:
		if (!sdi || !devc->samplerate)
			return SR_ERR_ARG;
		maximum = maximum_samples(devc, enabled_channel_mask(sdi));
		if (devc->logic_only && maximum != UINT64_MAX)
			maximum = MIN(maximum, LOGIC_ONLY_MAX_LIMIT_SAMPLES);
		if (maximum == UINT64_MAX)
			maximum_msec = UINT64_MAX / 1000;
		else
			maximum_msec = maximum / devc->samplerate * 1000 +
				(maximum % devc->samplerate) * 1000 / devc->samplerate;
		*data = std_gvar_tuple_u64(1, maximum_msec);
		return SR_OK;
	case SR_CONF_CAPTURE_RATIO:
		if (!sdi || devc->stream)
			return SR_ERR_NA;
		*data = std_gvar_tuple_u64(1, 90);
		return SR_OK;
	case SR_CONF_VOLTAGE_THRESHOLD:
		*data = std_gvar_thresholds(ARRAY_AND_SIZE(thresholds));
		return SR_OK;
	case SR_CONF_DEVICE_MODE:
	case SR_CONF_DATA_SOURCE:
		*data = std_gvar_array_str(device_modes, devc && devc->usb3 ? 2 : 1);
		return SR_OK;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		return SR_OK;
	default:
		if ((sdi && devc->logic_only) || (!sdi && logic_only_profile()))
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts,
				logic_drvopts, logic_devopts);
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	}
}

static void request_stop(struct dev_context *devc)
{
	devc->stopping = TRUE;
#if defined(_WIN32) && DLA_WCH_WORKER
	if (devc->worker && devc->worker->thread) {
		devc->upload_stop_pending = TRUE;
		dla_worker_request_stop(devc->worker);
	}
#endif
}

static void free_prepared_transfers(struct dev_context *devc)
{
	unsigned int i;

#if defined(_WIN32) && DLA_WCH_WORKER
	gboolean was_dispatching = devc->worker_dispatching;
	if (devc->use_wch)
		devc->worker_dispatching = TRUE;
	if (devc->worker) {
		if (!dla_worker_free(devc->worker)) {
			sr_err("Cannot release a live WCH worker.");
			devc->worker_dispatching = was_dispatching;
			return;
		}
		devc->worker = NULL;
	}
	if (devc->worker_ready && !devc->source_added) {
		if (!CloseHandle(devc->worker_ready))
			sr_err("Cannot close WCH completion event; handle retained.");
		else
			devc->worker_ready = NULL;
	}
#endif

	wire_trace_close(devc);
	for (i = 0; i < ARRAY_SIZE(devc->transfers); i++) {
		if (!devc->transfers[i])
			continue;
		g_free(devc->transfers[i]->buffer);
		libusb_free_transfer(devc->transfers[i]);
		devc->transfers[i] = NULL;
		devc->transfer_submitted[i] = FALSE;
	}
	g_free(devc->decoded);
	devc->decoded = NULL;
	devc->decoded_size = 0;
#ifdef _WIN32
	g_free(devc->wch_buffer);
	devc->wch_buffer = NULL;
#if DLA_WCH_WORKER
	devc->worker_dispatching = was_dispatching;
#endif
#endif
}

static void clear_private(void *priv)
{
	if (priv)
		free_prepared_transfers(priv);
}

static int dev_clear(const struct sr_dev_driver *driver)
{
	struct drv_context *drvc = driver->context;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	GSList *l;
	int ret;

	/* Refuse the whole operation before closing any instance. A frontend
	 * session can still own these objects while its callbacks are pending. */
	for (l = drvc->instances; l; l = l->next) {
		sdi = l->data;
		if (!sdi)
			return SR_ERR_BUG;
		devc = sdi->priv;
		if (devc && has_acquisition_resources(devc)) {
			sr_err("Cannot clear DLA-32 instances before acquisition has stopped.");
			return SR_ERR;
		}
	}
	for (l = drvc->instances; l; l = l->next) {
		sdi = l->data;
		/* Close the actual handle even if an earlier caller changed status. */
		if (sdi->priv && (ret = dev_close(sdi)) != SR_OK)
			return ret;
		sdi->status = SR_ST_INACTIVE;
	}
	return std_dev_clear_with_callback(driver, clear_private);
}

static void finish_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	int ret;

#if defined(_WIN32) && DLA_WCH_WORKER
	/* Caller must reconcile and retire the native thread before END/free. */
	if (devc->worker) {
		sr_err("Cannot finish while WCH worker ownership remains.");
		return;
	}
#endif

	if (devc->source_added) {
#ifdef _WIN32
		if (devc->use_wch) {
#if DLA_WCH_WORKER
			if (devc->worker_ready)
				sr_session_source_remove_pollfd(sdi->session, &devc->worker_pollfd);
			else
#endif
				sr_session_source_remove(sdi->session, -1);
		} else
#endif
		usb_source_remove(sdi->session, devc->usb_ctx);
		devc->source_added = FALSE;
	}
	free_prepared_transfers(devc);
	devc->submitted_transfers = 0;
	devc->pending = 0;
	capture_finish_status(devc);
	devc->run_limit_samples = 0;
	devc->acquiring = FALSE;
	devc->starting = FALSE;
	devc->stopping = FALSE;
	devc->stop_sent = FALSE;
	devc->cancel_requested = FALSE;
	devc->data_started = FALSE;
	if (devc->header_sent) {
		devc->header_sent = FALSE;
		ret = capture_send_status(sdi);
		if (ret != SR_OK)
			sr_err("Failed to deliver capture status: %s.", sr_strerror(ret));
		ret = std_session_send_df_end(sdi);
		if (ret != SR_OK)
			sr_err("Failed to deliver acquisition END: %s.", sr_strerror(ret));
	}
}

static void retire_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi = transfer->user_data;
	struct dev_context *devc = sdi->priv;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(devc->transfers); i++) {
		if (devc->transfers[i] != transfer)
			continue;
		devc->transfers[i] = NULL;
		devc->transfer_submitted[i] = FALSE;
		break;
	}
	g_free(transfer->buffer);
	libusb_free_transfer(transfer);
	if (devc->submitted_transfers)
		devc->submitted_transfers--;
}

static void cancel_transfers(struct dev_context *devc)
{
	unsigned int i;
	int ret;

	for (i = 0; i < ARRAY_SIZE(devc->transfers); i++) {
		if (!devc->transfers[i] || !devc->transfer_submitted[i])
			continue;
		ret = libusb_cancel_transfer(devc->transfers[i]);
		if (ret != LIBUSB_SUCCESS && ret != LIBUSB_ERROR_NOT_FOUND)
			sr_warn("Failed to cancel USB transfer: %s.", libusb_error_name(ret));
		/* Even NOT_FOUND may mean completion is waiting to be dispatched.
		 * Only its callback can retire a submitted transfer safely. */
	}
}

static void note_data_progress(struct dev_context *devc)
{
	devc->data_started = TRUE;
	devc->last_data_us = g_get_monotonic_time();
}

static void check_data_progress(struct dev_context *devc)
{
	int64_t timeout;

	if (devc->stopping || (!devc->data_started && devc->has_trigger))
		return;
	timeout = devc->data_started ? DATA_STALL_USEC : devc->first_data_timeout_us;
	if (g_get_monotonic_time() - devc->last_data_us <= timeout)
		return;
	sr_err("Acquisition stalled after %" PRIu64 " samples (%s).",
		devc->limits.samples_read,
		devc->data_started ? "sample transfer" : "waiting for first data");
	capture_set_failure(devc, "timeout", "Capture incomplete: sample data did not arrive within the transfer timeout.");
	request_stop(devc);
}

static int resubmit_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi = transfer->user_data;
	struct dev_context *devc = sdi->priv;
	int ret;

	ret = libusb_submit_transfer(transfer);
	if (ret == LIBUSB_SUCCESS)
		return SR_OK;
	sr_err("Failed to resubmit USB transfer: %s.", libusb_error_name(ret));
	capture_set_failure(devc, ret == LIBUSB_ERROR_NO_DEVICE ? "disconnected" : "error",
		"Capture incomplete: the USB sample transfer could not be continued.");
	request_stop(devc);
	retire_transfer(transfer);
	return SR_ERR_IO;
}

static uint64_t decode_transfer(struct sr_dev_inst *sdi,
	const uint8_t *input, size_t length)
{
	struct dev_context *devc = sdi->priv;
	struct sr_datafeed_logic logic;
	struct sr_datafeed_packet packet;
	size_t produced;
	uint64_t samples, remaining;
	int ret;

	/* Save the same bytes this capture decodes, including an incomplete
	 * final frame and input beyond the software sample limit. */
	if (wire_trace_write(devc, input, length) != SR_OK) {
		capture_set_failure(devc, "error", "Capture incomplete: writing the raw USB trace failed.");
		request_stop(devc);
		return 0;
	}
	if (dla_decode_feed(&devc->decoder, devc->wire_tail, &devc->pending,
		devc->decoded, devc->decoded_size, input, length, &produced) < 0) {
		sr_err("Invalid USB frame state or insufficient decode buffer.");
		capture_set_failure(devc, "error", "Capture incomplete: the sample conversion failed.");
		request_stop(devc);
		return 0;
	}
	samples = produced / devc->decoder.unitsize;

	remaining = devc->run_limit_samples;
	if (remaining) {
		remaining -= MIN(remaining, devc->limits.samples_read);
		samples = MIN(samples, remaining);
	}
	if (!samples)
		return 0;
	logic.length = samples * devc->decoder.unitsize;
	logic.unitsize = devc->decoder.unitsize;
	logic.data = devc->decoded;
	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	ret = sr_session_send(sdi, &packet);
	if (ret != SR_OK) {
		sr_err("Failed to deliver acquisition samples: %s.", sr_strerror(ret));
		capture_set_failure(devc, "error", "Capture incomplete: the application did not accept the sample data.");
		request_stop(devc);
		return 0;
	}
	sr_sw_limits_update_samples_read(&devc->limits, samples);
	return samples;
}

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi = transfer->user_data;
	struct dev_context *devc = sdi->priv;
	gboolean valid_data;

	if (devc->wire_trace)
		sr_dbg("WIRE async completion: elapsed_us %" PRId64 ", status %d, count %d,"
			" payload_offset %" PRIu64 ", stopping %d.",
			g_get_monotonic_time() - devc->wire_trace_started_us,
			transfer->status, transfer->actual_length, devc->wire_trace_bytes,
			devc->stopping);
	valid_data = transfer->status == LIBUSB_TRANSFER_COMPLETED ||
		transfer->status == LIBUSB_TRANSFER_TIMED_OUT;
	if (devc->stopping) {
		retire_transfer(transfer);
		return;
	}
	if (!valid_data) {
		sr_err("USB stream discontinuity: transfer status %d (%d bytes).",
			transfer->status, transfer->actual_length);
		capture_set_failure(devc, transfer->status == LIBUSB_TRANSFER_NO_DEVICE ? "disconnected" : "error",
			"Capture incomplete: the USB sample transfer failed.");
		request_stop(devc);
		retire_transfer(transfer);
		return;
	}
	if (transfer->actual_length > 0) {
		note_data_progress(devc);
		decode_transfer(sdi, transfer->buffer, transfer->actual_length);
	}
	if (devc->stopping ||
			(devc->run_limit_samples &&
			devc->limits.samples_read >= devc->run_limit_samples)) {
		request_stop(devc);
		retire_transfer(transfer);
		return;
	}
	resubmit_transfer(transfer);
}

static int read_samples_sync(const struct sr_dev_inst *sdi, uint8_t *buffer,
	int length, int *count, unsigned int timeout)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct dev_context *devc = sdi->priv;
	int64_t started_us = devc->wire_trace ? g_get_monotonic_time() : 0;
	char prefix[65];
	int ret;

#ifdef _WIN32
	if (devc->use_wch) {
#if DLA_WCH_WORKER
		if (devc->worker && devc->worker->thread) {
			*count = 0;
			return LIBUSB_ERROR_BUSY;
		}
#endif
		ret = dla_wch_read(&devc->wch, 1, buffer, length, count, timeout);
	} else
#endif
		ret = libusb_bulk_transfer(usb->devhdl, 0x81, buffer, length, count, timeout);
	if (devc->wire_trace) {
		wire_trace_prefix(prefix, buffer,
			(ret == LIBUSB_SUCCESS || ret == LIBUSB_ERROR_TIMEOUT) &&
			*count > 0 && *count <= length ? (size_t)*count : 0);
		sr_dbg("WIRE read: phase %s, start_us %" PRId64 ", duration_us %" PRId64
			", status %d, requested %d, timeout_ms %u, count %d, count_mod32 %d,"
			" payload_offset %" PRIu64 ", prefix32 %s.",
			devc->stopping ? "post-stop-drain" :
			devc->acquiring ? "samples" : "pre-arm-drain",
			started_us - devc->wire_trace_started_us,
			g_get_monotonic_time() - started_us, ret, length, timeout, *count,
			*count % 32, devc->wire_trace_bytes, prefix);
	}
	return ret;
}

static void drain_stopped_endpoint(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint8_t *buffer;
	uint64_t total = 0;
	int64_t deadline;
	unsigned int empty = 0;
	int count, length, ret;

#ifdef _WIN32
	if (devc->use_wch && has_wch_stop_ownership(devc)) {
		sr_err("WCH post-stop drain skipped: capture STOP pending.");
		return;
	}
#endif
	buffer = g_try_malloc(TRANSFER_SIZE);
	if (!buffer) {
		sr_warn("Cannot allocate the post-stop drain buffer.");
		return;
	}
	deadline = g_get_monotonic_time() + STOP_DRAIN_USEC;
	while (empty < STOP_EMPTY_READS && total < STOP_DRAIN_BYTES &&
			g_get_monotonic_time() < deadline) {
		length = MIN((uint64_t)TRANSFER_SIZE, STOP_DRAIN_BYTES - total);
		count = 0;
		ret = read_samples_sync(sdi, buffer, length, &count, 20);
		if (ret != LIBUSB_SUCCESS && ret != LIBUSB_ERROR_TIMEOUT) {
			sr_warn("Post-stop drain failed: %s.", libusb_error_name(ret));
			break;
		}
		if (!count) {
			empty++;
			continue;
		}
		empty = 0;
		if (count > 0)
			total += count;
	}
	if (empty < STOP_EMPTY_READS)
		sr_warn("Post-stop endpoint did not become quiet after %" PRIu64
			" bytes within the drain bound.", total);
	if (devc->wire_trace)
		sr_dbg("WIRE post-stop drain summary: discarded %" PRIu64 ", mod32 %u,"
			" empty_reads %u.", total, (unsigned int)(total % 32), empty);
	g_free(buffer);
}

static void begin_ordered_stop(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint8_t packet[DLA_COMMAND_SIZE];

	if (!devc->stopping || devc->cancel_requested)
		return;
	devc->cancel_requested = TRUE;
#if defined(_WIN32) && DLA_WCH_WORKER
	if (devc->worker && devc->worker->thread) {
		devc->upload_stop_pending = TRUE;
		dla_worker_request_stop(devc->worker);
		return;
	}
#endif
	cancel_transfers(devc);
#ifdef _WIN32
	if (devc->use_wch) {
		devc->stop_sent = stop_wch_capture(sdi, "stop") == SR_OK;
		return;
	}
#endif
	dla_stop(packet);
	devc->stop_sent = TRUE;
	if (send_command(sdi, packet) != SR_OK)
		sr_err("Failed to send the acquisition stop command.");
}

static void rollback_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct timeval tv;
	gboolean event_error_logged = FALSE;
	int ret;
	if (!g_strcmp0(devc->capture_status, "running"))
		capture_set_failure(devc, "error", "Capture incomplete: starting the acquisition failed.");

#if defined(_WIN32) && DLA_WCH_WORKER
	gboolean was_dispatching = devc->worker_dispatching;
	if (devc->use_wch)
		devc->worker_dispatching = TRUE;
	/* All failure returns in start precede a successful handoff. A live worker
	 * can only be retired by its normal asynchronous main dispatch. */
	if (devc->worker) {
		if (devc->worker->thread) {
			request_stop(devc);
			sr_err("Startup rollback deferred to the active WCH worker.");
			devc->worker_dispatching = was_dispatching;
			return;
		}
		dla_worker_free(devc->worker);
		devc->worker = NULL;
	}
#endif

	request_stop(devc);
	begin_ordered_stop(sdi);
	/* sr_session_start() tears down its main context on failure. Cancel and
	 * dispatch all submitted transfers here, before returning that failure.
	 * Do not iterate the frontend GLib context or free transfers still owned
	 * by libusb. Cancellation is asynchronous, including NOT_FOUND races. */
	while (devc->submitted_transfers) {
		tv.tv_sec = 0;
		tv.tv_usec = USB_POLL_MS * 1000;
		ret = libusb_handle_events_timeout(devc->usb_ctx->libusb_ctx, &tv);
		if (ret == LIBUSB_SUCCESS || ret == LIBUSB_ERROR_INTERRUPTED)
			continue;
		if (!event_error_logged) {
			sr_err("USB rollback is waiting for pending callbacks: %s.",
				libusb_error_name(ret));
			event_error_logged = TRUE;
		}
		g_usleep(USB_POLL_MS * 1000);
	}
	drain_stopped_endpoint(sdi);
	finish_acquisition(sdi);
#if defined(_WIN32) && DLA_WCH_WORKER
	devc->worker_dispatching = was_dispatching;
#endif
}

#if defined(_WIN32) && DLA_WCH_WORKER
static void worker_emit_logs(struct dla_wch_worker *worker)
{
	struct dla_worker_log entry;

	while (dla_worker_take_log(worker, &entry))
		sr_log(entry.level, LOG_PREFIX ": %s [worker_io_us=%" PRId64 "]",
			entry.message, entry.original_us);
}

static void worker_preserve_unconsumed(struct dev_context *devc,
		const struct dla_worker_result *result)
{
	size_t written;
	char prefix[65];

	wire_trace_prefix(prefix, result->buffer, result->count);
	sr_dbg("WCH WORKER unconsumed read: generation %" PRIu64 ", read_id %" PRIu64
		", offset %" PRIu64 ", count %d, prefix32 %s; stop requested, no LOGIC.",
		devc->worker_generation, result->read_id,
		devc->worker_unconsumed_bytes, result->count, prefix);
	if (devc->worker_unconsumed_trace) {
		written = fwrite(result->buffer, 1, result->count, devc->worker_unconsumed_trace);
		if (written != (size_t)result->count || ferror(devc->worker_unconsumed_trace)) {
			devc->wire_trace_failed = TRUE;
			sr_err("WCH worker unconsumed trace write failed.");
		}
	}
	devc->worker_unconsumed_bytes += result->count;
}

static int receive_worker_events(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct dla_wch_worker *worker = devc->worker;
	struct dla_worker_result result;
	struct dla_worker_terminal terminal;
	char prefix[65];
	int keep = G_SOURCE_CONTINUE;

	if (devc->worker_dispatching || devc->starting)
		return G_SOURCE_CONTINUE;
	devc->worker_dispatching = TRUE;
	dla_worker_acknowledge(worker);
	worker_emit_logs(worker);
	if (dla_worker_take_result(worker, &result)) {
		wire_trace_prefix(prefix, result.buffer,
			(result.status == LIBUSB_SUCCESS || result.status == LIBUSB_ERROR_TIMEOUT) &&
			result.count > 0 && result.count <= TRANSFER_SIZE ? (size_t)result.count : 0);
		sr_dbg("WCH WORKER read: generation %" PRIu64 ", read_id %" PRIu64
			", io_start_us %" PRId64 ", duration_us %" PRId64
			", status %d, requested %u, timeout_ms %u, count %d,"
			" Windows error %lu, payload_offset %" PRIu64 ", prefix32 %s.",
			worker->generation, result.read_id, result.start_us,
			result.end_us - result.start_us, result.status,
			(unsigned int)TRANSFER_SIZE, TRANSFER_TIMEOUT_MS, result.count,
			result.windows_error, devc->wire_trace_bytes, prefix);
		if (result.status != LIBUSB_SUCCESS && result.status != LIBUSB_ERROR_TIMEOUT) {
			sr_err("WCH sample stream failed: %s.", libusb_error_name(result.status));
			capture_set_failure(devc, result.status == LIBUSB_ERROR_NO_DEVICE ? "disconnected" : "error",
				"Capture incomplete: the USB sample transfer failed.");
			request_stop(devc);
		} else if (result.count > 0) {
			if (devc->stopping) {
				worker_preserve_unconsumed(devc, &result);
			} else {
				/* Publish receipt time rather than delayed main dispatch time. */
				devc->data_started = TRUE;
				devc->last_data_us = result.end_us;
				decode_transfer(sdi, result.buffer, result.count);
			}
		}
		if (devc->run_limit_samples && devc->limits.samples_read >= devc->run_limit_samples)
			request_stop(devc);
		check_data_progress(devc);
		dla_worker_release_result(worker, !devc->stopping);
	} else {
		check_data_progress(devc);
	}
	begin_ordered_stop(sdi);
	if (dla_worker_exited(worker, &terminal)) {
		/* No SDK, worker log or buffer access can occur after native exit. */
		worker_emit_logs(worker);
		if (devc->run_was_stream && devc->run_settle_enabled &&
				terminal.stop_status == SR_OK && !terminal.stop_pending &&
				terminal.drain_quiet && !terminal.device_disconnected &&
				!terminal.diagnostics_failed &&
				(terminal.drain_status == LIBUSB_SUCCESS ||
				 terminal.drain_status == LIBUSB_ERROR_TIMEOUT)) {
			if (!devc->stream_retired_us) {
				devc->stream_retired_us = g_get_monotonic_time();
				sr_info("STREAM SETTLE retirement: hold 2000000 us; worker exited, SDK idle, frontend dispatch active.");
			}
			if (dla_stream_settle_remaining(1,0,devc->stream_retired_us,
					g_get_monotonic_time())) {
				/* Keep only the exited worker object and frontend source until
				 * the bounded idle interval has passed. No SDK work is delayed.
				 * Stop stays idempotent; startup cannot overlap owned resources.
				 * The existing 100ms source watchdog continues dispatching. */
				devc->worker_dispatching = FALSE;
				return G_SOURCE_CONTINUE;
			}
			sr_info("STREAM SETTLE retirement complete: elapsed_us %" PRId64 "; hardware ACK not claimed.",
				g_get_monotonic_time()-devc->stream_retired_us);
		}
		sr_info("WCH WORKER terminal: generation %" PRIu64 ", reads %" PRIu64
			", stop_status %d, pending %d, stop_start_us %" PRId64
			", stop_duration_us %" PRId64 ", drain_bytes %" PRIu64
			", drain_reads %u, empty_reads %u, quiet %d, diagnostics_failed %d,"
			" unconsumed_bytes %" PRIu64 "; native thread exited.",
			worker->generation, terminal.read_count, terminal.stop_status,
			terminal.stop_pending, terminal.stop_start_us,
			terminal.stop_end_us - terminal.stop_start_us, terminal.drain_bytes,
			terminal.drain_reads, terminal.drain_empty, terminal.drain_quiet,
			terminal.diagnostics_failed, devc->worker_unconsumed_bytes);
		if (terminal.diagnostics_failed) {
			sr_err("WCH worker diagnostic queue overflow/truncation; acquisition evidence incomplete.");
			capture_set_failure(devc, "error", "Capture diagnostics were lost. The received samples have not been certified as intact.");
		}
		/* A frontend logger may request stop recursively. The completed native
		 * I/O result is authoritative after all such callbacks have returned. */
		devc->upload_stop_pending = terminal.stop_pending;
		devc->wch_disconnected = terminal.device_disconnected;
		devc->stop_sent = terminal.stop_status == SR_OK;
		if (!dla_worker_free(worker)) {
			sr_err("WCH worker native exit gate changed; ownership retained.");
		} else {
			devc->worker = NULL;
			finish_acquisition(sdi);
			keep = G_SOURCE_REMOVE;
		}
	}
	/* Ownership covers END and every reentrant datafeed/logger callback. */
	devc->worker_dispatching = FALSE;
	return keep;
}
#endif

static int receive_events(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;
	struct timeval tv = {0, 0};
	int ret;

	(void)fd;
	(void)revents;
	/* A HEADER consumer may dispatch the frontend main context recursively.
	 * The queue and ARM must be complete before its source can run. */
	if (devc->starting)
		return G_SOURCE_CONTINUE;
#if defined(_WIN32) && DLA_WCH_WORKER
	if (devc->use_wch && devc->worker)
		return receive_worker_events(sdi);
#endif
	begin_ordered_stop(sdi);
#ifdef _WIN32
	if (devc->use_wch) {
		int count;
		int64_t deadline = g_get_monotonic_time() + 25000;
		while (!devc->stopping && g_get_monotonic_time() < deadline) {
			count = 0;
			ret = read_samples_sync(sdi, devc->wch_buffer, TRANSFER_SIZE, &count,
				TRANSFER_TIMEOUT_MS);
			if (ret != LIBUSB_SUCCESS && ret != LIBUSB_ERROR_TIMEOUT) {
				sr_err("WCH sample stream failed: %s.", libusb_error_name(ret));
				capture_set_failure(devc, ret == LIBUSB_ERROR_NO_DEVICE ? "disconnected" : "error",
					"Capture incomplete: the USB sample transfer failed.");
				request_stop(devc);
				break;
			}
			if (count) {
				note_data_progress(devc);
				decode_transfer(sdi, devc->wch_buffer, count);
			}
			if (devc->run_limit_samples &&
				devc->limits.samples_read >= devc->run_limit_samples)
				request_stop(devc);
			if (!count)
				break;
		}
		check_data_progress(devc);
		begin_ordered_stop(sdi);
		if (devc->stopping) {
			drain_stopped_endpoint(sdi);
			finish_acquisition(sdi);
			return G_SOURCE_REMOVE;
		}
		return G_SOURCE_CONTINUE;
	}
#endif
	ret = libusb_handle_events_timeout(devc->usb_ctx->libusb_ctx, &tv);
	if (ret != LIBUSB_SUCCESS && ret != LIBUSB_ERROR_INTERRUPTED && !devc->stopping) {
		sr_err("Failed to service USB events: %s.", libusb_error_name(ret));
		capture_set_failure(devc, ret == LIBUSB_ERROR_NO_DEVICE ? "disconnected" : "error",
			"Capture incomplete: USB event processing failed.");
		request_stop(devc);
	}
	check_data_progress(devc);
	begin_ordered_stop(sdi);
	if (devc->stopping && devc->stop_sent && !devc->submitted_transfers) {
		drain_stopped_endpoint(sdi);
		finish_acquisition(sdi);
		return G_SOURCE_REMOVE;
	}
	return G_SOURCE_CONTINUE;
}

static int prepare_trigger(const struct sr_dev_inst *sdi,
	uint8_t conditions[DLA_CHANNELS])
{
	const struct sr_trigger *trigger = sr_session_trigger_get(sdi->session);
	const struct sr_trigger_stage *stage;
	const struct sr_trigger_match *match;
	unsigned int condition;

	memset(conditions, 0, DLA_CHANNELS);
	if (!trigger)
		return SR_OK;
	if (g_slist_length(trigger->stages) != 1) {
		sr_err("DLA-32 supports one basic trigger stage, not advanced multi-stage triggers.");
		return SR_ERR_NA;
	}
	stage = trigger->stages->data;
	if (g_slist_length(stage->matches) != 1) {
		sr_err("DLA-32 supports exactly one channel condition per trigger.");
		return SR_ERR_NA;
	}
	match = stage->matches->data;
	if (!match->channel || match->channel->type != SR_CHANNEL_LOGIC ||
			match->channel->index >= DLA_CHANNELS || !match->channel->enabled) {
		sr_err("DLA-32 trigger must use one enabled logic channel.");
		return SR_ERR_ARG;
	}
	switch (match->match) {
	case SR_TRIGGER_RISING:
		condition = 1;
		break;
	case SR_TRIGGER_ONE:
		condition = 2;
		break;
	case SR_TRIGGER_FALLING:
		condition = 3;
		break;
	case SR_TRIGGER_ZERO:
		condition = 4;
		break;
	case SR_TRIGGER_EDGE:
		condition = 5;
		break;
	default:
		return SR_ERR_NA;
	}
	conditions[match->channel->index] = condition;
	return SR_OK;
}

static uint64_t configured_sample_count(const struct dev_context *devc)
{
	uint64_t by_time, milliseconds;

	if (devc->continuous)
		return 0;
	by_time = 0;
	milliseconds = devc->limits.limit_msec / 1000;
	if (milliseconds) {
		if (milliseconds > UINT64_MAX / devc->samplerate)
			by_time = UINT64_MAX;
		else
			by_time = devc->samplerate * milliseconds / 1000;
	}
	if (!devc->limits.limit_samples)
		return by_time;
	if (!by_time)
		return devc->limits.limit_samples;
	return MIN(devc->limits.limit_samples, by_time);
}

static const uint64_t durations[] = {
	1, 2, 5, 10, 20, 50, 100, 200, 500,
	1000, 2000, 5000, 10000, 20000, 50000,
};

static unsigned int duration_index(uint64_t milliseconds)
{
	unsigned int i;

	for (i = 0; i + 1 < ARRAY_SIZE(durations); i++)
		if (milliseconds <= durations[i])
			break;
	return i;
}

static uint64_t samples_to_milliseconds(uint64_t samples,
	uint64_t samplerate)
{
	uint64_t milliseconds, partial, remainder;

	milliseconds = samples / samplerate;
	if (milliseconds > UINT64_MAX / 1000)
		return UINT64_MAX;
	milliseconds *= 1000;
	remainder = samples % samplerate;
	partial = remainder * 1000 / samplerate;
	if (remainder * 1000 % samplerate)
		partial++;
	if (milliseconds > UINT64_MAX - partial)
		return UINT64_MAX;
	return milliseconds + partial;
}

static void configure_setup(struct dla_setup_config *setup,
	const struct dev_context *devc, uint64_t samples, gboolean has_trigger, uint32_t mask)
{
	static const uint16_t threshold_cv[] = {60, 160, 250};
	static const uint8_t threshold_range[] = {7, 2, 1};
	uint64_t milliseconds;

	memset(setup, 0, sizeof(*setup));
	setup->samplerate = devc->samplerate;
	setup->samplerate_index = samplerate_index(devc->samplerate);
	setup->threshold = threshold_cv[devc->threshold];
	setup->threshold_range = threshold_range[devc->threshold];
	setup->interval_ms = 3;
	setup->buffer = !devc->stream;
	setup->mode = devc->continuous ? 3 : 0;
	if (devc->continuous) {
		setup->samples = devc->samplerate;
		milliseconds = CONTINUOUS_WINDOW_MSEC;
	} else if (devc->limits.limit_msec) {
		setup->samples = samples;
		milliseconds = devc->limits.limit_msec / 1000;
	} else {
		setup->samples = samples;
		milliseconds = samples_to_milliseconds(samples, devc->samplerate);
	}
	setup->duration_ms = MIN(milliseconds, UINT16_MAX);
	setup->duration_index = duration_index(milliseconds);
	/* The duration selector describes a supported preset. In finite Buffer
	 * captures without a trigger, keep its millisecond field consistent.
	 * Requested device/frontend sample count remains unchanged. Do not round
	 * time-limited, triggered, continuous, oversized or memory-edge captures.
	 * Those separate scopes need their own hardware qualification. */
	if (setup->buffer && !has_trigger && !devc->limits.limit_msec &&
			!devc->continuous && milliseconds <= durations[ARRAY_SIZE(durations)-1] &&
			durations[setup->duration_index] * devc->samplerate <=
				maximum_samples(devc, mask) * 1000) {
		setup->duration_ms = durations[setup->duration_index];
	}
	/* Stream's position is fixed in the official UI; only Buffer is editable.
	 * This is the hardware setting, not a verified sample-index marker. */
	setup->trigger_position = has_trigger ?
		(devc->stream ? 500 : devc->capture_ratio * 10) : 0;
}

static void wait_before_arm(gboolean legacy_path)
{
	g_usleep(legacy_path ? 10000 : 100000);
}

static int prepare_transfers(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct libusb_transfer *transfer;
	uint8_t *buffer;
	unsigned int i;
	size_t frames;

	frames = TRANSFER_SIZE / devc->decoder.wire_channels + 1;
	devc->decoded_size = frames * 8 * devc->decoder.unitsize;
	devc->decoded = g_try_malloc(devc->decoded_size);
	if (!devc->decoded)
		return SR_ERR_MALLOC;
#ifdef _WIN32
	if (devc->use_wch) {
#if DLA_WCH_WORKER
		devc->worker_unconsumed_bytes = 0;
		if (devc->worker_generation == UINT64_MAX) {
			free_prepared_transfers(devc);
			return SR_ERR;
		}
		devc->worker_ready = CreateEventW(NULL, TRUE, FALSE, NULL);
		if (!devc->worker_ready) {
			free_prepared_transfers(devc);
			return SR_ERR_IO;
		}
		devc->worker_pollfd.fd = (gintptr)devc->worker_ready;
		devc->worker_pollfd.events = G_IO_IN;
		devc->worker_pollfd.revents = 0;
		devc->worker = dla_worker_prepare(&devc->wch, ++devc->worker_generation,
			devc->worker_ready);
		if (!devc->worker) {
			free_prepared_transfers(devc);
			return SR_ERR_MALLOC;
		}
#endif
		devc->wch_buffer = g_try_malloc(TRANSFER_SIZE);
		if (!devc->wch_buffer) {
			free_prepared_transfers(devc);
			return SR_ERR_MALLOC;
		}
		return SR_OK;
	}
#endif
	for (i = 0; i < ARRAY_SIZE(devc->transfers); i++) {
		buffer = g_try_malloc(TRANSFER_SIZE);
		transfer = libusb_alloc_transfer(0);
		if (!buffer || !transfer) {
			g_free(buffer);
			if (transfer)
				libusb_free_transfer(transfer);
			free_prepared_transfers(devc);
			return SR_ERR_MALLOC;
		}
		libusb_fill_bulk_transfer(transfer, usb->devhdl, 0x81, buffer,
			TRANSFER_SIZE, receive_transfer, (void *)sdi, TRANSFER_TIMEOUT_MS);
		devc->transfers[i] = transfer;
	}
	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct dla_setup_config setup;
	uint8_t conditions[DLA_CHANNELS], packet[DLA_COMMAND_SIZE];
	uint64_t samples, maximum, first_data_msec, hardware_buffer_msec = 0;
	uint64_t drain_total = 0;
	unsigned int drain_empty = 0;
	uint32_t mask;
	int count, ret, i;
	int64_t drain_deadline;
	gboolean has_trigger, legacy_path;

#if defined(_WIN32) && DLA_WCH_WORKER
	if (devc->wch_disconnected)
		return SR_ERR_DEV_CLOSED;
#endif
	if (has_acquisition_resources(devc))
		return SR_ERR;
	mask = enabled_channel_mask(sdi);
	if (!mask) {
		sr_err("Enable at least one DLA-32 logic channel.");
		return SR_ERR_ARG;
	}
	if (devc->stream && !devc->usb3) {
		sr_err("Experimental Stream mode requires a USB 3 connection.");
		return SR_ERR_NA;
	}
	if (devc->samplerate > maximum_samplerate(devc, mask)) {
		sr_err("%" PRIu64 " Hz exceeds the selected mode/channel limit.",
			devc->samplerate);
		return SR_ERR_ARG;
	}
	samples = configured_sample_count(devc);
	maximum = maximum_samples(devc, mask);
	if ((!samples && !devc->continuous) || samples > maximum) {
		sr_err("Sample limit exceeds the 4-Gbit Buffer memory for enabled channels.");
		return SR_ERR_ARG;
	}
	has_trigger = sr_session_trigger_get(sdi->session) != NULL;
	ret = prepare_trigger(sdi, conditions);
	if (ret != SR_OK)
		return ret;
	if (dla_decoder_init(&devc->decoder, mask, devc->stream) < 0)
		return SR_ERR_ARG;
	devc->channel_mask = mask;
	devc->run_limit_samples = devc->continuous ? 0 : samples;
	legacy_path = !devc->stream && mask == UINT32_MAX &&
		devc->samplerate == SR_MHZ(50) && !has_trigger &&
		!devc->limits.limit_msec && samples <= 500000;
	devc->stream_retired_us = 0;
	devc->run_was_stream = devc->stream;
	devc->run_settle_enabled = TRUE;
	ret = prepare_transfers((struct sr_dev_inst *)sdi);
	if (ret != SR_OK)
		return ret;
	ret = wire_trace_open(devc);
	if (ret != SR_OK) {
		free_prepared_transfers(devc);
		return ret;
	}

	sr_info("PREARM drain policy: %u consecutive empty reads.",
		(unsigned int)DLA_PREARM_EMPTY_READS);

	/* Retire SDK read ownership before writing capture STOP; the observed
	 * active-upload STOP failure is not assumed to be a universal SDK rule. */
#ifdef _WIN32
	if (devc->use_wch) {
		if (stop_wch_capture(sdi, "pre-arm") != SR_OK) {
			/* Startup still fails if its one explicit cleanup retry succeeds. */
			if (stop_wch_capture(sdi, "failed-start") != SR_OK) {
				sr_err("WCH failed-start retains pending STOP ownership.");
			}
			else
				drain_stopped_endpoint(sdi);
			free_prepared_transfers(devc);
			return SR_ERR_IO;
		}
	} else
#endif
	{
		dla_stop(packet);
		if (send_command(sdi, packet) != SR_OK) {
			free_prepared_transfers(devc);
			return SR_ERR_IO;
		}
	}
	drain_deadline = g_get_monotonic_time() + STOP_DRAIN_USEC;
	for (i = 0; i < 64; i++) {
		count = 0;
		ret = read_samples_sync(sdi,
#ifdef _WIN32
			devc->use_wch ? devc->wch_buffer :
#endif
			devc->transfers[0]->buffer, TRANSFER_SIZE, &count, 20);
		sr_dbg("Pre-arm drain %d: status %d, %d bytes.", i, ret, count);
		if (count > 0)
			drain_total += count;
		if ((ret == LIBUSB_SUCCESS || ret == LIBUSB_ERROR_TIMEOUT) && !count) {
			drain_empty++;
			if (drain_empty >= DLA_PREARM_EMPTY_READS)
				break;
		} else {
			drain_empty = 0;
		}
		if (ret && ret != LIBUSB_ERROR_TIMEOUT) {
			free_prepared_transfers(devc);
			return SR_ERR_IO;
		}
		if (g_get_monotonic_time() >= drain_deadline) {
			sr_err("Timed out draining the previous acquisition.");
			free_prepared_transfers(devc);
			return SR_ERR_TIMEOUT;
		}
	}
	if (i == 64) {
		sr_err("Device still streaming after stop; reconnect before retrying.");
		free_prepared_transfers(devc);
		return SR_ERR_IO;
	}
	if (devc->wire_trace)
		sr_dbg("WIRE pre-arm drain summary: discarded %" PRIu64 ", mod32 %u, reads %d,"
			" empty_reads %u, required_empty_reads %u.",
			drain_total, (unsigned int)(drain_total % 32), i + 1,
			drain_empty, (unsigned int)DLA_PREARM_EMPTY_READS);

	if (legacy_path)
		dla_setup(packet, ((const uint16_t[]) { 60, 160, 250 })[devc->threshold],
			((const uint8_t[]) { 7, 2, 1 })[devc->threshold], samples);
	else {
		configure_setup(&setup, devc, samples, has_trigger, mask);
		hardware_buffer_msec = !devc->stream && !has_trigger ? setup.duration_ms : 0;
		dla_setup_configure(packet, &setup);
	}
	if (send_command(sdi, packet) != SR_OK) {
		free_prepared_transfers(devc);
		return SR_ERR_IO;
	}
	wait_before_arm(legacy_path);
	devc->usb_ctx = ((struct drv_context *)sdi->driver->context)->sr_ctx;
	devc->pending = devc->submitted_transfers = 0;
	devc->stopping = devc->stop_sent = devc->cancel_requested = FALSE;
	devc->header_sent = FALSE;
	devc->data_started = FALSE;
	devc->has_trigger = has_trigger;
	first_data_msec = devc->stream ? 0 :
		samples_to_milliseconds(samples, devc->samplerate);
	first_data_msec = MAX(first_data_msec, hardware_buffer_msec);
	devc->first_data_timeout_us = first_data_msec >
		(uint64_t)(G_MAXINT64 - DATA_STALL_USEC) / 1000 ? G_MAXINT64 :
		(int64_t)first_data_msec * 1000 + DATA_STALL_USEC;
	devc->acquiring = TRUE;
	devc->capture_status = "running";
	devc->capture_error = "";
	devc->starting = TRUE;
	devc->last_data_us = g_get_monotonic_time();
	sr_sw_limits_acquisition_start(&devc->limits);
#ifdef _WIN32
	if (devc->use_wch) {
#if DLA_WCH_WORKER
		/* Completion event dispatches immediately; 100ms is only the
		 * no-data/native-exit watchdog, without global timer changes. */
		ret = sr_session_source_add_pollfd(sdi->session, &devc->worker_pollfd,
			100, receive_events, (void *)sdi);
#else
		ret = sr_session_source_add(sdi->session, -1, 0, WCH_POLL_MS,
			receive_events, (void *)sdi);
#endif
	} else
#endif
		ret = usb_source_add(sdi->session, devc->usb_ctx,
			USB_POLL_MS, receive_events, (void *)sdi);
	if (ret != SR_OK) {
		rollback_acquisition((struct sr_dev_inst *)sdi);
		return ret;
	}
	devc->source_added = TRUE;
	ret = std_session_send_df_header(sdi);
	if (ret != SR_OK) {
		sr_err("Failed to deliver acquisition HEADER: %s.", sr_strerror(ret));
		rollback_acquisition((struct sr_dev_inst *)sdi);
		return ret;
	}
	devc->header_sent = TRUE;
	if (devc->stopping) {
#if defined(_WIN32) && DLA_WCH_WORKER
		if (devc->worker && !dla_worker_start(devc->worker, TRUE)) {
			sr_err("Cannot create WCH retirement worker after HEADER stop.");
			rollback_acquisition((struct sr_dev_inst *)sdi);
			return SR_ERR_IO;
		}
#endif
		devc->starting = FALSE;
		return SR_OK;
	}
	for (i = 0; i < NUM_TRANSFERS; i++) {
#ifdef _WIN32
		if (devc->use_wch)
			break;
#endif
		ret = libusb_submit_transfer(devc->transfers[i]);
		if (ret != LIBUSB_SUCCESS) {
			sr_err("Failed to submit USB transfer: %s.",
				libusb_error_name(ret));
			rollback_acquisition((struct sr_dev_inst *)sdi);
			return SR_ERR_IO;
		}
		devc->transfer_submitted[i] = TRUE;
		devc->submitted_transfers++;
	}
	if (legacy_path) {
		dla_channels(packet);
		ret = send_command(sdi, packet);
	} else {
		ret = dla_channels_config(packet, mask, conditions, 0) < 0 ?
			SR_ERR_ARG : send_command(sdi, packet);
	}
	if (ret != SR_OK) {
		sr_err("Failed to arm the acquisition: %s.", sr_strerror(ret));
		rollback_acquisition((struct sr_dev_inst *)sdi);
		return ret;
	}
#if defined(_WIN32) && DLA_WCH_WORKER
	if (devc->worker && !dla_worker_start(devc->worker, devc->stopping)) {
		sr_err("Cannot create WCH read worker after accepted ARM; synchronous rollback required.");
		rollback_acquisition((struct sr_dev_inst *)sdi);
		return SR_ERR_IO;
	}
#endif
	devc->last_data_us = g_get_monotonic_time();
	devc->starting = FALSE;
	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

#if defined(_WIN32) && DLA_WCH_WORKER
	if ((devc->worker_dispatching || (devc->worker && devc->worker->thread)) && !devc->acquiring)
		return SR_ERR;
#endif

	if (devc->acquiring)
		request_stop(devc);
#ifdef _WIN32
	else if (devc->use_wch && has_wch_stop_ownership(devc)) {
		int ret = stop_wch_capture(sdi, "inactive-stop");
		if (ret == SR_OK)
			drain_stopped_endpoint(sdi);
		return ret;
	}
#endif
	return SR_OK;
}

static struct sr_dev_driver fnirsi_dla32_driver_info = {
	.name = "fnirsi-dla32",
	.longname = "FNIRSI DLA-32 (experimental)",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
};
SR_REGISTER_DEV_DRIVER(fnirsi_dla32_driver_info);
