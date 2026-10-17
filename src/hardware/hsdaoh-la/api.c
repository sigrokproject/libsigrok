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
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "protocol.h"

/* the Pico's USB CDC port: baud rate does not matter, DTR must be set or
 * the firmware does not send its answers */
#define SERIALCOMM "115200/8n1/dtr=1/rts=0/flow=0"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,		/* optional: the Pico's serial port, to set the rate */
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

static struct sr_dev_driver hsdaoh_la_driver_info;

static int cmp_u64(const void *a, const void *b)
{
	uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
	return (x > y) - (x < y);
}

/*
 * The firmware only has integer dividers: rate = sys_clock / div.
 * Offer the fastest few dividers plus the ones closest to a 1-2-5 series.
 */
static void build_samplerate_list(struct dev_context *devc)
{
	static const unsigned int steps[] = { 1, 2, 5 };
	uint64_t *list;
	unsigned int n = 0, max = 128;

	g_free(devc->samplerate_list);
	devc->samplerate_list = NULL;
	devc->num_samplerates = 0;
	if (!devc->sys_clock || !devc->min_div)
		return;

	list = g_malloc0(max * sizeof(uint64_t));
	for (unsigned int d = devc->min_div; d < devc->min_div + 6 && d <= HSDAOH_LA_MAX_DIVIDER; d++)
		list[n++] = hsdaoh_la_rate(devc->sys_clock, d);

	for (uint64_t decade = 1000; decade <= devc->sys_clock && n < max; decade *= 10) {
		for (unsigned int i = 0; i < G_N_ELEMENTS(steps) && n < max; i++) {
			uint64_t target = decade * steps[i];
			uint64_t d = (devc->sys_clock + target / 2) / target;
			if (d < devc->min_div || d > HSDAOH_LA_MAX_DIVIDER)
				continue;
			list[n++] = hsdaoh_la_rate(devc->sys_clock, d);
		}
	}

	qsort(list, n, sizeof(uint64_t), cmp_u64);
	unsigned int u = 0;
	for (unsigned int i = 0; i < n; i++)
		if (!u || list[i] != list[u - 1])
			list[u++] = list[i];

	devc->samplerate_list = list;
	devc->num_samplerates = u;
}

static void clear_helper(struct dev_context *devc)
{
	if (devc->serial)
		sr_serial_dev_inst_free(devc->serial);
	g_free(devc->samplerate_list);
	g_free(devc->trig_tbl);
	g_free(devc->buf);
	g_free(devc->hist);
	g_free(devc->unpack);
	g_mutex_clear(&devc->mutex);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, (std_dev_clear_callback)clear_helper);
}

/* USB IDs of the Pico SDK's USB serial port (stdio_usb) */
#define PICO_USB_VID		0x2e8a
#define PICO_USB_PID_RP2350	0x0009
#define PICO_USB_PID_RP2040	0x000a

/* ask the la8 firmware on 'port' for its status; TRUE if it answered */
static gboolean probe_pico(struct dev_context *devc, const char *port, const char *serialcomm)
{
	char reply[1024];

	devc->serial = sr_serial_dev_inst_new(port, serialcomm ? serialcomm : SERIALCOMM);
	if (hsdaoh_la_pico_command(devc, "?", reply, sizeof(reply)) == SR_OK &&
	    strstr(reply, "la8") && hsdaoh_la_pico_parse_status(devc, reply) == SR_OK) {
		sr_info("la8 on %s: system clock %" PRIu64 " Hz, divider %u (minimum %u).",
			port, devc->sys_clock, devc->cur_div, devc->min_div);
		build_samplerate_list(devc);
		return TRUE;
	}
	sr_serial_dev_inst_free(devc->serial);
	devc->serial = NULL;
	return FALSE;
}

static gboolean in_list(GSList *l, const char *name)
{
	for (; l; l = l->next)
		if (!g_ascii_strcasecmp(l->data, name))
			return TRUE;
	return FALSE;
}

/* the Windows name of a generic USB CDC serial port, e.g. a Pico's */
static gboolean generic_usb_cdc(const struct sr_serial_port *p)
{
	return (p->description && strstr(p->description, "USB Serial Device")) ||
		(p->name && strstr(p->name, "ttyACM"));
}

