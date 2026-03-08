/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Philipp Marek <philipp@marek.priv.at>
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

/**
 * @file api.c
 *
 * libsigrok driver API for the Hantek DSO2xxx oscilloscope series with the
 * "quick-fetch" firmware patch installed.
 *
 * Usage summary
 * -------------
 * This driver does NOT enumerate devices via USB; instead it connects over
 * TCP to the scope's IP address (requires DavidAlfa's USB-networking kernel
 * image, which exposes the scope at 192.168.7.1 by default).
 *
 * No SCPI or serial scan is possible; scan() accepts a conn= option,
 * but will try the default IP/port as well.
 *
 *   sigrok-cli -d hantek-dso2xxx:conn=192.168.7.1/5025*
 *
 * No configurable scan/acquisition options are exposed (the driver follows
 * all scope settings passively). Waveform capture is triggered by pressing
 * the SAVE TO USB button on the instrument, sigrok can ask for frames as well, though.
 */

#define _GNU_SOURCE
#include "protocol.h"
#include <config.h>
#include <string.h>
#include <glib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/* --------------------------------------------------------------------------
 * Static driver metadata
 * -------------------------------------------------------------------------- */

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
};

/*
 * No acquisition options are exposed.  The driver is entirely passive: it
 * reads whatever the scope decides to send when the user presses SAVE TO USB.
 */
static const uint32_t devopts[] = {
	SR_CONF_CONN | SR_CONF_GET,
	SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET,
};

/* --------------------------------------------------------------------------
 * scan() helpers
 * -------------------------------------------------------------------------- */

/**
 * Parse a "address/port" or "address:port" connection string.
 *
 * The address and port are separated by '/' (preferred by libsigrok) or ':'.
 * If no separator is found the whole string is used as address and
 * HANTEK_DEFAULT_PORT is used for the port.
 */
static void parse_conn(const char *conn, char **addr_out, char **port_out)
{
	const char *sep;
	char       *addr, *port;
	const char tcp_raw[] = "tcp-raw/";

	if (strncmp(conn, tcp_raw, sizeof(tcp_raw)-1) == 0)
		conn += sizeof(tcp_raw)-1;

	sep = strrchr(conn, '/');
	if (!sep)
		sep = strrchr(conn, ':');

	if (sep) {
		addr = g_strndup(conn, sep - conn);
		port = g_strdup(sep + 1);
	} else {
		addr = g_strdup(conn);
		port = g_strdup(HANTEK_DEFAULT_PORT);
	}

	*addr_out = addr;
	*port_out = port;
}

/* --------------------------------------------------------------------------
 * Driver callbacks
 * -------------------------------------------------------------------------- */

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst    *sdi = NULL;
	struct dev_context    *devc = NULL;
	struct sr_config      *src;
	GSList                *devices = NULL;
	GSList                *l;
	const char            *conn    = NULL;
	char                  *addr, *port;
	int                   status;

	/* Require a conn= option – we cannot enumerate scopes. */
	for (l = options; l; l = l->next) {
		src = l->data;
		if (src->key == SR_CONF_CONN) {
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (conn) {
		parse_conn(conn, &addr, &port);
	} else {
		/* Is there an unscan() to drop memory allocated?
		 * Then this would be wrong... */
		addr = HANTEK_DEFAULT_ADDR;
		port = HANTEK_DEFAULT_PORT;
	}

	sr_dbg("Creating device for %s:%s", addr, port);

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status   = SR_ST_INACTIVE;
	sdi->vendor   = g_strdup("Hantek");
	sdi->model    = g_strdup("DSO2xxx (quick-fetch)");
	sdi->driver   = di;

	/* Add the two analogue channels. */
	sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "CH1");
	sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "CH2");

	devc                = g_malloc0(sizeof(*devc));
	devc->tcp_address   = addr;   /* ownership transferred */
	devc->tcp_port      = port;   /* ownership transferred */
	devc->sockfd        = -1;
	sdi->priv           = devc;

	status = hantek_dso2xxx_tcp_connect(sdi);
	if (status != SR_OK) {
		goto nope;
	}

	status = hantek_dso2xxx_timesync(devc);
	if (status != SR_OK) {
		sr_err("Timesync failure (to %s:%s)",
				devc->tcp_address, devc->tcp_port);
		goto nope;
	}

	hantek_dso2xxx_tcp_close(sdi);

	/* Register with the driver instance list. */
	devices = g_slist_append(devices, sdi);
	return std_scan_complete(di, devices);

