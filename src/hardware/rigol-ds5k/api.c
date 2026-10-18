/*
 * Rigol DS5000 series driver (DS5102MA).
 *
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 audiossis <audiossis@users.noreply.github.com>
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
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <libusb.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define NUM_CHANNELS DS5K_NUM_CHANNELS
#define NUM_VDIV_STEPS DS5K_NUM_VDIV_STEPS
#define NUM_COUPLING 3
#define NUM_TIMEBASES 33
#define NUM_PROBES 2

/*
 * Poll interval between frames during rolling acquisition, in ms. A frame
 * (state refresh + download) costs only a few tens of ms, so a short
 * interval yields a smooth rolling display. The poll callback runs
 * synchronously on the session main loop, so frames never overlap.
 */
#define DS5K_POLL_INTERVAL_MS 50

/*
 * Physical volts/div steps of the DS5000 front end. The scope reports and
 * accepts ":CHANNEL<n>:RANGE:" values scaled by the probe attenuation, so
 * the exposed V/div list is this table multiplied by the probe factor.
 */
static const uint64_t vdivs_base[NUM_VDIV_STEPS][2] = {
	{ 20, 1000 }, { 50, 1000 }, { 100, 1000 }, { 200, 1000 },
	{ 500, 1000 }, { 1, 1 }, { 2, 1 }, { 5, 1 },
};

static const char *coupling[] = { "AC", "DC", "GND" };

static const uint64_t probes[NUM_PROBES] = { 1, 10 };

static const uint64_t timebases[NUM_TIMEBASES][2] = {
	{ 1, 1000000000 }, { 2, 1000000000 }, { 5, 1000000000 },
	{ 10, 1000000000 }, { 20, 1000000000 }, { 50, 1000000000 },
	{ 100, 1000000000 }, { 200, 1000000000 }, { 500, 1000000000 },
	{ 1, 1000000 }, { 2, 1000000 }, { 5, 1000000 },
	{ 10, 1000000 }, { 20, 1000000 }, { 50, 1000000 },
	{ 100, 1000000 }, { 200, 1000000 }, { 500, 1000000 },
	{ 1, 1000 }, { 2, 1000 }, { 5, 1000 },
	{ 10, 1000 }, { 20, 1000 }, { 50, 1000 },
	{ 100, 1000 }, { 200, 1000 }, { 500, 1000 },
	{ 1, 1 }, { 2, 1 }, { 5, 1 },
	{ 10, 1 }, { 20, 1 }, { 50, 1 },
};

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	/*
	 * Capability marker: the driver can keep producing frames until it is
	 * told to stop, which is what sigrok-cli --continuous and PulseView's
	 * rolling display key off. Without this both frontends treat the
	 * device as single-shot only.
	 */
	SR_CONF_CONTINUOUS,
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TIMEBASE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_NUM_HDIV | SR_CONF_GET,
};

static const uint32_t devopts_cg[] = {
	SR_CONF_NUM_VDIV | SR_CONF_GET,
	SR_CONF_VDIV | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_PROBE_FACTOR | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static struct sr_dev_inst *probe_device(struct sr_usb_dev_inst *usb)
{
	struct sr_dev_inst *sdi;
	struct rigol_ds5k_state *state;
	struct sr_channel_group *cg;
	struct sr_channel *ch;
	int i;

	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	sdi->vendor = g_strdup("Rigol");
	sdi->model = g_strdup("DS5000 series");
	sdi->status = SR_ST_INACTIVE;
	sdi->inst_type = SR_INST_USB;
	sdi->conn = usb;

	state = g_malloc0(sizeof(struct rigol_ds5k_state));
	for (i = 0; i < NUM_CHANNELS; i++) {
		state->probe[i] = 1;
		state->vdiv_idx[i] = 4;	/* 500 mV/div */
		state->coupling[i] = 0;	/* AC */
	}
	sdi->priv = state;

	for (i = 0; i < NUM_CHANNELS; i++) {
		/*
		 * sr_channel_new() already appends the channel to
		 * sdi->channels; do not append it a second time here.
		 */
		ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE,
			i == 0 ? "CH1" : "CH2");
		cg = sr_channel_group_new(sdi, i == 0 ? "CH1" : "CH2", NULL);
		cg->channels = g_slist_append(cg->channels, ch);
	}

	return sdi;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct sr_dev_inst *sdi;
	struct sr_usb_dev_inst *usb;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	GSList *devices = NULL;
	int i, bus, addr;

	(void)options;
	drvc = di->context;

	if (libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist) < 0)
		return std_scan_complete(di, NULL);

	for (i = 0; devlist[i]; i++) {
		if (libusb_get_device_descriptor(devlist[i], &des) < 0)
			continue;
		if (des.idVendor != DS5K_VID || des.idProduct != DS5K_PID)
			continue;
		bus = libusb_get_bus_number(devlist[i]);
		addr = libusb_get_device_address(devlist[i]);
		usb = sr_usb_dev_inst_new(bus, addr, NULL);
		sdi = probe_device(usb);
		devices = g_slist_append(devices, sdi);
	}

	libusb_free_device_list(devlist, 1);

	return std_scan_complete(di, devices);
}

