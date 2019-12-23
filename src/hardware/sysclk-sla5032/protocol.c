/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Vitaliy Vorobyov
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

/*
 * Register description (all registers are 32bit):
 *
 * Rx - means register with index x (register address is x*4)
 *
 * R0(wr): trigger sel0 (low/high)
 * R0(rd): n*256 samples (post trigger) captured
 *
 * R1(wr): trigger sel1 (level/edge)
 * R1(rd): current sampled value
 *
 * R2(wr): trigger enable mask
 *
 * R2(rd): (status register)
 * b0: 1 - keys entered
 * b1: 1 - triggered
 * b3: 1 - capture done
 *
 * not configured: B6FF9C97, 12FF9C97, 92FF9C97, 16FF9C97, ...
 * configured: A5A5A5A0, after enter keys A5A5A5A1
 *
 * sel1 (one bit per channel):
 *  0 - level triggered
 *  1 - edge triggered
 *
 * sel0 (one bit per channel):
 *  0 - (low level trigger, sel1=0), (falling edge, sel1=1)
 *  1 - (high level trigger, sel1=0), (raising edge, sel1=1)
 *
 * mask (one bit per channel):
 *  0 - disable trigger on channel n
 *  1 - enable trigger on channel n
 *
 * R3: upload base address or num samples (0x300000)
 *
 * R4: pll divisor - 1
 *  0 - div 1 (no division)
 *  1 - div 2
 *  2 - div 3
 *  ...
 *  n-1 - div n
 *
 * R5(rd/wr):
 * b0: 1 - enable pll mul 2, 0 - disable pll mul 2
 * b1: ??
 * b2: ??
 * b3: ??
 * b4:
 * b5: ->0->1 upload next data chunk (to pc)
 * b6: ??
 * b7: 0 - enable pll mul 1.25, 1 - disable pll mul 1.25
 * b8: ??
 *
 * R6: post trigger depth, value x means (x+1)*256 (samples), min value is 1
 * R7: pre trigger depth, value y means (y+1)*256 (samples), min value is 1
 * (x+1)*256 + (y+1)*256 <= 64M
 *
 * R9:  PWM1 HI (1 width-1)
 * R10: PWM1 LO (0 width-1)
 *
 * R11: PWM2 HI (1 width-1)
 * R12: PWM2 LO (0 width-1)
 *
 * R14:
 * 1 - start sample?
 * 0 - upload done?
 *
 * R16: rom key 0
 * R17: rom key 1
 *
 * key0 is F6 81 13 64
 * key1 is 00 00 00 00
 *
 * start sample:
 * r5 <= b2 <= 0
 * r5 <= b3 <= 0
 * r5 <= b5 <= 0
 *
 * r5 <= b6 <= 1
 * r5 <= b1 <= 1
 * r5 <= b1 <= 0
 *
 * r5 <= b8 <= 1
 * r5 <= b8 <= 0
 *
 * r5 <= b6 <= 1
 * r5 <= b2 <= 1
 *
 * read back:
 * r5 <= 0x08  (b3)
 * r5 <= 0x28  (b5,b3)
 */

#define BITSTREAM_NAME "sysclk-sla5032.bit"
#define BITSTREAM_MAX_SIZE (512 * 1024) /* Bitstream size limit for safety */
#define BITSTREAM_HEADER_SIZE 0x69
#define FW_CHUNK_SIZE 250
#define XILINX_SYNC_WORD 0xAA995566

