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

#include "protocol.h"

#include <stdint.h>
#include <string.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <errno.h>
#include <math.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

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
	    command == NULL || (reply_len > 0 && reply == NULL))
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

	if (cnt < 1 || cnt + offset > 64 || table == NULL)
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

static int prime_fpga(const struct sr_dev_inst *sdi)
{
	uint8_t eeprom_data[16];
	uint8_t old_reg_10, version;
	uint8_t regs[8][2] = {
		{10, 0x00},
		{10, 0x40},
		{12, 0},
		{10, 0xc0},
		{10, 0x40},
		{6, 0},
		{7, 1},
		{7, 0}
	};
	int i, ret;

	if ((ret = read_eeprom(sdi, 16, 16, eeprom_data)) != SR_OK)
		return ret;

	if ((ret = read_fpga_register(sdi, 10, &old_reg_10)) != SR_OK)
		return ret;

	regs[0][1] = (old_reg_10 &= 0x7f);
	regs[1][1] |= old_reg_10;
	regs[3][1] |= old_reg_10;
	regs[4][1] |= old_reg_10;

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

	if ((ret = write_fpga_register(sdi, 10, old_reg_10)) != SR_OK)
		return ret;

	if ((ret = read_fpga_register(sdi, 0, &version)) != SR_OK)
		return ret;

	if (version != 0x10) {
		sr_err("Invalid FPGA bitstream version: 0x%02x != 0x10.", version);
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
			*table++ = sin(j * M_PI / len) * 255;
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
	if ((fw = g_fopen(filename, "rb")) == NULL) {
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
	uint8_t clock_select, reg1, reg10;
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

	if ((ret = read_fpga_register(sdi, 1, &reg1)) != SR_OK)
		return ret;

	if (reg1 != 0x08) {
		sr_dbg("Invalid state at acquisition setup: 0x%02x != 0x08.", reg1);
		return SR_ERR;
	}

	if ((ret = write_fpga_register(sdi, 1, 0x40)) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, 10, clock_select)) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, 4, (uint8_t)(div - 1))) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, 2, (uint8_t)(channels & 0xff))) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, 3, (uint8_t)(channels >> 8))) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, 1, 0x42)) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, 1, 0x40)) != SR_OK)
		return ret;

	if ((ret = read_fpga_register(sdi, 1, &reg1)) != SR_OK)
		return ret;

	if (reg1 != 0x48) {
		sr_dbg("Invalid state at acquisition setup: 0x%02x != 0x48.", reg1);
		return SR_ERR;
	}

	if ((ret = read_fpga_register(sdi, 10, &reg10)) != SR_OK)
		return ret;

	if (reg10 != clock_select) {
		sr_dbg("Invalid state at acquisition setup: 0x%02x != 0x%02x.",
		       reg10, clock_select);
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int logic16_start_acquisition(const struct sr_dev_inst *sdi)
{
	static const uint8_t command[1] = {
		COMMAND_START_ACQUISITION,
	};
	int ret;

	if ((ret = do_ep1_command(sdi, command, 1, NULL, 0)) != SR_OK)
		return ret;

	return write_fpga_register(sdi, 1, 0x41);
}

SR_PRIV int logic16_abort_acquisition(const struct sr_dev_inst *sdi)
{
	static const uint8_t command[1] = {
		COMMAND_ABORT_ACQUISITION_ASYNC,
	};
	int ret;
	uint8_t reg1, reg8, reg9;

	if ((ret = do_ep1_command(sdi, command, 1, NULL, 0)) != SR_OK)
		return ret;

	if ((ret = write_fpga_register(sdi, 1, 0x00)) != SR_OK)
		return ret;

	if ((ret = read_fpga_register(sdi, 1, &reg1)) != SR_OK)
		return ret;

	if (reg1 != 0x08) {
		sr_dbg("Invalid state at acquisition stop: 0x%02x != 0x08.", reg1);
		return SR_ERR;
	}

	if ((ret = read_fpga_register(sdi, 8, &reg8)) != SR_OK)
		return ret;

	if ((ret = read_fpga_register(sdi, 9, &reg9)) != SR_OK)
		return ret;

	return SR_OK;
}

SR_PRIV int logic16_init_device(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;

	devc->cur_voltage_range = VOLTAGE_RANGE_UNKNOWN;

	if ((ret = abort_acquisition_sync(sdi)) != SR_OK)
		return ret;

	if ((ret = read_eeprom(sdi, 8, 8, devc->eeprom_data)) != SR_OK)
		return ret;

	ret = upload_fpga_bitstream(sdi, devc->selected_voltage_range);
	if (ret != SR_OK)
		return ret;

	return SR_OK;
}

static void finish_acquisition(struct dev_context *devc)
{
	struct sr_datafeed_packet packet;

	/* Terminate session. */
	packet.type = SR_DF_END;
	sr_session_send(devc->cb_data, &packet);

	/* Remove fds from polling. */
	usb_source_remove(devc->ctx);

	devc->num_transfers = 0;
	g_free(devc->transfers);
	g_free(devc->convbuffer);
}

static void free_transfer(struct libusb_transfer *transfer)
{
	struct dev_context *devc;
	unsigned int i;

	devc = transfer->user_data;

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
		finish_acquisition(devc);
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

static size_t convert_sample_data_16(struct dev_context *devc,
				     uint8_t *dest, size_t destcnt,
				     const uint8_t *src, size_t srccnt)
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

static size_t convert_sample_data_8(struct dev_context *devc,
				    uint8_t *dest, size_t destcnt,
				    const uint8_t *src, size_t srccnt)
{
	uint8_t *channel_data;
	int i, cur_channel;
	size_t ret = 0;
	uint16_t sample;
	uint8_t channel_mask;

	srccnt /= 2;

	channel_data = (uint8_t *)devc->channel_data;
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
			if (destcnt < 16) {
				sr_err("Conversion buffer too small!");
				break;
			}
			memcpy(dest, channel_data, 16);
			memset(channel_data, 0, 16);
			dest += 16;
			ret += 16;
			destcnt -= 16;
		}
	}

	devc->cur_channel = cur_channel;

	return ret;
}