/*---- Scope state helpers ----------------------------------------------*/

/*
 * A reply can still contain junk (e.g. a leftover test pattern that
 * failed to strip), so only accept an unambiguous probe factor and
 * otherwise report "unknown" (-1). Callers then leave the cached
 * value untouched instead of corrupting the channel scaling.
 */
static int ds5k_query_probe(struct sr_usb_dev_inst *usb, int ch)
{
	char cmd[32], buf[32];
	int probe;

	snprintf(cmd, sizeof(cmd), ":CHANNEL%d:PROBE:?", ch + 1);
	if (rigol_ds5k_query(usb, cmd, buf, sizeof(buf)) != SR_OK)
		return -1;
	probe = atoi(buf);
	if (probe != 1 && probe != 10 && probe != 100 && probe != 1000)
		return -1;
	return probe;
}

/* Report the coupling, or -1 if the reply was not one we know. */
static int ds5k_query_coupling(struct sr_usb_dev_inst *usb, int ch)
{
	char cmd[32], buf[32];
	int i;

	snprintf(cmd, sizeof(cmd), ":CHANNEL%d:COUPLING:?", ch + 1);
	if (rigol_ds5k_query(usb, cmd, buf, sizeof(buf)) != SR_OK)
		return -1;
	for (i = 0; i < NUM_COUPLING; i++)
		if (!strcmp(buf, coupling[i]))
			return i;
	return -1;
}

/* Return the V/div (probe corrected) reported by the scope. */
static int ds5k_query_vdiv(struct sr_usb_dev_inst *usb, int ch, double *val)
{
	char cmd[32];

	snprintf(cmd, sizeof(cmd), ":CHANNEL%d:RANGE:?", ch + 1);
	return rigol_ds5k_get_double(usb, cmd, val);
}

/* Map a probe-referred V/div value to an index in vdivs_base. */
static int ds5k_vdiv_index(double val, int probe)
{
	int i;

	for (i = 0; i < NUM_VDIV_STEPS; i++) {
		double base = (double)vdivs_base[i][0] / vdivs_base[i][1];

		if (fabs(val - base * probe) < base * probe * 0.02)
			return i;
	}

	return -1;
}

/* Refresh the cached per-channel state from the scope. */
static void ds5k_refresh_state(struct sr_dev_inst *sdi)
{
	struct rigol_ds5k_state *state = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	int i, idx, p, c;
	double val;

	if (!usb || !usb->devhdl)
		return;

	for (i = 0; i < NUM_CHANNELS; i++) {
		if ((p = ds5k_query_probe(usb, i)) > 0)
			state->probe[i] = p;
		if ((c = ds5k_query_coupling(usb, i)) >= 0)
			state->coupling[i] = c;
		if (ds5k_query_vdiv(usb, i, &val) == SR_OK) {
			idx = ds5k_vdiv_index(val, state->probe[i]);
			if (idx >= 0)
				state->vdiv_idx[i] = idx;
		}
	}
}

static int ds5k_query_tdiv(struct sr_usb_dev_inst *usb, double *val)
{
	return rigol_ds5k_get_double(usb, ":TimeBase:RANGE:?", val);
}

/*
 * The scope delivers DS5K_WINDOW_SAMPLES samples across DS5K_HDIV divisions,
 * so the sample rate follows from the timebase.
 */
