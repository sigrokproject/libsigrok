/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Håvard Espeland <gus@ping.uio.no>,
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
 * ASIX Sigma Logic Analyzer Driver
 */

#include <ftdi.h>
#include <string.h>
#include <zlib.h>
#include <sigrok.h>
#include <config.h>
#include "asix-sigma.h"

#define USB_VENDOR			0xa600
#define USB_PRODUCT			0xa000
#define USB_DESCRIPTION			"ASIX SIGMA"
#define USB_VENDOR_NAME			"ASIX"
#define USB_MODEL_NAME			"SIGMA"
#define USB_MODEL_VERSION		""
#define TRIGGER_TYPES			"rf10"

static GSList *device_instances = NULL;

static uint64_t supported_samplerates[] = {
	KHZ(200),
	KHZ(250),
	KHZ(500),
	MHZ(1),
	MHZ(5),
	MHZ(10),
	MHZ(25),
	MHZ(50),
	MHZ(100),
	MHZ(200),
	0,
};

static struct samplerates samplerates = {
	KHZ(200),
	MHZ(200),
	0,
	supported_samplerates,
};

static int capabilities[] = {
	HWCAP_LOGIC_ANALYZER,
	HWCAP_SAMPLERATE,
	HWCAP_CAPTURE_RATIO,
	HWCAP_PROBECONFIG,

	HWCAP_LIMIT_MSEC,
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

static void hw_stop_acquisition(int device_index, gpointer session_device_id);

static int sigma_read(void *buf, size_t size, struct sigma *sigma)
{
	int ret;

	ret = ftdi_read_data(&sigma->ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		g_warning("ftdi_read_data failed: %s",
			  ftdi_get_error_string(&sigma->ftdic));
	}

	return ret;
}

static int sigma_write(void *buf, size_t size, struct sigma *sigma)
{
	int ret;

	ret = ftdi_write_data(&sigma->ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		g_warning("ftdi_write_data failed: %s",
			  ftdi_get_error_string(&sigma->ftdic));
	} else if ((size_t) ret != size) {
		g_warning("ftdi_write_data did not complete write\n");
	}

	return ret;
}

static int sigma_write_register(uint8_t reg, uint8_t *data, size_t len,
		struct sigma *sigma)
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

	return sigma_write(buf, idx, sigma);
}

static int sigma_set_register(uint8_t reg, uint8_t value, struct sigma *sigma)
{
	return sigma_write_register(reg, &value, 1, sigma);
}

static int sigma_read_register(uint8_t reg, uint8_t *data, size_t len,
		struct sigma *sigma)
{
	uint8_t buf[3];

	buf[0] = REG_ADDR_LOW | (reg & 0xf);
	buf[1] = REG_ADDR_HIGH | (reg >> 4);
	buf[2] = REG_READ_ADDR;

	sigma_write(buf, sizeof(buf), sigma);

	return sigma_read(data, len, sigma);
}

static uint8_t sigma_get_register(uint8_t reg, struct sigma *sigma)
{
	uint8_t value;

	if (1 != sigma_read_register(reg, &value, 1, sigma)) {
		g_warning("Sigma_get_register: 1 byte expected");
		return 0;
	}

	return value;
}

static int sigma_read_pos(uint32_t *stoppos, uint32_t *triggerpos,
		struct sigma *sigma)
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

	sigma_write(buf, sizeof(buf), sigma);

	sigma_read(result, sizeof(result), sigma);

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
		uint8_t *data, struct sigma *sigma)
{
	size_t i;
	uint8_t buf[4096];
	int idx = 0;

	/* Send the startchunk. Index start with 1. */
	buf[0] = startchunk >> 8;
	buf[1] = startchunk & 0xff;
	sigma_write_register(WRITE_MEMROW, buf, 2, sigma);

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

	sigma_write(buf, idx, sigma);

	return sigma_read(data, numchunks * CHUNK_SIZE, sigma);
}

/* Upload trigger look-up tables to Sigma. */
static int sigma_write_trigger_lut(struct triggerlut *lut, struct sigma *sigma)
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
				sigma);
		sigma_set_register(WRITE_TRIGGER_SELECT1, 0x30 | i, sigma);
	}

	/* Send the parameters */
	sigma_write_register(WRITE_TRIGGER_SELECT0, (uint8_t *) &lut->params,
			     sizeof(lut->params), sigma);

	return SR_OK;
}