static int la_write_cmd_buf(const struct sr_usb_dev_inst *usb, uint8_t cmd,
		unsigned int addr, unsigned int len, const void *data)
{
	uint8_t *cmd_pkt;
	int ret, xfer_len;
	int cmd_len;

	cmd_pkt = g_try_malloc(len + 10);
	if (!cmd_pkt) {
		ret = SR_ERR_MALLOC;
		goto exit;
	}

	cmd_pkt[0] = cmd;
	cmd_len = 1;
	xfer_len = 0;

	switch(cmd) {
	case CMD_INIT_FW_UPLOAD: /* init firmware upload */
		break;
	case CMD_UPLOAD_FW_CHUNK:
		cmd_pkt[1] = len;
		cmd_len += 1 + len;
		memcpy(&cmd_pkt[2], data, len);
		break;
	case CMD_READ_REG: /* read register */
		cmd_pkt[1] = addr;
		cmd_pkt[2] = len;
		cmd_len += 2;
		break;
	case CMD_WRITE_REG: /* write register */
		cmd_pkt[1] = addr;
		cmd_pkt[2] = len;
		cmd_len += 2 + len;
		memcpy(&cmd_pkt[3], data, len);
		break;
	case CMD_READ_MEM: /* read mem */
		cmd_pkt[1] = (addr >> 8) & 0xFF;
		cmd_pkt[2] = addr & 0xFF;
		cmd_pkt[3] = len;
		cmd_len += 3;
		break;
	case CMD_READ_DATA: /* read samples */
		cmd_pkt[1] = addr;
		cmd_len += 1;
		break;
	}

	ret = libusb_bulk_transfer(usb->devhdl, EP_COMMAND, cmd_pkt, cmd_len,
			&xfer_len, USB_CMD_TIMEOUT_MS);
	if (ret != 0) {
		sr_dbg("Failed to send command %d: %s.",
			   cmd, libusb_error_name(ret));
		return SR_ERR;
	}

	if (xfer_len != cmd_len) {
		sr_dbg("Invalid send command response of length %d.", xfer_len);
		return SR_ERR;
	}

exit:
	g_free(cmd_pkt);
	return ret;
}

static int la_read_reg(const struct sr_usb_dev_inst *usb, unsigned int reg, uint32_t *val)
{
	int ret, xfer_len;
	uint32_t reply;

	ret = la_write_cmd_buf(usb, CMD_READ_REG, reg * sizeof(uint32_t),
			sizeof(reply), NULL); /* rd reg */
	if (ret != SR_OK)
		return ret;

	ret = libusb_bulk_transfer(usb->devhdl, EP_REPLY, (uint8_t *)&reply,
			sizeof(reply), &xfer_len, USB_REPLY_TIMEOUT_MS);
	if (ret != SR_OK)
		return ret;

	if (xfer_len != sizeof(uint32_t)) {
		sr_dbg("Invalid register read response of length %d.", xfer_len);
		return SR_ERR;
	}

	*val = GUINT32_FROM_BE(reply);

	return ret;
}

static int la_write_reg(const struct sr_usb_dev_inst *usb, unsigned int reg, uint32_t val)
{
	uint32_t val_be;

	val_be = GUINT32_TO_BE(val);

	return la_write_cmd_buf(usb, CMD_WRITE_REG, reg * sizeof(uint32_t),
			sizeof(val_be), &val_be); /* wr reg */
}

static int la_read_mem(const struct sr_usb_dev_inst *usb, unsigned int addr, unsigned int len, void *data)
{
	int ret, xfer_len;

	ret = la_write_cmd_buf(usb, CMD_READ_MEM, addr, len, NULL); /* rd mem */
	if (ret != SR_OK)
		return ret;

	xfer_len = 0;
	ret = libusb_bulk_transfer(usb->devhdl, EP_REPLY, (uint8_t *)data,
			len, &xfer_len, USB_REPLY_TIMEOUT_MS);
	if (xfer_len != (int)len) {
		sr_dbg("Invalid memory read response of length %d.", xfer_len);
		return SR_ERR;
	}

	return ret;
}

static int la_read_samples(const struct sr_usb_dev_inst *usb, unsigned int addr)
{
	return la_write_cmd_buf(usb, CMD_READ_DATA, addr, 0, NULL); /* rd samples */
}

static int sla5032_set_depth(const struct sr_usb_dev_inst *usb, uint32_t pre, uint32_t post)
{
	int ret;

	/* (pre + 1)*256 + (post + 1)*256 <= 64*1024*1024 */
	ret = la_write_reg(usb, 7, pre);
	if (ret != SR_OK)
		return ret;

	return la_write_reg(usb, 6, post);
}

static int sla5032_set_triggers(const struct sr_usb_dev_inst *usb,
		uint32_t trg_value, uint32_t trg_edge_mask, uint32_t trg_mask)
{
	int ret;

	sr_dbg("set trigger: val: %08X, e_mask: %08X, mask: %08X.", trg_value,
		trg_edge_mask, trg_mask);

	ret = la_write_reg(usb, 0, trg_value);
	if (ret != SR_OK)
		return ret;

	ret = la_write_reg(usb, 1, trg_edge_mask);
	if (ret != SR_OK)
		return ret;

	return la_write_reg(usb, 2, trg_mask);
}

