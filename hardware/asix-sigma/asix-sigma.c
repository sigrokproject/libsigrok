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
#include "asix-sigma.h"

#define USB_VENDOR			0xa600
#define USB_PRODUCT			0xa000
#define USB_DESCRIPTION			"ASIX SIGMA"
#define USB_VENDOR_NAME			"ASIX"
#define USB_MODEL_NAME			"SIGMA"
#define USB_MODEL_VERSION		""
#define TRIGGER_TYPES			"rf10"

static GSList *device_instances = NULL;

// XXX These should be per device
static struct ftdi_context ftdic;
static uint64_t cur_samplerate = 0;
static uint32_t limit_msec = 0;
static struct timeval start_tv;
static int cur_firmware = -1;
static int num_probes = 0;
static int samples_per_event = 0;
static int capture_ratio = 50;

/* Single-pin trigger support (100 and 200 MHz).*/
static uint8_t triggerpin = 1;
static uint8_t triggerfall = 0;

/* Simple trigger support (<= 50 MHz). */
static uint16_t triggermask = 1;
static uint16_t triggervalue = 1;

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

static int sigma_read(void *buf, size_t size)
{
	int ret;

	ret = ftdi_read_data(&ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		g_warning("ftdi_read_data failed: %s",
			  ftdi_get_error_string(&ftdic));
	}

	return ret;
}

static int sigma_write(void *buf, size_t size)
{
	int ret;

	ret = ftdi_write_data(&ftdic, (unsigned char *)buf, size);
	if (ret < 0) {
		g_warning("ftdi_write_data failed: %s",
			  ftdi_get_error_string(&ftdic));
	} else if ((size_t) ret != size) {
		g_warning("ftdi_write_data did not complete write\n");
	}

	return ret;
}

static int sigma_write_register(uint8_t reg, uint8_t *data, size_t len)
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

	return sigma_write(buf, idx);
}

static int sigma_set_register(uint8_t reg, uint8_t value)
{
	return sigma_write_register(reg, &value, 1);
}

static int sigma_read_register(uint8_t reg, uint8_t *data, size_t len)
{
	uint8_t buf[3];

	buf[0] = REG_ADDR_LOW | (reg & 0xf);
	buf[1] = REG_ADDR_HIGH | (reg >> 4);
	buf[2] = REG_READ_ADDR;

	sigma_write(buf, sizeof(buf));

	return sigma_read(data, len);
}

static uint8_t sigma_get_register(uint8_t reg)
{
	uint8_t value;

	if (1 != sigma_read_register(reg, &value, 1)) {
		g_warning("Sigma_get_register: 1 byte expected");
		return 0;
	}

	return value;
}

static int sigma_read_pos(uint32_t *stoppos, uint32_t *triggerpos)
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

	sigma_write(buf, sizeof(buf));

	sigma_read(result, sizeof(result));

	*triggerpos = result[0] | (result[1] << 8) | (result[2] << 16);
	*stoppos = result[3] | (result[4] << 8) | (result[5] << 16);

	/* Not really sure why this must be done, but according to spec. */
	if ((--*stoppos & 0x1ff) == 0x1ff)
		stoppos -= 64;

	if ((*--triggerpos & 0x1ff) == 0x1ff)
		triggerpos -= 64;

	return 1;
}

static int sigma_read_dram(uint16_t startchunk, size_t numchunks, uint8_t *data)
{
	size_t i;
	uint8_t buf[4096];
	int idx = 0;

	/* Send the startchunk. Index start with 1. */
	buf[0] = startchunk >> 8;
	buf[1] = startchunk & 0xff;
	sigma_write_register(WRITE_MEMROW, buf, 2);

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

	sigma_write(buf, idx);

	return sigma_read(data, numchunks * CHUNK_SIZE);
}

