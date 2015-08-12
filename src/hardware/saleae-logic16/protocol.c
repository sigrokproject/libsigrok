/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Marcus Comstedt <marcus@mc.pp.se>
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
 * Copyright (C) 2012 Joel Holdsworth <joel@airwebreathe.org.uk>
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

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define FPGA_FIRMWARE_18	FIRMWARE_DIR"/saleae-logic16-fpga-18.bitstream"
#define FPGA_FIRMWARE_33	FIRMWARE_DIR"/saleae-logic16-fpga-33.bitstream"

#define MAX_SAMPLE_RATE		SR_MHZ(100)
#define MAX_4CH_SAMPLE_RATE	SR_MHZ(50)
#define MAX_7CH_SAMPLE_RATE	SR_MHZ(40)
#define MAX_8CH_SAMPLE_RATE	SR_MHZ(32)
#define MAX_10CH_SAMPLE_RATE	SR_MHZ(25)
#define MAX_13CH_SAMPLE_RATE	SR_MHZ(16)

#define BASE_CLOCK_0_FREQ	SR_MHZ(100)
#define BASE_CLOCK_1_FREQ	SR_MHZ(160)

#define COMMAND_START_ACQUISITION	1
#define COMMAND_ABORT_ACQUISITION_ASYNC	2
#define COMMAND_WRITE_EEPROM		6
#define COMMAND_READ_EEPROM		7
#define COMMAND_WRITE_LED_TABLE		0x7a
#define COMMAND_SET_LED_MODE		0x7b
#define COMMAND_RETURN_TO_BOOTLOADER	0x7c
#define COMMAND_ABORT_ACQUISITION_SYNC	0x7d
#define COMMAND_FPGA_UPLOAD_INIT	0x7e
#define COMMAND_FPGA_UPLOAD_SEND_DATA	0x7f
#define COMMAND_FPGA_WRITE_REGISTER	0x80
#define COMMAND_FPGA_READ_REGISTER	0x81
#define COMMAND_GET_REVID		0x82

#define WRITE_EEPROM_COOKIE1		0x42
#define WRITE_EEPROM_COOKIE2		0x55
#define READ_EEPROM_COOKIE1		0x33
#define READ_EEPROM_COOKIE2		0x81
#define ABORT_ACQUISITION_SYNC_PATTERN	0x55

#define MAX_EMPTY_TRANSFERS		64

/* Register mappings for old and new bitstream versions */

enum fpga_register_id {
	FPGA_REGISTER_VERSION,
	FPGA_REGISTER_STATUS_CONTROL,
	FPGA_REGISTER_CHANNEL_SELECT_LOW,
	FPGA_REGISTER_CHANNEL_SELECT_HIGH,
	FPGA_REGISTER_SAMPLE_RATE_DIVISOR,
	FPGA_REGISTER_LED_BRIGHTNESS,
	FPGA_REGISTER_PRIMER_DATA1,
	FPGA_REGISTER_PRIMER_CONTROL,
	FPGA_REGISTER_MODE,
	FPGA_REGISTER_PRIMER_DATA2,
	FPGA_REGISTER_MAX = FPGA_REGISTER_PRIMER_DATA2
};

enum fpga_status_control_bit {
	FPGA_STATUS_CONTROL_BIT_RUNNING,
	FPGA_STATUS_CONTROL_BIT_UPDATE,
	FPGA_STATUS_CONTROL_BIT_UNKNOWN1,
	FPGA_STATUS_CONTROL_BIT_OVERFLOW,
	FPGA_STATUS_CONTROL_BIT_UNKNOWN2,
	FPGA_STATUS_CONTROL_BIT_MAX = FPGA_STATUS_CONTROL_BIT_UNKNOWN2
};

enum fpga_mode_bit {
	FPGA_MODE_BIT_CLOCK,
	FPGA_MODE_BIT_UNKNOWN1,
	FPGA_MODE_BIT_UNKNOWN2,
	FPGA_MODE_BIT_MAX = FPGA_MODE_BIT_UNKNOWN2
};

