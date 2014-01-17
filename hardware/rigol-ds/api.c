/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Martin Ling <martin-git@earth.li>
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2013 Mathias Grimmberger <mgri@zaphod.sax.de>
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

static const int32_t hwopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM
};

static const int32_t hwcaps[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_TIMEBASE,
	SR_CONF_TRIGGER_SOURCE,
	SR_CONF_TRIGGER_SLOPE,
	SR_CONF_HORIZ_TRIGGERPOS,
	SR_CONF_NUM_TIMEBASE,
	SR_CONF_LIMIT_FRAMES,
	SR_CONF_SAMPLERATE,
};

static const int32_t analog_hwcaps[] = {
	SR_CONF_NUM_VDIV,
	SR_CONF_VDIV,
	SR_CONF_COUPLING,
	SR_CONF_DATA_SOURCE,
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
	{ 100, 1 },
	{ 200, 1 },
	{ 500, 1 },
	/* { 1000, 1 }, Confuses other code? */
};

static const uint64_t vdivs[][2] = {
	/* microvolts */
	{ 500, 1000000 },
	/* millivolts */
	{ 1, 1000 },
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

#define NUM_TIMEBASE  ARRAY_SIZE(timebases)
#define NUM_VDIV      ARRAY_SIZE(vdivs)

static const char *trigger_sources[] = {
	"CH1",
	"CH2",
	"CH3",
	"CH4",
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

/* Do not change the order of entries */
static const char *data_sources[] = {
	"Live",
	"Memory",
	"Segmented",
};

/* 
 * name, series, protocol flavor, min timebase, max timebase, min vdiv,
 * digital channels, number of horizontal divs
 */

#define RIGOL "Rigol Technologies"
#define AGILENT "Agilent Technologies"

static const struct rigol_ds_model supported_models[] = {
	{RIGOL, "DS1052E", RIGOL_DS1000, PROTOCOL_LEGACY, {5, 1000000000}, {50, 1}, {2, 1000}, 2, false, 12},
	{RIGOL, "DS1102E", RIGOL_DS1000, PROTOCOL_LEGACY, {2, 1000000000}, {50, 1}, {2, 1000}, 2, false, 12},
	{RIGOL, "DS1152E", RIGOL_DS1000, PROTOCOL_LEGACY, {2, 1000000000}, {50, 1}, {2, 1000}, 2, false, 12},
	{RIGOL, "DS1052D", RIGOL_DS1000, PROTOCOL_LEGACY, {5, 1000000000}, {50, 1}, {2, 1000}, 2, true, 12},
	{RIGOL, "DS1102D", RIGOL_DS1000, PROTOCOL_LEGACY, {2, 1000000000}, {50, 1}, {2, 1000}, 2, true, 12},
	{RIGOL, "DS1152D", RIGOL_DS1000, PROTOCOL_LEGACY, {2, 1000000000}, {50, 1}, {2, 1000}, 2, true, 12},
	{RIGOL, "DS2072", RIGOL_DS2000, PROTOCOL_IEEE488_2, {5, 1000000000}, {500, 1}, {500, 1000000}, 2, false, 14},
	{RIGOL, "DS2102", RIGOL_DS2000, PROTOCOL_IEEE488_2, {5, 1000000000}, {500, 1}, {500, 1000000}, 2, false, 14},
	{RIGOL, "DS2202", RIGOL_DS2000, PROTOCOL_IEEE488_2, {2, 1000000000}, {500, 1}, {500, 1000000}, 2, false, 14},
	{RIGOL, "DS2302", RIGOL_DS2000, PROTOCOL_IEEE488_2, {1, 1000000000}, {1000, 1}, {500, 1000000}, 2, false, 14},
	{RIGOL, "DS2072A", RIGOL_DS2000, PROTOCOL_IEEE488_2, {5, 1000000000}, {1000, 1}, {500, 1000000}, 2, false, 14},
	{RIGOL, "DS2102A", RIGOL_DS2000, PROTOCOL_IEEE488_2, {5, 1000000000}, {1000, 1}, {500, 1000000}, 2, false, 14},
	{RIGOL, "DS2202A", RIGOL_DS2000, PROTOCOL_IEEE488_2, {2, 1000000000}, {1000, 1}, {500, 1000000}, 2, false, 14},
	{RIGOL, "DS2302A", RIGOL_DS2000, PROTOCOL_IEEE488_2, {1, 1000000000}, {1000, 1}, {500, 1000000}, 2, false, 14},
	{RIGOL, "VS5022", RIGOL_VS5000, PROTOCOL_LEGACY, {20, 1000000000}, {50, 1}, {2, 1000}, 2, false, 14},
	{RIGOL, "VS5022D", RIGOL_VS5000, PROTOCOL_LEGACY, {20, 1000000000}, {50, 1}, {2, 1000}, 2, true, 14},
	{RIGOL, "VS5042", RIGOL_VS5000, PROTOCOL_LEGACY, {10, 1000000000}, {50, 1}, {2, 1000}, 2, false, 14},
	{RIGOL, "VS5042D", RIGOL_VS5000, PROTOCOL_LEGACY, {10, 1000000000}, {50, 1}, {2, 1000}, 2, true, 14},
	{RIGOL, "VS5062", RIGOL_VS5000, PROTOCOL_LEGACY, {5, 1000000000}, {50, 1}, {2, 1000}, 2, false, 14},
	{RIGOL, "VS5062D", RIGOL_VS5000, PROTOCOL_LEGACY, {5, 1000000000}, {50, 1}, {2, 1000}, 2, true, 14},
	{RIGOL, "VS5102", RIGOL_VS5000, PROTOCOL_LEGACY, {2, 1000000000}, {50, 1}, {2, 1000}, 2, false, 14},
	{RIGOL, "VS5102D", RIGOL_VS5000, PROTOCOL_LEGACY, {2, 1000000000}, {50, 1}, {2, 1000}, 2, true, 14},
	{RIGOL, "VS5202", RIGOL_VS5000, PROTOCOL_LEGACY, {2, 1000000000}, {50, 1}, {2, 1000}, 2, false, 14},
	{RIGOL, "VS5202D", RIGOL_VS5000, PROTOCOL_LEGACY, {2, 1000000000}, {50, 1}, {2, 1000}, 2, true, 14},
	{AGILENT, "DSO1002A", AGILENT_DSO1000, PROTOCOL_IEEE488_2, {5, 1000000000}, {50, 1}, {2, 1000}, 2, false, 12},
	{AGILENT, "DSO1004A", AGILENT_DSO1000, PROTOCOL_IEEE488_2, {5, 1000000000}, {50, 1}, {2, 1000}, 4, false, 12},
	{AGILENT, "DSO1012A", AGILENT_DSO1000, PROTOCOL_IEEE488_2, {2, 1000000000}, {50, 1}, {2, 1000}, 2, false, 12},
	{AGILENT, "DSO1014A", AGILENT_DSO1000, PROTOCOL_IEEE488_2, {2, 1000000000}, {50, 1}, {2, 1000}, 4, false, 12},
	{AGILENT, "DSO1022A", AGILENT_DSO1000, PROTOCOL_IEEE488_2, {2, 1000000000}, {50, 1}, {2, 1000}, 2, false, 12},
	{AGILENT, "DSO1024A", AGILENT_DSO1000, PROTOCOL_IEEE488_2, {2, 1000000000}, {50, 1}, {2, 1000}, 4, false, 12},
};

SR_PRIV struct sr_dev_driver rigol_ds_driver_info;
static struct sr_dev_driver *di = &rigol_ds_driver_info;

static void clear_helper(void *priv)
{
	struct dev_context *devc;

	devc = priv;
	g_free(devc->data);
	g_free(devc->buffer);
	g_free(devc->coupling[0]);
	g_free(devc->coupling[1]);
	g_free(devc->trigger_source);
	g_free(devc->trigger_slope);
	g_slist_free(devc->analog_groups[0].probes);
	g_slist_free(devc->analog_groups[1].probes);
	g_slist_free(devc->digital_group.probes);
}

static int dev_clear(void)
{
	return std_dev_clear(di, clear_helper);
}

static int set_cfg(const struct sr_dev_inst *sdi, const char *format, ...)
{
	va_list args;
	int ret;

	va_start(args, format);
	ret = sr_scpi_send_variadic(sdi->conn, format, args);
	va_end(args);

	if (ret != SR_OK)
		return SR_ERR;

	/* When setting a bunch of parameters in a row, the DS1052E scrambles
	 * some of them unless there is at least 100ms delay in between. */
	sr_spew("delay %dms", 100);
	g_usleep(100000);

	return SR_OK;
}

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static int probe_port(const char *resource, const char *serialcomm, GSList **devices)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_scpi_dev_inst *scpi;
	struct sr_scpi_hw_info *hw_info;
	struct sr_probe *probe;
	unsigned int i;
	const struct rigol_ds_model *model = NULL;
	gchar *channel_name;

	*devices = NULL;

	if (!(scpi = scpi_dev_inst_new(resource, serialcomm)))
		return SR_ERR;

	if (sr_scpi_open(scpi) != SR_OK) {
		sr_info("Couldn't open SCPI device.");
		sr_scpi_free(scpi);
		return SR_ERR;
	};

	if (sr_scpi_get_hw_id(scpi, &hw_info) != SR_OK) {
		sr_info("Couldn't get IDN response.");
		sr_scpi_close(scpi);
		sr_scpi_free(scpi);
		return SR_ERR;
	}

	for (i = 0; i < ARRAY_SIZE(supported_models); i++) {
		if (!strcasecmp(hw_info->manufacturer, supported_models[i].vendor) &&
				!strcmp(hw_info->model, supported_models[i].name)) {
			model = &supported_models[i];
			break;
		}
	}

	if (!model || !(sdi = sr_dev_inst_new(0, SR_ST_ACTIVE,
					      hw_info->manufacturer, hw_info->model,
						  hw_info->firmware_version))) {
		sr_scpi_hw_info_free(hw_info);
		sr_scpi_close(scpi);
		sr_scpi_free(scpi);
		return SR_ERR_NA;
	}

	sr_scpi_hw_info_free(hw_info);
	sr_scpi_close(scpi);

	sdi->conn = scpi;

	sdi->driver = di;
	sdi->inst_type = SR_INST_SCPI;

	if (!(devc = g_try_malloc0(sizeof(struct dev_context))))
		return SR_ERR_MALLOC;

	devc->limit_frames = 0;
	devc->model = model;

	for (i = 0; i < model->analog_channels; i++) {
		if (!(channel_name = g_strdup_printf("CH%d", i + 1)))
			return SR_ERR_MALLOC;
		probe = sr_probe_new(i, SR_PROBE_ANALOG, TRUE, channel_name);
		sdi->probes = g_slist_append(sdi->probes, probe);
		devc->analog_groups[i].name = channel_name;
		devc->analog_groups[i].probes = g_slist_append(NULL, probe);
		sdi->probe_groups = g_slist_append(sdi->probe_groups,
				&devc->analog_groups[i]);
	}

	if (devc->model->has_digital) {
		for (i = 0; i < 16; i++) {
			if (!(channel_name = g_strdup_printf("D%d", i)))
				return SR_ERR_MALLOC;
			probe = sr_probe_new(i, SR_PROBE_LOGIC, TRUE, channel_name);
			g_free(channel_name);
			if (!probe)
				return SR_ERR_MALLOC;
			sdi->probes = g_slist_append(sdi->probes, probe);
			devc->digital_group.probes = g_slist_append(
					devc->digital_group.probes, probe);
		}
		devc->digital_group.name = "LA";
		sdi->probe_groups = g_slist_append(sdi->probe_groups,
				&devc->digital_group);
	}

	for (i = 0; i < NUM_TIMEBASE; i++) {
		if (!memcmp(&devc->model->min_timebase, &timebases[i], sizeof(uint64_t[2])))
			devc->timebases = &timebases[i];
		if (!memcmp(&devc->model->max_timebase, &timebases[i], sizeof(uint64_t[2])))
			devc->num_timebases = &timebases[i] - devc->timebases + 1;
	}

	for (i = 0; i < NUM_VDIV; i++)
		if (!memcmp(&devc->model->min_vdiv, &vdivs[i], sizeof(uint64_t[2])))
			devc->vdivs = &vdivs[i];

	if (!(devc->buffer = g_try_malloc(ACQ_BUFFER_SIZE)))
		return SR_ERR_MALLOC;
	if (!(devc->data = g_try_malloc(ACQ_BUFFER_SIZE * sizeof(float))))
		return SR_ERR_MALLOC;

	devc->data_source = DATA_SOURCE_LIVE;

	sdi->priv = devc;

	*devices = g_slist_append(NULL, sdi);

	return SR_OK;
}

static GSList *scan(GSList *options)
{
	struct drv_context *drvc;
	struct sr_config *src;
	GSList *l, *devices;
	GDir *dir;
	int ret;
	const gchar *dev_name;
	gchar *port = NULL;
	gchar *serialcomm = NULL;

	drvc = di->priv;

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			port = (char *)g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = (char *)g_variant_get_string(src->data, NULL);
			break;
		}
	}

	devices = NULL;
	if (port) {
		if (probe_port(port, serialcomm, &devices) == SR_ERR_MALLOC) {
			g_free(port);
			if (serialcomm)
				g_free(serialcomm);
			return NULL;
		}
	} else {
		if (!(dir = g_dir_open("/sys/class/usbmisc/", 0, NULL)))
			if (!(dir = g_dir_open("/sys/class/usb/", 0, NULL)))
				return NULL;
		while ((dev_name = g_dir_read_name(dir))) {
			if (strncmp(dev_name, "usbtmc", 6))
				continue;
			port = g_strconcat("/dev/", dev_name, NULL);
			ret = probe_port(port, serialcomm, &devices);
			g_free(port);
			if (serialcomm)
				g_free(serialcomm);
			if (ret == SR_ERR_MALLOC) {
				g_dir_close(dir);
				return NULL;
			}
		}
		g_dir_close(dir);
	}

	/* Tack a copy of the newly found devices onto the driver list. */
	l = g_slist_copy(devices);
	drvc->instances = g_slist_concat(drvc->instances, l);

	return devices;
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi = sdi->conn;

	if (sr_scpi_open(scpi) < 0)
		return SR_ERR;

	if (rigol_ds_get_dev_cfg(sdi) != SR_OK)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct sr_scpi_dev_inst *scpi;

	scpi = sdi->conn;

	if (scpi) {
		if (sr_scpi_close(scpi) < 0)
			return SR_ERR;
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

static int cleanup(void)
{
	return dev_clear();
}

static int analog_frame_size(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_probe *probe;
	int analog_probes = 0;
	GSList *l;

	if (devc->model->protocol == PROTOCOL_LEGACY) {
		if (devc->model->series == RIGOL_VS5000)
			return VS5000_ANALOG_LIVE_WAVEFORM_SIZE;
		else
			return DS1000_ANALOG_LIVE_WAVEFORM_SIZE;
	} else {
		for (l = sdi->probes; l; l = l->next) {
			probe = l->data;
			if (probe->type == SR_PROBE_ANALOG && probe->enabled)
				analog_probes++;
		}
		if (devc->data_source == DATA_SOURCE_MEMORY) {
			if (analog_probes == 1)
				return DS2000_ANALOG_MEM_WAVEFORM_SIZE_1C;
			else
				return DS2000_ANALOG_MEM_WAVEFORM_SIZE_2C;
		} else {
			if (devc->model->series == AGILENT_DSO1000)
				return DSO1000_ANALOG_LIVE_WAVEFORM_SIZE;
			else
				return DS2000_ANALOG_LIVE_WAVEFORM_SIZE;
		}
	}
}

static int digital_frame_size(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	switch (devc->model->series) {
	case RIGOL_VS5000:
		return VS5000_DIGITAL_WAVEFORM_SIZE;
	case RIGOL_DS1000:
		return DS1000_DIGITAL_WAVEFORM_SIZE;
	default:
		return 0;
	}
}

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	struct dev_context *devc;
	uint64_t samplerate;

	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	/* If a probe group is specified, it must be a valid one. */
	if (probe_group && !g_slist_find(sdi->probe_groups, probe_group)) {
		sr_err("Invalid probe group specified.");
		return SR_ERR;
	}

	switch (id) {
	case SR_CONF_NUM_TIMEBASE:
		*data = g_variant_new_int32(devc->model->num_horizontal_divs);
		break;
	case SR_CONF_NUM_VDIV:
		*data = g_variant_new_int32(8);
	case SR_CONF_DATA_SOURCE:
		if (devc->data_source == DATA_SOURCE_LIVE)
			*data = g_variant_new_string("Live");
		else if (devc->data_source == DATA_SOURCE_MEMORY)
			*data = g_variant_new_string("Memory");
		else
			*data = g_variant_new_string("Segmented");
		break;
	case SR_CONF_SAMPLERATE:
		if (devc->data_source == DATA_SOURCE_LIVE) {
			samplerate = analog_frame_size(sdi) /
				(devc->timebase * devc->model->num_horizontal_divs);
			*data = g_variant_new_uint64(samplerate);
		} else {
			return SR_ERR_NA;
		}
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	struct dev_context *devc;
	uint64_t tmp_u64, p, q;
	double t_dbl;
	unsigned int i, j;
	int ret;
	const char *tmp_str;

	if (!(devc = sdi->priv))
		return SR_ERR_ARG;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	/* If a probe group is specified, it must be a valid one. */
	if (probe_group && !g_slist_find(sdi->probe_groups, probe_group)) {
		sr_err("Invalid probe group specified.");
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
		t_dbl = -(devc->horiz_triggerpos - 0.5) * devc->timebase * devc->num_timebases;
		ret = set_cfg(sdi, ":TIM:OFFS %.6f", t_dbl);
		break;
	case SR_CONF_TIMEBASE:
		g_variant_get(data, "(tt)", &p, &q);
		for (i = 0; i < devc->num_timebases; i++) {
			if (devc->timebases[i][0] == p && devc->timebases[i][1] == q) {
				devc->timebase = (float)p / q;
				ret = set_cfg(sdi, ":TIM:SCAL %.9f", devc->timebase);
				break;
			}
		}
		if (i == devc->num_timebases)
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
				else if (!strcmp(devc->trigger_source, "CH3"))
					tmp_str = "CHAN3";
				else if (!strcmp(devc->trigger_source, "CH4"))
					tmp_str = "CHAN4";
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
		if (!probe_group) {
			sr_err("No probe group specified.");
			return SR_ERR_PROBE_GROUP;
		}
		g_variant_get(data, "(tt)", &p, &q);
		for (i = 0; i < 2; i++) {
			if (probe_group == &devc->analog_groups[i]) {
				for (j = 0; j < ARRAY_SIZE(vdivs); j++) {
					if (vdivs[j][0] != p || vdivs[j][1] != q)
						continue;
					devc->vdiv[i] = (float)p / q;
					return set_cfg(sdi, ":CHAN%d:SCAL %.3f", i + 1,
							devc->vdiv[i]);
				}
				return SR_ERR_ARG;
			}
		}
		return SR_ERR_NA;
	case SR_CONF_COUPLING:
		if (!probe_group) {
			sr_err("No probe group specified.");
			return SR_ERR_PROBE_GROUP;
		}
		tmp_str = g_variant_get_string(data, NULL);
		for (i = 0; i < 2; i++) {
			if (probe_group == &devc->analog_groups[i]) {
				for (j = 0; j < ARRAY_SIZE(coupling); j++) {
					if (!strcmp(tmp_str, coupling[j])) {
						g_free(devc->coupling[i]);
						devc->coupling[i] = g_strdup(coupling[j]);
						return set_cfg(sdi, ":CHAN%d:COUP %s", i + 1,
								devc->coupling[i]);
					}
				}
				return SR_ERR_ARG;
			}
		}
		return SR_ERR_NA;
	case SR_CONF_DATA_SOURCE:
		tmp_str = g_variant_get_string(data, NULL);
		if (!strcmp(tmp_str, "Live"))
			devc->data_source = DATA_SOURCE_LIVE;
		else if (!strcmp(tmp_str, "Memory"))
			devc->data_source = DATA_SOURCE_MEMORY;
		else if (devc->model->protocol == PROTOCOL_IEEE488_2
			 && !strcmp(tmp_str, "Segmented"))
			devc->data_source = DATA_SOURCE_SEGMENTED;
		else
			return SR_ERR;
		break;
	default:
		ret = SR_ERR_NA;
		break;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_probe_group *probe_group)
{
	GVariant *tuple, *rational[2];
	GVariantBuilder gvb;
	unsigned int i;
	struct dev_context *devc = NULL;

	if (sdi)
		devc = sdi->priv;

	if (key == SR_CONF_SCAN_OPTIONS) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwopts, ARRAY_SIZE(hwopts), sizeof(int32_t));
		return SR_OK;
	} else if (key == SR_CONF_DEVICE_OPTIONS && probe_group == NULL) {
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
			hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		return SR_OK;
	}

	/* Every other option requires a valid device instance. */
	if (!sdi || !(devc = sdi->priv))
		return SR_ERR_ARG;

	/* If a probe group is specified, it must be a valid one. */
	if (probe_group) {
		if (probe_group != &devc->analog_groups[0]
				&& probe_group != &devc->analog_groups[1]) {
			sr_err("Invalid probe group specified.");
			return SR_ERR;
		}
	}

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		if (!probe_group) {
			sr_err("No probe group specified.");
			return SR_ERR_PROBE_GROUP;
		}
		if (probe_group == &devc->digital_group) {
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				NULL, 0, sizeof(int32_t));
			return SR_OK;
		} else {
			for (i = 0; i < 2; i++) {
				if (probe_group == &devc->analog_groups[i]) {
					*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
						analog_hwcaps, ARRAY_SIZE(analog_hwcaps), sizeof(int32_t));
					return SR_OK;
				}
			}
			return SR_ERR_NA;
		}
		break;
	case SR_CONF_COUPLING:
		if (!probe_group) {
			sr_err("No probe group specified.");
			return SR_ERR_PROBE_GROUP;
		}
		*data = g_variant_new_strv(coupling, ARRAY_SIZE(coupling));
		break;
	case SR_CONF_VDIV:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		if (!probe_group) {
			sr_err("No probe group specified.");
			return SR_ERR_PROBE_GROUP;
		}
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < NUM_VDIV; i++) {
			rational[0] = g_variant_new_uint64(devc->vdivs[i][0]);
			rational[1] = g_variant_new_uint64(devc->vdivs[i][1]);
			tuple = g_variant_new_tuple(rational, 2);
			g_variant_builder_add_value(&gvb, tuple);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TIMEBASE:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		if (devc->num_timebases <= 0)
			return SR_ERR_NA;
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		for (i = 0; i < devc->num_timebases; i++) {
			rational[0] = g_variant_new_uint64(devc->timebases[i][0]);
			rational[1] = g_variant_new_uint64(devc->timebases[i][1]);
			tuple = g_variant_new_tuple(rational, 2);
			g_variant_builder_add_value(&gvb, tuple);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		*data = g_variant_new_strv(trigger_sources,
				devc->model->has_digital ? ARRAY_SIZE(trigger_sources) : 4);
		break;
	case SR_CONF_DATA_SOURCE:
		if (!devc)
			/* Can't know this until we have the exact model. */
			return SR_ERR_ARG;
		/* This needs tweaking by series/model! */
		if (devc->model->series == RIGOL_DS2000)
			*data = g_variant_new_strv(data_sources, ARRAY_SIZE(data_sources));
		else
			*data = g_variant_new_strv(data_sources, ARRAY_SIZE(data_sources) - 1);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct sr_scpi_dev_inst *scpi;
	struct dev_context *devc;
	struct sr_probe *probe;
	GSList *l;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	scpi = sdi->conn;
	devc = sdi->priv;

	devc->num_frames = 0;

	for (l = sdi->probes; l; l = l->next) {
		probe = l->data;
		sr_dbg("handling probe %s", probe->name);
		if (probe->type == SR_PROBE_ANALOG) {
			if (probe->enabled)
				devc->enabled_analog_probes = g_slist_append(
						devc->enabled_analog_probes, probe);
			if (probe->enabled != devc->analog_channels[probe->index]) {
				/* Enabled channel is currently disabled, or vice versa. */
				if (set_cfg(sdi, ":CHAN%d:DISP %s", probe->index + 1,
						probe->enabled ? "ON" : "OFF") != SR_OK)
					return SR_ERR;
				devc->analog_channels[probe->index] = probe->enabled;
			}
		} else if (probe->type == SR_PROBE_LOGIC) {
			if (probe->enabled) {
				devc->enabled_digital_probes = g_slist_append(
						devc->enabled_digital_probes, probe);
				/* Turn on LA module if currently off. */
				if (!devc->la_enabled) {
					if (set_cfg(sdi, ":LA:DISP ON") != SR_OK)
						return SR_ERR;
					devc->la_enabled = TRUE;
				}
			}
			if (probe->enabled != devc->digital_channels[probe->index]) {
				/* Enabled channel is currently disabled, or vice versa. */
				if (set_cfg(sdi, ":DIG%d:TURN %s", probe->index,
						probe->enabled ? "ON" : "OFF") != SR_OK)
					return SR_ERR;
				devc->digital_channels[probe->index] = probe->enabled;
			}
		}
	}

	if (!devc->enabled_analog_probes && !devc->enabled_digital_probes)
		return SR_ERR;

	/* Turn off LA module if on and no digital probes selected. */
	if (devc->la_enabled && !devc->enabled_digital_probes)
		if (set_cfg(sdi, ":LA:DISP OFF") != SR_OK)
			return SR_ERR;

	if (devc->data_source == DATA_SOURCE_LIVE) {
		if (set_cfg(sdi, ":RUN") != SR_OK)
			return SR_ERR;
	} else if (devc->data_source == DATA_SOURCE_MEMORY) {
		if (devc->model->series != RIGOL_DS2000) {
			sr_err("Data source 'Memory' not supported for this device");
			return SR_ERR;
		}
	} else if (devc->data_source == DATA_SOURCE_SEGMENTED) {
		sr_err("Data source 'Segmented' not yet supported");
		return SR_ERR;
	}

	sr_scpi_source_add(scpi, G_IO_IN, 50, rigol_ds_receive, (void *)sdi);

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	if (devc->enabled_analog_probes)
		devc->channel_entry = devc->enabled_analog_probes;
	else
		devc->channel_entry = devc->enabled_digital_probes;

	devc->analog_frame_size = analog_frame_size(sdi);
	devc->digital_frame_size = digital_frame_size(sdi);

	if (devc->model->protocol == PROTOCOL_LEGACY) {
		/* Fetch the first frame. */
		if (rigol_ds_channel_start(sdi) != SR_OK)
			return SR_ERR;
	} else {
		if (devc->enabled_analog_probes) {
			if (devc->data_source == DATA_SOURCE_MEMORY) {
				/* Apparently for the DS2000 the memory
				 * depth can only be set in Running state -
				 * this matches the behaviour of the UI. */
				if (set_cfg(sdi, ":RUN") != SR_OK)
					return SR_ERR;
				if (set_cfg(sdi, "ACQ:MDEP %d", devc->analog_frame_size) != SR_OK)
					return SR_ERR;
				if (set_cfg(sdi, ":STOP") != SR_OK)
					return SR_ERR;
			}
			if (rigol_ds_capture_start(sdi) != SR_OK)
				return SR_ERR;
		}
	}

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	struct sr_scpi_dev_inst *scpi;
	struct sr_datafeed_packet packet;

	(void)cb_data;

	devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't stop acquisition.");
		return SR_ERR;
	}

	/* End of last frame. */
	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);

	g_slist_free(devc->enabled_analog_probes);
	g_slist_free(devc->enabled_digital_probes);
	devc->enabled_analog_probes = NULL;
	devc->enabled_digital_probes = NULL;
	scpi = sdi->conn;
	sr_scpi_source_remove(scpi);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver rigol_ds_driver_info = {
	.name = "rigol-ds",
	.longname = "Rigol DS",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
