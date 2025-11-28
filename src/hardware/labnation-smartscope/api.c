/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Karl Palsson <karlp@tweak.net.au>
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

#include <string.h>
#include <config.h>
#include "protocol.h"

#define POLL_INTERVAL_MS   1 // 2048k bytes per second
// ./configure --enable-all-drivers=no --enable-demo --enable-labnation-smartscope --enable-fx2lafw

/* scan options */
static const uint32_t scanopts[] = {
    //SR_CONF_NUM_LOGIC_CHANNELS,
	//SR_CONF_NUM_ANALOG_CHANNELS,
    SR_CONF_PROBE_NAMES
};

/* TODO - it can be much more, but one step at a time */
static const uint32_t drvopts[] = {
    SR_CONF_LOGIC_ANALYZER,
#if ENABLE_SCOPE
	SR_CONF_OSCILLOSCOPE
#endif
};

static const uint32_t devopts[] = {
	//SR_CONF_CONTINUOUS,
    //SR_CONF_LIMIT_FRAMES | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_LIMIT_SAMPLES | SR_CONF_LIST | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_SAMPLERATE    | SR_CONF_LIST | SR_CONF_GET | SR_CONF_SET,
    SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
    SR_CONF_CAPTURE_RATIO | SR_CONF_GET | SR_CONF_SET,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
};

static const uint64_t samplerates[] = {
	SR_MHZ(6.25),
    SR_MHZ(12.5),
    SR_MHZ(25),
    SR_MHZ(50),
    SR_MHZ(100)
};

static const char *channel_names_logic[] = {
	"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7"
};

#if ENABLE_SCOPE
static const char *channel_names_analog[] = {
	"A", "B"
};

static const uint32_t devopts_cg_logic[] = {

};

static const uint32_t devopts_cg_analog_group[] = {

};

static const uint32_t devopts_cg_analog_channel[] = {
	//SR_CONF_MEASURED_QUANTITY | SR_CONF_GET | SR_CONF_SET,
	//SR_CONF_AMPLITUDE | SR_CONF_GET | SR_CONF_SET,
	//SR_CONF_OFFSET | SR_CONF_GET | SR_CONF_SET,

};
#endif
static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct sr_channel_group *cg;
	GSList *gs_list;
	struct libusb_device_descriptor des;
	libusb_device **devlist;
	struct libusb_device_handle *hdl;
	int ret, i, j;
	char manufacturer[64], product[64], serial_num[64], connection_id[64];

    (void)options;

	gs_list = NULL;
	drvc = di->context;
	drvc->instances = NULL;

    sr_dbg("--- Scanning for devices ---");

	libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);

	for (i = 0; devlist[i]; i++) {
		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != LNSS_VID && des.idProduct != LNSS_PID) {
			continue;
		}

		if ((ret = libusb_open(devlist[i], &hdl)) < 0) {
			sr_warn("Failed to open potential device with "
				"VID:PID %04x:%04x: %s.", des.idVendor,
				des.idProduct, libusb_error_name(ret));
			continue;
		}

		if (des.iManufacturer == 0) {
			manufacturer[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iManufacturer, (unsigned char *) manufacturer,
				sizeof(manufacturer))) < 0) {
			sr_warn("Failed to get manufacturer string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		if (des.iProduct == 0) {
			product[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iProduct, (unsigned char *) product,
				sizeof(product))) < 0) {
			sr_warn("Failed to get product string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		if (des.iSerialNumber == 0) {
			serial_num[0] = '\0';
		} else if ((ret = libusb_get_string_descriptor_ascii(hdl,
				des.iSerialNumber, (unsigned char *) serial_num,
				sizeof(serial_num))) < 0) {
			sr_warn("Failed to get serial number string descriptor: %s.",
				libusb_error_name(ret));
			continue;
		}

		usb_get_port_path(devlist[i], connection_id, sizeof(connection_id));

		libusb_close(hdl);

		sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INITIALIZING;
		sdi->model = g_strdup(product);
		sdi->vendor = g_strdup(manufacturer);
		// TODO - print this to a string? Or query it another way?
		//sdi->version = g_strdup(des.bcdDevice);
		sdi->serial_num = g_strdup(serial_num);
		sdi->connection_id = g_strdup(connection_id);

		sdi->status = SR_ST_INACTIVE;
		sdi->inst_type = SR_INST_USB;
		sdi->conn = sr_usb_dev_inst_new(libusb_get_bus_number(devlist[i]),
				libusb_get_device_address(devlist[i]), NULL);

        /* TODO Store anything else in sdi->priv */
		devc = g_malloc0(sizeof(struct dev_context));
		memcpy(devc->hw_rev, sdi->serial_num + strlen(sdi->serial_num) - 3, 3);
		devc->hw_rev[3] = 0;
        devc->capture_ratio = DEFAULT_CAPTURE_RACIO;
        devc->samplerate = DEFAULT_SAMPLERATE;
        devc->limit_samples = DEFAULT_NUM_SAMPLES;
        devc->acquisition_id = 255;

		sr_info("%s: Found device with sn: %s", __func__, sdi->serial_num);

        cg = sr_channel_group_new(sdi, "Logic", NULL);
        for (j = 0; j < LNSS_NUM_CHANNELS; j++) {
            ch = sr_channel_new(sdi, j, SR_CHANNEL_LOGIC, TRUE, channel_names_logic[j]);
            cg->channels = g_slist_append(cg->channels, ch);
        }
#if ENABLE_SCOPE
        /* Group analog channels */
        cg = sr_channel_group_new(sdi, "Analog", NULL);
        for (j = 0; j < LNSS_NUM_AN_CHANNELS; j++) {
			ch = sr_channel_new(sdi, j + LNSS_NUM_CHANNELS, SR_CHANNEL_ANALOG, TRUE, channel_names_analog[j]);
            cg->channels = g_slist_append(cg->channels, ch);
		}
#endif
        sdi->priv = devc;

        gs_list = g_slist_append(gs_list, sdi);

        // stop on first device
        break;
	}

	libusb_free_device_list(devlist, 1);

	return std_scan_complete(di, gs_list);
}