static uint64_t ds5k_samplerate_for(double tdiv)
{
	uint64_t sr;

	if (tdiv <= 0.0)
		return 0;

	sr = (uint64_t)(DS5K_WINDOW_SAMPLES / (DS5K_HDIV * tdiv) + 0.5);
	if (sr > DS5K_MAX_SAMPLERATE)
		sr = DS5K_MAX_SAMPLERATE;

	return sr;
}

/*
 * Return the (numerator, denominator) of a V/div table entry, scaled by the
 * probe factor. Uses millivolts for the sub-volt steps and volts for the
 * rest, so e.g. 1 V/div comes out as (1, 1) rather than (1000, 1000).
 */
static void ds5k_vdiv_tuple(int idx, int probe, uint64_t *num, uint64_t *den)
{
	uint64_t n = vdivs_base[idx][0] * (uint64_t)probe;
	uint64_t d = vdivs_base[idx][1];

	while (d >= 1000 && n >= 1000) {
		n /= 1000;
		d /= 1000;
	}

	*num = n;
	*den = d;
}

/*---- Configuration ----------------------------------------------------*/

/*
 * Hand the front panel back to the operator after talking to the scope while
 * no acquisition is running. The DS5000 drops into remote mode (panel locked,
 * "rmt" shown) as soon as it receives any command, and only leaves it when
 * ":KEY:LOCK:ENABLE" is the last thing on the wire; so any driver entry point
 * that touches the scope outside an acquisition ends with this. It is
 * idempotent, and it is skipped during an acquisition because the next
 * download re-asserts remote mode anyway.
 */
static void ds5k_quiesce(const struct sr_dev_inst *sdi)
{
	struct rigol_ds5k_state *state = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;

	if (!state || !usb || !usb->devhdl || state->acquiring)
		return;

	rigol_ds5k_release_panel(usb);
}

