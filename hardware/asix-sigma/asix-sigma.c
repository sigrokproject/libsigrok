/*
 * This file is part of the libsigrok project.
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

SR_PRIV struct sr_dev_driver asix_sigma_driver_info;
static struct sr_dev_driver *di = &asix_sigma_driver_info;
static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data);

/*
 * The ASIX Sigma supports arbitrary integer frequency divider in
 * the 50MHz mode. The divider is in range 1...256 , allowing for
 * very precise sampling rate selection. This driver supports only
 * a subset of the sampling rates.
 */
static const uint64_t samplerates[] = {
	SR_KHZ(200),	/* div=250 */
	SR_KHZ(250),	/* div=200 */
	SR_KHZ(500),	/* div=100 */
	SR_MHZ(1),	/* div=50  */
	SR_MHZ(5),	/* div=10  */
	SR_MHZ(10),	/* div=5   */
	SR_MHZ(25),	/* div=2   */
	SR_MHZ(50),	/* div=1   */
	SR_MHZ(100),	/* Special FW needed */
	SR_MHZ(200),	/* Special FW needed */
};

/*
 * Channel numbers seem to go from 1-16, according to this image:
 * http://tools.asix.net/img/sigma_sigmacab_pins_720.jpg
 * (the cable has two additional GND pins, and a TI and TO pin)
 */
static const char *channel_names[] = {
	"1", "2", "3", "4", "5", "6", "7", "8",
	"9", "10", "11", "12", "13", "14", "15", "16",
};

static const int32_t hwcaps[] = {
	SR_CONF_LOGIC_ANALYZER,
	SR_CONF_SAMPLERATE,
	SR_CONF_TRIGGER_MATCH,
	SR_CONF_CAPTURE_RATIO,
	SR_CONF_LIMIT_MSEC,
};

static const int32_t trigger_matches[] = {
	SR_TRIGGER_ZERO,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
};

static const char *sigma_firmware_files[] = {
	/* 50 MHz, supports 8 bit fractions */
	FIRMWARE_DIR "/asix-sigma-50.fw",
	/* 100 MHz */
	FIRMWARE_DIR "/asix-sigma-100.fw",
	/* 200 MHz */
	FIRMWARE_DIR "/asix-sigma-200.fw",
	/* Synchronous clock from pin */
	FIRMWARE_DIR "/asix-sigma-50sync.fw",
	/* Frequency counter */
	FIRMWARE_DIR "/asix-sigma-phasor.fw",
};

static int sigma_read(void *buf, size_t size, struct dev_context *devc)
{
	int ret;

	ret = ftdi_read_data(&devc->ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		sr_err("ftdi_read_data failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
	}

	return ret;
}

static int sigma_write(void *buf, size_t size, struct dev_context *devc)
{
	int ret;

	ret = ftdi_write_data(&devc->ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		sr_err("ftdi_write_data failed: %s",
		       ftdi_get_error_string(&devc->ftdic));
	} else if ((size_t) ret != size) {
		sr_err("ftdi_write_data did not complete write.");
	}

	return ret;
}

static int sigma_write_register(uint8_t reg, uint8_t *data, size_t len,
				struct dev_context *devc)
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

	return sigma_write(buf, idx, devc);
}

static int sigma_set_register(uint8_t reg, uint8_t value, struct dev_context *devc)
{
	return sigma_write_register(reg, &value, 1, devc);
}

static int sigma_read_register(uint8_t reg, uint8_t *data, size_t len,
			       struct dev_context *devc)
{
	uint8_t buf[3];

	buf[0] = REG_ADDR_LOW | (reg & 0xf);
	buf[1] = REG_ADDR_HIGH | (reg >> 4);
	buf[2] = REG_READ_ADDR;

	sigma_write(buf, sizeof(buf), devc);

	return sigma_read(data, len, devc);
}

static uint8_t sigma_get_register(uint8_t reg, struct dev_context *devc)
{
	uint8_t value;

	if (1 != sigma_read_register(reg, &value, 1, devc)) {
		sr_err("sigma_get_register: 1 byte expected");
		return 0;
	}

	return value;
}

static int sigma_read_pos(uint32_t *stoppos, uint32_t *triggerpos,
			  struct dev_context *devc)
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

	sigma_write(buf, sizeof(buf), devc);

	sigma_read(result, sizeof(result), devc);

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
			   uint8_t *data, struct dev_context *devc)
{
	size_t i;
	uint8_t buf[4096];
	int idx = 0;

	/* Send the startchunk. Index start with 1. */
	buf[0] = startchunk >> 8;
	buf[1] = startchunk & 0xff;
	sigma_write_register(WRITE_MEMROW, buf, 2, devc);

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

	sigma_write(buf, idx, devc);

	return sigma_read(data, numchunks * CHUNK_SIZE, devc);
}

/* Upload trigger look-up tables to Sigma. */
static int sigma_write_trigger_lut(struct triggerlut *lut, struct dev_context *devc)
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
				     devc);
		sigma_set_register(WRITE_TRIGGER_SELECT1, 0x30 | i, devc);
	}

	/* Send the parameters */
	sigma_write_register(WRITE_TRIGGER_SELECT0, (uint8_t *) &lut->params,
			     sizeof(lut->params), devc);

	return SR_OK;
}

