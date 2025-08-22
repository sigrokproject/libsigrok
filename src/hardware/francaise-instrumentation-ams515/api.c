/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2022 François Revol <revol@free.fr>
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
#include "protocol.h"
#include <math.h>

static struct sr_dev_driver francaise_instrumentation_ams515_driver_info;

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_POWER_SUPPLY,
};

/*
 * Implemented commands:
 *
 * R?		(Reference?) Return Model and Version
 * T / T?	Toggle echo
 * [ABC] / [ABC]?	Set / query channel target voltage
 * I?		Overcurrent indicator, returns "Ok", or ">[ABC]" (not sure for more than 1)
 * S[ABC] / S?	sets / query selected output on the front panel
 * V		Lock front panel
 * D / D?	disable display (does *not* disable outputs)
 *
 * Note lowercase letters are also accepted as commands.
 *
 * Unimplemented commands:
 *
 * M[ABC]	Memorize target
 *
 * Current features:
 *
 * - setting target voltage for each channel.
 * - reporting of overcurrent condition.
 *
 * Limitations:
 *
 * - We cannot control a channel in overcurrent condition,
 * so it must be physically lifted before we can change the
 * target.
 *
 * - we cannot report the actual output voltage as it's
 * not measured. We could eventually implement monitoring
 * of manual controls for when the panel is not locked.
 *
 * - The C channel clicks a relay when crossing the zero,
 * and technically supports both "+0" and "-0", maybe we
 * could cache the value and chose which to use depending
 * on the previous target.
 */

static const uint32_t devopts[] = {
	SR_CONF_CHANNEL_CONFIG | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET,
};

static const uint32_t devopts_cg[] = {
	SR_CONF_ENABLED | SR_CONF_GET,
	SR_CONF_VOLTAGE_TARGET | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE | SR_CONF_GET,
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD | SR_CONF_GET,
};

struct channel_spec {
	gdouble voltage[3];
	gdouble current[3];
};

static const struct channel_spec channel_specs[] = {
	{ { 0, 15, 0.1L }, { 1, 1, 0 } }, // Actually +/- symetrical outputs
	{ { 2, 5.5, 0.1L }, { 3, 3, 0 } },
	{ { -15, 15, 0.1L }, { 0.2, 0.2, 0 } },
};

static const char *channel_modes[] = { "Front Panel Enabled",
				       "Front Panel Locked",
				       "Front Panel Off" };

/* We MUST disable hardware flow control it seems */
#define SERIALCOMM "9600/8n1/flow=0"

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	GSList *l, *devices;
	int i, res;
	const char *conn, *serialcomm;
	char *buf, **tokens;
	const char *ident_request = "R?\r";

	devices = NULL;
	conn = serialcomm = NULL;
	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = g_variant_get_string(src->data, NULL);
			break;
		}
	}
	if (!conn)
		return NULL;
	if (!serialcomm)
		serialcomm = SERIALCOMM;

	serial = sr_serial_dev_inst_new(conn, serialcomm);

	if (serial_open(serial, SERIAL_RDWR) != SR_OK) {
		sr_err("Unable open serial port.");
		return NULL;
	}

	buf = g_malloc(ANSWER_MAX);
	sdi = g_malloc0(sizeof(struct sr_dev_inst));
	devc = g_malloc0(sizeof(struct dev_context));
	sdi->conn = serial;
	sdi->priv = devc;
	g_mutex_init(&devc->mutex);

	res = francaise_instrumentation_ams515_send_raw(sdi, ident_request, buf,
							TRUE);
	if (res < SR_OK)
		return NULL;
	tokens = g_strsplit(buf, " ", 2);

	// 4.1 is the only known version for now.
	if (tokens[0] && !strcmp("AMS515", tokens[0]) && tokens[1] &&
	    !strncmp("4.1", tokens[1], 2)) {
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup("Française d'Instrumentation");
		sdi->model = g_strdup(tokens[0]);
		sdi->version = g_strdup(tokens[1]);
		sdi->inst_type = SR_INST_SERIAL;
		devc->selected_channel = -1;
		devc->resync = FALSE;
		devc->overcurrent = FALSE;
		for (i = 0; i < MAX_CHANNELS; i++) {
			char name[4];
			snprintf(name, 2, "%c", 'A' + i);
			ch = sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE,
					    name);
			cg = sr_channel_group_new(sdi, name, NULL);
			cg->channels = g_slist_append(NULL, ch);
		}
		devices = g_slist_append(devices, sdi);
	}
	g_strfreev(tokens);
	g_free(buf);

	serial_close(serial);
	if (!devices) {
		sr_serial_dev_inst_free(serial);
		g_free(devc);
		g_free(sdi);
	}

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret, res;

	if (!sdi)
		return SR_ERR_ARG;

	ret = std_serial_dev_open(sdi);
	if (ret != SR_OK)
		return ret;

	/* Request the unit to turn echo off if not already. */
	res = francaise_instrumentation_ams515_set_echo(sdi, FALSE);
	if (res != SR_OK)
		sr_dbg("Failed to disable echo on unit.");

	return ret;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	if (!sdi)
		return SR_ERR_ARG;

	// Enable front panel again
	francaise_instrumentation_ams515_set_state(sdi, 'D', FALSE);
	francaise_instrumentation_ams515_set_state(sdi, 'V', FALSE);
	// Turn back echo mode
	francaise_instrumentation_ams515_set_echo(sdi, TRUE);

	return std_serial_dev_close(sdi);
}

