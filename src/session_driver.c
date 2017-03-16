/*
 * This file is part of the libsigrok project.
 *
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

#include <config.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <zip.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "virtual-session"

/* size of payloads sent across the session bus */
/** @cond PRIVATE */
#define CHUNKSIZE (4 * 1024 * 1024)
/** @endcond */

SR_PRIV struct sr_dev_driver session_driver_info;

struct session_vdev {
	char *sessionfile;
	char *capturefile;
	struct zip *archive;
	struct zip_file *capfile;
	int bytes_read;
	uint64_t samplerate;
	int unitsize;
	int num_channels;
	int num_analog_channels;
	int cur_analog_channel;
	GArray *analog_channels;
	int cur_chunk;
	gboolean finished;
};

static const uint32_t devopts[] = {
	SR_CONF_CAPTUREFILE | SR_CONF_SET,
	SR_CONF_CAPTURE_UNITSIZE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_NUM_LOGIC_CHANNELS | SR_CONF_SET,
	SR_CONF_NUM_ANALOG_CHANNELS | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SESSIONFILE | SR_CONF_SET,
};

static gboolean stream_session_data(struct sr_dev_inst *sdi)
{
	struct session_vdev *vdev;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct zip_stat zs;
	int ret, got_data;
	char capturefile[16];
	void *buf;

	got_data = FALSE;
	vdev = sdi->priv;

	if (!vdev->capfile) {
		/* No capture file opened yet, or finished with the last
		 * chunked one. */
		if (vdev->capturefile && (vdev->cur_chunk == 0)) {
			/* capturefile is always the unchunked base name. */
			if (zip_stat(vdev->archive, vdev->capturefile, 0, &zs) != -1) {
				/* No chunks, just a single capture file. */
				vdev->cur_chunk = 0;
				if (!(vdev->capfile = zip_fopen(vdev->archive,
						vdev->capturefile, 0)))
					return FALSE;
				sr_dbg("Opened %s.", vdev->capturefile);
			} else {
				/* Try as first chunk filename. */
				snprintf(capturefile, 15, "%s-1", vdev->capturefile);
				if (zip_stat(vdev->archive, capturefile, 0, &zs) != -1) {
					vdev->cur_chunk = 1;
					if (!(vdev->capfile = zip_fopen(vdev->archive,
							capturefile, 0)))
						return FALSE;
					sr_dbg("Opened %s.", capturefile);
				} else {
					sr_err("No capture file '%s' in " "session file '%s'.",
							vdev->capturefile, vdev->sessionfile);
					return FALSE;
				}
			}
		} else {
			/* Capture data is chunked, advance to the next chunk. */
			vdev->cur_chunk++;
			snprintf(capturefile, 15, "%s-%d", vdev->capturefile,
					vdev->cur_chunk);
			if (zip_stat(vdev->archive, capturefile, 0, &zs) != -1) {
				if (!(vdev->capfile = zip_fopen(vdev->archive,
						capturefile, 0)))
					return FALSE;
				sr_dbg("Opened %s.", capturefile);
			} else if (vdev->cur_analog_channel < vdev->num_analog_channels) {
				vdev->capturefile = g_strdup_printf("analog-1-%d",
						vdev->num_channels + vdev->cur_analog_channel + 1);
				vdev->cur_analog_channel++;
				vdev->cur_chunk = 0;
				return TRUE;
			} else {
				/* We got all the chunks, finish up. */
				return FALSE;
			}
		}
	}

	buf = g_malloc(CHUNKSIZE);

	/* unitsize is not defined for purely analog session files. */
	if (vdev->unitsize)
		ret = zip_fread(vdev->capfile, buf,
				CHUNKSIZE / vdev->unitsize * vdev->unitsize);
	else
		ret = zip_fread(vdev->capfile, buf, CHUNKSIZE);

	if (ret > 0) {
		got_data = TRUE;
		if (vdev->cur_analog_channel != 0) {
			packet.type = SR_DF_ANALOG;
			packet.payload = &analog;
			/* TODO: Use proper 'digits' value for this device (and its modes). */
			sr_analog_init(&analog, &encoding, &meaning, &spec, 2);
			analog.meaning->channels = g_slist_prepend(NULL,
					g_array_index(vdev->analog_channels,
						struct sr_channel *, vdev->cur_analog_channel - 1));
			analog.num_samples = ret / sizeof(float);
			analog.meaning->mq = SR_MQ_VOLTAGE;
			analog.meaning->unit = SR_UNIT_VOLT;
			analog.meaning->mqflags = SR_MQFLAG_DC;
			analog.data = (float *) buf;
		} else {
			if (ret % vdev->unitsize != 0)
				sr_warn("Read size %d not a multiple of the"
					" unit size %d.", ret, vdev->unitsize);
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = ret;
			logic.unitsize = vdev->unitsize;
			logic.data = buf;
		}
		vdev->bytes_read += ret;
		sr_session_send(sdi, &packet);
	} else {
		/* done with this capture file */
		zip_fclose(vdev->capfile);
		vdev->capfile = NULL;
		if (vdev->cur_chunk != 0) {
			/* There might be more chunks, so don't fall through
			 * to the SR_DF_END here. */
			got_data = TRUE;
		}
	}
	g_free(buf);

	return got_data;
}

