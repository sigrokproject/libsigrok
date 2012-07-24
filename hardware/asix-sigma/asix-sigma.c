/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Håvard Espeland <gus@ping.uio.no>,
 * Copyright (C) 2010 Martin Stensgård <mastensg@ping.uio.no>
 * Copyright (C) 2010 Carl Henrik Lunde <chlunde@ping.uio.no>
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

/*
 * ASIX SIGMA/SIGMA2 logic analyzer driver
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <ftdi.h>
#include <string.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "asix-sigma.h"

#define USB_VENDOR			0xa600
#define USB_PRODUCT			0xa000
#define USB_DESCRIPTION			"ASIX SIGMA"
#define USB_VENDOR_NAME			"ASIX"
#define USB_MODEL_NAME			"SIGMA"
#define USB_MODEL_VERSION		""
#define TRIGGER_TYPES			"rf10"
#define NUM_PROBES			16

SR_PRIV struct sr_dev_driver asix_sigma_driver_info;
static struct sr_dev_driver *adi = &asix_sigma_driver_info;

static const uint64_t supported_samplerates[] = {
	SR_KHZ(200),
	SR_KHZ(250),
	SR_KHZ(500),
	SR_MHZ(1),
	SR_MHZ(5),
	SR_MHZ(10),
	SR_MHZ(25),
	SR_MHZ(50),
	SR_MHZ(100),
	SR_MHZ(200),
	0,
};

/*
 * Probe numbers seem to go from 1-16, according to this image:
 * http://tools.asix.net/img/sigma_sigmacab_pins_720.jpg
 * (the cable has two additional GND pins, and a TI and TO pin)
 */
static const char *probe_names[NUM_PROBES + 1] = {
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"10",
	"11",
	"12",
	"13",
	"14",
	"15",
	"16",
	NULL,
};

static const struct sr_samplerates samplerates = {
	0,
	0,
	0,
	supported_samplerates,
};

static const int hwcaps[] = {
	SR_HWCAP_LOGIC_ANALYZER,
	SR_HWCAP_SAMPLERATE,
	SR_HWCAP_CAPTURE_RATIO,
	SR_HWCAP_PROBECONFIG,

	SR_HWCAP_LIMIT_MSEC,
	0,
};

/* Force the FPGA to reboot. */
static uint8_t suicide[] = {
	0x84, 0x84, 0x88, 0x84, 0x88, 0x84, 0x88, 0x84,
};

/* Prepare to upload firmware (FPGA specific). */
static uint8_t init[] = {
	0x03, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01,
};

/* Initialize the logic analyzer mode. */
static uint8_t logic_mode_start[] = {
	0x00, 0x40, 0x0f, 0x25, 0x35, 0x40,
	0x2a, 0x3a, 0x40, 0x03, 0x20, 0x38,
};

static const char *firmware_files[] = {
	"asix-sigma-50.fw",	/* 50 MHz, supports 8 bit fractions */
	"asix-sigma-100.fw",	/* 100 MHz */
	"asix-sigma-200.fw",	/* 200 MHz */
	"asix-sigma-50sync.fw",	/* Synchronous clock from pin */
	"asix-sigma-phasor.fw",	/* Frequency counter */
};

static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data);

static int sigma_read(void *buf, size_t size, struct context *ctx)
{
	int ret;

	ret = ftdi_read_data(&ctx->ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		sr_err("sigma: ftdi_read_data failed: %s",
		       ftdi_get_error_string(&ctx->ftdic));
	}

	return ret;
}

static int sigma_write(void *buf, size_t size, struct context *ctx)
{
	int ret;

	ret = ftdi_write_data(&ctx->ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		sr_err("sigma: ftdi_write_data failed: %s",
		       ftdi_get_error_string(&ctx->ftdic));
	} else if ((size_t) ret != size) {
		sr_err("sigma: ftdi_write_data did not complete write.");
	}

	return ret;
}

static int sigma_write_register(uint8_t reg, uint8_t *data, size_t len,
				struct context *ctx)
{
	size_t i;
	uint8_t buf[len + 2];
	int idx = 0;

	buf[idx++] = REG_ADDR_LOW | (reg & 0xf);
	buf[idx++] = REG_ADDR_HIGH | (reg >> 4);

	for (i = 0; i < len; ++i) {
		buf[idx++] = REG_DATA_LOW | (data[i] & 0xf);
		buf[idx++] = REG_DATA_HIGH_WRITE | (data[i] >> 4);
	}

	return sigma_write(buf, idx, ctx);
}

static int sigma_set_register(uint8_t reg, uint8_t value, struct context *ctx)
{
	return sigma_write_register(reg, &value, 1, ctx);
}

static int sigma_read_register(uint8_t reg, uint8_t *data, size_t len,
			       struct context *ctx)
{
	uint8_t buf[3];

	buf[0] = REG_ADDR_LOW | (reg & 0xf);
	buf[1] = REG_ADDR_HIGH | (reg >> 4);
	buf[2] = REG_READ_ADDR;

	sigma_write(buf, sizeof(buf), ctx);

	return sigma_read(data, len, ctx);
}

static uint8_t sigma_get_register(uint8_t reg, struct context *ctx)
{
	uint8_t value;

	if (1 != sigma_read_register(reg, &value, 1, ctx)) {
		sr_err("sigma: sigma_get_register: 1 byte expected");
		return 0;
	}

	return value;
}

static int sigma_read_pos(uint32_t *stoppos, uint32_t *triggerpos,
			  struct context *ctx)
{
	uint8_t buf[] = {
		REG_ADDR_LOW | READ_TRIGGER_POS_LOW,

		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
		REG_READ_ADDR | NEXT_REG,
	};
	uint8_t result[6];