static void dev_clear_callback(void *priv)
{
	struct dev_context *devc;

	if (!priv)
		return;

	devc = priv;
	g_mutex_clear(&devc->mutex);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear_with_callback(di, dev_clear_callback);
}

static int config_get(uint32_t key, GVariant **data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	int channel, ival, ret;
	char answer[ANSWER_MAX];
	gboolean bval;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	if (!cg) {
		switch (key) {
		case SR_CONF_CHANNEL_CONFIG:
			*data = g_variant_new_string(
				channel_modes[devc->panel_mode]);
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
			*data = g_variant_new_boolean(TRUE);
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE:
			bval = devc->overcurrent;
			// If we already have a situation, report it immediately, else query the device
			if (!bval) {
				ret = francaise_instrumentation_ams515_query_str(
					sdi, 'I', answer);
				if (ret < SR_OK)
					return ret;
				if (answer[0] == '>' &&
				    (answer[1] >= 'A' && answer[1] <= 'C'))
					bval = TRUE;
			}
			*data = g_variant_new_boolean(bval);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		/* We only ever have one channel per channel group in this driver. */
		ch = cg->channels->data;
		channel = ch->index;

		if (channel < 0 || channel > MAX_CHANNELS)
			return SR_ERR_ARG;

		switch (key) {
		case SR_CONF_ENABLED:
			*data = g_variant_new_boolean(TRUE);
			break;
		case SR_CONF_VOLTAGE_TARGET:
			ret = francaise_instrumentation_ams515_query_int(
				sdi, 'A' + channel, &ival);
			if (ret < SR_OK)
				return ret;
			*data = g_variant_new_double(
				(double)(ival * 150 / 0x96) / 10);
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_ENABLED:
			*data = g_variant_new_boolean(TRUE);
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE:
			ret = francaise_instrumentation_ams515_query_str(
				sdi, 'I', answer);
			if (ret < SR_OK)
				return ret;
			if (!strcmp(answer, "Ok"))
				bval = FALSE;
			else if (answer[0] == '>') {
				int i;
				bval = FALSE;
				for (i = 1; i < ANSWER_MAX && answer[i]; i++) {
					if (answer[i] == 'A' + channel)
						bval = TRUE;
				}
			} else
				return SR_ERR;
			*data = g_variant_new_boolean(bval);
			break;
		case SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD:
			*data = g_variant_new_double(
				channel_specs[channel].current[1]);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	gdouble dval;
	int channel, ival, ret = SR_OK;

	devc = sdi->priv;

	if (!cg) {
		switch (key) {
		case SR_CONF_CHANNEL_CONFIG:
			if ((ival = std_str_idx(
				     data, ARRAY_AND_SIZE(channel_modes))) < 0)
				return SR_ERR_ARG;
			if (ival > 2)
				return SR_ERR_ARG;
			devc->panel_mode = ival;
			ret = francaise_instrumentation_ams515_set_state(
				sdi, 'V', ival > 0);
			ret = francaise_instrumentation_ams515_set_state(
				sdi, 'D', ival > 1);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		/* We only ever have one channel per channel group in this driver. */
		ch = cg->channels->data;
		channel = ch->index;

		if (channel < 0 || channel > MAX_CHANNELS)
			return SR_ERR_ARG;

		switch (key) {
		case SR_CONF_VOLTAGE_TARGET:
			dval = g_variant_get_double(data);
			if (dval < channel_specs[channel].voltage[0] ||
			    dval > channel_specs[channel].voltage[1])
				return SR_ERR_ARG;
			// Switch the front panel display to the channel we are modifying.
			// Depending on the update frequency it might not be a good idea though.
			if (channel != devc->selected_channel) {
				francaise_instrumentation_ams515_send_char(
					sdi, 'S', 'A' + channel);
				devc->selected_channel = channel;
			}
			ival = round(dval * 10) * 0x96 / 150;
			ret = francaise_instrumentation_ams515_send_int(
				sdi, 'A' + channel, ival);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
		       const struct sr_dev_inst *sdi,
		       const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	int channel;

	devc = (sdi) ? sdi->priv : NULL;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts,
					       drvopts, devopts);
		case SR_CONF_CHANNEL_CONFIG:
			if (!devc)
				return SR_ERR_ARG;
			// we only support independent channels
			*data = g_variant_new_strv(
				ARRAY_AND_SIZE(channel_modes));
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		/* We only ever have one channel per channel group in this driver. */
		ch = cg->channels->data;
		channel = ch->index;

		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
			break;
		case SR_CONF_VOLTAGE_TARGET:
			if (!devc)
				return SR_ERR_ARG;
			*data = std_gvar_min_max_step_array(
				channel_specs[channel].voltage);
			break;
		default:
			return SR_ERR_NA;
		}
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN | G_IO_ERR, 500,
			  francaise_instrumentation_ams515_receive_data,
			  (void *)sdi);

	std_session_send_df_header(sdi);

	return SR_OK;
}

static struct sr_dev_driver francaise_instrumentation_ams515_driver_info = {
	.name = "francaise-instrumentation-ams515",
	.longname = "Française d'Instrumentation AMS515",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(francaise_instrumentation_ams515_driver_info);