/* Upload trigger look-up tables to Sigma */
static int sigma_write_trigger_lut(struct triggerlut *lut)
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

		sigma_write_register(WRITE_TRIGGER_SELECT0, tmp, sizeof(tmp));
		sigma_set_register(WRITE_TRIGGER_SELECT1, 0x30 | i);
	}

	/* Send the parameters */
	sigma_write_register(WRITE_TRIGGER_SELECT0, (uint8_t *) &lut->params,
			     sizeof(lut->params));

	return SIGROK_OK;
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
	struct sigrok_device_instance *sdi;

	deviceinfo = deviceinfo;

	ftdi_init(&ftdic);

	/* Look for SIGMAs. */
	if (ftdi_usb_open_desc(&ftdic, USB_VENDOR, USB_PRODUCT,
			       USB_DESCRIPTION, NULL) < 0)
		return 0;

	/* Register SIGMA device. */
	sdi = sigrok_device_instance_new(0, ST_INITIALIZING,
			USB_VENDOR_NAME, USB_MODEL_NAME, USB_MODEL_VERSION);
	if (!sdi)
		return 0;

	device_instances = g_slist_append(device_instances, sdi);

	/* We will open the device again when we need it. */
	ftdi_usb_close(&ftdic);

	return 1;
}

static int upload_firmware(int firmware_idx)
{
	int ret;
	unsigned char *buf;
	unsigned char pins;
	size_t buf_size;
	unsigned char result[32];
	char firmware_path[128];

	/* Make sure it's an ASIX SIGMA. */
	if ((ret = ftdi_usb_open_desc(&ftdic,
		USB_VENDOR, USB_PRODUCT, USB_DESCRIPTION, NULL)) < 0) {
		g_warning("ftdi_usb_open failed: %s",
			  ftdi_get_error_string(&ftdic));
		return 0;
	}

	if ((ret = ftdi_set_bitmode(&ftdic, 0xdf, BITMODE_BITBANG)) < 0) {
		g_warning("ftdi_set_bitmode failed: %s",
			  ftdi_get_error_string(&ftdic));
		return 0;
	}

	/* Four times the speed of sigmalogan - Works well. */
	if ((ret = ftdi_set_baudrate(&ftdic, 750000)) < 0) {
		g_warning("ftdi_set_baudrate failed: %s",
			  ftdi_get_error_string(&ftdic));
		return 0;
	}

	/* Force the FPGA to reboot. */
	sigma_write(suicide, sizeof(suicide));
	sigma_write(suicide, sizeof(suicide));
	sigma_write(suicide, sizeof(suicide));
	sigma_write(suicide, sizeof(suicide));

	/* Prepare to upload firmware (FPGA specific). */
	sigma_write(init, sizeof(init));

	ftdi_usb_purge_buffers(&ftdic);

	/* Wait until the FPGA asserts INIT_B. */
	while (1) {
		ret = sigma_read(result, 1);
		if (result[0] & 0x20)
			break;
	}

	/* Prepare firmware. */
	snprintf(firmware_path, sizeof(firmware_path), "%s/%s", FIRMWARE_DIR,
		 firmware_files[firmware_idx]);

	if (-1 == bin2bitbang(firmware_path, &buf, &buf_size)) {
		g_warning("An error occured while reading the firmware: %s",
			  firmware_path);
		return SIGROK_ERR;
	}

	/* Upload firmare. */
	sigma_write(buf, buf_size);

	g_free(buf);

	if ((ret = ftdi_set_bitmode(&ftdic, 0x00, BITMODE_RESET)) < 0) {
		g_warning("ftdi_set_bitmode failed: %s",
			  ftdi_get_error_string(&ftdic));
		return SIGROK_ERR;
	}

	ftdi_usb_purge_buffers(&ftdic);

	/* Discard garbage. */
	while (1 == sigma_read(&pins, 1))
		;

	/* Initialize the logic analyzer mode. */
	sigma_write(logic_mode_start, sizeof(logic_mode_start));

	/* Expect a 3 byte reply. */
	ret = sigma_read(result, 3);
	if (ret != 3 ||
	    result[0] != 0xa6 || result[1] != 0x55 || result[2] != 0xaa) {
		g_warning("Configuration failed. Invalid reply received.");
		return SIGROK_ERR;
	}

	cur_firmware = firmware_idx;

	return SIGROK_OK;
}