/*
 * No serial port given (e.g. PulseView's automatic scan at startup, or
 * "Connect to Device" with USB): look for the Pico's serial port, so the
 * sample rate can still be changed. Tried in this order:
 *  1. HSDAOH_LA_PORT environment variable (e.g. COM21)
 *  2. ports with the Pico SDK's USB IDs
 *  3. other generic USB CDC ports ("USB Serial Device (COMx)" on Windows,
 *     /dev/ttyACMx on Linux), for when the USB IDs cannot be read
 * Only the la8 status command "?" is sent.
 */
static char *find_pico(struct dev_context *devc, const char *serialcomm)
{
	static const uint16_t pids[] = { PICO_USB_PID_RP2350, PICO_USB_PID_RP2040 };
	GSList *tried = NULL, *ports, *l;
	const char *env;
	char *found = NULL;

	env = g_getenv("HSDAOH_LA_PORT");
	if (env && *env) {
		sr_info("Trying %s (HSDAOH_LA_PORT).", env);
		if (probe_pico(devc, env, serialcomm))
			found = g_strdup(env);
		tried = g_slist_append(tried, g_strdup(env));
	}

	for (unsigned int k = 0; k < G_N_ELEMENTS(pids) && !found; k++) {
		ports = sr_serial_find_usb(PICO_USB_VID, pids[k]);
		for (l = ports; l && !found; l = l->next) {
			if (in_list(tried, l->data))
				continue;
			sr_info("Trying Pico serial port %s.", (const char *)l->data);
			if (probe_pico(devc, l->data, serialcomm))
				found = g_strdup(l->data);
			tried = g_slist_append(tried, g_strdup(l->data));
		}
		g_slist_free_full(ports, g_free);
	}

	if (!found) {
		ports = sr_serial_list(NULL);
		for (l = ports; l && !found; l = l->next) {
			struct sr_serial_port *p = l->data;
			if (!generic_usb_cdc(p) || in_list(tried, p->name))
				continue;
			sr_info("Trying USB serial port %s (%s).", p->name,
				p->description ? p->description : "");
			if (probe_pico(devc, p->name, serialcomm))
				found = g_strdup(p->name);
			tried = g_slist_append(tried, g_strdup(p->name));
		}
		g_slist_free_full(ports, (GDestroyNotify)sr_serial_free);
	}

	if (!found)
		sr_warn("No la8 serial port found (tried %u). The sample rate cannot "
			"be changed: close other programs using the Pico's COM port, "
			"or connect with the Pico's serial port.", g_slist_length(tried));
	g_slist_free_full(tried, g_free);
	return found;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_config *src;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	GSList *l, *devices = NULL;
	const char *conn = NULL, *serialcomm = NULL;
	uint32_t count;
	char *port = NULL;

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

	count = hsdaoh_get_device_count();
	sr_dbg("%u hsdaoh device(s) found.", count);

	for (uint32_t i = 0; i < count; i++) {
		devc = g_malloc0(sizeof(struct dev_context));
		g_mutex_init(&devc->mutex);
		devc->hsdaoh_index = i;
		devc->samplerate = HSDAOH_LA_DEFAULT_SAMPLERATE;
		devc->limit_samples = SR_MHZ(1);
		devc->capture_ratio = 10;
		devc->state = HL_IDLE;

		/* the serial port belongs to the Pico on the first stick */
		if (i == 0) {
			if (conn) {
				if (probe_pico(devc, conn, serialcomm))
					port = g_strdup(conn);
				else
					sr_warn("No la8 status from %s, the sample rate cannot be changed.", conn);
			} else {
				port = find_pico(devc, serialcomm);
			}
		}

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup("hsdaoh");
		sdi->model = g_strdup("la8 (RP2350 + MS2130)");
		sdi->inst_type = SR_INST_USER;
		sdi->connection_id = devc->serial ?
			g_strdup_printf("hsdaoh/%u + %s", i, port) :
			g_strdup_printf("hsdaoh/%u", i);
		sdi->driver = &hsdaoh_la_driver_info;
		sdi->priv = devc;

		for (int ch = 0; ch < HSDAOH_LA_NUM_CHANNELS; ch++) {
			char name[8];
			snprintf(name, sizeof(name), "D%d", ch);
			sr_channel_new(sdi, ch, SR_CHANNEL_LOGIC, TRUE, name);
		}

		devices = g_slist_append(devices, sdi);
	}

	g_free(port);
	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (hsdaoh_open(&devc->hsdaoh, devc->hsdaoh_index) < 0) {
		sr_err("Failed to open hsdaoh device #%u.", devc->hsdaoh_index);
		devc->hsdaoh = NULL;
		return SR_ERR;
	}
	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->hsdaoh) {
		if (devc->streaming)
			hsdaoh_stop_stream(devc->hsdaoh);
		devc->streaming = FALSE;
		hsdaoh_close(devc->hsdaoh);
		devc->hsdaoh = NULL;
	}
	return SR_OK;
}

