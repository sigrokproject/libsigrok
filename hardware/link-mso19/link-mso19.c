/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Daniel Ribeiro <drwyrm@gmail.com>
 * Copyright (C) 2012 Renato Caldas <rmsc@fe.up.pt>
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
#include <arpa/inet.h>
#include "sigrok.h"
#include "sigrok-internal.h"
#include "link-mso19.h"

#define USB_VENDOR "3195"
#define USB_PRODUCT "f190"

#define NUM_PROBES 8

static int capabilities[] = {
	SR_HWCAP_LOGIC_ANALYZER,
//	SR_HWCAP_OSCILLOSCOPE,
//	SR_HWCAP_PAT_GENERATOR,

	SR_HWCAP_SAMPLERATE,
//	SR_HWCAP_CAPTURE_RATIO,
	SR_HWCAP_LIMIT_SAMPLES,
	0,
};

static const char *probe_names[NUM_PROBES + 1] = {
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	NULL,
};

static uint64_t supported_samplerates[] = {
	SR_HZ(100),
	SR_HZ(200),
	SR_HZ(500),
	SR_KHZ(1),
	SR_KHZ(2),
	SR_KHZ(5),
	SR_KHZ(10),
	SR_KHZ(20),
	SR_KHZ(50),
	SR_KHZ(100),
	SR_KHZ(200),
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(2),
	SR_MHZ(5),
	SR_MHZ(10),
	SR_MHZ(20),
	SR_MHZ(50),
	SR_MHZ(100),
	SR_MHZ(200),
	0,
};

static struct sr_samplerates samplerates = {
	SR_HZ(100),
	SR_MHZ(200),
	SR_HZ(0),
	supported_samplerates,
};

static GSList *device_instances = NULL;

static int mso_send_control_message(struct sr_device_instance *sdi,
		uint16_t payload[], int n)
{
	int fd = sdi->serial->fd;
	int i, w, ret, s = n * 2 + sizeof(mso_head) + sizeof(mso_foot);
	char *p, *buf;

	ret = SR_ERR;

	if (fd < 0)
		goto ret;

	if (!(buf = g_try_malloc(s))) {
		sr_err("mso19: %s: buf malloc failed", __func__);
		ret = SR_ERR_MALLOC;
		goto ret;
	}

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
	g_free(buf);
ret:
	return ret;
}

