/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2017 Jan Luebbe <jluebbe@lasnet.de>
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

#include <config.h>
#include <string.h>
#include "protocol.h"

#define COMMAND_START_CAPTURE	0x01
#define COMMAND_STOP_CAPTURE	0x02
#define COMMAND_READ_EEPROM	0x07
#define COMMAND_WRITE_REG	0x80
#define COMMAND_READ_REG	0x81
#define COMMAND_WRITE_I2C	0x87
#define COMMAND_READ_I2C	0x88
#define COMMAND_WAKE_I2C	0x89
#define COMMAND_READ_FW_VER	0x8b

#define REG_LED_RED		0x0f
#define REG_LED_GREEN		0x10
#define REG_LED_BLUE		0x11

static void iterate_lfsr(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint32_t lfsr = devc->lfsr;
	int i, max;

	max = (lfsr & 0x1f) + 34;
	for (i = 0; i <= max; i++) {
		lfsr = (lfsr >> 1) |		\
			((lfsr ^		\
			  (lfsr >> 1) ^		\
			  (lfsr >> 21) ^	\
			  (lfsr >> 31)		\
			  ) << 31);
	}
	sr_dbg("Iterate 0x%08x -> 0x%08x", devc->lfsr, lfsr);
	devc->lfsr = lfsr;
}

static void encrypt(const struct sr_dev_inst *sdi, const uint8_t *in, uint8_t *out, uint8_t len)
{
	struct dev_context *devc = sdi->priv;
	uint32_t lfsr = devc->lfsr;
	uint8_t value, mask;
	int i;

	for (i = 0; i < len; i++) {
		value = in[i];
		mask = lfsr >> (i%4*8);
		if (i == 0)
			value = (value & 0x28) | ((value ^ mask) & ~0x28);
		else
			value = value ^ mask;
		out[i] = value;
	}
	iterate_lfsr(sdi);
}

static void decrypt(const struct sr_dev_inst *sdi, uint8_t *data, uint8_t len)
{
	struct dev_context *devc = sdi->priv;
	uint32_t lfsr = devc->lfsr;
	int i;

	for (i = 0; i < len; i++) {
		data[i] ^= (lfsr >> (i%4*8));
	}
	iterate_lfsr(sdi);
}

static int transact(const struct sr_dev_inst *sdi,
		    const uint8_t *req, uint8_t req_len,
		    uint8_t *rsp, uint8_t rsp_len)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	uint8_t *req_enc;
	uint8_t rsp_dummy[1] = {};
	int ret, xfer;

	if (req_len < 2 || req_len > 64 || rsp_len > 128 ||
	    !req || (rsp_len > 0 && !rsp))
		return SR_ERR_ARG;

	req_enc = g_malloc(req_len);
	encrypt(sdi, req, req_enc, req_len);

	ret = libusb_bulk_transfer(usb->devhdl, 1, req_enc, req_len, &xfer, 1000);
	if (ret != 0) {
		sr_dbg("Failed to send request 0x%02x: %s.",
		       req[1], libusb_error_name(ret));
		return SR_ERR;
	}
	if (xfer != req_len) {
		sr_dbg("Failed to send request 0x%02x: incorrect length "
		       "%d != %d.", req[1], xfer, req_len);
		return SR_ERR;
	}

	if (req[0] == 0x20) { // reseed
		return SR_OK;
	} else if (rsp_len == 0) {
		rsp = rsp_dummy;
		rsp_len = sizeof(rsp_dummy);
	}

	ret = libusb_bulk_transfer(usb->devhdl, 0x80 | 1, rsp, rsp_len,
				   &xfer, 1000);
	if (ret != 0) {
		sr_dbg("Failed to receive response to request 0x%02x: %s.",
		       req[1], libusb_error_name(ret));
		return SR_ERR;
	}
	if (xfer != rsp_len) {
		sr_dbg("Failed to receive response to request 0x%02x: "
		       "incorrect length %d != %d.", req[1], xfer, rsp_len);
		return SR_ERR;
	}

	decrypt(sdi, rsp, rsp_len);

	return SR_OK;
}

