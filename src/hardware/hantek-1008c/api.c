/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 VK4TMZ
 *
 * Hantek 1008C oscilloscope driver.
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
#include <math.h>
#include "protocol.h"

static struct sr_dev_driver hantek_1008c_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_DEVICE_MODE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_RANGE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_LEVEL | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
};

/*
 * PulseView-facing aggregate Scan and Roll transport rates. Roll transports
 * one word for every enabled channel plus one auxiliary word per row, so its
 * stored aggregate values are twice the established CH1-only sample rates.
 * Official C9/CA Scan is exposed for the validated A3=1A..22 region. Scan
 * words are interleaved across the exact enabled-channel count, without the
 * odd-count dummy lanes used by Triggered mode. Nominal aggregate Scan rates
 * are 800/400/200/80/40/20/8/4/2 samples/s; measured host-side row cadences
 * follow the corresponding 2x observation model, and reference-tone timing
 * validates that interpretation. Sub-1 sample/s settings are intentionally
 * not advertised.
 */
static const struct h1008c_rate rate_table[] = {
	{ UINT64_C(2),       0x22, H1008C_MODE_ROLL },
	{ UINT64_C(2),       0x22, H1008C_MODE_SCAN },
	{ UINT64_C(4),       0x21, H1008C_MODE_SCAN },
	{ UINT64_C(4),       0x21, H1008C_MODE_ROLL },
	{ UINT64_C(10),      0x20, H1008C_MODE_ROLL },
	{ UINT64_C(8),       0x20, H1008C_MODE_SCAN },
	{ UINT64_C(18),      0x1f, H1008C_MODE_ROLL },
	{ UINT64_C(20),      0x1f, H1008C_MODE_SCAN },
	{ UINT64_C(46),      0x1e, H1008C_MODE_ROLL },
	{ UINT64_C(40),      0x1e, H1008C_MODE_SCAN },
	{ UINT64_C(100),     0x1d, H1008C_MODE_ROLL },
	{ UINT64_C(80),      0x1d, H1008C_MODE_SCAN },
	{ UINT64_C(200),     0x1c, H1008C_MODE_ROLL },
	{ UINT64_C(200),     0x1c, H1008C_MODE_SCAN },
	{ UINT64_C(402),     0x1b, H1008C_MODE_ROLL },
	{ UINT64_C(400),     0x1b, H1008C_MODE_SCAN },
	{ UINT64_C(802),     0x1a, H1008C_MODE_ROLL },
	{ UINT64_C(800),     0x1a, H1008C_MODE_SCAN },
	{ UINT64_C(2006),    0x19, H1008C_MODE_ROLL },
	{ UINT64_C(4012),    0x18, H1008C_MODE_ROLL },
};

/*
 * Aggregate direct-ADC rates for multi-channel Triggered mode.
 *
 * The physical-width table and every A3 entry below are hardware-validated.
 * Keep these separate from rate_table so Triggered's padded physical-width
 * divisor cannot be confused with Scan's exact enabled-channel divisor.
 *
 * PulseView is given the per-channel rate, calculated as aggregate / physical
 * acquisition width.  Integer division is intentional because libsigrok's
 * samplerate configuration is integer-valued (for example 400000 / 6 is
 * exposed as 66666 samples/s/channel).
 */
static const struct h1008c_rate multichannel_triggered_rate_table[] = {
	{ UINT64_C(2000),    0x19, H1008C_MODE_TRIGGERED },
	{ UINT64_C(4000),    0x18, H1008C_MODE_TRIGGERED },
	{ UINT64_C(8000),    0x17, H1008C_MODE_TRIGGERED },
	{ UINT64_C(20000),   0x16, H1008C_MODE_TRIGGERED },
	{ UINT64_C(40000),   0x15, H1008C_MODE_TRIGGERED },
	{ UINT64_C(80000),   0x14, H1008C_MODE_TRIGGERED },
	{ UINT64_C(200000),  0x13, H1008C_MODE_TRIGGERED },
	{ UINT64_C(400000),  0x12, H1008C_MODE_TRIGGERED },
	{ UINT64_C(800000),  0x11, H1008C_MODE_TRIGGERED },
	{ UINT64_C(2400000), 0x0f, H1008C_MODE_TRIGGERED },
};

#define H1008C_RECOVERY_WINDOW_US  (15 * G_USEC_PER_SEC)
#define H1008C_RECOVERY_INTERVAL_US (500 * 1000)

static const char *trigger_sources[] = {
	"None", "CH1", "CH2", "CH3", "CH4", "CH5", "CH6", "CH7", "CH8",
};
static const char *trigger_slopes[] = { "r", "f" };
static const char *device_modes[] = { "Trigger", "Scan", "Roll" };
static const char *range_names[] = { "Narrow", "Medium", "Wide" };
static const double range_scales[] = { 0.0002, 0.00125, 0.01 };
static const int32_t trigger_matches[] = {
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
};

static int apply_sample_limit(struct dev_context *devc);

SR_PRIV unsigned int h1008c_acquisition_width(unsigned int count)
{
	static const uint8_t widths[] = { 0, 1, 2, 4, 4, 6, 6, 8, 8 };

	return count <= H1008C_NUM_HW_CHANNELS ? widths[count] : 0;
}

SR_PRIV unsigned int h1008c_rate_divisor(enum h1008c_acquisition_mode mode,
		unsigned int enabled_count)
{
	if (!enabled_count)
		return 0;
	if (mode == H1008C_MODE_SCAN)
		return enabled_count;
	if (mode == H1008C_MODE_ROLL)
		return enabled_count + 1;
	return h1008c_acquisition_width(enabled_count);
}