static int la_set_res_reg_bit(const struct sr_usb_dev_inst *usb,
		unsigned int reg, unsigned int bit, unsigned int set_bit)
{
	int ret;
	uint32_t v;

	v = 0;
	ret = la_read_reg(usb, reg, &v);
	if (ret != SR_OK)
		return ret;

	if (set_bit)
		v |= (1u << bit);
	else
		v &= ~(1u << bit);

	return la_write_reg(usb, reg, v);
}

struct pll_tbl_entry_t
{
	unsigned int sr;
	uint32_t pll_div_minus_1;
	unsigned int pll_mul_flags;
};

enum {
	PLL_MUL2 = 1, /* x2 */
	PLL_MUL1_25 = 2, /* x1.25 */
};

static const struct pll_tbl_entry_t pll_tbl[] = {
	{ 500000000,     0, PLL_MUL2 | PLL_MUL1_25 }, /* 500M = f*2*1.25/1 */
	{ 400000000,     0, PLL_MUL2               }, /* 400M = f*2/1      */
	{ 250000000,     0, PLL_MUL1_25            }, /* 250M = f*1.25/1   */
	{ 200000000,     0, 0                      }, /* 200M = f/1        */
	{ 100000000,     1, 0                      }, /* 100M = f/2        */
	{  50000000,     3, 0                      }, /*  50M = f/4        */
	{  25000000,     7, 0                      }, /*  25M = f/8        */
	{  20000000,     9, 0                      }, /*  20M = f/10       */
	{  10000000,    19, 0                      }, /*  10M = f/20       */
	{   5000000,    39, 0                      }, /*   5M = f/40       */
	{   2000000,    99, 0                      }, /*   2M = f/100      */
	{   1000000,   199, 0                      }, /*   1M = f/200      */
	{    500000,   399, 0                      }, /* 500k = f/400      */
	{    200000,   999, 0                      }, /* 200k = f/1000     */
	{    100000,  1999, 0                      }, /* 100k = f/2000     */
	{     50000,  3999, 0                      }, /*  50k = f/4000     */
	{     20000,  9999, 0                      }, /*  20k = f/10000    */
	{     10000, 19999, 0                      }, /*  10k = f/20000    */
	{      5000, 39999, 0                      }, /*   5k = f/40000    */
	{      2000, 99999, 0                      }, /*   2k = f/100000   */
};

static int sla5032_set_samplerate(const struct sr_usb_dev_inst *usb, unsigned int sr)
{
	int i, ret;
	const struct pll_tbl_entry_t *e;

	e = NULL;
	for (i = 0; i < (int)ARRAY_SIZE(pll_tbl); i++) {
		if (sr == pll_tbl[i].sr) {
			e = &pll_tbl[i];
			break;
		}
	}

	if (!e)
		return SR_ERR_SAMPLERATE;

	sr_dbg("set sample rate: %u.", e->sr);

	ret = la_write_reg(usb, 4, e->pll_div_minus_1);
	if (ret != SR_OK)
		return ret;

	ret = la_set_res_reg_bit(usb, 5, 0,
		(e->pll_mul_flags & PLL_MUL2) ? 1 : 0); /* bit0 (1=en_mul2) */
	if (ret != SR_OK)
		return ret;

	return la_set_res_reg_bit(usb, 5, 7,
		(e->pll_mul_flags & PLL_MUL1_25) ? 0 : 1); /* bit7 (0=en_mul_1.25) */
}

static int sla5032_start_sample(const struct sr_usb_dev_inst *usb)
{
	int ret;
	const unsigned int bits[10][2] = {
		{2, 0}, {3, 0}, {5, 0}, {6, 1}, {1, 1},
		{1, 0}, {8, 1}, {8, 0}, {6, 1}, {2, 1},
	};

	ret = la_write_reg(usb, 14, 1);
	if (ret != SR_OK)
		return ret;

	for (size_t i = 0; i < ARRAY_SIZE(bits); i++) {
		ret = la_set_res_reg_bit(usb, 5, bits[i][0], bits[i][1]);
		if (ret != SR_OK)
			return ret;
	}

	return ret;
}