	sigma_write(buf, sizeof(buf), ctx);

	sigma_read(result, sizeof(result), ctx);

	*triggerpos = result[0] | (result[1] << 8) | (result[2] << 16);
	*stoppos = result[3] | (result[4] << 8) | (result[5] << 16);

	/* Not really sure why this must be done, but according to spec. */
	if ((--*stoppos & 0x1ff) == 0x1ff)
		stoppos -= 64;

	if ((*--triggerpos & 0x1ff) == 0x1ff)
		triggerpos -= 64;

	return 1;
}

static int sigma_read_dram(uint16_t startchunk, size_t numchunks,
			   uint8_t *data, struct context *ctx)
{
	size_t i;
	uint8_t buf[4096];
	int idx = 0;

	/* Send the startchunk. Index start with 1. */
	buf[0] = startchunk >> 8;
	buf[1] = startchunk & 0xff;
	sigma_write_register(WRITE_MEMROW, buf, 2, ctx);

	/* Read the DRAM. */
	buf[idx++] = REG_DRAM_BLOCK;
	buf[idx++] = REG_DRAM_WAIT_ACK;

	for (i = 0; i < numchunks; ++i) {
		/* Alternate bit to copy from DRAM to cache. */
		if (i != (numchunks - 1))
			buf[idx++] = REG_DRAM_BLOCK | (((i + 1) % 2) << 4);

		buf[idx++] = REG_DRAM_BLOCK_DATA | ((i % 2) << 4);

		if (i != (numchunks - 1))
			buf[idx++] = REG_DRAM_WAIT_ACK;
	}

	sigma_write(buf, idx, ctx);

	return sigma_read(data, numchunks * CHUNK_SIZE, ctx);
}

/* Upload trigger look-up tables to Sigma. */
static int sigma_write_trigger_lut(struct triggerlut *lut, struct context *ctx)
{
	int i;
	uint8_t tmp[2];
	uint16_t bit;

	/* Transpose the table and send to Sigma. */
	for (i = 0; i < 16; ++i) {
		bit = 1 << i;

		tmp[0] = tmp[1] = 0;

		if (lut->m2d[0] & bit)
			tmp[0] |= 0x01;
		if (lut->m2d[1] & bit)
			tmp[0] |= 0x02;
		if (lut->m2d[2] & bit)
			tmp[0] |= 0x04;
		if (lut->m2d[3] & bit)
			tmp[0] |= 0x08;

		if (lut->m3 & bit)
			tmp[0] |= 0x10;
		if (lut->m3s & bit)
			tmp[0] |= 0x20;
		if (lut->m4 & bit)
			tmp[0] |= 0x40;

		if (lut->m0d[0] & bit)
			tmp[1] |= 0x01;
		if (lut->m0d[1] & bit)
			tmp[1] |= 0x02;
		if (lut->m0d[2] & bit)
			tmp[1] |= 0x04;
		if (lut->m0d[3] & bit)
			tmp[1] |= 0x08;

		if (lut->m1d[0] & bit)
			tmp[1] |= 0x10;
		if (lut->m1d[1] & bit)
			tmp[1] |= 0x20;
		if (lut->m1d[2] & bit)
			tmp[1] |= 0x40;
		if (lut->m1d[3] & bit)
			tmp[1] |= 0x80;

		sigma_write_register(WRITE_TRIGGER_SELECT0, tmp, sizeof(tmp),
				     ctx);
		sigma_set_register(WRITE_TRIGGER_SELECT1, 0x30 | i, ctx);
	}

	/* Send the parameters */
	sigma_write_register(WRITE_TRIGGER_SELECT0, (uint8_t *) &lut->params,
			     sizeof(lut->params), ctx);

	return SR_OK;
}

/* Generate the bitbang stream for programming the FPGA. */
static int bin2bitbang(const char *filename,
		       unsigned char **buf, size_t *buf_size)
{
	FILE *f;
	unsigned long file_size;
	unsigned long offset = 0;
	unsigned char *p;
	uint8_t *firmware;
	unsigned long fwsize = 0;
	const int buffer_size = 65536;
	size_t i;
	int c, bit, v;
	uint32_t imm = 0x3f6df2ab;

	f = g_fopen(filename, "rb");
	if (!f) {
		sr_err("sigma: g_fopen(\"%s\", \"rb\")", filename);
		return SR_ERR;
	}

	if (-1 == fseek(f, 0, SEEK_END)) {
		sr_err("sigma: fseek on %s failed", filename);
		fclose(f);
		return SR_ERR;
	}

	file_size = ftell(f);

	fseek(f, 0, SEEK_SET);

	if (!(firmware = g_try_malloc(buffer_size))) {
		sr_err("sigma: %s: firmware malloc failed", __func__);
		fclose(f);
		return SR_ERR_MALLOC;
	}

	while ((c = getc(f)) != EOF) {
		imm = (imm + 0xa853753) % 177 + (imm * 0x8034052);
		firmware[fwsize++] = c ^ imm;
	}
	fclose(f);

	if(fwsize != file_size) {
	    sr_err("sigma: %s: Error reading firmware", filename);
	    fclose(f);
	    g_free(firmware);
	    return SR_ERR;
	}

	*buf_size = fwsize * 2 * 8;

	*buf = p = (unsigned char *)g_try_malloc(*buf_size);
	if (!p) {
		sr_err("sigma: %s: buf/p malloc failed", __func__);
		g_free(firmware);
		return SR_ERR_MALLOC;
	}

	for (i = 0; i < fwsize; ++i) {
		for (bit = 7; bit >= 0; --bit) {
			v = firmware[i] & 1 << bit ? 0x40 : 0x00;
			p[offset++] = v | 0x01;
			p[offset++] = v;
		}
	}

	g_free(firmware);

	if (offset != *buf_size) {
		g_free(*buf);
		sr_err("sigma: Error reading firmware %s "
		       "offset=%ld, file_size=%ld, buf_size=%zd.",
		       filename, offset, file_size, *buf_size);

		return SR_ERR;
	}

	return SR_OK;
}