static void clear_helper(void *priv)
{
	struct dev_context *devc;

	devc = priv;

	ftdi_deinit(&devc->ftdic);
}

static int dev_clear(void)
{
	return std_dev_clear(di, clear_helper);
}

static int init(struct sr_context *sr_ctx)
{
	return std_init(sr_ctx, di, LOG_PREFIX);
}

static GSList *scan(GSList *options)
{
	struct sr_dev_inst *sdi;
	struct sr_channel *ch;
	struct drv_context *drvc;
	struct dev_context *devc;
	GSList *devices;
	struct ftdi_device_list *devlist;
	char serial_txt[10];
	uint32_t serial;
	int ret;
	unsigned int i;

	(void)options;

	drvc = di->priv;

	devices = NULL;

	if (!(devc = g_try_malloc(sizeof(struct dev_context)))) {
		sr_err("%s: devc malloc failed", __func__);
		return NULL;
	}

	ftdi_init(&devc->ftdic);

	/* Look for SIGMAs. */

	if ((ret = ftdi_usb_find_all(&devc->ftdic, &devlist,
	    USB_VENDOR, USB_PRODUCT)) <= 0) {
		if (ret < 0)
			sr_err("ftdi_usb_find_all(): %d", ret);
		goto free;
	}

	/* Make sure it's a version 1 or 2 SIGMA. */
	ftdi_usb_get_strings(&devc->ftdic, devlist->dev, NULL, 0, NULL, 0,
			     serial_txt, sizeof(serial_txt));
	sscanf(serial_txt, "%x", &serial);

	if (serial < 0xa6010000 || serial > 0xa602ffff) {
		sr_err("Only SIGMA and SIGMA2 are supported "
		       "in this version of libsigrok.");
		goto free;
	}

	sr_info("Found ASIX SIGMA - Serial: %s", serial_txt);

	devc->cur_samplerate = samplerates[0];
	devc->period_ps = 0;
	devc->limit_msec = 0;
	devc->cur_firmware = -1;
	devc->num_channels = 0;
	devc->samples_per_event = 0;
	devc->capture_ratio = 50;
	devc->use_triggers = 0;

	/* Register SIGMA device. */
	if (!(sdi = sr_dev_inst_new(0, SR_ST_INITIALIZING, USB_VENDOR_NAME,
				    USB_MODEL_NAME, NULL))) {
		sr_err("%s: sdi was NULL", __func__);
		goto free;
	}
	sdi->driver = di;

	for (i = 0; i < ARRAY_SIZE(channel_names); i++) {
		ch = sr_channel_new(i, SR_CHANNEL_LOGIC, TRUE,
				    channel_names[i]);
		if (!ch)
			return NULL;
		sdi->channels = g_slist_append(sdi->channels, ch);
	}

	devices = g_slist_append(devices, sdi);
	drvc->instances = g_slist_append(drvc->instances, sdi);
	sdi->priv = devc;

	/* We will open the device again when we need it. */
	ftdi_list_free(&devlist);

	return devices;

free:
	ftdi_deinit(&devc->ftdic);
	g_free(devc);
	return NULL;
}

static GSList *dev_list(void)
{
	return ((struct drv_context *)(di->priv))->instances;
}

/*
 * Configure the FPGA for bitbang mode.
 * This sequence is documented in section 2. of the ASIX Sigma programming
 * manual. This sequence is necessary to configure the FPGA in the Sigma
 * into Bitbang mode, in which it can be programmed with the firmware.
 */
static int sigma_fpga_init_bitbang(struct dev_context *devc)
{
	uint8_t suicide[] = {
		0x84, 0x84, 0x88, 0x84, 0x88, 0x84, 0x88, 0x84,
	};
	uint8_t init_array[] = {
		0x01, 0x03, 0x03, 0x01, 0x01, 0x01, 0x01, 0x01,
		0x01, 0x01,
	};
	int i, ret, timeout = 10000;
	uint8_t data;

	/* Section 2. part 1), do the FPGA suicide. */
	sigma_write(suicide, sizeof(suicide), devc);
	sigma_write(suicide, sizeof(suicide), devc);
	sigma_write(suicide, sizeof(suicide), devc);
	sigma_write(suicide, sizeof(suicide), devc);

	/* Section 2. part 2), do pulse on D1. */
	sigma_write(init_array, sizeof(init_array), devc);
	ftdi_usb_purge_buffers(&devc->ftdic);

	/* Wait until the FPGA asserts D6/INIT_B. */
	for (i = 0; i < timeout; i++) {
		ret = sigma_read(&data, 1, devc);
		if (ret < 0)
			return ret;
		/* Test if pin D6 got asserted. */
		if (data & (1 << 5))
			return 0;
		/* The D6 was not asserted yet, wait a bit. */
		usleep(10000);
	}

	return SR_ERR_TIMEOUT;
}