static int config_get_live(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct rigol_ds5k_state *state;
	struct sr_usb_dev_inst *usb;
	double val, tdiv;
	int ch_idx, idx, probe;

	switch (key) {
	case SR_CONF_NUM_HDIV:
		*data = g_variant_new_int32(DS5K_HDIV);
		return SR_OK;
	}

	if (!sdi)
		return SR_ERR_ARG;
	state = sdi->priv;
	usb = sdi->conn;

	if (!cg) {
		switch (key) {
		case SR_CONF_CONN:
			if (!usb)
				return SR_ERR_ARG;
			*data = g_variant_new_printf("%d.%d", usb->bus,
				usb->address);
			return SR_OK;
		case SR_CONF_SAMPLERATE:
			if (usb && usb->devhdl &&
			    ds5k_query_tdiv(usb, &tdiv) == SR_OK)
				*data = g_variant_new_uint64(
					ds5k_samplerate_for(tdiv));
			else
				*data = g_variant_new_uint64(400000);
			return SR_OK;
		case SR_CONF_LIMIT_SAMPLES:
			*data = g_variant_new_uint64(DS5K_WINDOW_SAMPLES);
			return SR_OK;
		case SR_CONF_LIMIT_FRAMES:
			*data = g_variant_new_uint64(state->limit_frames);
			return SR_OK;
		case SR_CONF_TIMEBASE:
			if (usb && usb->devhdl &&
			    ds5k_query_tdiv(usb, &tdiv) == SR_OK) {
				for (idx = 0; idx < NUM_TIMEBASES; idx++) {
					double t =
						(double)timebases[idx][0] /
						timebases[idx][1];
					if (fabs(t - tdiv) < t * 0.01) {
						*data = g_variant_new("(tt)",
							timebases[idx][0],
							timebases[idx][1]);
						return SR_OK;
					}
				}
			}
			*data = g_variant_new("(tt)", timebases[20][0],
				timebases[20][1]);
			return SR_OK;
		default:
			return SR_ERR_NA;
		}
	}

	if (sdi->channel_groups->data == cg)
		ch_idx = 0;
	else if (sdi->channel_groups->next &&
		sdi->channel_groups->next->data == cg)
		ch_idx = 1;
	else
		return SR_ERR_ARG;

	/*
	 * Report what the scope is actually set to. Talking to it locks the
	 * front panel for the moment, but the config_get() wrapper hands it
	 * straight back (ds5k_quiesce), so live reads are safe at any time.
	 */
	probe = state->probe[ch_idx];
	if (usb && usb->devhdl) {
		int p = ds5k_query_probe(usb, ch_idx);
		int c = ds5k_query_coupling(usb, ch_idx);

		if (p > 0)
			probe = state->probe[ch_idx] = p;
		if (c >= 0)
			state->coupling[ch_idx] = c;
	}

	switch (key) {
	case SR_CONF_NUM_VDIV:
		*data = g_variant_new_int32(DS5K_VDIV);
		return SR_OK;
	case SR_CONF_PROBE_FACTOR:
		*data = g_variant_new_uint64(probe);
		return SR_OK;
	case SR_CONF_VDIV:
		idx = state->vdiv_idx[ch_idx];
		if (usb && usb->devhdl &&
		    ds5k_query_vdiv(usb, ch_idx, &val) == SR_OK) {
			int i = ds5k_vdiv_index(val, probe);

			if (i >= 0) {
				idx = i;
				state->vdiv_idx[ch_idx] = i;
			}
		}
		if (idx < 0)
			idx = 4;
		{
			uint64_t n, d;

			ds5k_vdiv_tuple(idx, probe, &n, &d);
			*data = g_variant_new("(tt)", n, d);
		}
		return SR_OK;
	case SR_CONF_COUPLING:
		*data = g_variant_new_string(coupling[state->coupling[ch_idx]]);
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret = config_get_live(key, data, sdi, cg);

	if (sdi && sdi->priv)
		ds5k_quiesce(sdi);

	return ret;
}

static void ds5k_format_vdiv(char *buf, size_t len, double v)
{
	if (v >= 10.0)
		snprintf(buf, len, "%.1fV", v);
	else if (v >= 1.0)
		snprintf(buf, len, "%.2fV", v);
	else if (v >= 0.1)
		snprintf(buf, len, "%.0fmV", v * 1000.0);
	else
		snprintf(buf, len, "%.1fmV", v * 1000.0);
}

static void ds5k_format_tdiv(char *buf, size_t len, double t)
{
	if (t >= 1.0)
		snprintf(buf, len, "%.1fs", t);
	else if (t >= 1e-3)
		snprintf(buf, len, "%.1fms", t * 1e3);
	else if (t >= 1e-6)
		snprintf(buf, len, "%.1fus", t * 1e6);
	else
		snprintf(buf, len, "%.1fns", t * 1e9);
}

static int config_set_live(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct rigol_ds5k_state *state;
	struct sr_usb_dev_inst *usb;
	char cmd[64], tmp[32];
	uint64_t p, q, rate;
	double val;
	int ch_idx, i;

	if (!sdi)
		return SR_ERR_ARG;
	state = sdi->priv;
	usb = sdi->conn;

	if (!usb || !usb->devhdl)
		return SR_ERR;

	if (!cg) {
		switch (key) {
		case SR_CONF_LIMIT_FRAMES:
			state->limit_frames = g_variant_get_uint64(data);
			return SR_OK;
		case SR_CONF_TIMEBASE:
			g_variant_get(data, "(tt)", &p, &q);
			val = (double)p / q;
			ds5k_format_tdiv(tmp, sizeof(tmp), val);
			snprintf(cmd, sizeof(cmd), ":TimeBase:RANGE:%s",
				tmp);
			return rigol_ds5k_send(usb, cmd);
		case SR_CONF_SAMPLERATE:
			/*
			 * The sample rate is not set directly: it follows
			 * from the timebase. Pick the fastest timebase that
			 * produces the requested rate and set that.
			 */
			rate = g_variant_get_uint64(data);
			if (rate == 0)
				return SR_ERR_ARG;
			for (i = 0; i < NUM_TIMEBASES; i++) {
				val = (double)timebases[i][0] /
					timebases[i][1];
				if (ds5k_samplerate_for(val) == rate)
					break;
			}
			if (i == NUM_TIMEBASES)
				return SR_ERR_ARG;
			ds5k_format_tdiv(tmp, sizeof(tmp), val);
			snprintf(cmd, sizeof(cmd), ":TimeBase:RANGE:%s",
				tmp);
			return rigol_ds5k_send(usb, cmd);
		default:
			return SR_ERR_NA;
		}
	}

	if (sdi->channel_groups->data == cg)
		ch_idx = 0;
	else if (sdi->channel_groups->next &&
		sdi->channel_groups->next->data == cg)
		ch_idx = 1;
	else
		return SR_ERR_ARG;

	{
		int q = ds5k_query_probe(usb, ch_idx);

		if (q > 0)
			state->probe[ch_idx] = q;
	}

	switch (key) {
	case SR_CONF_COUPLING:
	{
		const char *s = g_variant_get_string(data, NULL);

		for (i = 0; i < NUM_COUPLING; i++)
			if (!strcmp(s, coupling[i]))
				break;
		if (i == NUM_COUPLING)
			return SR_ERR_ARG;
		snprintf(cmd, sizeof(cmd), ":CHANNEL%d:COUPLING:%s",
			ch_idx + 1, coupling[i]);
		if (rigol_ds5k_send(usb, cmd) != SR_OK)
			return SR_ERR;
		state->coupling[ch_idx] = i;
		return SR_OK;
	}
	case SR_CONF_PROBE_FACTOR:
		p = g_variant_get_uint64(data);
		for (i = 0; i < NUM_PROBES; i++)
			if (probes[i] == p)
				break;
		if (i == NUM_PROBES)
			return SR_ERR_ARG;
		snprintf(cmd, sizeof(cmd), ":CHANNEL%d:PROBE:%lluX",
			ch_idx + 1, (unsigned long long)p);
		if (rigol_ds5k_send(usb, cmd) != SR_OK)
			return SR_ERR;
		state->probe[ch_idx] = (int)p;
		return SR_OK;
	case SR_CONF_VDIV:
		g_variant_get(data, "(tt)", &p, &q);
		val = (double)p / q;
		ds5k_format_vdiv(tmp, sizeof(tmp), val);
		snprintf(cmd, sizeof(cmd), ":CHANNEL%d:RANGE:%s",
			ch_idx + 1, tmp);
		if (rigol_ds5k_send(usb, cmd) != SR_OK)
			return SR_ERR;
		i = ds5k_vdiv_index(val, state->probe[ch_idx]);
		state->vdiv_idx[ch_idx] = i;
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret = config_set_live(key, data, sdi, cg);

	/* Leave the front panel local once the change is done. */
	if (sdi && sdi->priv)
		ds5k_quiesce(sdi);

	return ret;
}

static GVariant *ds5k_vdiv_list(int probe)
{
	uint64_t v[NUM_VDIV_STEPS][2];
	int i;

	for (i = 0; i < NUM_VDIV_STEPS; i++)
		ds5k_vdiv_tuple(i, probe, &v[i][0], &v[i][1]);

	return std_gvar_tuple_array(v, NUM_VDIV_STEPS);
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct rigol_ds5k_state *state;
	int ch_idx, probe;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg,
				scanopts, drvopts, devopts);
		case SR_CONF_SAMPLERATE:
		{
			uint64_t rates[NUM_TIMEBASES];
			unsigned int i, n;

			/*
			 * The sample rate follows from the timebase, and the
			 * ADC tops out at DS5K_MAX_SAMPLERATE, so the fastest
			 * timebases all map to the same rate: deduplicate.
			 */
			for (i = 0, n = 0; i < NUM_TIMEBASES; i++) {
				uint64_t r = ds5k_samplerate_for(
					(double)timebases[i][0] /
					timebases[i][1]);
				unsigned int j;

				for (j = 0; j < n; j++)
					if (rates[j] == r)
						break;
				if (j == n)
					rates[n++] = r;
			}
			*data = std_gvar_samplerates(rates, n);
			return SR_OK;
		}
		case SR_CONF_TIMEBASE:
			*data = std_gvar_tuple_array(timebases, NUM_TIMEBASES);
			return SR_OK;
		default:
			return SR_ERR_NA;
		}
	}

	state = sdi ? sdi->priv : NULL;

	if (sdi->channel_groups->data == cg)
		ch_idx = 0;
	else if (sdi->channel_groups->next &&
		sdi->channel_groups->next->data == cg)
		ch_idx = 1;
	else
		return SR_ERR_ARG;

	probe = state ? state->probe[ch_idx] : 1;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = std_gvar_array_u32(devopts_cg, ARRAY_SIZE(devopts_cg));
		return SR_OK;
	case SR_CONF_VDIV:
		*data = ds5k_vdiv_list(probe);
		return SR_OK;
	case SR_CONF_COUPLING:
		*data = g_variant_new_strv(coupling, NUM_COUPLING);
		return SR_OK;
	case SR_CONF_PROBE_FACTOR:
		*data = std_gvar_array_u64(probes, NUM_PROBES);
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
}

