// SPDX-License-Identifier: GPL-3.0-only
/*
 * Copyright (C) 2026 whatshu <shussm@qq.com>
 *
 * FNIRSI DLA-16 experimental driver. Protocol reference:
 * https://github.com/TechBirdCompany/PXView-DLA32-overlay (GPL-3.0).
 * Command framing and CRC checked against factory DLA-Logic 1.0.3.
 */

#include <config.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include <math.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define LOG_PREFIX "fnirsi-dla16"
#define NUM_TRANSFERS 8
#define TRANSFER_SIZE (1024 * 1024)
#define TRANSFER_TIMEOUT_MS 1000
#define USB_POLL_MS 10
#define BUFFER_MEMORY_BITS (UINT64_C(4) * 1024 * 1024 * 1024)
#define DEFAULT_LIMIT_SAMPLES 50000
#define LOGIC_ONLY_MAX_LIMIT_SAMPLES UINT64_C(100000000)
#define CONTINUOUS_WINDOW_MSEC 1000
#define STOP_DRAIN_USEC G_TIME_SPAN_SECOND
#define STOP_DRAIN_BYTES (UINT64_C(64) * 1024 * 1024)
#define STOP_EMPTY_READS 2

struct pwm_setting {
	uint32_t frequency;
	unsigned int duty;
	gboolean enabled;
};

struct dev_context {
	struct sr_sw_limits limits;
	uint64_t samplerate;
	int threshold;
	gboolean stream, continuous, usb3, logic_only;
	struct pwm_setting pwm[2];
	struct dla_decoder decoder;
	uint16_t channel_mask;
	uint64_t run_limit_samples;
	struct sr_context *usb_ctx;
	struct libusb_transfer *transfers[NUM_TRANSFERS];
	gboolean transfer_submitted[NUM_TRANSFERS];
	unsigned int submitted_transfers;
	gboolean acquiring, stopping, stop_sent, cancel_requested;
	gboolean source_added, header_sent;
	size_t pending;
	uint8_t wire_tail[DLA_CHANNELS];
	uint8_t *decoded;
	size_t decoded_size;
};

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
};
static const uint32_t logic_devopts[] = {
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_VOLTAGE_THRESHOLD | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CONN | SR_CONF_GET,
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
	return !g_strcmp0(g_getenv("FNIRSI_DLA16_LOGIC_ONLY"), "1");
}

static uint16_t enabled_channel_mask(const struct sr_dev_inst *sdi)
{
	const struct sr_channel *channel;
	GSList *l;
	uint16_t mask = 0;

	for (l = sdi->channels; l; l = l->next) {
		channel = l->data;
		if (channel->type == SR_CHANNEL_LOGIC && channel->enabled &&
				channel->index < DLA_CHANNELS)
			mask |= 1U << channel->index;
	}
	return mask;
}

static uint64_t maximum_samplerate(const struct dev_context *devc,
	uint16_t mask)
{
	unsigned int channels = __builtin_popcount(mask);

	if (!channels)
		return 0;
	if (!devc->stream)
		return channels <= 8 ? SR_GHZ(1) : SR_MHZ(500);
	if (!devc->usb3)
		return 0;
	if (channels <= 2)
		return SR_GHZ(1);
	if (channels <= 4)
		return SR_MHZ(500);
	if (channels <= 8)
		return SR_MHZ(250);
	return SR_MHZ(125);
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
	uint16_t mask)
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
	return index >= 0 && index < 2 ? index : -1;
}

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
		sdi->model = g_strdup("DLA-16 (experimental)");
		sdi->connection_id = g_strdup(bus_addr);
		sdi->status = SR_ST_INACTIVE;
		sdi->inst_type = SR_INST_USB;
		sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(list[i]),
			libusb_get_device_address(list[i]), NULL);
		devc = sdi->priv = g_malloc0(sizeof(*devc));
		sr_sw_limits_init(&devc->limits);
		devc->limits.limit_samples = DEFAULT_LIMIT_SAMPLES;
		devc->samplerate = SR_MHZ(50);
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

static int send_pwm_config(const struct sr_dev_inst *sdi, unsigned int index,
	const struct pwm_setting *setting)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	uint8_t packet[DLA_COMMAND_SIZE];

	if (!usb->devhdl)
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

	if (devc->acquiring) {
		sr_err("Cannot close while asynchronous transfers are active; stop acquisition first.");
		return SR_ERR;
	}
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
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		return sr_sw_limits_config_get(&devc->limits, key, data);
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

	if (devc->acquiring)
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
		ret = send_pwm_config(sdi, pwm_index, &candidate);
		if (ret == SR_OK)
			devc->pwm[pwm_index] = candidate;
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
	uint16_t mask;
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
		if (maximum == UINT64_MAX)
			maximum_msec = UINT64_MAX / 1000;
		else
			maximum_msec = maximum / devc->samplerate * 1000 +
				(maximum % devc->samplerate) * 1000 / devc->samplerate;
		*data = std_gvar_tuple_u64(1, maximum_msec);
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
}

