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
#include "config.h"
#include "fluke-dmm.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>


SR_PRIV struct sr_dev_driver driver_info;
static struct sr_dev_driver *di = &driver_info;

static const struct flukedmm_profile supported_flukedmm[] = {
	{ FLUKE_187, "187" },
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
		sr_err("fluke-dmm: driver context malloc failed.");
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
			if (*buflen > 0 && *(*buf + *buflen - 1) == '\r')
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
		sr_dbg("fluke-dmm: received '%s'", *buf);

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
	if (!conn) {
		sr_dbg("fluke-dmm: no serial port provided");
		return NULL;
	}
	if (!serialcomm) {
		sr_dbg("fluke-dmm: no serial communication parameters provided");
		return NULL;
	}

	if ((fd = serial_open(conn, O_RDWR|O_NONBLOCK)) == -1) {
		sr_err("fluke-dmm: unable to open %s: %s", conn, strerror(errno));
		return NULL;
	}

	if (serial_set_paramstr(fd, serialcomm) != SR_OK) {
		sr_err("fluke-dmm: unable to set serial parameters: %s",
				strerror(errno));
		return NULL;
	}

	serial_flush(fd);
	if (serial_write(fd, "ID\r", 3) == -1) {
		sr_err("fluke-dmm: unable to send identification string: %s",
				strerror(errno));
		return NULL;
	}

	len = 128;
	buf = g_try_malloc(len);
	serial_readline(fd, &buf, &len, 150);
	if (!len)
		return NULL;

	if (len < 2 || buf[0] != '0' || buf[1] != '\r') {
		sr_err("fluke-dmm: invalid response to identification string");
		return NULL;
	}

	tokens = g_strsplit(buf + 2, ",", 3);
	if (!strncmp("FLUKE", tokens[0], 5)
			&& tokens[1] && tokens[2]) {
		for (i = 0; supported_flukedmm[i].model; i++) {
			if (strcmp(supported_flukedmm[i].modelname, tokens[0]))
				continue;
			if (!(sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, "Fluke",
					tokens[0] + 6, tokens[1])))
				return NULL;
			if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
				sr_dbg("fluke-dmm: failed to malloc devc");
				return NULL;
			}
			devc->profile = &supported_flukedmm[i];
			devc->serial = sr_serial_dev_inst_new(conn, -1);
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

	/* TODO */

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{

	/* TODO */

	return SR_OK;
}

static int hw_cleanup(void)
{

	clear_instances();

	/* TODO */

	return SR_OK;
}

static int hw_info_get(int info_id, const void **data,
       const struct sr_dev_inst *sdi)
{


	switch (info_id) {
	/* TODO */
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
		const void *value)
{
	int ret;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	ret = SR_OK;
	switch (hwcap) {
	/* TODO */
	default:
		ret = SR_ERR_ARG;
	}

	return ret;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{

	/* TODO */

	return SR_OK;
}

static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data)
{

	(void)cb_data;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	/* TODO */

	return SR_OK;
}

SR_PRIV struct sr_dev_driver driver_info = {
	.name = "fluke-dmm",
	.longname = "Fluke 18x/28x series DMMs",
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