static int receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct session_vdev *vdev;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	vdev = sdi->priv;

	if (!vdev->finished && !stream_session_data(sdi))
		vdev->finished = TRUE;
	if (!vdev->finished)
		return G_SOURCE_CONTINUE;

	if (vdev->capfile) {
		zip_fclose(vdev->capfile);
		vdev->capfile = NULL;
	}
	if (vdev->archive) {
		zip_discard(vdev->archive);
		vdev->archive = NULL;
	}

	std_session_send_df_end(sdi);

	return G_SOURCE_REMOVE;
}

/* driver callbacks */

static int dev_clear(const struct sr_dev_driver *di)
{
	struct drv_context *drvc;
	GSList *l;

	drvc = di->context;
	for (l = drvc->instances; l; l = l->next)
		sr_dev_inst_free(l->data);
	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di;
	struct drv_context *drvc;
	struct session_vdev *vdev;

	di = sdi->driver;
	drvc = di->context;
	vdev = g_malloc0(sizeof(struct session_vdev));
	sdi->priv = vdev;
	drvc->instances = g_slist_append(drvc->instances, sdi);

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	const struct session_vdev *const vdev = sdi->priv;
	g_free(vdev->sessionfile);
	g_free(vdev->capturefile);

	g_free(sdi->priv);
	sdi->priv = NULL;

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct session_vdev *vdev;

	(void)cg;

	if (!sdi)
		return SR_ERR;

	vdev = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(vdev->samplerate);
		break;
	case SR_CONF_CAPTURE_UNITSIZE:
		*data = g_variant_new_uint64(vdev->unitsize);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct session_vdev *vdev;

	(void)cg;

	vdev = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		vdev->samplerate = g_variant_get_uint64(data);
		sr_info("Setting samplerate to %" PRIu64 ".", vdev->samplerate);
		break;
	case SR_CONF_SESSIONFILE:
		g_free(vdev->sessionfile);
		vdev->sessionfile = g_strdup(g_variant_get_string(data, NULL));
		sr_info("Setting sessionfile to '%s'.", vdev->sessionfile);
		break;
	case SR_CONF_CAPTUREFILE:
		g_free(vdev->capturefile);
		vdev->capturefile = g_strdup(g_variant_get_string(data, NULL));
		sr_info("Setting capturefile to '%s'.", vdev->capturefile);
		break;
	case SR_CONF_CAPTURE_UNITSIZE:
		vdev->unitsize = g_variant_get_uint64(data);
		break;
	case SR_CONF_NUM_LOGIC_CHANNELS:
		vdev->num_channels = g_variant_get_int32(data);
		break;
	case SR_CONF_NUM_ANALOG_CHANNELS:
		vdev->num_analog_channels = g_variant_get_int32(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	(void)sdi;
	(void)cg;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct session_vdev *vdev;
	int ret;
	GSList *l;
	struct sr_channel *ch;

	vdev = sdi->priv;
	vdev->bytes_read = 0;
	vdev->cur_analog_channel = 0;
	vdev->analog_channels = g_array_sized_new(FALSE, FALSE,
			sizeof(struct sr_channel *), vdev->num_analog_channels);
	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type == SR_CHANNEL_ANALOG)
			g_array_append_val(vdev->analog_channels, ch);
	}
	vdev->cur_chunk = 0;
	vdev->finished = FALSE;

	sr_info("Opening archive %s file %s", vdev->sessionfile,
		vdev->capturefile);

	if (!(vdev->archive = zip_open(vdev->sessionfile, 0, &ret))) {
		sr_err("Failed to open session file '%s': "
		       "zip error %d.", vdev->sessionfile, ret);
		return SR_ERR;
	}

	std_session_send_df_header(sdi);

	/* freewheeling source */
	sr_session_source_add(sdi->session, -1, 0, 0, receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct session_vdev *vdev;

	vdev = sdi->priv;

	vdev->finished = TRUE;

	return SR_OK;
}

/** @private */
SR_PRIV struct sr_dev_driver session_driver = {
	.name = "virtual-session",
	.longname = "Session-emulating driver",
	.api_version = 1,
	.init = std_init,
	.cleanup = dev_clear,
	.scan = NULL,
	.dev_list = NULL,
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