static int hw_opendev(int device_index)
{
	struct sigrok_device_instance *sdi;
	int ret;

	/* Make sure it's an ASIX SIGMA. */
	if ((ret = ftdi_usb_open_desc(&ftdic,
		USB_VENDOR, USB_PRODUCT, USB_DESCRIPTION, NULL)) < 0) {

		g_warning("ftdi_usb_open failed: %s",
			ftdi_get_error_string(&ftdic));

		return 0;
	}

	if (!(sdi = get_sigrok_device_instance(device_instances, device_index)))
		return SIGROK_ERR;

	sdi->status = ST_ACTIVE;

	return SIGROK_OK;
}

static int set_samplerate(struct sigrok_device_instance *sdi, uint64_t samplerate)
{
	int i, ret;

	sdi = sdi;

	for (i = 0; supported_samplerates[i]; i++) {
		if (supported_samplerates[i] == samplerate)
			break;
	}
	if (supported_samplerates[i] == 0)
		return SIGROK_ERR_SAMPLERATE;

	if (samplerate <= MHZ(50)) {
		ret = upload_firmware(0);
		num_probes = 16;
	}
	if (samplerate == MHZ(100)) {
		ret = upload_firmware(1);
		num_probes = 8;
	}
	else if (samplerate == MHZ(200)) {
		ret = upload_firmware(2);
		num_probes = 4;
	}

	cur_samplerate = samplerate;
	samples_per_event = 16 / num_probes;

	g_message("Firmware uploaded");

	return ret;
}

/* Only trigger on single pin supported (in 100-200 MHz modes). */
static int configure_probes(GSList *probes)
{
	struct probe *probe;
	GSList *l;
	int trigger_set = 0;

	triggermask = 0;
	triggervalue = 0;

	for (l = probes; l; l = l->next) {
		probe = (struct probe *)l->data;

		if (!probe->enabled || !probe->trigger)
			continue;

		if (cur_samplerate >= MHZ(100)) {
			/* Fast trigger support */
			if (trigger_set) {
				g_warning("Asix Sigma only supports a single pin trigger "
						"in 100 and 200 MHz mode.");
				return SIGROK_ERR;
			}
			if (probe->trigger[0] == 'f')
				triggerfall = 1;
			else if (probe->trigger[0] == 'r')
				triggerfall = 0;
			else {
				g_warning("Asix Sigma only supports "
					  "rising/falling trigger in 100 "
					  "and 200 MHz mode.");
				return SIGROK_ERR;
			}

			triggerpin = probe->index - 1;
		} else {
			/* Normal trigger support */
			triggermask |= 1 << (probe->index - 1);

			if (probe->trigger[0] == '1')
				triggervalue |= 1 << (probe->index - 1);
			else if (probe->trigger[0] == '0')
				triggervalue |= 0 << (probe->index - 1);
			else {
				g_warning("Asix Sigma only supports "
					  "trigger values in <= 50"
					  " MHz mode.");
				return SIGROK_ERR;
			}

		}

		++trigger_set;
	}

	return SIGROK_OK;
}

static void hw_closedev(int device_index)
{
	device_index = device_index;

	ftdi_usb_close(&ftdic);
}

static void hw_cleanup(void)
{
}

static void *hw_get_device_info(int device_index, int device_info_id)
{
	struct sigrok_device_instance *sdi;
	void *info = NULL;

	if (!(sdi = get_sigrok_device_instance(device_instances, device_index))) {
		fprintf(stderr, "It's NULL.\n");
		return NULL;
	}

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
		info = &cur_samplerate;
		break;
	}

	return info;
}

