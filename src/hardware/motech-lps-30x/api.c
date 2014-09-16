/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Matthias Heidbrink <m-sigrok@heidbrink.biz>
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com> (code from atten-pps3xxx)
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

/** @file
 *  <em>Motech LPS-30x series</em> power supply driver
 *  @internal
 */

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#include "protocol.h"

/* Forward declarations */
SR_PRIV struct sr_dev_driver motech_lps_301_driver_info;
SR_PRIV int lps_read_reply(struct sr_serial_dev_inst *serial, char **buf, int *buflen);
SR_PRIV int lps_send_va(struct sr_serial_dev_inst *serial, const char* fmt, va_list args);
SR_PRIV int lps_cmd_ok(struct sr_serial_dev_inst *serial, const char* fmt, ...);
SR_PRIV int lps_cmd_reply(char* reply, struct sr_serial_dev_inst *serial, const char* fmt, ...);
SR_PRIV int lps_query_status(struct sr_dev_inst* sdi);

/* Serial communication parameters */
#define SERIALCOMM "2400/8n1/dtr=1/rts=1/flow=0"

#define VENDOR_MOTECH "Motech"

/** Driver scanning options. */
static const uint32_t hwopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

/** Hardware capabilities generic. */
static const uint32_t devopts[] = {
	/* Device class */
	SR_CONF_POWER_SUPPLY,
	/* Aquisition modes. */
	SR_CONF_LIMIT_SAMPLES,
	SR_CONF_LIMIT_MSEC,
	SR_CONF_CONTINUOUS,
	/* Device configuration */
	SR_CONF_OUTPUT_CHANNEL_CONFIG,
};

/** Hardware capabilities channel 1, 2. */
static const uint32_t devopts_ch12[] = {
	SR_CONF_OUTPUT_VOLTAGE,
	SR_CONF_OUTPUT_VOLTAGE_MAX,
	SR_CONF_OUTPUT_CURRENT,
	SR_CONF_OUTPUT_CURRENT_MAX,
	SR_CONF_OUTPUT_ENABLED,
};

/** Hardware capabilities channel 3. (LPS-304/305 only). */
static const uint32_t devopts_ch3[] = {
	SR_CONF_OUTPUT_VOLTAGE,
	SR_CONF_OUTPUT_ENABLED,
};

static const char *channel_modes[] = {
	"Independent",
	"Track1",
	"Track2",
};

static struct lps_modelspec models[] = {
	{ LPS_UNKNOWN, "Dummy", 0,
		{

		}
	},
	{ LPS_301, "LPS-301", 1,
		{
			/* Channel 1 */
			{ { 0, 32, 0.01 }, { 0.005, 2, 0.001 } },
		},
	},
	{ LPS_302, "LPS-302", 1,
		{
			/* Channel 1 */
			{ { 0, 32, 0.01 }, { 0.005, 3, 0.001 } },
		},
	},
	{ LPS_303, "LPS-303", 1,
		{
			/* Channel 1 */
			{ { 0, 32, 0.01 }, { 0.005, 3, 0.001 } },
		},
	},
	{ LPS_304, "LPS-304", 3,
		{
			/* Channel 1 */
			{ { 0, 32, 0.01 }, { 0.005, 3, 0.001 } },
			/* Channel 2 */
			{ { 0, 32, 0.01 }, { 0.005, 3, 0.001 } },
			/* Channel 3 */
			{ { 5, 5, 0.0 }, { 0.005, 3, 0.001 } },
		},
	},
	{ LPS_305, "LPS-305", 3,
		{
			/* Channel 1 */
			{ { 0, 32, 0.01 }, { 0.005, 3, 0.001 } },
			/* Channel 2 */
			{ { 0, 32, 0.01 }, { 0.005, 3, 0.001 } },
			/* Channel 3 */
			{ { 3.3, 5, 1.7 }, { 0.005, 3, 0.001 } },
		},
	},
};

static int init_lps301(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, &motech_lps_301_driver_info, LOG_PREFIX);
}

/** Send command to device with va_list.
 */