static void clear_instances(void)
{
	GSList *l;
	struct sr_dev_inst *sdi;
	struct context *ctx;

	/* Properly close all devices. */
	for (l = adi->instances; l; l = l->next) {
		if (!(sdi = l->data)) {
			/* Log error, but continue cleaning up the rest. */
			sr_err("sigma: %s: sdi was NULL, continuing", __func__);
			continue;
		}
		if (sdi->priv) {
			ctx = sdi->priv;
			ftdi_free(&ctx->ftdic);
			g_free(ctx);
		}
		sr_dev_inst_free(sdi);
	}
	g_slist_free(adi->instances);
	adi->instances = NULL;

}

static int hw_init(void)
{

	/* Nothing to do. */

	return SR_OK;
}

static GSList *hw_scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct context *ctx;
	GSList *devices;
	struct ftdi_device_list *devlist;
	char serial_txt[10];
	uint32_t serial;
	int ret;

	(void)options;
	devices = NULL;
	clear_instances();

	if (!(ctx = g_try_malloc(sizeof(struct context)))) {
		sr_err("sigma: %s: ctx malloc failed", __func__);
		return NULL;
	}

	ftdi_init(&ctx->ftdic);

	/* Look for SIGMAs. */

	if ((ret = ftdi_usb_find_all(&ctx->ftdic, &devlist,
	    USB_VENDOR, USB_PRODUCT)) <= 0) {
		if (ret < 0)
			sr_err("ftdi_usb_find_all(): %d", ret);
		goto free;
	}

	/* Make sure it's a version 1 or 2 SIGMA. */
	ftdi_usb_get_strings(&ctx->ftdic, devlist->dev, NULL, 0, NULL, 0,
			     serial_txt, sizeof(serial_txt));
	sscanf(serial_txt, "%x", &serial);

	if (serial < 0xa6010000 || serial > 0xa602ffff) {
		sr_err("sigma: Only SIGMA and SIGMA2 are supported "
		       "in this version of sigrok.");
		goto free;
	}

	sr_info("Found ASIX SIGMA - Serial: %s", serial_txt);

	ctx->cur_samplerate = 0;
	ctx->period_ps = 0;
	ctx->limit_msec = 0;
	ctx->cur_firmware = -1;
	ctx->num_probes = 0;
	ctx->samples_per_event = 0;
	ctx->capture_ratio = 50;
	ctx->use_triggers = 0;

	/* Register SIGMA device. */
	if (!(sdi = sr_dev_inst_new(0, SR_ST_INITIALIZING, USB_VENDOR_NAME,
				    USB_MODEL_NAME, USB_MODEL_VERSION))) {
		sr_err("sigma: %s: sdi was NULL", __func__);
		goto free;
	}
	sdi->driver = adi;
	devices = g_slist_append(devices, sdi);
	adi->instances = g_slist_append(adi->instances, sdi);
	sdi->priv = ctx;

	/* We will open the device again when we need it. */
	ftdi_list_free(&devlist);

	return devices;

free:
	ftdi_deinit(&ctx->ftdic);
	g_free(ctx);
	return NULL;
}

static int upload_firmware(int firmware_idx, struct context *ctx)
{
	int ret;
	unsigned char *buf;
	unsigned char pins;
	size_t buf_size;
	unsigned char result[32];
	char firmware_path[128];

	/* Make sure it's an ASIX SIGMA. */
	if ((ret = ftdi_usb_open_desc(&ctx->ftdic,
		USB_VENDOR, USB_PRODUCT, USB_DESCRIPTION, NULL)) < 0) {
		sr_err("sigma: ftdi_usb_open failed: %s",
		       ftdi_get_error_string(&ctx->ftdic));
		return 0;
	}

	if ((ret = ftdi_set_bitmode(&ctx->ftdic, 0xdf, BITMODE_BITBANG)) < 0) {
		sr_err("sigma: ftdi_set_bitmode failed: %s",
		       ftdi_get_error_string(&ctx->ftdic));
		return 0;
	}

	/* Four times the speed of sigmalogan - Works well. */
	if ((ret = ftdi_set_baudrate(&ctx->ftdic, 750000)) < 0) {
		sr_err("sigma: ftdi_set_baudrate failed: %s",
		       ftdi_get_error_string(&ctx->ftdic));
		return 0;
	}

	/* Force the FPGA to reboot. */
	sigma_write(suicide, sizeof(suicide), ctx);
	sigma_write(suicide, sizeof(suicide), ctx);
	sigma_write(suicide, sizeof(suicide), ctx);
	sigma_write(suicide, sizeof(suicide), ctx);

	/* Prepare to upload firmware (FPGA specific). */
	sigma_write(init, sizeof(init), ctx);

	ftdi_usb_purge_buffers(&ctx->ftdic);

	/* Wait until the FPGA asserts INIT_B. */
	while (1) {
		ret = sigma_read(result, 1, ctx);
		if (result[0] & 0x20)
			break;
	}

	/* Prepare firmware. */
	snprintf(firmware_path, sizeof(firmware_path), "%s/%s", FIRMWARE_DIR,
		 firmware_files[firmware_idx]);

	if ((ret = bin2bitbang(firmware_path, &buf, &buf_size)) != SR_OK) {
		sr_err("sigma: An error occured while reading the firmware: %s",
		       firmware_path);
		return ret;
	}

	/* Upload firmare. */
	sr_info("sigma: Uploading firmware %s", firmware_files[firmware_idx]);
	sigma_write(buf, buf_size, ctx);

	g_free(buf);

	if ((ret = ftdi_set_bitmode(&ctx->ftdic, 0x00, BITMODE_RESET)) < 0) {
		sr_err("sigma: ftdi_set_bitmode failed: %s",
		       ftdi_get_error_string(&ctx->ftdic));
		return SR_ERR;
	}

	ftdi_usb_purge_buffers(&ctx->ftdic);

	/* Discard garbage. */
	while (1 == sigma_read(&pins, 1, ctx))
		;

	/* Initialize the logic analyzer mode. */
	sigma_write(logic_mode_start, sizeof(logic_mode_start), ctx);

	/* Expect a 3 byte reply. */
	ret = sigma_read(result, 3, ctx);
	if (ret != 3 ||
	    result[0] != 0xa6 || result[1] != 0x55 || result[2] != 0xaa) {
		sr_err("sigma: Configuration failed. Invalid reply received.");
		return SR_ERR;
	}

	ctx->cur_firmware = firmware_idx;

	sr_info("sigma: Firmware uploaded");

	return SR_OK;
}