static int reseed(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint8_t req[] = {0x20, 0x24, 0x4b, 0x35, 0x8e};

	devc->lfsr = 0;
	return transact(sdi, req, sizeof(req), NULL, 0);
}

static int write_regs(const struct sr_dev_inst *sdi, uint8_t (*regs)[2], uint8_t cnt)
{
	uint8_t req[64];
	int i;

	if (cnt < 1 || cnt > 30)
		return SR_ERR_ARG;

	req[0] = 0x00;
	req[1] = COMMAND_WRITE_REG;
	req[2] = cnt;

	for (i = 0; i < cnt; i++) {
		req[3 + 2 * i] = regs[i][0];
		req[4 + 2 * i] = regs[i][1];
	}

	return transact(sdi, req, 3 + 2*cnt, NULL, 0);
}

static int write_reg(const struct sr_dev_inst *sdi,
		     uint8_t address, uint8_t value)
{
	uint8_t regs[2] = {address, value};

	return write_regs(sdi, &regs, 1);
}

static int get_firmware_version(const struct sr_dev_inst *sdi)
{
	uint8_t req[2] = {0x00, COMMAND_READ_FW_VER};
	uint8_t rsp[128] = {};
	int ret;

	ret = transact(sdi, req, sizeof(req), rsp, sizeof(rsp));
	if (ret == SR_OK) {
		rsp[63] = 0;
		sr_dbg("fw-version: %s", rsp);
	}

	return ret;
}

static int read_i2c(const struct sr_dev_inst *sdi, uint8_t *data, uint8_t len)
{
	uint8_t req[5];
	uint8_t rsp[1+128];
	int ret;

	if (len < 1 || len > 128 || !data)
		return SR_ERR_ARG;

	req[0] = 0x00;
	req[1] = COMMAND_READ_I2C;
	req[2] = 0xc0; // fixed address
	req[3] = len;
	req[4] = 0; // len msb?

	ret = transact(sdi, req, sizeof(req), rsp, 1 + len);
	if (ret != SR_OK) {
		return ret;
	}
	if (rsp[0] != 0x02) {
		sr_dbg("Failed to do I2C read (0x%02x).", rsp[0]);
		return SR_ERR;
	}

	memcpy(data, rsp+1, len);
	return SR_OK;
}

static int write_i2c(const struct sr_dev_inst *sdi, const uint8_t *data, uint8_t len)
{
	uint8_t req[5 + 128];
	uint8_t rsp[1];
	int ret;

	if (len < 1 || len > 128 || !data)
		return SR_ERR_ARG;

	req[0] = 0x00;
	req[1] = COMMAND_WRITE_I2C;
	req[2] = 0xc0; // fixed address
	req[3] = len;
	req[4] = 0; // len msb?
	memcpy(req + 5, data, len);

	ret = transact(sdi, req, 5 + len, rsp, sizeof(rsp));
	if (ret != SR_OK) {
		return ret;
	}
	if (rsp[0] != 0x02) {
		sr_dbg("Failed to do I2C write (0x%02x).", rsp[0]);
		return SR_ERR;
	}

	return SR_OK;
}

static int wake_i2c(const struct sr_dev_inst *sdi)
{
	uint8_t req[] = {0x00, COMMAND_WAKE_I2C};
	uint8_t rsp[1] = {};
	uint8_t i2c_rsp[1+1+2] = {};
	int ret;

	ret = transact(sdi, req, sizeof(req), rsp, sizeof(rsp));
	if (ret != SR_OK) {
		return ret;
	}
	if (rsp[0] != 0x00) {
		sr_dbg("Failed to do I2C wake trigger (0x%02x).", rsp[0]);
		return SR_ERR;
	}

	ret = read_i2c(sdi, i2c_rsp, sizeof(i2c_rsp));
	if (ret != SR_OK) {
		return ret;
	}
	if (i2c_rsp[1] != 0x11) {
		sr_dbg("Failed to do I2C wake read (0x%02x).", i2c_rsp[0]);
		return SR_ERR;
	}

	return SR_OK;
}