static int hw_get_status(int device_index)
{
	struct sigrok_device_instance *sdi;

	sdi = get_sigrok_device_instance(device_instances, device_index);
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
	struct sigrok_device_instance *sdi;
	int ret;

	if (!(sdi = get_sigrok_device_instance(device_instances, device_index)))
		return SIGROK_ERR;

	if (capability == HWCAP_SAMPLERATE) {
		ret = set_samplerate(sdi, *(uint64_t*) value);
	} else if (capability == HWCAP_PROBECONFIG) {
		ret = configure_probes(value);
	} else if (capability == HWCAP_LIMIT_MSEC) {
		limit_msec = strtoull(value, NULL, 10);
		ret = SIGROK_OK;
	} else if (capability == HWCAP_CAPTURE_RATIO) {
		capture_ratio = strtoull(value, NULL, 10);
		ret = SIGROK_OK;
	} else if (capability == HWCAP_PROBECONFIG) {
		ret = configure_probes((GSList *) value);
	} else {
		ret = SIGROK_ERR;
	}

	return ret;
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
			   uint16_t *lastsample, int triggerpos, void *user_data)
{
	uint16_t tsdiff, ts;
	uint16_t samples[65536 * samples_per_event];
	struct datafeed_packet packet;
	int i, j, k, l, numpad, tosend;
	size_t n = 0, sent = 0;
	int clustersize = EVENTS_PER_CLUSTER * samples_per_event;
	uint16_t *event;
	uint16_t cur_sample;
	int triggerts = -1;
	int triggeroff = 0;

	if (triggerpos != -1) {
		if (cur_samplerate <= MHZ(50))
			triggerpos -= EVENTS_PER_CLUSTER;
		else
			triggeroff = 3;

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

		/* Pad last sample up to current point. */
		numpad = tsdiff * samples_per_event - clustersize;
		if (numpad > 0) {
			for (j = 0; j < numpad; ++j)
				samples[j] = *lastsample;

			n = numpad;
		}

		/* Send samples between previous and this timestamp to sigrok. */
		sent = 0;
		while (sent < n) {
			tosend = MIN(2048, n - sent);

			packet.type = DF_LOGIC16;
			packet.length = tosend * sizeof(uint16_t);
			packet.payload = samples + sent;
			session_bus(user_data, &packet);

			sent += tosend;
		}
		n = 0;

		event = (uint16_t *) &buf[i * 16 + 2];
		cur_sample = 0;

		/* For each event in cluster. */
		for (j = 0; j < 7; ++j) {

			/* For each sample in event. */
			for (k = 0; k < samples_per_event; ++k) {
				cur_sample = 0;

				/* For each probe. */
				for (l = 0; l < num_probes; ++l)
					cur_sample |= (!!(event[j] & (1 << (l *
						      samples_per_event + k))))
						      << l;

				samples[n++] = cur_sample;
			}
		}

		/* Send data up to trigger point (if triggered). */
		sent = 0;
		if (i == triggerts) {
			/*
			 * Trigger is presumptively only accurate to event, i.e.
			 * for 100 and 200 MHz, where multiple samples are coded
			 * in a single event, the trigger does not match the
			 * exact sample.
			 */
			tosend = (triggerpos % 7) - triggeroff;

			if (tosend > 0) {
				packet.type = DF_LOGIC16;
				packet.length = tosend * sizeof(uint16_t);
				packet.payload = samples;
				session_bus(user_data, &packet);

				sent += tosend;
			}

			packet.type = DF_TRIGGER;
			packet.length = 0;
			packet.payload = 0;
			session_bus(user_data, &packet);
		}

		/* Send rest of the chunk to sigrok. */
		tosend = n - sent;

		packet.type = DF_LOGIC16;
		packet.length = tosend * sizeof(uint16_t);
		packet.payload = samples + sent;
		session_bus(user_data, &packet);

		*lastsample = samples[n - 1];
	}

	return SIGROK_OK;
}