SR_PRIV const char *h1008c_range_name(uint8_t range_id)
{
	return range_id >= 1 && range_id <= ARRAY_SIZE(range_names) ?
		range_names[range_id - 1] : NULL;
}

SR_PRIV int h1008c_range_id(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(range_names); i++) {
		if (!strcmp(name, range_names[i]))
			return i + 1;
	}
	return -1;
}

static double nominal_scale(uint8_t range_id)
{
	return range_id >= 1 && range_id <= ARRAY_SIZE(range_scales) ?
		range_scales[range_id - 1] : 0.0;
}

SR_PRIV const char *h1008c_trigger_source_name(uint8_t source)
{
	if (source == H1008C_TRIGGER_SOURCE_NONE)
		return trigger_sources[0];
	return source < H1008C_NUM_HW_CHANNELS ? trigger_sources[source + 1] : NULL;
}

SR_PRIV int h1008c_trigger_source_id(const char *name)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(trigger_sources); i++) {
		if (!strcmp(name, trigger_sources[i]))
			return i ? (int)i - 1 : H1008C_TRIGGER_SOURCE_NONE;
	}
	return -1;
}

SR_PRIV int h1008c_trigger_level_to_adc(double volts, double zero_adc,
		double volts_per_count, uint16_t *raw_adc)
{
	double raw;

	if (!raw_adc || !isfinite(volts) || !isfinite(zero_adc) ||
	    !isfinite(volts_per_count) || volts_per_count <= 0.0)
		return SR_ERR_ARG;
	raw = zero_adc + volts / volts_per_count;
	if (raw < 0.0 || raw > 4095.0)
		return SR_ERR_ARG;
	*raw_adc = (uint16_t)llround(raw);
	return SR_OK;
}

SR_PRIV size_t h1008c_rate_count(enum h1008c_acquisition_mode mode)
{
	size_t count = 0, i;

	if (mode == H1008C_MODE_TRIGGERED)
		return ARRAY_SIZE(multichannel_triggered_rate_table);
	for (i = 0; i < ARRAY_SIZE(rate_table); i++) {
		if (rate_table[i].mode == mode)
			count++;
	}
	return count;
}

SR_PRIV int h1008c_rate_get(enum h1008c_acquisition_mode mode,
		size_t index, struct h1008c_rate *rate)
{
	size_t i;

	if (!rate)
		return SR_ERR_ARG;
	if (mode == H1008C_MODE_TRIGGERED) {
		if (index >= ARRAY_SIZE(multichannel_triggered_rate_table))
			return SR_ERR_ARG;
		*rate = multichannel_triggered_rate_table[index];
		return SR_OK;
	}
	for (i = 0; i < ARRAY_SIZE(rate_table); i++) {
		if (rate_table[i].mode != mode)
			continue;
		if (!index--) {
			*rate = rate_table[i];
			return SR_OK;
		}
	}
	return SR_ERR_ARG;
}

static unsigned int enabled_channel_count(const struct sr_dev_inst *sdi)
{
	GSList *l;
	unsigned int count = 0;

	for (l = sdi->channels; l; l = l->next) {
		const struct sr_channel *ch = l->data;
		if (ch->type == SR_CHANNEL_ANALOG && ch->enabled)
			count++;
	}
	return count;
}

static void capture_enabled_mask(const struct sr_dev_inst *sdi,
		struct dev_context *devc)
{
	GSList *l;

	memset(devc->enabled_mask, 0, sizeof(devc->enabled_mask));
	devc->enabled_count = 0;
	for (l = sdi->channels; l; l = l->next) {
		const struct sr_channel *ch = l->data;
		if (ch->type != SR_CHANNEL_ANALOG || !ch->enabled)
			continue;
		if (ch->index >= 0 && ch->index < H1008C_NUM_HW_CHANNELS) {
			devc->enabled_mask[ch->index] = 1;
			devc->enabled_count++;
		}
	}
	devc->acquisition_width = h1008c_acquisition_width(devc->enabled_count);
}
SR_PRIV const struct h1008c_rate *h1008c_find_effective_rate(uint64_t samplerate,
		unsigned int divisor, enum h1008c_acquisition_mode mode)
{
	size_t i;

	if (!divisor)
		return NULL;
	if (mode != H1008C_MODE_TRIGGERED) {
		for (i = 0; i < ARRAY_SIZE(rate_table); i++) {
			const struct h1008c_rate *rate = &rate_table[i];

			if (rate->mode == mode && rate->samplerate >= divisor &&
			    rate->samplerate / divisor == samplerate)
				return rate;
		}
		return NULL;
	}
	for (i = 0; i < ARRAY_SIZE(multichannel_triggered_rate_table); i++) {
		const struct h1008c_rate *rate = &multichannel_triggered_rate_table[i];

		if (rate->samplerate / divisor == samplerate)
			return rate;
	}
	return NULL;
}

static const struct h1008c_rate *default_rate_for_mode(
		enum h1008c_acquisition_mode mode)
{
	const struct h1008c_rate *selected = NULL;
	size_t i;

	if (mode == H1008C_MODE_TRIGGERED)
		return &multichannel_triggered_rate_table[
			ARRAY_SIZE(multichannel_triggered_rate_table) - 1];
	for (i = 0; i < ARRAY_SIZE(rate_table); i++) {
		if (rate_table[i].mode != mode)
			continue;
		if (!selected || rate_table[i].samplerate > selected->samplerate)
			selected = &rate_table[i];
	}
	return selected;
}

