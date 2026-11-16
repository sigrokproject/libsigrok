/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Felipe Calliari <calliarifelipe@gmail.com>
 *
 * Driver for the "LogicAnalyzer" firmware by Agustín Gimenez Bernad (gusmanb)
 * running on Raspberry Pi Pico / Pico W / Pico 2 / RP2040-Zero boards.
 * Firmware: https://github.com/gusmanb/logicanalyzer
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
#include <string.h>
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
};

/* Candidate rates; filtered by what the device reports. */
static const uint64_t base_samplerates[] = {
	SR_KHZ(10), SR_KHZ(20), SR_KHZ(25), SR_KHZ(50),
	SR_KHZ(100), SR_KHZ(200), SR_KHZ(250), SR_KHZ(500),
	SR_MHZ(1), SR_MHZ(2), SR_MHZ(2.5), SR_MHZ(5),
	SR_MHZ(10), SR_MHZ(20), SR_MHZ(25), SR_MHZ(40), SR_MHZ(50),
	SR_MHZ(100), SR_MHZ(200),
};

static struct sr_dev_driver gusmanb_logicanalyzer_driver_info;

static uint64_t min_samplerate(const struct dev_context *devc)
{
	/* Same limit as the official software (16 bit PIO divider). */
	return (devc->max_freq * 2) / 65535 + 1;
}

static struct sr_dev_inst *probe(struct sr_serial_dev_inst *serial)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	char *name;
	int i;

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	devc = g_malloc0(sizeof(*devc));
	if (la_identify(serial, devc) != SR_OK) {
		/* One retry: the first bytes after opening can get lost. */
		g_usleep(200 * 1000);
		if (la_identify(serial, devc) != SR_OK) {
			serial_close(serial);
			g_free(devc->board);
			g_free(devc);
			return NULL;
		}
	}
	serial_close(serial);

	devc->cur_samplerate = MIN(SR_MHZ(1), devc->max_freq);
	devc->limit_samples = MIN(10000, devc->buffer_size / 4);
	devc->capture_ratio = 10;

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup("gusmanb");
	sdi->model = g_strdup_printf("LogicAnalyzer %s", devc->board);
	sdi->version = g_strdup_printf("V%d_%d", devc->fw_major,
		devc->fw_minor);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;
	sdi->connection_id = g_strdup(serial->port);
	sdi->priv = devc;

	for (i = 0; i < devc->num_channels; i++) {
		name = g_strdup_printf("CH%d", i + 1);
		sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, name);
		g_free(name);
	}

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	struct sr_dev_inst *sdi;
	const char *conn, *serialcomm;
	GSList *l, *ports, *devices;

	conn = NULL;
	serialcomm = LA_SERIALCOMM;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (conn)
		ports = g_slist_append(NULL, g_strdup(conn));
	else
		ports = sr_serial_find_usb(LA_USB_VID, LA_USB_PID);

	devices = NULL;
	for (l = ports; l; l = l->next) {
		serial = sr_serial_dev_inst_new(l->data, serialcomm);
		sdi = probe(serial);
		if (sdi)
			devices = g_slist_append(devices, sdi);
		else
			sr_serial_dev_inst_free(serial);
	}
	g_slist_free_full(ports, g_free);

	return std_scan_complete(di, devices);
}

