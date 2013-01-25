/*
 * This file is part of the sigrok project.
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

#include "protocol.h"

static const int hwcaps[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_SAMPLERATE,
	SR_CONF_TRIGGER_SLOPE,
	SR_CONF_HORIZ_TRIGGERPOS,
//      SR_CONF_CAPTURE_RATIO,
	SR_CONF_LIMIT_SAMPLES,
//      SR_CONF_RLE,
	0,
};

/*
 * Probes are numbered 0 to 7.
 *
 * See also: http://www.linkinstruments.com/images/mso19_1113.gif
 */
SR_PRIV const char *mso19_probe_names[NUM_PROBES + 1] = {
	"0", "1", "2", "3", "4", "5", "6", "7", NULL
};

/*supported samplerates */
static const struct sr_samplerates samplerates = {
	SR_HZ(100),
	SR_MHZ(200),
	SR_HZ(100),
	NULL,
};

SR_PRIV struct sr_dev_driver link_mso19_driver_info;
static struct sr_dev_driver *di = &link_mso19_driver_info;

static int hw_init(struct sr_context *sr_ctx)
{
	struct drv_context *drvc;

	if (!(drvc = g_try_malloc0(sizeof(struct drv_context)))) {
		sr_err("Driver context malloc failed.");
		return SR_ERR_MALLOC;
	}

	drvc->sr_ctx = sr_ctx;
	di->priv = drvc;

	return SR_OK;
}

static GSList *hw_scan(GSList *options)
{
	int i;
	GSList *devices = NULL;
	const char *conn = NULL;
	const char *serialcomm = NULL;
	GSList *l;
	struct sr_config *src;
	struct udev *udev;

	(void)options;

	for (l = options; l; l = l->next) {
		src = l->data;
		switch (src->key) {
		case SR_CONF_CONN:
			conn = src->value;
			break;
		case SR_CONF_SERIALCOMM:
			serialcomm = src->value;
			break;
		}
	}
	if (!conn)
		conn = SERIALCONN;
	if (serialcomm == NULL)
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
		if (!(devc = g_try_malloc0(sizeof(struct dev_context)))) {
			sr_err("Device context malloc failed.");
			return devices;
		}

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

		if (!(devc->serial = sr_serial_dev_inst_new(conn, serialcomm))) {
			g_free(devc);
			return devices;
		}

		struct sr_dev_inst *sdi = sr_dev_inst_new(0, SR_ST_INACTIVE,
						manufacturer, product, hwrev);

		if (!sdi) {
			sr_err("Unable to create device instance for %s",
			       sysname);
			sr_dev_inst_free(sdi);
			g_free(devc);
			return devices;
		}

		sdi->driver = di;
		sdi->priv = devc;

		for (i = 0; i < NUM_PROBES; i++) {
			struct sr_probe *probe;
			if (!(probe = sr_probe_new(i, SR_PROBE_LOGIC, TRUE,
						   mso19_probe_names[i])))
				return 0;
			sdi->probes = g_slist_append(sdi->probes, probe);
		}

		//Add the driver
		struct drv_context *drvc = di->priv;
		drvc->instances = g_slist_append(drvc->instances, sdi);
		devices = g_slist_append(devices, sdi);
	}

	return devices;
}

static GSList *hw_dev_list(void)
{
	struct drv_context *drvc;

	drvc = di->priv;

	return drvc->instances;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;

	devc = sdi->priv;

	if (serial_open(devc->serial, SERIAL_RDWR) != SR_OK)
		return SR_ERR;

	sdi->status = SR_ST_ACTIVE;

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

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	if (devc->serial && devc->serial->fd != -1) {
		serial_close(devc->serial);
		sdi->status = SR_ST_INACTIVE;
	}

	return SR_OK;
}

static int hw_cleanup(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	struct drv_context *drvc;
	struct dev_context *devc;
	int ret = SR_OK;

	if (!(drvc = di->priv))
		return SR_OK;

	/* Properly close and free all devices. */
	for (l = drvc->instances; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("%s: sdi was NULL, continuing", __func__);
			ret = SR_ERR_BUG;
			continue;
		}
		if (!(devc = sdi->priv)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("%s: sdi->priv was NULL, continuing", __func__);
			ret = SR_ERR_BUG;
			continue;
		}
		hw_dev_close(sdi);
		sr_serial_dev_inst_free(devc->serial);
		sr_dev_inst_free(sdi);
	}
	g_slist_free(drvc->instances);
	drvc->instances = NULL;

	return ret;
}

