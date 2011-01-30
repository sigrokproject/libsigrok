/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <inttypes.h>
#include <glib.h>
#include <libudev.h>
#include <sigrok.h>
#include <arpa/inet.h>
#include <sigrok-internal.h>
#include "config.h"
#include "link-mso19.h"

#define USB_VENDOR "3195"
#define USB_PRODUCT "f190"

static int capabilities[] = {
	SR_HWCAP_LOGIC_ANALYZER,
//	SR_HWCAP_OSCILLOSCOPE,
//	SR_HWCAP_PAT_GENERATOR,

	SR_HWCAP_SAMPLERATE,
//	SR_HWCAP_CAPTURE_RATIO,
	SR_HWCAP_LIMIT_SAMPLES,
	0,
};

static uint64_t supported_samplerates[] = {
	100, 200, 500, KHZ(1), KHZ(2), KHZ(5), KHZ(10), KHZ(20),
	KHZ(50), KHZ(100), KHZ(200), KHZ(500), MHZ(1), MHZ(2), MHZ(5),
	MHZ(10), MHZ(20), MHZ(50), MHZ(100), MHZ(200), 0
};

static struct samplerates samplerates = {
	100, MHZ(200), 0, supported_samplerates,
};

static GSList *device_instances = NULL;

static int mso_send_control_message(struct sr_device_instance *sdi,
		uint16_t payload[], int n)
{
	int fd = sdi->serial->fd;
	int i, w, ret, s = n * 2 + sizeof(mso_head) + sizeof(mso_foot);
	char *p, *buf;

	if (fd < 0)
		goto ret;

	buf = malloc(s);
	if (!buf)
		goto ret;

	p = buf;
	memcpy(p, mso_head, sizeof(mso_head));
	p += sizeof(mso_head);

	for (i = 0; i < n; i++) {
		*(uint16_t *) p = htons(payload[i]);
		p += 2;
	}
	memcpy(p, mso_foot, sizeof(mso_foot));

	w = 0;
	while (w < s) {
		ret = serial_write(fd, buf + w, s - w);
		if (ret < 0) {
			ret = SR_ERR;
			goto free;
		}
		w += ret;
	}
	ret = SR_OK;
free:
	free(buf);
ret:
	return ret;
}

