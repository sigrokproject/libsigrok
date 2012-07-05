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
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "link-mso19.h"

#define USB_VENDOR "3195"
#define USB_PRODUCT "f190"

#define NUM_PROBES 8

static const int hwcaps[] = {
	SR_HWCAP_LOGIC_ANALYZER,
//	SR_HWCAP_OSCILLOSCOPE,
//	SR_HWCAP_PAT_GENERATOR,

	SR_HWCAP_SAMPLERATE,
//	SR_HWCAP_CAPTURE_RATIO,
	SR_HWCAP_LIMIT_SAMPLES,
	0,
};

/*
 * Probes are numbered 0 to 7.
 *
 * See also: http://www.linkinstruments.com/images/mso19_1113.gif
 */
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

static const uint64_t supported_samplerates[] = {
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

static const struct sr_samplerates samplerates = {
	0,
	0,
	0,
	supported_samplerates,
};

static GSList *dev_insts = NULL;

static int mso_send_control_message(struct sr_dev_inst *sdi,
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

static int mso_reset_adc(struct sr_dev_inst *sdi)
{
	struct context *ctx = sdi->priv;
	uint16_t ops[2];

	ops[0] = mso_trans(REG_CTL1, (ctx->ctlbase1 | BIT_CTL1_RESETADC));
	ops[1] = mso_trans(REG_CTL1, ctx->ctlbase1);
	ctx->ctlbase1 |= BIT_CTL1_ADC_UNKNOWN4;

	sr_dbg("mso19: Requesting ADC reset");
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_reset_fsm(struct sr_dev_inst *sdi)
{
	struct context *ctx = sdi->priv;
	uint16_t ops[1];

	ctx->ctlbase1 |= BIT_CTL1_RESETFSM;
	ops[0] = mso_trans(REG_CTL1, ctx->ctlbase1);

	sr_dbg("mso19: Requesting ADC reset");
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_toggle_led(struct sr_dev_inst *sdi, int state)
{
	struct context *ctx = sdi->priv;
	uint16_t ops[1];

	ctx->ctlbase1 &= ~BIT_CTL1_LED;
	if (state)
		ctx->ctlbase1 |= BIT_CTL1_LED;
	ops[0] = mso_trans(REG_CTL1, ctx->ctlbase1);

	sr_dbg("mso19: Requesting LED toggle");
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_check_trigger(struct sr_dev_inst *sdi, uint8_t *info)
{
	uint16_t ops[] = { mso_trans(REG_TRIGGER, 0) };
	char buf[1];
	int ret;

	sr_dbg("mso19: Requesting trigger state");
	ret = mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
	if (info == NULL || ret != SR_OK)
		return ret;

	buf[0] = 0;
	if (serial_read(sdi->serial->fd, buf, 1) != 1) /* FIXME: Need timeout */
		ret = SR_ERR;
	*info = buf[0];

	sr_dbg("mso19: Trigger state is: 0x%x", *info);
	return ret;
}

static int mso_read_buffer(struct sr_dev_inst *sdi)
{
	uint16_t ops[] = { mso_trans(REG_BUFFER, 0) };

	sr_dbg("mso19: Requesting buffer dump");
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_arm(struct sr_dev_inst *sdi)
{
	struct context *ctx = sdi->priv;
	uint16_t ops[] = {
		mso_trans(REG_CTL1, ctx->ctlbase1 | BIT_CTL1_RESETFSM),
		mso_trans(REG_CTL1, ctx->ctlbase1 | BIT_CTL1_ARM),
		mso_trans(REG_CTL1, ctx->ctlbase1),
	};

	sr_dbg("mso19: Requesting trigger arm");
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_force_capture(struct sr_dev_inst *sdi)
{
	struct context *ctx = sdi->priv;
	uint16_t ops[] = {
		mso_trans(REG_CTL1, ctx->ctlbase1 | 8),
		mso_trans(REG_CTL1, ctx->ctlbase1),
	};

	sr_dbg("mso19: Requesting forced capture");
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_dac_out(struct sr_dev_inst *sdi, uint16_t val)
{
	struct context *ctx = sdi->priv;
	uint16_t ops[] = {
		mso_trans(REG_DAC1, (val >> 8) & 0xff),
		mso_trans(REG_DAC2, val & 0xff),
		mso_trans(REG_CTL1, ctx->ctlbase1 | BIT_CTL1_RESETADC),
	};

	sr_dbg("mso19: Setting dac word to 0x%x", val);
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_clkrate_out(struct sr_dev_inst *sdi, uint16_t val)
{
	uint16_t ops[] = {
		mso_trans(REG_CLKRATE1, (val >> 8) & 0xff),
		mso_trans(REG_CLKRATE2, val & 0xff),
	};

	sr_dbg("mso19: Setting clkrate word to 0x%x", val);
	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_configure_rate(struct sr_dev_inst *sdi, uint32_t rate)
{
	struct context *ctx = sdi->priv;
	unsigned int i;
	int ret = SR_ERR;

	for (i = 0; i < ARRAY_SIZE(rate_map); i++) {
		if (rate_map[i].rate == rate) {
			ctx->ctlbase2 = rate_map[i].slowmode;
			ret = mso_clkrate_out(sdi, rate_map[i].val);
			if (ret == SR_OK)
				ctx->cur_rate = rate;
			return ret;
		}
	}
	return ret;
}

static inline uint16_t mso_calc_raw_from_mv(struct context *ctx)
{
	return (uint16_t) (0x200 -
			((ctx->dso_trigger_voltage / ctx->dso_probe_attn) /
			 ctx->vbit));
}

static int mso_configure_trigger(struct sr_dev_inst *sdi)
{
	struct context *ctx = sdi->priv;
	uint16_t ops[16];
	uint16_t dso_trigger = mso_calc_raw_from_mv(ctx);

	dso_trigger &= 0x3ff;
	if ((!ctx->trigger_slope && ctx->trigger_chan == 1) ||
			(ctx->trigger_slope &&
			 (ctx->trigger_chan == 0 ||
			  ctx->trigger_chan == 2 ||
			  ctx->trigger_chan == 3)))
		dso_trigger |= 0x400;

	switch (ctx->trigger_chan) {
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

	switch (ctx->trigger_outsrc) {
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

	ops[0] = mso_trans(5, ctx->la_trigger);
	ops[1] = mso_trans(6, ctx->la_trigger_mask);
	ops[2] = mso_trans(3, dso_trigger & 0xff);
	ops[3] = mso_trans(4, (dso_trigger >> 8) & 0xff);
	ops[4] = mso_trans(11,
			ctx->dso_trigger_width / SR_HZ_TO_NS(ctx->cur_rate));

	/* Select the SPI/I2C trigger config bank */
	ops[5] = mso_trans(REG_CTL2, (ctx->ctlbase2 | BITS_CTL2_BANK(2)));
	/* Configure the SPI/I2C protocol trigger */
	ops[6] = mso_trans(REG_PT_WORD(0), ctx->protocol_trigger.word[0]);
	ops[7] = mso_trans(REG_PT_WORD(1), ctx->protocol_trigger.word[1]);
	ops[8] = mso_trans(REG_PT_WORD(2), ctx->protocol_trigger.word[2]);
	ops[9] = mso_trans(REG_PT_WORD(3), ctx->protocol_trigger.word[3]);
	ops[10] = mso_trans(REG_PT_MASK(0), ctx->protocol_trigger.mask[0]);
	ops[11] = mso_trans(REG_PT_MASK(1), ctx->protocol_trigger.mask[1]);
	ops[12] = mso_trans(REG_PT_MASK(2), ctx->protocol_trigger.mask[2]);
	ops[13] = mso_trans(REG_PT_MASK(3), ctx->protocol_trigger.mask[3]);
	ops[14] = mso_trans(REG_PT_SPIMODE, ctx->protocol_trigger.spimode);
	/* Select the default config bank */
	ops[15] = mso_trans(REG_CTL2, ctx->ctlbase2);

	return mso_send_control_message(sdi, ARRAY_AND_SIZE(ops));
}

static int mso_configure_threshold_level(struct sr_dev_inst *sdi)
{
	struct context *ctx = sdi->priv;

	return mso_dac_out(sdi, la_threshold_map[ctx->la_threshold]);
}

static int mso_parse_serial(const char *iSerial, const char *iProduct,
			    struct context *ctx)
{
	unsigned int u1, u2, u3, u4, u5, u6;

	iProduct = iProduct;
	/* FIXME: This code is in the original app, but I think its
	 * used only for the GUI */
/*	if (strstr(iProduct, "REV_02") || strstr(iProduct, "REV_03"))
		ctx->num_sample_rates = 0x16;
	else
		ctx->num_sample_rates = 0x10; */

	/* parse iSerial */
	if (iSerial[0] != '4' || sscanf(iSerial, "%5u%3u%3u%1u%1u%6u",
				&u1, &u2, &u3, &u4, &u5, &u6) != 6)
		return SR_ERR;
	ctx->hwmodel = u4;
	ctx->hwrev = u5;
	ctx->serial = u6;
	ctx->vbit = u1 / 10000;
	if (ctx->vbit == 0)
		ctx->vbit = 4.19195;
	ctx->dac_offset = u2;
	if (ctx->dac_offset == 0)
		ctx->dac_offset = 0x1ff;
	ctx->offset_range = u3;
	if (ctx->offset_range == 0)
		ctx->offset_range = 0x17d;

	/*
	 * FIXME: There is more code on the original software to handle
	 * bigger iSerial strings, but as I can't test on my device
	 * I will not implement it yet
	 */

	return SR_OK;
}

static int hw_init(void)
{

	/* Nothing to do. */

	return SR_OK;
}

static int hw_scan(void)
{
	struct sr_dev_inst *sdi;
	int devcnt = 0;
	struct udev *udev;
	struct udev_enumerate *enumerate;
	struct udev_list_entry *devs, *dev_list_entry;
	struct context *ctx;

	/* It's easier to map usb<->serial using udev */
	/*
	 * FIXME: On windows we can get the same information from the
	 * registry, add an #ifdef here later
	 */
	udev = udev_new();
	if (!udev) {
		sr_err("mso19: Failed to initialize udev.");
		goto ret;
	}
	enumerate = udev_enumerate_new(udev);
	udev_enumerate_add_match_subsystem(enumerate, "usb-serial");
	udev_enumerate_scan_devices(enumerate);
	devs = udev_enumerate_get_list_entry(enumerate);
	udev_list_entry_foreach(dev_list_entry, devs) {
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
			sr_err("mso19: Unable to find parent usb device for %s",
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
			sr_err("mso19: Could not parse iProduct: %s", iProduct);
			continue;
		}
		strncpy(product, iProduct, s);
		product[s] = 0;
		strcpy(manufacturer, iProduct + s);

		if (!(ctx = g_try_malloc0(sizeof(struct context)))) {
			sr_err("mso19: %s: ctx malloc failed", __func__);
			continue; /* TODO: Errors handled correctly? */
		}

		if (mso_parse_serial(iSerial, iProduct, ctx) != SR_OK) {
			sr_err("mso19: Invalid iSerial: %s", iSerial);
			goto err_free_ctx;
		}
		sprintf(hwrev, "r%d", ctx->hwrev);

		/* hardware initial state */
		ctx->ctlbase1 = 0;
		{
			/* Initialize the protocol trigger configuration */
			int i;
			for (i = 0; i < 4; i++) {
				ctx->protocol_trigger.word[i] = 0;
				ctx->protocol_trigger.mask[i] = 0xff;
			}
			ctx->protocol_trigger.spimode = 0;
		}

		sdi = sr_dev_inst_new(devcnt, SR_ST_INITIALIZING,
				      manufacturer, product, hwrev);
		if (!sdi) {
			sr_err("mso19: Unable to create device instance for %s",
			       sysname);
			goto err_free_ctx;
		}

		/* save a pointer to our private instance data */
		sdi->priv = ctx;

		sdi->serial = sr_serial_dev_inst_new(path, -1);
		if (!sdi->serial)
			goto err_dev_inst_free;

		dev_insts = g_slist_append(dev_insts, sdi);
		devcnt++;
		continue;

err_dev_inst_free:
		sr_dev_inst_free(sdi);
err_free_ctx:
		g_free(ctx);
	}

	udev_enumerate_unref(enumerate);
	udev_unref(udev);

ret:
	return devcnt;
}

static int hw_cleanup(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	int ret;

	ret = SR_OK;
	/* Properly close all devices. */
	for (l = dev_insts; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("mso19: %s: sdi was NULL, continuing", __func__);
			ret = SR_ERR_BUG;
			continue;
		}
		if (sdi->serial->fd != -1)
			serial_close(sdi->serial->fd);
		sr_dev_inst_free(sdi);
	}
	g_slist_free(dev_insts);
	dev_insts = NULL;

	return ret;
}

static int hw_dev_open(int dev_index)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	int ret = SR_ERR;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return ret;

	ctx = sdi->priv;
	sdi->serial->fd = serial_open(sdi->serial->port, O_RDWR);
	if (sdi->serial->fd == -1)
		return ret;

	ret = serial_set_params(sdi->serial->fd, 460800, 8, 0, 1, 2);
	if (ret != SR_OK)
		return ret;

	sdi->status = SR_ST_ACTIVE;

	/* FIXME: discard serial buffer */

	mso_check_trigger(sdi, &ctx->trigger_state);
	sr_dbg("mso19: trigger state: 0x%x", ctx->trigger_state);

	ret = mso_reset_adc(sdi);
	if (ret != SR_OK)
		return ret;

	mso_check_trigger(sdi, &ctx->trigger_state);
	sr_dbg("mso19: trigger state: 0x%x", ctx->trigger_state);

//	ret = mso_reset_fsm(sdi);
//	if (ret != SR_OK)
//		return ret;

	sr_dbg("mso19: Finished %s", __func__);

//	return SR_ERR;
	return SR_OK;
}

static int hw_dev_close(int dev_index)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index))) {
		sr_err("mso19: %s: sdi was NULL", __func__);
		return SR_ERR_BUG;
	}

	/* TODO */
	if (sdi->serial->fd != -1) {
		serial_close(sdi->serial->fd);
		sdi->serial->fd = -1;
		sdi->status = SR_ST_INACTIVE;
	}

	sr_dbg("mso19: finished %s", __func__);
	return SR_OK;
}

static const void *hw_dev_info_get(int dev_index, int dev_info_id)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	const void *info = NULL;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return NULL;
	ctx = sdi->priv;

	switch (dev_info_id) {
	case SR_DI_INST:
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
		info = &ctx->cur_rate;
		break;
	}
	return info;
}

static int hw_dev_status_get(int dev_index)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ST_NOT_FOUND;

	return sdi->status;
}

static const int *hw_hwcap_get_all(void)
{
	return hwcaps;
}

static int hw_dev_config_set(int dev_index, int hwcap, const void *value)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return SR_ERR;

	switch (hwcap) {
	case SR_HWCAP_SAMPLERATE:
		return mso_configure_rate(sdi, *(const uint64_t *) value);
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
static int receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct context *ctx = sdi->priv;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint8_t in[1024], logic_out[1024];
	double analog_out[1024];
	size_t i, s;

	/* Avoid compiler warnings. */
	(void)revents;

	s = serial_read(fd, in, sizeof(in));
	if (s <= 0)
		return FALSE;

	/* No samples */
	if (ctx->trigger_state != MSO_TRIGGER_DATAREADY) {
		ctx->trigger_state = in[0];
		if (ctx->trigger_state == MSO_TRIGGER_DATAREADY) {
			mso_read_buffer(sdi);
			ctx->buffer_n = 0;
		} else {
			mso_check_trigger(sdi, NULL);
		}
		return FALSE;
	}

	/* the hardware always dumps 1024 samples, 24bits each */
	if (ctx->buffer_n < 3072) {
		memcpy(ctx->buffer + ctx->buffer_n, in, s);
		ctx->buffer_n += s;
	}
	if (ctx->buffer_n < 3072)
		return FALSE;

	/* do the conversion */
	for (i = 0; i < 1024; i++) {
		/* FIXME: Need to do conversion to mV */
		analog_out[i] = (ctx->buffer[i * 3] & 0x3f) |
			((ctx->buffer[i * 3 + 1] & 0xf) << 6);
		logic_out[i] = ((ctx->buffer[i * 3 + 1] & 0x30) >> 4) |
			((ctx->buffer[i * 3 + 2] & 0x3f) << 2);
	}

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.length = 1024;
	logic.unitsize = 1;
	logic.data = logic_out;
	sr_session_send(ctx->session_dev_id, &packet);

	// Dont bother fixing this yet, keep it "old style"
	/*
	packet.type = SR_DF_ANALOG;
	packet.length = 1024;
	packet.unitsize = sizeof(double);
	packet.payload = analog_out;
	sr_session_send(ctx->session_dev_id, &packet);
	*/

	packet.type = SR_DF_END;
	sr_session_send(ctx->session_dev_id, &packet);

	return TRUE;
}

static int hw_dev_acquisition_start(int dev_index, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	int ret = SR_ERR;

	if (!(sdi = sr_dev_inst_get(dev_insts, dev_index)))
		return ret;
	ctx = sdi->priv;

	/* FIXME: No need to do full reconfigure every time */
//	ret = mso_reset_fsm(sdi);
//	if (ret != SR_OK)
//		return ret;

	/* FIXME: ACDC Mode */
	ctx->ctlbase1 &= 0x7f;
//	ctx->ctlbase1 |= ctx->acdcmode;

	ret = mso_configure_rate(sdi, ctx->cur_rate);
	if (ret != SR_OK)
		return ret;

	/* set dac offset */
	ret = mso_dac_out(sdi, ctx->dac_offset);
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

	mso_check_trigger(sdi, &ctx->trigger_state);
	ret = mso_check_trigger(sdi, NULL);
	if (ret != SR_OK)
		return ret;

	ctx->session_dev_id = cb_data;
	sr_source_add(sdi->serial->fd, G_IO_IN, -1, receive_data, sdi);

	packet.type = SR_DF_HEADER;
	packet.payload = (unsigned char *) &header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	header.samplerate = ctx->cur_rate;
	// header.num_analog_probes = 1;
	header.num_logic_probes = 8;
	sr_session_send(ctx->session_dev_id, &packet);

	return ret;
}

/* TODO: This stops acquisition on ALL devices, ignoring dev_index. */
static int hw_dev_acquisition_stop(int dev_index, void *cb_data)
{
	struct sr_datafeed_packet packet;

	/* Avoid compiler warnings. */
	(void)dev_index;

	packet.type = SR_DF_END;
	sr_session_send(cb_data, &packet);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver link_mso19_driver_info = {
	.name = "link-mso19",
	.longname = "Link Instruments MSO-19",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.dev_info_get = hw_dev_info_get,
	.dev_status_get = hw_dev_status_get,
	.hwcap_get_all = hw_hwcap_get_all,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
};