SR_PRIV int lps_send_va(struct sr_serial_dev_inst *serial, const char* fmt, va_list args)
{
	int retc;
	char auxfmt[LINELEN_MAX];
	char buf[LINELEN_MAX];

	snprintf(auxfmt, sizeof(auxfmt), "%s\r\n", fmt);
	vsnprintf(buf, sizeof(buf), auxfmt, args);

	sr_spew("lps_send_va: \"%s\"", buf);

	retc = serial_write_nonblocking(serial, buf, strlen(buf));

	if (retc < 0)
		return SR_ERR;

	return SR_OK;
}

/** Send command to device.
 */
SR_PRIV int lps_send_req(struct sr_serial_dev_inst *serial, const char* fmt, ...)
{
	int retc;
	va_list args;

	va_start(args, fmt);
	retc = lps_send_va(serial, fmt, args);
	va_end(args);

	return retc;
}

/** Send command and consume simple OK reply. */
SR_PRIV int lps_cmd_ok(struct sr_serial_dev_inst *serial, const char* fmt, ...)
{
	int retc;
	va_list args;
	char buf[LINELEN_MAX];
	char* bufptr;
	int  buflen;

	/* Send command */
	va_start(args, fmt);
	retc = lps_send_va(serial, fmt, args);
	va_end(args);

	if (retc != SR_OK)
		return SR_ERR;

	/* Read reply */
	buf[0] = '\0';
	bufptr = buf;
	buflen = sizeof(buf);
	retc = lps_read_reply(serial, &bufptr, &buflen);
	if ((retc == SR_OK) && (buflen == 0))
		return SR_OK;

	return SR_ERR;
}

/** Send command and read reply string.
 *  \param reply Pointer to buffer of size LINELEN_MAX. Will be NUL-terminated.
 */
SR_PRIV int lps_cmd_reply(char* reply, struct sr_serial_dev_inst *serial, const char* fmt, ...)
{
	int retc;
	va_list args;
	char buf[LINELEN_MAX];
	char* bufptr;
	int  buflen;

	reply[0] = '\0';

	/* Send command */
	va_start(args, fmt);
	retc = lps_send_va(serial, fmt, args);
	va_end(args);

	if (retc != SR_OK)
		return SR_ERR;

	/* Read reply */
	buf[0] = '\0';
	bufptr = buf;
	buflen = sizeof(buf);
	retc = lps_read_reply(serial, &bufptr, &buflen);
	if ((retc == SR_OK) && (buflen > 0)) {
		strcpy(reply, buf);
		return SR_OK;
	}

	return SR_ERR;
}

/** Process integer value returned by STATUS command. */
SR_PRIV int lps_process_status(struct sr_dev_inst* sdi, int stat)
{
	struct dev_context* devc;
	int tracking_mode;

	devc = (struct dev_context*)sdi->priv;

	sr_spew("Status: %d", stat);
	devc->channel_status[0].cc_mode = (stat & 0x01) != 0;
	sr_spew("Channel 1 %s mode", devc->channel_status[0].cc_mode?"CC":"CV");
	if (devc->model->num_channels > 1) {
		devc->channel_status[1].cc_mode = (stat & 0x02) != 0;
		sr_spew("Channel 2 %s mode", devc->channel_status[1].cc_mode?"CC":"CV");

		tracking_mode = (stat & 0x0c) >> 2;
		switch (tracking_mode) {
		case 0: devc->tracking_mode = 0;
			break;
		case 2: devc->tracking_mode = 1;
			break;
		case 3: devc->tracking_mode = 2;
			break;
		default:
			sr_err("Illegal channel tracking mode %d!", tracking_mode);
			devc->tracking_mode = 0;
			break;
		}

		sr_spew("Channel tracking: %d", devc->tracking_mode);
	}
	devc->channel_status[0].output_enabled = devc->channel_status[1].output_enabled = stat&0x040?TRUE:FALSE;
	sr_spew("Channel 1%s output: %s", devc->model->num_channels > 1?"+2":"", devc->channel_status[0].output_enabled?"ON":"OFF");
	if (devc->model->num_channels > 2) {
		devc->channel_status[2].output_enabled = stat&0x010?TRUE:FALSE;
		devc->channel_status[2].output_voltage_last = stat&0x020?3.3:5;
		sr_spew("Channel 3 output: %s, U=%02f V, overload=%d",
			devc->channel_status[2].output_enabled?"ON":"OFF",
			devc->channel_status[2].output_voltage_last,
			stat&0x080?1:0);
	}
	sr_spew("Fan=%d, beep=%d, CC output compensated=%d", stat&0x0100?1:0, stat&0x0200?1:0, stat&0x0400?1:0);

	return SR_OK;
}

