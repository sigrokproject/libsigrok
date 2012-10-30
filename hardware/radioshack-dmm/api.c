/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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

#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "radioshack-dmm.h"

static const int hwopts[] = {
	SR_HWOPT_CONN,
	SR_HWOPT_SERIALCOMM,
	0,
};

static const int hwcaps[] = {
	SR_HWCAP_MULTIMETER,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_CONTINUOUS,
	0,
};

static const char *probe_names[] = {
	"Probe",
	NULL,
};

SR_PRIV struct sr_dev_driver radioshackdmm_driver_info;
static struct sr_dev_driver *di = &radioshackdmm_driver_info;

static const struct radioshackdmm_profile supported_radioshackdmm[] = {
	{ RADIOSHACK_22_812, "22-812", 100 },
};

/* Properly close and free all devices. */
static int clear_instances(void)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *l;

	if (!(drvc = di->priv))
		return SR_OK;

	drvc = di->priv;
	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data))
			continue;
		if (!(devc = sdi->priv))
			continue;
		sr_serial_dev_inst_free(devc->serial);
		sr_dev_inst_free(sdi);
	}
	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return SR_OK;
}

static int hw_init(void)
{
	struct drv_context *drvc;

	if (!(drvc = g_try_malloc0(sizeof(struct drv_context)))) {
		sr_err("Driver context malloc failed.");
		return SR_ERR;
	}

	di->priv = drvc;

	return SR_OK;
}

static GSList *rs_22_812_scan(const char *conn, const char *serialcomm)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_probe *probe;
	GSList *devices;
	int fd, retry;
	size_t len;
	char buf[128], *b;

	if ((fd = serial_open(conn, O_RDONLY|O_NONBLOCK)) == -1) {
		sr_err("Unable to open %s: %s.", conn, strerror(errno));
		return NULL;
	}
	if (serial_set_paramstr(fd, serialcomm) != SR_OK) {
		sr_err("Unable to set serial parameters.");
		return NULL;
	}

	sr_info("Probing port %s readonly.", conn);

	drvc = di->priv;
	b = buf;
	retry = 0;
	devices = NULL;
	/* There's no way to get an ID from the multimeter. It just sends data
	 * periodically, so the best we can do is check if the packets match the
	 * expected format. */
	while (!devices && retry < 3)
	{
		size_t i;
		size_t good_packets = 0;
		retry++;
		serial_flush(fd);

		/* Let's get a bit of data and see if we can find a packet */
		len = sizeof(buf);
		serial_readline(fd, &b, &len, 250);
		if( (len == 0) || (len < RS_22_812_PACKET_SIZE) ) {
			/* Not enough data received, is the DMM connected ? */
			continue;
		}

		/* Let's treat our buffer like a stream, and find any
		 * valid packets */
		for( i = 0; i < len - RS_22_812_PACKET_SIZE + 1;
		    /* don't increment i here */ )
		{
			const rs_22_812_packet *packet = (void *)(&buf[i]);
			if( !rs_22_812_is_packet_valid(packet) ){
				i++;
				continue;
			}
			good_packets++;
			i += RS_22_812_PACKET_SIZE;
		}

		/* If we dropped more than two packets worth of data, something
		 * is wrong */
		size_t dropped = len - (good_packets * RS_22_812_PACKET_SIZE);
		if(dropped > 2 * RS_22_812_PACKET_SIZE)
			continue;

		/* Let's see if we have anything good */
		if (good_packets == 0)
			continue;

		sr_info("Found RS 22-812 on port %s.", conn);

		if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, "RadioShack",
					    "22-812", "")))
			return NULL;
		if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
			sr_err("Device context malloc failed.");
			return NULL;
		}

		/* devc->profile = RADIOSHACK_22_812; */
		devc->serial = sr_serial_dev_inst_new(conn, -1);
		devc->serialcomm = g_strdup(serialcomm);

		sdi->priv = devc;
		sdi->driver = di;
		if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
			return NULL;
		sdi->probes = g_slist_append(sdi->probes, probe);
		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);
		break;
	}
	serial_close(fd);

	return devices;
}

