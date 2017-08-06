/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
 * Copyright (C) 2012 Renato Caldas <rmsc@fe.up.pt>
 * Copyright (C) 2013 Lior Elazary <lelazary@yahoo.com>
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

static const uint32_t drvopts[] = {
	SR_CONF_OSCILLOSCOPE,
	SR_CONF_LOGIC_ANALYZER,
};

static const uint32_t devopts[] = {
	SR_CONF_LIMIT_SAMPLES | SR_CONF_SET,
	SR_CONF_SAMPLERATE | SR_CONF_GET | SR_CONF_SET | SR_CONF_LIST,
	SR_CONF_TRIGGER_TYPE | SR_CONF_LIST,
	SR_CONF_TRIGGER_SLOPE | SR_CONF_SET,
	SR_CONF_HORIZ_TRIGGERPOS | SR_CONF_SET,
	SR_CONF_CAPTURE_RATIO | SR_CONF_SET,
	SR_CONF_RLE | SR_CONF_SET,
};

/*
 * Channels are numbered 0 to 7.
 *
 * See also: http://www.linkinstruments.com/images/mso19_1113.gif
 */
static const char *channel_names[] = {
	/* Note: DSO needs to be first. */
	"DSO", "0", "1", "2", "3", "4", "5", "6", "7",
};

static const uint64_t samplerates[] = {
	SR_HZ(100),
	SR_MHZ(200),
	SR_HZ(100),
};

static const char *trigger_slopes[2] = {
	"r", "f",
};

static GSList *scan(struct sr_dev_driver *di, GSList *options)
{
	int i;
	GSList *devices = NULL;
	const char *conn = NULL;
	const char *serialcomm = NULL;
	GSList *l;
	struct sr_config *src;
	struct udev *udev;
	int chtype;

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
		conn = SERIALCONN;
	if (!serialcomm)
		serialcomm = SERIALCOMM;

	udev = udev_new();
	if (!udev) {
		sr_err("Failed to initialize udev.");
	}

	struct udev_enumerate *enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "usb-serial");
	udev_enumerate_scan_devices(enumerate);
	struct udev_list_entry *devs = udev_enumerate_get_list_entry(enumerate);
	struct udev_list_entry *dev_list_entry;
	for (dev_list_entry = devs;
	     dev_list_entry != NULL;
	     dev_list_entry = udev_list_entry_get_next(dev_list_entry)) {
		const char *syspath = udev_list_entry_get_name(dev_list_entry);
		struct udev_device *dev =
		    udev_device_new_from_syspath(udev, syspath);
		const char *sysname = udev_device_get_sysname(dev);
		struct udev_device *parent =
		    udev_device_get_parent_with_subsystem_devtype(dev, "usb",
								  "usb_device");

		if (!parent) {
			sr_err("Unable to find parent usb device for %s",
			       sysname);
			continue;
		}

		const char *idVendor =
		    udev_device_get_sysattr_value(parent, "idVendor");
		const char *idProduct =
		    udev_device_get_sysattr_value(parent, "idProduct");
		if (strcmp(USB_VENDOR, idVendor)
		    || strcmp(USB_PRODUCT, idProduct))
			continue;

		const char *iSerial =
		    udev_device_get_sysattr_value(parent, "serial");
		const char *iProduct =
		    udev_device_get_sysattr_value(parent, "product");

		char path[32];
		snprintf(path, sizeof(path), "/dev/%s", sysname);
		conn = path;

		size_t s = strcspn(iProduct, " ");
		char product[32];
		char manufacturer[32];
		if (s > sizeof(product) ||
		    strlen(iProduct) - s > sizeof(manufacturer)) {
			sr_err("Could not parse iProduct: %s.", iProduct);
			continue;
		}
		strncpy(product, iProduct, s);
		product[s] = 0;
		strcpy(manufacturer, iProduct + s + 1);

		//Create the device context and set its params
		struct dev_context *devc;
		devc = g_malloc0(sizeof(struct dev_context));

		if (mso_parse_serial(iSerial, iProduct, devc) != SR_OK) {
			sr_err("Invalid iSerial: %s.", iSerial);
			g_free(devc);
			return devices;
		}

		char hwrev[32];
		sprintf(hwrev, "r%d", devc->hwrev);
		devc->ctlbase1 = 0;
		devc->protocol_trigger.spimode = 0;
		for (i = 0; i < 4; i++) {
			devc->protocol_trigger.word[i] = 0;
			devc->protocol_trigger.mask[i] = 0xff;
		}

		devc->serial = sr_serial_dev_inst_new(conn, serialcomm);

		struct sr_dev_inst *sdi = g_malloc0(sizeof(struct sr_dev_inst));
		sdi->status = SR_ST_INACTIVE;
		sdi->vendor = g_strdup(manufacturer);
		sdi->model = g_strdup(product);
		sdi->version = g_strdup(hwrev);
		sdi->priv = devc;

		for (i = 0; i < ARRAY_SIZE(channel_names); i++) {
			chtype = (i == 0) ? SR_CHANNEL_ANALOG : SR_CHANNEL_LOGIC;
			sr_channel_new(sdi, i, chtype, TRUE, channel_names[i]);
		}
	}

	return std_scan_complete(di, devices);
}