static int receive_data(int fd, int revents, void *user_data)
{
	struct datafeed_packet packet;
	const int chunks_per_read = 32;
	unsigned char buf[chunks_per_read * CHUNK_SIZE];
	int bufsz, numchunks, curchunk, i, newchunks;
	uint32_t triggerpos, stoppos, running_msec;
	struct timeval tv;
	uint16_t lastts = 0;
	uint16_t lastsample = 0;
	uint8_t modestatus;
	int triggerchunk = -1;

	fd = fd;
	revents = revents;

	/* Get the current position. */
	sigma_read_pos(&stoppos, &triggerpos);
	numchunks = stoppos / 512;

	/* Check if the has expired, or memory is full. */
	gettimeofday(&tv, 0);
	running_msec = (tv.tv_sec - start_tv.tv_sec) * 1000 +
		       (tv.tv_usec - start_tv.tv_usec) / 1000;

	if (running_msec < limit_msec && numchunks < 32767)
		return FALSE;

	/* Stop acqusition. */
	sigma_set_register(WRITE_MODE, 0x11);

	/* Set SDRAM Read Enable. */
	sigma_set_register(WRITE_MODE, 0x02);

	/* Get the current position. */
	sigma_read_pos(&stoppos, &triggerpos);

	/* Check if trigger has fired. */
	modestatus = sigma_get_register(READ_MODE);
	if (modestatus & 0x20) {
		triggerchunk = triggerpos / 512;
	}

	/* Download sample data. */
	for (curchunk = 0; curchunk < numchunks;) {
		newchunks = MIN(chunks_per_read, numchunks - curchunk);

		g_message("Downloading sample data: %.0f %%",
			  100.0 * curchunk / numchunks);

		bufsz = sigma_read_dram(curchunk, newchunks, buf);

		/* Find first ts. */
		if (curchunk == 0)
			lastts = *(uint16_t *) buf - 1;

		/* Decode chunks and send them to sigrok. */
		for (i = 0; i < newchunks; ++i) {
			if (curchunk + i == triggerchunk)
				decode_chunk_ts(buf + (i * CHUNK_SIZE),
						&lastts, &lastsample,
						triggerpos & 0x1ff, user_data);
			else
				decode_chunk_ts(buf + (i * CHUNK_SIZE),
						&lastts, &lastsample,
						-1, user_data);
		}

		curchunk += newchunks;
	}

	/* End of data. */
	packet.type = DF_END;
	packet.length = 0;
	session_bus(user_data, &packet);

	return TRUE;
}

/*
 * Build trigger LUTs used by 50 MHz and lower sample rates for supporting
 * simple pin change and state triggers. Only two transitions (rise/fall) can be
 * set at any time, but a full mask and value can be set (0/1).
 */
static int build_basic_trigger(struct triggerlut *lut)
{
	int i, j, k, bit;

	memset(lut, 0, sizeof(struct triggerlut));

	/* Unknown */
	lut->m4 = 0xa000;

	/* Set the LUT for controlling value/maske trigger */

	/* For each quad probe. */
	for (i = 0; i < 4; ++i) {
		lut->m2d[i] = 0xffff;

		/* For each bit in LUT. */
		for (j = 0; j < 16; ++j)

			/* For each probe in quad. */
			for (k = 0; k < 4; ++k) {
				bit = 1 << (i * 4 + k);

				if ((triggermask & bit) &&
				    ((!(triggervalue & bit)) !=
				     (!(j & (1 << k)))))
					lut->m2d[i] &= ~(1 << j);
			}
	}

	/* Unused when not triggering on transitions */
	lut->m3 = 0xffff;

	/* Triggertype: event */
	lut->params.selres = 3;

	return SIGROK_OK;
}