static int crypto_random(const struct sr_dev_inst *sdi, uint8_t *data)
{
	uint8_t i2c_req[8] = {0x03, 0x07, 0x1b, 0x00, 0x00, 0x00, 0x24, 0xcd};
	uint8_t i2c_rsp[1+32+2] = {};
	int ret;

	ret = write_i2c(sdi, i2c_req, sizeof(i2c_req));
	if (ret != SR_OK) {
		return ret;
	}

	g_usleep(100000); // TODO: poll instead

	ret = read_i2c(sdi, i2c_rsp, sizeof(i2c_rsp));
	if (ret != SR_OK) {
		return ret;
	}

	if (data) {
		memcpy(data, i2c_rsp+1, 32);
	}

	return SR_OK;
}

static int crypto_nonce(const struct sr_dev_inst *sdi, uint8_t *data)
{
	uint8_t i2c_req[6+20+2] = {0x03, 0x1b, 0x16, 0x00, 0x00, 0x00};
	uint8_t i2c_rsp[1+32+2] = {};
	int ret;

	// CRC
	i2c_req[26] = 0x7d;
	i2c_req[27] = 0xe0;

	ret = write_i2c(sdi, i2c_req, sizeof(i2c_req));
	if (ret != SR_OK) {
		return ret;
	}

	g_usleep(100000); // TODO: poll instead

	ret = read_i2c(sdi, i2c_rsp, sizeof(i2c_rsp));
	if (ret != SR_OK) {
		return ret;
	}

	if (data) {
		memcpy(data, i2c_rsp+1, 32);
	}

	return SR_OK;
}

static int crypto_sign(const struct sr_dev_inst *sdi, uint8_t *data, uint8_t *crc)
{
	uint8_t i2c_req[8] = {0x03, 0x07, 0x41, 0x80, 0x00, 0x00, 0x28, 0x05};
	uint8_t i2c_rsp[1+64+2] = {};
	int ret;

	ret = write_i2c(sdi, i2c_req, sizeof(i2c_req));
	if (ret != SR_OK) {
		return ret;
	}

	g_usleep(100000); // TODO: poll instead

	ret = read_i2c(sdi, i2c_rsp, sizeof(i2c_rsp));
	if (ret != SR_OK) {
		return ret;
	}

	memcpy(data, i2c_rsp+1, 64);
	memcpy(crc, i2c_rsp+1+64, 2);

	return SR_OK;
}

static int authenticate(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint8_t random[32] = {};
	uint8_t nonce[32] = {};
	uint8_t sig[64] = {};
	uint8_t sig_crc[64] = {};
	uint32_t lfsr;
	int i, ret;

	ret = wake_i2c(sdi);
	if (ret != SR_OK)
		return ret;

	ret = crypto_random(sdi, random);
	if (ret != SR_OK)
		return ret;
	sr_dbg("random: 0x%02x 0x%02x 0x%02x 0x%02x", random[0], random[1], random[2], random[3]);

	ret = crypto_nonce(sdi, nonce);
	if (ret != SR_OK)
		return ret;
	sr_dbg("nonce: 0x%02x 0x%02x 0x%02x 0x%02x", nonce[0], nonce[1], nonce[2], nonce[3]);

	ret = crypto_nonce(sdi, nonce);
	if (ret != SR_OK)
		return ret;
	sr_dbg("nonce: 0x%02x 0x%02x 0x%02x 0x%02x", nonce[0], nonce[1], nonce[2], nonce[3]);

	ret = crypto_sign(sdi, sig, sig_crc);
	if (ret != SR_OK)
		return ret;
	sr_dbg("sig: 0x%02x 0x%02x 0x%02x 0x%02x", sig[0], sig[1], sig[2], sig[3]);
	sr_dbg("sig crc: 0x%02x 0x%02x", sig_crc[0], sig_crc[1]);

	lfsr = 0;
	for (i = 0; i < 28; i++)
		lfsr ^= nonce[i] << (8*(i%4));
	lfsr ^= sig_crc[0] | sig_crc[1] << 8;

	sr_dbg("Authenticate 0x%08x -> 0x%08x", devc->lfsr, lfsr);
	devc->lfsr = lfsr;

	return SR_OK;
}

