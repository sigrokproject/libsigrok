/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Martin Ling <martin-git@earth.li>
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "protocol.h"

#define NUM_TIMEBASE  12
#define NUM_VDIV      8

static const int32_t hwopts[] = {
	SR_CONF_CONN,
};

static const int32_t hwcaps[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_TIMEBASE,
	SR_CONF_TRIGGER_SOURCE,
	SR_CONF_TRIGGER_SLOPE,
	SR_CONF_HORIZ_TRIGGERPOS,
	SR_CONF_VDIV,
	SR_CONF_COUPLING,
	SR_CONF_NUM_TIMEBASE,
	SR_CONF_NUM_VDIV,
};

static const uint64_t timebases[][2] = {
	/* nanoseconds */
	{ 2, 1000000000 },
	{ 5, 1000000000 },
	{ 10, 1000000000 },
	{ 20, 1000000000 },
	{ 50, 1000000000 },
	{ 100, 1000000000 },
	{ 500, 1000000000 },
	/* microseconds */
	{ 1, 1000000 },
	{ 2, 1000000 },
	{ 5, 1000000 },
	{ 10, 1000000 },
	{ 20, 1000000 },
	{ 50, 1000000 },
	{ 100, 1000000 },
	{ 200, 1000000 },
	{ 500, 1000000 },
	/* milliseconds */
	{ 1, 1000 },
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* seconds */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
	{ 20, 1 },
	{ 50, 1 },
};

static const uint64_t vdivs[][2] = {
	/* millivolts */
	{ 2, 1000 },
	{ 5, 1000 },
	{ 10, 1000 },
	{ 20, 1000 },
	{ 50, 1000 },
	{ 100, 1000 },
	{ 200, 1000 },
	{ 500, 1000 },
	/* volts */
	{ 1, 1 },
	{ 2, 1 },
	{ 5, 1 },
	{ 10, 1 },
};

static const char *trigger_sources[] = {
	"CH1",
	"CH2",
	"EXT",
	"AC Line",
	"D0",
	"D1",
	"D2",
	"D3",
	"D4",
	"D5",
	"D6",
	"D7",
	"D8",
	"D9",
	"D10",
	"D11",
	"D12",
	"D13",
	"D14",
	"D15",
};

static const char *coupling[] = {
	"AC",
	"DC",
	"GND",
};

static const char *supported_models[] = {
	"DS1052E",
	"DS1102E",
	"DS1052D",
	"DS1102D",
};

SR_PRIV struct sr_dev_driver rigol_ds1xx2_driver_info;
static struct sr_dev_driver *di = &rigol_ds1xx2_driver_info;

/* Properly close and free all devices. */
static int clear_instances(void)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *l;

	if (!(drvc = di->priv))
		return SR_OK;

	for (l = drvc->instances; l; l = l->next) {

		if (!(sdi = l->data))
			continue;

		if (sdi->conn) {
			serial_close(sdi->conn);
			sr_serial_dev_inst_free(sdi->conn);
		}

		if ((devc = sdi->priv)) {
			g_free(devc->device);
			g_free(devc->coupling[0]);
			g_free(devc->coupling[1]);
			g_free(devc->trigger_source);
			g_free(devc->trigger_slope);
		}

		sr_dev_inst_free(sdi);
	}

	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
}

static int set_cfg(const struct sr_dev_inst *sdi, const char *format, ...)
{
	va_list args;
	char buf[256];

	va_start(args, format);
	vsnprintf(buf, 255, format, args);
	va_end(args);
	if (rigol_ds1xx2_send(sdi, buf) != SR_OK)
		return SR_ERR;

	/* When setting a bunch of parameters in a row, the DS1052E scrambles
	 * some of them unless there is at least 100ms delay in between. */
	sr_spew("delay %dms", 100);
	g_usleep(100000);

	return SR_OK;
}

static int hw_init(struct sr_context *sr_ctx)
{
	return std_hw_init(sr_ctx, di, DRIVER_LOG_DOMAIN);
}