static int sla5032_get_status(const struct sr_usb_dev_inst *usb, uint32_t status[3])
{
	int ret;
	uint32_t v;

	ret = la_read_reg(usb, 1, &status[0]);
	if (ret != SR_OK)
		return ret;

	status[1] = 1; /* wait trigger */

	ret = la_read_reg(usb, 0, &status[2]);
	if (ret != SR_OK)
		return ret;

	v = 0;
	ret = la_read_reg(usb, 2, &v);
	if (ret != SR_OK)
		return ret;

	if (v & 8) {
		status[1] = 3; /* sample done */
		sr_dbg("get status, reg2: %08X.", v);
	} else if (v & 2) {
		status[1] = 2; /* triggered */
	}

	return ret;
}

static int la_read_samples_data(const struct sr_usb_dev_inst *usb, void *buf,
		unsigned int len, int *xfer_len)
{
	return libusb_bulk_transfer(usb->devhdl, EP_DATA, (uint8_t *)buf, len,
			xfer_len, USB_DATA_TIMEOUT_MS);
}

static int sla5032_read_data_chunk(const struct sr_usb_dev_inst *usb,
		void *buf, unsigned int len, int *xfer_len)
{
	int ret;

	ret = la_read_samples(usb, 3);
	if (ret != SR_OK)
		return ret;

	ret = la_write_reg(usb, 3, 0x300000);
	if (ret != SR_OK)
		return ret;

	ret = la_set_res_reg_bit(usb, 5, 4, 0);
	if (ret != SR_OK)
		return ret;

	ret = la_set_res_reg_bit(usb, 5, 4, 1);
	if (ret != SR_OK)
		return ret;

	return la_read_samples_data(usb, buf, len, xfer_len);
}

static int sla5032_set_read_back(const struct sr_usb_dev_inst *usb)
{
	int ret;

	ret = la_write_reg(usb, 5, 0x08);
	if (ret != SR_OK)
		return ret;

	return la_write_reg(usb, 5, 0x28);
}

static int sla5032_set_pwm1(const struct sr_usb_dev_inst *usb, uint32_t hi, uint32_t lo)
{
	int ret;

	ret = la_write_reg(usb, 9, hi);
	if (ret != SR_OK)
		return ret;

	return la_write_reg(usb, 10, lo);
}

static int sla5032_set_pwm2(const struct sr_usb_dev_inst *usb, uint32_t hi, uint32_t lo)
{
	int ret;

	ret = la_write_reg(usb, 11, hi);
	if (ret != SR_OK)
		return ret;

	return la_write_reg(usb, 12, lo);
}

static int sla5032_write_reg14_zero(const struct sr_usb_dev_inst *usb)
{
	return la_write_reg(usb, 14, 0);
}

static int la_cfg_fpga_done(const struct sr_usb_dev_inst *usb, unsigned int addr)
{
	uint8_t done_key[8];
	uint32_t k0, k1;
	unsigned int reg2;
	int ret;

	memset(done_key, 0, sizeof(done_key));

	ret = la_read_mem(usb, addr, sizeof(done_key), done_key); /* read key from eeprom */
	if (ret != SR_OK)
		return ret;

	k0 = RL32(done_key);	 /* 0x641381F6 */
	k1 = RL32(done_key + 4); /* 0x00000000 */

	sr_dbg("cfg fpga done, k0: %08X, k1: %08X.", k0, k1);

	ret = la_write_reg(usb, 16, k0);
	if (ret != SR_OK)
		return ret;

	ret = la_write_reg(usb, 17, k1);
	if (ret != SR_OK)
		return ret;

	reg2 = 0;
	ret = la_read_reg(usb, 2, &reg2);

	sr_dbg("cfg fpga done, reg2: %08X.", reg2);

	return ret;
}

/*
 * Load a bitstream file into memory. Returns a newly allocated array
 * consisting of a 32-bit length field followed by the bitstream data.
 */
