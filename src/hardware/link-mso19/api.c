/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
 * Copyright (C) 2012 Renato Caldas <rmsc@fe.up.pt>
 * Copyright (C) 2013 Lior Elazary <lelazary@yahoo.com>
 * Copyright (C) 2022 Paul Kasemir <paul.kasemir@gmail.com>
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
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_LIMIT_SAMPLES | SR_CONF_GET | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SOURCE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_MATCH | SR_CONF_LIST,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_SET,
	SR_CONF_CAPTURE_RATIO | SR_CONF_SET,
};

static const uint32_t devopts_cg_analog[] = {
	SR_CONF_COUPLING | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const uint32_t devopts_cg_digital[] = {
	SR_CONF_TRIGGER_SLOPE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
};

static const char *coupling[] = {
	"DC", "AC",
};

static const uint64_t samplerates[] = {
	SR_HZ(100),
	SR_MHZ(200),
	SR_HZ(100),
};

static const char *trigger_sources[] = {
	"DSO", "LA", // "SPI", "I2C",
};

enum {
	TRIGGER_SOURCE_DSO = 0,
	TRIGGER_SOURCE_LA,
	// TRIGGER_SOURCE_SPI,
	// TRIGGER_SOURCE_I2C,
};

static const char *dso_trigger_slopes[] = {
	"Rising", "Falling",
};

static const char *la_trigger_slopes[] = {
	"F->T", "T->F",
};

enum {
	TRIGGER_SLOPE_RISING = 0,
	TRIGGER_SLOPE_FALLING,
	TRIGGER_SLOPE_F_T = 0,
	TRIGGER_SLOPE_T_F,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
};

static void mso_update_trigger_slope(struct dev_context *devc)
{
	switch (devc->trigger_source) {
	case TRIGGER_SOURCE_DSO:
		switch (devc->dso_trigger_slope) {
		case TRIGGER_SLOPE_RISING:
			TRIG_UPDATE_EDGE(devc->ctltrig, TRIG_EDGE_RISING);
			break;
		case TRIGGER_SLOPE_FALLING:
			TRIG_UPDATE_EDGE(devc->ctltrig, TRIG_EDGE_FALLING);
			break;
		}
		break;
	case TRIGGER_SOURCE_LA:
		switch (devc->la_trigger_slope) {
		case TRIGGER_SLOPE_F_T:
			TRIG_UPDATE_EDGE(devc->ctltrig, TRIG_EDGE_F_T);
			break;
		case TRIGGER_SLOPE_T_F:
			TRIG_UPDATE_EDGE(devc->ctltrig, TRIG_EDGE_T_F);
			break;
		}
		break;
	}
}

static GSList* scan_handle_port(GSList *devices, struct sp_port *port)
{
	int usb_vid, usb_pid;
	char *port_name;
	char *vendor, *product, *serial_num;
	struct dev_context *devc;
	struct sr_dev_inst *sdi;
	unsigned int i;
	char hwrev[32];
	struct sr_channel_group *cg;

	if (sp_get_port_transport(port) != SP_TRANSPORT_USB) {
		return devices;
	}
	if (sp_get_port_usb_vid_pid(port, &usb_vid, &usb_pid) != SP_OK) {
		return devices;
	}
	if (USB_VENDOR != usb_vid || USB_PRODUCT != usb_pid) {
		return devices;
	}

	//Create the device context and set its params
	devc = g_malloc0(sizeof(*devc));

	port_name = sp_get_port_name(port);
	vendor = sp_get_port_usb_manufacturer(port);
	product = sp_get_port_usb_product(port);

	// MSO-19 stores device specific parameters in the usb serial number
	// We depend on libserialport to collect the serial number.
	serial_num = sp_get_port_usb_serial(port);
	if (mso_parse_serial(serial_num, product, devc) != SR_OK) {
		sr_err("Invalid serial: %s.", serial_num);
		g_free(devc);
		return devices;
	}
	sprintf(hwrev, "r%d", devc->hwrev);
	devc->ctlbase1 = BIT_CTL1_ADC_ENABLE;
	TRIG_UPDATE_OUT(devc->ctltrig, TRIG_OUT_TRIGGER);
	TRIG_UPDATE_SRC(devc->ctltrig, TRIG_SRC_DSO);
	mso_update_trigger_slope(devc);
	devc->coupling = coupling[0];
	devc->cur_rate = SR_KHZ(10);
	devc->dso_probe_attn = 10;
	devc->limit_samples = MSO_NUM_SAMPLES;

	devc->protocol_trigger.spimode = 0;
	for (i = 0; i < ARRAY_SIZE(devc->protocol_trigger.word); i++) {
		devc->protocol_trigger.word[i] = 0;
		devc->protocol_trigger.mask[i] = 0xff;
	}

	devc->serial = sr_serial_dev_inst_new(port_name, SERIALCOMM);

	sdi = g_malloc0(sizeof(*sdi));
	sdi->status = SR_ST_INACTIVE;
	sdi->vendor = g_strdup(vendor);
	sdi->model = g_strdup(product);
	sdi->version = g_strdup(hwrev);
	sdi->serial_num = g_strdup(serial_num);
	sdi->inst_type = SR_INST_SERIAL;
	sdi->conn = devc->serial;
	sdi->priv = devc;

	cg = g_malloc0(sizeof(struct sr_channel_group));
	cg->name = g_strdup("DSO");
	cg->channels = g_slist_append(cg->channels,
			sr_channel_new(sdi, i, SR_CHANNEL_ANALOG, TRUE, "DSO"));
	sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);

	cg = g_malloc0(sizeof(struct sr_channel_group));
	cg->name = g_strdup("LA");
	for (i = 0; i < MSO_NUM_LOGIC_CHANNELS; i++) {
		char channel_name[2] = {'0' + i, '\0'};
		cg->channels = g_slist_append(cg->channels,
				sr_channel_new(sdi, i, SR_CHANNEL_LOGIC, TRUE, channel_name));
	}
	sdi->channel_groups = g_slist_append(sdi->channel_groups, cg);

	devices = g_slist_append(devices, sdi);

	return devices;
}

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	GSList *devices = NULL;
	const char *conn = NULL;
	GSList *l;
	struct sr_config *src;

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = g_variant_get_string(src->data, NULL);
			break;
		}
	}

	if (conn) {
		struct sp_port *port;
		if (sp_get_port_by_name(conn, &port) == SP_OK) {
			devices = scan_handle_port(devices, port);
			sp_free_port(port);
		}
	} else {
		struct sp_port **port_list;
		struct sp_port **port_p;
		if (sp_list_ports(&port_list) == SP_OK) {
			for (port_p = port_list; *port_p; port_p++) {
				devices = scan_handle_port(devices, *port_p);
			}
			sp_free_port_list(port_list);
		}
	}

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;

	devc = sdi->priv;

	if (std_serial_dev_open(sdi) != SR_OK)
		return SR_ERR;

	serial_flush(devc->serial);

	ret = mso_reset_adc(sdi);
	if (ret != SR_OK)
		goto error_close;

	serial_flush(devc->serial);

	ret = mso_read_status(devc->serial, &devc->status);
	if (ret != SR_OK) {
		goto error_close;
	}

	ret = mso_reset_fsm(sdi);
	if (ret != SR_OK) {
		goto error_close;
	}

	return SR_OK;