static size_t convert_sample_data(struct dev_context *devc,
				  uint8_t *dest, size_t destcnt,
				  const uint8_t *src, size_t srccnt,
				  int unitsize)
{
	return (unitsize == 2 ?
		convert_sample_data_16(devc, dest, destcnt, src, srccnt) :
		convert_sample_data_8(devc, dest, destcnt, src, srccnt));
}

SR_PRIV void logic16_receive_transfer(struct libusb_transfer *transfer)
{
	gboolean packet_has_error = FALSE;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	struct dev_context *devc;
	size_t converted_length;

	devc = transfer->user_data;

	/*
	 * If acquisition has already ended, just free any queued up
	 * transfer that come in.
	 */
	if (devc->num_samples < 0) {
		free_transfer(transfer);
		return;
	}

	sr_info("receive_transfer(): status %d received %d bytes.",
		transfer->status, transfer->actual_length);

	switch (transfer->status) {
	case LIBUSB_TRANSFER_NO_DEVICE:
		devc->num_samples = -2;
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
			devc->num_samples = -2;
			free_transfer(transfer);
		} else {
			resubmit_transfer(transfer);
		}
		return;
	} else {
		devc->empty_transfer_count = 0;
	}

	converted_length = convert_sample_data(devc, devc->convbuffer,
				devc->convbuffer_size, transfer->buffer,
				transfer->actual_length, devc->unitsize);

	if (converted_length > 0) {
		/* Cap sample count if needed. */
		if (devc->limit_samples &&
		    (uint64_t)devc->num_samples + converted_length
		    > devc->limit_samples) {
			converted_length =
				devc->limit_samples - devc->num_samples;
		}

		/* Send the incoming transfer to the session bus. */
		packet.type = SR_DF_LOGIC;
		packet.payload = &logic;
		logic.length = converted_length * devc->unitsize;
		logic.unitsize = devc->unitsize;
		logic.data = devc->convbuffer;
		sr_session_send(devc->cb_data, &packet);

		devc->num_samples += converted_length;
		if (devc->limit_samples &&
		    (uint64_t)devc->num_samples >= devc->limit_samples) {
			devc->num_samples = -2;
			free_transfer(transfer);
			return;
		}
	}

	resubmit_transfer(transfer);
}