static unsigned char *load_bitstream(struct sr_context *ctx,
					const char *name, int *length_p)
{
	struct sr_resource fw;
	unsigned char *stream, *fw_data;
	ssize_t length, count;

	if (sr_resource_open(ctx, &fw, SR_RESOURCE_FIRMWARE, name) != SR_OK)
		return NULL;

	if (fw.size <= BITSTREAM_HEADER_SIZE || fw.size > BITSTREAM_MAX_SIZE) {
		sr_err("Refusing to load bitstream of unreasonable size "
			   "(%" PRIu64 " bytes).", fw.size);
		sr_resource_close(ctx, &fw);
		return NULL;
	}

	stream = g_try_malloc(fw.size);
	if (!stream) {
		sr_err("Failed to allocate bitstream buffer.");
		sr_resource_close(ctx, &fw);
		return NULL;
	}

	count = sr_resource_read(ctx, &fw, stream, fw.size);
	sr_resource_close(ctx, &fw);

	if (count != (ssize_t)fw.size) {
		sr_err("Failed to read bitstream '%s'.", name);
		g_free(stream);
		return NULL;
	}

	if (RB32(stream + BITSTREAM_HEADER_SIZE) != XILINX_SYNC_WORD) {
		sr_err("Invalid bitstream signature.");
		g_free(stream);
		return NULL;
	}

	length = fw.size - BITSTREAM_HEADER_SIZE + 0x100;
	fw_data = g_try_malloc(length);
	if (!fw_data) {
		sr_err("Failed to allocate bitstream aligned buffer.");
		return NULL;
	}

	memset(fw_data, 0xFF, 0x100);
	memcpy(fw_data + 0x100, stream + BITSTREAM_HEADER_SIZE,
			fw.size - BITSTREAM_HEADER_SIZE);
	g_free(stream);

	*length_p = length;

	return fw_data;
}

static int sla5032_is_configured(const struct sr_usb_dev_inst *usb, gboolean *is_configured)
{
	int ret;
	uint32_t reg2;

	reg2 = 0;
	ret = la_read_reg(usb, 2, &reg2);
	if (ret == SR_OK)
		*is_configured = (reg2 & 0xFFFFFFF1) == 0xA5A5A5A1 ? TRUE : FALSE;

	return ret;
}

/* Load a Binary File from the firmware directory, transfer it to the device. */
static int sla5032_send_bitstream(struct sr_context *ctx,
		const struct sr_usb_dev_inst *usb, const char *name)
{
	unsigned char *stream;
	int ret, length, i, n, m;
	uint32_t reg2;

	if (!ctx || !usb || !name)
		return SR_ERR_BUG;

	stream = load_bitstream(ctx, name, &length);
	if (!stream)
		return SR_ERR;

	sr_dbg("Downloading FPGA bitstream '%s'.", name);

	reg2 = 0;
	ret = la_read_reg(usb, 2, &reg2);
	sr_dbg("send bitstream, reg2: %08X.", reg2);

	/* Transfer the entire bitstream in one URB. */
	ret = la_write_cmd_buf(usb, CMD_INIT_FW_UPLOAD, 0, 0, NULL); /* init firmware upload */
	if (ret != SR_OK) {
		g_free(stream);
		return ret;
	}

	n = length / FW_CHUNK_SIZE;
	m = length % FW_CHUNK_SIZE;

	for (i = 0; i < n; i++) {
		/* upload firmware chunk */
		ret = la_write_cmd_buf(usb, CMD_UPLOAD_FW_CHUNK, 0,
				FW_CHUNK_SIZE, &stream[i * FW_CHUNK_SIZE]);

		if (ret != SR_OK) {
			g_free(stream);
			return ret;
		}
	}

	if (m != 0) {
		/* upload firmware last chunk */
		ret = la_write_cmd_buf(usb, CMD_UPLOAD_FW_CHUNK, 0, m,
				&stream[n * FW_CHUNK_SIZE]);

		if (ret != SR_OK) {
			g_free(stream);
			return ret;
		}
	}

	g_free(stream);

	la_cfg_fpga_done(usb, 4000);

	sla5032_write_reg14_zero(usb);

	sr_dbg("FPGA bitstream download of %d bytes done.", length);

	return SR_OK;
}

/* Select and transfer FPGA bitstream for the current configuration. */
SR_PRIV int sla5032_apply_fpga_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct drv_context *drvc;
	int ret;
	gboolean is_configured;

	devc = sdi->priv;
	drvc = sdi->driver->context;

	if (FPGA_NOCONF != devc->active_fpga_config)
		return SR_OK; /* No change. */

	is_configured = FALSE;
	ret = sla5032_is_configured(sdi->conn, &is_configured);
	if (ret != SR_OK)
		return ret;

	if (is_configured) {
		devc->active_fpga_config = FPGA_CONF;
		return ret;
	}

	sr_dbg("FPGA not configured, send bitstream.");
	ret = sla5032_send_bitstream(drvc->sr_ctx, sdi->conn, BITSTREAM_NAME);
	devc->active_fpga_config = (ret == SR_OK) ? FPGA_CONF : FPGA_NOCONF;

	return ret;
}