static int select_rate(struct dev_context *devc,
		const struct h1008c_rate *rate, unsigned int divisor)
{
	if (!rate || !divisor)
		return SR_ERR_SAMPLERATE;
	devc->base_samplerate = rate->samplerate;
	devc->samplerate = rate->samplerate / divisor;
	devc->a3 = rate->a3;
	devc->acquisition_mode = rate->mode;
	return apply_sample_limit(devc);
}

#define H1008C_TRIGGERED_FRAME_SAMPLES UINT64_C(4000)

static uint64_t effective_sample_limit(const struct dev_context *devc,
		uint64_t requested)
{
	uint64_t frames;

	if (!requested || devc->acquisition_mode != H1008C_MODE_TRIGGERED)
		return requested;

	/*
	 * TRIGGERED mode has an indivisible 4K hardware frame. Round the configured
	 * limit up here, at the configuration boundary, so frontends such as
	 * PulseView also learn the true capture size instead of allocating for
	 * (for example) 5000 samples and clipping the second 4K frame.
	 */
	{
		uint64_t frame_samples = H1008C_TRIGGERED_FRAME_SAMPLES /
			MAX(1U, devc->acquisition_width);
		frames = (requested + frame_samples - 1) / frame_samples;
		return frames * frame_samples;
	}
}