static const uint8_t fpga_register_map_old[FPGA_REGISTER_MAX + 1] = {
	[FPGA_REGISTER_VERSION]			= 0,
	[FPGA_REGISTER_STATUS_CONTROL]		= 1,
	[FPGA_REGISTER_CHANNEL_SELECT_LOW]	= 2,
	[FPGA_REGISTER_CHANNEL_SELECT_HIGH]	= 3,
	[FPGA_REGISTER_SAMPLE_RATE_DIVISOR]	= 4,
	[FPGA_REGISTER_LED_BRIGHTNESS]		= 5,
	[FPGA_REGISTER_PRIMER_DATA1]		= 6,
	[FPGA_REGISTER_PRIMER_CONTROL]		= 7,
	[FPGA_REGISTER_MODE]			= 10,
	[FPGA_REGISTER_PRIMER_DATA2]		= 12,
};

static const uint8_t fpga_register_map_new[FPGA_REGISTER_MAX + 1] = {
	[FPGA_REGISTER_VERSION]			= 7,
	[FPGA_REGISTER_STATUS_CONTROL]		= 15,
	[FPGA_REGISTER_CHANNEL_SELECT_LOW]	= 1,
	[FPGA_REGISTER_CHANNEL_SELECT_HIGH]	= 6,
	[FPGA_REGISTER_SAMPLE_RATE_DIVISOR]	= 11,
	[FPGA_REGISTER_LED_BRIGHTNESS]		= 5,
	[FPGA_REGISTER_PRIMER_DATA1]		= 14,
	[FPGA_REGISTER_PRIMER_CONTROL]		= 2,
	[FPGA_REGISTER_MODE]			= 4,
	[FPGA_REGISTER_PRIMER_DATA2]		= 3,
};

static const uint8_t fpga_status_control_bit_map_old[FPGA_STATUS_CONTROL_BIT_MAX + 1] = {
	[FPGA_STATUS_CONTROL_BIT_RUNNING]	= 0x01,
	[FPGA_STATUS_CONTROL_BIT_UPDATE]	= 0x02,
	[FPGA_STATUS_CONTROL_BIT_UNKNOWN1]	= 0x08,
	[FPGA_STATUS_CONTROL_BIT_OVERFLOW]	= 0x20,
	[FPGA_STATUS_CONTROL_BIT_UNKNOWN2]	= 0x40,
};

static const uint8_t fpga_status_control_bit_map_new[FPGA_STATUS_CONTROL_BIT_MAX + 1] = {
	[FPGA_STATUS_CONTROL_BIT_RUNNING]	= 0x20,
	[FPGA_STATUS_CONTROL_BIT_UPDATE]	= 0x08,
	[FPGA_STATUS_CONTROL_BIT_UNKNOWN1]	= 0x10,
	[FPGA_STATUS_CONTROL_BIT_OVERFLOW]	= 0x01,
	[FPGA_STATUS_CONTROL_BIT_UNKNOWN2]	= 0x04,
};

static const uint8_t fpga_mode_bit_map_old[FPGA_MODE_BIT_MAX + 1] = {
	[FPGA_MODE_BIT_CLOCK]		= 0x01,
	[FPGA_MODE_BIT_UNKNOWN1]	= 0x40,
	[FPGA_MODE_BIT_UNKNOWN2]	= 0x80,
};

static const uint8_t fpga_mode_bit_map_new[FPGA_MODE_BIT_MAX + 1] = {
	[FPGA_MODE_BIT_CLOCK]		= 0x04,
	[FPGA_MODE_BIT_UNKNOWN1]	= 0x80,
	[FPGA_MODE_BIT_UNKNOWN2]	= 0x01,
};