static void free_prepared_transfers(struct dev_context *devc)
{
	unsigned int i;

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
}

static void finish_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->source_added) {
		usb_source_remove(sdi->session, devc->usb_ctx);
		devc->source_added = FALSE;
	}
	free_prepared_transfers(devc);
	devc->submitted_transfers = 0;
	devc->pending = 0;
	devc->run_limit_samples = 0;
	devc->acquiring = FALSE;
	devc->stopping = FALSE;
	devc->stop_sent = FALSE;
	devc->cancel_requested = FALSE;
	if (devc->header_sent) {
		devc->header_sent = FALSE;
		std_session_send_df_end(sdi);
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

	for (i = 0; i < ARRAY_SIZE(devc->transfers); i++)
		if (devc->transfers[i] && devc->transfer_submitted[i])
			libusb_cancel_transfer(devc->transfers[i]);
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
	uint8_t *output = devc->decoded;
	size_t needed, frames;
	uint64_t samples, remaining;

	samples = 0;
	if (devc->pending) {
		needed = devc->decoder.wire_channels - devc->pending;
		if (length < needed) {
			memcpy(devc->wire_tail + devc->pending, input, length);
			devc->pending += length;
			return 0;
		}
		memcpy(devc->wire_tail + devc->pending, input, needed);
		dla_decode_frames(&devc->decoder, output, devc->wire_tail, 1);
		output += 8 * devc->decoder.unitsize;
		samples += 8;
		input += needed;
		length -= needed;
		devc->pending = 0;
	}
	frames = length / devc->decoder.wire_channels;
	if (frames) {
		dla_decode_frames(&devc->decoder, output, input, frames);
		samples += frames * 8;
		input += frames * devc->decoder.wire_channels;
		length -= frames * devc->decoder.wire_channels;
	}
	if (length) {
		memcpy(devc->wire_tail, input, length);
		devc->pending = length;
	}

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
	sr_session_send(sdi, &packet);
	sr_sw_limits_update_samples_read(&devc->limits, samples);
	return samples;
}

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi = transfer->user_data;
	struct dev_context *devc = sdi->priv;
	gboolean valid_data;

	valid_data = transfer->status == LIBUSB_TRANSFER_COMPLETED ||
		transfer->status == LIBUSB_TRANSFER_TIMED_OUT;
	if (devc->stopping) {
		retire_transfer(transfer);
		return;
	}
	if (!valid_data) {
		sr_err("USB stream discontinuity: transfer status %d (%d bytes).",
			transfer->status, transfer->actual_length);
		request_stop(devc);
		retire_transfer(transfer);
		return;
	}
	if (transfer->actual_length)
		decode_transfer(sdi, transfer->buffer, transfer->actual_length);
	if (devc->stopping ||
			(!devc->stream && devc->run_limit_samples &&
			devc->limits.samples_read >= devc->run_limit_samples) ||
			(devc->stream && sr_sw_limits_check(&devc->limits))) {
		request_stop(devc);
		retire_transfer(transfer);
		return;
	}
	resubmit_transfer(transfer);
}

