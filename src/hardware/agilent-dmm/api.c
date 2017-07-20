/*
 * This file is part of the libsigrok project.
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

#include <config.h>
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

static const uint32_t scanopts[] = {
	SR_CONF_CONN,
	SR_CONF_SERIALCOMM,
};

static const uint32_t drvopts[] = {
	SR_CONF_MULTIMETER,
};

static const uint32_t devopts[] = {
	SR_CONF_CONTINUOUS,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_DATA_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint64_t samplerates[] = {
	SR_HZ(1),
	SR_HZ(20),
	SR_HZ(1),
};

static const char *data_sources[] = {
	"Live",
	"Log-Hand",
	"Log-Trig",
	"Log-Auto",
	"Log-Export",
};

extern const struct agdmm_job agdmm_jobs_live[];
extern const struct agdmm_job agdmm_jobs_log[];
extern const struct agdmm_recv agdmm_recvs_u123x[];
extern const struct agdmm_recv agdmm_recvs_u124x[];
extern const struct agdmm_recv agdmm_recvs_u124xc[];
extern const struct agdmm_recv agdmm_recvs_u125x[];
extern const struct agdmm_recv agdmm_recvs_u128x[];

/* This works on all the Agilent U12xxA series, although the
 * U127xA can apparently also run at 19200/8n1. */
#define SERIALCOMM "9600/8n1"

static const struct agdmm_profile supported_agdmm[] = {
	{ AGILENT_U1231, "U1231A", 1, agdmm_jobs_live, NULL, agdmm_recvs_u123x },
	{ AGILENT_U1232, "U1232A", 1, agdmm_jobs_live, NULL, agdmm_recvs_u123x },
	{ AGILENT_U1233, "U1233A", 1, agdmm_jobs_live, NULL, agdmm_recvs_u123x },

	{ AGILENT_U1241, "U1241A", 2, agdmm_jobs_live, NULL, agdmm_recvs_u124x },
	{ AGILENT_U1242, "U1242A", 2, agdmm_jobs_live, NULL, agdmm_recvs_u124x },
	{ AGILENT_U1241, "U1241B", 2, agdmm_jobs_live, NULL, agdmm_recvs_u124x },
	{ AGILENT_U1242, "U1242B", 2, agdmm_jobs_live, NULL, agdmm_recvs_u124x },

	{ KEYSIGHT_U1241C, "U1241C", 2, agdmm_jobs_live, agdmm_jobs_log, agdmm_recvs_u124xc },
	{ KEYSIGHT_U1242C, "U1242C", 2, agdmm_jobs_live, agdmm_jobs_log, agdmm_recvs_u124xc },

	{ AGILENT_U1251, "U1251A", 3, agdmm_jobs_live, NULL, agdmm_recvs_u125x },
	{ AGILENT_U1252, "U1252A", 3, agdmm_jobs_live, NULL, agdmm_recvs_u125x },
	{ AGILENT_U1253, "U1253A", 3, agdmm_jobs_live, NULL, agdmm_recvs_u125x },
	{ AGILENT_U1251, "U1251B", 3, agdmm_jobs_live, NULL, agdmm_recvs_u125x },
	{ AGILENT_U1252, "U1252B", 3, agdmm_jobs_live, NULL, agdmm_recvs_u125x },
	{ AGILENT_U1253, "U1253B", 3, agdmm_jobs_live, NULL, agdmm_recvs_u125x },

	{ KEYSIGHT_U1281, "U1281A", 3, agdmm_jobs_live, agdmm_jobs_log, agdmm_recvs_u128x },
	{ KEYSIGHT_U1282, "U1282A", 3, agdmm_jobs_live, agdmm_jobs_log, agdmm_recvs_u128x },
	ALL_ZERO
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_config *src;
	struct sr_serial_dev_inst *serial;
	GSList *l, *devices;
	int len, i;
	const char *conn, *serialcomm;
	char *buf, **tokens;

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

	if (serial_open(serial, SERIAL_RDWR) != SR_OK)
		return NULL;

	serial_flush(serial);
	if (serial_write_blocking(serial, "*IDN?\r\n", 7, SERIAL_WRITE_TIMEOUT_MS) < 7) {
		sr_err("Unable to send identification string.");
		return NULL;
	}

	len = 128;
	buf = g_malloc(len);
	serial_readline(serial, &buf, &len, 250);
	if (!len)
		return NULL;

	tokens = g_strsplit(buf, ",", 4);
	if ((!strcmp("Agilent Technologies", tokens[0]) ||
	     !strcmp("Keysight Technologies", tokens[0]))
			&& tokens[1] && tokens[2] && tokens[3]) {
		for (i = 0; supported_agdmm[i].model; i++) {
			if (strcmp(supported_agdmm[i].modelname, tokens[1]))
				continue;
			sdi = g_malloc0(sizeof(struct sr_dev_inst));
			sdi->status = SR_ST_INACTIVE;
			sdi->vendor = g_strdup(tokens[0][0] == 'A' ? "Agilent" : "Keysight");
			sdi->model = g_strdup(tokens[1]);
			sdi->version = g_strdup(tokens[3]);
			devc = g_malloc0(sizeof(struct dev_context));
			sr_sw_limits_init(&devc->limits);
			devc->profile = &supported_agdmm[i];
			devc->data_source = DEFAULT_DATA_SOURCE;
			devc->cur_samplerate = 5;
			if (supported_agdmm[i].nb_channels > 1) {
				int temp_chan = supported_agdmm[i].nb_channels - 1;
				devc->cur_mq[temp_chan] = SR_MQ_TEMPERATURE;
				devc->cur_unit[temp_chan] = SR_UNIT_CELSIUS;
				devc->cur_digits[temp_chan] = 1;
				devc->cur_encoding[temp_chan] = 2;
			}
			sdi->inst_type = SR_INST_SERIAL;
			sdi->conn = serial;
			sdi->priv = devc;
			sr_channel_new(sdi, 0, SR_CHANNEL_ANALOG, TRUE, "P1");
			if (supported_agdmm[i].nb_channels > 1)
				sr_channel_new(sdi, 1, SR_CHANNEL_ANALOG, TRUE, "P2");
			if (supported_agdmm[i].nb_channels > 2)
				sr_channel_new(sdi, 2, SR_CHANNEL_ANALOG, TRUE, "P3");
			devices = g_slist_append(devices, sdi);
			break;
		}
	}
	g_strfreev(tokens);
	g_free(buf);

	serial_close(serial);
	if (!devices)
		sr_serial_dev_inst_free(serial);

	return std_scan_complete(di, devices);
}

