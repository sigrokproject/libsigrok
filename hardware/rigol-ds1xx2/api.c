/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Martin Ling <martin-git@earth.li>
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

static const int32_t hwcaps[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LIMIT_SAMPLES,
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
		if (!(devc = sdi->priv))
			continue;

		g_free(devc->device);
		g_slist_free(devc->enabled_probes);
		close(devc->fd);

		sr_dev_inst_free(sdi);
	}

	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
}

static int hw_init(struct sr_context *sr_ctx)
{
	return std_hw_init(sr_ctx, di, DRIVER_LOG_DOMAIN);
}

static GSList *hw_scan(GSList *options)
{
	struct drv_context *drvc;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_probe *probe;
	GSList *devices;
	GDir *dir;
	const gchar *dev_name;
	const gchar *dev_dir = "/dev/";
	const gchar *prefix = "usbtmc";
	gchar *device;
	const gchar *idn_query = "*IDN?";
	unsigned int i;
	int len, num_tokens, fd;
	const gchar *delimiter = ",";
	gchar **tokens;
	const char *manufacturer, *model, *version;
	gboolean matched = FALSE;
	char buf[256];

	(void)options;

	drvc = di->priv;
	drvc->instances = NULL;

	devices = NULL;

	dir = g_dir_open("/sys/class/usb/", 0, NULL);

	if (dir == NULL)
		return NULL;

	while ((dev_name = g_dir_read_name(dir)) != NULL) {
		if (strncmp(dev_name, prefix, strlen(prefix))) 
			continue;

		device = g_strconcat(dev_dir, dev_name, NULL);

		fd = open(device, O_RDWR);
		len = write(fd, idn_query, strlen(idn_query));
		len = read(fd, buf, sizeof(buf));
		close(fd);
		if (len == 0) {
			g_free(device);
			return NULL;
		}

		buf[len] = 0;
		tokens = g_strsplit(buf, delimiter, 0);
		close(fd);
		sr_dbg("response: %s %d [%s]", device, len, buf);

		for (num_tokens = 0; tokens[num_tokens] != NULL; num_tokens++);

		if (num_tokens < 4) {
			g_strfreev(tokens);
			g_free(device);
			return NULL;
		}

		manufacturer = tokens[0];
		model = tokens[1];
		version = tokens[3];

		if (strcmp(manufacturer, "Rigol Technologies")) {
			g_strfreev(tokens);
			g_free(device);
			return NULL;
		}

		for (i = 0; i < ARRAY_SIZE(supported_models); i++) {
			if (!strcmp(model, supported_models[i])) {
				matched = 1;
				break;
			}
		}

		if (!matched || !(sdi = sr_dev_inst_new(0, SR_ST_ACTIVE,
			manufacturer, model, version))) {
			g_strfreev(tokens);
			g_free(device);
			return NULL;
		}

		g_strfreev(tokens);

		if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
			sr_err("Device context malloc failed.");
			g_free(device);
			return NULL;
		}

		devc->device = device;

		sdi->priv = devc;
		sdi->driver = di;

		for (i = 0; i < 2; i++) {
			if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE,
			    i == 0 ? "CH1" : "CH2")))
				return NULL;
			sdi->probes = g_slist_append(sdi->probes, probe);
		}

		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);
	}

	g_dir_close(dir);

	return devices;
}