#define FPGA_REG(x) \
	(devc->fpga_register_map[FPGA_REGISTER_ ## x])

#define FPGA_STATUS_CONTROL(x) \
	(devc->fpga_status_control_bit_map[FPGA_STATUS_CONTROL_BIT_ ## x])

#define FPGA_MODE(x) \
	(devc->fpga_mode_bit_map[FPGA_MODE_BIT_ ## x])

static void encrypt(uint8_t *dest, const uint8_t *src, uint8_t cnt)
{
	uint8_t state1 = 0x9b, state2 = 0x54;
	uint8_t t, v;
	int i;

	for (i = 0; i < cnt; i++) {
		v = src[i];
		t = (((v ^ state2 ^ 0x2b) - 0x05) ^ 0x35) - 0x39;
		t = (((t ^ state1 ^ 0x5a) - 0xb0) ^ 0x38) - 0x45;
		dest[i] = state2 = t;
		state1 = v;
	}
}

static void decrypt(uint8_t *dest, const uint8_t *src, uint8_t cnt)
{
	uint8_t state1 = 0x9b, state2 = 0x54;
	uint8_t t, v;
	int i;

	for (i = 0; i < cnt; i++) {
		v = src[i];
		t = (((v + 0x45) ^ 0x38) + 0xb0) ^ 0x5a ^ state1;
		t = (((t + 0x39) ^ 0x35) + 0x05) ^ 0x2b ^ state2;
		dest[i] = state1 = t;
		state2 = v;
	}
}

static int do_ep1_command(const struct sr_dev_inst *sdi,
			  const uint8_t *command, uint8_t cmd_len,
			  uint8_t *reply, uint8_t reply_len)
{
	uint8_t buf[64];
	struct sr_usb_dev_inst *usb;
	int ret, xfer;

	usb = sdi->conn;

	if (cmd_len < 1 || cmd_len > 64 || reply_len > 64 ||
	    !command || (reply_len > 0 && !reply))
		return SR_ERR_ARG;

	encrypt(buf, command, cmd_len);

	ret = libusb_bulk_transfer(usb->devhdl, 1, buf, cmd_len, &xfer, 1000);
	if (ret != 0) {
		sr_dbg("Failed to send EP1 command 0x%02x: %s.",
		       command[0], libusb_error_name(ret));
		return SR_ERR;
	}
	if (xfer != cmd_len) {
		sr_dbg("Failed to send EP1 command 0x%02x: incorrect length "
		       "%d != %d.", xfer, cmd_len);
		return SR_ERR;
	}

	if (reply_len == 0)
		return SR_OK;

	ret = libusb_bulk_transfer(usb->devhdl, 0x80 | 1, buf, reply_len,
				   &xfer, 1000);
	if (ret != 0) {
		sr_dbg("Failed to receive reply to EP1 command 0x%02x: %s.",
		       command[0], libusb_error_name(ret));
		return SR_ERR;
	}
	if (xfer != reply_len) {
		sr_dbg("Failed to receive reply to EP1 command 0x%02x: "
		       "incorrect length %d != %d.", xfer, reply_len);
		return SR_ERR;
	}

	decrypt(reply, buf, reply_len);

	return SR_OK;
}

static int read_eeprom(const struct sr_dev_inst *sdi,
		       uint8_t address, uint8_t length, uint8_t *buf)
{
	uint8_t command[5] = {
		COMMAND_READ_EEPROM,
		READ_EEPROM_COOKIE1,
		READ_EEPROM_COOKIE2,
		address,
		length,
	};

	return do_ep1_command(sdi, command, 5, buf, length);
}

static int upload_led_table(const struct sr_dev_inst *sdi,
			    const uint8_t *table, uint8_t offset, uint8_t cnt)
{
	uint8_t chunk, command[64];
	int ret;

	if (cnt < 1 || cnt + offset > 64 || !table)
		return SR_ERR_ARG;

	while (cnt > 0) {
		chunk = (cnt > 32 ? 32 : cnt);

		command[0] = COMMAND_WRITE_LED_TABLE;
		command[1] = offset;
		command[2] = chunk;
		memcpy(command + 3, table, chunk);

		ret = do_ep1_command(sdi, command, 3 + chunk, NULL, 0);
		if (ret != SR_OK)
			return ret;

		table += chunk;
		offset += chunk;
		cnt -= chunk;
	}

	return SR_OK;
}

static int set_led_mode(const struct sr_dev_inst *sdi,
			uint8_t animate, uint16_t t2reload, uint8_t div,
			uint8_t repeat)
{
	uint8_t command[6] = {
		COMMAND_SET_LED_MODE,
		animate,
		t2reload & 0xff,
		t2reload >> 8,
		div,
		repeat,
	};

	return do_ep1_command(sdi, command, 6, NULL, 0);
}

static int read_fpga_register(const struct sr_dev_inst *sdi,
			      uint8_t address, uint8_t *value)
{
	uint8_t command[3] = {
		COMMAND_FPGA_READ_REGISTER,
		1,
		address,
	};

	return do_ep1_command(sdi, command, 3, value, 1);
}

static int write_fpga_registers(const struct sr_dev_inst *sdi,
				uint8_t (*regs)[2], uint8_t cnt)
{
	uint8_t command[64];
	int i;

	if (cnt < 1 || cnt > 31)
		return SR_ERR_ARG;

	command[0] = COMMAND_FPGA_WRITE_REGISTER;
	command[1] = cnt;
	for (i = 0; i < cnt; i++) {
		command[2 + 2 * i] = regs[i][0];
		command[3 + 2 * i] = regs[i][1];
	}

	return do_ep1_command(sdi, command, 2 * (cnt + 1), NULL, 0);
}

static int write_fpga_register(const struct sr_dev_inst *sdi,
			       uint8_t address, uint8_t value)
{
	uint8_t regs[2] = { address, value };

	return write_fpga_registers(sdi, &regs, 1);
}

static uint8_t map_eeprom_data(uint8_t v)
{
	return (((v ^ 0x80) + 0x44) ^ 0xd5) + 0x69;
}

static int setup_register_mapping(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	if (devc->fpga_variant != FPGA_VARIANT_MCUPRO) {
		uint8_t reg0, reg7;

		/*
		 * Check for newer bitstream version by polling the
		 * version register at the old and new location.
		 */

		if ((ret = read_fpga_register(sdi, 0 /* No mapping */, &reg0)) != SR_OK)
			return ret;

		if ((ret = read_fpga_register(sdi, 7 /* No mapping */, &reg7)) != SR_OK)
			return ret;

		if (reg0 == 0 && reg7 > 0x10) {
			sr_info("Original Saleae Logic16 using new bitstream.");
			devc->fpga_variant = FPGA_VARIANT_ORIGINAL_NEW_BITSTREAM;
		} else {
			sr_info("Original Saleae Logic16 using old bitstream.");
			devc->fpga_variant = FPGA_VARIANT_ORIGINAL;
		}
	}

	if (devc->fpga_variant == FPGA_VARIANT_ORIGINAL_NEW_BITSTREAM) {
		devc->fpga_register_map = fpga_register_map_new;
		devc->fpga_status_control_bit_map = fpga_status_control_bit_map_new;
		devc->fpga_mode_bit_map = fpga_mode_bit_map_new;
	} else {
		devc->fpga_register_map = fpga_register_map_old;
		devc->fpga_status_control_bit_map = fpga_status_control_bit_map_old;
		devc->fpga_mode_bit_map = fpga_mode_bit_map_old;
	}

	return SR_OK;
}

static int prime_fpga(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint8_t eeprom_data[16];
	uint8_t old_mode_reg, version;
	uint8_t regs[8][2] = {
		{FPGA_REG(MODE), 0x00},
		{FPGA_REG(MODE), FPGA_MODE(UNKNOWN1)},
		{FPGA_REG(PRIMER_DATA2), 0},
		{FPGA_REG(MODE), FPGA_MODE(UNKNOWN1) | FPGA_MODE(UNKNOWN2)},
		{FPGA_REG(MODE), FPGA_MODE(UNKNOWN1)},
		{FPGA_REG(PRIMER_DATA1), 0},
		{FPGA_REG(PRIMER_CONTROL), 1},
		{FPGA_REG(PRIMER_CONTROL), 0}
	};
	int i, ret;

	if ((ret = read_eeprom(sdi, 16, 16, eeprom_data)) != SR_OK)
		return ret;

	if ((ret = read_fpga_register(sdi, FPGA_REG(MODE), &old_mode_reg)) != SR_OK)
		return ret;

	regs[0][1] = (old_mode_reg &= ~FPGA_MODE(UNKNOWN2));
	regs[1][1] |= old_mode_reg;
	regs[3][1] |= old_mode_reg;
	regs[4][1] |= old_mode_reg;

	for (i = 0; i < 16; i++) {
		regs[2][1] = eeprom_data[i];
		regs[5][1] = map_eeprom_data(eeprom_data[i]);
		if (i)
			ret = write_fpga_registers(sdi, &regs[2], 6);
		else
			ret = write_fpga_registers(sdi, &regs[0], 8);
		if (ret != SR_OK)
			return ret;
	}

	if ((ret = write_fpga_register(sdi, FPGA_REG(MODE), old_mode_reg)) != SR_OK)
		return ret;

	if ((ret = read_fpga_register(sdi, FPGA_REG(VERSION), &version)) != SR_OK)
		return ret;

	if (version != 0x10 && version != 0x13 && version != 0x40 && version != 0x41) {
		sr_err("Unsupported FPGA version: 0x%02x.", version);
		return SR_ERR;
	}

	return SR_OK;
}

static void make_heartbeat(uint8_t *table, int len)
{
	int i, j;

	memset(table, 0, len);
	len >>= 3;
	for (i = 0; i < 2; i++)
		for (j = 0; j < len; j++)
			*table++ = sin(j * G_PI / len) * 255;
}

static int configure_led(const struct sr_dev_inst *sdi)
{
	uint8_t table[64];
	int ret;

	make_heartbeat(table, 64);
	if ((ret = upload_led_table(sdi, table, 0, 64)) != SR_OK)
		return ret;

	return set_led_mode(sdi, 1, 6250, 0, 1);
}

static int upload_fpga_bitstream(const struct sr_dev_inst *sdi,
				 enum voltage_range vrange)
{
	struct dev_context *devc;
	int offset, chunksize, ret;
	const char *filename;
	uint8_t len, buf[256 * 62], command[64];
	FILE *fw;

	devc = sdi->priv;

	if (devc->cur_voltage_range == vrange)
		return SR_OK;

	if (devc->fpga_variant != FPGA_VARIANT_MCUPRO) {
		switch (vrange) {
		case VOLTAGE_RANGE_18_33_V:
			filename = FPGA_FIRMWARE_18;
			break;
		case VOLTAGE_RANGE_5_V:
			filename = FPGA_FIRMWARE_33;
			break;
		default:
			sr_err("Unsupported voltage range.");
			return SR_ERR;
		}

		sr_info("Uploading FPGA bitstream at %s.", filename);
		if (!(fw = g_fopen(filename, "rb"))) {
			sr_err("Unable to open bitstream file %s for reading: %s.",
			       filename, strerror(errno));
			return SR_ERR;
		}

		buf[0] = COMMAND_FPGA_UPLOAD_INIT;
		if ((ret = do_ep1_command(sdi, buf, 1, NULL, 0)) != SR_OK) {
			fclose(fw);
			return ret;
		}

		while (1) {
			chunksize = fread(buf, 1, sizeof(buf), fw);
			if (chunksize == 0)
				break;

			for (offset = 0; offset < chunksize; offset += 62) {
				len = (offset + 62 > chunksize ?
					chunksize - offset : 62);
				command[0] = COMMAND_FPGA_UPLOAD_SEND_DATA;
				command[1] = len;
				memcpy(command + 2, buf + offset, len);
				ret = do_ep1_command(sdi, command, len + 2, NULL, 0);
				if (ret != SR_OK) {
					fclose(fw);
					return ret;
				}
			}

			sr_info("Uploaded %d bytes.", chunksize);
		}
		fclose(fw);
		sr_info("FPGA bitstream upload done.");
	}

	/* This needs to be called before accessing any FPGA registers. */
	if ((ret = setup_register_mapping(sdi)) != SR_OK)
		return ret;

	if ((ret = prime_fpga(sdi)) != SR_OK)
		return ret;

	if ((ret = configure_led(sdi)) != SR_OK)
		return ret;

	devc->cur_voltage_range = vrange;
	return SR_OK;
}

static int abort_acquisition_sync(const struct sr_dev_inst *sdi)
{
	static const uint8_t command[2] = {
		COMMAND_ABORT_ACQUISITION_SYNC,
		ABORT_ACQUISITION_SYNC_PATTERN,
	};
	uint8_t reply, expected_reply;
	int ret;

	if ((ret = do_ep1_command(sdi, command, 2, &reply, 1)) != SR_OK)
		return ret;

	expected_reply = ~command[1];
	if (reply != expected_reply) {
		sr_err("Invalid response for abort acquisition command: "
		       "0x%02x != 0x%02x.", reply, expected_reply);
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int logic16_setup_acquisition(const struct sr_dev_inst *sdi,
			     uint64_t samplerate, uint16_t channels)
{
	uint8_t clock_select, sta_con_reg, mode_reg;
	uint64_t div;
	int i, ret, nchan = 0;
	struct dev_context *devc;

	devc = sdi->priv;

	if (samplerate == 0 || samplerate > MAX_SAMPLE_RATE) {
		sr_err("Unable to sample at %" PRIu64 "Hz.", samplerate);
		return SR_ERR;
	}

	if (BASE_CLOCK_0_FREQ % samplerate == 0 &&
	    (div = BASE_CLOCK_0_FREQ / samplerate) <= 256) {
		clock_select = 0;
	} else if (BASE_CLOCK_1_FREQ % samplerate == 0 &&
		   (div = BASE_CLOCK_1_FREQ / samplerate) <= 256) {
		clock_select = 1;
	} else {
		sr_err("Unable to sample at %" PRIu64 "Hz.", samplerate);
		return SR_ERR;
	}

	for (i = 0; i < 16; i++)
		if (channels & (1U << i))
			nchan++;

	if ((nchan >= 13 && samplerate > MAX_13CH_SAMPLE_RATE) ||
	    (nchan >= 10 && samplerate > MAX_10CH_SAMPLE_RATE) ||
	    (nchan >= 8  && samplerate > MAX_8CH_SAMPLE_RATE) ||
	    (nchan >= 7  && samplerate > MAX_7CH_SAMPLE_RATE) ||
	    (nchan >= 4  && samplerate > MAX_4CH_SAMPLE_RATE)) {
		sr_err("Unable to sample at %" PRIu64 "Hz "
		       "with this many channels.", samplerate);
		return SR_ERR;
	}

	ret = upload_fpga_bitstream(sdi, devc->selected_voltage_range);
	if (ret != SR_OK)
		return ret;

	if ((ret = read_fpga_register(sdi, FPGA_REG(STATUS_CONTROL), &sta_con_reg)) != SR_OK)
		return ret;

	/* Ignore FIFO overflow on previous capture */
	sta_con_reg &= ~FPGA_STATUS_CONTROL(OVERFLOW);

	if (devc->fpga_variant != FPGA_VARIANT_MCUPRO && sta_con_reg != FPGA_STATUS_CONTROL(UNKNOWN1)) {
		sr_dbg("Invalid state at acquisition setup register 1: 0x%02x != 0x%02x. "
		       "Proceeding anyway.", sta_con_reg, FPGA_STATUS_CONTROL(UNKNOWN1));
	}

	if ((ret = write_fpga_register(sdi, FPGA_REG(STATUS_CONTROL), FPGA_STATUS_CONTROL(UNKNOWN2))) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, FPGA_REG(MODE), (clock_select? FPGA_MODE(CLOCK) : 0))) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, FPGA_REG(SAMPLE_RATE_DIVISOR), (uint8_t)(div - 1))) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, FPGA_REG(CHANNEL_SELECT_LOW), (uint8_t)(channels & 0xff))) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, FPGA_REG(CHANNEL_SELECT_HIGH), (uint8_t)(channels >> 8))) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, FPGA_REG(STATUS_CONTROL), FPGA_STATUS_CONTROL(UNKNOWN2) | FPGA_STATUS_CONTROL(UPDATE))) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, FPGA_REG(STATUS_CONTROL), FPGA_STATUS_CONTROL(UNKNOWN2))) != SR_OK)
		return ret;

	if ((ret = read_fpga_register(sdi, FPGA_REG(STATUS_CONTROL), &sta_con_reg)) != SR_OK)
		return ret;

	if (devc->fpga_variant != FPGA_VARIANT_MCUPRO && sta_con_reg != (FPGA_STATUS_CONTROL(UNKNOWN2) | FPGA_STATUS_CONTROL(UNKNOWN1))) {
		sr_dbg("Invalid state at acquisition setup register 1: 0x%02x != 0x%02x. "
		       "Proceeding anyway.", sta_con_reg, FPGA_STATUS_CONTROL(UNKNOWN2) | FPGA_STATUS_CONTROL(UNKNOWN1));
	}

	if ((ret = read_fpga_register(sdi, FPGA_REG(MODE), &mode_reg)) != SR_OK)
		return ret;

	if (devc->fpga_variant != FPGA_VARIANT_MCUPRO && mode_reg != (clock_select? FPGA_MODE(CLOCK) : 0)) {
		sr_dbg("Invalid state at acquisition setup register 10: 0x%02x != 0x%02x. "
		       "Proceeding anyway.", mode_reg, (clock_select? FPGA_MODE(CLOCK) : 0));
	}

	return SR_OK;
}