/* Generate the bitbang stream for programming the FPGA. */
static int bin2bitbang(const char *filename,
		       unsigned char **buf, size_t *buf_size)
{
	FILE *f;
	long file_size;
	unsigned long offset = 0;
	unsigned char *p;
	uint8_t *compressed_buf, *firmware;
	uLongf csize, fwsize;
	const int buffer_size = 65536;
	size_t i;
	int c, ret, bit, v;
	uint32_t imm = 0x3f6df2ab;

	f = fopen(filename, "r");
	if (!f) {
		g_warning("fopen(\"%s\", \"r\")", filename);
		return -1;
	}

	if (-1 == fseek(f, 0, SEEK_END)) {
		g_warning("fseek on %s failed", filename);
		fclose(f);
		return -1;
	}

	file_size = ftell(f);

	fseek(f, 0, SEEK_SET);

	compressed_buf = g_malloc(file_size);
	firmware = g_malloc(buffer_size);

	if (!compressed_buf || !firmware) {
		g_warning("Error allocating buffers");
		return -1;
	}

	csize = 0;
	while ((c = getc(f)) != EOF) {
		imm = (imm + 0xa853753) % 177 + (imm * 0x8034052);
		compressed_buf[csize++] = c ^ imm;
	}
	fclose(f);

	fwsize = buffer_size;
	ret = uncompress(firmware, &fwsize, compressed_buf, csize);
	if (ret < 0) {
		g_free(compressed_buf);
		g_free(firmware);
		g_warning("Could not unpack Sigma firmware. (Error %d)\n", ret);
		return -1;
	}

	g_free(compressed_buf);

	*buf_size = fwsize * 2 * 8;

	*buf = p = (unsigned char *)g_malloc(*buf_size);

	if (!p) {
		g_warning("Error allocating buffers");
		return -1;
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
		g_warning("Error reading firmware %s "
			  "offset=%ld, file_size=%ld, buf_size=%zd\n",
			  filename, offset, file_size, *buf_size);

		return -1;
	}

	return 0;
}

static int hw_init(char *deviceinfo)
{
	struct sr_device_instance *sdi;
	struct sigma *sigma = g_malloc(sizeof(struct sigma));

	deviceinfo = deviceinfo;

	if (!sigma)
		return 0;

	ftdi_init(&sigma->ftdic);

	/* Look for SIGMAs. */
	if (ftdi_usb_open_desc(&sigma->ftdic, USB_VENDOR, USB_PRODUCT,
			       USB_DESCRIPTION, NULL) < 0)
		goto free;

	sigma->cur_samplerate = 0;
	sigma->limit_msec = 0;
	sigma->cur_firmware = -1;
	sigma->num_probes = 0;
	sigma->samples_per_event = 0;
	sigma->capture_ratio = 50;
	sigma->use_triggers = 0;

	/* Register SIGMA device. */
	sdi = sr_device_instance_new(0, ST_INITIALIZING,
			USB_VENDOR_NAME, USB_MODEL_NAME, USB_MODEL_VERSION);
	if (!sdi)
		goto free;

	sdi->priv = sigma;

	device_instances = g_slist_append(device_instances, sdi);

	/* We will open the device again when we need it. */
	ftdi_usb_close(&sigma->ftdic);

	return 1;
free:
	free(sigma);
	return 0;
}

