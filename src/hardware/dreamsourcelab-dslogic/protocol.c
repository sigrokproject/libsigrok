/*
 * This file is part of the libsigrok project.
 *
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

#include <config.h>
#include <math.h>
#include <stdbool.h>
#include <glib.h>
#include <glib/gstdio.h>
#include "protocol.h"

/* Protocol commands */
#define DS_CMD_CTL_WR			0xb0
#define DS_CMD_CTL_RD_PRE		0xb1
#define DS_CMD_CTL_RD			0xb2

#define DS_CMD_GET_FW_VERSION		0xb0
#define DS_CMD_GET_REVID_VERSION	0xb1
#define DS_CMD_START			0xb2
#define DS_CMD_CONFIG			0xb3
#define DS_CMD_SETTING			0xb4
#define DS_CMD_CONTROL			0xb5
#define DS_CMD_STATUS			0xb6
#define DS_CMD_STATUS_INFO		0xb7
#define DS_CMD_WR_REG			0xb8
#define DS_CMD_WR_NVM			0xb9
#define DS_CMD_RD_NVM			0xba
#define DS_CMD_RD_NVM_PRE		0xbb
#define DS_CMD_GET_HW_INFO		0xbc

#define DS_I2C_REG_VTH_ADDR		0x78
#define DS_I2C_REG_CTR1_ADDR	0x71
#define DS_I2C_REG_CTR0_ADDR	0x70
#define DS_I2C_REG_COMB_ADDR	0x68
#define DS_I2C_REG_EI2C_ADDR	0x60
#define DS_I2C_REG_VCO_ADDR		0x48
#define DS_I2C_REG_HDL_VERSION_ADDR	0x04

#define DS_START_FLAGS_STOP		(1 << 7)
#define DS_START_FLAGS_CLK_48MHZ	(1 << 6)
#define DS_START_FLAGS_SAMPLE_WIDE	(1 << 5)
#define DS_START_FLAGS_MODE_LA		(1 << 4)

#define DS_ADDR_COMB			0x68
#define DS_ADDR_EEWP			0x70
#define DS_ADDR_VTH			0x78

#define DS_MAX_LOGIC_DEPTH		SR_MHZ(16)
#define DS_MAX_TRIG_PERCENT		90

#define DS_MODE_TRIG_EN			(1 << 0)
#define DS_MODE_CLK_TYPE		(1 << 1)
#define DS_MODE_CLK_EDGE		(1 << 2)
#define DS_MODE_RLE_MODE		(1 << 3)
#define DS_MODE_DSO_MODE		(1 << 4)
#define DS_MODE_HALF_MODE		(1 << 5)
#define DS_MODE_QUAR_MODE		(1 << 6)
#define DS_MODE_ANALOG_MODE		(1 << 7)
#define DS_MODE_FILTER			(1 << 8)
#define DS_MODE_INSTANT			(1 << 9)
#define DS_MODE_STRIG_MODE		(1 << 11)
#define DS_MODE_STREAM_MODE		(1 << 12)
#define DS_MODE_LPB_TEST		(1 << 13)
#define DS_MODE_EXT_TEST		(1 << 14)
#define DS_MODE_INT_TEST		(1 << 15)

// read only
#define DS_GPIF_DONE     (1 << 7)
#define DS_FPGA_DONE     (1 << 6)
#define DS_FPGA_INIT_B   (1 << 5)
// write only
#define DS_CH_CH0        (1 << 7)
#define DS_CH_COM        (1 << 6)
#define DS_CH_CH1        (1 << 5)
// read/write
#define DS_SYS_OVERFLOW  (1 << 4)
#define DS_SYS_CLR       (1 << 3)
#define DS_SYS_EN        (1 << 2)
#define DS_LED_RED       (1 << 1)
#define DS_LED_GREEN     (1 << 0)

#define DS_WR_PROG_B        (1 << 2)
#define DS_WR_INTRDY        (1 << 7)
#define DS_WR_WORDWIDE      (1 << 0)

#define DSLOGIC_ATOMIC_SAMPLES		(sizeof(uint64_t) * 8)
#define DSLOGIC_ATOMIC_BYTES		sizeof(uint64_t)

/*
 * The FPGA is configured with TLV tuples. Length is specified as the
 * number of 16-bit words.
 */
#define _DS_CFG(variable, wordcnt) ((variable << 8) | wordcnt)
#define DS_CFG_START			0xf5a5f5a5
#define DS_CFG_MODE			_DS_CFG(0, 1)
#define DS_CFG_DIVIDER			_DS_CFG(1, 2)
#define DS_CFG_COUNT			_DS_CFG(3, 2)
#define DS_CFG_TRIG_POS			_DS_CFG(5, 2)
#define DS_CFG_TRIG_GLB			_DS_CFG(7, 1)
#define DS_CFG_CH_EN			_DS_CFG(8, 1)
#define DS_CFG_CH_EN_V2			_DS_CFG(10, 2)
#define DS_CFG_TRIG			_DS_CFG(64, 160)
#define DS_CFG_END			0xfa5afa5a

#pragma pack(push, 1)

enum {
    DSL_CTL_FW_VERSION		= 0,
    DSL_CTL_REVID_VERSION	= 1,
    DSL_CTL_HW_STATUS		= 2,
    DSL_CTL_PROG_B			= 3,
    DSL_CTL_SYS				= 4,
    DSL_CTL_LED				= 5,
    DSL_CTL_INTRDY			= 6,
    DSL_CTL_WORDWIDE		= 7,

    DSL_CTL_START			= 8,
    DSL_CTL_STOP			= 9,
    DSL_CTL_BULK_WR			= 10,
    DSL_CTL_REG				= 11,
    DSL_CTL_NVM				= 12,

    DSL_CTL_I2C_DSO			= 13,
    DSL_CTL_I2C_REG			= 14,
    DSL_CTL_I2C_STATUS		= 15,

    DSL_CTL_DSO_EN0			= 16,
    DSL_CTL_DSO_DC0			= 17,
    DSL_CTL_DSO_ATT0		= 18,
    DSL_CTL_DSO_EN1			= 19,
    DSL_CTL_DSO_DC1			= 20,
    DSL_CTL_DSO_ATT1		= 21,

    DSL_CTL_AWG_WR			= 22,
    DSL_CTL_I2C_PROBE		= 23,
    DSL_CTL_I2C_EXT         = 24,
};

struct ctl_header {
    uint8_t dest;
    uint16_t offset;
    uint8_t size;
};

struct version_info {
	uint8_t major;
	uint8_t minor;
};

struct cmd_start_acquisition {
	uint8_t flags;
	uint8_t sample_delay_h;
	uint8_t sample_delay_l;
};

struct fpga_config_common {
	uint32_t sync;

	uint16_t mode_header;
	uint16_t mode;
	uint16_t divider_header;
	uint32_t divider;
	uint16_t count_header;
	uint32_t count;
	uint16_t trig_pos_header;
	uint32_t trig_pos;
	uint16_t trig_glb_header;
	uint16_t trig_glb;

