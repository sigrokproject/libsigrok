/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

/* size of payloads sent across the session bus */
#define CHUNKSIZE (512 * 1024)

struct session_vdev {
	char *sessionfile;
	char *capturefile;
	struct zip *archive;
	struct zip_file *capfile;
	int bytes_read;
	uint64_t samplerate;
	int unitsize;
	int num_probes;
};

static GSList *dev_insts = NULL;
static const int hwcaps[] = {
	SR_HWCAP_CAPTUREFILE,
	SR_HWCAP_CAPTURE_UNITSIZE,
	0,
};

/**
 * TODO.
 *
 * @param dev_index TODO.
 */
static struct session_vdev *get_vdev_by_index(int dev_index)
{
	struct sr_dev_inst *sdi;
	struct session_vdev *vdev;

	/* TODO: Sanity checks on dev_index. */

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("session driver: %s: device instance with device "
		       "index %d was not found", __func__, dev_index);
		return NULL;
	}

	/* TODO: Is sdi->priv == NULL valid? */

	vdev = sdi->priv;

	return vdev;
}

/**
 * TODO.
 *
 * @param fd TODO.
 * @param revents TODO.
 * @param cb_data TODO.
 *
 * @return TODO.
 */
static int receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct session_vdev *vdev;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	GSList *l;
	void *buf;
	int ret, got_data;

	/* Avoid compiler warnings. */
	(void)fd;
	(void)revents;

	sr_dbg("session_driver: feed chunk");

	got_data = FALSE;
	for (l = dev_insts; l; l = l->next) {
		sdi = l->data;
		vdev = sdi->priv;
		if (!vdev)
			/* already done with this instance */
			continue;

		if (!(buf = g_try_malloc(CHUNKSIZE))) {
			sr_err("session driver: %s: buf malloc failed",
			       __func__);
			return FALSE; /* TODO: SR_ERR_MALLOC */
		}

		ret = zip_fread(vdev->capfile, buf, CHUNKSIZE);
		if (ret > 0) {
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
			g_free(vdev->capturefile);
			g_free(vdev);
			sdi->priv = NULL;
		}
	}

	if (!got_data) {
		packet.type = SR_DF_END;
		sr_session_send(cb_data, &packet);
	}

	return TRUE;
}

/* driver callbacks */
static int hw_cleanup(void);

/**
 * TODO.
 *
 * @param devinfo TODO.
 *
 * @return TODO.
 */
static int hw_init(void)
{

	return 0;
}

/**
 * TODO.
 */
static int hw_cleanup(void)
{
	GSList *l;

	for (l = dev_insts; l; l = l->next)
		sr_dev_inst_free(l->data);
	g_slist_free(dev_insts);
	dev_insts = NULL;

	sr_session_source_remove(-1);

	return SR_OK;
}