SR_PRIV int logic16_start_acquisition(const struct sr_dev_inst *sdi)
{
	static const uint8_t command[1] = {
		COMMAND_START_ACQUISITION,
	};
	int ret;
	struct dev_context *devc;

	devc = sdi->priv;

	if ((ret = do_ep1_command(sdi, command, 1, NULL, 0)) != SR_OK)
		return ret;

	return write_fpga_register(sdi, FPGA_REG(STATUS_CONTROL), FPGA_STATUS_CONTROL(UNKNOWN2) | FPGA_STATUS_CONTROL(RUNNING));
}

SR_PRIV int logic16_abort_acquisition(const struct sr_dev_inst *sdi)
{
	static const uint8_t command[1] = {
		COMMAND_ABORT_ACQUISITION_ASYNC,
	};
	int ret;
	uint8_t sta_con_reg;
	struct dev_context *devc;

	devc = sdi->priv;

	if ((ret = do_ep1_command(sdi, command, 1, NULL, 0)) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, FPGA_REG(STATUS_CONTROL), 0x00)) != SR_OK)
		return ret;

	if ((ret = read_fpga_register(sdi, FPGA_REG(STATUS_CONTROL), &sta_con_reg)) != SR_OK)
		return ret;

	if (devc->fpga_variant != FPGA_VARIANT_MCUPRO && (sta_con_reg & ~FPGA_STATUS_CONTROL(OVERFLOW)) != FPGA_STATUS_CONTROL(UNKNOWN1)) {
		sr_dbg("Invalid state at acquisition stop: 0x%02x != 0x%02x.", sta_con_reg & ~0x20, FPGA_STATUS_CONTROL(UNKNOWN1));
		return SR_ERR;
	}


	if (devc->fpga_variant == FPGA_VARIANT_ORIGINAL) {
		uint8_t reg8, reg9;

		if ((ret = read_fpga_register(sdi, 8, &reg8)) != SR_OK)
			return ret;

		if ((ret = read_fpga_register(sdi, 9, &reg9)) != SR_OK)
			return ret;
	}

	if (devc->fpga_variant != FPGA_VARIANT_MCUPRO && sta_con_reg & FPGA_STATUS_CONTROL(OVERFLOW)) {
		sr_warn("FIFO overflow, capture data may be truncated.");
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int logic16_init_device(const struct sr_dev_inst *sdi)
{
	uint8_t version;
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	devc->cur_voltage_range = VOLTAGE_RANGE_UNKNOWN;

	if ((ret = abort_acquisition_sync(sdi)) != SR_OK)
		return ret;

	if ((ret = read_eeprom(sdi, 8, 8, devc->eeprom_data)) != SR_OK)
		return ret;

	/* mcupro Saleae16 has firmware pre-stored in FPGA.
	   So, we can query it right away. */
	if (read_fpga_register(sdi, 0 /* No mapping */, &version) == SR_OK &&
	    (version == 0x40 || version == 0x41)) {
		sr_info("mcupro Saleae16 detected.");
		devc->fpga_variant = FPGA_VARIANT_MCUPRO;
	} else {
		sr_info("Original Saleae Logic16 detected.");
		devc->fpga_variant = FPGA_VARIANT_ORIGINAL;
	}

	ret = upload_fpga_bitstream(sdi, devc->selected_voltage_range);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static void finish_acquisition(struct sr_dev_inst *sdi)
{
	struct sr_datafeed_packet packet;
	struct dev_context *devc;

	devc = sdi->priv;

	/* Terminate session. */
	packet.type = SR_DF_END;
	sr_session_send(devc->cb_data, &packet);

	/* Remove fds from polling. */
	usb_source_remove(sdi->session, devc->ctx);

	devc->num_transfers = 0;
	g_free(devc->transfers);
	g_free(devc->convbuffer);
	if (devc->stl) {
		soft_trigger_logic_free(devc->stl);
		devc->stl = NULL;
	}
}

static void free_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	unsigned int i;

	sdi = transfer->user_data;
	devc = sdi->priv;

	g_free(transfer->buffer);
	transfer->buffer = NULL;
	libusb_free_transfer(transfer);

	for (i = 0; i < devc->num_transfers; i++) {
		if (devc->transfers[i] == transfer) {
			devc->transfers[i] = NULL;
			break;
		}
	}

	devc->submitted_transfers--;
	if (devc->submitted_transfers == 0)
		finish_acquisition(sdi);
}

static void resubmit_transfer(struct libusb_transfer *transfer)
{
	int ret;

	if ((ret = libusb_submit_transfer(transfer)) == LIBUSB_SUCCESS)
		return;

	free_transfer(transfer);
	/* TODO: Stop session? */

	sr_err("%s: %s", __func__, libusb_error_name(ret));
}

static size_t convert_sample_data(struct dev_context *devc,
		uint8_t *dest, size_t destcnt, const uint8_t *src, size_t srccnt)
{
	uint16_t *channel_data;
	int i, cur_channel;
	size_t ret = 0;
	uint16_t sample, channel_mask;

	srccnt /= 2;

	channel_data = devc->channel_data;
	cur_channel = devc->cur_channel;

	while (srccnt--) {
		sample = src[0] | (src[1] << 8);
		src += 2;

		channel_mask = devc->channel_masks[cur_channel];

		for (i = 15; i >= 0; --i, sample >>= 1)
			if (sample & 1)
				channel_data[i] |= channel_mask;

		if (++cur_channel == devc->num_channels) {
			cur_channel = 0;
			if (destcnt < 16 * 2) {
				sr_err("Conversion buffer too small!");
				break;
			}
			memcpy(dest, channel_data, 16 * 2);
			memset(channel_data, 0, 16 * 2);
			dest += 16 * 2;
			ret += 16;
			destcnt -= 16 * 2;
		}
	}

	devc->cur_channel = cur_channel;

	return ret;
}

SR_PRIV void LIBUSB_CALL logic16_receive_transfer(struct libusb_transfer *transfer)
{
	gboolean packet_has_error = FALSE;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	size_t new_samples, num_samples;
	int trigger_offset;
	int pre_trigger_samples;

	sdi = transfer->user_data;
	devc = sdi->priv;

	/*
	 * If acquisition has already ended, just free any queued up
	 * transfer that come in.
	 */
	if (devc->sent_samples < 0) {
		free_transfer(transfer);
		return;
	}

	sr_info("receive_transfer(): status %s received %d bytes.",
		libusb_error_name(transfer->status), transfer->actual_length);

	switch (transfer->status) {
	case LIBUSB_TRANSFER_NO_DEVICE:
		devc->sent_samples = -2;
		free_transfer(transfer);
		return;
	case LIBUSB_TRANSFER_COMPLETED:
	case LIBUSB_TRANSFER_TIMED_OUT: /* We may have received some data though. */
		break;
	default:
		packet_has_error = TRUE;
		break;
	}

	if (transfer->actual_length & 1) {
		sr_err("Got an odd number of bytes from the device. "
		       "This should not happen.");
		/* Bail out right away. */
		packet_has_error = TRUE;
		devc->empty_transfer_count = MAX_EMPTY_TRANSFERS;
	}

	if (transfer->actual_length == 0 || packet_has_error) {
		devc->empty_transfer_count++;
		if (devc->empty_transfer_count > MAX_EMPTY_TRANSFERS) {
			/*
			 * The FX2 gave up. End the acquisition, the frontend
			 * will work out that the samplecount is short.
			 */
			devc->sent_samples = -2;
			free_transfer(transfer);
		} else {
			resubmit_transfer(transfer);
		}
		return;
	} else {
		devc->empty_transfer_count = 0;
	}

	new_samples = convert_sample_data(devc, devc->convbuffer,
			devc->convbuffer_size, transfer->buffer, transfer->actual_length);

	if (new_samples > 0) {
		if (devc->trigger_fired) {
			/* Send the incoming transfer to the session bus. */
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			if (devc->limit_samples &&
					new_samples > devc->limit_samples - devc->sent_samples)
				new_samples = devc->limit_samples - devc->sent_samples;
			logic.length = new_samples * 2;
			logic.unitsize = 2;
			logic.data = devc->convbuffer;
			sr_session_send(devc->cb_data, &packet);
			devc->sent_samples += new_samples;
		} else {
			trigger_offset = soft_trigger_logic_check(devc->stl,
					devc->convbuffer, new_samples * 2, &pre_trigger_samples);
			if (trigger_offset > -1) {
				devc->sent_samples += pre_trigger_samples;
				packet.type = SR_DF_LOGIC;
				packet.payload = &logic;
				num_samples = new_samples - trigger_offset;
				if (devc->limit_samples &&
						num_samples > devc->limit_samples - devc->sent_samples)
					num_samples = devc->limit_samples - devc->sent_samples;
				logic.length = num_samples * 2;
				logic.unitsize = 2;
				logic.data = devc->convbuffer + trigger_offset * 2;
				sr_session_send(devc->cb_data, &packet);
				devc->sent_samples += num_samples;

				devc->trigger_fired = TRUE;
			}
		}

		if (devc->limit_samples &&
				(uint64_t)devc->sent_samples >= devc->limit_samples) {
			devc->sent_samples = -2;
			free_transfer(transfer);
			return;
		}
	}

	resubmit_transfer(transfer);
}