/** Send STATUS commend and process status string. */
SR_PRIV int lps_query_status(struct sr_dev_inst* sdi)
{
	char buf[LINELEN_MAX];
	int stat;
	struct dev_context* devc;

	devc = (struct dev_context*)sdi->priv;

	devc->req_sent_at = g_get_real_time();

	if (lps_cmd_reply(buf, sdi->conn, "STATUS") < 0) {
		sr_err("%s: Failed to read status: %d %s", __func__, errno, strerror(errno));
		return SR_ERR;
	}

	if (sr_atoi(buf, &stat) != SR_OK)
		return SR_ERR;

	return lps_process_status(sdi, stat);
}

static gint64 calc_timeout_ms(gint64 start_us)
{
	gint64 result = REQ_TIMEOUT_MS - ((g_get_real_time() - start_us) / 1000);

	if (result < 0)
		return 0;

	return result;
}

/** Read message into buf until "OK" received.
 *  \retval SR_OK Msg received; buf and buflen contain result, if any except OK.
 *  \retval SR_ERR Error, including timeout.
*/
SR_PRIV int lps_read_reply(struct sr_serial_dev_inst *serial, char **buf, int *buflen)
{
	int retries;
	char buf2[LINELEN_MAX];
	char *buf2ptr;
	int buf2len;
	gint64 timeout_start;

	*buf[0] = '\0';

	/* Read one line. It is either a data message or "OK". */
	timeout_start = g_get_real_time();
	buf2len = *buflen;
	/* Up to 5 tries because serial_readline() will consume only one CR or LF per
	 * call, but device sends up to 4 in a row. */
	for (retries = 0; retries < 5; retries++) {
		*buflen = buf2len;
		if (serial_readline(serial, buf, buflen, calc_timeout_ms(timeout_start)) != SR_OK)
			return SR_ERR;
		if (!strcmp(*buf, "OK")) { /* We got an OK! */
			*buf[0] = '\0';
			*buflen = 0;
			return SR_OK;
		}
		if (*buflen > 0) /* We got a msg! */
			break;
	}

	/* A data msg is in buf (possibly ERROR), need to consume "OK". */
	buf2[0] = '\0';
	buf2ptr = buf2;
	for (retries = 0; retries < 5; retries++) {
		buf2len = sizeof(buf2);
		if (serial_readline(serial, &buf2ptr, &buf2len, calc_timeout_ms(timeout_start)) != SR_OK)
			return SR_ERR;

		if (!strcmp(buf2ptr, "OK")) { /* We got an OK! */
			if (!strcmp(*buf, "ERROR")) { /* OK came after msg ERROR! */
				sr_spew("ERROR found!");
				*buf[0] = '\0';
				*buflen = 0;
				return SR_ERR;
			}
			return SR_OK;
		}
	}

	return SR_ERR; /* Timeout! */
}

/** Scan for LPS-300 series device.
 */