static int set_samplerate(struct dev_context *devc, uint64_t rate)
{
	char cmd[32], reply[1024];
	uint64_t div;
	int ret;

	if (!rate)
		return SR_ERR_ARG;

	if (!devc->serial || !devc->sys_clock) {
		if (rate == devc->samplerate)
			return SR_OK;
		sr_err("The sample rate can only be changed with the Pico's serial "
			"port given as connection (conn=COMx or /dev/ttyACMx).");
		return SR_ERR_NA;
	}

	div = (devc->sys_clock + rate / 2) / rate;
	if (div < devc->min_div)
		div = devc->min_div;
	if (div > HSDAOH_LA_MAX_DIVIDER)
		div = HSDAOH_LA_MAX_DIVIDER;

	snprintf(cmd, sizeof(cmd), "div %u", (unsigned int)div);
	ret = hsdaoh_la_pico_command(devc, cmd, reply, sizeof(reply));
	if (ret == SR_ERR_ARG) {
		sr_err("la8 refused '%s': %s", cmd, reply);
		return SR_ERR_ARG;
	}
	if (ret != SR_OK)
		return ret;
	if (hsdaoh_la_pico_parse_status(devc, reply) != SR_OK) {
		devc->cur_div = (unsigned int)div;
		devc->samplerate = hsdaoh_la_rate(devc->sys_clock, div);
	}
	/* the stream may be running: ignore the next buffers, they can still
	 * contain samples taken at the old rate */
	g_mutex_lock(&devc->mutex);
	devc->skip_bufs = 3;
	g_mutex_unlock(&devc->mutex);
	sr_info("Sample rate %" PRIu64 " Hz (divider %u).", devc->samplerate, devc->cur_div);
	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CONN:
		if (!devc->serial)
			return SR_ERR_NA;
		*data = g_variant_new_string(devc->serial->port);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->samplerate);
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
		return set_samplerate(devc, g_variant_get_uint64(data));
	case SR_CONF_LIMIT_SAMPLES:
		v = g_variant_get_uint64(data);
		if (!v || v > HSDAOH_LA_MAX_SAMPLES) {
			sr_err("Sample count must be 1..%" PRIu64 ".",
				(uint64_t)HSDAOH_LA_MAX_SAMPLES);
			return SR_ERR_ARG;
		}
		devc->limit_samples = v;
		break;
	case SR_CONF_CAPTURE_RATIO:
		v = g_variant_get_uint64(data);
		if (v > 100)
			return SR_ERR_ARG;
		devc->capture_ratio = v;
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

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		if (!sdi)
			return SR_ERR_ARG;
		devc = sdi->priv;
		if (devc->num_samplerates)
			*data = std_gvar_samplerates(devc->samplerate_list, devc->num_samplerates);
		else
			*data = std_gvar_samplerates(&devc->samplerate, 1);
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	case SR_CONF_LIMIT_SAMPLES:
		/* one byte per sample in RAM on the PC */
		*data = std_gvar_tuple_u64(1, HSDAOH_LA_MAX_SAMPLES);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_trigger *trigger;
	int ret;

	/* a failed reopen in an earlier start left no handle: try again */
	if (!devc->hsdaoh && hsdaoh_open(&devc->hsdaoh, devc->hsdaoh_index) < 0) {
		devc->hsdaoh = NULL;
		sr_err("Failed to open hsdaoh device #%u.", devc->hsdaoh_index);
		return SR_ERR_DEV_CLOSED;
	}

	trigger = sr_session_trigger_get(sdi->session);
	if ((ret = hsdaoh_la_build_trigger(devc, trigger)) != SR_OK)
		return ret;

	devc->pre = devc->have_trigger ? devc->limit_samples * devc->capture_ratio / 100 : 0;
	devc->post = devc->limit_samples - devc->pre;
	if (!devc->post) {
		devc->post = 1;
		devc->pre = devc->limit_samples - 1;
	}

	/* gsize is 32 bits in a 32-bit build: never let the size wrap */
	if (devc->limit_samples > HSDAOH_LA_MAX_SAMPLES ||
	    devc->pre + devc->post > G_MAXSIZE) {
		sr_err("Sample count %" PRIu64 " is too large.", devc->limit_samples);
		return SR_ERR_ARG;
	}

	g_free(devc->buf);
	g_free(devc->hist);
	devc->hist = NULL;
	devc->buf = g_try_malloc((gsize)(devc->pre + devc->post));
	if (devc->pre)
		devc->hist = g_try_malloc((gsize)devc->pre);
	if (!devc->buf || (devc->pre && !devc->hist)) {
		sr_err("Not enough memory for %" PRIu64 " samples.", devc->limit_samples);
		g_free(devc->buf);
		devc->buf = NULL;
		return SR_ERR_MALLOC;
	}

	g_mutex_lock(&devc->mutex);
	hsdaoh_la_reset_capture(devc);
	devc->stream_srate = 0;
	g_mutex_unlock(&devc->mutex);

	std_session_send_df_header(sdi);
	sr_session_source_add(sdi->session, -1, 0, 50,
		hsdaoh_la_receive_data, (void *)sdi);

	/* the stream keeps running between acquisitions; restart it only
	 * if libhsdaoh reported an error since */
	if (devc->streaming && devc->stream_error) {
		sr_warn("The HDMI stream had an error, restarting it.");
		hsdaoh_stop_stream(devc->hsdaoh);
		devc->streaming = FALSE;
	}
	if (devc->streaming) {
		sr_info("Waiting for %s, then %" PRIu64 " + %" PRIu64 " samples.",
			devc->have_trigger ? "the trigger" : "data", devc->pre, devc->post);
		return SR_OK;
	}

	devc->stream_error = FALSE;
	ret = hsdaoh_start_stream(devc->hsdaoh, hsdaoh_la_data_cb, (void *)sdi, 0);
	if (ret < 0) {
		/*
		 * libhsdaoh before e7509c2 (Oct 2026) cannot start a stream
		 * again on a handle whose stream was stopped (returns -2):
		 * the second Run in PulseView failed. Reopen the device and
		 * try once more; this also recovers from other start errors.
		 */
		sr_warn("Starting the hsdaoh stream failed (%d), reopening the device.", ret);
		hsdaoh_close(devc->hsdaoh);
		devc->hsdaoh = NULL;
		if (hsdaoh_open(&devc->hsdaoh, devc->hsdaoh_index) < 0) {
			devc->hsdaoh = NULL;
			ret = -1;
		} else {
			ret = hsdaoh_start_stream(devc->hsdaoh, hsdaoh_la_data_cb, (void *)sdi, 0);
		}
	}
	if (ret < 0) {
		sr_err("Failed to start the hsdaoh stream (%d).", ret);
		devc->state = HL_FAILED;
		hsdaoh_la_end_acquisition(sdi, FALSE);
		return SR_ERR;
	}
	devc->streaming = TRUE;

	sr_info("Waiting for %s, then %" PRIu64 " + %" PRIu64 " samples.",
		devc->have_trigger ? "the trigger" : "data", devc->pre, devc->post);
	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->state == HL_IDLE)
		return SR_OK;
	hsdaoh_la_end_acquisition(sdi, TRUE);
	return SR_OK;
}

static struct sr_dev_driver hsdaoh_la_driver_info = {
	.name = "hsdaoh-la",
	.longname = "hsdaoh la8 (RP2350 + MS2130 HDMI capture)",
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
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(hsdaoh_la_driver_info);