/* Callback handling data */
static int la_prepare_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	int i, j, ret, xfer_len;
	uint8_t *rle_buf, *samples;
	const uint8_t *p, *q;
	uint16_t rle_count;
	int samples_count, rle_samples_count;
	uint32_t status[3];
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint32_t value;
	int trigger_offset;

	enum {
		RLE_SAMPLE_SIZE = sizeof(uint32_t) + sizeof(uint16_t),
		RLE_SAMPLES_COUNT = 0x100000,
		RLE_BUF_SIZE = RLE_SAMPLES_COUNT * RLE_SAMPLE_SIZE,
		RLE_END_MARKER = 0xFFFF,
	};

	(void)fd;
	(void)revents;

	sdi = cb_data;
	devc = sdi->priv;
	usb = sdi->conn;

	memset(status, 0, sizeof(status));
	ret = sla5032_get_status(usb, status);
	if (ret != SR_OK) {
		sla5032_write_reg14_zero(usb);
		sr_dev_acquisition_stop(sdi);
		return G_SOURCE_CONTINUE;
	}

	/* data not ready (acquision in progress) */
	if (status[1] != 3)
		return G_SOURCE_CONTINUE;

	sr_dbg("acquision done, status: %u.", (unsigned int)status[2]);

	/* data ready (download, decode and send to sigrok) */
	ret = sla5032_set_read_back(usb);
	if (ret != SR_OK) {
		sla5032_write_reg14_zero(usb);
		sr_dev_acquisition_stop(sdi);
		return G_SOURCE_CONTINUE;
	}

	rle_buf = g_try_malloc(RLE_BUF_SIZE);
	if (rle_buf == NULL) {
		sla5032_write_reg14_zero(usb);
		sr_dev_acquisition_stop(sdi);
		return G_SOURCE_CONTINUE;
	}

	do {
		xfer_len = 0;
		ret = sla5032_read_data_chunk(usb, rle_buf, RLE_BUF_SIZE, &xfer_len);
		if (ret != SR_OK) {
			sla5032_write_reg14_zero(usb);
			g_free(rle_buf);
			sr_dev_acquisition_stop(sdi);

			sr_dbg("acquision done, ret: %d.", ret);
			return G_SOURCE_CONTINUE;
		}

		sr_dbg("acquision done, xfer_len: %d.", xfer_len);

		if (xfer_len == 0) {
			sla5032_write_reg14_zero(usb);
			g_free(rle_buf);
			sr_dev_acquisition_stop(sdi);
			return G_SOURCE_CONTINUE;
		}

		p = rle_buf;
		samples_count = 0;
		rle_samples_count = xfer_len / RLE_SAMPLE_SIZE;

		sr_dbg("acquision done, rle_samples_count: %d.", rle_samples_count);

		for (i = 0; i < rle_samples_count; i++) {
			p += sizeof(uint32_t); /* skip sample value */

			rle_count = RL16(p); /* read RLE counter */
			p += sizeof(uint16_t);
			if (rle_count == RLE_END_MARKER) {
				rle_samples_count = i;
				break;
			}
			samples_count += rle_count + 1;
		}
		sr_dbg("acquision done, samples_count: %d.", samples_count);

		if (samples_count == 0) {
			sr_dbg("acquision done, no samples.");
			sla5032_write_reg14_zero(usb);
			g_free(rle_buf);
			sr_dev_acquisition_stop(sdi);
			return G_SOURCE_CONTINUE;
		}

		/* Decode RLE */
		samples = g_try_malloc(samples_count * sizeof(uint32_t));
		if (!samples) {
			sr_dbg("memory allocation error.");
			sla5032_write_reg14_zero(usb);
			g_free(rle_buf);
			sr_dev_acquisition_stop(sdi);
			return G_SOURCE_CONTINUE;
		}

		p = rle_buf;
		q = samples;
		for (i = 0; i < rle_samples_count; i++) {
			value = RL32(p);
			p += sizeof(uint32_t); /* read sample value */

			rle_count = RL16(p); /* read RLE counter */
			p += sizeof(uint16_t);

			if (rle_count == RLE_END_MARKER) {
				sr_dbg("RLE end marker found.");
				break;
			}

			for (j = 0; j <= rle_count; j++) {
				WL32(q, value);
				q += sizeof(uint32_t);
			}
		}

		if (devc->trigger_fired) {
			/* Send the incoming transfer to the session bus. */
			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;

			logic.length = samples_count * sizeof(uint32_t);
			logic.unitsize = sizeof(uint32_t);
			logic.data = samples;
			sr_session_send(sdi, &packet);
		} else {
			trigger_offset = soft_trigger_logic_check(devc->stl,
				samples, samples_count * sizeof(uint32_t), NULL);
			if (trigger_offset > -1) {
				packet.type = SR_DF_LOGIC;
				packet.payload = &logic;
				int num_samples = samples_count - trigger_offset;

				logic.length = num_samples * sizeof(uint32_t);
				logic.unitsize = sizeof(uint32_t);
				logic.data = samples + trigger_offset * sizeof(uint32_t);
				sr_session_send(sdi, &packet);

				devc->trigger_fired = TRUE;
			}
		}

		g_free(samples);
	} while (rle_samples_count == RLE_SAMPLES_COUNT);

	sr_dbg("acquision stop, rle_samples_count < RLE_SAMPLES_COUNT.");

	sla5032_write_reg14_zero(usb);

	sr_dev_acquisition_stop(sdi); /* if all data transfered */

	g_free(rle_buf);

	if (devc->stl) {
		soft_trigger_logic_free(devc->stl);
		devc->stl = NULL;
	}

	return G_SOURCE_CONTINUE;
}