static GSList *do_scan(lps_modelid modelid, struct sr_dev_driver *drv, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	GSList *devices;
	const char *conn, *serialcomm;
	int cnt;
	gchar  buf[LINELEN_MAX];
	gchar channel[10];
	char*  verstr;

	sdi = NULL;
	devc = NULL;
	conn = serialcomm = NULL;
	devices = NULL;

	drvc = drv->priv;
	drvc->instances = NULL;

	sr_spew("scan() called!");

	/* Process and check options. */
	if (sr_serial_extract_options(options, &conn, &serialcomm) != SR_OK)
		return NULL;
	if (!serialcomm)
		serialcomm = SERIALCOMM;

	/* Init serial port. */
	if (!(serial = sr_serial_dev_inst_new(conn, serialcomm)))
		return NULL;

	if (serial_open(serial, SERIAL_RDWR | SERIAL_NONBLOCK) != SR_OK)
		goto exit_err;

	/* Query and verify model string. */
	serial_flush(serial);
	if (lps_cmd_reply(buf, serial, "MODEL") != SR_OK)
		return NULL;

	/* Check model string. */
	if (strncmp(buf, "LPS-", 4)) {
		sr_spew("Unknown model code \"%s\"!", buf);
		return NULL;
	}

	/* Bug in device FW 1.17, model number is empty, so this can't work with this FW! */
	if (modelid == LPS_UNKNOWN) {
		g_strstrip(buf);
		for (cnt = LPS_301; cnt <= LPS_305; cnt++) {
			if (!strcmp(buf, models[cnt].modelstr)) {
				modelid = cnt;
				break;
			}
		}
		if (modelid == LPS_UNKNOWN) {
			sr_err("Unable to detect model from model string '%s'!", buf);
			return NULL;
		}
	}

	/* Query version */
	verstr = NULL;
	if (lps_cmd_reply(buf, serial, "VERSION") == SR_OK) {
		if (strncmp(buf, "Ver-", 4)) {
			sr_spew("Version string %s not recognized.", buf);
			goto exit_err;
		}


		g_strstrip(buf);
		verstr = buf + 4;
	}
	else  /* Bug in device FW 1.17: Quering version string fails while output is active.
		Therefore just print an error message, but do not exit with error. */
		sr_err("Failed to query for hardware version: %d %s", errno, strerror(errno));

	sdi = sr_dev_inst_new(0, SR_ST_INACTIVE, VENDOR_MOTECH, models[modelid].modelstr, verstr);
	sdi->driver = drv;
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = serial;

	devc = g_malloc0(sizeof(struct dev_context));
	devc->model = &models[modelid];
	devc->limit_samples = 0;
	devc->limit_msec = 0;
	devc->num_samples = 0;
	devc->elapsed_msec = g_timer_new();

	sdi->priv = devc;

	/* Setup channels and channel groups. */
	for (cnt = 0; cnt < models[modelid].num_channels; cnt++) {
		snprintf(channel, sizeof(channel), "CH%d", cnt + 1);
		ch = sr_channel_new(cnt, SR_CHANNEL_ANALOG, TRUE, channel);
		sdi->channels = g_slist_append(sdi->channels, ch);

		devc->channel_status[cnt].info = g_slist_append(NULL, ch);

		cg = g_malloc(sizeof(struct sr_channel_group));
		snprintf(channel, sizeof(channel), "CG%d", cnt+1);
		cg->name = g_strdup(channel);
		cg->priv = NULL;
		cg->channels = g_slist_append(NULL, ch);

		sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);
	}

	drvc->instances = g_slist_append(drvc->instances, sdi);
	devices = g_slist_append(devices, sdi);

	/* Query status */
	if (lps_query_status(sdi) != SR_OK)
		goto exit_err;

	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return devices;

exit_err:
	sr_info("%s: Error!", __func__);

	if (serial) {
		serial_close(serial);
		sr_serial_dev_inst_free(serial);
	}
	if (devc)
		g_free(devc);
	if (sdi)
		sr_dev_inst_free(sdi);

	return NULL;
}

/** Scan for LPS-301 device. */
static GSList *scan_lps301(GSList *options)
{
	return do_scan(LPS_301, &motech_lps_301_driver_info, options);
}

static GSList *doDevList(struct sr_dev_driver *drv)
{
	return ((struct drv_context *)(drv->priv))->instances;
}

static GSList *dev_list_lps301(void)
{
	return doDevList(&motech_lps_301_driver_info);
}

static void dev_clear_private(struct dev_context* devc)
{
	int ch_idx;

	/* Free channel_status.info (list only, data owned by sdi). */
	for (ch_idx = 0; ch_idx < devc->model->num_channels; ch_idx++)
		g_slist_free(devc->channel_status[ch_idx].info);

	g_timer_destroy(devc->elapsed_msec);
}

static int dev_clear_lps301(void)
{
	return std_dev_clear(&motech_lps_301_driver_info, (std_dev_clear_callback)dev_clear_private);
}

