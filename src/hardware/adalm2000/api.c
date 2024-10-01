/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Analog Devices Inc.
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


static const uint32_t scanopts[] = {
	SR_CONF_CONN,
};

static const uint32_t drvopts[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_OSCILLOSCOPE,
};

static const uint32_t devopts[] = {
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_LIMIT_MSEC | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_AVERAGING | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_AVG_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg_analog_group[] = {
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_analog_channel[] = {
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_HIGH_RESOLUTION | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_TRIGGER_LEVEL | SR_CONF_GET | SR_CONF_SET,
};

static const uint32_t devopts_cg[] = {
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

static const uint64_t samplerates[] = {
	SR_KHZ(1),
	SR_KHZ(10),
	SR_KHZ(100),
	SR_MHZ(1),
	SR_MHZ(10),
	SR_MHZ(100),
};

static const char *trigger_sources[] = {
	"CHANNEL 1",
	"CHANNEL 2",
	"CHANNEL 1 OR CHANNEL 2",
	"CHANNEL 1 AND CHANNEL 2",
	"CHANNEL 1 XOR CHANNEL 2",
	"NONE",
};

static const char *trigger_slopes[] = {
	"RISING",
	"FALLING",
	"LOW",
	"HIGH",
};

static struct sr_dev_driver adalm2000_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;

	struct sr_channel_group *cg, *acg;
	struct sr_config *src;
	GSList *l, *devices;
	struct CONTEXT_INFO **devlist;
	unsigned int i, j, len;
	char *conn;
	char channel_name[16];
	char ip[30];
	gboolean ip_connection;

	conn = NULL;
	ip_connection = FALSE;

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = (char *) g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (conn) {
		if (strstr(conn, "tcp")) {
			strtok(conn, "/");
			snprintf(ip, 30, "ip:%s", strtok(NULL, "/"));
			ip_connection = TRUE;
		}
	}
	devices = NULL;

	len = sr_libm2k_context_get_all(&devlist);

	for (i = 0; i < len; i++) {
		struct CONTEXT_INFO *info = (struct CONTEXT_INFO *) devlist[i];

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		devc = g_malloc0(sizeof(struct dev_context));

		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup(info->manufacturer);
		sdi->model = g_strdup(info->product);
		sdi->serial_num = g_strdup(info->serial);
		sdi->connection_id = g_strdup(info->uri);
		if (ip_connection) {
			sdi->conn = g_strdup(ip);
		} else {
			sdi->conn = g_strdup(info->uri);
		}

		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup("Logic");

		for (j = 0; j < DEFAULT_NUM_LOGIC_CHANNELS; j++) {
			snprintf(channel_name, 16, "DIO%d", j);
			ch = sr_channel_new(sdi, j, SR_CHANNEL_LOGIC, TRUE,
					    channel_name);
			cg->channels = g_slist_append(cg->channels, ch);
		}
		sdi->channel_groups = g_slist_append(NULL, cg);

		acg = g_malloc0(sizeof(struct sr_channel_group));
		acg->name = g_strdup("Analog");
		sdi->channel_groups = g_slist_append(sdi->channel_groups, acg);

		for (j = 0; j < DEFAULT_NUM_ANALOG_CHANNELS; j++) {
			snprintf(channel_name, 16, "A%d", j);

			cg = g_malloc0(sizeof(struct sr_channel_group));
			cg->name = g_strdup(channel_name);

			ch = sr_channel_new(sdi, j,
					    SR_CHANNEL_ANALOG, TRUE,
					    channel_name);

			acg->channels = g_slist_append(acg->channels, ch);

			cg->channels = g_slist_append(cg->channels, ch);
			sdi->channel_groups = g_slist_append(
				sdi->channel_groups, cg);

		}
		devc->m2k = NULL;
		devc->logic_unitsize = 2;
		devc->buffersize = 1 << 16;
		sr_analog_init(&devc->packet, &devc->encoding, &devc->meaning,
			       &devc->spec, 6);

		devc->meaning.mq = SR_MQ_VOLTAGE;
		devc->meaning.unit = SR_UNIT_VOLT;
		devc->meaning.mqflags = 0;

		sdi->priv = devc;
		sdi->inst_type = SR_INST_USB;

		devices = g_slist_append(devices, sdi);
	}
	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	devc->m2k = sr_libm2k_context_open(sdi->conn);
	if (!devc->m2k) {
		sr_err("Failed to open device");
		return SR_ERR;
	}
	sr_libm2k_context_adc_calibrate(devc->m2k);
	devc->avg_samples = sr_libm2k_analog_oversampling_ratio_get(devc->m2k);

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	sr_info("Closing device on ...");
	ret = sr_libm2k_context_close(&(devc->m2k));
	if (ret) {
		sr_err("Failed to close device");
		return SR_ERR;
	}
	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
		      const struct sr_dev_inst *sdi,
		      const struct sr_channel_group *cg)
{
	unsigned int idx, capture_ratio, samplerate;
	int delay;
	gboolean analog_enabled, digital_enabled;
	struct sr_channel *ch;
	struct dev_context *devc;
	if (!sdi) {
		return SR_ERR_ARG;
	}

	devc = sdi->priv;

	if (!cg) {
		switch (key) {
		case SR_CONF_SAMPLERATE:
			digital_enabled = (adalm2000_nb_enabled_channels(sdi, SR_CHANNEL_LOGIC) > 0)
					  ? TRUE : FALSE;
			samplerate = sr_libm2k_analog_samplerate_get(devc->m2k);
			if (digital_enabled) {
				sr_libm2k_digital_samplerate_set(devc->m2k, samplerate);
			}
			*data = g_variant_new_uint64(samplerate);
			break;
		case SR_CONF_LIMIT_SAMPLES:
			*data = g_variant_new_uint64(devc->limit_samples);
			break;
		case SR_CONF_LIMIT_MSEC:
			*data = g_variant_new_uint64(devc->limit_msec);
			break;
		case SR_CONF_AVERAGING:
			*data = g_variant_new_boolean(devc->avg);
			break;
		case SR_CONF_AVG_SAMPLES:
			*data = g_variant_new_uint64(devc->avg_samples);
			break;
		case SR_CONF_CAPTURE_RATIO:
			analog_enabled = (adalm2000_nb_enabled_channels(sdi, SR_CHANNEL_ANALOG) > 0)
					 ? TRUE : FALSE;
			digital_enabled = (adalm2000_nb_enabled_channels(sdi, SR_CHANNEL_LOGIC) > 0)
					  ? TRUE : FALSE;

			if (analog_enabled) {
				delay = sr_libm2k_analog_trigger_delay_get(devc->m2k);
			} else {
				delay = sr_libm2k_digital_trigger_delay_get(devc->m2k);
			}
			if (delay > 0) {
				delay = 0;
				capture_ratio = 0;
			} else {
				capture_ratio = delay * 100 / MAX_NEG_DELAY;
			}

			if (analog_enabled) {
				sr_libm2k_analog_trigger_delay_set(devc->m2k, 0);
			}
			if (digital_enabled) {
				sr_libm2k_digital_trigger_delay_set(devc->m2k, delay);
			}
			*data = g_variant_new_uint64(capture_ratio);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		ch = cg->channels->data;
		idx = ch->index;
		switch (key) {
		case SR_CONF_TRIGGER_SOURCE:
			if (sr_libm2k_analog_trigger_mode_get(devc->m2k, 0) == ALWAYS &&
			    sr_libm2k_analog_trigger_mode_get(devc->m2k, 1) == ALWAYS) {
				*data = g_variant_new_string(trigger_sources[5]);
			} else {
				*data = g_variant_new_string(
					trigger_sources[sr_libm2k_analog_trigger_source_get(devc->m2k)]);
			}
			break;
		case SR_CONF_TRIGGER_SLOPE:
			if (ch->type != SR_CHANNEL_ANALOG) {
				return SR_ERR_ARG;
			}
			*data = g_variant_new_string(
				trigger_slopes[sr_libm2k_analog_trigger_condition_get(devc->m2k, idx)]);
			break;
		case SR_CONF_TRIGGER_LEVEL:
			*data = g_variant_new_double(sr_libm2k_analog_trigger_level_get(devc->m2k, idx));
			break;
		case SR_CONF_HIGH_RESOLUTION:
			if (ch->type != SR_CHANNEL_ANALOG) {
				return SR_ERR_ARG;
			}
			if (sr_libm2k_analog_range_get(devc->m2k, idx) == PLUS_MINUS_2_5V) {
				*data = g_variant_new_boolean(TRUE);
			} else {
				*data = g_variant_new_boolean(FALSE);
			}
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
	int ch_idx, idx, delay;
	char *trigger_source, *trigger_slope;
	gboolean analog_enabled, digital_enabled, high_resolution;
	struct sr_channel *ch;
	struct dev_context *devc;

	if (!sdi) {
		return SR_ERR_ARG;
	}

	devc = sdi->priv;

	if (!cg) {
		switch (key) {
		case SR_CONF_SAMPLERATE:
			analog_enabled = (adalm2000_nb_enabled_channels(sdi, SR_CHANNEL_ANALOG) > 0)
					 ? TRUE : FALSE;
			digital_enabled = (adalm2000_nb_enabled_channels(sdi, SR_CHANNEL_LOGIC) > 0)
					  ? TRUE : FALSE;
			if (analog_enabled) {
				sr_libm2k_analog_samplerate_set(devc->m2k, g_variant_get_uint64(data));
			}
			if (digital_enabled) {
				sr_libm2k_digital_samplerate_set(devc->m2k, g_variant_get_uint64(data));
			}
			break;
		case SR_CONF_LIMIT_SAMPLES:
			devc->limit_samples = g_variant_get_uint64(data);
			devc->limit_msec = 0;
			break;
		case SR_CONF_LIMIT_MSEC:
			devc->limit_msec = g_variant_get_uint64(data);
			devc->limit_samples = 0;
			break;
		case SR_CONF_CAPTURE_RATIO:
			analog_enabled = (adalm2000_nb_enabled_channels(sdi, SR_CHANNEL_ANALOG) > 0)
					 ? TRUE : FALSE;
			digital_enabled = (adalm2000_nb_enabled_channels(sdi, SR_CHANNEL_LOGIC) > 0)
					  ? TRUE : FALSE;
			delay = MAX_NEG_DELAY *
				(int) g_variant_get_uint64(data) / 100;
			if (analog_enabled) {
				sr_libm2k_analog_trigger_delay_set(devc->m2k, delay);
			}
			if (digital_enabled) {
				sr_libm2k_digital_trigger_delay_set(devc->m2k, delay);
			}
			break;
		case SR_CONF_AVERAGING:
			devc->avg = g_variant_get_boolean(data);
			break;
		case SR_CONF_AVG_SAMPLES:
			devc->avg_samples = g_variant_get_uint64(data);
			break;
		default:
			return SR_ERR_NA;
		}
	} else {
		ch = cg->channels->data;
		ch_idx = ch->index;

		switch (key) {
		case SR_CONF_TRIGGER_SOURCE:
			if (ch->type != SR_CHANNEL_ANALOG) {
				return SR_ERR_ARG;
			}
			if ((idx = std_str_idx(data, ARRAY_AND_SIZE(trigger_sources))) < 0) {
				return SR_ERR_ARG;
			}
			trigger_source = g_strdup(trigger_sources[idx]);
			if (!strcmp(trigger_source, trigger_sources[0])) {
				sr_libm2k_analog_trigger_source_set(devc->m2k, CH_1);
				sr_libm2k_analog_trigger_mode_set(devc->m2k, 0, ANALOG);
				sr_libm2k_analog_trigger_mode_set(devc->m2k, 1, ALWAYS);
			} else if (!strcmp(trigger_source, trigger_sources[1])) {
				sr_libm2k_analog_trigger_source_set(devc->m2k, CH_2);
				sr_libm2k_analog_trigger_mode_set(devc->m2k, 0, ALWAYS);
				sr_libm2k_analog_trigger_mode_set(devc->m2k, 1, ANALOG);
			} else if (!strcmp(trigger_source, trigger_sources[2])) {
				sr_libm2k_analog_trigger_source_set(devc->m2k, CH_1_OR_CH_2);
				sr_libm2k_analog_trigger_mode_set(devc->m2k, 0, ANALOG);
				sr_libm2k_analog_trigger_mode_set(devc->m2k, 1, ANALOG);
			} else if (!strcmp(trigger_source, trigger_sources[3])) {
				sr_libm2k_analog_trigger_source_set(devc->m2k, CH_1_AND_CH_2);
				sr_libm2k_analog_trigger_mode_set(devc->m2k, 0, ANALOG);
				sr_libm2k_analog_trigger_mode_set(devc->m2k, 1, ANALOG);
			} else if (!strcmp(trigger_source, trigger_sources[4])) {
				sr_libm2k_analog_trigger_source_set(devc->m2k, CH_1_XOR_CH_2);
				sr_libm2k_analog_trigger_mode_set(devc->m2k, 0, ANALOG);
				sr_libm2k_analog_trigger_mode_set(devc->m2k, 1, ANALOG);
			} else {
				sr_libm2k_analog_trigger_mode_set(devc->m2k, 0, ALWAYS);
				sr_libm2k_analog_trigger_mode_set(devc->m2k, 1, ALWAYS);
			}
			g_free(trigger_source);
			break;
		case SR_CONF_TRIGGER_SLOPE:
			if (ch->type != SR_CHANNEL_ANALOG) {
				return SR_ERR_ARG;
			}
			if ((idx = std_str_idx(data, ARRAY_AND_SIZE(trigger_slopes))) < 0) {
				return SR_ERR_ARG;
			}
			trigger_slope = g_strdup(trigger_slopes[idx]);
			if (strcmp(trigger_slope, trigger_slopes[0]) == 0) {
				sr_libm2k_analog_trigger_condition_set(devc->m2k, ch_idx, RISING);
			} else if (strcmp(trigger_slope, trigger_slopes[1]) == 0) {
				sr_libm2k_analog_trigger_condition_set(devc->m2k, ch_idx, FALLING);
			} else if (strcmp(trigger_slope, trigger_slopes[2]) == 0) {
				sr_libm2k_analog_trigger_condition_set(devc->m2k, ch_idx, LOW);
			} else if (strcmp(trigger_slope, trigger_slopes[3]) == 0) {
				sr_libm2k_analog_trigger_condition_set(devc->m2k, ch_idx, HIGH);
			}
			g_free(trigger_slope);
			break;
		case SR_CONF_TRIGGER_LEVEL:
			analog_enabled = (adalm2000_nb_enabled_channels(sdi, SR_CHANNEL_ANALOG) > 0)
					 ? TRUE : FALSE;
			if (analog_enabled) {
				sr_libm2k_analog_trigger_level_set(devc->m2k, ch_idx,
								   g_variant_get_double(data));
			}
			break;
		case SR_CONF_HIGH_RESOLUTION:
			if (ch->type != SR_CHANNEL_ANALOG) {
				return SR_ERR_ARG;
			}
			high_resolution = g_variant_get_boolean(data);
			if (high_resolution) {
				sr_libm2k_analog_range_set(devc->m2k, ch_idx, PLUS_MINUS_2_5V);
			} else {
				sr_libm2k_analog_range_set(devc->m2k, ch_idx, PLUS_MINUS_25V);
			}
			break;
		default:
			return SR_ERR_NA;
		}
	}
	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
		       const struct sr_dev_inst *sdi,
		       const struct sr_channel_group *cg)
{
	struct sr_channel *ch;

	if (!cg) {
		switch (key) {
		case SR_CONF_SCAN_OPTIONS:
		case SR_CONF_DEVICE_OPTIONS:
			return STD_CONFIG_LIST(key, data, sdi, cg,
					       scanopts, drvopts,
					       devopts);

		case SR_CONF_SAMPLERATE:
			*data = std_gvar_samplerates(
				ARRAY_AND_SIZE(samplerates));
			break;
		case SR_CONF_TRIGGER_MATCH:
			*data = std_gvar_array_i32(
				ARRAY_AND_SIZE(trigger_matches));
			break;

		default:
			return SR_ERR_NA;
		}
	} else {
		ch = cg->channels->data;

		switch (key) {
		case SR_CONF_DEVICE_OPTIONS:
			if (ch->type == SR_CHANNEL_ANALOG) {
				if (strcmp(cg->name, "Analog") == 0) {
					*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_analog_group));
				} else {
					*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_analog_channel));
				}
			} else {
				*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg));
			}
			break;
		case SR_CONF_TRIGGER_SOURCE:
			*data = g_variant_new_strv(ARRAY_AND_SIZE(trigger_sources));
			break;
		case SR_CONF_TRIGGER_SLOPE:
			*data = g_variant_new_strv(ARRAY_AND_SIZE(trigger_slopes));
			break;
		default:
			return SR_ERR_NA;
		}
	}
	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	GSList *l;
	gboolean analog_enabled, digital_enabled;
	struct sr_channel *ch;

	devc = sdi->priv;
	devc->sent_samples = 0;
	analog_enabled = (adalm2000_nb_enabled_channels(sdi, SR_CHANNEL_ANALOG) > 0) ? TRUE : FALSE;
	digital_enabled = (adalm2000_nb_enabled_channels(sdi, SR_CHANNEL_LOGIC) > 0) ? TRUE : FALSE;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type == SR_CHANNEL_ANALOG) {
			sr_libm2k_analog_channel_enable(devc->m2k, ch->index, 1);
		}
	}

	if (adalm2000_convert_trigger(sdi) != SR_OK) {
		sr_err("Failed to configure triggers.");
		return SR_ERR;
	}

	if (analog_enabled) {
		if (devc->avg) {
			sr_libm2k_analog_oversampling_ratio_set(devc->m2k, devc->avg_samples);
		}
		sr_libm2k_analog_kernel_buffers_count_set(devc->m2k, 64);
		sr_libm2k_analog_streaming_flag_set(devc->m2k, 0);
	}
	if (digital_enabled) {
		sr_libm2k_digital_kernel_buffers_count_set(devc->m2k, 64);
		sr_libm2k_digital_streaming_flag_set(devc->m2k, 0);
	}

	if (sr_libm2k_has_mixed_signal(devc->m2k)) {
		sr_libm2k_mixed_signal_acquisition_start(devc->m2k, devc->buffersize);
	} else {
		if (analog_enabled) {
			sr_libm2k_analog_acquisition_start(devc->m2k, devc->buffersize);
		}
		if (digital_enabled) {
			sr_libm2k_digital_acquisition_start(devc->m2k, devc->buffersize);
		}
	}

	std_session_send_df_header(sdi);
	sr_session_source_add(sdi->session, -1, G_IO_IN, 0, adalm2000_receive_data,
			      (struct sr_dev_inst *) sdi);

	devc->start_time = g_get_monotonic_time();
	devc->spent_us = 0;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	gboolean analog_enabled, digital_enabled;

	devc = sdi->priv;

	sr_libm2k_analog_acquisition_cancel(devc->m2k);
	sr_libm2k_digital_acquisition_cancel(devc->m2k);

	analog_enabled = (adalm2000_nb_enabled_channels(sdi, SR_CHANNEL_ANALOG) > 0) ? TRUE : FALSE;
	digital_enabled = (adalm2000_nb_enabled_channels(sdi, SR_CHANNEL_LOGIC) > 0) ? TRUE : FALSE;

	if (sr_libm2k_has_mixed_signal(devc->m2k)) {
		sr_libm2k_mixed_signal_acquisition_stop(devc->m2k);
	} else {
		if (digital_enabled) {
			sr_libm2k_digital_acquisition_stop(devc->m2k);
		}
		if (analog_enabled) {
			sr_libm2k_analog_acquisition_stop(devc->m2k);
		}
	}

	sr_session_source_remove(sdi->session, -1);
	std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver adalm2000_driver_info = {
	.name = "adalm2000",
	.longname = "ADALM2000",
	.api_version = 1,
	.init = std_init,
	.cleanup = std_cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = std_dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(adalm2000_driver_info);
