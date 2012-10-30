/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "agilent-dmm.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

static const int hwopts[] = {
	SR_HWOPT_CONN,
	SR_HWOPT_SERIALCOMM,
	0,
};

static const int hwcaps[] = {
	SR_HWCAP_MULTIMETER,
	SR_HWCAP_LIMIT_SAMPLES,
	SR_HWCAP_LIMIT_MSEC,
	SR_HWCAP_CONTINUOUS,
	0,
};

static const char *probe_names[] = {
	"Probe",
	NULL,
};

extern const struct agdmm_job agdmm_jobs_u123x[];
extern const struct agdmm_recv agdmm_recvs_u123x[];
extern const struct agdmm_job agdmm_jobs_u125x[];
extern const struct agdmm_recv agdmm_recvs_u125x[];

static const struct agdmm_profile supported_agdmm[] = {
	{ AGILENT_U1231A, "U1231A", 9600, agdmm_jobs_u123x, agdmm_recvs_u123x },
	{ AGILENT_U1232A, "U1232A", 9600, agdmm_jobs_u123x, agdmm_recvs_u123x },
	{ AGILENT_U1233A, "U1233A", 9600, agdmm_jobs_u123x, agdmm_recvs_u123x },
	{ AGILENT_U1251A, "U1251A", 9600, agdmm_jobs_u125x, agdmm_recvs_u125x },
	{ AGILENT_U1252A, "U1252A", 9600, agdmm_jobs_u125x, agdmm_recvs_u125x },
	{ AGILENT_U1253A, "U1253A", 9600, agdmm_jobs_u125x, agdmm_recvs_u125x },
	{ 0, NULL, 0, NULL, NULL }
};

SR_PRIV struct sr_dev_driver agdmm_driver_info;
static struct sr_dev_driver *di = &agdmm_driver_info;

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

static int serial_readline(int fd, char **buf, int *buflen, uint64_t timeout_ms)
{
	uint64_t start;
	int maxlen, len;

	timeout_ms *= 1000;
	start = g_get_monotonic_time();

	maxlen = *buflen;
	*buflen = len = 0;
	while(1) {
		len = maxlen - *buflen - 1;
		if (len < 1)
			break;
		len = serial_read(fd, *buf + *buflen, len);
		if (len > 0) {
			*buflen += len;
			*(*buf + *buflen) = '\0';
			if (*buflen > 0 && *(*buf + *buflen - 1) == '\n')
				/* End of line */
				break;
		}
		if (g_get_monotonic_time() - start > timeout_ms)
			/* Timeout */
			break;
		g_usleep(2000);
	}

	/* Strip CRLF */
	while (*buflen) {
		if (*(*buf + *buflen - 1) == '\r' || *(*buf + *buflen - 1) == '\n')
			*(*buf + --*buflen) = '\0';
		else
			break;
	}
	if (*buflen)
		sr_dbg("Received '%s'.", *buf);

	return SR_OK;
}

static GSList *hw_scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_hwopt *opt;
	struct sr_probe *probe;
	GSList *l, *devices;
	int len, fd, i;
	const char *conn, *serialcomm;
	char *buf, **tokens;

	drvc = di->priv;
	drvc->instances = NULL;

	devices = NULL;
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
	if (!conn || !serialcomm)
		return NULL;

	if ((fd = serial_open(conn, O_RDWR|O_NONBLOCK)) == -1) {
		sr_err("Unable to open %s: %s.", conn, strerror(errno));
		return NULL;
	}

	if (serial_set_paramstr(fd, serialcomm) != SR_OK) {
		sr_err("Unable to set serial parameters: %s.",
		       strerror(errno));
		return NULL;
	}

	serial_flush(fd);
	if (serial_write(fd, "*IDN?\r\n", 7) == -1) {
		sr_err("Unable to send identification string: %s.",
		       strerror(errno));
		return NULL;
	}

	len = 128;
	buf = g_try_malloc(len);
	serial_readline(fd, &buf, &len, 150);
	if (!len)
		return NULL;

	tokens = g_strsplit(buf, ",", 4);
	if (!strcmp("Agilent Technologies", tokens[0])
			&& tokens[2] && tokens[3]) {
		for (i = 0; supported_agdmm[i].model; i++) {
			if (strcmp(supported_agdmm[i].modelname, tokens[1]))
				continue;
			if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, tokens[0],
					tokens[1], tokens[3])))
				return NULL;
			if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
				sr_dbg("failed to malloc devc");
				return NULL;
			}
			devc->profile = &supported_agdmm[i];
			devc->serial = sr_serial_dev_inst_new(conn, -1);
			devc->cur_mq = -1;
			sdi->priv = devc;
			sdi->driver = di;
			if (!(probe = sr_probe_new(0, SR_PROBE_ANALOG, TRUE, "P1")))
				return NULL;
			sdi->probes = g_slist_append(sdi->probes, probe);
			drvc->instances = g_slist_append(drvc->instances, sdi);
			devices = g_slist_append(devices, sdi);
			break;
		}
	}
	g_strfreev(tokens);
	g_free(buf);

	serial_close(fd);

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

	devc->serial->fd = serial_open(devc->serial->port, O_RDWR | O_NONBLOCK);
	if (devc->serial->fd == -1) {
		sr_err("Couldn't open serial port '%s'.",
		       devc->serial->port);
		return SR_ERR;
	}
	serial_set_params(devc->serial->fd, devc->profile->serial_speed, 8,
			SERIAL_PARITY_NONE, 1, 0);
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
	case SR_HWCAP_LIMIT_MSEC:
		/* TODO: not yet implemented */
		if (*(const uint64_t *)value == 0) {
			sr_err("LIMIT_MSEC can't be 0.");
			return SR_ERR;
		}
		devc->limit_msec = *(const uint64_t *)value;
		sr_dbg("Setting time limit to %" PRIu64 "ms.",
		       devc->limit_msec);
		break;
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
	sr_source_add(devc->serial->fd, G_IO_IN, 100, agdmm_receive_data, (void *)sdi);

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

SR_PRIV struct sr_dev_driver agdmm_driver_info = {
	.name = "agilent-dmm",
	.longname = "Agilent U12xx series DMMs",
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