static int cleanup(void)
{
	return dev_clear_lps301();
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	int ch_idx;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if (!cg) {
		/* No channel group: global options. */
		switch (key) {
		case SR_CONF_LIMIT_SAMPLES:
			*data = g_variant_new_uint64(devc->limit_samples);
			break;
		case SR_CONF_LIMIT_MSEC:
			*data = g_variant_new_uint64(devc->limit_msec);
			break;
		case SR_CONF_OUTPUT_CHANNEL_CONFIG:
			*data = g_variant_new_string(channel_modes[devc->tracking_mode]);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		/* We only ever have one channel per channel group in this driver. */
		ch = cg->channels->data;
		ch_idx = ch->index;
		switch (key) {
		case SR_CONF_OUTPUT_VOLTAGE:
			*data = g_variant_new_double(devc->channel_status[ch_idx].output_voltage_last);
			break;
		case SR_CONF_OUTPUT_VOLTAGE_MAX:
			*data = g_variant_new_double(devc->channel_status[ch_idx].output_voltage_max);
			break;
		case SR_CONF_OUTPUT_CURRENT:
			*data = g_variant_new_double(devc->channel_status[ch_idx].output_current_last);
			break;
		case SR_CONF_OUTPUT_CURRENT_MAX:
			*data = g_variant_new_double(devc->channel_status[ch_idx].output_current_max);
			break;
		case SR_CONF_OUTPUT_ENABLED:
			*data = g_variant_new_boolean(devc->channel_status[ch_idx].output_enabled);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	gdouble dval;
	int ch_idx;
	const char *sval;
	gboolean bval;
	int idx;
	gboolean found;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	/* Cannot change settings while acquisition active, would cause a mess with commands.
	 * Changing this would be possible, but tricky. */
	if (devc->acq_running)
		return SR_ERR_NA;

	if (!cg) {
		/* No channel group: global options. */
		switch (key) {
		case SR_CONF_LIMIT_MSEC:
			if (g_variant_get_uint64(data) == 0) {
				sr_err("LIMIT_MSEC can't be 0.");
				return SR_ERR;
			}
			devc->limit_msec = g_variant_get_uint64(data);
			sr_dbg("Setting time limit to %" PRIu64 "ms.",
				devc->limit_msec);
			break;
		case SR_CONF_LIMIT_SAMPLES:
			devc->limit_samples = g_variant_get_uint64(data);
			sr_dbg("Setting sample limit to %" PRIu64 ".",
				devc->limit_samples);
			break;
		case SR_CONF_OUTPUT_CHANNEL_CONFIG:
			sval = g_variant_get_string(data, NULL);
			found = FALSE;
			for (idx = 0; idx < (int)ARRAY_SIZE(channel_modes); idx++)
			{
				if (!strcmp(sval, channel_modes[idx])) {
					found = TRUE;
					if (devc->tracking_mode == idx)
						break;	/* Nothing to do! */
					devc->tracking_mode = idx;
					if (devc->model->modelid >= LPS_304) /* No use to set anything in the smaller models. */
						return lps_cmd_ok(sdi->conn, "TRACK%1d", devc->tracking_mode);
				}
				if (devc->model->modelid <= LPS_303) /* Only first setting possible for smaller models. */
					break;
			}
			if (!found) {
				return SR_ERR_ARG;
			}
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		/* Channel group specified: per-channel options. */
		/* We only ever have one channel per channel group in this driver. */
		ch = cg->channels->data;
		ch_idx = ch->index;

		switch (key) {
		case SR_CONF_OUTPUT_VOLTAGE_MAX:
			dval = g_variant_get_double(data);
			if (dval < 0 || dval > devc->model->channels[ch_idx].voltage[1])
				return SR_ERR_ARG;
			if (ch_idx == 2) {
				if (devc->model->modelid < LPS_304)
					return SR_ERR_ARG;

				if (fabs(dval - 5.000) <= 0.001)
					dval = 5.0;
				else if ((devc->model->modelid >= LPS_305) && (fabs(dval - 3.300) <= 0.001))
					dval = 3.3;
				else return SR_ERR_ARG;
			}

			devc->channel_status[ch_idx].output_voltage_max = dval;
			if (ch_idx == 2)
				return lps_cmd_ok(sdi->conn, "VDD%1.0f", trunc(dval));
			else
				return lps_cmd_ok(sdi->conn, "VSET%d %05.3f", ch_idx+1, dval);
			break;
		case SR_CONF_OUTPUT_CURRENT_MAX:
			dval = g_variant_get_double(data);
			if (dval < 0 || dval > devc->model->channels[ch_idx].current[1])
				return SR_ERR_ARG;
			if (ch_idx == 2) /* No current setting for CH3. */
				return SR_ERR_NA;
			devc->channel_status[ch_idx].output_current_max = dval;
			return lps_cmd_ok(sdi->conn, "ISET%d %05.4f", ch_idx+1, dval);
			break;
		case SR_CONF_OUTPUT_ENABLED:
			bval = g_variant_get_boolean(data);
			if (bval == devc->channel_status[ch_idx].output_enabled) /* Nothing to do. */
				break;
			devc->channel_status[ch_idx].output_enabled = bval;
			if (ch_idx != 2) { /* Channels 1,2 can be set only together. */
				devc->channel_status[ch_idx^1].output_enabled = bval;
				return lps_cmd_ok(sdi->conn, "OUT%1d", (int)bval);
			} else { /* Channel 3: No command to disable output, set voltage to 0 instead. */
				if (bval)
					return lps_cmd_ok(sdi->conn, "VDD%1.0f", devc->channel_status[ch_idx].output_voltage_max);
				else
					return lps_cmd_ok(sdi->conn, "VDD0");
			}
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	int ch_idx, i;
	GVariant *gvar;
	GVariantBuilder gvb;

	(void)data;

	/* Driver options, no device instance necessary. */
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
						  hwopts, ARRAY_SIZE(hwopts), sizeof(uint32_t));
		return SR_OK;
	default:
		if (sdi == NULL)
			return SR_ERR_ARG;

		devc = sdi->priv;
	}

	/* Device options, independent from channel groups. */
	if (cg == NULL) {
		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
					devopts, ARRAY_SIZE(devopts), sizeof(uint32_t));
			return SR_OK;
		case SR_CONF_OUTPUT_CHANNEL_CONFIG:
			if (devc->model->modelid <= LPS_303) {
				/* The 1-channel models. */
				*data = g_variant_new_strv(channel_modes, 1);
			} else {
				/* The other models support all modes. */
				*data = g_variant_new_strv(channel_modes, ARRAY_SIZE(channel_modes));
			}
			return SR_OK;
			break;
		default:
			return SR_ERR_NA;
		}
	}

	/* Device options, depending on channel groups. */
	ch = cg->channels->data;
	ch_idx = ch->index;
	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		if ((ch_idx == 0) || (ch_idx == 1)) /* CH1, CH2 */
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				  devopts_ch12, ARRAY_SIZE(devopts_ch12), sizeof(uint32_t));
		else /* Must be CH3 */
			*data = g_variant_new_fixed_array(G_VARIANT_TYPE_UINT32,
				  devopts_ch3, ARRAY_SIZE(devopts_ch3), sizeof(uint32_t));
		break;
	case SR_CONF_OUTPUT_VOLTAGE_MAX:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		/* Min, max, step. */
		for (i = 0; i < 3; i++) {
			gvar = g_variant_new_double(devc->model->channels[ch_idx].voltage[i]);
			g_variant_builder_add_value(&gvb, gvar);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_OUTPUT_CURRENT_MAX:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE_ARRAY);
		/* Min, max, step. */
		for (i = 0; i < 3; i++) {
			gvar = g_variant_new_double(devc->model->channels[ch_idx].current[i]);
			g_variant_builder_add_value(&gvb, gvar);
		}
		*data = g_variant_builder_end(&gvb);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	devc->acq_running = TRUE;

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 50,
			motech_lps_30x_receive_data, (void *)sdi);
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Start timer, if required. */
	if (devc->limit_msec)
		g_timer_start(devc->elapsed_msec);

	devc->acq_req = AQ_NONE;
	/* Do not start polling device here, the read function will do it in 50 ms. */

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;

	/* Stop timer, if required. */
	if (sdi && (devc = sdi->priv) && devc->limit_msec)
		g_timer_stop(devc->elapsed_msec);

	return std_serial_dev_acquisition_stop(sdi, cb_data, std_serial_dev_close,
			sdi->conn, LOG_PREFIX);
}

SR_PRIV struct sr_dev_driver motech_lps_301_driver_info = {
	.name = "motech-lps-301",
	.longname = "Motech LPS-301",
	.api_version = 1,
	.init = init_lps301,
	.cleanup = cleanup,
	.scan = scan_lps301,
	.dev_list = dev_list_lps301,
	.dev_clear = dev_clear_lps301,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