static int apply_sample_limit(struct dev_context *devc)
{
	GVariant *value;
	uint64_t effective;
	int ret;

	effective = effective_sample_limit(devc, devc->requested_limit_samples);
	value = g_variant_new_uint64(effective);
	ret = sr_sw_limits_config_set(&devc->limits, SR_CONF_LIMIT_SAMPLES, value);
	if (ret == SR_OK && effective != devc->requested_limit_samples)
		sr_info("Triggered sample limit rounded from %" PRIu64
			" to %" PRIu64 " to preserve complete 4K frames.",
			devc->requested_limit_samples, effective);
	return ret;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc = di->context;
	struct libusb_device_descriptor des;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb_match;
	libusb_device **devlist;
	GSList *devices, *conn_devices, *l;
	const char *conn;
	char connection_id[64];
	char name[8];
	int ch, i;

	devices = NULL;
	conn_devices = NULL;
	conn = NULL;
	for (l = options; l; l = l->next) {
		struct sr_config *src = l->data;
		if (src->key == SR_CONF_CONN) {
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (conn)
		conn_devices = sr_usb_find(drvc->sr_ctx->libusb_ctx, conn);

	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	for (i = 0; devlist[i]; i++) {
		if (conn) {
			usb_match = NULL;
			for (l = conn_devices; l; l = l->next) {
				usb_match = l->data;
				if (usb_match->bus == libusb_get_bus_number(devlist[i]) &&
				    usb_match->address == libusb_get_device_address(devlist[i]))
					break;
			}
			if (!l)
				continue;
		}

		libusb_get_device_descriptor(devlist[i], &des);
		if (des.idVendor != H1008C_USB_VID || des.idProduct != H1008C_USB_PID)
			continue;
		if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
			continue;

		sdi = g_malloc0(sizeof(*sdi));
		sdi->driver = &hantek_1008c_driver_info;
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup("Hantek");
		sdi->model = g_strdup("1008C");
		sdi->connection_id = g_strdup(connection_id);
		sdi->inst_type = SR_INST_USB;
		sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
			libusb_get_device_address(devlist[i]), NULL);

		/* All eight physical analog inputs are selectable; default to CH1 only. */
		for (ch = 0; ch < H1008C_NUM_HW_CHANNELS; ch++) {
			g_snprintf(name, sizeof(name), "CH%d", ch + 1);
			sr_channel_new(sdi, ch, SR_CHANNEL_ANALOG, ch == 0, name);
		}
		devc = g_malloc0(sizeof(*devc));
		sr_sw_limits_init(&devc->limits);
		devc->samplerate = H1008C_DEFAULT_BASE_SAMPLERATE;
		devc->base_samplerate = H1008C_DEFAULT_BASE_SAMPLERATE;
		devc->enabled_count = 1;
		devc->acquisition_width = 1;
		devc->enabled_mask[0] = 1;
		devc->a3 = H1008C_A3_24MSPS;
		devc->range_id = H1008C_DEFAULT_RANGE;
		devc->acquisition_mode = H1008C_MODE_TRIGGERED;
		devc->trigger_enabled = FALSE;
		devc->trigger_source = H1008C_TRIGGER_SOURCE_NONE;
		devc->trigger_slope = H1008C_TRIGGER_RISING;
		devc->trigger_level_volts = 0.0;
		devc->trigger_level_adc = 0x0800;
		sdi->priv = devc;
		devices = g_slist_append(devices, sdi);
	}

	g_slist_free_full(conn_devices, (GDestroyNotify)sr_usb_dev_inst_free);
	libusb_free_device_list(devlist, 1);
	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	(void)cg;

	switch (key) {
	case SR_CONF_CONN: {
		struct sr_usb_dev_inst *usb = sdi->conn;
		if (!usb)
			return SR_ERR_ARG;
		*data = g_variant_new_printf("%d.%d", usb->bus, usb->address);
		return SR_OK;
	}
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_FRAMES:
		return sr_sw_limits_config_get(&devc->limits, key, data);
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->samplerate);
		return SR_OK;
	case SR_CONF_DEVICE_MODE:
		*data = g_variant_new_string(
			devc->acquisition_mode == H1008C_MODE_TRIGGERED ? "Trigger" :
			devc->acquisition_mode == H1008C_MODE_SCAN ? "Scan" : "Roll");
		return SR_OK;
	case SR_CONF_RANGE:
		*data = g_variant_new_string(h1008c_range_name(devc->range_id));
		return SR_OK;
	case SR_CONF_TRIGGER_SOURCE:
		*data = g_variant_new_string(
			h1008c_trigger_source_name(devc->trigger_source));
		return SR_OK;
	case SR_CONF_TRIGGER_SLOPE:
		*data = g_variant_new_string(
			devc->trigger_slope == H1008C_TRIGGER_RISING ? "r" : "f");
		return SR_OK;
	case SR_CONF_TRIGGER_LEVEL:
		*data = g_variant_new_double(devc->trigger_level_volts);
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static int config_set(uint32_t key, GVariant *data,
		const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc = sdi->priv;
	const struct h1008c_rate *rate;
	uint64_t samplerate;
	(void)cg;

	if (key == SR_CONF_LIMIT_SAMPLES) {
		devc->requested_limit_samples = g_variant_get_uint64(data);
		return apply_sample_limit(devc);
	}
	if (key == SR_CONF_LIMIT_FRAMES)
		return sr_sw_limits_config_set(&devc->limits, key, data);
	if (key == SR_CONF_RANGE) {
		const char *name = g_variant_get_string(data, NULL);
		int range_id = h1008c_range_id(name);

		if (range_id < 0)
			return SR_ERR_ARG;
		devc->range_id = range_id;
		sr_info("Selected input range: %s (A2=%02x, nominal %.9g V/count).",
			h1008c_range_name(devc->range_id), devc->range_id,
			nominal_scale(devc->range_id));
		return SR_OK;
	}
	if (key == SR_CONF_DEVICE_MODE) {
		const struct h1008c_rate *default_rate;
		const char *name = g_variant_get_string(data, NULL);
		enum h1008c_acquisition_mode mode;
		unsigned int enabled_count, divisor;

		if (!strcmp(name, "Trigger"))
			mode = H1008C_MODE_TRIGGERED;
		else if (!strcmp(name, "Scan"))
			mode = H1008C_MODE_SCAN;
		else if (!strcmp(name, "Roll"))
			mode = H1008C_MODE_ROLL;
		else
			return SR_ERR_ARG;
		enabled_count = MAX(1U, enabled_channel_count(sdi));
		divisor = h1008c_rate_divisor(mode, enabled_count);
		if (mode == devc->acquisition_mode)
			return SR_OK;
		default_rate = default_rate_for_mode(mode);
		if (select_rate(devc, default_rate, divisor) != SR_OK)
			return SR_ERR;
		if (mode != H1008C_MODE_TRIGGERED)
			devc->trigger_source = H1008C_TRIGGER_SOURCE_NONE;
		sr_info("Selected %s device mode; defaulting to %" PRIu64
			" samples/s, A3=%02x.", name, devc->samplerate, devc->a3);
		return SR_OK;
	}
	if (key == SR_CONF_TRIGGER_SOURCE) {
		const char *name = g_variant_get_string(data, NULL);
		int source = h1008c_trigger_source_id(name);

		if (source < 0)
			return SR_ERR_ARG;
		if (source != H1008C_TRIGGER_SOURCE_NONE &&
		    devc->acquisition_mode != H1008C_MODE_TRIGGERED) {
			sr_err("Hardware trigger sources are available in Trigger mode only.");
			return SR_ERR_NA;
		}
		if (source != H1008C_TRIGGER_SOURCE_NONE &&
		    !devc->enabled_mask[source]) {
			sr_err("CH%d must be enabled before it can be selected as "
				"the hardware trigger source.", source + 1);
			return SR_ERR_NA;
		}
		devc->trigger_source = source;
		sr_info("Selected hardware trigger source: %s.", name);
		return SR_OK;
	}
	if (key == SR_CONF_TRIGGER_SLOPE) {
		const char *slope = g_variant_get_string(data, NULL);
		if (!strcmp(slope, "r"))
			devc->trigger_slope = H1008C_TRIGGER_RISING;
		else if (!strcmp(slope, "f"))
			devc->trigger_slope = H1008C_TRIGGER_FALLING;
		else
			return SR_ERR_ARG;
		return SR_OK;
	}
	if (key == SR_CONF_TRIGGER_LEVEL) {
		double volts = g_variant_get_double(data);

		if (!isfinite(volts))
			return SR_ERR_ARG;
		devc->trigger_level_volts = volts;
		sr_info("Selected hardware trigger level: %g V.", volts);
		return SR_OK;
	}
	if (key != SR_CONF_SAMPLERATE)
		return SR_ERR_NA;
	samplerate = g_variant_get_uint64(data);
	devc->acquisition_width = h1008c_acquisition_width(enabled_channel_count(sdi));
	rate = h1008c_find_effective_rate(samplerate,
		h1008c_rate_divisor(devc->acquisition_mode,
			enabled_channel_count(sdi)),
		devc->acquisition_mode);
	if (!rate)
		return SR_ERR_SAMPLERATE;
	/*
	 * PulseView may set sample count and samplerate in either order. Re-apply
	 * the user's requested sample limit after mode selection so TRIGGERED always
	 * advertises an integral number of 4K frames while continuous ROLL/Scan
	 * modes remain exact.
	 */
	if (select_rate(devc, rate,
	    h1008c_rate_divisor(devc->acquisition_mode,
		    enabled_channel_count(sdi))) != SR_OK)
		return SR_ERR;
	sr_info("Selected %" PRIu64 " samples/s: %s mode, A3=%02x.",
		devc->samplerate,
		devc->acquisition_mode == H1008C_MODE_TRIGGERED ? "triggered" :
		devc->acquisition_mode == H1008C_MODE_SCAN ? "scan" : "roll",
		devc->a3);
	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
		const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	if (key == SR_CONF_SAMPLERATE) {
		struct dev_context *devc;
		uint64_t rates[ARRAY_SIZE(rate_table) +
			ARRAY_SIZE(multichannel_triggered_rate_table)];
		unsigned int enabled_count, divisor, n = 0;
		size_t i;

		if (!sdi || !sdi->priv)
			return SR_ERR_ARG;
		devc = sdi->priv;

		/* Keep configuration usable while the frontend temporarily has no
		 * channels selected; acquisition_start still rejects that state. */
		enabled_count = MAX(1U, enabled_channel_count(sdi));
		divisor = h1008c_rate_divisor(devc->acquisition_mode, enabled_count);
		if (devc->acquisition_mode == H1008C_MODE_TRIGGERED) {
			for (i = 0;
			     i < ARRAY_SIZE(multichannel_triggered_rate_table); i++) {
				uint64_t effective =
					multichannel_triggered_rate_table[i].samplerate /
					divisor;

				if (!n || rates[n - 1] != effective)
					rates[n++] = effective;
			}
		} else {
			for (i = 0; i < ARRAY_SIZE(rate_table); i++) {
				uint64_t effective;

				if (rate_table[i].mode != devc->acquisition_mode ||
				    rate_table[i].samplerate < divisor)
					continue;
				effective = rate_table[i].samplerate / divisor;
				if (!n || rates[n - 1] != effective)
					rates[n++] = effective;
			}
		}
		*data = std_gvar_samplerates(rates, n);
		return SR_OK;
	}
	if (key == SR_CONF_DEVICE_MODE) {
		*data = g_variant_new_strv(device_modes, ARRAY_SIZE(device_modes));
		return SR_OK;
	}
	if (key == SR_CONF_RANGE) {
		*data = g_variant_new_strv(range_names, ARRAY_SIZE(range_names));
		return SR_OK;
	}
	if (key == SR_CONF_TRIGGER_SOURCE) {
		*data = g_variant_new_strv(trigger_sources, ARRAY_SIZE(trigger_sources));
		return SR_OK;
	}
	if (key == SR_CONF_TRIGGER_SLOPE) {
		*data = g_variant_new_strv(trigger_slopes, ARRAY_SIZE(trigger_slopes));
		return SR_OK;
	}
	if (key == SR_CONF_TRIGGER_MATCH) {
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		return SR_OK;
	}
	return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
}

static int config_channel_set(const struct sr_dev_inst *sdi,
		struct sr_channel *ch, unsigned int changes)
{
	struct dev_context *devc = sdi->priv;
	const struct h1008c_rate *rate;

	(void)ch;
	if (!(changes & SR_CHANNEL_SET_ENABLED))
		return SR_OK;

	/* sr_dev_channel_enable() has already updated sdi->channels. Refresh all
	 * channel-count-derived configuration immediately, before the next Run. */
	capture_enabled_mask(sdi, devc);
	if (devc->trigger_source != H1008C_TRIGGER_SOURCE_NONE &&
	    !devc->enabled_mask[devc->trigger_source]) {
		sr_info("Selected trigger source CH%u was disabled; falling back to None.",
			devc->trigger_source + 1);
		devc->trigger_source = H1008C_TRIGGER_SOURCE_NONE;
	}
	if (!devc->acquisition_width) {
		devc->samplerate = devc->base_samplerate;
		return apply_sample_limit(devc);
	}

	/* Preserve the aggregate A3 selection across channel-count changes. */
	{
		unsigned int divisor = h1008c_rate_divisor(devc->acquisition_mode,
			devc->enabled_count);
		rate = h1008c_find_effective_rate(devc->base_samplerate / divisor,
			divisor, devc->acquisition_mode);
	}
	if (!rate)
		rate = default_rate_for_mode(devc->acquisition_mode);

	return select_rate(devc, rate,
		h1008c_rate_divisor(devc->acquisition_mode, devc->enabled_count));
}

static int dev_open(struct sr_dev_inst *sdi)
{
	return h1008c_open(sdi);
}

static int dev_close(struct sr_dev_inst *sdi)
{
	return h1008c_close(sdi);
}

static void load_persistent_calibration(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	GKeyFile *keyfile;
	GError *error;
	gchar *path, *section;
	gint version;
	double zero_adc, volts_per_count;
	unsigned int channel;

	memset(devc->calibration_valid, 0, sizeof(devc->calibration_valid));
	memset(devc->calibration_zero_adc, 0, sizeof(devc->calibration_zero_adc));
	memset(devc->calibration_volts_per_count, 0,
		sizeof(devc->calibration_volts_per_count));

	path = g_build_filename(g_get_user_data_dir(), "hantek-1008c",
		"calibration.ini", NULL);
	keyfile = g_key_file_new();
	error = NULL;
	if (!g_key_file_load_from_file(keyfile, path, G_KEY_FILE_NONE, &error)) {
		if (error && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
			sr_warn("Unable to read calibration file %s: %s.", path, error->message);
		g_clear_error(&error);
		g_key_file_free(keyfile);
		g_free(path);
		return;
	}

	error = NULL;
	version = g_key_file_get_integer(keyfile, "format", "version", &error);
	if (error || version != 1) {
		sr_warn("Ignoring unsupported Hantek calibration format in %s.", path);
		g_clear_error(&error);
		g_key_file_free(keyfile);
		g_free(path);
		return;
	}

	for (channel = 0; channel < H1008C_NUM_HW_CHANNELS; channel++) {
		section = g_strdup_printf("device %s channel CH%u range %02X",
			sdi->connection_id ? sdi->connection_id : "", channel + 1,
			devc->range_id);
		if (!g_key_file_has_group(keyfile, section)) {
			g_free(section);
			continue;
		}

		error = NULL;
		zero_adc = g_key_file_get_double(keyfile, section, "zero_adc", &error);
		if (error) {
			sr_warn("Invalid zero_adc in Hantek calibration section %s.", section);
			g_clear_error(&error);
			g_free(section);
			continue;
		}
		volts_per_count = g_key_file_get_double(keyfile, section,
			"volts_per_count", &error);
		if (error || !isfinite(zero_adc) || !isfinite(volts_per_count) ||
			volts_per_count <= 0.0) {
			sr_warn("Invalid Hantek calibration values in section %s.", section);
			g_clear_error(&error);
			g_free(section);
			continue;
		}

		devc->calibration_zero_adc[channel] = zero_adc;
		devc->calibration_volts_per_count[channel] = volts_per_count;
		devc->calibration_valid[channel] = TRUE;
		sr_info("Loaded calibration for CH%u range %s (A2=%02X): zero=%.3f, "
			"scale=%.9g V/count.", channel + 1,
			h1008c_range_name(devc->range_id), devc->range_id,
			zero_adc, volts_per_count);
		g_free(section);
	}

	g_key_file_free(keyfile);
	g_free(path);
}

static void calibrate_samples(const struct dev_context *devc,
		unsigned int channel, float *samples, size_t count)
{
	size_t i;

	if (channel >= H1008C_NUM_HW_CHANNELS ||
		!devc->calibration_valid[channel])
		return;
	for (i = 0; i < count; i++)
		samples[i] = (float)((samples[i] - devc->calibration_zero_adc[channel]) *
			devc->calibration_volts_per_count[channel]);
}

static int update_trigger_level_adc(struct dev_context *devc)
{
	double zero_adc, volts_per_count;
	unsigned int source;
	int ret;

	if (devc->trigger_source == H1008C_TRIGGER_SOURCE_NONE) {
		devc->trigger_level_adc = 0x0800;
		return SR_OK;
	}
	source = devc->trigger_source;
	if (devc->calibration_valid[source]) {
		zero_adc = devc->calibration_zero_adc[source];
		volts_per_count = devc->calibration_volts_per_count[source];
	} else {
		zero_adc = 2048.0;
		volts_per_count = nominal_scale(devc->range_id);
		sr_warn("No calibration for CH%u range %s; trigger conversion uses "
			"midscale zero and nominal %.9g V/count.", source + 1,
			h1008c_range_name(devc->range_id), volts_per_count);
	}
	ret = h1008c_trigger_level_to_adc(devc->trigger_level_volts,
		zero_adc, volts_per_count, &devc->trigger_level_adc);
	if (ret != SR_OK) {
		sr_err("Trigger level %g V is outside the representable CH%u %s range.",
			devc->trigger_level_volts, source + 1,
			h1008c_range_name(devc->range_id));
		return ret;
	}
	sr_info("Trigger level for CH%u range %s: %g V -> AB=%04x "
		"(zero=%.3f, scale=%.9g V/count%s).", source + 1,
		h1008c_range_name(devc->range_id), devc->trigger_level_volts,
		devc->trigger_level_adc, zero_adc, volts_per_count,
		devc->calibration_valid[source] ? ", calibrated" : ", nominal fallback");
	return SR_OK;
}

static void send_analog_channel(struct sr_dev_inst *sdi,
		struct sr_channel *channel, float *samples, size_t count,
		gboolean calibrated)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;

	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.num_samples = count;
	analog.data = samples;
	if (calibrated) {
		analog.meaning->mq = SR_MQ_VOLTAGE;
		analog.meaning->unit = SR_UNIT_VOLT;
		analog.encoding->digits = 3;
		analog.spec->spec_digits = 3;
	} else {
		analog.meaning->mq = SR_MQ_COUNT;
		analog.meaning->unit = SR_UNIT_PIECE;
		analog.encoding->digits = 0;
		analog.spec->spec_digits = 0;
	}
	analog.meaning->mqflags = 0;
	analog.meaning->channels = g_slist_append(NULL, channel);

	sr_session_send(sdi, &packet);
	g_slist_free(analog.meaning->channels);
}

static int send_analog_samples(struct sr_dev_inst *sdi, const float *samples,
		size_t count, gboolean frame)
{
	struct dev_context *devc = sdi->priv;
	struct sr_channel *ch;
	float *channel_samples;
	GSList *l;
	size_t i, lane;
	gboolean calibrated;

	/*
	 * Each channel needs its own packet because analogue meaning and encoding
	 * apply to the whole packet. CH1 has a persisted voltage calibration;
	 * channels without an independently established calibration must remain
	 * honest raw ADC counts rather than inheriting CH1's scale and offset.
	 */
	channel_samples = g_try_new(float, count);
	if (!channel_samples)
		return SR_ERR_MALLOC;

	if (frame)
		std_session_send_df_frame_begin(sdi);
	lane = 0;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_ANALOG || !ch->enabled)
			continue;
		for (i = 0; i < count; i++)
			channel_samples[i] = samples[i * devc->enabled_count + lane];
		calibrated = ch->index >= 0 && ch->index < H1008C_NUM_HW_CHANNELS &&
			devc->calibration_valid[ch->index];
		if (calibrated)
			calibrate_samples(devc, ch->index, channel_samples, count);
		send_analog_channel(sdi, ch, channel_samples, count, calibrated);
		lane++;
	}
	if (frame)
		std_session_send_df_frame_end(sdi);
	g_free(channel_samples);

	return SR_OK;
}