/*
 * Configure the FPGA for logic-analyzer mode.
 */
static int sigma_fpga_init_la(struct dev_context *devc)
{
	/* Initialize the logic analyzer mode. */
	uint8_t logic_mode_start[] = {
		REG_ADDR_LOW  | (READ_ID & 0xf),
		REG_ADDR_HIGH | (READ_ID >> 8),
		REG_READ_ADDR,	/* Read ID register. */

		REG_ADDR_LOW | (WRITE_TEST & 0xf),
		REG_DATA_LOW | 0x5,
		REG_DATA_HIGH_WRITE | 0x5,
		REG_READ_ADDR,	/* Read scratch register. */

		REG_DATA_LOW | 0xa,
		REG_DATA_HIGH_WRITE | 0xa,
		REG_READ_ADDR,	/* Read scratch register. */

		REG_ADDR_LOW | (WRITE_MODE & 0xf),
		REG_DATA_LOW | 0x0,
		REG_DATA_HIGH_WRITE | 0x8,
	};

	uint8_t result[3];
	int ret;

	/* Initialize the logic analyzer mode. */
	sigma_write(logic_mode_start, sizeof(logic_mode_start), devc);

	/* Expect a 3 byte reply since we issued three READ requests. */
	ret = sigma_read(result, 3, devc);
	if (ret != 3)
		goto err;

	if (result[0] != 0xa6 || result[1] != 0x55 || result[2] != 0xaa)
		goto err;

	return SR_OK;
err:
	sr_err("Configuration failed. Invalid reply received.");
	return SR_ERR;
}

/*
 * Read the firmware from a file and transform it into a series of bitbang
 * pulses used to program the FPGA. Note that the *bb_cmd must be free()'d
 * by the caller of this function.
 */
static int sigma_fw_2_bitbang(const char *filename,
			      uint8_t **bb_cmd, gsize *bb_cmd_size)
{
	GMappedFile *file;
	GError *error;
	gsize i, file_size, bb_size;
	gchar *firmware;
	uint8_t *bb_stream, *bbs;
	uint32_t imm;
	int bit, v;
	int ret = SR_OK;

	/*
	 * Map the file and make the mapped buffer writable.
	 * NOTE: Using writable=TRUE does _NOT_ mean that file that is mapped
	 *       will be modified. It will not be modified until someone uses
	 *       g_file_set_contents() on it.
	 */
	error = NULL;
	file = g_mapped_file_new(filename, TRUE, &error);
	g_assert_no_error(error);

	file_size = g_mapped_file_get_length(file);
	firmware = g_mapped_file_get_contents(file);
	g_assert(firmware);

	/* Weird magic transformation below, I have no idea what it does. */
	imm = 0x3f6df2ab;
	for (i = 0; i < file_size; i++) {
		imm = (imm + 0xa853753) % 177 + (imm * 0x8034052);
		firmware[i] ^= imm & 0xff;
	}

	/*
	 * Now that the firmware is "transformed", we will transcribe the
	 * firmware blob into a sequence of toggles of the Dx wires. This
	 * sequence will be fed directly into the Sigma, which must be in
	 * the FPGA bitbang programming mode.
	 */

	/* Each bit of firmware is transcribed as two toggles of Dx wires. */
	bb_size = file_size * 8 * 2;
	bb_stream = (uint8_t *)g_try_malloc(bb_size);
	if (!bb_stream) {
		sr_err("%s: Failed to allocate bitbang stream", __func__);
		ret = SR_ERR_MALLOC;
		goto exit;
	}

	bbs = bb_stream;
	for (i = 0; i < file_size; i++) {
		for (bit = 7; bit >= 0; bit--) {
			v = (firmware[i] & (1 << bit)) ? 0x40 : 0x00;
			*bbs++ = v | 0x01;
			*bbs++ = v;
		}
	}

	/* The transformation completed successfully, return the result. */
	*bb_cmd = bb_stream;
	*bb_cmd_size = bb_size;

exit:
	g_mapped_file_unref(file);
	return ret;
}