static int config_get(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	int ret;

	(void)cg;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		ret = sr_sw_limits_config_get(&devc->limits, key, data);
		break;
	case SR_CONF_DATA_SOURCE:
		*data = g_variant_new_string(data_sources[devc->data_source]);
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t samplerate;
	const char *tmp_str;
	unsigned int i;
	int ret;

	(void)cg;

	devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
	case SR_CONF_SAMPLERATE:
		samplerate = g_variant_get_uint64(data);
		if (samplerate < samplerates[0] || samplerate > samplerates[1])
			ret = SR_ERR_ARG;
		else
			devc->cur_samplerate = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_LIMIT_MSEC:
		ret = sr_sw_limits_config_set(&devc->limits, key, data);
		break;
	case SR_CONF_DATA_SOURCE: {
		tmp_str = g_variant_get_string(data, NULL);
		for (i = 0; i < ARRAY_SIZE(data_sources); i++)
			if (!strcmp(tmp_str, data_sources[i])) {
				devc->data_source = i;
				break;
			}
		if (i == ARRAY_SIZE(data_sources))
			return SR_ERR;
		break;
	}
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates_steps(samplerates, ARRAY_SIZE(samplerates));
		break;
	case SR_CONF_DATA_SOURCE:
		*data = g_variant_new_strv(data_sources, ARRAY_SIZE(data_sources));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_serial_dev_inst *serial;

	devc->cur_channel = sr_next_enabled_channel(sdi, NULL);
	devc->cur_conf = sr_next_enabled_channel(sdi, NULL);
	devc->cur_sample = 1;
	devc->cur_mq[0] = -1;
	if (devc->profile->nb_channels > 2)
		devc->cur_mq[1] = -1;

	if (devc->data_source == DATA_SOURCE_LIVE) {
		devc->jobs = devc->profile->jobs_live;
	} else {
		devc->jobs = devc->profile->jobs_log;
		if (!devc->jobs) {
			sr_err("Log data source is not implemented for this model.");
			return SR_ERR_NA;
		}
		if (!((struct sr_channel *)sdi->channels->data)->enabled) {
			sr_err("Log data is only available for channel P1.");
			return SR_ERR_NA;
		}
	}

	sr_sw_limits_acquisition_start(&devc->limits);
	std_session_send_df_header(sdi);

	serial = sdi->conn;
	serial_source_add(sdi->session, serial, G_IO_IN, 10,
			agdmm_receive_data, (void *)sdi);

	return SR_OK;
}

static struct sr_dev_driver agdmm_driver_info = {
	.name = "agilent-dmm",
	.longname = "Agilent U12xx series DMMs",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = std_serial_dev_open,
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = std_serial_dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(agdmm_driver_info);