static int mso_reset_adc(struct sr_device_instance *sdi)
{
	struct mso *mso = sdi->priv;
	uint16_t ops[2];

	ops[0] = mso_trans(REG_CTL, (mso->ctlbase | BIT_CTL_RESETADC));
	ops[1] = mso_trans(REG_CTL, mso->ctlbase);
	mso->ctlbase |= BIT_CTL_ADC_UNKNOWN4;

	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_reset_fsm(struct sr_device_instance *sdi)
{
	struct mso *mso = sdi->priv;
	uint16_t ops[1];

	mso->ctlbase |= BIT_CTL_RESETFSM;
	ops[0] = mso_trans(REG_CTL, mso->ctlbase);

	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_toggle_led(struct sr_device_instance *sdi, int state)
{
	struct mso *mso = sdi->priv;
	uint16_t ops[1];

	mso->ctlbase &= BIT_CTL_LED;
	if (state)
		mso->ctlbase |= BIT_CTL_LED;
	ops[0] = mso_trans(REG_CTL, mso->ctlbase);

	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_check_trigger(struct sr_device_instance *sdi,
		uint8_t *info)
{
	uint16_t ops[] = { mso_trans(REG_TRIGGER, 0) };
	char buf[1];
	int ret;

	ret = mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
	if (info == NULL || ret != SR_OK)
		return ret;

	buf[0] = 0;
	if (serial_read(sdi->serial->fd, buf, 1) != 1) /* FIXME: Need timeout */
		ret = SR_ERR;
	*info = buf[0];

	return ret;
}

static int mso_read_buffer(struct sr_device_instance *sdi)
{
	uint16_t ops[] = { mso_trans(REG_BUFFER, 0) };

	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_arm(struct sr_device_instance *sdi)
{
	struct mso *mso = sdi->priv;
	uint16_t ops[] = {
		mso_trans(REG_CTL, mso->ctlbase | BIT_CTL_RESETFSM),
		mso_trans(REG_CTL, mso->ctlbase | BIT_CTL_ARM),
		mso_trans(REG_CTL, mso->ctlbase),
	};

	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_force_capture(struct sr_device_instance *sdi)
{
	struct mso *mso = sdi->priv;
	uint16_t ops[] = {
		mso_trans(REG_CTL, mso->ctlbase | 8),
		mso_trans(REG_CTL, mso->ctlbase),
	};

	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_dac_out(struct sr_device_instance *sdi, uint16_t val)
{
	struct mso *mso = sdi->priv;
	uint16_t ops[] = {
		mso_trans(REG_DAC1, (val >> 8) & 0xff),
		mso_trans(REG_DAC2, val & 0xff),
		mso_trans(REG_CTL, mso->ctlbase | BIT_CTL_RESETADC),
	};

	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_clkrate_out(struct sr_device_instance *sdi, uint16_t val)
{
	uint16_t ops[] = {
		mso_trans(REG_CLKRATE1, (val >> 8) & 0xff),
		mso_trans(REG_CLKRATE2, val & 0xff),
	};

	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_configure_rate(struct sr_device_instance *sdi,
		uint32_t rate)
{
	struct mso *mso = sdi->priv;
	unsigned int i;
	int ret = SR_ERR;

	for (i = 0; i < ARRAY_SIZE(rate_map); i++) {
		if (rate_map[i].rate == rate) {
			mso->slowmode = rate_map[i].slowmode;
			ret = mso_clkrate_out(sdi, rate_map[i].val);
			if (ret == SR_OK)
				mso->cur_rate = rate;
			return ret;
		}
	}
	return ret;
}


static inline uint16_t mso_calc_raw_from_mv(struct mso *mso)
{
	return (uint16_t) (0x200 -
			((mso->dso_trigger_voltage / mso->dso_probe_attn) /
			 mso->vbit));
}

static int mso_configure_trigger(struct sr_device_instance *sdi)
{
	struct mso *mso = sdi->priv;
	uint16_t ops[16];
	uint16_t dso_trigger = mso_calc_raw_from_mv(mso);

	dso_trigger &= 0x3ff;
	if ((!mso->trigger_slope && mso->trigger_chan == 1) ||
			(mso->trigger_slope &&
			 (mso->trigger_chan == 0 ||
			  mso->trigger_chan == 2 ||
			  mso->trigger_chan == 3)))
		dso_trigger |= 0x400;

	switch (mso->trigger_chan) {
	case 1:
		dso_trigger |= 0xe000;
	case 2:
		dso_trigger |= 0x4000;
		break;
	case 3:
		dso_trigger |= 0x2000;
		break;
	case 4:
		dso_trigger |= 0xa000;
		break;
	case 5:
		dso_trigger |= 0x8000;
		break;
	default:
	case 0:
		break;
	}

	switch (mso->trigger_outsrc) {
	case 1:
		dso_trigger |= 0x800;
		break;
	case 2:
		dso_trigger |= 0x1000;
		break;
	case 3:
		dso_trigger |= 0x1800;
		break;

	}

	ops[0] = mso_trans(5, mso->la_trigger);
	ops[1] = mso_trans(6, mso->la_trigger_mask);
	ops[2] = mso_trans(3, dso_trigger & 0xff);
	ops[3] = mso_trans(4, (dso_trigger >> 8) & 0xff);
	ops[4] = mso_trans(11,
			mso->dso_trigger_width / HZ_TO_NS(mso->cur_rate));
	ops[5] = mso_trans(15, (2 | mso->slowmode));

	/* FIXME SPI/I2C Triggers */
	ops[6] = mso_trans(0, 0);
	ops[7] = mso_trans(1, 0);
	ops[8] = mso_trans(2, 0);
	ops[9] = mso_trans(3, 0);
	ops[10] = mso_trans(4, 0xff);
	ops[11] = mso_trans(5, 0xff);
	ops[12] = mso_trans(6, 0xff);
	ops[13] = mso_trans(7, 0xff);
	ops[14] = mso_trans(8, mso->trigger_spimode);
	ops[15] = mso_trans(15, mso->slowmode);

	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_configure_threshold_level(struct sr_device_instance *sdi)
{
	struct mso *mso = sdi->priv;

	return mso_dac_out(sdi, la_threshold_map[mso->la_threshold]);
}

static int mso_parse_serial(const char *iSerial, const char *iProduct,
		struct mso *mso)
{
	unsigned int u1, u2, u3, u4, u5, u6;

	iProduct = iProduct;
	/* FIXME: This code is in the original app, but I think its
	 * used only for the GUI */
/*	if (strstr(iProduct, "REV_02") || strstr(iProduct, "REV_03"))
		mso->num_sample_rates = 0x16;
	else
		mso->num_sample_rates = 0x10; */

	/* parse iSerial */
	if (iSerial[0] != '4' || sscanf(iSerial, "%5u%3u%3u%1u%1u%6u",
				&u1, &u2, &u3, &u4, &u5, &u6) != 6)
		return SR_ERR;
	mso->hwmodel = u4;
	mso->hwrev = u5;
	mso->serial = u6;
	mso->vbit = u1 / 10000;
	if (mso->vbit == 0)
		mso->vbit = 4.19195;
	mso->dac_offset = u2;
	if (mso->dac_offset == 0)
		mso->dac_offset = 0x1ff;
	mso->offset_range = u3;
	if (mso->offset_range == 0)
		mso->offset_range = 0x17d;

	/*
	 * FIXME: There is more code on the original software to handle
	 * bigger iSerial strings, but as I can't test on my device
	 * I will not implement it yet
	 */

	return SR_OK;
}

static int hw_init(char *deviceinfo)
{
	struct sr_device_instance *sdi;
	int devcnt = 0;
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devices, *dev_list_entry;
	struct mso *mso;

	deviceinfo = deviceinfo;

	/* It's easier to map usb<->serial using udev */
	/*
	 * FIXME: On windows we can get the same information from the
	 * registry, add an #ifdef here later
	 */
	udev = udev_new();
	if (!udev) {
		g_warning("Failed to initialize udev.");
		goto ret;
	}
	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "usb-serial");
	udev_enumerate_scan_devices(enumerate);
	devices = udev_enumerate_get_list_entry(enumerate);
	udev_list_entry_foreach(dev_list_entry, devices) {
		const char *syspath, *sysname, *idVendor, *idProduct,
			*iSerial, *iProduct;
		char path[32], manufacturer[32], product[32], hwrev[32];
		struct udev_device *dev, *parent;
		size_t s;

		syspath = udev_list_entry_get_name(dev_list_entry);
		dev = udev_device_new_from_syspath(udev, syspath);
		sysname = udev_device_get_sysname(dev);
		parent = udev_device_get_parent_with_subsystem_devtype(
				dev, "usb", "usb_device");
		if (!parent) {
			g_warning("Unable to find parent usb device for %s",
					sysname);
			continue;
		}

		idVendor = udev_device_get_sysattr_value(parent, "idVendor");
		idProduct = udev_device_get_sysattr_value(parent, "idProduct");
		if (strcmp(USB_VENDOR, idVendor)
				|| strcmp(USB_PRODUCT, idProduct))
			continue;

		iSerial = udev_device_get_sysattr_value(parent, "serial");
		iProduct = udev_device_get_sysattr_value(parent, "product");

		snprintf(path, sizeof(path), "/dev/%s", sysname);

		s = strcspn(iProduct, " ");
		if (s > sizeof(product) ||
				strlen(iProduct) - s > sizeof(manufacturer)) {
			g_warning("Could not parse iProduct: %s", iProduct);
			continue;
		}
		strncpy(product, iProduct, s);
		product[s] = 0;
		strcpy(manufacturer, iProduct + s);
		sprintf(hwrev, "r%d", mso->hwrev);

		mso = malloc(sizeof(struct mso));
		if (!mso)
			continue;
		memset(mso, 0, sizeof(struct mso));

		if (mso_parse_serial(iSerial, iProduct, mso) != SR_OK) {
			g_warning("Invalid iSerial: %s", iSerial);
			goto err_free_mso;
		}
		/* hardware initial state */
		mso->ctlbase = 0;

		sdi = sr_device_instance_new(devcnt, SR_ST_INITIALIZING,
			manufacturer, product, hwrev);
		if (!sdi) {
			g_warning("Unable to create device instance for %s",
					sysname);
			goto err_free_mso;
		}

		/* save a pointer to our private instance data */
		sdi->priv = mso;

		sdi->serial = sr_serial_device_instance_new(path, -1);
		if (!sdi->serial)
			goto err_device_instance_free;

		device_instances = g_slist_append(device_instances, sdi);
		devcnt++;
		continue;

err_device_instance_free:
		sr_device_instance_free(sdi);
err_free_mso:
		free(mso);
	}

	udev_enumerate_unref(enumerate);
	udev_unref(udev);

ret:
	return devcnt;
}

static void hw_cleanup(void)
{
	GSList *l;
	struct sr_device_instance *sdi;

	/* Properly close all devices. */
	for (l = device_instances; l; l = l->next) {
		sdi = l->data;
		if (sdi->serial->fd != -1)
			serial_close(sdi->serial->fd);
		if (sdi->priv != NULL)
			free(sdi->priv);
		sr_device_instance_free(sdi);
	}
	g_slist_free(device_instances);
	device_instances = NULL;
}

static int hw_opendev(int device_index)
{
	struct sr_device_instance *sdi;
	struct mso *mso;
	int ret = SR_ERR;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return ret;

	mso = sdi->priv;
	sdi->serial->fd = serial_open(sdi->serial->port, O_RDWR);
	if (sdi->serial->fd == -1)
		return ret;

	ret = serial_set_params(sdi->serial->fd, 460800, 8, 0, 1, 2);
	if (ret != SR_OK)
		return ret;

	sdi->status = SR_ST_ACTIVE;

	/* FIXME: discard serial buffer */

	mso_check_trigger(sdi, &mso->trigger_state);
//	g_warning("trigger state: %c", mso->trigger_state);

	ret = mso_reset_adc(sdi);
	if (ret != SR_OK)
		return ret;

	mso_check_trigger(sdi, &mso->trigger_state);
//	g_warning("trigger state: %c", mso->trigger_state);

//	ret = mso_reset_fsm(sdi);
//	if (ret != SR_OK)
//		return ret;

//	return SR_ERR;
	return SR_OK;
}

static void hw_closedev(int device_index)
{
	struct sr_device_instance *sdi;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return;

	if (sdi->serial->fd != -1) {
		serial_close(sdi->serial->fd);
		sdi->serial->fd = -1;
		sdi->status = SR_ST_INACTIVE;
	}
}

static void *hw_get_device_info(int device_index, int device_info_id)
{
	struct sr_device_instance *sdi;
	struct mso *mso;
	void *info = NULL;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return NULL;
	mso = sdi->priv;

	switch (device_info_id) {
	case SR_DI_INSTANCE:
		info = sdi;
		break;
	case SR_DI_NUM_PROBES: /* FIXME: How to report analog probe? */
		info = GINT_TO_POINTER(8);
		break;
	case SR_DI_SAMPLERATES:
		info = &samplerates;
		break;
	case SR_DI_TRIGGER_TYPES:
		info = "01"; /* FIXME */
		break;
	case SR_DI_CUR_SAMPLERATE:
		info = &mso->cur_rate;
		break;
	}
	return info;
}

static int hw_get_status(int device_index)
{
	struct sr_device_instance *sdi;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ST_NOT_FOUND;

	return sdi->status;
}

static int *hw_get_capabilities(void)
{
	return capabilities;
}

static int hw_set_configuration(int device_index, int capability, void *value)
{
	struct sr_device_instance *sdi;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;

	switch (capability) {
	case SR_HWCAP_SAMPLERATE:
		return mso_configure_rate(sdi, *(uint64_t *) value);
	case SR_HWCAP_PROBECONFIG:
	case SR_HWCAP_LIMIT_SAMPLES:
	default:
		return SR_OK; /* FIXME */
	}

}

#define MSO_TRIGGER_UNKNOWN	'!'
#define MSO_TRIGGER_UNKNOWN1	'1'
#define MSO_TRIGGER_UNKNOWN2	'2'
#define MSO_TRIGGER_UNKNOWN3	'3'
#define MSO_TRIGGER_WAIT	'4'
#define MSO_TRIGGER_FIRED	'5'
#define MSO_TRIGGER_DATAREADY	'6'

/* FIXME: Pass errors? */
static int receive_data(int fd, int revents, void *user_data)
{
	struct sr_device_instance *sdi = user_data;
	struct mso *mso = sdi->priv;
	struct sr_datafeed_packet packet;
	uint8_t in[1024], logic_out[1024];
	double analog_out[1024];
	size_t i, s;

	revents = revents;

	s = serial_read(fd, in, sizeof(in));
	if (s <= 0)
		return FALSE;

	/* No samples */
	if (mso->trigger_state != MSO_TRIGGER_DATAREADY) {
		mso->trigger_state = in[0];
		if (mso->trigger_state == MSO_TRIGGER_DATAREADY) {
			mso_read_buffer(sdi);
			mso->buffer_n = 0;
		} else {
			mso_check_trigger(sdi, NULL);
		}
		return FALSE;
	}

	/* the hardware always dumps 1024 samples, 24bits each */
	if (mso->buffer_n < 3072) {
		memcpy(mso->buffer + mso->buffer_n, in, s);
		mso->buffer_n += s;
	}
	if (mso->buffer_n < 3072)
		return FALSE;

	/* do the conversion */
	for (i = 0; i < 1024; i++) {
		/* FIXME: Need to do conversion to mV */
		analog_out[i] = (mso->buffer[i * 3] & 0x3f) |
			((mso->buffer[i * 3 + 1] & 0xf) << 6);
		logic_out[i] = ((mso->buffer[i * 3 + 1] & 0x30) >> 4) |
			((mso->buffer[i * 3 + 2] & 0x3f) << 2);
	}

	packet.type = SR_DF_LOGIC;
	packet.length = 1024;
	packet.unitsize = 1;
	packet.payload = logic_out;
	session_bus(mso->session_id, &packet);


	packet.type = SR_DF_ANALOG;
	packet.length = 1024;
	packet.unitsize = sizeof(double);
	packet.payload = analog_out;
	session_bus(mso->session_id, &packet);

	packet.type = SR_DF_END;
	session_bus(mso->session_id, &packet);

	return TRUE;
}

static int hw_start_acquisition(int device_index, gpointer session_device_id)
{
	struct sr_device_instance *sdi;
	struct mso *mso;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	int ret = SR_ERR;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return ret;
	mso = sdi->priv;

	/* FIXME: No need to do full reconfigure every time */
//	ret = mso_reset_fsm(sdi);
//	if (ret != SR_OK)
//		return ret;

	/* FIXME: ACDC Mode */
	mso->ctlbase &= 0x7f;
//	mso->ctlbase |= mso->acdcmode;

	ret = mso_configure_rate(sdi, mso->cur_rate);
	if (ret != SR_OK)
		return ret;

	/* set dac offset */
	ret = mso_dac_out(sdi, mso->dac_offset);
	if (ret != SR_OK)
		return ret;

	ret = mso_configure_threshold_level(sdi);
	if (ret != SR_OK)
		return ret;

	ret = mso_configure_trigger(sdi);
	if (ret != SR_OK)
		return ret;

	/* FIXME: trigger_position */


	/* END of config hardware part */

	/* with trigger */
	ret = mso_arm(sdi);
	if (ret != SR_OK)
		return ret;

	/* without trigger */
//	ret = mso_force_capture(sdi);
//	if (ret != SR_OK)
//		return ret;

	mso_check_trigger(sdi, &mso->trigger_state);
	ret = mso_check_trigger(sdi, NULL);
	if (ret != SR_OK)
		return ret;

	mso->session_id = session_device_id;
	source_add(sdi->serial->fd, G_IO_IN, -1, receive_data, sdi);

	packet.type = SR_DF_HEADER;
	packet.length = sizeof(struct sr_datafeed_header);
	packet.payload = (unsigned char *) &header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	header.samplerate = mso->cur_rate;
	header.num_analog_probes = 1;
	header.num_logic_probes = 8;
	header.protocol_id = SR_PROTO_RAW;
	session_bus(session_device_id, &packet);

	return ret;
}

/* FIXME */
static void hw_stop_acquisition(int device_index, gpointer session_device_id)
{
	struct sr_datafeed_packet packet;

	device_index = device_index;

	packet.type = SR_DF_END;
	session_bus(session_device_id, &packet);
}

struct sr_device_plugin link_mso19_plugin_info = {
	.name = "link-mso19",
	.longname = "Link Instruments MSO-19",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.open = hw_opendev,
	.close = hw_closedev,
	.get_device_info = hw_get_device_info,
	.get_status = hw_get_status,
	.get_capabilities = hw_get_capabilities,
	.set_configuration = hw_set_configuration,
	.start_acquisition = hw_start_acquisition,
	.stop_acquisition = hw_stop_acquisition,
};