/*---- Acquisition ------------------------------------------------------*/

static int ds5k_send_channel(const struct sr_dev_inst *sdi, int ch_idx,
	size_t num_samples, uint8_t *raw)
{
	struct rigol_ds5k_state *state = sdi->priv;
	struct sr_channel *ch;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	double vdiv;
	float *samples;
	size_t i;
	int idx, digits;

	ch = g_slist_nth_data(sdi->channels, ch_idx);
	if (!ch)
		return SR_ERR_ARG;

	idx = state->vdiv_idx[ch_idx];
	if (idx < 0)
		idx = 4;
	vdiv = (double)vdivs_base[idx][0] / vdivs_base[idx][1] *
		state->probe[ch_idx];

	samples = g_try_malloc(num_samples * sizeof(float));
	if (!samples)
		return SR_ERR_MALLOC;

	/*
	 * Raw 8-bit samples: lower code = higher on screen = higher voltage.
	 * One division is DS5K_CODES_PER_DIV codes.
	 */
	for (i = 0; i < num_samples; i++)
		samples[i] = (DS5K_CODE_ZERO - raw[i]) * vdiv /
			DS5K_CODES_PER_DIV;

	digits = vdiv >= 1.0 ? 3 : 4;

	sr_analog_init(&analog, &encoding, &meaning, &spec, digits);
	analog.data = samples;
	analog.num_samples = num_samples;
	analog.meaning->mq = SR_MQ_VOLTAGE;
	analog.meaning->unit = SR_UNIT_VOLT;
	analog.meaning->mqflags = (state->coupling[ch_idx] == 0) ?
		SR_MQFLAG_AC : SR_MQFLAG_DC;
	analog.meaning->channels = g_slist_append(NULL, ch);
	analog.encoding->digits = digits;
	analog.spec->spec_digits = digits;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(sdi, &packet);

	g_slist_free(analog.meaning->channels);
	g_free(samples);

	return SR_OK;
}