static int hw_start_acquisition(int device_index, gpointer session_device_id)
{
	struct sigrok_device_instance *sdi;
	struct datafeed_packet packet;
	struct datafeed_header header;
	struct clockselect_50 clockselect;
	int frac;
	uint8_t triggerselect;
	struct triggerinout triggerinout_conf;
	struct triggerlut lut;

	session_device_id = session_device_id;

	if (!(sdi = get_sigrok_device_instance(device_instances, device_index)))
		return SIGROK_ERR;

	device_index = device_index;

	/* If the samplerate has not been set, default to 50 MHz. */
	if (cur_firmware == -1)
		set_samplerate(sdi, MHZ(50));

	/* Enter trigger programming mode. */
	sigma_set_register(WRITE_TRIGGER_SELECT1, 0x20);

	/* 100 and 200 MHz mode. */
	if (cur_samplerate >= MHZ(100)) {
		sigma_set_register(WRITE_TRIGGER_SELECT1, 0x81);

		triggerselect = (1 << LEDSEL1) | (triggerfall << 3) |
					(triggerpin & 0x7);

	/* All other modes. */
	} else if (cur_samplerate <= MHZ(50)) {
		build_basic_trigger(&lut);

		sigma_write_trigger_lut(&lut);

		triggerselect = (1 << LEDSEL1) | (1 << LEDSEL0);
	}

	/* Setup trigger in and out pins to default values. */
	memset(&triggerinout_conf, 0, sizeof(struct triggerinout));
	triggerinout_conf.trgout_bytrigger = 1;
	triggerinout_conf.trgout_enable = 1;

	sigma_write_register(WRITE_TRIGGER_OPTION,
			     (uint8_t *) &triggerinout_conf,
			     sizeof(struct triggerinout));

	/* Go back to normal mode. */
	sigma_set_register(WRITE_TRIGGER_SELECT1, triggerselect);

	/* Set clock select register. */
	if (cur_samplerate == MHZ(200))
		/* Enable 4 probes. */
		sigma_set_register(WRITE_CLOCK_SELECT, 0xf0);
	else if (cur_samplerate == MHZ(100))
		/* Enable 8 probes. */
		sigma_set_register(WRITE_CLOCK_SELECT, 0x00);
	else {
		/*
		 * 50 MHz mode (or fraction thereof). Any fraction down to
		 * 50 MHz / 256 can be used, but is not supported by sigrok API.
		 */
		frac = MHZ(50) / cur_samplerate - 1;

		clockselect.async = 0;
		clockselect.fraction = frac;
		clockselect.disabled_probes = 0;

		sigma_write_register(WRITE_CLOCK_SELECT,
				     (uint8_t *) &clockselect,
				     sizeof(clockselect));
	}

	/* Setup maximum post trigger time. */
	sigma_set_register(WRITE_POST_TRIGGER, (capture_ratio * 256) / 100);

	/* Start acqusition. */
	gettimeofday(&start_tv, 0);
	sigma_set_register(WRITE_MODE, 0x0d);

	/* Send header packet to the session bus. */
	packet.type = DF_HEADER;
	packet.length = sizeof(struct datafeed_header);
	packet.payload = &header;
	header.feed_version = 1;
	gettimeofday(&header.starttime, NULL);
	header.samplerate = cur_samplerate;
	header.protocol_id = PROTO_RAW;
	header.num_probes = num_probes;
	session_bus(session_device_id, &packet);

	/* Add capture source. */
	source_add(0, G_IO_IN, 10, receive_data, session_device_id);

	return SIGROK_OK;
}

static void hw_stop_acquisition(int device_index, gpointer session_device_id)
{
	device_index = device_index;
	session_device_id = session_device_id;

	/* Stop acquisition. */
	sigma_set_register(WRITE_MODE, 0x11);

	// XXX Set some state to indicate that data should be sent to sigrok
	//     Now, we just wait for timeout
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