static int upload_firmware(int firmware_idx, struct dev_context *devc)
{
	int ret;
	unsigned char *buf;
	unsigned char pins;
	size_t buf_size;
	const char *firmware = sigma_firmware_files[firmware_idx];
	struct ftdi_context *ftdic = &devc->ftdic;

	/* Make sure it's an ASIX SIGMA. */
	ret = ftdi_usb_open_desc(ftdic, USB_VENDOR, USB_PRODUCT,
				 USB_DESCRIPTION, NULL);
	if (ret < 0) {
		sr_err("ftdi_usb_open failed: %s",
		       ftdi_get_error_string(ftdic));
		return 0;
	}

	ret = ftdi_set_bitmode(ftdic, 0xdf, BITMODE_BITBANG);
	if (ret < 0) {
		sr_err("ftdi_set_bitmode failed: %s",
		       ftdi_get_error_string(ftdic));
		return 0;
	}

	/* Four times the speed of sigmalogan - Works well. */
	ret = ftdi_set_baudrate(ftdic, 750000);
	if (ret < 0) {
		sr_err("ftdi_set_baudrate failed: %s",
		       ftdi_get_error_string(ftdic));
		return 0;
	}

	/* Initialize the FPGA for firmware upload. */
	ret = sigma_fpga_init_bitbang(devc);
	if (ret)
		return ret;

	/* Prepare firmware. */
	ret = sigma_fw_2_bitbang(firmware, &buf, &buf_size);
	if (ret != SR_OK) {
		sr_err("An error occured while reading the firmware: %s",
		       firmware);
		return ret;
	}

	/* Upload firmare. */
	sr_info("Uploading firmware file '%s'.", firmware);
	sigma_write(buf, buf_size, devc);

	g_free(buf);

	ret = ftdi_set_bitmode(ftdic, 0x00, BITMODE_RESET);
	if (ret < 0) {
		sr_err("ftdi_set_bitmode failed: %s",
		       ftdi_get_error_string(ftdic));
		return SR_ERR;
	}

	ftdi_usb_purge_buffers(ftdic);

	/* Discard garbage. */
	while (sigma_read(&pins, 1, devc) == 1)
		;

	/* Initialize the FPGA for logic-analyzer mode. */
	ret = sigma_fpga_init_la(devc);
	if (ret != SR_OK)
		return ret;

	devc->cur_firmware = firmware_idx;

	sr_info("Firmware uploaded.");

	return SR_OK;
}

static int dev_open(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	/* Make sure it's an ASIX SIGMA. */
	if ((ret = ftdi_usb_open_desc(&devc->ftdic,
		USB_VENDOR, USB_PRODUCT, USB_DESCRIPTION, NULL)) < 0) {

		sr_err("ftdi_usb_open failed: %s",
		       ftdi_get_error_string(&devc->ftdic));

		return 0;
	}

	sdi->status = SR_ST_ACTIVE;

	return SR_OK;
}

static int set_samplerate(const struct sr_dev_inst *sdi, uint64_t samplerate)
{
	struct dev_context *devc;
	unsigned int i;
	int ret;

	devc = sdi->priv;
	ret = SR_OK;

	for (i = 0; i < ARRAY_SIZE(samplerates); i++) {
		if (samplerates[i] == samplerate)
			break;
	}
	if (samplerates[i] == 0)
		return SR_ERR_SAMPLERATE;

	if (samplerate <= SR_MHZ(50)) {
		ret = upload_firmware(0, devc);
		devc->num_channels = 16;
	} else if (samplerate == SR_MHZ(100)) {
		ret = upload_firmware(1, devc);
		devc->num_channels = 8;
	} else if (samplerate == SR_MHZ(200)) {
		ret = upload_firmware(2, devc);
		devc->num_channels = 4;
	}

	if (ret == SR_OK) {
		devc->cur_samplerate = samplerate;
		devc->period_ps = 1000000000000ULL / samplerate;
		devc->samples_per_event = 16 / devc->num_channels;
		devc->state.state = SIGMA_IDLE;
	}

	return ret;
}

/*
 * In 100 and 200 MHz mode, only a single pin rising/falling can be
 * set as trigger. In other modes, two rising/falling triggers can be set,
 * in addition to value/mask trigger for any number of channels.
 *
 * The Sigma supports complex triggers using boolean expressions, but this
 * has not been implemented yet.
 */
static int convert_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	const GSList *l, *m;
	int channelbit, trigger_set;

	devc = sdi->priv;
	memset(&devc->trigger, 0, sizeof(struct sigma_trigger));
	if (!(trigger = sr_session_trigger_get()))
		return SR_OK;

	trigger_set = 0;
	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			if (!match->channel->enabled)
				/* Ignore disabled channels with a trigger. */
				continue;
			channelbit = 1 << (match->channel->index);
			if (devc->cur_samplerate >= SR_MHZ(100)) {
				/* Fast trigger support. */
				if (trigger_set) {
					sr_err("Only a single pin trigger is "
							"supported in 100 and 200MHz mode.");
					return SR_ERR;
				}
				if (match->match == SR_TRIGGER_FALLING)
					devc->trigger.fallingmask |= channelbit;
				else if (match->match == SR_TRIGGER_RISING)
					devc->trigger.risingmask |= channelbit;
				else {
					sr_err("Only rising/falling trigger is "
							"supported in 100 and 200MHz mode.");
					return SR_ERR;
				}

				++trigger_set;
			} else {
				/* Simple trigger support (event). */
				if (match->match == SR_TRIGGER_ONE) {
					devc->trigger.simplevalue |= channelbit;
					devc->trigger.simplemask |= channelbit;
				}
				else if (match->match == SR_TRIGGER_ZERO) {
					devc->trigger.simplevalue &= ~channelbit;
					devc->trigger.simplemask |= channelbit;
				}
				else if (match->match == SR_TRIGGER_FALLING) {
					devc->trigger.fallingmask |= channelbit;
					++trigger_set;
				}
				else if (match->match == SR_TRIGGER_RISING) {
					devc->trigger.risingmask |= channelbit;
					++trigger_set;
				}

				/*
				 * Actually, Sigma supports 2 rising/falling triggers,
				 * but they are ORed and the current trigger syntax
				 * does not permit ORed triggers.
				 */
				if (trigger_set > 1) {
					sr_err("Only 1 rising/falling trigger "
						   "is supported.");
					return SR_ERR;
				}
			}
		}
	}


	return SR_OK;
}