SR_PRIV int sla5032_start_acquisition(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct sr_trigger *trigger;
	int ret;
	enum { poll_interval_ms = 100 };
	uint64_t pre, post;

	devc = sdi->priv;
	usb = sdi->conn;

	if (devc->state != STATE_IDLE) {
		sr_err("Not in idle state, cannot start acquisition.");
		return SR_ERR;
	}

	pre = (devc->limit_samples * devc->capture_ratio) / 100;
	post = devc->limit_samples - pre;

	if ((trigger = sr_session_trigger_get(sdi->session))) {
		devc->stl = soft_trigger_logic_new(sdi, trigger, pre);
		if (!devc->stl) {
			sr_err("stl alloc error.");
			return SR_ERR_MALLOC;
		}
		devc->trigger_fired = FALSE;
	}
	else
		devc->trigger_fired = TRUE;

	sr_dbg("start acquision, smp lim: %" PRIu64 ", cap ratio: %" PRIu64
	       ".", devc->limit_samples, devc->capture_ratio);

	sr_dbg("start acquision, pre: %" PRIu64 ", post: %" PRIu64 ".", pre, post);
	pre /= 256;
	pre = MAX(pre, 2);
	pre--;

	post /= 256;
	post = MAX(post, 2);
	post--;

	sr_dbg("start acquision, pre: %" PRIx64 ", post: %" PRIx64 ".", pre, post);

	/* (x + 1) * 256 (samples)  pre, post */
	ret = sla5032_set_depth(usb, pre, post);
	if (ret != SR_OK)
		return ret;

	ret = sla5032_set_triggers(usb, devc->trigger_values, devc->trigger_edge_mask, devc->trigger_mask);
	if (ret != SR_OK)
		return ret;

	ret = sla5032_set_samplerate(usb, devc->samplerate);
	if (ret != SR_OK)
		return ret;

	/* TODO: make PWM generator as separate configurable subdevice */
	enum {
		pwm1_hi = 20000000 - 1,
		pwm1_lo = 200000 - 1,
		pwm2_hi = 15 - 1,
		pwm2_lo = 5 - 1,
	};

	ret = sla5032_set_pwm1(usb, pwm1_hi, pwm1_lo);
	if (ret != SR_OK)
		return ret;

	ret = sla5032_set_pwm2(usb, pwm2_hi, pwm2_lo);
	if (ret != SR_OK)
		return ret;

	ret = sla5032_start_sample(usb);
	if (ret != SR_OK)
		return ret;

	sr_session_source_add(sdi->session, -1, 0, poll_interval_ms,
			la_prepare_data, (struct sr_dev_inst *)sdi);

	std_session_send_df_header(sdi);

	return ret;
}