static void clear_helper(struct dev_context *devc)
{
	g_free(devc->board);
	g_free(devc->out_buf);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di,
		(std_dev_clear_callback)clear_helper);
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CONN:
		serial = sdi->conn;
		*data = g_variant_new_string(serial->port);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static gboolean samplerate_valid(const struct dev_context *devc, uint64_t sr)
{
	if (devc->blast_freq && sr == devc->blast_freq)
		return TRUE;

	return sr >= min_samplerate(devc) && sr <= devc->max_freq;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t v;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		v = g_variant_get_uint64(data);
		if (!samplerate_valid(devc, v)) {
			sr_err("Unsupported samplerate %" PRIu64 " Hz.", v);
			return SR_ERR_ARG;
		}
		devc->cur_samplerate = v;
		break;
	case SR_CONF_LIMIT_SAMPLES:
		v = g_variant_get_uint64(data);
		if (v < 2 || v > devc->buffer_size)
			return SR_ERR_ARG;
		devc->limit_samples = v;
		break;
	case SR_CONF_CAPTURE_RATIO:
		devc->capture_ratio = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t rates[G_N_ELEMENTS(base_samplerates) + 2];
	unsigned int i, n;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts,
			devopts);
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_SAMPLERATE:
		if (!sdi)
			return SR_ERR_ARG;
		devc = sdi->priv;
		n = 0;
		for (i = 0; i < G_N_ELEMENTS(base_samplerates); i++) {
			if (base_samplerates[i] >= min_samplerate(devc) &&
			    base_samplerates[i] < devc->max_freq)
				rates[n++] = base_samplerates[i];
		}
		rates[n++] = devc->max_freq;
		if (devc->blast_freq > devc->max_freq)
			rates[n++] = devc->blast_freq;
		*data = std_gvar_samplerates(rates, n);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		if (!sdi)
			return SR_ERR_ARG;
		devc = sdi->priv;
		/* Real maximum depends on the highest enabled channel:
		 * buffer / 1 (CH1-8), / 2 (CH9-16) or / 4 (CH17-24). */
		*data = std_gvar_tuple_u64(2, devc->buffer_size);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

/*
 * Translate the sigrok trigger into a firmware request.
 * Returns SR_OK and fills req->trigger_* fields; *has_trigger tells whether
 * a real trigger condition was configured.
 */
static int setup_trigger(const struct sr_dev_inst *sdi,
	struct la_capture_request *req, gboolean blast, gboolean *has_trigger)
{
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	GSList *l;
	int idx[LA_MAX_CHANNELS], val[LA_MAX_CHANNELS], types[LA_MAX_CHANNELS];
	int n, i, lo, hi;
	uint16_t pattern;

	*has_trigger = FALSE;
	n = 0;

	trigger = sr_session_trigger_get(sdi->session);
	if (trigger && trigger->stages) {
		if (g_slist_length(trigger->stages) > 1) {
			sr_err("Only a single trigger stage is supported.");
			return SR_ERR_NA;
		}
		stage = trigger->stages->data;
		for (l = stage->matches; l; l = l->next) {
			match = l->data;
			if (!match->match)
				continue;
			if (n >= LA_MAX_CHANNELS)
				return SR_ERR_ARG;
			idx[n] = match->channel->index;
			types[n] = match->match;
			val[n] = (match->match == SR_TRIGGER_ONE ||
				match->match == SR_TRIGGER_RISING) ? 1 : 0;
			n++;
		}
	}

	if (n == 0) {
		/*
		 * No trigger: trigger on "low level" of the external trigger
		 * input (GPIO1), which is pulled down and therefore fires
		 * immediately as long as nothing drives it high.
		 */
		req->trigger_type = blast ? LA_TRIG_BLAST : LA_TRIG_EDGE;
		req->trigger_channel = LA_EXT_TRIGGER_CHANNEL;
		req->inverted_or_count = 1;
		return SR_OK;
	}

	*has_trigger = TRUE;

	if (n == 1) {
		/*
		 * The firmware triggers on a level: "positive" fires as soon as
		 * the pin is high, "negative" as soon as it is low.
		 */
		req->trigger_type = blast ? LA_TRIG_BLAST : LA_TRIG_EDGE;
		req->trigger_channel = idx[0];
		req->inverted_or_count = val[0] ? 0 : 1;
		return SR_OK;
	}

	/* Pattern trigger over several channels. */
	if (blast) {
		sr_err("Pattern triggers are not available at the blast "
			"samplerate; use a single channel trigger.");
		return SR_ERR_NA;
	}