static int config_get(int id, const void **data, const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	switch (id) {
	case SR_DI_HWCAPS:
		*data = hwcaps;
		break;
	case SR_DI_SAMPLERATES:
		*data = &samplerates;
		break;
	case SR_DI_TRIGGER_TYPES:
		*data = (char *)TRIGGER_TYPES;
		break;
	case SR_DI_CUR_SAMPLERATE:
		if (sdi) {
			devc = sdi->priv;
			*data = &devc->cur_rate;
		} else
			return SR_ERR;
		break;
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int config_set(int id, const void *value, const struct sr_dev_inst *sdi)
{
	int ret;
	struct dev_context *devc;
	uint64_t num_samples, slope;
	int trigger_pos;
	float pos;

	devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	switch (id) {
	case SR_CONF_SAMPLERATE:
		// FIXME
		return mso_configure_rate(sdi, *(const uint64_t *)value);
		ret = SR_OK;
		break;
	case SR_CONF_LIMIT_SAMPLES:
		num_samples = *(uint64_t *)value;
		if (num_samples < 1024) {
			sr_err("minimum of 1024 samples required");
			ret = SR_ERR_ARG;
		} else {
			devc->limit_samples = num_samples;
			sr_dbg("setting limit_samples to %i\n",
			       num_samples);
			ret = SR_OK;
		}
		break;
	case SR_CONF_CAPTURE_RATIO:
		ret = SR_OK;
		break;
	case SR_CONF_TRIGGER_SLOPE:
		slope = *(uint64_t *)value;
		if (slope != SLOPE_NEGATIVE && slope != SLOPE_POSITIVE) {
			sr_err("Invalid trigger slope");
			ret = SR_ERR_ARG;
		} else {
			devc->trigger_slope = slope;
			ret = SR_OK;
		}
		break;
	case SR_CONF_HORIZ_TRIGGERPOS:
		pos = *(float *)value;
		if (pos < 0 || pos > 255) {
			sr_err("Trigger position (%f) should be between 0 and 255.", pos);
			ret = SR_ERR_ARG;
		} else {
			trigger_pos = (int)pos;
			devc->trigger_holdoff[0] = trigger_pos & 0xff;
			ret = SR_OK;
		}
		break;
	case SR_CONF_RLE:
		ret = SR_OK;
		break;
	default:
		ret = SR_ERR;
		break;
	}

	return ret;
}

static int config_list(int key, const void **data, const struct sr_dev_inst *sdi)
{

	(void)sdi;

	switch (key) {
	case SR_CONF_SAMPLERATE:
		*data = &samplerates;
		break;
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
				    void *cb_data)
{
	struct sr_datafeed_packet *packet;
	struct sr_datafeed_header *header;
	struct dev_context *devc;
	int ret = SR_ERR;

	devc = sdi->priv;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR;

	if (mso_configure_probes(sdi) != SR_OK) {
		sr_err("Failed to configure probes.");
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

	sr_source_add(devc->serial->fd, G_IO_IN, -1, mso_receive_data, cb_data);

	if (!(packet = g_try_malloc(sizeof(struct sr_datafeed_packet)))) {
		sr_err("Datafeed packet malloc failed.");
		return SR_ERR_MALLOC;
	}

	if (!(header = g_try_malloc(sizeof(struct sr_datafeed_header)))) {
		sr_err("Datafeed header malloc failed.");
		g_free(packet);
		return SR_ERR_MALLOC;
	}

	packet->type = SR_DF_HEADER;
	packet->payload = (unsigned char *)header;
	header->feed_version = 1;
	gettimeofday(&header->starttime, NULL);
	sr_session_send(cb_data, packet);

	g_free(header);
	g_free(packet);

	return SR_OK;
}

/* This stops acquisition on ALL devices, ignoring dev_index. */
static int hw_dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	(void)cb_data;

	stop_acquisition(sdi);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver link_mso19_driver_info = {
	.name = "link-mso19",
	.longname = "Link Instruments MSO-19",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_list = hw_dev_list,
	.dev_clear = hw_cleanup,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.priv = NULL,
};