static int hw_dev_open(struct sr_dev_inst *sdi)
{
	struct context *ctx;
	int ret;

	ctx = sdi->priv;

	/* Make sure it's an ASIX SIGMA. */
	if ((ret = ftdi_usb_open_desc(&ctx->ftdic,
		USB_VENDOR, USB_PRODUCT, USB_DESCRIPTION, NULL)) < 0) {

		sr_err("sigma: ftdi_usb_open failed: %s",
		       ftdi_get_error_string(&ctx->ftdic));

		return 0;
	}

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int set_samplerate(const struct sr_dev_inst *sdi, uint64_t samplerate)
{
	int i, ret;
	struct context *ctx = sdi->priv;

	for (i = 0; supported_samplerates[i]; i++) {
		if (supported_samplerates[i] == samplerate)
			break;
	}
	if (supported_samplerates[i] == 0)
		return SR_ERR_SAMPLERATE;

	if (samplerate <= SR_MHZ(50)) {
		ret = upload_firmware(0, ctx);
		ctx->num_probes = 16;
	}
	if (samplerate == SR_MHZ(100)) {
		ret = upload_firmware(1, ctx);
		ctx->num_probes = 8;
	}
	else if (samplerate == SR_MHZ(200)) {
		ret = upload_firmware(2, ctx);
		ctx->num_probes = 4;
	}

	ctx->cur_samplerate = samplerate;
	ctx->period_ps = 1000000000000 / samplerate;
	ctx->samples_per_event = 16 / ctx->num_probes;
	ctx->state.state = SIGMA_IDLE;

	return ret;
}

/*
 * In 100 and 200 MHz mode, only a single pin rising/falling can be
 * set as trigger. In other modes, two rising/falling triggers can be set,
 * in addition to value/mask trigger for any number of probes.
 *
 * The Sigma supports complex triggers using boolean expressions, but this
 * has not been implemented yet.
 */
static int configure_probes(const struct sr_dev_inst *sdi, const GSList *probes)
{
	struct context *ctx = sdi->priv;
	const struct sr_probe *probe;
	const GSList *l;
	int trigger_set = 0;
	int probebit;

	memset(&ctx->trigger, 0, sizeof(struct sigma_trigger));

	for (l = probes; l; l = l->next) {
		probe = (struct sr_probe *)l->data;
		probebit = 1 << (probe->index);

		if (!probe->enabled || !probe->trigger)
			continue;

		if (ctx->cur_samplerate >= SR_MHZ(100)) {
			/* Fast trigger support. */
			if (trigger_set) {
				sr_err("sigma: ASIX SIGMA only supports a single "
				       "pin trigger in 100 and 200MHz mode.");
				return SR_ERR;
			}
			if (probe->trigger[0] == 'f')
				ctx->trigger.fallingmask |= probebit;
			else if (probe->trigger[0] == 'r')
				ctx->trigger.risingmask |= probebit;
			else {
				sr_err("sigma: ASIX SIGMA only supports "
				       "rising/falling trigger in 100 "
				       "and 200MHz mode.");
				return SR_ERR;
			}

			++trigger_set;
		} else {
			/* Simple trigger support (event). */
			if (probe->trigger[0] == '1') {
				ctx->trigger.simplevalue |= probebit;
				ctx->trigger.simplemask |= probebit;
			}
			else if (probe->trigger[0] == '0') {
				ctx->trigger.simplevalue &= ~probebit;
				ctx->trigger.simplemask |= probebit;
			}
			else if (probe->trigger[0] == 'f') {
				ctx->trigger.fallingmask |= probebit;
				++trigger_set;
			}
			else if (probe->trigger[0] == 'r') {
				ctx->trigger.risingmask |= probebit;
				++trigger_set;
			}

			/*
			 * Actually, Sigma supports 2 rising/falling triggers,
			 * but they are ORed and the current trigger syntax
			 * does not permit ORed triggers.
			 */
			if (trigger_set > 1) {
				sr_err("sigma: ASIX SIGMA only supports 1 "
				       "rising/falling triggers.");
				return SR_ERR;
			}
		}

		if (trigger_set)
			ctx->use_triggers = 1;
	}

	return SR_OK;
}

static int hw_dev_close(struct sr_dev_inst *sdi)
{
	struct context *ctx;

	if (!(ctx = sdi->priv)) {
		sr_err("sigma: %s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	/* TODO */
	if (sdi->status == SR_ST_ACTIVE)
		ftdi_usb_close(&ctx->ftdic);

	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int hw_cleanup(void)
{

	clear_instances();

	return SR_OK;
}

static int hw_info_get(int info_id, const void **data,
       const struct sr_dev_inst *sdi)
{
	struct context *ctx;

	switch (info_id) {
	case SR_DI_INST:
		*data = sdi;
		break;
	case SR_DI_HWCAPS:
		*data = hwcaps;
		break;
	case SR_DI_NUM_PROBES:
		*data = GINT_TO_POINTER(NUM_PROBES);
		break;
	case SR_DI_PROBE_NAMES:
		*data = probe_names;
		break;
	case SR_DI_SAMPLERATES:
		*data = &samplerates;
		break;
	case SR_DI_TRIGGER_TYPES:
		*data = (char *)TRIGGER_TYPES;
		break;
	case SR_DI_CUR_SAMPLERATE:
		if (sdi) {
			ctx = sdi->priv;
			*data = &ctx->cur_samplerate;
		} else
			return SR_ERR;
		break;
	default:
		return SR_ERR_ARG;
	}

	return SR_OK;
}

static int hw_dev_config_set(const struct sr_dev_inst *sdi, int hwcap,
		const void *value)
{
	struct context *ctx;
	int ret;

	ctx = sdi->priv;

	if (hwcap == SR_HWCAP_SAMPLERATE) {
		ret = set_samplerate(sdi, *(const uint64_t *)value);
	} else if (hwcap == SR_HWCAP_PROBECONFIG) {
		ret = configure_probes(sdi, value);
	} else if (hwcap == SR_HWCAP_LIMIT_MSEC) {
		ctx->limit_msec = *(const uint64_t *)value;
		if (ctx->limit_msec > 0)
			ret = SR_OK;
		else
			ret = SR_ERR;
	} else if (hwcap == SR_HWCAP_CAPTURE_RATIO) {
		ctx->capture_ratio = *(const uint64_t *)value;
		if (ctx->capture_ratio < 0 || ctx->capture_ratio > 100)
			ret = SR_ERR;
		else
			ret = SR_OK;
	} else {
		ret = SR_ERR;
	}

	return ret;
}

/* Software trigger to determine exact trigger position. */
static int get_trigger_offset(uint16_t *samples, uint16_t last_sample,
			      struct sigma_trigger *t)
{
	int i;

	for (i = 0; i < 8; ++i) {
		if (i > 0)
			last_sample = samples[i-1];

		/* Simple triggers. */
		if ((samples[i] & t->simplemask) != t->simplevalue)
			continue;

		/* Rising edge. */
		if ((last_sample & t->risingmask) != 0 || (samples[i] &
		    t->risingmask) != t->risingmask)
			continue;

		/* Falling edge. */
		if ((last_sample & t->fallingmask) != t->fallingmask ||
		    (samples[i] & t->fallingmask) != 0)
			continue;

		break;
	}

	/* If we did not match, return original trigger pos. */
	return i & 0x7;
}

/*
 * Decode chunk of 1024 bytes, 64 clusters, 7 events per cluster.
 * Each event is 20ns apart, and can contain multiple samples.
 *
 * For 200 MHz, events contain 4 samples for each channel, spread 5 ns apart.
 * For 100 MHz, events contain 2 samples for each channel, spread 10 ns apart.
 * For 50 MHz and below, events contain one sample for each channel,
 * spread 20 ns apart.
 */
static int decode_chunk_ts(uint8_t *buf, uint16_t *lastts,
			   uint16_t *lastsample, int triggerpos,
			   uint16_t limit_chunk, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct context *ctx = sdi->priv;
	uint16_t tsdiff, ts;
	uint16_t samples[65536 * ctx->samples_per_event];
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	int i, j, k, l, numpad, tosend;
	size_t n = 0, sent = 0;
	int clustersize = EVENTS_PER_CLUSTER * ctx->samples_per_event;
	uint16_t *event;
	uint16_t cur_sample;
	int triggerts = -1;

	/* Check if trigger is in this chunk. */
	if (triggerpos != -1) {
		if (ctx->cur_samplerate <= SR_MHZ(50))
			triggerpos -= EVENTS_PER_CLUSTER - 1;

		if (triggerpos < 0)
			triggerpos = 0;

		/* Find in which cluster the trigger occured. */
		triggerts = triggerpos / 7;
	}

	/* For each ts. */
	for (i = 0; i < 64; ++i) {
		ts = *(uint16_t *) &buf[i * 16];
		tsdiff = ts - *lastts;
		*lastts = ts;

		/* Decode partial chunk. */
		if (limit_chunk && ts > limit_chunk)
			return SR_OK;

		/* Pad last sample up to current point. */
		numpad = tsdiff * ctx->samples_per_event - clustersize;
		if (numpad > 0) {
			for (j = 0; j < numpad; ++j)
				samples[j] = *lastsample;

			n = numpad;
		}

		/* Send samples between previous and this timestamp to sigrok. */
		sent = 0;
		while (sent < n) {
			tosend = MIN(2048, n - sent);

			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = tosend * sizeof(uint16_t);
			logic.unitsize = 2;
			logic.data = samples + sent;
			sr_session_send(ctx->session_dev_id, &packet);

			sent += tosend;
		}
		n = 0;

		event = (uint16_t *) &buf[i * 16 + 2];
		cur_sample = 0;

		/* For each event in cluster. */
		for (j = 0; j < 7; ++j) {

			/* For each sample in event. */
			for (k = 0; k < ctx->samples_per_event; ++k) {
				cur_sample = 0;

				/* For each probe. */
				for (l = 0; l < ctx->num_probes; ++l)
					cur_sample |= (!!(event[j] & (1 << (l *
					   ctx->samples_per_event + k)))) << l;

				samples[n++] = cur_sample;
			}
		}

		/* Send data up to trigger point (if triggered). */
		sent = 0;
		if (i == triggerts) {
			/*
			 * Trigger is not always accurate to sample because of
			 * pipeline delay. However, it always triggers before
			 * the actual event. We therefore look at the next
			 * samples to pinpoint the exact position of the trigger.
			 */
			tosend = get_trigger_offset(samples, *lastsample,
						    &ctx->trigger);

			if (tosend > 0) {
				packet.type = SR_DF_LOGIC;
				packet.payload = &logic;
				logic.length = tosend * sizeof(uint16_t);
				logic.unitsize = 2;
				logic.data = samples;
				sr_session_send(ctx->session_dev_id, &packet);

				sent += tosend;
			}

			/* Only send trigger if explicitly enabled. */
			if (ctx->use_triggers) {
				packet.type = SR_DF_TRIGGER;
				sr_session_send(ctx->session_dev_id, &packet);
			}
		}

		/* Send rest of the chunk to sigrok. */
		tosend = n - sent;

		if (tosend > 0) {
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.length = tosend * sizeof(uint16_t);
			logic.unitsize = 2;
			logic.data = samples + sent;
			sr_session_send(ctx->session_dev_id, &packet);
		}

		*lastsample = samples[n - 1];
	}

	return SR_OK;
}

static int receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct context *ctx = sdi->priv;
	struct sr_datafeed_packet packet;
	const int chunks_per_read = 32;
	unsigned char buf[chunks_per_read * CHUNK_SIZE];
	int bufsz, numchunks, i, newchunks;
	uint64_t running_msec;
	struct timeval tv;

	/* Avoid compiler warnings. */
	(void)fd;
	(void)revents;

	/* Get the current position. */
	sigma_read_pos(&ctx->state.stoppos, &ctx->state.triggerpos, ctx);

	numchunks = (ctx->state.stoppos + 511) / 512;

	if (ctx->state.state == SIGMA_IDLE)
		return TRUE;

	if (ctx->state.state == SIGMA_CAPTURE) {
		/* Check if the timer has expired, or memory is full. */
		gettimeofday(&tv, 0);
		running_msec = (tv.tv_sec - ctx->start_tv.tv_sec) * 1000 +
			(tv.tv_usec - ctx->start_tv.tv_usec) / 1000;

		if (running_msec < ctx->limit_msec && numchunks < 32767)
			return TRUE; /* While capturing... */
		else
			hw_dev_acquisition_stop(sdi, sdi);

	} else if (ctx->state.state == SIGMA_DOWNLOAD) {
		if (ctx->state.chunks_downloaded >= numchunks) {
			/* End of samples. */
			packet.type = SR_DF_END;
			sr_session_send(ctx->session_dev_id, &packet);

			ctx->state.state = SIGMA_IDLE;

			return TRUE;
		}

		newchunks = MIN(chunks_per_read,
				numchunks - ctx->state.chunks_downloaded);

		sr_info("sigma: Downloading sample data: %.0f %%",
			100.0 * ctx->state.chunks_downloaded / numchunks);

		bufsz = sigma_read_dram(ctx->state.chunks_downloaded,
					newchunks, buf, ctx);
		/* TODO: Check bufsz. For now, just avoid compiler warnings. */
		(void)bufsz;

		/* Find first ts. */
		if (ctx->state.chunks_downloaded == 0) {
			ctx->state.lastts = *(uint16_t *) buf - 1;
			ctx->state.lastsample = 0;
		}

		/* Decode chunks and send them to sigrok. */
		for (i = 0; i < newchunks; ++i) {
			int limit_chunk = 0;

			/* The last chunk may potentially be only in part. */
			if (ctx->state.chunks_downloaded == numchunks - 1) {
				/* Find the last valid timestamp */
				limit_chunk = ctx->state.stoppos % 512 + ctx->state.lastts;
			}

			if (ctx->state.chunks_downloaded + i == ctx->state.triggerchunk)
				decode_chunk_ts(buf + (i * CHUNK_SIZE),
						&ctx->state.lastts,
						&ctx->state.lastsample,
						ctx->state.triggerpos & 0x1ff,
						limit_chunk, sdi);
			else
				decode_chunk_ts(buf + (i * CHUNK_SIZE),
						&ctx->state.lastts,
						&ctx->state.lastsample,
						-1, limit_chunk, sdi);

			++ctx->state.chunks_downloaded;
		}
	}

	return TRUE;
}

/* Build a LUT entry used by the trigger functions. */
static void build_lut_entry(uint16_t value, uint16_t mask, uint16_t *entry)
{
	int i, j, k, bit;

	/* For each quad probe. */
	for (i = 0; i < 4; ++i) {
		entry[i] = 0xffff;

		/* For each bit in LUT. */
		for (j = 0; j < 16; ++j)

			/* For each probe in quad. */
			for (k = 0; k < 4; ++k) {
				bit = 1 << (i * 4 + k);

				/* Set bit in entry */
				if ((mask & bit) &&
				    ((!(value & bit)) !=
				    (!(j & (1 << k)))))
					entry[i] &= ~(1 << j);
			}
	}
}

/* Add a logical function to LUT mask. */
static void add_trigger_function(enum triggerop oper, enum triggerfunc func,
				 int index, int neg, uint16_t *mask)
{
	int i, j;
	int x[2][2], tmp, a, b, aset, bset, rset;

	memset(x, 0, 4 * sizeof(int));

	/* Trigger detect condition. */
	switch (oper) {
	case OP_LEVEL:
		x[0][1] = 1;
		x[1][1] = 1;
		break;
	case OP_NOT:
		x[0][0] = 1;
		x[1][0] = 1;
		break;
	case OP_RISE:
		x[0][1] = 1;
		break;
	case OP_FALL:
		x[1][0] = 1;
		break;
	case OP_RISEFALL:
		x[0][1] = 1;
		x[1][0] = 1;
		break;
	case OP_NOTRISE:
		x[1][1] = 1;
		x[0][0] = 1;
		x[1][0] = 1;
		break;
	case OP_NOTFALL:
		x[1][1] = 1;
		x[0][0] = 1;
		x[0][1] = 1;
		break;
	case OP_NOTRISEFALL:
		x[1][1] = 1;
		x[0][0] = 1;
		break;
	}

	/* Transpose if neg is set. */
	if (neg) {
		for (i = 0; i < 2; ++i) {
			for (j = 0; j < 2; ++j) {
				tmp = x[i][j];
				x[i][j] = x[1-i][1-j];
				x[1-i][1-j] = tmp;
			}
		}
	}

	/* Update mask with function. */
	for (i = 0; i < 16; ++i) {
		a = (i >> (2 * index + 0)) & 1;
		b = (i >> (2 * index + 1)) & 1;

		aset = (*mask >> i) & 1;
		bset = x[b][a];

		if (func == FUNC_AND || func == FUNC_NAND)
			rset = aset & bset;
		else if (func == FUNC_OR || func == FUNC_NOR)
			rset = aset | bset;
		else if (func == FUNC_XOR || func == FUNC_NXOR)
			rset = aset ^ bset;

		if (func == FUNC_NAND || func == FUNC_NOR || func == FUNC_NXOR)
			rset = !rset;

		*mask &= ~(1 << i);

		if (rset)
			*mask |= 1 << i;
	}
}

/*
 * Build trigger LUTs used by 50 MHz and lower sample rates for supporting
 * simple pin change and state triggers. Only two transitions (rise/fall) can be
 * set at any time, but a full mask and value can be set (0/1).
 */
static int build_basic_trigger(struct triggerlut *lut, struct context *ctx)
{
	int i,j;
	uint16_t masks[2] = { 0, 0 };

	memset(lut, 0, sizeof(struct triggerlut));

	/* Contant for simple triggers. */
	lut->m4 = 0xa000;

	/* Value/mask trigger support. */
	build_lut_entry(ctx->trigger.simplevalue, ctx->trigger.simplemask,
			lut->m2d);

	/* Rise/fall trigger support. */
	for (i = 0, j = 0; i < 16; ++i) {
		if (ctx->trigger.risingmask & (1 << i) ||
		    ctx->trigger.fallingmask & (1 << i))
			masks[j++] = 1 << i;
	}

	build_lut_entry(masks[0], masks[0], lut->m0d);
	build_lut_entry(masks[1], masks[1], lut->m1d);

	/* Add glue logic */
	if (masks[0] || masks[1]) {
		/* Transition trigger. */
		if (masks[0] & ctx->trigger.risingmask)
			add_trigger_function(OP_RISE, FUNC_OR, 0, 0, &lut->m3);
		if (masks[0] & ctx->trigger.fallingmask)
			add_trigger_function(OP_FALL, FUNC_OR, 0, 0, &lut->m3);
		if (masks[1] & ctx->trigger.risingmask)
			add_trigger_function(OP_RISE, FUNC_OR, 1, 0, &lut->m3);
		if (masks[1] & ctx->trigger.fallingmask)
			add_trigger_function(OP_FALL, FUNC_OR, 1, 0, &lut->m3);
	} else {
		/* Only value/mask trigger. */
		lut->m3 = 0xffff;
	}

	/* Triggertype: event. */
	lut->params.selres = 3;

	return SR_OK;
}

static int hw_dev_acquisition_start(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct context *ctx;
	struct sr_datafeed_packet *packet;
	struct sr_datafeed_header *header;
	struct sr_datafeed_meta_logic meta;
	struct clockselect_50 clockselect;
	int frac, triggerpin, ret;
	uint8_t triggerselect;
	struct triggerinout triggerinout_conf;
	struct triggerlut lut;

	ctx = sdi->priv;

	/* If the samplerate has not been set, default to 200 kHz. */
	if (ctx->cur_firmware == -1) {
		if ((ret = set_samplerate(sdi, SR_KHZ(200))) != SR_OK)
			return ret;
	}

	/* Enter trigger programming mode. */
	sigma_set_register(WRITE_TRIGGER_SELECT1, 0x20, ctx);

	/* 100 and 200 MHz mode. */
	if (ctx->cur_samplerate >= SR_MHZ(100)) {
		sigma_set_register(WRITE_TRIGGER_SELECT1, 0x81, ctx);

		/* Find which pin to trigger on from mask. */
		for (triggerpin = 0; triggerpin < 8; ++triggerpin)
			if ((ctx->trigger.risingmask | ctx->trigger.fallingmask) &
			    (1 << triggerpin))
				break;

		/* Set trigger pin and light LED on trigger. */
		triggerselect = (1 << LEDSEL1) | (triggerpin & 0x7);

		/* Default rising edge. */
		if (ctx->trigger.fallingmask)
			triggerselect |= 1 << 3;

	/* All other modes. */
	} else if (ctx->cur_samplerate <= SR_MHZ(50)) {
		build_basic_trigger(&lut, ctx);

		sigma_write_trigger_lut(&lut, ctx);

		triggerselect = (1 << LEDSEL1) | (1 << LEDSEL0);
	}

	/* Setup trigger in and out pins to default values. */
	memset(&triggerinout_conf, 0, sizeof(struct triggerinout));
	triggerinout_conf.trgout_bytrigger = 1;
	triggerinout_conf.trgout_enable = 1;

	sigma_write_register(WRITE_TRIGGER_OPTION,
			     (uint8_t *) &triggerinout_conf,
			     sizeof(struct triggerinout), ctx);

	/* Go back to normal mode. */
	sigma_set_register(WRITE_TRIGGER_SELECT1, triggerselect, ctx);

	/* Set clock select register. */
	if (ctx->cur_samplerate == SR_MHZ(200))
		/* Enable 4 probes. */
		sigma_set_register(WRITE_CLOCK_SELECT, 0xf0, ctx);
	else if (ctx->cur_samplerate == SR_MHZ(100))
		/* Enable 8 probes. */
		sigma_set_register(WRITE_CLOCK_SELECT, 0x00, ctx);
	else {
		/*
		 * 50 MHz mode (or fraction thereof). Any fraction down to
		 * 50 MHz / 256 can be used, but is not supported by sigrok API.
		 */
		frac = SR_MHZ(50) / ctx->cur_samplerate - 1;

		clockselect.async = 0;
		clockselect.fraction = frac;
		clockselect.disabled_probes = 0;

		sigma_write_register(WRITE_CLOCK_SELECT,
				     (uint8_t *) &clockselect,
				     sizeof(clockselect), ctx);
	}

	/* Setup maximum post trigger time. */
	sigma_set_register(WRITE_POST_TRIGGER,
			   (ctx->capture_ratio * 255) / 100, ctx);

	/* Start acqusition. */
	gettimeofday(&ctx->start_tv, 0);
	sigma_set_register(WRITE_MODE, 0x0d, ctx);

	ctx->session_dev_id = cb_data;

	if (!(packet = g_try_malloc(sizeof(struct sr_datafeed_packet)))) {
		sr_err("sigma: %s: packet malloc failed.", __func__);
		return SR_ERR_MALLOC;
	}

	if (!(header = g_try_malloc(sizeof(struct sr_datafeed_header)))) {
		sr_err("sigma: %s: header malloc failed.", __func__);
		return SR_ERR_MALLOC;
	}

	/* Send header packet to the session bus. */
	packet->type = SR_DF_HEADER;
	packet->payload = header;
	header->feed_version = 1;
	gettimeofday(&header->starttime, NULL);
	sr_session_send(ctx->session_dev_id, packet);

	/* Send metadata about the SR_DF_LOGIC packets to come. */
	packet->type = SR_DF_META_LOGIC;
	packet->payload = &meta;
	meta.samplerate = ctx->cur_samplerate;
	meta.num_probes = ctx->num_probes;
	sr_session_send(ctx->session_dev_id, packet);

	/* Add capture source. */
	sr_source_add(0, G_IO_IN, 10, receive_data, (void *)sdi);

	g_free(header);
	g_free(packet);

	ctx->state.state = SIGMA_CAPTURE;

	return SR_OK;
}

static int hw_dev_acquisition_stop(const struct sr_dev_inst *sdi,
		void *cb_data)
{
	struct context *ctx;
	uint8_t modestatus;

	/* Avoid compiler warnings. */
	(void)cb_data;

	if (!(ctx = sdi->priv)) {
		sr_err("sigma: %s: sdi->priv was NULL", __func__);
		return SR_ERR_BUG;
	}

	/* Stop acquisition. */
	sigma_set_register(WRITE_MODE, 0x11, ctx);

	/* Set SDRAM Read Enable. */
	sigma_set_register(WRITE_MODE, 0x02, ctx);

	/* Get the current position. */
	sigma_read_pos(&ctx->state.stoppos, &ctx->state.triggerpos, ctx);

	/* Check if trigger has fired. */
	modestatus = sigma_get_register(READ_MODE, ctx);
	if (modestatus & 0x20)
		ctx->state.triggerchunk = ctx->state.triggerpos / 512;
	else
		ctx->state.triggerchunk = -1;

	ctx->state.chunks_downloaded = 0;

	ctx->state.state = SIGMA_DOWNLOAD;

	return SR_OK;
}

SR_PRIV struct sr_dev_driver asix_sigma_driver_info = {
	.name = "asix-sigma",
	.longname = "ASIX SIGMA/SIGMA2",
	.api_version = 1,
	.init = hw_init,
	.cleanup = hw_cleanup,
	.scan = hw_scan,
	.dev_open = hw_dev_open,
	.dev_close = hw_dev_close,
	.info_get = hw_info_get,
	.dev_config_set = hw_dev_config_set,
	.dev_acquisition_start = hw_dev_acquisition_start,
	.dev_acquisition_stop = hw_dev_acquisition_stop,
	.instances = NULL,
};