static GSList *hw_scan(GSList *options)
{
	struct sr_hwopt *opt;
	GSList *l, *devices;
	const char *conn, *serialcomm;

	conn = serialcomm = NULL;
	for (l = options; l; l = l->next) {
		opt = l->data;
		switch (opt->hwopt) {
		case SR_HWOPT_CONN:
			conn = opt->value;
			break;
		case SR_HWOPT_SERIALCOMM:
			serialcomm = opt->value;
			break;
		}
	}
	if (!conn)
		return NULL;

	if (serialcomm) {
		/* Use the provided comm specs. */
		devices = rs_22_812_scan(conn, serialcomm);
	} else {
		/* Then try the default 4800 8n1 */
		devices = rs_22_812_scan(conn, "4800/8n1");
	}

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

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	devc->serial->fd = serial_open(devc->serial->port, O_RDONLY);
	if (devc->serial->fd == -1) {
		sr_err("Couldn't open serial port '%s'.",
		       devc->serial->port);
		return SR_ERR;
	}
	if (serial_set_paramstr(devc->serial->fd, devc->serialcomm) != SR_OK) {
		sr_err("Unable to set serial parameters.");
		return SR_ERR;
	}
	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	if (devc->serial && devc->serial->fd != -1) {
		serial_close(devc->serial->fd);
		devc->serial->fd = -1;
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

static int hw_cleanup(void)
{
	clear_instances();

	return SR_OK;
}

static int hw_info_get(int info_id, const void **data,
       const struct sr_dev_inst *sdi)
{
	(void)sdi; /* Does nothing. prevents "unused parameter" warning */

	switch (info_id) {
	case SR_DI_HWOPTS:
		*data = hwopts;
		break;
	case SR_DI_HWCAPS:
		*data = hwcaps;
		break;
	case SR_DI_NUM_PROBES:
		*data = GINT_TO_POINTER(1);
		break;
	case SR_DI_PROBE_NAMES:
		*data = probe_names;
		break;
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
		const void *value)
{
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	switch (hwcap) {
	case SR_HWCAP_LIMIT_SAMPLES:
		devc->limit_samples = *(const uint64_t *)value;
		sr_dbg("Setting sample limit to %" PRIu64 ".",
		       devc->limit_samples);
		break;
	default:
		sr_err("Unknown capability: %d.", hwcap);
		return SR_ERR;
		break;
	}

	return SR_OK;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct sr_datafeed_meta_analog meta;
	struct dev_context *devc;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	sr_dbg("Starting acquisition.");

	devc->cb_data = cb_data;

	/* Reset the number of samples to take. If we've already collected our
	 * quota, but we start a new session, and don't reset this, we'll just
	 * quit without aquiring any new samples */
	devc->num_samples = 0;

	/* Send header packet to the session bus. */
	sr_dbg("Sending SR_DF_HEADER.");
	packet.type = SR_DF_HEADER;
	packet.payload = (uint8_t *)&header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	sr_session_send(devc->cb_data, &packet);

	/* Send metadata about the SR_DF_ANALOG packets to come. */
	sr_dbg("Sending SR_DF_META_ANALOG.");
	packet.type = SR_DF_META_ANALOG;
	packet.payload = &meta;
	meta.num_probes = 1;
	sr_session_send(devc->cb_data, &packet);

	/* Poll every 100ms, or whenever some data comes in. */
	sr_source_add(devc->serial->fd, G_IO_IN, 50,
		      radioshack_receive_data, (void *)sdi );

	return SR_OK;
}

static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct sr_datafeed_packet packet;
	struct dev_context *devc;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	if (!(devc = sdi->priv)) {
		sr_err("sdi->priv was NULL.");
		return SR_ERR_BUG;
	}

	sr_dbg("Stopping acquisition.");

	sr_source_remove(devc->serial->fd);
	hw_dev_close((struct sr_dev_inst *)sdi);

	/* Send end packet to the session bus. */
	sr_dbg("Sending SR_DF_END.");
	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver radioshackdmm_driver_info = {
	.name = "radioshack-dmm",
	.longname = "Radioshack 22-812/22-039 DMMs",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = clear_instances,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