static int ds5k_acquire_frame(const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct sr_channel *ch;
	GSList *l;
	uint8_t raw[DS5K_WINDOW_SAMPLES + 64];
	size_t num_samples;
	int r;

	std_session_send_df_frame_begin(sdi);

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type != SR_CHANNEL_ANALOG || !ch->enabled)
			continue;

		num_samples = 0;
		r = rigol_ds5k_download(usb, ch->index + 1, raw,
			sizeof(raw), &num_samples);
		if (r != SR_OK || num_samples == 0) {
			sr_err("Failed to download waveform for %s.",
				ch->name);
			continue;
		}
		sr_dbg("Downloaded %zu samples for %s.", num_samples,
			ch->name);

		ds5k_send_channel(sdi, ch->index, num_samples, raw);
	}

	std_session_send_df_frame_end(sdi);

	return SR_OK;
}

/*
 * Deliver one frame, then reschedule. `limit_frames == 0` means "keep
 * going" (rolling acquisition); a non-zero limit stops the session once
 * that many frames have been sent, which is what sigrok-cli's --frames
 * relies on.
 */
static int ds5k_poll(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct rigol_ds5k_state *state;
	struct sr_usb_dev_inst *usb;
	double tdiv;
	uint64_t samplerate;

	(void)fd;
	(void)revents;

	if (!sdi || !(state = sdi->priv) || !(usb = sdi->conn))
		return FALSE;

	/* The device may have been unplugged while the session ran. */
	if (!usb->devhdl) {
		sr_err("Device is gone; stopping acquisition.");
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	/* Track front-panel changes made between frames. */
	ds5k_refresh_state(sdi);

	if (ds5k_query_tdiv(usb, &tdiv) == SR_OK) {
		samplerate = ds5k_samplerate_for(tdiv);
		if (samplerate != state->samplerate) {
			state->samplerate = samplerate;
			sr_session_send_meta(sdi, SR_CONF_SAMPLERATE,
				g_variant_new_uint64(samplerate));
		}
	}

	if (ds5k_acquire_frame(sdi) != SR_OK) {
		sr_err("Failed to acquire frame; stopping acquisition.");
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	state->num_frames++;

	if (state->limit_frames && state->num_frames >= state->limit_frames)
		sr_dev_acquisition_stop(sdi);

	return TRUE;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct drv_context *drvc = sdi->driver->context;
	int r;

	r = sr_usb_open(drvc->sr_ctx->libusb_ctx, usb);
	if (r != SR_OK) {
		sr_err("Failed to open Rigol DS5000USB.");
		return SR_ERR;
	}

	/*
	 * Opening itself sends nothing and does not claim interface 0, so
	 * merely having the device open does not disturb the scope. The
	 * interface is claimed for the duration of each acquisition, in
	 * dev_acquisition_start(). Config access outside an acquisition may
	 * talk to the scope, but always hands the front panel back
	 * afterwards (ds5k_quiesce).
	 */

	sdi->status = SR_ST_ACTIVE;

	sr_dbg("Rigol DS5000USB opened.");

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct rigol_ds5k_state *state = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;

	state->acquiring = FALSE;

	/*
	 * Hand the front panel back on the way out. The command is
	 * idempotent, so sending it even when the panel is already local
	 * does nothing; this also covers a final config read after Stop.
	 */
	if (usb && usb->devhdl)
		rigol_ds5k_release_panel(usb);

	if (usb && usb->devhdl) {
		if (state->claimed) {
			libusb_release_interface(usb->devhdl, 0);
			state->claimed = FALSE;
		}
		sr_usb_close(usb);
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct rigol_ds5k_state *state = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;
	double tdiv;
	uint64_t samplerate;
	int r;

	state->num_frames = 0;

	/*
	 * The firmware locks the scope's front panel as soon as the host
	 * sends it a command, so we claim interface 0 for the duration of
	 * the acquisition. Claiming is a host-side usbfs operation and is
	 * not itself what locks the panel, but it keeps our transfers legal
	 * while running. Outside an acquisition the interface is released
	 * and every config access ends by handing the panel back.
	 */
	if (!state->claimed) {
		r = libusb_claim_interface(usb->devhdl, 0);
		if (r < 0) {
			sr_err("Failed to claim interface: %s",
				libusb_error_name(r));
			return SR_ERR;
		}
		state->claimed = TRUE;
	}

	/* Pick up any front-panel changes before converting the samples. */
	ds5k_refresh_state((struct sr_dev_inst *)sdi);

	state->acquiring = TRUE;

	if (ds5k_query_tdiv(usb, &tdiv) == SR_OK)
		samplerate = ds5k_samplerate_for(tdiv);
	else
		samplerate = 400000;

	state->samplerate = samplerate;

	std_session_send_df_header(sdi);
	sr_session_send_meta(sdi, SR_CONF_SAMPLERATE,
		g_variant_new_uint64(samplerate));

	/*
	 * One frame per poll. A frame (state refresh + download) takes only
	 * a few tens of ms, so a short interval gives a smooth rolling
	 * display. The callback runs synchronously on the session's main
	 * loop, so a frame can never overlap the previous one.
	 */
	sr_session_source_add(sdi->session, -1, 0, DS5K_POLL_INTERVAL_MS,
		ds5k_poll, (void *)sdi);
	state->polling = TRUE;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct rigol_ds5k_state *state = sdi->priv;
	struct sr_usb_dev_inst *usb = sdi->conn;

	if (state->polling) {
		sr_session_source_remove(sdi->session, -1);
		state->polling = FALSE;
	}

	state->acquiring = FALSE;

	/*
	 * Hand the front panel back before letting go of the device. This must
	 * be the last command the scope sees: the DS5000 leaves remote mode
	 * when ":KEY:LOCK:ENABLE" is the final thing on the wire and nothing
	 * follows it. A later config_get() would re-assert remote mode, which
	 * is expected while the operator is still driving PulseView.
	 */
	if (state->claimed && usb && usb->devhdl)
		rigol_ds5k_release_panel(usb);

	if (state->claimed && usb && usb->devhdl) {
		libusb_release_interface(usb->devhdl, 0);
		state->claimed = FALSE;
	}

	std_session_send_df_end(sdi);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver rigol_ds5k_driver_info = {
	.name = "rigol-ds5k",
	.longname = "Rigol DS5000",
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
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(rigol_ds5k_driver_info);