static int set_led(const struct sr_dev_inst *sdi, uint8_t r, uint8_t g, uint8_t b)
{
	uint8_t regs[][2] = {
		{REG_LED_RED, r},
		{REG_LED_GREEN, g},
		{REG_LED_BLUE, b},
	};

	authenticate(sdi);

	return write_regs(sdi, regs, G_N_ELEMENTS(regs));
}

static int configure_channels(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	const struct sr_channel *c;
	const GSList *l;
	uint16_t mask;

	devc->dig_channel_cnt = 0;
	devc->dig_channel_mask = 0;
	for (l = sdi->channels; l; l = l->next) {
		c = l->data;
		if (!c->enabled)
			continue;

		mask = 1 << c->index;
		devc->dig_channel_masks[devc->dig_channel_cnt++] = mask;
		devc->dig_channel_mask |= mask;

	}
	sr_dbg("%d channels enabled (0x%04x)",
	       devc->dig_channel_cnt,
	       devc->dig_channel_mask);

	return SR_OK;
}

SR_PRIV int saleae_logic_pro_init(const struct sr_dev_inst *sdi)
{
	reseed(sdi);
	get_firmware_version(sdi);
	/* setting the LED doesn't work yet */
	/* set_led(sdi, 0x00, 0x00, 0xff); */

	return SR_OK;
}

SR_PRIV int saleae_logic_pro_prepare(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint8_t regs_unknown[][2] = {
		{0x03, 0x0f},
		{0x04, 0x00},
		{0x05, 0x00},
	};
	uint8_t regs_config[][2] = {
		{0x00, 0x00},
		{0x08, 0x00}, /* analog channel mask (LSB) */
		{0x09, 0x00}, /* analog channel mask (MSB) */
		{0x06, 0x01}, /* digital channel mask (LSB) */
		{0x07, 0x00}, /* digital channel mask (MSB) */
		{0x0a, 0x00}, /* analog sample rate? */
		{0x0b, 0x64}, /* digital sample rate? */
		{0x0c, 0x00},
		{0x0d, 0x00}, /* analog mux rate? */
		{0x0e, 0x01}, /* digital mux rate? */
		{0x12, 0x04},
		{0x13, 0x00},
		{0x14, 0xff}, /* pre-divider? */
	};
	uint8_t start_req[] = {0x00, 0x01};
	uint8_t start_rsp[2] = {};

	configure_channels(sdi);

	/* digital channel mask and muxing */
	regs_config[3][1] = devc->dig_channel_mask;
	regs_config[4][1] = devc->dig_channel_mask >> 8;
	regs_config[9][1] = devc->dig_channel_cnt;

	/* samplerate */
	switch (devc->dig_samplerate) {
	case SR_MHZ(1):
		regs_config[6][1] = 0x64;
		break;
	case SR_MHZ(2):
		regs_config[6][1] = 0x32;
		break;
	case SR_KHZ(2500):
		regs_config[6][1] = 0x28;
		break;
	case SR_MHZ(10):
		regs_config[6][1] = 0x0a;
		break;
	case SR_MHZ(25):
		regs_config[6][1] = 0x04;
		regs_config[12][1] = 0x80;
		break;
	case SR_MHZ(50):
		regs_config[6][1] = 0x02;
		regs_config[12][1] = 0x40;
		break;
	default:
		return SR_ERR_ARG;
	}

	authenticate(sdi);

	write_reg(sdi, 0x15, 0x03);
	write_regs(sdi, regs_unknown, G_N_ELEMENTS(regs_unknown));
	write_regs(sdi, regs_config, G_N_ELEMENTS(regs_config));

	transact(sdi, start_req, sizeof(start_req), start_rsp, sizeof(start_rsp));

	return SR_OK;
}