static int receive_samples(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;
	float *samples;
	size_t count;
	uint64_t remain;
	gboolean frame;
	int ret;

	(void)fd;
	(void)revents;
	if (!devc->running)
		return TRUE;

	frame = devc->acquisition_mode == H1008C_MODE_TRIGGERED;
	if (frame)
		ret = h1008c_acquire_triggered_frame(sdi, &samples, &count);
	else if (devc->acquisition_mode == H1008C_MODE_SCAN)
		ret = h1008c_read_scan(sdi, &samples, &count);
	else
		ret = h1008c_read_roll(sdi, &samples, &count);
	if (ret != SR_OK) {
		sr_err("Hantek 1008C %s acquisition failed; stopping.",
			frame ? "triggered" :
			devc->acquisition_mode == H1008C_MODE_SCAN ? "scan" : "roll");
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	/* Empty polls mean no samples are ready yet, including an armed triggered. */
	if (!count)
		return TRUE;

	remain = count;
	/*
	 * A triggered is one physical acquisition frame. Never truncate a completed
	 * hardware frame merely to satisfy an aggregate sample limit: doing so
	 * makes the final sweep appear shortened in frontends such as PulseView.
	 * Let the software limit stop acquisition after the complete frame, even
	 * when that means samples_read exceeds the requested limit by < 4K.
	 *
	 * Roll mode is a genuine continuous stream, so it may stop exactly at the
	 * requested sample count.
	 */
	if (!frame && devc->limits.limit_samples) {
		uint64_t left = devc->limits.limit_samples > devc->limits.samples_read ?
			devc->limits.limit_samples - devc->limits.samples_read : 0;
		remain = MIN((uint64_t)count, left);
	}
	if (!remain) {
		g_free(samples);
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	ret = send_analog_samples(sdi, samples, remain, frame);
	g_free(samples);
	if (ret != SR_OK) {
		sr_err("Unable to allocate per-channel analogue sample buffer.");
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	sr_sw_limits_update_samples_read(&devc->limits, remain);
	if (frame) {
		devc->triggered_count++;
		sr_sw_limits_update_frames_read(&devc->limits, 1);
		sr_dbg("triggered=%" PRIu64 " frame=%" PRIu64 " samples=%" PRIu64
			" total=%" PRIu64 " direct-adc",
			devc->triggered_count, devc->limits.frames_read, remain,
			devc->limits.samples_read);
	} else {
		sr_dbg("%s samples=%" PRIu64 " total=%" PRIu64,
			devc->acquisition_mode == H1008C_MODE_SCAN ? "scan" : "roll",
			remain, devc->limits.samples_read);
	}

	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);
	return TRUE;
}

static int configure_session_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;

	devc->trigger_enabled = FALSE;
	trigger = sr_session_trigger_get(sdi->session);
	if (!trigger || !trigger->stages) {
		/*
		 * PulseView currently exposes SR_CONF_TRIGGER_SOURCE/SLOPE for
		 * analog devices but does not construct a generic analog session
		 * trigger from the signal popup.  Treat an explicitly selected
		 * channel source as a frontend fallback; the canonical session trigger
		 * path below still takes precedence whenever one is present.
		 */
		if (devc->trigger_source == H1008C_TRIGGER_SOURCE_NONE)
			return SR_OK;
		if (!devc->enabled_mask[devc->trigger_source]) {
			sr_err("CH%u must be enabled when selected as hardware trigger.",
				devc->trigger_source + 1);
			return SR_ERR_ARG;
		}
		if (devc->acquisition_mode != H1008C_MODE_TRIGGERED) {
			sr_err("Hantek 1008C hardware edge triggering is currently "
				"supported in triggered mode only.");
			return SR_ERR_NA;
		}
		devc->trigger_enabled = TRUE;
		return SR_OK;
	}
	if (trigger->stages->next) {
		sr_err("Hantek 1008C supports one trigger stage only.");
		return SR_ERR_ARG;
	}
	stage = trigger->stages->data;
	if (!stage->matches || stage->matches->next) {
		sr_err("Hantek 1008C supports one edge trigger match only.");
		return SR_ERR_ARG;
	}
	match = stage->matches->data;
	if (!match->channel || match->channel->index < 0 ||
	    match->channel->index >= H1008C_NUM_HW_CHANNELS ||
	    match->channel->type != SR_CHANNEL_ANALOG) {
		sr_err("Hantek 1008C hardware trigger source must be CH1 through CH8.");
		return SR_ERR_ARG;
	}
	if (!devc->enabled_mask[match->channel->index]) {
		sr_err("CH%d must be enabled for the Hantek 1008C hardware trigger.",
			match->channel->index + 1);
		return SR_ERR_ARG;
	}
	devc->trigger_source = match->channel->index;
	if (match->match == SR_TRIGGER_RISING)
		devc->trigger_slope = H1008C_TRIGGER_RISING;
	else if (match->match == SR_TRIGGER_FALLING)
		devc->trigger_slope = H1008C_TRIGGER_FALLING;
	else {
		sr_err("Hantek 1008C supports rising/falling hardware edge triggers only.");
		return SR_ERR_ARG;
	}
	if (devc->acquisition_mode != H1008C_MODE_TRIGGERED) {
		sr_err("Hantek 1008C hardware edge triggering is currently "
			"supported in triggered mode only.");
		return SR_ERR_NA;
	}
	devc->trigger_enabled = TRUE;
	return SR_OK;
}

static int reopen_and_startup_with_retry(struct sr_dev_inst *sdi,
		struct dev_context *devc)
{
	gint64 started, deadline, now, delay;
	unsigned int attempts;
	int ret;

	started = g_get_monotonic_time();
	deadline = started + H1008C_RECOVERY_WINDOW_US;
	attempts = 1;
	ret = h1008c_reopen(sdi);
	if (ret == SR_OK)
		ret = h1008c_startup(sdi, devc->a3, devc->range_id, devc->enabled_count,
			devc->enabled_mask);
	if (ret == SR_OK)
		return SR_OK;

	sr_warn("USB acquisition setup failed; entering recovery.");
	sr_warn("USB recovery window 15.0 s, retry interval 0.5 s.");
	for (;;) {
		now = g_get_monotonic_time();
		if (now >= deadline)
			break;
		delay = MIN((gint64)H1008C_RECOVERY_INTERVAL_US, deadline - now);
		g_usleep((gulong)delay);
		attempts++;
		sr_warn("USB recovery retry %u at +%.1f s.", attempts - 1,
			(double)(g_get_monotonic_time() - started) / G_USEC_PER_SEC);
		ret = h1008c_reopen(sdi);
		if (ret == SR_OK)
			ret = h1008c_startup(sdi, devc->a3, devc->range_id, devc->enabled_count,
				devc->enabled_mask);
		if (ret == SR_OK) {
			sr_info("USB recovery succeeded after %.1f s (%u setup attempts).",
				(double)(g_get_monotonic_time() - started) /
				G_USEC_PER_SEC, attempts);
			return SR_OK;
		}
		sr_warn("USB recovery retry %u failed.", attempts - 1);
	}

	sr_err("USB recovery failed after %.1f s (%u setup attempts).",
		(double)(g_get_monotonic_time() - started) / G_USEC_PER_SEC,
		attempts);
	return SR_ERR;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_dev_inst *mutable_sdi = (struct sr_dev_inst *)sdi;

	capture_enabled_mask(sdi, devc);
	if (!devc->enabled_count) {
		sr_err("Hantek 1008C requires at least one enabled analog channel.");
		return SR_ERR_ARG;
	}
	devc->samplerate = devc->base_samplerate /
		h1008c_rate_divisor(devc->acquisition_mode, devc->enabled_count);
	if (apply_sample_limit(devc) != SR_OK)
		return SR_ERR;

	load_persistent_calibration(sdi);
	if (configure_session_trigger(sdi) != SR_OK)
		return SR_ERR;
	if (update_trigger_level_adc(devc) != SR_OK)
		return SR_ERR;
	/*
	 * Keep the full startup/final configuration on the selected A3 for both
	 * acquisition families.  The Python reference ROLL path is validated this
	 * way, and the official application likewise applies the selected A3 as
	 * part of final initialization rather than substituting a triggered-only rate.
	 * h1008c_start_roll() deliberately sends the same A3 again immediately
	 * before A4 02, matching the validated C7/C8 laboratory sequence.
	 */
	if (reopen_and_startup_with_retry(mutable_sdi, devc) != SR_OK)
		return SR_ERR;
	devc->running = TRUE;
	devc->triggered_count = 0;
	devc->triggered_armed = FALSE;
	devc->triggered_forced = FALSE;
	devc->triggered_arm_us = 0;
	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);
	(void)sr_session_send_meta(sdi, SR_CONF_SAMPLERATE,
		g_variant_new_uint64(devc->samplerate));
	devc->scan_carry_len = 0;
	if (devc->acquisition_mode == H1008C_MODE_ROLL) {
		if (h1008c_start_roll(sdi, devc->a3) != SR_OK) {
			devc->running = FALSE;
			return SR_ERR;
		}
	} else if (devc->acquisition_mode == H1008C_MODE_SCAN) {
		if (h1008c_start_scan(sdi, devc->a3) != SR_OK) {
			devc->running = FALSE;
			return SR_ERR;
		}
	}
	sr_info("Acquisition start: %" PRIu64
		" samples/s, %s mode, A3=%02x, trigger=%s%s.",
		devc->samplerate,
		devc->acquisition_mode == H1008C_MODE_TRIGGERED ? "triggered" :
		devc->acquisition_mode == H1008C_MODE_SCAN ? "scan" : "roll",
		devc->a3,
		devc->trigger_enabled ? "normal/edge" : "auto/free-running",
		devc->trigger_enabled ?
			(devc->trigger_slope == H1008C_TRIGGER_RISING ?
			"/rising" : "/falling") : "");
	return sr_session_source_add(sdi->session, -1, 0,
		devc->acquisition_mode == H1008C_MODE_TRIGGERED ? 1 : 5,
		receive_samples, (struct sr_dev_inst *)sdi);
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	int ret;

	if (!devc->running)
		return SR_OK;
	sr_dbg("Acquisition stop requested: mode=%d triggered_armed=%s.",
		devc->acquisition_mode, devc->triggered_armed ? "yes" : "no");
	if (devc->acquisition_mode == H1008C_MODE_TRIGGERED && devc->triggered_armed) {
		ret = h1008c_abort_frame(sdi);
		if (ret != SR_OK)
			sr_warn("Failed to abort armed triggered during acquisition stop.");
	}
	devc->running = FALSE;
	sr_session_source_remove(sdi->session, -1);
	std_session_send_df_end(sdi);
	sr_dbg("Acquisition stop complete.");
	return SR_OK;
}

static struct sr_dev_driver hantek_1008c_driver_info = {
	.name = "hantek-1008c",
	.longname = "Hantek 1008C",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_channel_set = config_channel_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(hantek_1008c_driver_info);