static int dev_close(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	/* TODO */
	if (sdi->status == SR_ST_ACTIVE)
		ftdi_usb_close(&devc->ftdic);

	sdi->status = SR_ST_INACTIVE;

	return SR_OK;
}

static int cleanup(void)
{
	return dev_clear();
}

static int config_get(int id, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;

	(void)cg;

	if (!sdi)
		return SR_ERR;
	devc = sdi->priv;

	switch (id) {
	case SR_CONF_SAMPLERATE:
		*data = g_variant_new_uint64(devc->cur_samplerate);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(devc->limit_msec);
		break;
	case SR_CONF_CAPTURE_RATIO:
		*data = g_variant_new_uint64(devc->capture_ratio);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

static int config_set(int id, GVariant *data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	struct dev_context *devc;
	uint64_t tmp;
	int ret;

	(void)cg;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	ret = SR_OK;
	switch (id) {
	case SR_CONF_SAMPLERATE:
		ret = set_samplerate(sdi, g_variant_get_uint64(data));
		break;
	case SR_CONF_LIMIT_MSEC:
		tmp = g_variant_get_uint64(data);
		if (tmp > 0)
			devc->limit_msec = g_variant_get_uint64(data);
		else
			ret = SR_ERR;
		break;
	case SR_CONF_LIMIT_SAMPLES:
		tmp = g_variant_get_uint64(data);
		devc->limit_msec = tmp * 1000 / devc->cur_samplerate;
		break;
	case SR_CONF_CAPTURE_RATIO:
		tmp = g_variant_get_uint64(data);
		if (tmp <= 100)
			devc->capture_ratio = tmp;
		else
			ret = SR_ERR;
		break;
	default:
		ret = SR_ERR_NA;
	}

	return ret;
}

static int config_list(int key, GVariant **data, const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg)
{
	GVariant *gvar;
	GVariantBuilder gvb;

	(void)sdi;
	(void)cg;

	switch (key) {
	case SR_CONF_DEVICE_OPTIONS:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				hwcaps, ARRAY_SIZE(hwcaps), sizeof(int32_t));
		break;
	case SR_CONF_SAMPLERATE:
		g_variant_builder_init(&gvb, G_VARIANT_TYPE("a{sv}"));
		gvar = g_variant_new_fixed_array(G_VARIANT_TYPE("t"), samplerates,
				ARRAY_SIZE(samplerates), sizeof(uint64_t));
		g_variant_builder_add(&gvb, "{sv}", "samplerates", gvar);
		*data = g_variant_builder_end(&gvb);
		break;
	case SR_CONF_TRIGGER_MATCH:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_INT32,
				trigger_matches, ARRAY_SIZE(trigger_matches),
				sizeof(int32_t));
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

/* Software trigger to determine exact trigger position. */
static int get_trigger_offset(uint8_t *samples, uint16_t last_sample,
			      struct sigma_trigger *t)
{
	int i;
	uint16_t sample = 0;

	for (i = 0; i < 8; ++i) {
		if (i > 0)
			last_sample = sample;
		sample = samples[2 * i] | (samples[2 * i + 1] << 8);

		/* Simple triggers. */
		if ((sample & t->simplemask) != t->simplevalue)
			continue;

		/* Rising edge. */
		if (((last_sample & t->risingmask) != 0) ||
		    ((sample & t->risingmask) != t->risingmask))
			continue;

		/* Falling edge. */
		if ((last_sample & t->fallingmask) != t->fallingmask ||
		    (sample & t->fallingmask) != 0)
			continue;

		break;
	}

	/* If we did not match, return original trigger pos. */
	return i & 0x7;
}


/*
 * Return the timestamp of "DRAM cluster".
 */
static uint16_t sigma_dram_cluster_ts(struct sigma_dram_cluster *cluster)
{
	return (cluster->timestamp_hi << 8) | cluster->timestamp_lo;
}

static void sigma_decode_dram_cluster(struct sigma_dram_cluster *dram_cluster,
				      unsigned int events_in_cluster,
				      unsigned int triggered,
				      struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sigma_state *ss = &devc->state;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint16_t tsdiff, ts;
	uint8_t samples[2048];
	unsigned int i;

	ts = sigma_dram_cluster_ts(dram_cluster);
	tsdiff = ts - ss->lastts;
	ss->lastts = ts;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = 2;
	logic.data = samples;

	/*
	 * First of all, send Sigrok a copy of the last sample from
	 * previous cluster as many times as needed to make up for
	 * the differential characteristics of data we get from the
	 * Sigma. Sigrok needs one sample of data per period.
	 *
	 * One DRAM cluster contains a timestamp and seven samples,
	 * the units of timestamp are "devc->period_ps" , the first
	 * sample in the cluster happens at the time of the timestamp
	 * and the remaining samples happen at timestamp +1...+6 .
	 */
	for (ts = 0; ts < tsdiff - (EVENTS_PER_CLUSTER - 1); ts++) {
		i = ts % 1024;
		samples[2 * i + 0] = ss->lastsample & 0xff;
		samples[2 * i + 1] = ss->lastsample >> 8;

		/*
		 * If we have 1024 samples ready or we're at the
		 * end of submitting the padding samples, submit
		 * the packet to Sigrok.
		 */
		if ((i == 1023) || (ts == (tsdiff - EVENTS_PER_CLUSTER))) {
			logic.length = (i + 1) * logic.unitsize;
			sr_session_send(devc->cb_data, &packet);
		}
	}

	/*
	 * Parse the samples in current cluster and prepare them
	 * to be submitted to Sigrok.
	 */
	for (i = 0; i < events_in_cluster; i++) {
		samples[2 * i + 1] = dram_cluster->samples[i].sample_lo;
		samples[2 * i + 0] = dram_cluster->samples[i].sample_hi;
	}

	/* Send data up to trigger point (if triggered). */
	int trigger_offset = 0;
	if (triggered) {
		/*
		 * Trigger is not always accurate to sample because of
		 * pipeline delay. However, it always triggers before
		 * the actual event. We therefore look at the next
		 * samples to pinpoint the exact position of the trigger.
		 */
		trigger_offset = get_trigger_offset(samples,
					ss->lastsample, &devc->trigger);

		if (trigger_offset > 0) {
			packet.type = SR_DF_LOGIC;
			logic.length = trigger_offset * logic.unitsize;
			sr_session_send(devc->cb_data, &packet);
			events_in_cluster -= trigger_offset;
		}

		/* Only send trigger if explicitly enabled. */
		if (devc->use_triggers) {
			packet.type = SR_DF_TRIGGER;
			sr_session_send(devc->cb_data, &packet);
		}
	}

	if (events_in_cluster > 0) {
		packet.type = SR_DF_LOGIC;
		logic.length = events_in_cluster * logic.unitsize;
		logic.data = samples + (trigger_offset * logic.unitsize);
		sr_session_send(devc->cb_data, &packet);
	}

	ss->lastsample =
		samples[2 * (events_in_cluster - 1) + 0] |
		(samples[2 * (events_in_cluster - 1) + 1] << 8);

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
static int decode_chunk_ts(struct sigma_dram_line *dram_line,
			   uint16_t events_in_line,
			   uint32_t trigger_event,
			   void *cb_data)
{
	struct sigma_dram_cluster *dram_cluster;
	struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;
	unsigned int clusters_in_line =
		(events_in_line + (EVENTS_PER_CLUSTER - 1)) / EVENTS_PER_CLUSTER;
	unsigned int events_in_cluster;
	unsigned int i;
	uint32_t trigger_cluster = ~0, triggered = 0;

	/* Check if trigger is in this chunk. */
	if (trigger_event < (64 * 7)) {
		if (devc->cur_samplerate <= SR_MHZ(50)) {
			trigger_event -= MIN(EVENTS_PER_CLUSTER - 1,
					     trigger_event);
		}

		/* Find in which cluster the trigger occured. */
		trigger_cluster = trigger_event / EVENTS_PER_CLUSTER;
	}

	/* For each full DRAM cluster. */
	for (i = 0; i < clusters_in_line; i++) {
		dram_cluster = &dram_line->cluster[i];

		/* The last cluster might not be full. */
		if ((i == clusters_in_line - 1) &&
		    (events_in_line % EVENTS_PER_CLUSTER)) {
			events_in_cluster = events_in_line % EVENTS_PER_CLUSTER;
		} else {
			events_in_cluster = EVENTS_PER_CLUSTER;
		}

		triggered = (i == trigger_cluster);
		sigma_decode_dram_cluster(dram_cluster, events_in_cluster,
					  triggered, sdi);
	}

	return SR_OK;
}

static int download_capture(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	const int chunks_per_read = 32;
	struct sigma_dram_line *dram_line;
	int bufsz;
	uint32_t stoppos, triggerpos;
	struct sr_datafeed_packet packet;
	uint8_t modestatus;

	uint32_t i;
	uint32_t dl_lines_total, dl_lines_curr, dl_lines_done;
	uint32_t dl_events_in_line = 64 * 7;
	uint32_t trg_line = ~0, trg_event = ~0;

	dram_line = g_try_malloc0(chunks_per_read * sizeof(*dram_line));
	if (!dram_line)
		return FALSE;

	sr_info("Downloading sample data.");

	/* Stop acquisition. */
	sigma_set_register(WRITE_MODE, 0x11, devc);

	/* Set SDRAM Read Enable. */
	sigma_set_register(WRITE_MODE, 0x02, devc);

	/* Get the current position. */
	sigma_read_pos(&stoppos, &triggerpos, devc);

	/* Check if trigger has fired. */
	modestatus = sigma_get_register(READ_MODE, devc);
	if (modestatus & 0x20) {
		trg_line = triggerpos >> 9;
		trg_event = triggerpos & 0x1ff;
	}

	/*
	 * Determine how many 1024b "DRAM lines" do we need to read from the
	 * Sigma so we have a complete set of samples. Note that the last
	 * line can be only partial, containing less than 64 clusters.
	 */
	dl_lines_total = (stoppos >> 9) + 1;

	dl_lines_done = 0;

	while (dl_lines_total > dl_lines_done) {
		/* We can download only up-to 32 DRAM lines in one go! */
		dl_lines_curr = MIN(chunks_per_read, dl_lines_total);

		bufsz = sigma_read_dram(dl_lines_done, dl_lines_curr,
					(uint8_t *)dram_line, devc);
		/* TODO: Check bufsz. For now, just avoid compiler warnings. */
		(void)bufsz;

		/* This is the first DRAM line, so find the initial timestamp. */
		if (dl_lines_done == 0) {
			devc->state.lastts =
				sigma_dram_cluster_ts(&dram_line[0].cluster[0]);
			devc->state.lastsample = 0;
		}

		for (i = 0; i < dl_lines_curr; i++) {
			uint32_t trigger_event = ~0;
			/* The last "DRAM line" can be only partially full. */
			if (dl_lines_done + i == dl_lines_total - 1)
				dl_events_in_line = stoppos & 0x1ff;

			/* Test if the trigger happened on this line. */
			if (dl_lines_done + i == trg_line)
				trigger_event = trg_event;

			decode_chunk_ts(dram_line + i, dl_events_in_line,
					trigger_event, sdi);
		}

		dl_lines_done += dl_lines_curr;
	}

	/* All done. */
	packet.type = SR_DF_END;
	sr_session_send(sdi, &packet);

	dev_acquisition_stop(sdi, sdi);

	g_free(dram_line);

	return TRUE;
}

/*
 * Handle the Sigma when in CAPTURE mode. This function checks:
 * - Sampling time ended
 * - DRAM capacity overflow
 * This function triggers download of the samples from Sigma
 * in case either of the above conditions is true.
 */
static int sigma_capture_mode(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	uint64_t running_msec;
	struct timeval tv;

	uint32_t stoppos, triggerpos;

	/* Check if the selected sampling duration passed. */
	gettimeofday(&tv, 0);
	running_msec = (tv.tv_sec - devc->start_tv.tv_sec) * 1000 +
		       (tv.tv_usec - devc->start_tv.tv_usec) / 1000;
	if (running_msec >= devc->limit_msec)
		return download_capture(sdi);

	/* Get the position in DRAM to which the FPGA is writing now. */
	sigma_read_pos(&stoppos, &triggerpos, devc);
	/* Test if DRAM is full and if so, download the data. */
	if ((stoppos >> 9) == 32767)
		return download_capture(sdi);

	return TRUE;
}

static int receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;

	if (devc->state.state == SIGMA_IDLE)
		return TRUE;

	if (devc->state.state == SIGMA_CAPTURE)
		return sigma_capture_mode(sdi);

	return TRUE;
}

/* Build a LUT entry used by the trigger functions. */
static void build_lut_entry(uint16_t value, uint16_t mask, uint16_t *entry)
{
	int i, j, k, bit;

	/* For each quad channel. */
	for (i = 0; i < 4; ++i) {
		entry[i] = 0xffff;

		/* For each bit in LUT. */
		for (j = 0; j < 16; ++j)

			/* For each channel in quad. */
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
static int build_basic_trigger(struct triggerlut *lut, struct dev_context *devc)
{
	int i,j;
	uint16_t masks[2] = { 0, 0 };

	memset(lut, 0, sizeof(struct triggerlut));

	/* Contant for simple triggers. */
	lut->m4 = 0xa000;

	/* Value/mask trigger support. */
	build_lut_entry(devc->trigger.simplevalue, devc->trigger.simplemask,
			lut->m2d);

	/* Rise/fall trigger support. */
	for (i = 0, j = 0; i < 16; ++i) {
		if (devc->trigger.risingmask & (1 << i) ||
		    devc->trigger.fallingmask & (1 << i))
			masks[j++] = 1 << i;
	}

	build_lut_entry(masks[0], masks[0], lut->m0d);
	build_lut_entry(masks[1], masks[1], lut->m1d);

	/* Add glue logic */
	if (masks[0] || masks[1]) {
		/* Transition trigger. */
		if (masks[0] & devc->trigger.risingmask)
			add_trigger_function(OP_RISE, FUNC_OR, 0, 0, &lut->m3);
		if (masks[0] & devc->trigger.fallingmask)
			add_trigger_function(OP_FALL, FUNC_OR, 0, 0, &lut->m3);
		if (masks[1] & devc->trigger.risingmask)
			add_trigger_function(OP_RISE, FUNC_OR, 1, 0, &lut->m3);
		if (masks[1] & devc->trigger.fallingmask)
			add_trigger_function(OP_FALL, FUNC_OR, 1, 0, &lut->m3);
	} else {
		/* Only value/mask trigger. */
		lut->m3 = 0xffff;
	}

	/* Triggertype: event. */
	lut->params.selres = 3;

	return SR_OK;
}

static int dev_acquisition_start(const struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;
	struct clockselect_50 clockselect;
	int frac, triggerpin, ret;
	uint8_t triggerselect = 0;
	struct triggerinout triggerinout_conf;
	struct triggerlut lut;

	if (sdi->status != SR_ST_ACTIVE)
		return SR_ERR_DEV_CLOSED;

	devc = sdi->priv;

	if (convert_trigger(sdi) != SR_OK) {
		sr_err("Failed to configure triggers.");
		return SR_ERR;
	}

	/* If the samplerate has not been set, default to 200 kHz. */
	if (devc->cur_firmware == -1) {
		if ((ret = set_samplerate(sdi, SR_KHZ(200))) != SR_OK)
			return ret;
	}

	/* Enter trigger programming mode. */
	sigma_set_register(WRITE_TRIGGER_SELECT1, 0x20, devc);

	/* 100 and 200 MHz mode. */
	if (devc->cur_samplerate >= SR_MHZ(100)) {
		sigma_set_register(WRITE_TRIGGER_SELECT1, 0x81, devc);

		/* Find which pin to trigger on from mask. */
		for (triggerpin = 0; triggerpin < 8; ++triggerpin)
			if ((devc->trigger.risingmask | devc->trigger.fallingmask) &
			    (1 << triggerpin))
				break;

		/* Set trigger pin and light LED on trigger. */
		triggerselect = (1 << LEDSEL1) | (triggerpin & 0x7);

		/* Default rising edge. */
		if (devc->trigger.fallingmask)
			triggerselect |= 1 << 3;

	/* All other modes. */
	} else if (devc->cur_samplerate <= SR_MHZ(50)) {
		build_basic_trigger(&lut, devc);

		sigma_write_trigger_lut(&lut, devc);

		triggerselect = (1 << LEDSEL1) | (1 << LEDSEL0);
	}

	/* Setup trigger in and out pins to default values. */
	memset(&triggerinout_conf, 0, sizeof(struct triggerinout));
	triggerinout_conf.trgout_bytrigger = 1;
	triggerinout_conf.trgout_enable = 1;

	sigma_write_register(WRITE_TRIGGER_OPTION,
			     (uint8_t *) &triggerinout_conf,
			     sizeof(struct triggerinout), devc);

	/* Go back to normal mode. */
	sigma_set_register(WRITE_TRIGGER_SELECT1, triggerselect, devc);

	/* Set clock select register. */
	if (devc->cur_samplerate == SR_MHZ(200))
		/* Enable 4 channels. */
		sigma_set_register(WRITE_CLOCK_SELECT, 0xf0, devc);
	else if (devc->cur_samplerate == SR_MHZ(100))
		/* Enable 8 channels. */
		sigma_set_register(WRITE_CLOCK_SELECT, 0x00, devc);
	else {
		/*
		 * 50 MHz mode (or fraction thereof). Any fraction down to
		 * 50 MHz / 256 can be used, but is not supported by sigrok API.
		 */
		frac = SR_MHZ(50) / devc->cur_samplerate - 1;

		clockselect.async = 0;
		clockselect.fraction = frac;
		clockselect.disabled_channels = 0;

		sigma_write_register(WRITE_CLOCK_SELECT,
				     (uint8_t *) &clockselect,
				     sizeof(clockselect), devc);
	}

	/* Setup maximum post trigger time. */
	sigma_set_register(WRITE_POST_TRIGGER,
			   (devc->capture_ratio * 255) / 100, devc);

	/* Start acqusition. */
	gettimeofday(&devc->start_tv, 0);
	sigma_set_register(WRITE_MODE, 0x0d, devc);

	devc->cb_data = cb_data;

	/* Send header packet to the session bus. */
	std_session_send_df_header(cb_data, LOG_PREFIX);

	/* Add capture source. */
	sr_source_add(0, G_IO_IN, 10, receive_data, (void *)sdi);

	devc->state.state = SIGMA_CAPTURE;

	return SR_OK;
}

static int dev_acquisition_stop(struct sr_dev_inst *sdi, void *cb_data)
{
	struct dev_context *devc;

	(void)cb_data;

	devc = sdi->priv;
	devc->state.state = SIGMA_IDLE;

	sr_source_remove(0);

	return SR_OK;
}

SR_PRIV struct sr_dev_driver asix_sigma_driver_info = {
	.name = "asix-sigma",
	.longname = "ASIX SIGMA/SIGMA2",
	.api_version = 1,
	.init = init,
	.cleanup = cleanup,
	.scan = scan,
	.dev_list = dev_list,
	.dev_clear = dev_clear,
	.config_get = config_get,
	.config_set = config_set,
	.config_list = config_list,
	.dev_open = dev_open,
	.dev_close = dev_close,
	.dev_acquisition_start = dev_acquisition_start,
	.dev_acquisition_stop = dev_acquisition_stop,
	.priv = NULL,
};