nope:
	sr_dev_inst_free(sdi);
	return NULL;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	sdi->status = SR_ST_ACTIVE;
	/* The TCP connection is opened only as-needed. */

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	sdi->status = SR_ST_INACTIVE;
	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;

	switch (key) {
	case SR_CONF_CONN:
		*data = g_variant_new_printf("%s/%s",
		                             devc->tcp_address,
		                             devc->tcp_port);
		break;
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->sample_rate);
		break;
	case SR_CONF_LIMIT_FRAMES:
		*data = g_variant_new_uint64(devc->frames_received);
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;
	(void)data;

	if (!sdi)
		return SR_ERR_ARG;
	devc = sdi->priv;
	(void)devc;

	/*
	 * The only writable key is LIMIT_FRAMES, but we treat the driver as
	 * "no configurable options": just accept and ignore it for now so
	 * frontends don't error out.
	 */
	switch (key) {
	case SR_CONF_LIMIT_FRAMES:
		/* No-op: the driver runs continuously until the session ends. */
		break;
	default:
		return SR_ERR_NA;
	}
	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
		       const struct sr_dev_inst *sdi,
		       const struct sr_channel_group *cg)
{
	return STD_CONFIG_LIST(key, data, sdi, cg,
	                       scanopts, drvopts, devopts);
}

/* --------------------------------------------------------------------------
 * Acquisition I/O callback (runs in the GLib main loop)
 * -------------------------------------------------------------------------- */

/**
 * GLib I/O watch callback.  Called whenever the TCP socket is readable,
 * which means the scope has started sending a waveform frame.
 */
static int receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi = cb_data;
	struct dev_context        *devc;

	(void)fd;
	devc = sdi->priv;

	if (!devc->acq_running)
		return FALSE; /* Unregister the source. */

	if (revents & (G_IO_ERR | G_IO_HUP)) {
		sr_err("Socket error / hangup during acquisition.");
		sr_dev_acquisition_stop((struct sr_dev_inst *)sdi);
		return FALSE;
	}

	if (revents & G_IO_IN) {
		if (hantek_dso2xxx_receive_frame(sdi) != SR_OK) {
			sr_err("Frame receive failed; stopping acquisition.");
			sr_dev_acquisition_stop((struct sr_dev_inst *)sdi);
			return FALSE;
		}
	}

	return TRUE; /* Keep the I/O watch active for the next frame. */
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	if (hantek_dso2xxx_tcp_connect(sdi) != SR_OK)
		return SR_ERR;
	if (hantek_dso2xxx_timesync(devc) != SR_OK)
		return SR_ERR;
	if (hantek_dso2xxx_acq_now(devc) != SR_OK)
		return SR_ERR;

	devc->acq_running    = TRUE;
	devc->frames_received = 0;

	std_session_send_df_header(sdi);

	/*
	 * Register the socket fd as a GLib I/O source so receive_data()
	 * is called each time the scope sends a frame (triggered by the
	 * user pressing SAVE TO USB).
	 */
	sr_session_source_add(sdi->session, devc->sockfd,
	                      G_IO_IN | G_IO_ERR | G_IO_HUP,
	                      HANTEK_TCP_TIMEOUT_MS,
	                      receive_data, (void *)sdi);

	sr_info("Acquisition started – press SAVE TO USB on the scope.");
	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	hantek_dso2xxx_tcp_close(sdi);

	if (!devc->acq_running)
		return SR_OK;

	devc->acq_running = FALSE;
	sr_session_source_remove(sdi->session, devc->sockfd);
	std_session_send_df_end(sdi);

	sr_info("Acquisition stopped. Frames received: %" PRIu64,
	        devc->frames_received);
	return SR_OK;
}

SR_PRIV int drv_init(struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
	hantek_dso2xxx_c_locale = newlocale(LC_ALL_MASK, "C", NULL);
	return std_init(di, sr_ctx);
}

/* --------------------------------------------------------------------------
 * Driver instance definition
 * -------------------------------------------------------------------------- */

static struct sr_dev_driver hantek_dso2xxx_driver_info = {
	.name             = "hantek-dso2xxx",
	.longname         = "Hantek DSO2xxx (quick-fetch)",
	.api_version      = 1,
	.init             = drv_init,
	.cleanup          = std_cleanup,
	.scan             = scan,
	.dev_list         = std_dev_list,
	.dev_clear        = std_dev_clear,
	.config_get       = config_get,
	.config_set       = config_set,
	.config_list      = config_list,
	.dev_open         = dev_open,
	.dev_close        = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop  = dev_acquisition_stop,
	.context          = NULL,
};
SR_REGISTER_DEV_DRIVER(hantek_dso2xxx_driver_info);