static void drain_stopped_endpoint(const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	uint8_t *buffer;
	uint64_t total = 0;
	int64_t deadline;
	unsigned int empty = 0;
	int count, length, ret;

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
		ret = libusb_bulk_transfer(usb->devhdl, 0x81, buffer, length,
			&count, 20);
		if (ret != LIBUSB_SUCCESS && ret != LIBUSB_ERROR_TIMEOUT) {
			sr_warn("Post-stop drain failed: %s.", libusb_error_name(ret));
			break;
		}
		if (ret == LIBUSB_ERROR_TIMEOUT && !count) {
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
	g_free(buffer);
}

static void begin_ordered_stop(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint8_t packet[DLA_COMMAND_SIZE];

	if (!devc->stopping || devc->cancel_requested)
		return;
	devc->cancel_requested = TRUE;
	cancel_transfers(devc);
	dla_stop(packet);
	devc->stop_sent = TRUE;
	if (send_command(sdi, packet) != SR_OK)
		sr_err("Failed to send the acquisition stop command.");
}

static int receive_events(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;
	struct timeval tv = {0, 0};
	int ret;

	(void)fd;
	(void)revents;
	begin_ordered_stop(sdi);
	ret = libusb_handle_events_timeout(devc->usb_ctx->libusb_ctx, &tv);
	if (ret != LIBUSB_SUCCESS && !devc->stopping) {
		sr_err("Failed to service USB events: %s.", libusb_error_name(ret));
		request_stop(devc);
	}
	/* Short timed captures must end even if no full USB transfer arrives. */
	if (!devc->stopping && devc->stream && sr_sw_limits_check(&devc->limits))
		request_stop(devc);
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
		sr_err("DLA-16 supports one basic trigger stage, not advanced multi-stage triggers.");
		return SR_ERR_NA;
	}
	stage = trigger->stages->data;
	if (g_slist_length(stage->matches) != 1) {
		sr_err("DLA-16 supports exactly one channel condition per trigger.");
		return SR_ERR_NA;
	}
	match = stage->matches->data;
	if (!match->channel || match->channel->type != SR_CHANNEL_LOGIC ||
			match->channel->index >= DLA_CHANNELS || !match->channel->enabled) {
		sr_err("DLA-16 trigger must use one enabled logic channel.");
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

static unsigned int duration_index(uint64_t milliseconds)
{
	static const uint64_t durations[] = {
		1, 2, 5, 10, 20, 50, 100, 200, 500,
		1000, 2000, 5000, 10000, 20000, 50000,
	};
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
	const struct dev_context *devc, uint64_t samples, gboolean has_trigger)
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
	setup->trigger_position = has_trigger ? 500 : 0;
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
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct dla_setup_config setup;
	uint8_t conditions[DLA_CHANNELS], packet[DLA_COMMAND_SIZE];
	uint64_t samples, maximum;
	uint16_t mask;
	int count, ret, i;
	int64_t drain_deadline;
	gboolean has_trigger, legacy_path;

	if (devc->acquiring)
		return SR_ERR;
	mask = enabled_channel_mask(sdi);
	if (!mask) {
		sr_err("Enable at least one DLA-16 logic channel.");
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
	devc->run_limit_samples = devc->continuous ||
		(devc->stream && devc->limits.limit_msec) ? 0 : samples;
	legacy_path = !devc->stream && mask == UINT16_MAX &&
		devc->samplerate == SR_MHZ(50) && !has_trigger &&
		!devc->limits.limit_msec && samples <= 500000;
	ret = prepare_transfers((struct sr_dev_inst *)sdi);
	if (ret != SR_OK)
		return ret;

	/* Stop and drain stale data before arming the new asynchronous queue. */
	dla_stop(packet);
	if (send_command(sdi, packet) != SR_OK) {
		free_prepared_transfers(devc);
		return SR_ERR_IO;
	}
	drain_deadline = g_get_monotonic_time() + G_TIME_SPAN_SECOND;
	for (i = 0; i < 64; i++) {
		count = 0;
		ret = libusb_bulk_transfer(usb->devhdl, 0x81,
			devc->transfers[0]->buffer, TRANSFER_SIZE, &count, 20);
		if (ret == LIBUSB_ERROR_TIMEOUT && !count)
			break;
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

	if (legacy_path)
		dla_setup(packet, ((const uint16_t[]) { 60, 160, 250 })[devc->threshold],
			((const uint8_t[]) { 7, 2, 1 })[devc->threshold], samples);
	else {
		configure_setup(&setup, devc, samples, has_trigger);
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
	devc->acquiring = TRUE;
	sr_sw_limits_acquisition_start(&devc->limits);
	ret = usb_source_add(sdi->session, devc->usb_ctx,
		USB_POLL_MS, receive_events, (void *)sdi);
	if (ret != SR_OK) {
		devc->acquiring = FALSE;
		free_prepared_transfers(devc);
		return ret;
	}
	devc->source_added = TRUE;
	devc->header_sent = TRUE;
	std_session_send_df_header(sdi);
	if (devc->stopping)
		return SR_OK;
	for (i = 0; i < NUM_TRANSFERS; i++) {
		ret = libusb_submit_transfer(devc->transfers[i]);
		if (ret != LIBUSB_SUCCESS) {
			sr_err("Failed to submit USB transfer: %s; ending the empty acquisition.",
				libusb_error_name(ret));
			request_stop(devc);
			return SR_OK;
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
		sr_err("Failed to arm the acquisition; ending it without sample data.");
		request_stop(devc);
		return SR_OK;
	}
	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->acquiring)
		request_stop(devc);
	return SR_OK;
}

static struct sr_dev_driver fnirsi_dla16_driver_info = {
	.name = "fnirsi-dla16",
	.longname = "FNIRSI DLA-16 (experimental)",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
};
SR_REGISTER_DEV_DRIVER(fnirsi_dla16_driver_info);