static int probe_device(struct drv_context *drvc, const char *device)
{
	struct sr_serial_dev_inst *serial;
	const gchar *idn_query = "*IDN?";
	unsigned int i;
	int len, num_tokens;
	const gchar *delimiter = ",";
	gchar **tokens;
	const char *manufacturer, *model, *version;
	gboolean matched = FALSE;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	gboolean has_digital = FALSE;
	char buf[256];
	gchar *channel_name;
	struct sr_probe *probe;

	if (!(serial = sr_serial_dev_inst_new(device, NULL)))
		return SR_ERR_MALLOC;

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return SR_ERR;
	len = serial_write(serial, idn_query, strlen(idn_query));
	len = serial_read(serial, buf, sizeof(buf));
	if (serial_close(serial) != SR_OK)
		return SR_ERR;

	sr_serial_dev_inst_free(serial);

	if (len == 0)
		return SR_ERR_NA;

	buf[len] = 0;
	tokens = g_strsplit(buf, delimiter, 0);
	sr_dbg("response: %s %d [%s]", device, len, buf);

	for (num_tokens = 0; tokens[num_tokens] != NULL; num_tokens++);

	if (num_tokens < 4) {
		g_strfreev(tokens);
		return SR_ERR_NA;
	}

	manufacturer = tokens[0];
	model = tokens[1];
	version = tokens[3];

	if (strcmp(manufacturer, "Rigol Technologies")) {
		g_strfreev(tokens);
		return SR_ERR_NA;
	}

	for (i = 0; i < ARRAY_SIZE(supported_models); i++) {
		if (!strcmp(model, supported_models[i])) {
			matched = 1;
			has_digital = g_str_has_suffix(model, "D");
			break;
		}
	}

	if (!matched || !(sdi = sr_dev_inst_new(0, SR_ST_ACTIVE,
		manufacturer, model, version))) {
		g_strfreev(tokens);
		return SR_ERR_NA;
	}

	g_strfreev(tokens);

	if (!(devc = g_try_malloc0(sizeof(struct dev_context))))
		return SR_ERR_MALLOC;

	devc->limit_frames = 0;
	if (!(devc->device = g_strdup(device)))
		return SR_ERR_MALLOC;
	devc->has_digital = has_digital;
	sdi->priv = devc;
	sdi->driver = di;
	drvc->instances = g_slist_append(drvc->instances, sdi);

	for (i = 0; i < 2; i++) {
		if (!(probe = sr_probe_new(i, SR_PROBE_ANALOG, TRUE,
				i == 0 ? "CH1" : "CH2")))
			return SR_ERR_MALLOC;
		sdi->probes = g_slist_append(sdi->probes, probe);
	}

	if (devc->has_digital) {
		for (i = 0; i < 16; i++) {
			if (!(channel_name = g_strdup_printf("D%d", i)))
				return SR_ERR_MALLOC;
			probe = sr_probe_new(i, SR_PROBE_LOGIC, TRUE, channel_name);
			g_free(channel_name);
			if (!probe)
				return SR_ERR_MALLOC;
			sdi->probes = g_slist_append(sdi->probes, probe);
		}
	}

	return SR_OK;
}