static int upload_firmware(int firmware_idx, struct sigma *sigma)
{
	int ret;
	unsigned char *buf;
	unsigned char pins;
	size_t buf_size;
	unsigned char result[32];
	char firmware_path[128];

	/* Make sure it's an ASIX SIGMA. */
	if ((ret = ftdi_usb_open_desc(&sigma->ftdic,
		USB_VENDOR, USB_PRODUCT, USB_DESCRIPTION, NULL)) < 0) {
		g_warning("ftdi_usb_open failed: %s",
			  ftdi_get_error_string(&sigma->ftdic));
		return 0;
	}

	if ((ret = ftdi_set_bitmode(&sigma->ftdic, 0xdf, BITMODE_BITBANG)) < 0) {
		g_warning("ftdi_set_bitmode failed: %s",
			  ftdi_get_error_string(&sigma->ftdic));
		return 0;
	}

	/* Four times the speed of sigmalogan - Works well. */
	if ((ret = ftdi_set_baudrate(&sigma->ftdic, 750000)) < 0) {
		g_warning("ftdi_set_baudrate failed: %s",
			  ftdi_get_error_string(&sigma->ftdic));
		return 0;
	}

	/* Force the FPGA to reboot. */
	sigma_write(suicide, sizeof(suicide), sigma);
	sigma_write(suicide, sizeof(suicide), sigma);
	sigma_write(suicide, sizeof(suicide), sigma);
	sigma_write(suicide, sizeof(suicide), sigma);

	/* Prepare to upload firmware (FPGA specific). */
	sigma_write(init, sizeof(init), sigma);

	ftdi_usb_purge_buffers(&sigma->ftdic);

	/* Wait until the FPGA asserts INIT_B. */
	while (1) {
		ret = sigma_read(result, 1, sigma);
		if (result[0] & 0x20)
			break;
	}

	/* Prepare firmware. */
	snprintf(firmware_path, sizeof(firmware_path), "%s/%s", FIRMWARE_DIR,
		 firmware_files[firmware_idx]);

	if (-1 == bin2bitbang(firmware_path, &buf, &buf_size)) {
		g_warning("An error occured while reading the firmware: %s",
			  firmware_path);
		return SR_ERR;
	}

	/* Upload firmare. */
	sigma_write(buf, buf_size, sigma);

	g_free(buf);

	if ((ret = ftdi_set_bitmode(&sigma->ftdic, 0x00, BITMODE_RESET)) < 0) {
		g_warning("ftdi_set_bitmode failed: %s",
			  ftdi_get_error_string(&sigma->ftdic));
		return SR_ERR;
	}

	ftdi_usb_purge_buffers(&sigma->ftdic);

	/* Discard garbage. */
	while (1 == sigma_read(&pins, 1, sigma))
		;

	/* Initialize the logic analyzer mode. */
	sigma_write(logic_mode_start, sizeof(logic_mode_start), sigma);

	/* Expect a 3 byte reply. */
	ret = sigma_read(result, 3, sigma);
	if (ret != 3 ||
	    result[0] != 0xa6 || result[1] != 0x55 || result[2] != 0xaa) {
		g_warning("Configuration failed. Invalid reply received.");
		return SR_ERR;
	}

	sigma->cur_firmware = firmware_idx;

	return SR_OK;
}

static int hw_opendev(int device_index)
{
	struct sr_device_instance *sdi;
	struct sigma *sigma;
	int ret;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;

	sigma = sdi->priv;

	/* Make sure it's an ASIX SIGMA. */
	if ((ret = ftdi_usb_open_desc(&sigma->ftdic,
		USB_VENDOR, USB_PRODUCT, USB_DESCRIPTION, NULL)) < 0) {

		g_warning("ftdi_usb_open failed: %s",
			ftdi_get_error_string(&sigma->ftdic));

		return 0;
	}

	sdi->status = ST_ACTIVE;

	return SR_OK;
}