	uint16_t trig_header;
	uint16_t trig_mask0[NUM_TRIGGER_STAGES];
	uint16_t trig_mask1[NUM_TRIGGER_STAGES];
	uint16_t trig_value0[NUM_TRIGGER_STAGES];
	uint16_t trig_value1[NUM_TRIGGER_STAGES];
	uint16_t trig_edge0[NUM_TRIGGER_STAGES];
	uint16_t trig_edge1[NUM_TRIGGER_STAGES];
	uint16_t trig_logic0[NUM_TRIGGER_STAGES];
	uint16_t trig_logic1[NUM_TRIGGER_STAGES];
	uint32_t trig_count[NUM_TRIGGER_STAGES];

};

struct fpga_config_v1 {
	struct fpga_config_common common;
	uint16_t ch_en_header;
	uint16_t ch_en;

	uint32_t end_sync;
};

struct fpga_config_v2 {
	struct fpga_config_common common;

	uint16_t ch_en_header;
	uint32_t ch_en;

	uint32_t end_sync;
};

struct vco_config {
	uint8_t dest;
	uint8_t cnt;
	uint8_t delay;
	uint8_t byte[4];
};

static const struct vco_config vco_init_config[] = {
	{DS_I2C_REG_VCO_ADDR+2, 1, 0, {0x01, 0x00, 0x00, 0x00}},
	{DS_I2C_REG_VCO_ADDR, 4, 0, {0x01, 0x61, 0x00, 0x30}},
	{DS_I2C_REG_VCO_ADDR, 4, 0, {0x01, 0x40, 0xF1, 0x46}},
	{DS_I2C_REG_VCO_ADDR, 4, 10, {0x01, 0x62, 0x3D, 0x40}},
	{0, 0, 0, {0, 0, 0, 0}}
};

#pragma pack(pop)

/*
 * This should be larger than the FPGA bitstream image so that it'll get
 * uploaded in one big operation. There seem to be issues when uploading
 * it in chunks.
 */
#define FW_BUFSIZE (1024 * 1024)

#define FPGA_UPLOAD_DELAY (10 * 1000)

#define USB_TIMEOUT (3 * 1000)

static int command_read_ext(libusb_device_handle *devhdl, uint8_t control_code,
					uint8_t offset, uint8_t read_len, uint8_t *read_buffer)
{
	int ret;

	struct ctl_header cmd;
	cmd.dest = control_code;
	cmd.size = read_len;
	cmd.offset = offset;

	/* Send the control message. */
	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_ENDPOINT_OUT, DS_CMD_CTL_RD_PRE, 0x0000, 0x0000,
			(unsigned char *)&cmd, sizeof(struct ctl_header), USB_TIMEOUT);
	if (ret < 0) {
		sr_err("Unable to send CMD_CTL_RD_PRE command(dest:%d/offset:%d/size:%d): %s.",
				cmd.dest, cmd.offset, cmd.size,
				libusb_error_name(ret));
		return SR_ERR;
	}

	g_usleep(10*1000);

	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_ENDPOINT_IN, DS_CMD_CTL_RD, 0x0000, 0x0000,
		(unsigned char *)read_buffer, read_len, USB_TIMEOUT);

	if (ret < 0) {
		sr_err("Unable to send DS_CMD_CTL_RD for command %d: %s.",
				control_code,
				libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;

}

static int command_read(libusb_device_handle *devhdl, uint8_t control_code,
					uint8_t read_len, uint8_t *read_buffer)
{
	return command_read_ext(devhdl, control_code, 0, read_len,
						read_buffer);
}

static int command_write_ext(libusb_device_handle *devhdl, uint8_t control_code,
					uint8_t offset, uint8_t write_len, uint8_t *write_buffer)
{
	int ret;
	struct ctl_header cmd;
	cmd.dest = control_code;
	cmd.size = write_len;
	cmd.offset = offset;
	unsigned char *buf;
	buf = g_malloc(sizeof(cmd) + write_len);
	memcpy(buf, &cmd, sizeof(cmd));
	memcpy(&buf[sizeof(cmd)], write_buffer, write_len);

	/* Send the control message. */
	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
			LIBUSB_ENDPOINT_OUT, DS_CMD_CTL_WR, 0x0000, 0x0000,
			buf, write_len+sizeof(cmd), USB_TIMEOUT);
	g_free(buf);
	if (ret < 0) {
		sr_err("Unable to send DS_CMD_CTL_WR command(dest:%d/offset:%d/size:%d): %s.",
				cmd.dest, cmd.offset, cmd.size,
				libusb_error_name(ret));
		return SR_ERR;
	}
	return SR_OK;

}

static int command_write(libusb_device_handle *devhdl, uint8_t control_code,
					uint8_t write_len, uint8_t *write_buffer)
{
	return command_write_ext(devhdl, control_code, 0, write_len,
						write_buffer);
}

