/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024 Sean W Jasin <swjasin03@gmail.com>
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

#include "libsigrok/libsigrok.h"
#include "protocol.h"
#include <config.h>
#include <iio/iio.h>

#define M2K_VID 0x0456
#define M2K_PID 0xb672

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

static const uint32_t devopts_cg[] = {};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,	SR_TRIGGER_ONE,	 SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING, SR_TRIGGER_EDGE,
};

static const uint64_t samplerates[] = {
	SR_KHZ(1), SR_KHZ(10), SR_KHZ(100), SR_MHZ(1), SR_MHZ(10), SR_MHZ(100),
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

static struct sr_dev_driver adalm2k_driver_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options) {
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
    struct sr_channel_group *cg;
    struct sr_channel *ch;
    char channel_name[16];
	/* struct sr_usb_dev_inst *usb; */
	struct sr_config *src;

	GSList *devices;

	(void)options;

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

	/* TODO: Clean this, very basic concept */
    struct iio_scan *scan = iio_scan(NULL, "usb=0456:b672");
    if(iio_scan_get_results_count(scan) > 0) {
		sr_dbg("Found M2K.");
        char *uri = g_strdup((char *)iio_scan_get_uri(scan, 0));
		sdi = g_malloc(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INITIALIZING;
		sdi->vendor = g_strdup("Analog Devices");
		sdi->model = g_strdup("M2K");
		sdi->connection_id = (void *)uri;
		sdi->conn = NULL;
		sdi->inst_type = SR_INST_USB;
		sdi->driver = NULL;
		sdi->session = NULL;
		sdi->version = g_strdup("0.0.1");
		sdi->channels = NULL;
		sdi->serial_num = NULL;

		cg = g_malloc0(sizeof(struct sr_channel_group));
		cg->name = g_strdup("Logic");

		for (int j = 0; j < DEFAULT_NUM_LOGIC_CHANNELS; j++) {
			snprintf(channel_name, 16, "DIO%d", j);
			ch = sr_channel_new(sdi, j, SR_CHANNEL_LOGIC, TRUE, channel_name);
			cg->channels = g_slist_append(cg->channels, ch);
		}
		sdi->channel_groups = g_slist_append(NULL, cg);

		devc = g_malloc(sizeof(struct dev_context));
		devc->m2k = NULL;
        devc->mask = iio_create_channels_mask(18);
		devc->logic_unitsize = 2;
		devc->buffersize = 1 << 16;
		devc->meaning.mq = SR_MQ_VOLTAGE;
		devc->meaning.unit = SR_UNIT_VOLT;
		devc->meaning.mqflags = 0;

		sdi->priv = devc;
        sdi->inst_type = SR_INST_USB;
		devices = g_slist_append(devices, sdi);
	}
    iio_scan_destroy(scan);

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi) {
    int err;
	(void)sdi;
	struct dev_context *devc;
	devc = sdi->priv;
	devc->m2k = iio_create_context(NULL, sdi->connection_id);
    err = iio_err(devc->m2k);
	if (err) {
		sr_err("Failed to open device");
		return SR_ERR;
	}
	sr_dbg("OK");
	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi) {
	(void)sdi;
	struct dev_context *devc;
	devc = sdi->priv;
    if (devc->m2k) {
        iio_context_destroy(devc->m2k);
    }
    /* NOTE: No valid way of checking if the device has been destroyed properly */ 
	/* if (devc->m2k) { */
	/* 	sr_err("Failed to close device"); */
	/* 	return SR_ERR; */
	/* } */
    sr_dbg("Successfully closed device");

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
					  const struct sr_dev_inst *sdi,
					  const struct sr_channel_group *cg) {
	unsigned int idx, capture_ratio, samplerate;
	int delay;
	gboolean analog_en, digital_en;
	struct sr_channel *ch;
	struct dev_context *devc;

	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	if (!sdi) {
		return SR_ERR_ARG;
	}

	ret = SR_OK;

	devc = sdi->priv;
    sr_dbg("Getting configs");

	switch (key) {
	case SR_CONF_SAMPLERATE:
        sr_dbg("SAMPLERATE");
		digital_en =
			adalm2k_driver_nb_enabled_channels(sdi, SR_CHANNEL_LOGIC) > 0;
		if (digital_en) {
		}
        /* samplerate = 99999999; */
		*data = g_variant_new_uint64(devc->samplerate);
		/* TODO: samplerate = m2k_get_samplerate(sdi); */
		/* TODO: add support for analog */
		break;
	case SR_CONF_LIMIT_SAMPLES:
        sr_dbg("LIMIT SAMPLES");
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_LIMIT_MSEC:
        sr_dbg("LIMIT MSEC");
		*data = g_variant_new_uint64(devc->limit_msec);
		break;
	case SR_CONF_AVERAGING:
        sr_dbg("AVERAGING");
		*data = g_variant_new_boolean(devc->avg);
		break;
	case SR_CONF_AVG_SAMPLES:
        sr_dbg("AVG SAMPLES");
		*data = g_variant_new_uint64(devc->avg_samples);
		break;
	case SR_CONF_CAPTURE_RATIO:
        *data = g_variant_new_uint64(0);
        sr_dbg("CAP RATIO");
		break;
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
					  const struct sr_dev_inst *sdi,
					  const struct sr_channel_group *cg) {
	struct sr_channel *ch;
	struct dev_context *devc;

	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	devc = sdi->priv;

	ret = SR_OK;
    sr_dbg("Setting configs");
	switch (key) {
	case SR_CONF_SAMPLERATE:
        sr_dbg("SAMPLERATE");
        devc->samplerate = g_variant_get_uint64(data);
        /* Set samplerate when starting acquisition */
        /* if(adalm2k_driver_set_samplerate(sdi) < 0) { */
        /*     sr_err("Failed to set samplerate"); */
			/* ret = SR_ERR_NA; */
			/* break; */
		/* } */
        /* sr_dbg("Successfully changed samplerate"); */
		/* TODO: implement analog */
		break;
	case SR_CONF_LIMIT_SAMPLES:
        sr_dbg("LIMIT SAMPLES");
		devc->limit_samples = g_variant_get_uint64(data);
		devc->limit_msec = 0;
		break;
	case SR_CONF_LIMIT_MSEC:
        sr_dbg("LIMIT MSEC");
		devc->limit_msec = g_variant_get_uint64(data);
		devc->limit_samples = 0;
		break;
	case SR_CONF_CAPTURE_RATIO:
        sr_dbg("CAP RATIO");
		break;
	case SR_CONF_AVERAGING:
        sr_dbg("AVERAGING");
		devc->avg = g_variant_get_boolean(data);
		break;
	case SR_CONF_AVG_SAMPLES:
        sr_dbg("AVG SAMPLES");
		devc->avg_samples = g_variant_get_uint64(data);
		break;
	/* TODO */
	default:
        sr_dbg("ERR");
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
					   const struct sr_dev_inst *sdi,
					   const struct sr_channel_group *cg) {
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
    sr_dbg("Listing configs");
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
		break;
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
        break;
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi) {
	/* TODO: configure hardware, reset acquisition state, set up
	 * callbacks and send header packet. */
    struct dev_context *devc;
    gboolean analog_en, digital_en;
    struct sr_channel *ch;
    GSList *l;

	(void)sdi;

    devc = sdi->priv;

    devc->sent_samples = 0;
    analog_en = adalm2k_driver_nb_enabled_channels(sdi, SR_CHANNEL_ANALOG) ? TRUE : FALSE;
    digital_en = adalm2k_driver_nb_enabled_channels(sdi, SR_CHANNEL_LOGIC) ? TRUE : FALSE;
    adalm2k_driver_set_samplerate(sdi);

    for(l = sdi->channels; l; l = l->next) {
        ch = l->data;
        if(ch->type == SR_CHANNEL_LOGIC) {
            sr_dbg("Enabling channels");
            adalm2k_driver_enable_channel(sdi, ch->index);
        }
    }

    std_session_send_df_header(sdi);
    sr_session_source_add(sdi->session, -1, G_IO_IN, 0, adalm2k_driver_receive_data, (struct sr_dev_inst *)sdi);

    devc->start_time = g_get_monotonic_time();
    devc->spent_us = 0;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi) {
	/* TODO: stop acquisition. */

	(void)sdi;

    sr_session_source_remove(sdi->session, -1);
    std_session_send_df_end(sdi);

	return SR_OK;
}

static struct sr_dev_driver adalm2k_driver_driver_info = {
	.name = "adalm2k-driver",
	.longname = "adalm2k-driver",
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
SR_REGISTER_DEV_DRIVER(adalm2k_driver_driver_info);