	lo = hi = idx[0];
	for (i = 0; i < n; i++) {
		if (types[i] != SR_TRIGGER_ZERO && types[i] != SR_TRIGGER_ONE) {
			sr_err("Pattern triggers only support 0/1 levels.");
			return SR_ERR_NA;
		}
		lo = MIN(lo, idx[i]);
		hi = MAX(hi, idx[i]);
	}
	if (hi > 15) {
		sr_err("Pattern triggers can only use CH1-CH16.");
		return SR_ERR_NA;
	}
	if (hi - lo + 1 != n) {
		sr_err("Pattern trigger channels must be contiguous "
			"(every channel between CH%d and CH%d needs a "
			"0/1 condition).", lo + 1, hi + 1);
		return SR_ERR_NA;
	}

	pattern = 0;
	for (i = 0; i < n; i++)
		if (val[i])
			pattern |= 1 << (idx[i] - lo);

	req->trigger_type = (n <= 5) ? LA_TRIG_FAST : LA_TRIG_COMPLEX;
	req->trigger_channel = lo;
	req->inverted_or_count = n;
	req->trigger_value = pattern;

	sr_info("Pattern trigger (%s) on CH%d-CH%d, value 0x%04x. "
		"Requires GPIO0 connected to GPIO1.",
		n <= 5 ? "fast" : "complex", lo + 1, hi + 1, pattern);

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct sr_channel *ch;
	struct la_capture_request req;
	uint8_t payload[LA_CAPTURE_REQUEST_SIZE];
	char line[64];
	uint8_t c;
	GSList *l;
	gboolean blast, has_trigger;
	uint32_t max_samples, limit, pre, post, max_pre, offset;
	int ret, highest, len, i;
	double delay_ns, period_ns;

	devc = sdi->priv;
	serial = sdi->conn;

	memset(&req, 0, sizeof(req));