static int mso_reset_adc(struct sr_device_instance *sdi)
{
	struct mso *mso = sdi->priv;
	uint16_t ops[2];

	ops[0] = mso_trans(REG_CTL1, (mso->ctlbase1 | BIT_CTL1_RESETADC));
	ops[1] = mso_trans(REG_CTL1, mso->ctlbase1);
	mso->ctlbase1 |= BIT_CTL1_ADC_UNKNOWN4;

	sr_dbg("Requesting ADC reset");
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_reset_fsm(struct sr_device_instance *sdi)
{
	struct mso *mso = sdi->priv;
	uint16_t ops[1];

	mso->ctlbase1 |= BIT_CTL1_RESETFSM;
	ops[0] = mso_trans(REG_CTL1, mso->ctlbase1);

	sr_dbg("Requesting ADC reset");
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_toggle_led(struct sr_device_instance *sdi, int state)
{
	struct mso *mso = sdi->priv;
	uint16_t ops[1];

	mso->ctlbase1 &= ~BIT_CTL1_LED;
	if (state)
		mso->ctlbase1 |= BIT_CTL1_LED;
	ops[0] = mso_trans(REG_CTL1, mso->ctlbase1);

	sr_dbg("Requesting LED toggle");
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_check_trigger(struct sr_device_instance *sdi,
		uint8_t *info)
{
	uint16_t ops[] = { mso_trans(REG_TRIGGER, 0) };
	char buf[1];
	int ret;

	sr_dbg("Requesting trigger state");
	ret = mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
	if (info == NULL || ret != SR_OK)
		return ret;

	buf[0] = 0;
	if (serial_read(sdi->serial->fd, buf, 1) != 1) /* FIXME: Need timeout */
		ret = SR_ERR;
	*info = buf[0];

	sr_dbg("Trigger state is: 0x%x", *info);
	return ret;
}

static int mso_read_buffer(struct sr_device_instance *sdi)
{
	uint16_t ops[] = { mso_trans(REG_BUFFER, 0) };

	sr_dbg("Requesting buffer dump");
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_arm(struct sr_device_instance *sdi)
{
	struct mso *mso = sdi->priv;
	uint16_t ops[] = {
		mso_trans(REG_CTL1, mso->ctlbase1 | BIT_CTL1_RESETFSM),
		mso_trans(REG_CTL1, mso->ctlbase1 | BIT_CTL1_ARM),
		mso_trans(REG_CTL1, mso->ctlbase1),
	};

	sr_dbg("Requesting trigger arm");
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_force_capture(struct sr_device_instance *sdi)
{
	struct mso *mso = sdi->priv;
	uint16_t ops[] = {
		mso_trans(REG_CTL1, mso->ctlbase1 | 8),
		mso_trans(REG_CTL1, mso->ctlbase1),
	};

	sr_dbg("Requesting forced capture");
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_dac_out(struct sr_device_instance *sdi, uint16_t val)
{
	struct mso *mso = sdi->priv;
	uint16_t ops[] = {
		mso_trans(REG_DAC1, (val >> 8) & 0xff),
		mso_trans(REG_DAC2, val & 0xff),
		mso_trans(REG_CTL1, mso->ctlbase1 | BIT_CTL1_RESETADC),
	};

	sr_dbg("Setting dac word to 0x%x", val);
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_clkrate_out(struct sr_device_instance *sdi, uint16_t val)
{
	uint16_t ops[] = {
		mso_trans(REG_CLKRATE1, (val >> 8) & 0xff),
		mso_trans(REG_CLKRATE2, val & 0xff),
	};

	sr_dbg("Setting clkrate word to 0x%x", val);
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
			mso->ctlbase2 = rate_map[i].slowmode;
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
			mso->dso_trigger_width / SR_HZ_TO_NS(mso->cur_rate));

	/* Select the SPI/I2C trigger config bank */
	ops[5] = mso_trans(REG_CTL2, (mso->ctlbase2 | BITS_CTL2_BANK(2)));
	/* Configure the SPI/I2C protocol trigger */
	ops[6] = mso_trans(REG_PT_WORD(0), mso->protocol_trigger.word[0]);
	ops[7] = mso_trans(REG_PT_WORD(1), mso->protocol_trigger.word[1]);
	ops[8] = mso_trans(REG_PT_WORD(2), mso->protocol_trigger.word[2]);
	ops[9] = mso_trans(REG_PT_WORD(3), mso->protocol_trigger.word[3]);
	ops[10] = mso_trans(REG_PT_MASK(0), mso->protocol_trigger.mask[0]);
	ops[11] = mso_trans(REG_PT_MASK(1), mso->protocol_trigger.mask[1]);
	ops[12] = mso_trans(REG_PT_MASK(2), mso->protocol_trigger.mask[2]);
	ops[13] = mso_trans(REG_PT_MASK(3), mso->protocol_trigger.mask[3]);
	ops[14] = mso_trans(REG_PT_SPIMODE, mso->protocol_trigger.spimode);
	/* Select the default config bank */
	ops[15] = mso_trans(REG_CTL2, mso->ctlbase2);

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

static int hw_init(const char *deviceinfo)
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
		sr_warn("Failed to initialize udev.");
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
			sr_warn("Unable to find parent usb device for %s",
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
			sr_warn("Could not parse iProduct: %s", iProduct);
			continue;
		}
		strncpy(product, iProduct, s);
		product[s] = 0;
		strcpy(manufacturer, iProduct + s);

		if (!(mso = g_try_malloc0(sizeof(struct mso)))) {
			sr_err("mso19: %s: mso malloc failed", __func__);
			continue; /* TODO: Errors handled correctly? */
		}

		if (mso_parse_serial(iSerial, iProduct, mso) != SR_OK) {
			sr_warn("Invalid iSerial: %s", iSerial);
			goto err_free_mso;
		}
		sprintf(hwrev, "r%d", mso->hwrev);

		/* hardware initial state */
		mso->ctlbase1 = 0;
		{
			/* Initialize the protocol trigger configuration */
			int i;
			for (i = 0; i < 4; i++)
			{
				mso->protocol_trigger.word[i] = 0;
				mso->protocol_trigger.mask[i] = 0xff;
			}
			mso->protocol_trigger.spimode = 0;
		}

		sdi = sr_device_instance_new(devcnt, SR_ST_INITIALIZING,
			manufacturer, product, hwrev);
		if (!sdi) {
			sr_warn("Unable to create device instance for %s",
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
		{
			free(sdi->priv);
			sdi->priv = NULL;
		}
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
	sr_dbg("trigger state: 0x%x", mso->trigger_state);

	ret = mso_reset_adc(sdi);
	if (ret != SR_OK)
		return ret;

	mso_check_trigger(sdi, &mso->trigger_state);
	sr_dbg("trigger state: 0x%x", mso->trigger_state);

//	ret = mso_reset_fsm(sdi);
//	if (ret != SR_OK)
//		return ret;

	sr_dbg("Finished %s", __func__);

//	return SR_ERR;
	return SR_OK;
}

static int hw_closedev(int device_index)
{
	struct sr_device_instance *sdi;

	if (!(sdi = sr_get_device_instance(device_instances, device_index))) {
		sr_err("mso19: %s: sdi was NULL", __func__);
		return SR_ERR; /* TODO: SR_ERR_ARG? */
	}

	/* TODO */
	if (sdi->serial->fd != -1) {
		serial_close(sdi->serial->fd);
		sdi->serial->fd = -1;
		sdi->status = SR_ST_INACTIVE;
	}

	sr_dbg("finished %s", __func__);
	return SR_OK;
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
		info = GINT_TO_POINTER(NUM_PROBES);
		break;
	case SR_DI_PROBE_NAMES: 
		info = probe_names;
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
	struct sr_datafeed_logic logic;
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
	packet.payload = &logic;
	logic.length = 1024;
	logic.unitsize = 1;
	logic.data = logic_out;
	sr_session_bus(mso->session_id, &packet);

	// Dont bother fixing this yet, keep it "old style"
	/*
	packet.type = SR_DF_ANALOG;
	packet.length = 1024;
	packet.unitsize = sizeof(double);
	packet.payload = analog_out;
	sr_session_bus(mso->session_id, &packet);
	*/

	packet.type = SR_DF_END;
	sr_session_bus(mso->session_id, &packet);

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
	mso->ctlbase1 &= 0x7f;
//	mso->ctlbase1 |= mso->acdcmode;

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
	sr_source_add(sdi->serial->fd, G_IO_IN, -1, receive_data, sdi);

	packet.type = SR_DF_HEADER;
	packet.payload = (unsigned char *) &header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	header.samplerate = mso->cur_rate;
	// header.num_analog_probes = 1;
	header.num_logic_probes = 8;
	sr_session_bus(session_device_id, &packet);

	return ret;
}

/* FIXME */
static void hw_stop_acquisition(int device_index, gpointer session_device_id)
{
	struct sr_datafeed_packet packet;

	device_index = device_index;

	packet.type = SR_DF_END;
	sr_session_bus(session_device_id, &packet);
}

SR_PRIV struct sr_device_plugin link_mso19_plugin_info = {
	.name = "link-mso19",
	.longname = "Link Instruments MSO-19",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.opendev = hw_opendev,
	.closedev = hw_closedev,
	.get_device_info = hw_get_device_info,
	.get_status = hw_get_status,
	.get_capabilities = hw_get_capabilities,
	.set_configuration = hw_set_configuration,
	.start_acquisition = hw_start_acquisition,
	.stop_acquisition = hw_stop_acquisition,
};