static int command_get_fw_version(struct sr_dev_inst *sdi,
				  struct version_info *vi)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct dev_context *devc = sdi->priv;
	libusb_device_handle *devhdl = usb->devhdl;
	int ret;

	if (devc->profile->api_version == DS_API_V2)
		return command_read(devhdl,	DSL_CTL_FW_VERSION,
				sizeof(struct version_info), (uint8_t *)vi);

	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_ENDPOINT_IN, DS_CMD_GET_FW_VERSION, 0x0000, 0x0000,
		(unsigned char *)vi, sizeof(struct version_info), USB_TIMEOUT);

	if (ret < 0) {
		sr_err("Unable to get version info: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int command_get_revid_version(struct sr_dev_inst *sdi, uint8_t *revid)
{
	struct sr_usb_dev_inst *usb = sdi->conn;
	struct dev_context *devc = sdi->priv;
	libusb_device_handle *devhdl = usb->devhdl;

	if (devc->profile->api_version == DS_API_V2)
		return command_read(devhdl,	DSL_CTL_REVID_VERSION,
				sizeof(uint8_t), revid);
	int ret;

	ret = libusb_control_transfer(devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
		LIBUSB_ENDPOINT_IN, DS_CMD_GET_REVID_VERSION, 0x0000, 0x0000,
		revid, 1, USB_TIMEOUT);

	if (ret < 0) {
		sr_err("Unable to get REVID: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}


static int write_register(libusb_device_handle *devhdl, uint8_t addr,
							uint8_t value)
{
    if (command_write_ext(devhdl, DSL_CTL_I2C_REG, addr, sizeof(value), &value)
			!= SR_OK) {
        sr_err("Sent DSL_CTL_I2C_REG command failed.");
        return SR_ERR;
    }

    return SR_OK;
}


static int configure_vco(libusb_device_handle *devhdl,
						const struct vco_config *config)
{
	while(config->dest) {
		if (config->delay > 0)
			g_usleep(config->delay*1000);
		for (int i = 0; i < config->cnt; i++) {
			write_register(devhdl, config->dest, config->byte[i]);
		}
		config++;
	}
	return SR_OK;
}

static int command_start_acquisition(const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	struct dslogic_mode mode;
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;
	usb = sdi->conn;

	if (devc->profile->api_version == DS_API_V1) {
		mode.flags = DS_START_FLAGS_MODE_LA | DS_START_FLAGS_SAMPLE_WIDE;
		mode.sample_delay_h = mode.sample_delay_l = 0;

		ret = libusb_control_transfer(usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
				LIBUSB_ENDPOINT_OUT, DS_CMD_START, 0x0000, 0x0000,
				(unsigned char *)&mode, sizeof(mode), USB_TIMEOUT);
		if (ret < 0) {
			sr_err("Failed to send start command: %s.", libusb_error_name(ret));
			return SR_ERR;
		}
	} else {
		if (command_write(usb->devhdl, DSL_CTL_START, 0, NULL) != SR_OK) {
			sr_err("Failed to send start command.");
			return SR_ERR;
		}
	}

	return SR_OK;
}

static int command_stop_acquisition(const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *usb;
	struct dslogic_mode mode;
	struct dev_context *devc;
	int ret;

	devc = sdi->priv;
	mode.flags = DS_START_FLAGS_STOP;
	mode.sample_delay_h = mode.sample_delay_l = 0;

	usb = sdi->conn;
	if (devc->profile->api_version == DS_API_V1) {
		ret = libusb_control_transfer(usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
				LIBUSB_ENDPOINT_OUT, DS_CMD_START, 0x0000, 0x0000,
				(unsigned char *)&mode, sizeof(struct dslogic_mode), USB_TIMEOUT);
	}
	else {
		ret = command_write(usb->devhdl, DSL_CTL_STOP, 0, NULL);
	}
	if (ret < 0) {
		sr_err("Failed to send stop command: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	return SR_OK;
}

static int fpga_firmware_upload_v2(struct libusb_device_handle *devhdl,
						struct sr_context *ctx,
						struct sr_resource *bitstream)
{
	uint8_t cmd[3];
	int result;
	unsigned char *buf;
	uint64_t sum;
	ssize_t chunksize;
	int transferred;
	bool fpga_done;

	result = command_read(devhdl, DSL_CTL_HW_STATUS, sizeof(uint8_t), cmd);
	if (result != SR_OK)
		return result;
	fpga_done = (cmd[0] & DS_FPGA_DONE) == DS_FPGA_DONE;

	if (!fpga_done) {

		cmd[0] = ~DS_WR_PROG_B;
		result = command_write(devhdl, DSL_CTL_PROG_B, sizeof(uint8_t), cmd);
		if (result != SR_OK)
			return result;

		cmd[0] = DS_WR_PROG_B;
		result = command_write(devhdl, DSL_CTL_PROG_B, sizeof(uint8_t), cmd);
		if (result != SR_OK)
			return result;

		while(1) {
			result = command_read(devhdl, DSL_CTL_HW_STATUS, sizeof(uint8_t), cmd);
			if (result != SR_OK)
				return result;
			if (cmd[0] & DS_FPGA_INIT_B)
				break;
		}

		cmd[0] = (uint8_t)~DS_WR_INTRDY;
		result = command_write(devhdl, DSL_CTL_INTRDY, sizeof(uint8_t), cmd);
		if (result != SR_OK)
			return result;

		cmd[0] = (uint8_t)bitstream->size;
		cmd[1] = (uint8_t)(bitstream->size >> 8);
		cmd[2] = (uint8_t)(bitstream->size >> 16);
		result = command_write(devhdl, DSL_CTL_BULK_WR, 3*sizeof(uint8_t), cmd);
		if (result != SR_OK)
			return result;

		buf = g_malloc(FW_BUFSIZE);
		sum = 0;
		while (1) {
			chunksize = sr_resource_read(ctx, bitstream,
					buf, FW_BUFSIZE);
			if (chunksize < 0) {
				g_free(buf);
				return SR_ERR;
			}
			if (chunksize == 0)
				break;

			result = libusb_bulk_transfer(devhdl, 2 | LIBUSB_ENDPOINT_OUT,
					buf, chunksize, &transferred, USB_TIMEOUT);
			if (result != SR_OK) {
				g_free(buf);
				return result;
			}
			sum += transferred;
			sr_spew("Uploaded %" PRIu64 "/%" PRIu64 " bytes.",
				sum, bitstream->size);

			if (transferred != chunksize) {
				sr_err("Short transfer while uploading FPGA firmware.");
				g_free(buf);
				return SR_ERR;
			}
		}
		g_free(buf);


		cmd[0] = DS_WR_INTRDY;
		result = command_write(devhdl, DSL_CTL_INTRDY, sizeof(uint8_t), cmd);
		if (result != SR_OK)
			return result;

		while(1) {
			result = command_read(devhdl, DSL_CTL_HW_STATUS, sizeof(uint8_t), cmd);
			if (result != SR_OK)
				return result;
			if (cmd[0] & DS_GPIF_DONE)
				break;
		}

		cmd[0] = (uint8_t)~DS_WR_INTRDY;
		result = command_write(devhdl, DSL_CTL_INTRDY, sizeof(uint8_t), cmd);
		if (result != SR_OK)
			return result;

		while(1) {
			result = command_read(devhdl, DSL_CTL_HW_STATUS, sizeof(uint8_t), cmd);
			if (result != SR_OK)
				return result;
			if (cmd[0] & DS_FPGA_DONE)
				break;
		}

		cmd[0] = DS_LED_GREEN;
		result = command_write(devhdl, DSL_CTL_LED, sizeof(uint8_t), cmd);
		if (result != SR_OK)
			return result;


		cmd[0] = DS_WR_WORDWIDE;
		result = command_write(devhdl, DSL_CTL_WORDWIDE, sizeof(uint8_t), cmd);
		if (result != SR_OK)
			return result;

	} else {
		/* fpga bitstream downloaded before */
		result = write_register(devhdl, DS_I2C_REG_CTR0_ADDR, 0);
		if (result != SR_OK)
			return result;
	}

	return SR_OK;
}

SR_PRIV int dslogic_fpga_firmware_upload(const struct sr_dev_inst *sdi)
{
	const char *name = NULL;
	uint64_t sum;
	struct sr_resource bitstream;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	unsigned char *buf;
	ssize_t chunksize;
	int transferred;
	int result, ret;
	const uint8_t cmd[3] = {0, 0, 0};

	drvc = sdi->driver->context;
	devc = sdi->priv;
	usb = sdi->conn;

	if (!strcmp(devc->profile->model, "DSLogic")) {
		if (devc->cur_threshold < 1.40)
			name = DSLOGIC_FPGA_FIRMWARE_3V3;
		else
			name = DSLOGIC_FPGA_FIRMWARE_5V;
	} else if (!strcmp(devc->profile->model, "DSLogic Pro")){
		name = DSLOGIC_PRO_FPGA_FIRMWARE;
	} else if (!strcmp(devc->profile->model, "DSLogic Plus")){
		name = DSLOGIC_PLUS_FPGA_FIRMWARE;
	} else if (!strcmp(devc->profile->model, "DSLogic Basic")){
		name = DSLOGIC_BASIC_FPGA_FIRMWARE;
	} else if (!strcmp(devc->profile->model, "DSCope")) {
		name = DSCOPE_FPGA_FIRMWARE;
	} else if (!strcmp(devc->profile->model, "DSLogic U3Pro16")) {
		name = DSLOGIC_U3PRO16_FPGA_FIRMWARE;
	} else {
		sr_err("Failed to select FPGA firmware.");
		return SR_ERR;
	}

	sr_dbg("Uploading FPGA firmware '%s'.", name);

	result = sr_resource_open(drvc->sr_ctx, &bitstream,
			SR_RESOURCE_FIRMWARE, name);
	if (result != SR_OK)
		return result;

	if (devc->profile->api_version == DS_API_V2) {
		result = fpga_firmware_upload_v2(usb->devhdl, drvc->sr_ctx,
									&bitstream);
		if (result != SR_OK) {
			sr_err("Failed to upload FPGA firmware.");
			sr_resource_close(drvc->sr_ctx, &bitstream);
			return SR_ERR;
		}
		if (devc->profile->dev_caps & DSLOGIC_CAPS_ADF4360) {
			/* configure frequency synthesizer/VCO if present */
			result = configure_vco(usb->devhdl, vco_init_config);
			if (result != SR_OK) {
				sr_err("Failed to configure the VCO.");
				
			}
		}
	} else {
		/* Tell the device firmware is coming. */
		if ((ret = libusb_control_transfer(usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
				LIBUSB_ENDPOINT_OUT, DS_CMD_CONFIG, 0x0000, 0x0000,
				(unsigned char *)&cmd, sizeof(cmd), USB_TIMEOUT)) < 0) {
			sr_err("Failed to upload FPGA firmware: %s.", libusb_error_name(ret));
			sr_resource_close(drvc->sr_ctx, &bitstream);
			return SR_ERR;
		}

		/* Give the FX2 time to get ready for FPGA firmware upload. */
		g_usleep(FPGA_UPLOAD_DELAY);

		buf = g_malloc(FW_BUFSIZE);
		sum = 0;
		result = SR_OK;
		while (1) {
			chunksize = sr_resource_read(drvc->sr_ctx, &bitstream,
					buf, FW_BUFSIZE);
			if (chunksize < 0)
				result = SR_ERR;
			if (chunksize <= 0)
				break;

			if ((ret = libusb_bulk_transfer(usb->devhdl, 2 | LIBUSB_ENDPOINT_OUT,
					buf, chunksize, &transferred, USB_TIMEOUT)) < 0) {
				sr_err("Unable to configure FPGA firmware: %s.",
						libusb_error_name(ret));
				result = SR_ERR;
				break;
			}
			sum += transferred;
			sr_spew("Uploaded %" PRIu64 "/%" PRIu64 " bytes.",
				sum, bitstream.size);

			if (transferred != chunksize) {
				sr_err("Short transfer while uploading FPGA firmware.");
				result = SR_ERR;
				break;
			}
		}
		g_free(buf);
	}
	sr_resource_close(drvc->sr_ctx, &bitstream);

	if (result == SR_OK)
		sr_dbg("FPGA firmware upload done.");

	return result;
}

static unsigned int enabled_channel_count(const struct sr_dev_inst *sdi)
{
	unsigned int count = 0;
	for (const GSList *l = sdi->channels; l; l = l->next) {
		const struct sr_channel *const probe = (struct sr_channel *)l->data;
		if (probe->enabled)
			count++;
	}
	return count;
}

static uint16_t enabled_channel_mask(const struct sr_dev_inst *sdi)
{
	unsigned int mask = 0;
	for (const GSList *l = sdi->channels; l; l = l->next) {
		const struct sr_channel *const probe = (struct sr_channel *)l->data;
		if (probe->enabled)
			mask |= 1 << probe->index;
	}
	return mask;
}

/*
 * Get the session trigger and configure the FPGA structure
 * accordingly.
 * @return @c true if any triggers are enabled, @c false otherwise.
 */
static bool set_trigger(const struct sr_dev_inst *sdi, struct fpga_config_common *cfg)
{
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	struct dev_context *devc;
	const GSList *l, *m;
	const unsigned int num_enabled_channels = enabled_channel_count(sdi);
	int num_trigger_stages = 0;

	int channelbit, i = 0;
	uint64_t trigger_point;

	devc = sdi->priv;

	for (i = 0; i < NUM_TRIGGER_STAGES; i++) {
		cfg->trig_mask0[i] = 0xffff;
		cfg->trig_mask1[i] = 0xffff;
		cfg->trig_value0[i] = 0;
		cfg->trig_value1[i] = 0;
		cfg->trig_edge0[i] = 0;
		cfg->trig_edge1[i] = 0;
		cfg->trig_logic0[i] = 2;
		cfg->trig_logic1[i] = 2;
		cfg->trig_count[i] = 0;
	}

	trigger_point = (devc->capture_ratio * devc->limit_samples) / 100;
	if (trigger_point < DSLOGIC_ATOMIC_SAMPLES)
		trigger_point = DSLOGIC_ATOMIC_SAMPLES;
	const uint64_t mem_depth_per_channel = devc->profile->mem_depth /
		num_enabled_channels;
	const uint64_t max_trigger_point = devc->continuous_mode ?
		((mem_depth_per_channel * 10) / 100) :
		((mem_depth_per_channel * DS_MAX_TRIG_PERCENT) / 100);
	if (trigger_point > max_trigger_point)
		trigger_point = max_trigger_point;
	cfg->trig_pos = trigger_point & ~(DSLOGIC_ATOMIC_SAMPLES - 1);

	if (!(trigger = sr_session_trigger_get(sdi->session))) {
		sr_dbg("No session trigger found");
		return false;
	}

	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		num_trigger_stages++;
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			if (!match->channel->enabled)
				/* Ignore disabled channels with a trigger. */
				continue;
			channelbit = 1 << (match->channel->index);
			/* Simple trigger support (event). */
			if (match->match == SR_TRIGGER_ONE) {
				cfg->trig_mask0[0] &= ~channelbit;
				cfg->trig_mask1[0] &= ~channelbit;
				cfg->trig_value0[0] |= channelbit;
				cfg->trig_value1[0] |= channelbit;
			} else if (match->match == SR_TRIGGER_ZERO) {
				cfg->trig_mask0[0] &= ~channelbit;
				cfg->trig_mask1[0] &= ~channelbit;
			} else if (match->match == SR_TRIGGER_FALLING) {
				cfg->trig_mask0[0] &= ~channelbit;
				cfg->trig_mask1[0] &= ~channelbit;
				cfg->trig_edge0[0] |= channelbit;
				cfg->trig_edge1[0] |= channelbit;
			} else if (match->match == SR_TRIGGER_RISING) {
				cfg->trig_mask0[0] &= ~channelbit;
				cfg->trig_mask1[0] &= ~channelbit;
				cfg->trig_value0[0] |= channelbit;
				cfg->trig_value1[0] |= channelbit;
				cfg->trig_edge0[0] |= channelbit;
				cfg->trig_edge1[0] |= channelbit;
			} else if (match->match == SR_TRIGGER_EDGE) {
				cfg->trig_edge0[0] |= channelbit;
				cfg->trig_edge1[0] |= channelbit;
			}
		}
	}

	if (devc->profile->api_version == DS_API_V1)
		cfg->trig_glb = (num_enabled_channels << 4);
	else
		cfg->trig_glb = (num_enabled_channels << 8);
	cfg->trig_glb |= (num_trigger_stages - 1);

	return num_trigger_stages != 0;
}

static int fpga_config_size(const struct sr_dev_inst *sdi)
{
	const struct dev_context *const devc = sdi->priv;
	if (devc->profile->api_version == DS_API_V1) {
		return sizeof(struct fpga_config_v1);
	} else {
		return sizeof(struct fpga_config_v2);
	}
}

static int fpga_configure(const struct sr_dev_inst *sdi)
{
	const struct dev_context *const devc = sdi->priv;
	const struct sr_usb_dev_inst *const usb = sdi->conn;
	uint8_t c[3];
	struct fpga_config_common *cfg;
	uint8_t *cfg_buffer;
	uint16_t mode = 0;
	uint32_t divider, tmp;
	int transferred, len, ret;

	sr_dbg("Configuring FPGA.");

	if (devc->profile->api_version == DS_API_V1) {
		struct fpga_config_v1 * v1_cfg;
		cfg_buffer = g_malloc0(sizeof(struct fpga_config_v1));
		v1_cfg = (struct fpga_config_v1 *)cfg_buffer;
		cfg = &v1_cfg->common;
		WL16(&v1_cfg->ch_en_header, DS_CFG_CH_EN);
		v1_cfg->ch_en = enabled_channel_mask(sdi);
		WL32(&v1_cfg->end_sync, DS_CFG_END);
	} else {
		struct fpga_config_v2 * v2_cfg;
		cfg_buffer = g_malloc0(sizeof(struct fpga_config_v2));
		v2_cfg = (struct fpga_config_v2 *)cfg_buffer;
		cfg = &v2_cfg->common;
		WL16(&v2_cfg->ch_en_header, DS_CFG_CH_EN_V2);
		v2_cfg->ch_en = enabled_channel_mask(sdi);
		WL32(&v2_cfg->end_sync, DS_CFG_END);
	}

	WL32(&cfg->sync, DS_CFG_START);
	WL16(&cfg->mode_header, DS_CFG_MODE);
	WL16(&cfg->divider_header, DS_CFG_DIVIDER);
	WL16(&cfg->count_header, DS_CFG_COUNT);
	WL16(&cfg->trig_pos_header, DS_CFG_TRIG_POS);
	WL16(&cfg->trig_glb_header, DS_CFG_TRIG_GLB);
	WL16(&cfg->trig_header, DS_CFG_TRIG);

	/* Pass in the length of a fixed-size struct. Really. */
	len = fpga_config_size(sdi) / 2;
	c[0] = len & 0xff;
	c[1] = (len >> 8) & 0xff;
	c[2] = (len >> 16) & 0xff;

	if (devc->profile->api_version == DS_API_V1) {
		ret = libusb_control_transfer(usb->devhdl, LIBUSB_REQUEST_TYPE_VENDOR |
				LIBUSB_ENDPOINT_OUT, DS_CMD_SETTING, 0x0000, 0x0000,
				c, sizeof(c), USB_TIMEOUT);
		if (ret < 0) {
			sr_err("Failed to send FPGA configure command: %s.",
				libusb_error_name(ret));
			g_free(cfg_buffer);
			return SR_ERR;
		}
	} else {
		ret = command_write(usb->devhdl, DSL_CTL_BULK_WR, sizeof(c), c);
		if (ret == SR_OK) {
			while(1) {
				ret = command_read(usb->devhdl, DSL_CTL_HW_STATUS, 1, c);
				if (ret != SR_OK)
					break;
				if (c[0] & DS_SYS_CLR)
					break;
			}
		}
		if (ret != SR_OK) {
			sr_err("Failed to send FPGA configure command");
			g_free(cfg_buffer);
			return ret;
		}
	}

	if (set_trigger(sdi, cfg))
		mode |= DS_MODE_TRIG_EN;

	if (devc->mode == DS_OP_INTERNAL_TEST)
		mode |= DS_MODE_INT_TEST;
	else if (devc->mode == DS_OP_EXTERNAL_TEST)
		mode |= DS_MODE_EXT_TEST;
	else if (devc->mode == DS_OP_LOOPBACK_TEST)
		mode |= DS_MODE_LPB_TEST;

	if (devc->cur_samplerate == devc->profile->half_samplerate)
		mode |= DS_MODE_HALF_MODE;
	else if (devc->cur_samplerate == devc->profile->quarter_samplerate)
		mode |= DS_MODE_QUAR_MODE;

	if (devc->continuous_mode)
		mode |= DS_MODE_STREAM_MODE;
	if (devc->external_clock) {
		mode |= DS_MODE_CLK_TYPE;
		if (devc->clock_edge == DS_EDGE_FALLING)
			mode |= DS_MODE_CLK_EDGE;
	}
	if (devc->limit_samples > DS_MAX_LOGIC_DEPTH *
		ceil(devc->cur_samplerate * 1.0 / devc->profile->max_samplerate)
		&& !devc->continuous_mode) {
		/* Enable RLE for long captures.
		 * Without this, captured data present errors.
		 */
		mode |= DS_MODE_RLE_MODE;
	}

	WL16(&cfg->mode, mode);
	tmp = ceil(devc->profile->max_samplerate * 1.0 / devc->cur_samplerate);
	divider  = ((tmp >= devc->profile->max_predivider) ?
			devc->profile->max_predivider - 1U : tmp - 1U) << 24;
	divider |= (uint32_t)ceil(tmp * 1.0 / devc->profile->max_predivider);
	WL32(&cfg->divider, divider);

	/* Number of 16-sample units. */
	WL32(&cfg->count, devc->limit_samples / 16);

	len = fpga_config_size(sdi);
	ret = libusb_bulk_transfer(usb->devhdl, 2 | LIBUSB_ENDPOINT_OUT,
			(unsigned char *)cfg_buffer, len, &transferred, USB_TIMEOUT);
	g_free(cfg_buffer);
	if (ret < 0 || transferred != len) {
		sr_err("Failed to send FPGA configuration: %s.", libusb_error_name(ret));
		return SR_ERR;
	}

	if (devc->profile->api_version == DS_API_V2) {
		/* assert INTRDY high to indicate data end) */
		c[0] = DS_WR_INTRDY;
		if (command_write(usb->devhdl, DSL_CTL_INTRDY, 1, c) != SR_OK) {
			sr_err("Failed to assert INTRDY high.");
			return SR_ERR;
		}

		/* check for FPGA done bit */
		if (command_read(usb->devhdl, DSL_CTL_HW_STATUS, 1, c) != SR_OK) {
			sr_err("Failed to read FPGA status.");
			return SR_ERR;
		}
		if (!(c[0] & DS_GPIF_DONE)) {
			sr_err("FPGA config failed.");
			return SR_ERR;
		}

	}

	return SR_OK;
}

SR_PRIV int dslogic_set_voltage_threshold(const struct sr_dev_inst *sdi, double threshold)
{
	int ret;
	struct dev_context *const devc = sdi->priv;
	const struct sr_usb_dev_inst *const usb = sdi->conn;
	const uint8_t value = (threshold / 5.0) * 255;
	const uint16_t cmd = value | (DS_ADDR_VTH << 8);

	/* Send the control command. */
	if (devc->profile->api_version == DS_API_V1) {
		ret = libusb_control_transfer(usb->devhdl,
				LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_OUT,
				DS_CMD_WR_REG, 0x0000, 0x0000,
				(unsigned char *)&cmd, sizeof(cmd), 3000);
	} else {
		ret = write_register(usb->devhdl, DS_I2C_REG_VTH_ADDR,
				value);
	}
	if (ret < 0) {
		sr_err("Unable to set voltage-threshold register: %s.",
		libusb_error_name(ret));
		return SR_ERR;
	}

	devc->cur_threshold = threshold;

	return SR_OK;
}

SR_PRIV int dslogic_dev_open(struct sr_dev_inst *sdi, struct sr_dev_driver *di)
{
	libusb_device **devlist;
	struct sr_usb_dev_inst *usb;
	struct libusb_device_descriptor des;
	struct dev_context *devc;
	struct drv_context *drvc;
	struct version_info vi;
	int ret = SR_ERR, i, device_count;
	uint8_t revid;
	uint8_t req_major_version;
	char connection_id[64];

	drvc = di->context;
	devc = sdi->priv;
	usb = sdi->conn;

	device_count = libusb_get_device_list(drvc->sr_ctx->libusb_ctx, &devlist);
	if (device_count < 0) {
		sr_err("Failed to get device list: %s.",
		       libusb_error_name(device_count));
		return SR_ERR;
	}

	for (i = 0; i < device_count; i++) {
		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != devc->profile->vid
		    || des.idProduct != devc->profile->pid)
			continue;

		if ((sdi->status == SR_ST_INITIALIZING) ||
				(sdi->status == SR_ST_INACTIVE)) {
			/* Check device by its physical USB bus/port address. */
			if (usb_get_port_path(devlist[i], connection_id, sizeof(connection_id)) < 0)
				continue;

			if (strcmp(sdi->connection_id, connection_id))
				/* This is not the one. */
				continue;
		}

		if (!(ret = libusb_open(devlist[i], &usb->devhdl))) {
			if (usb->address == 0xff)
				/*
				 * First time we touch this device after FW
				 * upload, so we don't know the address yet.
				 */
				usb->address = libusb_get_device_address(devlist[i]);
		} else {
			sr_err("Failed to open device: %s.",
			       libusb_error_name(ret));
			ret = SR_ERR;
			break;
		}

		if (libusb_has_capability(LIBUSB_CAP_SUPPORTS_DETACH_KERNEL_DRIVER)) {
			if (libusb_kernel_driver_active(usb->devhdl, USB_INTERFACE) == 1) {
				if ((ret = libusb_detach_kernel_driver(usb->devhdl, USB_INTERFACE)) < 0) {
					sr_err("Failed to detach kernel driver: %s.",
						libusb_error_name(ret));
					ret = SR_ERR;
					break;
				}
			}
		}

		ret = command_get_fw_version(sdi, &vi);
		if (ret != SR_OK) {
			sr_err("Failed to get firmware version.");
			break;
		}

		ret = command_get_revid_version(sdi, &revid);
		if (ret != SR_OK) {
			sr_err("Failed to get REVID.");
			break;
		}

		/*
		 * Changes in major version mean incompatible/API changes, so
		 * bail out if we encounter an incompatible version.
		 * Different minor versions are OK, they should be compatible.
		 */
		req_major_version = (devc->profile->api_version == DS_API_V1) ?
									DSLOGIC_REQUIRED_VERSION_MAJOR_V1 : 
									DSLOGIC_REQUIRED_VERSION_MAJOR_V2;
		if (vi.major != req_major_version) {
			sr_err("Expected firmware version %d.x, "
			       "got %d.%d.", req_major_version,
			       vi.major, vi.minor);
			ret = SR_ERR;
			break;
		}

		sr_info("Opened device on %d.%d (logical) / %s (physical), "
			"interface %d, firmware %d.%d.",
			usb->bus, usb->address, connection_id,
			USB_INTERFACE, vi.major, vi.minor);

		sr_info("Detected REVID=%d, it's a Cypress CY7C68013%s.",
			revid, (revid != 1) ? " (FX2)" : "A (FX2LP)");

		ret = SR_OK;

		break;
	}

	libusb_free_device_list(devlist, 1);

	return ret;
}

SR_PRIV struct dev_context *dslogic_dev_new(void)
{
	struct dev_context *devc;

	devc = g_malloc0(sizeof(struct dev_context));
	devc->profile = NULL;
	devc->fw_updated = 0;
	devc->cur_samplerate = 0;
	devc->limit_samples = 0;
	devc->capture_ratio = 0;
	devc->continuous_mode = FALSE;
	devc->clock_edge = DS_EDGE_RISING;

	return devc;
}

static void abort_acquisition(struct dev_context *devc)
{
	int i;

	devc->acq_aborted = TRUE;

	for (i = devc->num_transfers - 1; i >= 0; i--) {
		if (devc->transfers[i])
			libusb_cancel_transfer(devc->transfers[i]);
	}
	g_mutex_lock(&devc->data_proc_mutex);
	devc->data_proc_state = DS_DATA_PROC_ABORT_REQ;
	g_cond_signal(&devc->data_proc_state_cond);
	g_mutex_unlock(&devc->data_proc_mutex);
}

static void finish_acquisition(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;

	std_session_send_df_end(sdi);

	usb_source_remove(sdi->session, devc->ctx);

	devc->num_transfers = 0;
	g_free(devc->transfers);
	g_free(devc->deinterleave_buffer);
	g_cond_clear(&devc->data_proc_state_cond);
	g_mutex_clear(&devc->data_proc_mutex);
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

	sr_err("%s: %s", __func__, libusb_error_name(ret));
	free_transfer(transfer);

}

static void deinterleave_buffer(const uint8_t *src, size_t length,
	uint16_t *dst_ptr, size_t channel_count, uint16_t channel_mask)
{
	uint64_t *src_ptr = (uint64_t*)src;
	uint64_t last_data[NUM_CHANNELS];
	bool first_loop = TRUE;
	unsigned int idx;

	/* initially memset first samples of destination buffer to avoid
	recording uninitialized garbage for channels which are not enabled */
	memset(dst_ptr, 0, DSLOGIC_ATOMIC_SAMPLES * sizeof(uint16_t));
	/* for first iteration, force last_data to be different */
	for (unsigned int i = 0; i < channel_count; i++)
		last_data[i] = ~src_ptr[i];

	while (((uint8_t *)src_ptr) < (src + length)) {
		if (G_LIKELY(!first_loop)) {
			/* forward copy the previous data... */
			memcpy(&dst_ptr[DSLOGIC_ATOMIC_SAMPLES], dst_ptr,
				DSLOGIC_ATOMIC_SAMPLES * sizeof(uint16_t));
			dst_ptr += DSLOGIC_ATOMIC_SAMPLES;
		} else
			first_loop = FALSE;
		idx = 0;
		for (unsigned int channel = 0; channel != 16; channel++) {
			/* ...and now selectively search for changes per channel */
			if (channel_mask & (1 << channel)) {
				if (last_data[idx] != src_ptr[idx]) {
					for (unsigned int bit = 0; bit != DSLOGIC_ATOMIC_SAMPLES; bit++) {
						if (src_ptr[idx] & (UINT64_C(1) << bit))
							dst_ptr[bit] |= (1 << channel);
						else
							dst_ptr[bit] &= ~(uint16_t)(1 << channel);
					}
				}
				idx++;
			}
		}

		memcpy(last_data, src_ptr, channel_count * sizeof(uint64_t));
		src_ptr += channel_count;
	}
}

static void send_data(struct sr_dev_inst *sdi,
	uint16_t *data, size_t sample_count)
{
	const struct sr_datafeed_logic logic = {
		.length = sample_count * sizeof(uint16_t),
		.unitsize = sizeof(uint16_t),
		.data = data
	};

	const struct sr_datafeed_packet packet = {
		.type = SR_DF_LOGIC,
		.payload = &logic
	};

	sr_session_send(sdi, &packet);
}

static void * session_worker_thread(void *data) {
	struct sr_dev_inst *const sdi = data;
	struct dev_context *devc = sdi->priv;
	const size_t channel_count = enabled_channel_count(sdi);
	const uint16_t channel_mask = enabled_channel_mask(sdi);
	uint64_t num_samples;
	uint64_t limit_samples;
	unsigned int cur_sample_count;
	int trigger_offset;

	while (TRUE) {
		g_mutex_lock(&devc->data_proc_mutex);
		while (devc->data_proc_state == DS_DATA_PROC_IDLE)
			g_cond_wait(&devc->data_proc_state_cond, &devc->data_proc_mutex);
		if (devc->data_proc_state != DS_DATA_PROC_START_REQ) {
			g_cond_signal(&devc->data_proc_state_cond);
			g_mutex_unlock(&devc->data_proc_mutex);
			break;
		}

		devc->data_proc_state = DS_DATA_PROC_RUNNING;
		g_mutex_unlock(&devc->data_proc_mutex);
		limit_samples = devc->limit_samples;
		if (!devc->continuous_mode)
				limit_samples = devc->samples_captured;

		cur_sample_count = DSLOGIC_ATOMIC_SAMPLES * devc->completed_transfer->actual_length /
					(DSLOGIC_ATOMIC_BYTES * channel_count);
		if (!limit_samples || devc->sent_samples < limit_samples) {
			if (limit_samples && 
				devc->sent_samples + cur_sample_count > limit_samples)
				num_samples = limit_samples - devc->sent_samples;
			else
				num_samples = cur_sample_count;

			/**
			 * The DSLogic emits sample data as sequences of 64-bit sample words
			 * in a round-robin i.e. 64-bits from channel 0, 64-bits from channel 1
			 * etc. for each of the enabled channels, then looping back to the
			 * channel.
			 *
			 * Because sigrok's internal representation is bit-interleaved channels
			 * we must recast the data.
			 *
			 * Hopefully in future it will be possible to pass the data on as-is.
			 */
			if (devc->completed_transfer->actual_length % 
				(DSLOGIC_ATOMIC_BYTES * channel_count) != 0) {
				sr_err("Invalid transfer length!");
			} else {
				deinterleave_buffer(devc->completed_transfer->buffer,
					devc->completed_transfer->actual_length,
					devc->deinterleave_buffer, channel_count, channel_mask);

				/* Send the incoming transfer to the session bus. */
				if (devc->trigger_pos > devc->sent_samples
					&& devc->trigger_pos <= devc->sent_samples + num_samples) {
					/* DSLogic trigger in this block. Send trigger position. */
					trigger_offset = devc->trigger_pos - devc->sent_samples;
					/* Pre-trigger samples. */
					send_data(sdi, devc->deinterleave_buffer, trigger_offset);
					devc->sent_samples += trigger_offset;
					/* Trigger position. */
					devc->trigger_pos = 0;
					std_session_send_df_trigger(sdi);
					/* Post trigger samples. */
					num_samples -= trigger_offset;
					send_data(sdi, devc->deinterleave_buffer
						+ trigger_offset, num_samples);
					devc->sent_samples += num_samples;
				} else {
					send_data(sdi, devc->deinterleave_buffer, num_samples);
					devc->sent_samples += num_samples;
				}
			}
		}
		g_mutex_lock(&devc->data_proc_mutex);
		if (devc->data_proc_state == DS_DATA_PROC_RUNNING) {
			if (limit_samples && devc->sent_samples >= limit_samples) {
				devc->data_proc_state = DS_DATA_PROC_MAX_SAMPLES_REACHED;
			} else  {
				devc->data_proc_state = DS_DATA_PROC_IDLE;
			}
		}
		/* always submit a new transfer even if max sample count is reached.
		aborting the transfer is handling only in receive_transfer callback
		*/
		resubmit_transfer(devc->completed_transfer);
		g_cond_signal(&devc->data_proc_state_cond);
		g_mutex_unlock(&devc->data_proc_mutex);
	}

	return NULL;
}

static void LIBUSB_CALL receive_transfer(struct libusb_transfer *transfer)
{
	struct sr_dev_inst *const sdi = transfer->user_data;
	struct dev_context *const devc = sdi->priv;
	gboolean packet_has_error = FALSE;
	gboolean abort = FALSE;

	/*
	 * If acquisition has already ended, just free any queued up
	 * transfer that come in.
	 */
	if (devc->acq_aborted) {
		free_transfer(transfer);
		return;
	}

	sr_dbg("receive_transfer(): status %s received %d bytes.",
		libusb_error_name(transfer->status), transfer->actual_length);

	/* Save incoming transfer before reusing the transfer struct. */

	switch (transfer->status) {
	case LIBUSB_TRANSFER_NO_DEVICE:
		abort_acquisition(devc);
		free_transfer(transfer);
		return;
	case LIBUSB_TRANSFER_COMPLETED:
	case LIBUSB_TRANSFER_TIMED_OUT: /* We may have received some data though. */
		break;
	default:
		packet_has_error = TRUE;
		break;
	}

	if (transfer->actual_length == 0 || packet_has_error) {
		devc->empty_transfer_count++;
		if (devc->empty_transfer_count > MAX_EMPTY_TRANSFERS) {
			/*
			 * The FX2 gave up. End the acquisition, the frontend
			 * will work out that the samplecount is short.
			 */
			abort_acquisition(devc);
			free_transfer(transfer);
		} else {
			resubmit_transfer(transfer);
		}
		return;
	} else {
		devc->empty_transfer_count = 0;
	}

	/* 
	 * Since we are here in receive transfer callback, hand over 
	 * time-consuming data processing to a dedicated worker thread
	 * and continue here to process the next transfer which may have finished
	 * already.
	 */
	g_mutex_lock(&devc->data_proc_mutex);
	while ((devc->data_proc_state == DS_DATA_PROC_RUNNING) ||
		(devc->data_proc_state == DS_DATA_PROC_START_REQ))
		g_cond_wait(&devc->data_proc_state_cond, &devc->data_proc_mutex);
	if (devc->data_proc_state == DS_DATA_PROC_IDLE) {
		devc->completed_transfer = transfer;
		devc->data_proc_state = DS_DATA_PROC_START_REQ;
	} else {
		abort = TRUE;

	}
	g_cond_signal(&devc->data_proc_state_cond);
	g_mutex_unlock(&devc->data_proc_mutex);
	if (abort) {
		abort_acquisition(devc);
		free_transfer(transfer);

	}
}

static int receive_data(int fd, int revents, void *cb_data)
{
	struct timeval tv;
	struct drv_context *drvc;

	(void)fd;
	(void)revents;

	drvc = (struct drv_context *)cb_data;

	tv.tv_sec = tv.tv_usec = 0;
	libusb_handle_events_timeout(drvc->sr_ctx->libusb_ctx, &tv);

	return TRUE;
}

static size_t to_bytes_per_ms(const struct sr_dev_inst *sdi)
{
	const struct dev_context *const devc = sdi->priv;
	const size_t ch_count = enabled_channel_count(sdi);

	if (devc->continuous_mode)
		return (devc->cur_samplerate * ch_count) / (1000 * 8);


	/* If we're in buffered mode, the transfer rate is not so important,
	 * but we expect to get at least 10% of the high-speed USB bandwidth.
	 */
	return 35000000 / (1000 * 10);
}

static size_t get_buffer_size(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	/*
	 * The buffer should be large enough to hold 10ms of data and
	 * a multiple of the size of a data atom.
	 */
	const size_t block_size = enabled_channel_count(sdi) * devc->profile->block_size;
	const size_t s = 10 * to_bytes_per_ms(sdi);
	if (!block_size)
		return s;
	return ((s + block_size - 1) / block_size) * block_size;
}

static unsigned int get_number_of_transfers(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	/* Total buffer size should be able to hold about 100ms of data. */
	const unsigned int s = get_buffer_size(sdi);
	const unsigned int n = (devc->profile->usb_buffer_duration_ms *
					to_bytes_per_ms(sdi) + s - 1) / s;
	return (n > NUM_SIMUL_TRANSFERS) ? NUM_SIMUL_TRANSFERS : n;
}

static unsigned int get_timeout(const struct sr_dev_inst *sdi)
{
	const size_t total_size = get_buffer_size(sdi) *
		get_number_of_transfers(sdi);
	const unsigned int timeout = total_size / to_bytes_per_ms(sdi);
	return timeout + timeout / 4; /* Leave a headroom of 25% percent. */
}

static int start_transfers(const struct sr_dev_inst *sdi)
{
	const size_t channel_count = enabled_channel_count(sdi);
	const size_t size = get_buffer_size(sdi);
	const unsigned int num_transfers = get_number_of_transfers(sdi);
	const unsigned int timeout = get_timeout(sdi);

	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct libusb_transfer *transfer;
	unsigned int i;
	int ret;
	unsigned char *buf;

	devc = sdi->priv;
	usb = sdi->conn;

	devc->sent_samples = 0;
	devc->acq_aborted = FALSE;
	devc->empty_transfer_count = 0;
	devc->submitted_transfers = 0;

	g_free(devc->transfers);
	devc->transfers = g_try_malloc0(sizeof(*devc->transfers) * num_transfers);
	if (!devc->transfers) {
		sr_err("USB transfers malloc failed.");
		return SR_ERR_MALLOC;
	}

	devc->deinterleave_buffer = g_try_malloc(DSLOGIC_ATOMIC_SAMPLES *
		(size / (channel_count * DSLOGIC_ATOMIC_BYTES)) * sizeof(uint16_t));
	if (!devc->deinterleave_buffer) {
		sr_err("Deinterleave buffer malloc failed.");
		g_free(devc->deinterleave_buffer);
		return SR_ERR_MALLOC;
	}

	devc->num_transfers = num_transfers;
	for (i = 0; i < num_transfers; i++) {
		if (!(buf = g_try_malloc(size))) {
			sr_err("USB transfer buffer malloc failed.");
			return SR_ERR_MALLOC;
		}
		transfer = libusb_alloc_transfer(0);
		libusb_fill_bulk_transfer(transfer, usb->devhdl,
				6 | LIBUSB_ENDPOINT_IN, buf, size,
				receive_transfer, (void *)sdi, timeout);
		sr_info("submitting transfer: %d", i);
		if ((ret = libusb_submit_transfer(transfer)) != 0) {
			sr_err("Failed to submit transfer: %s.",
			       libusb_error_name(ret));
			libusb_free_transfer(transfer);
			g_free(buf);
			abort_acquisition(devc);
			return SR_ERR;
		}
		devc->transfers[i] = transfer;
		devc->submitted_transfers++;
	}

	std_session_send_df_header(sdi);

	return SR_OK;
}

static void LIBUSB_CALL trigger_receive(struct libusb_transfer *transfer)
{
	const struct sr_dev_inst *sdi;
	struct dslogic_trigger_pos *tpos;
	struct dev_context *devc;
	uint64_t remain_samples;

	sdi = transfer->user_data;
	devc = sdi->priv;
	if (transfer->status == LIBUSB_TRANSFER_CANCELLED) {
		sr_dbg("Trigger transfer canceled.");
		/* Terminate session. */
		std_session_send_df_end(sdi);
		usb_source_remove(sdi->session, devc->ctx);
		devc->num_transfers = 0;
		g_free(devc->transfers);
	} else if (transfer->status == LIBUSB_TRANSFER_COMPLETED
			&& transfer->actual_length == (int)devc->profile->block_size) {
		tpos = (struct dslogic_trigger_pos *)transfer->buffer;
		sr_info("tpos real_pos %d ram_saddr %d cnt_h %d cnt_l %d", tpos->real_pos,
			tpos->ram_saddr, tpos->remain_cnt_h, tpos->remain_cnt_l);
		remain_samples = ((uint64_t)tpos->remain_cnt_h) << 32;
		remain_samples += tpos->remain_cnt_l;
		if (devc->limit_samples > remain_samples)
			devc->samples_captured = devc->limit_samples - remain_samples;
		else
			devc->samples_captured = devc->limit_samples;
		devc->samples_captured &= ~(DSLOGIC_ATOMIC_SAMPLES - 1);
		devc->trigger_pos = tpos->real_pos;
		g_free(tpos);
		start_transfers(sdi);
	}
	libusb_free_transfer(transfer);
}

SR_PRIV int dslogic_acquisition_start(const struct sr_dev_inst *sdi)
{
	const unsigned int timeout = get_timeout(sdi);

	struct sr_dev_driver *di;
	struct drv_context *drvc;
	struct dev_context *devc;
	struct sr_usb_dev_inst *usb;
	struct dslogic_trigger_pos *tpos;
	struct libusb_transfer *transfer;
	int ret;

	di = sdi->driver;
	drvc = di->context;
	devc = sdi->priv;
	usb = sdi->conn;

	devc->ctx = drvc->sr_ctx;
	devc->sent_samples = 0;
	devc->empty_transfer_count = 0;
	devc->acq_aborted = FALSE;
	devc->data_proc_state = DS_DATA_PROC_IDLE;
	g_cond_init(&devc->data_proc_state_cond);
	g_mutex_init(&devc->data_proc_mutex);
	devc->thread_handle = g_thread_new("DSL session worker thread",
							session_worker_thread, (void*)sdi);


	usb_source_add(sdi->session, devc->ctx, timeout, receive_data, drvc);

	if ((ret = command_stop_acquisition(sdi)) != SR_OK)
		return ret;

	if ((ret = fpga_configure(sdi)) != SR_OK)
		return ret;

	if ((ret = command_start_acquisition(sdi)) != SR_OK)
		return ret;

	sr_dbg("Getting trigger.");
	tpos = g_malloc(devc->profile->block_size);
	transfer = libusb_alloc_transfer(0);
	libusb_fill_bulk_transfer(transfer, usb->devhdl, 6 | LIBUSB_ENDPOINT_IN,
			(unsigned char *)tpos, devc->profile->block_size,
			trigger_receive, (void *)sdi, 0);
	if ((ret = libusb_submit_transfer(transfer)) < 0) {
		sr_err("Failed to request trigger: %s.", libusb_error_name(ret));
		libusb_free_transfer(transfer);
		g_free(tpos);
		return SR_ERR;
	}

	devc->transfers = g_try_malloc0(sizeof(*devc->transfers));
	if (!devc->transfers) {
		sr_err("USB trigger_pos transfer malloc failed.");
		return SR_ERR_MALLOC;
	}
	devc->num_transfers = 1;
	devc->submitted_transfers++;
	devc->transfers[0] = transfer;

	return ret;
}

SR_PRIV int dslogic_acquisition_stop(struct sr_dev_inst *sdi)
{
	command_stop_acquisition(sdi);
	abort_acquisition(sdi->priv);
	return SR_OK;
}
