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

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/time.h>
#include <zip.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "virtual-session"

/* size of payloads sent across the session bus */
/** @cond PRIVATE */
#define CHUNKSIZE (512 * 1024)
/** @endcond */

struct session_vdev {
	char *sessionfile;
	char *capturefile;
	struct zip *archive;
	struct zip_file *capfile;
	int bytes_read;
	uint64_t samplerate;
	int unitsize;
	int num_probes;
	int cur_chunk;
	gboolean finished;
};

static GSList *dev_insts = NULL;
static const int hwcaps[] = {
	SR_CONF_CAPTUREFILE,
	SR_CONF_CAPTURE_UNITSIZE,
	SR_CONF_SAMPLERATE,
};

static int receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct session_vdev *vdev;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct zip_stat zs;
	GSList *l;
	int ret, got_data;
	char capturefile[16];
	void *buf;

	(void)fd;
	(void)revents;

	got_data = FALSE;
	for (l = dev_insts; l; l = l->next) {
		sdi = l->data;
		vdev = sdi->priv;
		if (vdev->finished)
			/* Already done with this instance. */
			continue;

		if (!vdev->capfile) {
			/* No capture file opened yet, or finished with the last
			 * chunked one. */
			if (vdev->cur_chunk == 0) {
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
				} else {
					/* We got all the chunks, finish up. */
					vdev->finished = TRUE;
					continue;
				}
			}
		}

		if (!(buf = g_try_malloc(CHUNKSIZE))) {
			sr_err("%s: buf malloc failed", __func__);
			return FALSE;
		}

		ret = zip_fread(vdev->capfile, buf,
				CHUNKSIZE / vdev->unitsize * vdev->unitsize);
		if (ret > 0) {
			if (ret % vdev->unitsize != 0)
				sr_warn("Read size %d not a multiple of the"
					" unit size %d.", ret, vdev->unitsize);
			got_data = TRUE;
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = ret;
			logic.unitsize = vdev->unitsize;
			logic.data = buf;
			vdev->bytes_read += ret;
			sr_session_send(cb_data, &packet);
		} else {
			/* done with this capture file */
			zip_fclose(vdev->capfile);
			vdev->capfile = NULL;
			if (vdev->cur_chunk == 0) {
				/* It was the only file. */
				vdev->finished = TRUE;
			} else {
				/* There might be more chunks, so don't fall through
				 * to the SR_DF_END here. */
				g_free(buf);
				return TRUE;
			}
		}
		g_free(buf);
	}

	if (!got_data) {
		packet.type = SR_DF_END;
		sr_session_send(cb_data, &packet);
		sr_session_source_remove(-1);
	}

	return TRUE;
}

/* driver callbacks */

static int init(struct sr_context *sr_ctx)
{
	(void)sr_ctx;

	return SR_OK;
}

static int dev_clear(void)
{
	GSList *l;

	for (l = dev_insts; l; l = l->next)
		sr_dev_inst_free(l->data);
	g_slist_free(dev_insts);
	dev_insts = NULL;

	return SR_OK;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct session_vdev *vdev;

	vdev = g_try_malloc0(sizeof(struct session_vdev));
	sdi->priv = vdev;
	dev_insts = g_slist_append(dev_insts, sdi);

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

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *channel_group)
{
	struct session_vdev *vdev;

	(void)channel_group;

	switch (id) {
	case SR_CONF_SAMPLERATE:
		if (sdi) {
			vdev = sdi->priv;
			*data = g_variant_new_uint64(vdev->samplerate);
		} else
			return SR_ERR;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *channel_group)
{
	struct session_vdev *vdev;

	(void)channel_group;

	vdev = sdi->priv;

	switch (id) {
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
	case SR_CONF_NUM_LOGIC_PROBES:
		vdev->num_probes = g_variant_get_uint64(data);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *channel_group)
{
	(void)sdi;
	(void)channel_group;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct session_vdev *vdev;
	int ret;

	vdev = sdi->priv;

	vdev->bytes_read = 0;
	vdev->cur_chunk = 0;
	vdev->finished = FALSE;

	sr_info("Opening archive %s file %s", vdev->sessionfile,
		vdev->capturefile);

	if (!(vdev->archive = zip_open(vdev->sessionfile, 0, &ret))) {
		sr_err("Failed to open session file '%s': "
		       "zip error %d.", vdev->sessionfile, ret);
		return SR_ERR;
	}

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* freewheeling source */
	sr_session_source_add(-1, 0, 0, receive_data, cb_data);

	return SR_OK;
}

/** @private */
SR_PRIV struct sr_dev_driver session_driver = {
	.name = "virtual-session",
	.longname = "Session-emulating driver",
	.api_version = 1,
	.init = init,
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
	.dev_acquisition_stop = NULL,
	.priv = NULL,
};
