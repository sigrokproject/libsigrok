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
#include "protocol.h"

#define SERIALCOMM "2400/8n1"

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

SR_PRIV struct sr_dev_driver tekpower_dmm_driver_info;
static struct sr_dev_driver *di = &tekpower_dmm_driver_info;

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
		return SR_ERR_MALLOC;
	}

	di->priv = drvc;

	return SR_OK;
}

typedef gboolean (*packet_valid_t)(const uint8_t *buf);

/**
 * Try to find a valid packet in a serial data stream.
 *
 * @param serial Previously initialized serial port structure.
 * @param buf Buffer containing the bytes to write.
 * @param count Size of the buffer.
 * @param packet_size Size, in bytes, of a valid packet.
 * @param is_valid Callback that assesses whether the packet is valid or not.
 * @param timeout_ms The timeout after which, if no packet is detected, to
 *                   abort scanning.
 * @param baudrate The baudrate of the serial port. This parameter is not
 *                 critical, but it helps fine tune the serial port polling
 *                 delay.
 *
 * @return SR_OK if a valid packet is found within the given timeout,
 *         SR_ERR upon failure.
 */
static int serial_stream_detect(struct sr_serial_dev_inst *serial,
				uint8_t *buf, size_t *buflen,
				size_t packet_size, packet_valid_t is_valid,
				uint64_t timeout_ms, int baudrate)
{
	uint64_t start, time, byte_delay_us;
	size_t ibuf, i, maxlen;
	int len;

	maxlen = *buflen;

	sr_dbg("Detecting packets on FD %d (timeout = %" PRIu64
	       "ms, baudrate = %d).", serial->fd, timeout_ms, baudrate);

	if (maxlen < (packet_size / 2) ) {
		sr_err("Buffer size must be at least twice the packet size.");
		return SR_ERR;
	}

	timeout_ms *= 1000;

	/* Assume 8n1 transmission. That is 10 bits for every byte. */
	byte_delay_us = 10 * (1000000 / baudrate);
	start = g_get_monotonic_time();

	i = ibuf = len = 0;
	while (ibuf < maxlen) {
		len = serial_read(serial, &buf[ibuf], 1);
		if (len > 0) {
			ibuf += len;
		} else if (len == 0) {
			sr_spew("Error: Only read 0 bytes.");
		} else {
			/* Error reading byte, but continuing anyway. */
		}
		if ((ibuf - i) >= packet_size) {
			/* We have at least a packet's worth of data. */
			if (is_valid(&buf[i])) {
				time = g_get_monotonic_time() - start;
				time /= 1000;
				sr_spew("Found valid %d-byte packet after "
					"%" PRIu64 "ms.", (ibuf - i), time);
				*buflen = ibuf;
				return SR_OK;
			} else {
				sr_spew("Got %d bytes, but not a valid "
					"packet.", (ibuf - i));
			}
			/* Not a valid packet. Continue searching. */
			i++;
		}
		if (g_get_monotonic_time() - start > timeout_ms) {
			/* Timeout */
			sr_dbg("Detection timed out after %dms.", timeout_ms);
			break;
		}
		g_usleep(byte_delay_us);
	}

	*buflen = ibuf;

	sr_err("Didn't find a valid packet (read %d bytes).", *buflen);

	return SR_ERR;
}

static GSList *lcd14_scan(const char *conn, const char *serialcomm)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_probe *probe;
	struct sr_serial_dev_inst *serial;
	GSList *devices;
	int dropped, ret;
	size_t len;
	uint8_t buf[128];

	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

	if (serial_open(serial, O_RDONLY | O_NONBLOCK) != SR_OK)
		return NULL;

	sr_info("Probing port %s readonly.", conn);

	drvc = di->priv;
	devices = NULL;
	serial_flush(serial);

	/*
	 * There's no way to get an ID from the multimeter. It just sends data
	 * periodically, so the best we can do is check if the packets match
	 * the expected format.
	 */

	/* Let's get a bit of data and see if we can find a packet. */
	len = sizeof(buf);

	ret = serial_stream_detect(serial, buf, &len, FS9721_PACKET_SIZE,
				   sr_fs9721_packet_valid, 1000, 2400);
	if (ret != SR_OK)
		goto scan_cleanup;

	/*
	 * If we dropped more than two packets worth of data, something is
	 * wrong. We shouldn't quit however, since the dropped bytes might be
	 * just zeroes at the beginning of the stream. Those can occur as a
	 * combination of the nonstandard cable that ships with this device and
	 * the serial port or USB to serial adapter.
	 */
	dropped = len - FS9721_PACKET_SIZE;
	if (dropped > 2 * FS9721_PACKET_SIZE)
		sr_warn("Had to drop too much data.");

	sr_info("Found device on port %s.", conn);

	if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, "TekPower",
				    "TP4000ZC", "")))
		goto scan_cleanup;

	if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
		sr_err("Device context malloc failed.");
		goto scan_cleanup;
	}

	devc->serial = serial;

	sdi->priv = devc;
	sdi->driver = di;
	if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
		goto scan_cleanup;
	sdi->probes = g_slist_append(sdi->probes, probe);
	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

scan_cleanup:
	serial_close(serial);

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
		devices = lcd14_scan(conn, serialcomm);
	} else {
		/* Try the default. */
		devices = lcd14_scan(conn, SERIALCOMM);
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

	if (serial_open(devc->serial, O_RDONLY) != SR_OK)
		return SR_ERR;

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
		serial_close(devc->serial);
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
	(void)sdi;

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

	/*
	 * Reset the number of samples to take. If we've already collected our
	 * quota, but we start a new session, and don't reset this, we'll just
	 * quit without acquiring any new samples.
	 */
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

	/* Poll every 50ms, or whenever some data comes in. */
	sr_source_add(devc->serial->fd, G_IO_IN, 50,
		      tekpower_dmm_receive_data, (void *)sdi);

	return SR_OK;
}

static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
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

SR_PRIV struct sr_dev_driver tekpower_dmm_driver_info = {
	.name = "tekpower-dmm",
	.longname = "TekPower/Digitek TP4000ZC/DT4000ZC DMM",
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