error_close:
	serial_close(devc->serial);
	return ret;
}

static int config_get(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_rate);
		break;
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(devc->limit_samples);
		break;
	case SR_CONF_COUPLING:
		if (!cg_is_analog(cg))
			return SR_ERR_NA;
		*data = g_variant_new_string(devc->coupling);
		break;
	case SR_CONF_TRIGGER_SOURCE:
		*data = g_variant_new_string(trigger_sources[devc->trigger_source]);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if (cg_is_analog(cg))
			*data = g_variant_new_string(dso_trigger_slopes[devc->dso_trigger_slope]);
		else if (cg_is_digital(cg))
			*data = g_variant_new_string(la_trigger_slopes[devc->la_trigger_slope]);
		else
			return SR_ERR_NA;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(uint32_t key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t num_samples;
	int trigger_pos;
	double pos;
	int idx;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		// FIXME
		return mso_configure_rate(sdi, g_variant_get_uint64(data));
	case SR_CONF_LIMIT_SAMPLES:
		num_samples = g_variant_get_uint64(data);
		if (num_samples != MSO_NUM_SAMPLES) {
			sr_err("Only %d samples are supported.", MSO_NUM_SAMPLES);
			return SR_ERR_ARG;
		}
		devc->limit_samples = num_samples;
		break;
	case SR_CONF_CAPTURE_RATIO:
		break;
	case SR_CONF_TRIGGER_SOURCE:
		idx = std_str_idx(data, ARRAY_AND_SIZE(trigger_sources));
		if (idx < 0)
			return SR_ERR_ARG;
		devc->trigger_source = idx;
		switch (idx) {
		case TRIGGER_SOURCE_DSO:
			TRIG_UPDATE_SRC(devc->ctltrig, TRIG_SRC_DSO);
			break;
		case TRIGGER_SOURCE_LA:
			TRIG_UPDATE_SRC(devc->ctltrig, TRIG_SRC_LA);
			break;
		}
		mso_update_trigger_slope(devc);
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if (cg_is_analog(cg)) {
			idx = std_str_idx(data, ARRAY_AND_SIZE(dso_trigger_slopes));
			if (idx < 0)
				return SR_ERR_ARG;
			devc->dso_trigger_slope = idx;
		} else if (cg_is_digital(cg)) {
			idx = std_str_idx(data, ARRAY_AND_SIZE(la_trigger_slopes));
			if (idx < 0)
				return SR_ERR_ARG;
			devc->la_trigger_slope = idx;
		} else {
			return SR_ERR_NA;
		}
		mso_update_trigger_slope(devc);
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		pos = g_variant_get_double(data);
		if (pos < 0 || pos > 255) {
			sr_err("Trigger position (%f) should be between 0 and 255.", pos);
			return SR_ERR_ARG;
		}
		trigger_pos = (int)pos;
		devc->trigger_holdoff[0] = trigger_pos & 0xff;
		break;
	case SR_CONF_COUPLING:
		if (!cg_is_analog(cg))
			return SR_ERR_ARG;
		idx = std_str_idx(data, ARRAY_AND_SIZE(coupling));
		if (idx < 0)
			return SR_ERR_ARG;
		devc->coupling = coupling[idx];
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_SCAN_OPTIONS:
	case SR_CONF_DEVICE_OPTIONS:
		if (!cg)
			return STD_CONFIG_LIST(key, data, sdi, cg, scanopts, drvopts, devopts);
		else if (cg_is_analog(cg))
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_analog));
		else if (cg_is_digital(cg))
			*data = std_gvar_array_u32(ARRAY_AND_SIZE(devopts_cg_digital));
		else
			return SR_ERR_NA;
		break;
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates));
		break;
	case SR_CONF_COUPLING:
		if (!cg_is_analog(cg))
			return SR_ERR_NA;
		*data = g_variant_new_strv(ARRAY_AND_SIZE(coupling));
		break;
	case SR_CONF_TRIGGER_SOURCE:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(trigger_sources));
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if (cg_is_analog(cg))
			*data = g_variant_new_strv(ARRAY_AND_SIZE(dso_trigger_slopes));
		else if (cg_is_digital(cg))
			*data = g_variant_new_strv(ARRAY_AND_SIZE(la_trigger_slopes));
		else
			return SR_ERR_NA;
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = std_gvar_array_i32(ARRAY_AND_SIZE(trigger_matches));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret = SR_ERR;

	devc = sdi->priv;

	if (mso_configure_channels(sdi) != SR_OK) {
		sr_err("Failed to configure channels.");
		return SR_ERR;
	}

	ret = mso_reset_fsm(sdi);
	if (ret != SR_OK) {
		return ret;
	}

	if (devc->coupling[0] == 'D') {
		devc->ctlbase1 |= BIT_CTL1_DC_COUPLING;
	} else {
		devc->ctlbase1 &= ~BIT_CTL1_DC_COUPLING;
	}

	ret = mso_configure_rate(sdi, devc->cur_rate);
	if (ret != SR_OK)
		return ret;

	/* set dac offset */
	ret = mso_dac_out(sdi, devc->dac_offset);
	if (ret != SR_OK)
		return ret;

	ret = mso_configure_threshold_level(sdi);
	if (ret != SR_OK)
		return ret;

	ret = mso_configure_trigger(sdi);
	if (ret != SR_OK)
		return ret;

	/* END of config hardware part */
	ret = mso_arm(sdi);
	if (ret != SR_OK)
		return ret;

	/* Start acquisition on the device. */
	ret = mso_read_status(devc->serial, &devc->status);
	if (ret != SR_OK)
		return ret;
	ret = mso_read_status(devc->serial, NULL);
	if (ret != SR_OK)
		return ret;

	std_session_send_df_header(sdi);

	/* Our first channel is analog, the other 8 are of type 'logic'. */
	/* TODO. */

	serial_source_add(sdi->session, devc->serial, G_IO_IN, -1,
			mso_receive_data, (void *)sdi);

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi)
{
	stop_acquisition(sdi);

	return SR_OK;
}

static struct sr_dev_driver link_mso19_driver_info = {
	.name = "link-mso19",
	.longname = "Link Instruments MSO-19",
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
	.dev_close = std_serial_dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.context = NULL,
};
SR_REGISTER_DEV_DRIVER(link_mso19_driver_info);