static int dev_clear(const struct sr_dev_driver *di)
{
	return std_dev_clear(di);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct sr_dev_driver *di = sdi->driver;
	struct drv_context *drvc = di->context;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int ret;
    char version[9];

	devc = sdi->priv;
	usb = sdi->conn;

	sr_dbg("Karl - opening dev: hwrev: %s", devc->hw_rev);

	if (sr_usb_open(drvc->sr_ctx->libusb_ctx, usb) != SR_OK)
		return SR_ERR;

	if ((ret = libusb_claim_interface(usb->devhdl, USB_INTERFACE)) < 0) {
		sr_err("Failed to claim interface: %s.",
			libusb_error_name(ret));
		return SR_ERR;
	}

    bool ok = lnss_get_pic_firmware_version(sdi->conn, (uint8_t*)version);
    if(ok){
        sr_dbg("PIC FW version: %s", version);
    }else{
        sr_warn("Failed to get PIC FW version");
    }

	// check current fpga version, and potentially upload
	ok = lnss_version_fpga(sdi->conn, version);
	if (!ok) {
		sr_dbg("fpga version was garbage, uploading based on hwrev");
		ok = lnss_load_fpga(sdi);
		if (!ok) {
			sr_err("Failed to load fpga on device!");
			return SR_ERR;
		}
		ok = lnss_version_fpga(sdi->conn, version);
		if (!ok) {
			sr_err("Failed to read back fpga version after load!");
			return SR_ERR;
		}
		sr_dbg("fpga version after load was: %s", version);
	} else {
		sr_dbg("fpga version sane: %s, no reason to upload", version);
	}

    lnss_init(sdi);

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	int res;
    struct sr_usb_dev_inst *usb;

    usb = sdi->conn;

    sr_info("%s: Releasing SmartScope usb interface", __func__);
    res = libusb_release_interface(usb->devhdl, 0);

    if(res){
        sr_err("libusb release error: %d", res);
        //return SR_ERR;
    }else{
        sr_err("Closing SmartScope usb device");
        libusb_close(usb->devhdl);
    }

	sdi->status = SR_ST_INACTIVE;

    lnss_cleanup(sdi);

	return SR_OK;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
    struct dev_context *devc;
	int ret;

	(void)cg;

    if (!sdi)
		return SR_ERR_ARG;

    devc = sdi->priv;

	ret = SR_OK;
	switch (key) {
        case SR_CONF_SAMPLERATE:
		    *data = g_variant_new_uint64(devc->samplerate);
		    break;
        case SR_CONF_LIMIT_SAMPLES:
		    *data = g_variant_new_uint64(devc->limit_samples);
		    break;
        case SR_CONF_CAPTURE_RATIO:
		    *data = g_variant_new_uint64(devc->capture_ratio);
		    break;
	    default:
		    return SR_ERR_NA;
	}

	return ret;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
    struct dev_context *devc;

	(void)cg;

    if (!sdi)
		return SR_ERR_ARG;

    devc = sdi->priv;

	switch (key) {
        case SR_CONF_SAMPLERATE:
            devc->samplerate = g_variant_get_uint64(data);
            break;
        case SR_CONF_LIMIT_SAMPLES:
		    devc->limit_samples = g_variant_get_uint64(data);
		    break;
        case SR_CONF_CAPTURE_RATIO:
            devc->capture_ratio = g_variant_get_uint64(data);
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
    struct dev_context *devc;

    devc = (sdi) ? sdi->priv : NULL;

    switch(key){
        case SR_CONF_SCAN_OPTIONS:
        case SR_CONF_DEVICE_OPTIONS:
            return std_opts_config_list(key, data, sdi, cg,
                ARRAY_AND_SIZE(scanopts),
                ARRAY_AND_SIZE(drvopts),
                (devc) ? devopts : NULL,
                (devc) ? ARRAY_SIZE(devopts) : 0);
        case SR_CONF_SAMPLERATE:
            *data = std_gvar_samplerates(ARRAY_AND_SIZE(samplerates));
			break;
		case SR_CONF_TRIGGER_MATCH:
			*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
			break;
        case SR_CONF_LIMIT_SAMPLES:
            *data = std_gvar_tuple_u64(2000, 4000000);
            break;
        default:
            return (sdi) ? SR_ERR_NA : SR_ERR_ARG;
    }

    return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
    const GSList *l;
	struct dev_context *devc;
    struct sr_trigger *trigger;
    struct sr_trigger_stage *stage;
    struct sr_trigger_match *match;
    uint8_t trg_rising_mask = 0;
    uint8_t trg_falling_mask = 0;
    uint8_t trg_high_mask = 0;
    uint8_t trg_low_mask = 0;
    int sr_error;

	devc = sdi->priv;
    devc->sent_samples = 0;

    /* Setup triggers */
    if ((trigger = sr_session_trigger_get(sdi->session))) {
        if (g_slist_length(trigger->stages) > 1){
			return SR_ERR_NA;
        }

        stage = g_slist_nth_data(trigger->stages, 0);

        if (!stage)
			return SR_ERR_ARG;

        for (l = stage->matches; l; l = l->next) {
			match = l->data;

			if (!match->match)
				continue;

			if (!match->channel->enabled)
				continue;

			int idx = match->channel->index;

			switch(match->match) {
			    case SR_TRIGGER_ZERO: trg_low_mask |= (1 << idx); break;
			    case SR_TRIGGER_ONE:  trg_high_mask |= (1 << idx); break;
			    case SR_TRIGGER_RISING: trg_rising_mask |= (1 << idx); break;
			    case SR_TRIGGER_FALLING: trg_falling_mask |= (1 << idx); break;
			    case SR_TRIGGER_EDGE:
                    trg_rising_mask |= (1 << idx);
                    trg_falling_mask |= (1 << idx);
                    break;
			    default:
				    break;
			}
		}
    }

#if ENABLE_SCOPE
    /* Prepare list of analog channels */
    g_slist_free(devc->enabled_analog_channels);
	devc->enabled_analog_channels = NULL;

	for (l = sdi->channels; l; l = l->next) {
		struct sr_channel *ch = l->data;
		if ((ch->type == SR_CHANNEL_ANALOG)	&& (ch->enabled)) {
			devc->enabled_analog_channels =
			    g_slist_append(devc->enabled_analog_channels, ch);
		}
	}
#endif

    devc->acquisition_depth = lnss_acquisition_depth_set(sdi, devc->limit_samples);

    if((sr_error = lnss_subsamplerate_set(sdi, devc->samplerate)) != SR_OK) return sr_error;

    sr_error = lnss_triggers_set(sdi, trg_falling_mask, trg_rising_mask, trg_low_mask, trg_high_mask);

    if(sr_error){
        return sr_error;
    }

	sr_session_source_add(sdi->session, -1, 0, POLL_INTERVAL_MS,
                            lnss_data_receive,
                            (void*)sdi);

    if((sr_error = lnss_aquisition_start(sdi)) != SR_OK) return sr_error;

    std_session_send_df_header(sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	sr_session_source_remove(sdi->session, -1);

	std_session_send_df_end(sdi);

	return SR_OK;
}

static int init (struct sr_dev_driver *di, struct sr_context *sr_ctx)
{
    return std_init(di, sr_ctx);;
}

static int cleanup (const struct sr_dev_driver *di)
{
    return std_cleanup(di);;
}

static struct sr_dev_driver labnation_smartscope_driver_info = {
	.name = "labnation-smartscope",
	.longname = "LabNation SmartScope",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = std_dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};

SR_REGISTER_DEV_DRIVER(labnation_smartscope_driver_info);
