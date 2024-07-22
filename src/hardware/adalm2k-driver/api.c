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

#include <config.h>
#include <iio/iio.h>
#include "libsigrok/libsigrok.h"
#include "protocol.h"

#define M2K_VID     0x0456
#define M2K_PID     0xb672 

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

static struct sr_dev_driver adalm2k_driver_driver_info;

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
    struct dev_context *devc;
    struct sr_dev_inst *sdi;
    /* struct sr_usb_dev_inst *usb; */
    struct sr_config *src;

	GSList *devices;

	(void)options;

	devices = NULL;
	drvc = di->context;
	drvc->instances = NULL;

    // TODO: Clean this, very basic concept
    if(iio_scan(NULL, "usb=0456:b672")) {
        sr_dbg("Found M2K.");
        sdi = g_malloc(sizeof(struct sr_dev_inst));
        sdi->status = SR_ST_INITIALIZING;
        sdi->vendor = g_strdup("Analog Devices");
        sdi->model = g_strdup("M2K");
        sdi->connection_id = 0;
        sdi->conn = g_strdup("usb=0456:b672");
        sdi->inst_type = SR_INST_USB;
        sdi->driver = NULL;
        sdi->session = NULL;
        sdi->version = g_strdup("0.0.1");
        sdi->channels = NULL;
        sdi->serial_num = NULL;
        sdi->channel_groups = NULL;

        devc = g_malloc(sizeof(struct dev_context));
        devc->m2k = NULL;
        devc->logic_unitsize = 2;
        devc->buffersize = 1 << 16;
        devc->meaning.mq = SR_MQ_VOLTAGE;
        devc->meaning.unit = SR_UNIT_VOLT;
        devc->meaning.mqflags = 0;
        
        sdi->priv = devc;
        devices = g_slist_append(devices, sdi);
    }
    /* sr_dbg("???"); */

	return std_scan_complete(di, devices);
    /* return devices; */
}

static int dev_open(struct sr_dev_inst *sdi)
{
	(void)sdi;
    struct dev_context *devc;
    devc = sdi->priv;
    devc->m2k = iio_create_context(NULL, sdi->conn);
    if (!devc->m2k) {
        sr_err("Failed to open device");
        return SR_ERR;
    }

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
    sr_dbg("???");
	(void)sdi;
    struct dev_context *devc;
    devc = sdi->priv;
    iio_context_destroy(devc->m2k);

	/* TODO: get handle from sdi->conn and close it. */

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	int ret;

	(void)sdi;
	(void)data;
	(void)cg;

	ret = SR_OK;
	switch (key) {
	/* TODO */
	default:
		return SR_ERR_NA;
	}

	return ret;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	/* TODO: configure hardware, reset acquisition state, set up
	 * callbacks and send header packet. */

	(void)sdi;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	/* TODO: stop acquisition. */

	(void)sdi;

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