static int set_samplerate(struct sr_device_instance *sdi,
			  uint64_t samplerate)
{
	int i, ret;
	struct sigma *sigma = sdi->priv;

	for (i = 0; supported_samplerates[i]; i++) {
		if (supported_samplerates[i] == samplerate)
			break;
	}
	if (supported_samplerates[i] == 0)
		return SR_ERR_SAMPLERATE;

	if (samplerate <= MHZ(50)) {
		ret = upload_firmware(0, sigma);
		sigma->num_probes = 16;
	}
	if (samplerate == MHZ(100)) {
		ret = upload_firmware(1, sigma);
		sigma->num_probes = 8;
	}
	else if (samplerate == MHZ(200)) {
		ret = upload_firmware(2, sigma);
		sigma->num_probes = 4;
	}

	sigma->cur_samplerate = samplerate;
	sigma->samples_per_event = 16 / sigma->num_probes;
	sigma->state.state = SIGMA_IDLE;

	g_message("Firmware uploaded");

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
static int configure_probes(struct sr_device_instance *sdi, GSList *probes)
{
	struct sigma *sigma = sdi->priv;
	struct probe *probe;
	GSList *l;
	int trigger_set = 0;
	int probebit;

	memset(&sigma->trigger, 0, sizeof(struct sigma_trigger));

	for (l = probes; l; l = l->next) {
		probe = (struct probe *)l->data;
		probebit = 1 << (probe->index - 1);

		if (!probe->enabled || !probe->trigger)
			continue;

		if (sigma->cur_samplerate >= MHZ(100)) {
			/* Fast trigger support. */
			if (trigger_set) {
				g_warning("Asix Sigma only supports a single "
						"pin trigger in 100 and 200 "
						"MHz mode.");
				return SR_ERR;
			}
			if (probe->trigger[0] == 'f')
				sigma->trigger.fallingmask |= probebit;
			else if (probe->trigger[0] == 'r')
				sigma->trigger.risingmask |= probebit;
			else {
				g_warning("Asix Sigma only supports "
					  "rising/falling trigger in 100 "
					  "and 200 MHz mode.");
				return SR_ERR;
			}

			++trigger_set;
		} else {
			/* Simple trigger support (event). */
			if (probe->trigger[0] == '1') {
				sigma->trigger.simplevalue |= probebit;
				sigma->trigger.simplemask |= probebit;
			}
			else if (probe->trigger[0] == '0') {
				sigma->trigger.simplevalue &= ~probebit;
				sigma->trigger.simplemask |= probebit;
			}
			else if (probe->trigger[0] == 'f') {
				sigma->trigger.fallingmask |= probebit;
				++trigger_set;
			}
			else if (probe->trigger[0] == 'r') {
				sigma->trigger.risingmask |= probebit;
				++trigger_set;
			}

                        /*
                         * Actually, Sigma supports 2 rising/falling triggers,
                         * but they are ORed and the current trigger syntax
                         * does not permit ORed triggers.
                         */
			if (trigger_set > 1) {
				g_warning("Asix Sigma only supports 1 rising/"
					  "falling triggers.");
				return SR_ERR;
			}
		}

		if (trigger_set)
			sigma->use_triggers = 1;
	}

	return SR_OK;
}

static void hw_closedev(int device_index)
{
	struct sr_device_instance *sdi;
	struct sigma *sigma;

	if ((sdi = sr_get_device_instance(device_instances, device_index)))
	{
		sigma = sdi->priv;
		if (sdi->status == ST_ACTIVE)
			ftdi_usb_close(&sigma->ftdic);

		sdi->status = ST_INACTIVE;
	}
}

static void hw_cleanup(void)
{
	GSList *l;
	struct sr_device_instance *sdi;

	/* Properly close all devices. */
	for (l = device_instances; l; l = l->next) {
		sdi = l->data;
		if (sdi->priv != NULL)
			free(sdi->priv);
		sr_device_instance_free(sdi);
	}
	g_slist_free(device_instances);
	device_instances = NULL;
}

static void *hw_get_device_info(int device_index, int device_info_id)
{
	struct sr_device_instance *sdi;
	struct sigma *sigma;
	void *info = NULL;

	if (!(sdi = sr_get_device_instance(device_instances, device_index))) {
		fprintf(stderr, "It's NULL.\n");
		return NULL;
	}

	sigma = sdi->priv;

	switch (device_info_id) {
	case DI_INSTANCE:
		info = sdi;
		break;
	case DI_NUM_PROBES:
		info = GINT_TO_POINTER(16);
		break;
	case DI_SAMPLERATES:
		info = &samplerates;
		break;
	case DI_TRIGGER_TYPES:
		info = (char *)TRIGGER_TYPES;
		break;
	case DI_CUR_SAMPLERATE:
		info = &sigma->cur_samplerate;
		break;
	}

	return info;
}

static int hw_get_status(int device_index)
{
	struct sr_device_instance *sdi;

	sdi = sr_get_device_instance(device_instances, device_index);
	if (sdi)
		return sdi->status;
	else
		return ST_NOT_FOUND;
}

static int *hw_get_capabilities(void)
{
	return capabilities;
}

static int hw_set_configuration(int device_index, int capability, void *value)
{
	struct sr_device_instance *sdi;
	struct sigma *sigma;
	int ret;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;

	sigma = sdi->priv;

	if (capability == HWCAP_SAMPLERATE) {
		ret = set_samplerate(sdi, *(uint64_t*) value);
	} else if (capability == HWCAP_PROBECONFIG) {
		ret = configure_probes(sdi, value);
	} else if (capability == HWCAP_LIMIT_MSEC) {
		sigma->limit_msec = *(uint64_t*) value;
		if (sigma->limit_msec > 0)
			ret = SR_OK;
		else
			ret = SR_ERR;
	} else if (capability == HWCAP_CAPTURE_RATIO) {
		sigma->capture_ratio = *(uint64_t*) value;
		if (sigma->capture_ratio < 0 || sigma->capture_ratio > 100)
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
			   uint16_t limit_chunk, void *user_data)
{
	struct sr_device_instance *sdi = user_data;
	struct sigma *sigma = sdi->priv;
	uint16_t tsdiff, ts;
	uint16_t samples[65536 * sigma->samples_per_event];
	struct sr_datafeed_packet packet;
	int i, j, k, l, numpad, tosend;
	size_t n = 0, sent = 0;
	int clustersize = EVENTS_PER_CLUSTER * sigma->samples_per_event;
	uint16_t *event;
	uint16_t cur_sample;
	int triggerts = -1;

	/* Check if trigger is in this chunk. */
	if (triggerpos != -1) {
		if (sigma->cur_samplerate <= MHZ(50))
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
		numpad = tsdiff * sigma->samples_per_event - clustersize;
		if (numpad > 0) {
			for (j = 0; j < numpad; ++j)
				samples[j] = *lastsample;

			n = numpad;
		}

		/* Send samples between previous and this timestamp to sigrok. */
		sent = 0;
		while (sent < n) {
			tosend = MIN(2048, n - sent);

			packet.type = DF_LOGIC;
			packet.length = tosend * sizeof(uint16_t);
			packet.unitsize = 2;
			packet.payload = samples + sent;
			session_bus(sigma->session_id, &packet);

			sent += tosend;
		}
		n = 0;

		event = (uint16_t *) &buf[i * 16 + 2];
		cur_sample = 0;

		/* For each event in cluster. */
		for (j = 0; j < 7; ++j) {

			/* For each sample in event. */
			for (k = 0; k < sigma->samples_per_event; ++k) {
				cur_sample = 0;

				/* For each probe. */
				for (l = 0; l < sigma->num_probes; ++l)
					cur_sample |= (!!(event[j] & (1 << (l *
						      sigma->samples_per_event
						      + k))))
						      << l;

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
						    &sigma->trigger);

			if (tosend > 0) {
				packet.type = DF_LOGIC;
				packet.length = tosend * sizeof(uint16_t);
				packet.unitsize = 2;
				packet.payload = samples;
				session_bus(sigma->session_id, &packet);

				sent += tosend;
			}

			/* Only send trigger if explicitly enabled. */
			if (sigma->use_triggers) {
				packet.type = DF_TRIGGER;
				packet.length = 0;
				packet.payload = 0;
				session_bus(sigma->session_id, &packet);
			}
		}

		/* Send rest of the chunk to sigrok. */
		tosend = n - sent;

		if (tosend > 0) {
			packet.type = DF_LOGIC;
			packet.length = tosend * sizeof(uint16_t);
			packet.unitsize = 2;
			packet.payload = samples + sent;
			session_bus(sigma->session_id, &packet);
		}

		*lastsample = samples[n - 1];
	}

	return SR_OK;
}

static int receive_data(int fd, int revents, void *user_data)
{
	struct sr_device_instance *sdi = user_data;
	struct sigma *sigma = sdi->priv;
	struct sr_datafeed_packet packet;
	const int chunks_per_read = 32;
	unsigned char buf[chunks_per_read * CHUNK_SIZE];
	int bufsz, numchunks, i, newchunks;
	uint64_t running_msec;
	struct timeval tv;

	fd = fd;
	revents = revents;

	numchunks = (sigma->state.stoppos + 511) / 512;

	if (sigma->state.state == SIGMA_IDLE)
		return FALSE;

	if (sigma->state.state == SIGMA_CAPTURE) {

		/* Check if the timer has expired, or memory is full. */
		gettimeofday(&tv, 0);
		running_msec = (tv.tv_sec - sigma->start_tv.tv_sec) * 1000 +
			(tv.tv_usec - sigma->start_tv.tv_usec) / 1000;

		if (running_msec < sigma->limit_msec && numchunks < 32767)
			return FALSE;

		hw_stop_acquisition(sdi->index, user_data);

		return FALSE;

	} else if (sigma->state.state == SIGMA_DOWNLOAD) {
		if (sigma->state.chunks_downloaded >= numchunks) {
			/* End of samples. */
			packet.type = DF_END;
			packet.length = 0;
			session_bus(sigma->session_id, &packet);

			sigma->state.state = SIGMA_IDLE;

			return TRUE;
		}

		newchunks = MIN(chunks_per_read,
				numchunks - sigma->state.chunks_downloaded);

		g_message("Downloading sample data: %.0f %%",
			  100.0 * sigma->state.chunks_downloaded / numchunks);

		bufsz = sigma_read_dram(sigma->state.chunks_downloaded,
					newchunks, buf, sigma);

		/* Find first ts. */
		if (sigma->state.chunks_downloaded == 0) {
			sigma->state.lastts = *(uint16_t *) buf - 1;
			sigma->state.lastsample = 0;
		}

		/* Decode chunks and send them to sigrok. */
		for (i = 0; i < newchunks; ++i) {
			int limit_chunk = 0;

			/* The last chunk may potentially be only in part. */
			if (sigma->state.chunks_downloaded == numchunks - 1)
			{
				/* Find the last valid timestamp */
				limit_chunk = sigma->state.stoppos % 512 + sigma->state.lastts;
			}

			if (sigma->state.chunks_downloaded + i == sigma->state.triggerchunk)
				decode_chunk_ts(buf + (i * CHUNK_SIZE),
						&sigma->state.lastts,
						&sigma->state.lastsample,
						sigma->state.triggerpos & 0x1ff,
						limit_chunk, user_data);
			else
				decode_chunk_ts(buf + (i * CHUNK_SIZE),
						&sigma->state.lastts,
						&sigma->state.lastsample,
						-1, limit_chunk, user_data);

			++sigma->state.chunks_downloaded;
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
		for (i = 0; i < 2; ++i)
			for (j = 0; j < 2; ++j) {
				tmp = x[i][j];
				x[i][j] = x[1-i][1-j];
				x[1-i][1-j] = tmp;
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
static int build_basic_trigger(struct triggerlut *lut, struct sigma *sigma)
{
	int i,j;
	uint16_t masks[2] = { 0, 0 };

	memset(lut, 0, sizeof(struct triggerlut));

	/* Contant for simple triggers. */
	lut->m4 = 0xa000;

	/* Value/mask trigger support. */
	build_lut_entry(sigma->trigger.simplevalue, sigma->trigger.simplemask,
			lut->m2d);

	/* Rise/fall trigger support. */
	for (i = 0, j = 0; i < 16; ++i) {
		if (sigma->trigger.risingmask & (1 << i) ||
		    sigma->trigger.fallingmask & (1 << i))
			masks[j++] = 1 << i;
	}

	build_lut_entry(masks[0], masks[0], lut->m0d);
	build_lut_entry(masks[1], masks[1], lut->m1d);

	/* Add glue logic */
	if (masks[0] || masks[1]) {
		/* Transition trigger. */
		if (masks[0] & sigma->trigger.risingmask)
			add_trigger_function(OP_RISE, FUNC_OR, 0, 0, &lut->m3);
		if (masks[0] & sigma->trigger.fallingmask)
			add_trigger_function(OP_FALL, FUNC_OR, 0, 0, &lut->m3);
		if (masks[1] & sigma->trigger.risingmask)
			add_trigger_function(OP_RISE, FUNC_OR, 1, 0, &lut->m3);
		if (masks[1] & sigma->trigger.fallingmask)
			add_trigger_function(OP_FALL, FUNC_OR, 1, 0, &lut->m3);
	} else {
		/* Only value/mask trigger. */
		lut->m3 = 0xffff;
	}

	/* Triggertype: event. */
	lut->params.selres = 3;

	return SR_OK;
}

static int hw_start_acquisition(int device_index, gpointer session_device_id)
{
	struct sr_device_instance *sdi;
	struct sigma *sigma;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_header header;
	struct clockselect_50 clockselect;
	int frac;
	uint8_t triggerselect;
	struct triggerinout triggerinout_conf;
	struct triggerlut lut;
	int triggerpin;

	session_device_id = session_device_id;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return SR_ERR;

	sigma = sdi->priv;

	/* If the samplerate has not been set, default to 200 KHz. */
	if (sigma->cur_firmware == -1)
		set_samplerate(sdi, KHZ(200));

	/* Enter trigger programming mode. */
	sigma_set_register(WRITE_TRIGGER_SELECT1, 0x20, sigma);

	/* 100 and 200 MHz mode. */
	if (sigma->cur_samplerate >= MHZ(100)) {
		sigma_set_register(WRITE_TRIGGER_SELECT1, 0x81, sigma);

		/* Find which pin to trigger on from mask. */
		for (triggerpin = 0; triggerpin < 8; ++triggerpin)
			if ((sigma->trigger.risingmask | sigma->trigger.fallingmask) &
			    (1 << triggerpin))
				break;

		/* Set trigger pin and light LED on trigger. */
		triggerselect = (1 << LEDSEL1) | (triggerpin & 0x7);

		/* Default rising edge. */
		if (sigma->trigger.fallingmask)
			triggerselect |= 1 << 3;

	/* All other modes. */
	} else if (sigma->cur_samplerate <= MHZ(50)) {
		build_basic_trigger(&lut, sigma);

		sigma_write_trigger_lut(&lut, sigma);

		triggerselect = (1 << LEDSEL1) | (1 << LEDSEL0);
	}

	/* Setup trigger in and out pins to default values. */
	memset(&triggerinout_conf, 0, sizeof(struct triggerinout));
	triggerinout_conf.trgout_bytrigger = 1;
	triggerinout_conf.trgout_enable = 1;

	sigma_write_register(WRITE_TRIGGER_OPTION,
			     (uint8_t *) &triggerinout_conf,
			     sizeof(struct triggerinout), sigma);

	/* Go back to normal mode. */
	sigma_set_register(WRITE_TRIGGER_SELECT1, triggerselect, sigma);

	/* Set clock select register. */
	if (sigma->cur_samplerate == MHZ(200))
		/* Enable 4 probes. */
		sigma_set_register(WRITE_CLOCK_SELECT, 0xf0, sigma);
	else if (sigma->cur_samplerate == MHZ(100))
		/* Enable 8 probes. */
		sigma_set_register(WRITE_CLOCK_SELECT, 0x00, sigma);
	else {
		/*
		 * 50 MHz mode (or fraction thereof). Any fraction down to
		 * 50 MHz / 256 can be used, but is not supported by sigrok API.
		 */
		frac = MHZ(50) / sigma->cur_samplerate - 1;

		clockselect.async = 0;
		clockselect.fraction = frac;
		clockselect.disabled_probes = 0;

		sigma_write_register(WRITE_CLOCK_SELECT,
				     (uint8_t *) &clockselect,
				     sizeof(clockselect), sigma);
	}

	/* Setup maximum post trigger time. */
	sigma_set_register(WRITE_POST_TRIGGER,
			(sigma->capture_ratio * 255) / 100, sigma);

	/* Start acqusition. */
	gettimeofday(&sigma->start_tv, 0);
	sigma_set_register(WRITE_MODE, 0x0d, sigma);

	sigma->session_id = session_device_id;

	/* Send header packet to the session bus. */
	packet.type = DF_HEADER;
	packet.length = sizeof(struct sr_datafeed_header);
	packet.payload = &header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	header.samplerate = sigma->cur_samplerate;
	header.protocol_id = PROTO_RAW;
	header.num_logic_probes = sigma->num_probes;
	header.num_analog_probes = 0;
	session_bus(session_device_id, &packet);

	/* Add capture source. */
	source_add(0, G_IO_IN, 10, receive_data, sdi);

	sigma->state.state = SIGMA_CAPTURE;

	return SR_OK;
}

static void hw_stop_acquisition(int device_index, gpointer session_device_id)
{
	struct sr_device_instance *sdi;
	struct sigma *sigma;
	uint8_t modestatus;

	if (!(sdi = sr_get_device_instance(device_instances, device_index)))
		return;

	sigma = sdi->priv;

	session_device_id = session_device_id;

	/* Stop acquisition. */
	sigma_set_register(WRITE_MODE, 0x11, sigma);

	/* Set SDRAM Read Enable. */
	sigma_set_register(WRITE_MODE, 0x02, sigma);

	/* Get the current position. */
	sigma_read_pos(&sigma->state.stoppos, &sigma->state.triggerpos, sigma);

	/* Check if trigger has fired. */
	modestatus = sigma_get_register(READ_MODE, sigma);
	if (modestatus & 0x20) {
		sigma->state.triggerchunk = sigma->state.triggerpos / 512;

	} else
		sigma->state.triggerchunk = -1;

	sigma->state.chunks_downloaded = 0;

	sigma->state.state = SIGMA_DOWNLOAD;
}

struct device_plugin asix_sigma_plugin_info = {
	"asix-sigma",
	1,
	hw_init,
	hw_cleanup,
	hw_opendev,
	hw_closedev,
	hw_get_device_info,
	hw_get_status,
	hw_get_capabilities,
	hw_set_configuration,
	hw_start_acquisition,
	hw_stop_acquisition,
};