	/* Enabled channels, in ascending order. */
	devc->chan_count = 0;
	highest = -1;
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (!ch->enabled)
			continue;
		req.channels[devc->chan_count] = ch->index;
		devc->chan_map[devc->chan_count] = ch->index;
		devc->chan_count++;
		highest = MAX(highest, ch->index);
	}
	if (!devc->chan_count) {
		sr_err("No channels enabled.");
		return SR_ERR;
	}
	req.channel_count = devc->chan_count;

	if (highest < 8) {
		req.capture_mode = LA_MODE_8CH;
		devc->bytes_per_sample = 1;
	} else if (highest < 16) {
		req.capture_mode = LA_MODE_16CH;
		devc->bytes_per_sample = 2;
	} else {
		req.capture_mode = LA_MODE_24CH;
		devc->bytes_per_sample = 4;
	}
	max_samples = devc->buffer_size / devc->bytes_per_sample;

	blast = devc->blast_freq && devc->cur_samplerate > devc->max_freq;
	req.frequency = devc->cur_samplerate;

	ret = setup_trigger(sdi, &req, blast, &has_trigger);
	if (ret != SR_OK)
		return ret;

	limit = devc->limit_samples;
	devc->skip_samples = 0;

	if (blast) {
		/* Blast mode: no pre-trigger samples. */
		if (limit > max_samples)
			limit = max_samples;
		pre = 0;
		post = limit;
	} else if (!has_trigger) {
		/*
		 * Immediate capture: request two (meaningless) pre-trigger
		 * samples and drop them.
		 */
		if (limit > max_samples - 2)
			limit = max_samples - 2;
		pre = 2;
		post = limit;
		devc->skip_samples = 2;
	} else {
		if (limit > max_samples)
			limit = max_samples;
		pre = (uint64_t)limit * devc->capture_ratio / 100;
		/* Firmware/official software limit for pre-trigger samples. */
		max_pre = max_samples / 10;
		if (pre > max_pre) {
			sr_warn("Pre-trigger limited to %u samples (10%% of "
				"buffer).", max_pre);
			pre = max_pre;
		}
		if (pre < 2)
			pre = 2;
		if (limit < pre + 2)
			limit = pre + 2;
		post = limit - pre;
	}

	if (limit != devc->limit_samples)
		sr_warn("Sample count limited to %u (buffer of %u samples "
			"with the enabled channels).", limit, max_samples);

	devc->trigger_pos = pre - devc->skip_samples;
	devc->has_trigger = has_trigger;
	devc->expected_samples = pre + post;

	req.pre_samples = pre;
	req.post_samples = post;

	/* Pattern triggers fire a few cycles late; compensate like the
	 * official software does. */
	if (req.trigger_type == LA_TRIG_COMPLEX ||
	    req.trigger_type == LA_TRIG_FAST) {
		period_ns = 1e9 / (double)devc->cur_samplerate;
		delay_ns = 1e9 / (double)devc->max_freq *
			(req.trigger_type == LA_TRIG_FAST ?
			LA_FAST_TRIGGER_DELAY : LA_COMPLEX_TRIGGER_DELAY);
		offset = (uint32_t)round(delay_ns / period_ns + 0.3);
		if (offset < post - 1) {
			req.pre_samples += offset;
			req.post_samples -= offset;
		}
	}

	sr_dbg("Capture: type %d, trig ch %d, inv/count %d, value 0x%x, "
		"%d channels, mode %d, %u Hz, pre %u, post %u.",
		req.trigger_type, req.trigger_channel, req.inverted_or_count,
		req.trigger_value, req.channel_count, req.capture_mode,
		req.frequency, req.pre_samples, req.post_samples);

	/* Reset reception state. */
	devc->len_pos = 0;
	devc->partial_pos = 0;
	devc->samples_rx = 0;
	devc->samples_sent = 0;
	devc->total_samples = 0;
	devc->stamp_bytes_left = 0;
	devc->out_len = 0;
	if (!devc->out_buf) {
		devc->out_cap = 64 * 1024;
		devc->out_buf = g_malloc(devc->out_cap * 3);
	}

	serial_flush(serial);
	la_drain(serial, 20, 200);

	la_pack_request(&req, payload);
	if (la_send_command(serial, LA_CMD_CAPTURE, payload,
			sizeof(payload)) != SR_OK)
		return SR_ERR_IO;

	for (i = 0; i < 3; i++) {
		len = la_read_line(serial, line, sizeof(line), 5000);
		if (len <= 0)
			continue;
		if (!strcmp(line, "CAPTURE_STARTED"))
			break;
		if (!strcmp(line, "CAPTURE_ERROR")) {
			sr_err("Device refused the capture settings.");
			return SR_ERR;
		}
		sr_dbg("Ignoring line '%s'.", line);
	}
	if (i == 3) {
		sr_err("No answer from the device to the capture request.");
		return SR_ERR_IO;
	}

	/*
	 * The reply ends in "\r\n" and la_read_line() stops at the '\r'.
	 * Swallow the '\n' so it is not taken as the first byte of the
	 * binary length. The firmware waits >= 100 ms before sending the
	 * length, so anything arriving right now is the line terminator.
	 */
	if (serial_read_blocking(serial, &c, 1, 50) == 1 &&
	    c != '\n' && c != '\r') {
		devc->len_buf[0] = c;
		devc->len_pos = 1;
	}

	devc->rx_state = LA_RX_LENGTH;

	std_session_send_df_header(sdi);

	serial_source_add(sdi->session, serial, G_IO_IN, 100,
		la_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	devc = sdi->priv;
	serial = sdi->conn;

	serial_source_remove(sdi->session, serial);

	/* Still waiting for the trigger or the data: cancel it. */
	if (devc->rx_state != LA_RX_DONE && devc->rx_state != LA_RX_IDLE) {
		if (devc->rx_state == LA_RX_LENGTH) {
			sr_info("Cancelling capture.");
			la_abort_capture(sdi);
		} else {
			/* Data already flowing; just discard the rest. */
			la_drain(serial, 300, 10000);
		}
	}
	devc->rx_state = LA_RX_IDLE;

	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver gusmanb_logicanalyzer_driver_info = {
	.name = "gusmanb-logicanalyzer",
	.longname = "gusmanb LogicAnalyzer (RP2040/RP2350)",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(gusmanb_logicanalyzer_driver_info);