static GSList *hw_scan(GSList *options)
{
	struct drv_context *drvc;
	GSList *l, *devices;
	struct sr_config *src;
	GDir *dir;
	const gchar *dev_name;
	const gchar *dev_dir = "/dev/";
	const gchar *prefix = "usbtmc";
	gchar *device = NULL;
	int ret;

	drvc = di->priv;
	drvc->instances = NULL;

	for (l = options; l; l = l->next) {
		src = l->data;
		if (src->key == SR_CONF_CONN) {
			device = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (device) {
		ret = probe_device(drvc, device);
		if (ret == SR_ERR_MALLOC) {
			clear_instances();
			return NULL;
		}
	} else {
		if (!(dir = g_dir_open("/sys/class/usb/", 0, NULL)))
			return NULL;
		while ((dev_name = g_dir_read_name(dir))) {
			if (strncmp(dev_name, prefix, strlen(prefix)))
				continue;
			device = g_strconcat(dev_dir, dev_name, NULL);
			ret = probe_device(drvc, device);
			g_free(device);
			if (ret == SR_ERR_MALLOC) {
				g_dir_close(dir);
				clear_instances();
				return NULL;
			}
		}

		g_dir_close(dir);
	}

	devices = g_slist_copy(drvc->instances);
	return devices;
}

static GSList *hw_dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (!(sdi->conn = sr_serial_dev_inst_new(devc->device, NULL)))
		return SR_ERR;

	if (serial_open(sdi->conn, SERIAL_RDWR) != SR_OK)
		return SR_ERR;

	if (rigol_ds1xx2_get_dev_cfg(sdi) != SR_OK)
		/* TODO: force configuration? */
		return SR_ERR;

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	if (serial_close(sdi->conn) != SR_OK)
		return SR_ERR;

	sr_serial_dev_inst_free(sdi->conn);
	sdi->conn = NULL;

	return SR_OK;
}

static int hw_cleanup(void)
{
	clear_instances();

	return SR_OK;
}

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi)
{

	(void)sdi;

	switch (id) {
	case SR_CONF_NUM_TIMEBASE:
		*data = g_variant_new_int32(NUM_TIMEBASE);
		break;
	case SR_CONF_NUM_VDIV:
		*data = g_variant_new_int32(NUM_VDIV);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint64_t tmp_u64, p, q;
	double t_dbl;
	unsigned int i;
	int ret;
	const char *tmp_str;

	devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't set config options.");
		return SR_ERR;
	}

	ret = SR_OK;
	switch (id) {
	case SR_CONF_LIMIT_FRAMES:
		devc->limit_frames = g_variant_get_uint64(data);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		tmp_u64 = g_variant_get_uint64(data);
		if (tmp_u64 != 0 && tmp_u64 != 1)
			return SR_ERR;
		g_free(devc->trigger_slope);
		devc->trigger_slope = g_strdup(tmp_u64 ? "POS" : "NEG");
		ret = set_cfg(sdi, ":TRIG:EDGE:SLOP %s", devc->trigger_slope);
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		t_dbl = g_variant_get_double(data);
		if (t_dbl < 0.0 || t_dbl > 1.0)
			return SR_ERR;
		devc->horiz_triggerpos = t_dbl;
		/* We have the trigger offset as a percentage of the frame, but
		 * need to express this in seconds. */
		t_dbl = -(devc->horiz_triggerpos - 0.5) * devc->timebase * NUM_TIMEBASE;
		ret = set_cfg(sdi, ":TIM:OFFS %.6f", t_dbl);
		break;
	case SR_CONF_TIMEBASE:
		g_variant_get(data, "(tt)", &p, &q);
		for (i = 0; i < ARRAY_SIZE(timebases); i++) {
			if (timebases[i][0] == p && timebases[i][1] == q) {
				devc->timebase = (float)p / q;
				ret = set_cfg(sdi, ":TIM:SCAL %.9f", devc->timebase);
				break;
			}
		}
		if (i == ARRAY_SIZE(timebases))
			ret = SR_ERR_ARG;
		break;
	case SR_CONF_TRIGGER_SOURCE:
		tmp_str = g_variant_get_string(data, NULL);
		for (i = 0; i < ARRAY_SIZE(trigger_sources); i++) {
			if (!strcmp(trigger_sources[i], tmp_str)) {
				g_free(devc->trigger_source);
				devc->trigger_source = g_strdup(trigger_sources[i]);
				if (!strcmp(devc->trigger_source, "AC Line"))
					tmp_str = "ACL";
				else if (!strcmp(devc->trigger_source, "CH1"))
					tmp_str = "CHAN1";
				else if (!strcmp(devc->trigger_source, "CH2"))
					tmp_str = "CHAN2";
				else
					tmp_str = (char *)devc->trigger_source;
				ret = set_cfg(sdi, ":TRIG:EDGE:SOUR %s", tmp_str);
				break;
			}
		}
		if (i == ARRAY_SIZE(trigger_sources))
			ret = SR_ERR_ARG;
		break;
	case SR_CONF_VDIV:
		g_variant_get(data, "(tt)", &p, &q);
		for (i = 0; i < ARRAY_SIZE(vdivs); i++) {
			if (vdivs[i][0] != p || vdivs[i][1] != q)
				continue;
			devc->vdiv[0] = devc->vdiv[1] = (float)p / q;
			set_cfg(sdi, ":CHAN1:SCAL %.3f", devc->vdiv[0]);
			ret = set_cfg(sdi, ":CHAN2:SCAL %.3f", devc->vdiv[1]);
			break;
		}
		if (i == ARRAY_SIZE(vdivs))
			ret = SR_ERR_ARG;
		break;
	case SR_CONF_COUPLING:
		/* TODO: Not supporting coupling per channel yet. */
		tmp_str = g_variant_get_string(data, NULL);
		for (i = 0; i < ARRAY_SIZE(coupling); i++) {
			if (!strcmp(tmp_str, coupling[i])) {
				g_free(devc->coupling[0]);
				g_free(devc->coupling[1]);
				devc->coupling[0] = g_strdup(coupling[i]);
				devc->coupling[1] = g_strdup(coupling[i]);
				set_cfg(sdi, ":CHAN1:COUP %s", devc->coupling[0]);
				ret = set_cfg(sdi, ":CHAN2:COUP %s", devc->coupling[1]);
				break;
			}
		}
		if (i == ARRAY_SIZE(coupling))
			ret = SR_ERR_ARG;
		break;
	default:
		ret = SR_ERR_NA;
		break;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi)
{
	GVariant *tuple, *rational[2];
	GVariantBuilder gvb;
	unsigned int i;
	struct dev_context *devc;

	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwopts, ARRAY_SIZE(hwopts), sizeof(int32_t));
		break;
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	case SR_CONF_COUPLING:
		*data = g_variant_new_strv(coupling, ARRAY_SIZE(coupling));
		break;
	case SR_CONF_VDIV:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < ARRAY_SIZE(vdivs); i++) {
			rational[0] = g_variant_new_uint64(vdivs[i][0]);
			rational[1] = g_variant_new_uint64(vdivs[i][1]);
			tuple = g_variant_new_tuple(rational, 2);
			g_variant_builder_add_value(&gvb, tuple);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TIMEBASE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < ARRAY_SIZE(timebases); i++) {
			rational[0] = g_variant_new_uint64(timebases[i][0]);
			rational[1] = g_variant_new_uint64(timebases[i][1]);
			tuple = g_variant_new_tuple(rational, 2);
			g_variant_builder_add_value(&gvb, tuple);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!sdi || !sdi->priv)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		devc = sdi->priv;
		*data = g_variant_new_strv(trigger_sources,
				devc->has_digital ? ARRAY_SIZE(trigger_sources) : 4);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_serial_dev_inst *serial;
	struct dev_context *devc;
	struct sr_probe *probe;
	GSList *l;
	char cmd[256];

	(void)cb_data;

	serial = sdi->conn;
	devc = sdi->priv;

	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		sr_dbg("handling probe %s", probe->name);
		if (probe->type == SR_PROBE_ANALOG) {
			if (probe->enabled)
				devc->enabled_analog_probes = g_slist_append(
						devc->enabled_analog_probes, probe);
			if (probe->enabled != devc->analog_channels[probe->index]) {
				/* Enabled channel is currently disabled, or vice versa. */
				sprintf(cmd, ":CHAN%d:DISP %s", probe->index + 1,
						probe->enabled ? "ON" : "OFF");
				if (rigol_ds1xx2_send(sdi, cmd) != SR_OK)
					return SR_ERR;
			}
		} else if (probe->type == SR_PROBE_LOGIC) {
			if (probe->enabled)
				devc->enabled_digital_probes = g_slist_append(
						devc->enabled_digital_probes, probe);
			if (probe->enabled != devc->digital_channels[probe->index]) {
				/* Enabled channel is currently disabled, or vice versa. */
				sprintf(cmd, ":DIG%d:TURN %s", probe->index,
						probe->enabled ? "ON" : "OFF");
				if (rigol_ds1xx2_send(sdi, cmd) != SR_OK)
					return SR_ERR;
			}
		}
	}
	if (!devc->enabled_analog_probes && !devc->enabled_digital_probes)
		return SR_ERR;

	sr_source_add(serial->fd, G_IO_IN, 50, rigol_ds1xx2_receive, (void *)sdi);

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, DRIVER_LOG_DOMAIN);

	/* Fetch the first frame. */
	if (devc->enabled_analog_probes) {
		devc->channel_frame = devc->enabled_analog_probes->data;
		if (rigol_ds1xx2_send(sdi, ":WAV:DATA? CHAN%d",
				devc->channel_frame->index + 1) != SR_OK)
			return SR_ERR;
	} else {
		devc->channel_frame = devc->enabled_digital_probes->data;
		if (rigol_ds1xx2_send(sdi, ":WAV:DATA? DIG") != SR_OK)
			return SR_ERR;
	}

	devc->num_frame_bytes = 0;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;

	(void)cb_data;

	devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't stop acquisition.");
		return SR_ERR;
	}

	g_slist_free(devc->enabled_analog_probes);
	g_slist_free(devc->enabled_digital_probes);
	devc->enabled_analog_probes = NULL;
	devc->enabled_digital_probes = NULL;
	sr_source_remove(devc->fd);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver rigol_ds1xx2_driver_info = {
	.name = "rigol-ds1xx2",
	.longname = "Rigol DS1xx2",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