SR_PRIV int saleae_logic_pro_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	devc->conv_size = 0;
	devc->batch_index = 0;

	write_reg(sdi, 0x00, 0x01);

	return SR_OK;
}

SR_PRIV int saleae_logic_pro_stop(const struct sr_dev_inst *sdi)
{
	uint8_t stop_req[] = {0x00, 0x02};
	uint8_t stop_rsp[2] = {};

	write_reg(sdi, 0x00, 0x00);
	transact(sdi, stop_req, sizeof(stop_req), stop_rsp, sizeof(stop_rsp));

	return SR_OK;
}

static void saleae_logic_pro_send_data(const struct sr_dev_inst *sdi,
				      void *data, size_t length, size_t unitsize)
{
	const struct sr_datafeed_logic logic = {
		.length = length,
		.unitsize = unitsize,
		.data = data
	};

	const struct sr_datafeed_packet packet = {
		.type = SR_DF_LOGIC,
		.payload = &logic
	};

	sr_session_send(sdi, &packet);
}

/*
 * One batch from the device consists of 32 samples per active digital channel.
 * This stream of batches is packed into USB packets with 16384 bytes each.
 */
static void saleae_logic_pro_convert_data(const struct sr_dev_inst *sdi,
					 const uint32_t *src, size_t srccnt)
{
	struct dev_context *devc = sdi->priv;
	uint8_t *dst = devc->conv_buffer;
	uint32_t samples;
	uint16_t channel_mask;
	unsigned int sample_index, batch_index;
	uint16_t *dst_batch;

	/* copy partial batch to the beginning */
	memcpy(dst, dst+devc->conv_size, CONV_BATCH_SIZE);
	/* reset converted size */
	devc->conv_size = 0;

	batch_index = devc->batch_index;
	while (srccnt--) {
		samples = *src++;
		dst_batch = (uint16_t*)dst;

		/* first index of the batch */
		if (batch_index == 0)
			memset(dst, 0, CONV_BATCH_SIZE);

		/* convert one channel */
		channel_mask = devc->dig_channel_masks[batch_index];
		for (sample_index = 0; sample_index <= 31; sample_index++)
			if ((samples >> (31-sample_index)) & 1)
				dst_batch[sample_index] |= channel_mask;

		/* last index of the batch */
		if (++batch_index == devc->dig_channel_cnt) {
			devc->conv_size += CONV_BATCH_SIZE;
			batch_index = 0;
			dst += CONV_BATCH_SIZE;
		}
	}
	devc->batch_index = batch_index;
}

SR_PRIV void LIBUSB_CALL saleae_logic_pro_receive_data(struct libusb_transfer *transfer)
{
	const struct sr_dev_inst *sdi = transfer->user_data;
	struct dev_context *devc = sdi->priv;
	int ret;

	switch (transfer->status) {
	case LIBUSB_TRANSFER_NO_DEVICE:
		sr_dbg("FIXME no device");
		return;
	case LIBUSB_TRANSFER_COMPLETED:
	case LIBUSB_TRANSFER_TIMED_OUT: /* We may have received some data though. */
		break;
	default:
		// FIXME
		return;
	}

	saleae_logic_pro_convert_data(sdi, (uint32_t*)transfer->buffer, 16*1024/4);
	saleae_logic_pro_send_data(sdi, devc->conv_buffer, devc->conv_size, 2);

	if ((ret = libusb_submit_transfer(transfer)) != LIBUSB_SUCCESS)
		sr_dbg("FIXME resubmit failed");
}