static int hw_dev_open(int dev_index)
{
	struct sr_dev_inst *sdi;

	sdi = sr_dev_inst_new(dev_index, SR_ST_INITIALIZING,
			      NULL, NULL, NULL);
	if (!sdi)
		return SR_ERR;

	if (!(sdi->priv = g_try_malloc0(sizeof(struct session_vdev)))) {
		sr_err("session driver: %s: sdi->priv malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	dev_insts = g_slist_append(dev_insts, sdi);

	return SR_OK;
}

static const void *hw_dev_info_get(int dev_index, int dev_info_id)
{
	struct session_vdev *vdev;
	void *info;

	if (dev_info_id != SR_DI_CUR_SAMPLERATE)
		return NULL;

	if (!(vdev = get_vdev_by_index(dev_index)))
		return NULL;

	info = &vdev->samplerate;

	return info;
}

static int hw_dev_status_get(int dev_index)
{
	/* Avoid compiler warnings. */
	(void)dev_index;

	if (sr_dev_list() != NULL)
		return SR_OK;
	else
		return SR_ERR;
}

/**
 * Get the list of hardware capabilities.
 *
 * @return A pointer to the (hardware) capabilities of this virtual session
 *         driver. This could be NULL, if no such capabilities exist.
 */
static const int *hw_hwcap_get_all(void)
{
	return hwcaps;
}

static int hw_dev_config_set(int dev_index, int hwcap, const void *value)
{
	struct session_vdev *vdev;
	const uint64_t *tmp_u64;

	if (!(vdev = get_vdev_by_index(dev_index)))
		return SR_ERR;

	switch (hwcap) {
	case SR_HWCAP_SAMPLERATE:
		tmp_u64 = value;
		vdev->samplerate = *tmp_u64;
		sr_info("session driver: setting samplerate to %" PRIu64,
		        vdev->samplerate);
		break;
	case SR_HWCAP_SESSIONFILE:
		vdev->sessionfile = g_strdup(value);
		sr_info("session driver: setting sessionfile to %s",
		        vdev->sessionfile);
		break;
	case SR_HWCAP_CAPTUREFILE:
		vdev->capturefile = g_strdup(value);
		sr_info("session driver: setting capturefile to %s",
		        vdev->capturefile);
		break;
	case SR_HWCAP_CAPTURE_UNITSIZE:
		tmp_u64 = value;
		vdev->unitsize = *tmp_u64;
		break;
	case SR_HWCAP_CAPTURE_NUM_PROBES:
		tmp_u64 = value;
		vdev->num_probes = *tmp_u64;
		break;
	default:
		sr_err("session driver: %s: unknown capability %d requested",
		       __func__, hwcap);
		return SR_ERR;
	}

	return SR_OK;
}

static int hw_dev_acquisition_start(int dev_index, void *cb_data)
{
	struct zip_stat zs;
	struct session_vdev *vdev;
	struct sr_datafeed_header *header;
	struct sr_datafeed_packet *packet;
	struct sr_datafeed_meta_logic meta;
	int ret;

	if (!(vdev = get_vdev_by_index(dev_index)))
		return SR_ERR;

	sr_info("session_driver: opening archive %s file %s", vdev->sessionfile,
		vdev->capturefile);

	if (!(vdev->archive = zip_open(vdev->sessionfile, 0, &ret))) {
		sr_err("session driver: Failed to open session file '%s': "
		       "zip error %d\n", vdev->sessionfile, ret);
		return SR_ERR;
	}

	if (zip_stat(vdev->archive, vdev->capturefile, 0, &zs) == -1) {
		sr_err("session driver: Failed to check capture file '%s' in "
		       "session file '%s'.", vdev->capturefile, vdev->sessionfile);
		return SR_ERR;
	}

	if (!(vdev->capfile = zip_fopen(vdev->archive, vdev->capturefile, 0))) {
		sr_err("session driver: Failed to open capture file '%s' in "
		       "session file '%s'.", vdev->capturefile, vdev->sessionfile);
		return SR_ERR;
	}

	/* freewheeling source */
	sr_session_source_add(-1, 0, 0, receive_data, cb_data);

	if (!(packet = g_try_malloc(sizeof(struct sr_datafeed_packet)))) {
		sr_err("session driver: %s: packet malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	if (!(header = g_try_malloc(sizeof(struct sr_datafeed_header)))) {
		sr_err("session driver: %s: header malloc failed", __func__);
		return SR_ERR_MALLOC;
	}

	/* Send header packet to the session bus. */
	packet->type = SR_DF_HEADER;
	packet->payload = (unsigned char *)header;
	header->feed_version = 1;
	gettimeofday(&header->starttime, NULL);
	sr_session_send(cb_data, packet);

	/* Send metadata about the SR_DF_LOGIC packets to come. */
	packet->type = SR_DF_META_LOGIC;
	packet->payload = &meta;
	meta.samplerate = vdev->samplerate;
	meta.num_probes = vdev->num_probes;
	sr_session_send(cb_data, packet);

	g_free(header);
	g_free(packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver session_driver = {
	.name = "session",
	.longname = "Session-emulating driver",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.dev_open = hw_dev_open,
	.dev_close = NULL,
	.dev_info_get = hw_dev_info_get,
	.dev_status_get = hw_dev_status_get,
	.hwcap_get_all = hw_hwcap_get_all,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = NULL,
};