static GSList *hw_dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int fd;

	devc = sdi->priv;

	if ((fd = open(devc->device, O_RDWR)) == -1)
		return SR_ERR;

	devc->fd = fd;

	devc->scale = 1;

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	close(devc->fd);

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
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint64_t tmp_u64, p, q;
	double tmp_double;
	unsigned int i;
	int tmp_int, ret;
	const char *tmp_str, *channel;

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
		rigol_ds1xx2_send_data(devc->fd, ":TRIG:EDGE:SLOP %s\n",
				       tmp_u64 ? "POS" : "NEG");
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		tmp_double = g_variant_get_double(data);
		rigol_ds1xx2_send_data(devc->fd, ":TIM:OFFS %.9f\n", tmp_double);
		break;
	case SR_CONF_TIMEBASE:
		g_variant_get(data, "(tt)", &p, &q);
		tmp_int = -1;
		for (i = 0; i < ARRAY_SIZE(timebases); i++) {
			if (timebases[i][0] == p && timebases[i][1] == q) {
				tmp_int = i;
				break;
			}
		}
		if (tmp_int >= 0)
			rigol_ds1xx2_send_data(devc->fd, ":TIM:SCAL %.9f\n",
					(float)timebases[i][0] / timebases[i][1]);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		tmp_str = g_variant_get_string(data, NULL);
		if (!strcmp(tmp_str, "CH1"))
			channel = "CHAN1";
		else if (!strcmp(tmp_str, "CH2"))
			channel = "CHAN2";
		else if (!strcmp(tmp_str, "EXT"))
			channel = "EXT";
		else if (!strcmp(tmp_str, "AC Line"))
			channel = "ACL";
		else {
			ret = SR_ERR_ARG;
			break;
		}
		rigol_ds1xx2_send_data(devc->fd, ":TRIG:EDGE:SOUR %s\n", channel);
		break;
	case SR_CONF_VDIV:
		g_variant_get(data, "(tt)", &p, &q);
		tmp_int = -1;
		for (i = 0; i < ARRAY_SIZE(vdivs); i++) {
			if (vdivs[i][0] != p || vdivs[i][1] != q)
				continue;
			devc->scale = (float)vdivs[i][0] / vdivs[i][1];
			rigol_ds1xx2_send_data(devc->fd, ":CHAN0:SCAL %.3f\n",
					devc->scale);
			rigol_ds1xx2_send_data(devc->fd, ":CHAN1:SCAL %.3f\n",
					devc->scale);
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
				rigol_ds1xx2_send_data(devc->fd, ":CHAN0:COUP %s\n",
						coupling[i]);
				rigol_ds1xx2_send_data(devc->fd, ":CHAN1:COUP %s\n",
						coupling[i]);
				break;
			}
		}
		if (i == ARRAY_SIZE(coupling))
			ret = SR_ERR_ARG;
		break;
	default:
		sr_err("Unknown hardware capability: %d.", id);
		ret = SR_ERR_ARG;
		break;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi)
{
	GVariant *tuple, *rational[2];
	GVariantBuilder gvb;
	unsigned int i;

	(void)sdi;

	switch (key) {
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
		*data = g_variant_new_strv(trigger_sources,
				ARRAY_SIZE(trigger_sources));
		break;
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	struct dev_context *devc;
	char buf[256];
	int len;

	(void)cb_data;

	devc = sdi->priv;

	devc->num_frames = 0;

	sr_source_add(devc->fd, G_IO_IN, 50, rigol_ds1xx2_receive_data, (void *)sdi);

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, DRIVER_LOG_DOMAIN);

	/* Hardcoded to CH1 only. */
	devc->enabled_probes = g_slist_append(NULL, sdi->probes->data);
	rigol_ds1xx2_send_data(devc->fd, ":CHAN1:SCAL?\n");
	len = read(devc->fd, buf, sizeof(buf));
	buf[len] = 0;
	devc->scale = atof(buf);
	sr_dbg("Scale is %.3f.", devc->scale);
	rigol_ds1xx2_send_data(devc->fd, ":CHAN1:OFFS?\n");
	len = read(devc->fd, buf, sizeof(buf));
	buf[len] = 0;
	devc->offset = atof(buf);
	sr_dbg("Offset is %.6f.", devc->offset);
	rigol_ds1xx2_send_data(devc->fd, ":WAV:DATA?\n");

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;

	(void)cb_data;

	devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't stop acquisition.");
		return SR_ERR;
	}

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
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