static int dev_open(struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;

	devc = sdi->priv;

	if (serial_open(devc->serial, SERIAL_RDWR) != SR_OK)
		return SR_ERR;

	/* FIXME: discard serial buffer */
	mso_check_trigger(devc->serial, &devc->trigger_state);
	sr_dbg("Trigger state: 0x%x.", devc->trigger_state);

	ret = mso_reset_adc(sdi);
	if (ret != SR_OK)
		return ret;

	mso_check_trigger(devc->serial, &devc->trigger_state);
	sr_dbg("Trigger state: 0x%x.", devc->trigger_state);

	//    ret = mso_reset_fsm(sdi);
	//    if (ret != SR_OK)
	//            return ret;
	//    return SR_ERR;

	return SR_OK;
}

static int config_get(int key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR_ARG;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_rate);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int key, GVariant *data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t num_samples;
	const char *slope;
	int trigger_pos;
	double pos;

	(void)cg;

	devc = sdi->priv;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		// FIXME
		return mso_configure_rate(sdi, g_variant_get_uint64(data));
	case SR_CONF_LIMIT_SAMPLES:
		num_samples = g_variant_get_uint64(data);
		if (num_samples != 1024) {
			sr_err("Only 1024 samples are supported.");
			return SR_ERR_ARG;
		}
		devc->limit_samples = num_samples;
		break;
	case SR_CONF_CAPTURE_RATIO:
		break;
	case SR_CONF_TRIGGER_SLOPE:
		if ((idx = std_str_idx(data, ARRAY_AND_SIZE(trigger_slopes))) < 0)
			return SR_ERR_ARG;
		devc->trigger_slope = idx;
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
	case SR_CONF_RLE:
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_list(int key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{
	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		return STD_CONFIG_LIST(key, data, sdi, cg, NULL, drvopts, devopts);
	case SR_CONF_SAMPLERATE:
		*data = std_gvar_samplerates_steps(ARRAY_AND_SIZE(samplerates));
		break;
	case SR_CONF_TRIGGER_TYPE:
		*data = g_variant_new_string(TRIGGER_TYPE);
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

	/* FIXME: No need to do full reconfigure every time */
//      ret = mso_reset_fsm(sdi);
//      if (ret != SR_OK)
//              return ret;

	/* FIXME: ACDC Mode */
	devc->ctlbase1 &= 0x7f;
//      devc->ctlbase1 |= devc->acdcmode;

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
	mso_check_trigger(devc->serial, &devc->trigger_state);
	ret = mso_check_trigger(devc->serial, NULL);
	if (ret != SR_OK)
		return ret;

	/* Reset trigger state. */
	devc->trigger_state = 0x00;

	std_session_send_df_header(sdi);

	/* Our first channel is analog, the other 8 are of type 'logic'. */
	/* TODO. */

	serial_source_add(sdi->session, devc->serial, G_IO_IN, -1,
			mso_receive_data, sdi);

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
