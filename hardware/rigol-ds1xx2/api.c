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

static const int hwcaps[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_TIMEBASE,
	SR_CONF_TRIGGER_SOURCE,
	SR_CONF_TRIGGER_SLOPE,
	SR_CONF_HORIZ_TRIGGERPOS,
	SR_CONF_VDIV,
	SR_CONF_COUPLING,
	0,
};

static const struct sr_rational timebases[] = {
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
	{ 0, 0},
};

static const struct sr_rational vdivs[] = {
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
	{ 0, 0 },
};

static const char *trigger_sources[] = {
	"CH1",
	"CH2",
	"EXT",
	"AC Line",
	NULL,
};

static const char *coupling[] = {
	"AC",
	"DC",
	"GND",
	NULL,
};

static const char *supported_models[] = {
	"DS1052E",
	"DS1102E",
	"DS1052D",
	"DS1102D"
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
	int len, num_tokens, fd, i;
	const gchar *delimiter = ",";
	gchar **tokens;
	const char *manufacturer, *model, *version;
	int num_models;
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

		num_models = sizeof(supported_models) / sizeof(supported_models[0]);

		for (i = 0; i < num_models; i++) {
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
	struct drv_context *drvc;

	drvc = di->priv;

	return drvc->instances;
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

static int config_set(int id, const void *value, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint64_t tmp_u64;
	float tmp_float;
	struct sr_rational tmp_rat;
	int ret, i, j;
	char *channel;

	devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE) {
		sr_err("Device inactive, can't set config options.");
		return SR_ERR;
	}

	ret = SR_OK;
	switch (id) {
	case SR_CONF_LIMIT_FRAMES:
		devc->limit_frames = *(const uint64_t *)value;
		break;
	case SR_CONF_TRIGGER_SLOPE:
		tmp_u64 = *(const int *)value;
		rigol_ds1xx2_send_data(devc->fd, ":TRIG:EDGE:SLOP %s\n",
				       tmp_u64 ? "POS" : "NEG");
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		tmp_float = *(const float *)value;
		rigol_ds1xx2_send_data(devc->fd, ":TIM:OFFS %.9f\n", tmp_float);
		break;
	case SR_CONF_TIMEBASE:
		tmp_rat = *(const struct sr_rational *)value;
		rigol_ds1xx2_send_data(devc->fd, ":TIM:SCAL %.9f\n",
				       (float)tmp_rat.p / tmp_rat.q);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		if (!strcmp(value, "CH1"))
			channel = "CHAN1";
		else if (!strcmp(value, "CH2"))
			channel = "CHAN2";
		else if (!strcmp(value, "EXT"))
			channel = "EXT";
		else if (!strcmp(value, "AC Line"))
			channel = "ACL";
		else {
			ret = SR_ERR_ARG;
			break;
		}
		rigol_ds1xx2_send_data(devc->fd, ":TRIG:EDGE:SOUR %s\n", channel);
		break;
	case SR_CONF_VDIV:
		/* TODO: Not supporting vdiv per channel yet. */
		tmp_rat = *(const struct sr_rational *)value;
		for (i = 0; vdivs[i].p && vdivs[i].q; i++) {
			if (vdivs[i].p == tmp_rat.p
					&& vdivs[i].q == tmp_rat.q) {
				devc->scale = (float)tmp_rat.p / tmp_rat.q;
				for (j = 0; j < 2; j++)
					rigol_ds1xx2_send_data(devc->fd,
					 ":CHAN%d:SCAL %.3f\n", j, devc->scale);
				break;
			}
		}
		if (vdivs[i].p == 0 && vdivs[i].q == 0)
			ret = SR_ERR_ARG;
		break;
	case SR_CONF_COUPLING:
		/* TODO: Not supporting coupling per channel yet. */
		for (i = 0; coupling[i]; i++) {
			if (!strcmp(value, coupling[i])) {
				for (j = 0; j < 2; j++)
					rigol_ds1xx2_send_data(devc->fd,
					  ":CHAN%d:COUP %s\n", j, coupling[i]);
				break;
			}
		}
		if (coupling[i] == 0)
			ret = SR_ERR_ARG;
		break;
	default:
		sr_err("Unknown hardware capability: %d.", id);
		ret = SR_ERR_ARG;
		break;
	}

	return ret;
}

static int config_list(int key, const void **data, const struct sr_dev_inst *sdi)
{

	(void)sdi;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = hwcaps;
		break;
	case SR_CONF_COUPLING:
		*data = coupling;
		break;
	case SR_CONF_VDIV:
		*data = vdivs;
		break;
	case SR_CONF_TIMEBASE:
		*data = timebases;
		break;
	case SR_CONF_TRIGGER_SOURCE:
		*data = trigger_sources;
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
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	char buf[256];
	int len;

	(void)cb_data;

	devc = sdi->priv;

	devc->num_frames = 0;

	sr_source_add(devc->fd, G_IO_IN, 50, rigol_ds1xx2_receive_data, (void *)sdi);

	/* Send header packet to the session bus. */
	packet.type = SR_DF_HEADER;
	packet.payload = (unsigned char *)&header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	sr_session_send(cb_data, &packet);

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
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
