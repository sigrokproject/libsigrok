/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2024-2026 PXView Developers <913461865@qq.com>
 * Copyright (C) 2026 dogtopus <dogtopus@users.noreply.github.com>
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
#include <strings.h>
#include "protocol.h"

#ifndef PX_LOGIC_FORCE_TIMER
#define PX_LOGIC_FORCE_TIMER 0
#endif

#define BUF_SIZE_MS 10

#define POLL_TIMEOUT (BUF_SIZE_MS / 2)
#define REQ_TIMEOUT 1000
#define MCU_PROGRAM_DELAY 100
#define FPGA_CHECK_PERIOD_US 10000
#define FPGA_CHECK_COUNT 200

#define NUM_SIMUL_XFERS 32
#define NUM_TRANSPOSE_WORKERS 3

#define FPGA_VCCIO 3.334
#define FPGA_F_PWM_VREF SR_MHZ(120)
#define FPGA_F_PWM SR_MHZ(125)
#define FPGA_PWM_VREF_PERIOD SR_KHZ(10)
#define FPGA_INPUT_VDIV (1.0 / 2.0)

/*
 * Limit the maximum size of a single transfer to below the global maximum size
 * of allocated transfers of all supported OS.
 * This is so we can have at least 1 transfer going.
 *
 * TODO: Find a way to test these values.
 */
/* 4MiB */
#define MAX_BUF_SIZE_SS (4 * 1024 * 1024)
/* 4.8Mbit == 600KB */
#define MAX_BUF_SIZE_HS (4800000 / 8)

#define MAX_PREPEND_LEN_BUFR 90
#define MAX_PREPEND_LEN_STRM 10

#define STRIPE_SIZE_BYTES sizeof(uint64_t)
#define STRIPE_SIZE_BITS STRIPE_SIZE_BYTES * 8
#define SAMPLES_IN_FRAME STRIPE_SIZE_BITS

#define REQ_PACKET_LEN 0x08
#define REQ_KEY_READ 0xfefe0001
#define REQ_KEY_WRITE 0xfefe0000
#define REQ_WRITE_ACK 0xfefefefe

#define EP_REG 0x01
#define EP_FIFO_SAMPLE 0x02
#define EP_FIFO_FWRAM 0x03

#define EP0_CMD_GET_TRIGGER_STATUS 0xb0

#define REG_MODE 0x0000
#define REG_PWM_VREF_CMP_PERIOD 0x0004
#define REG_PWM_VREF_CMP_DUTY 0x0008
#define REG_CHANNEL_EN 0x0010
#define REG_CLK_CONF 0x0014
#define REG_CLK_DIV 0x0018
#define REG_SAMPLE_BUFFER_SIZE 0x001c
#define REG_STOP 0x0020
#define REG_TRIG_LOW 0x0024
#define REG_TRIG_HIGH 0x0028
#define REG_TRIG_RISING 0x002c
#define REG_TRIG_FALLING 0x0030
#define REG_TRIG_EXT_MODE 0x003c
#define REG_PWM0_CONF 0x0040
#define REG_PWM0_CMP_PERIOD 0x0044
#define REG_PWM0_CMP_DUTY 0x0048
#define REG_PWM1_CONF 0x004c
#define REG_PWM1_CMP_PERIOD 0x0050
#define REG_PWM1_CMP_DUTY 0x0054
#define REG_TRIG_OUT_EN 0x0058

#define REG_XFER_BUFFER_SIZE 0x2008
#define REG_FWRAM_READ_START 0x200c
#define REG_FWRAM_READ_END 0x2010
#define REG_FWRAM_READ_BANK 0x2014
#define REG_FWRAM_WRITE_START 0x2018
#define REG_FWRAM_WRITE_END 0x201c
#define REG_FWRAM_WRITE_BANK 0x2020
#define REG_NUM_SAMPLES_LO 0x2024
#define REG_NUM_SAMPLES_HI 0x2028
#define REG_SAMPLER_RESET 0x202c
#define REG_MCU_RESET 0x2030
#define REG_MCU_FW_VERSION 0x2034
#define REG_ENABLED_NUM_CH 0x204c
#define REG_TRIG_POINT 0x2050
#define REG_TRIG_POINT_REAL 0x2054
#define REG_DEV_VARIANT 0x2058

#define FWRAM_MCU_PROG_FLASH 0
#define FWRAM_FPGA_CFGRAM 4

#define FWRAM_ADDR_FPGA 0x0
#define FWRAM_ADDR_MCU_BOOTLOADER 0x0
#define FWRAM_ADDR_MCU_USER 0xc000
#define FWRAM_SIZE_MCU_USER 0xc000

#define MODE_MASK_INIT (1 << 0)
#define MODE_MASK_STREAMING (1 << 1)
#define MODE_MASK_INIT2 (1 << 2)
#define MODE_MASK_FILTER_EN (1 << 3)
#define MODE_MASK_UNK_4 (1 << 4)

#define CLK_CONF_MASK_SELECT 0x7
#define CLK_CONF_MASK_EDGE (1 << 3)

#define PWM_CONF_MASK_EN (1 << 0)

#define ALIGN_4K(x) ((x / 4096 + 1) * 4096)
#define ALIGN_CH_FLOOR(x, ch) ((x / (4096 * ch)) * (4096 * ch))

#define MCU_FW_NAME "px-logic-mcu.fw"
#define FPGA_STAGE1_NAME "px-logic-fpga-stage1.fw"
#define FPGA_STAGE2_NAME "px-logic-fpga-stage2.fw"

#define MCU_FW_VERSION 0x56900028

struct trigger_status {
	uint64_t sample_offset;
	uint32_t activated;
	uint32_t pos_real;
};

struct sample_xfer_user_data {
	const struct sr_dev_inst *sdi;
	uint64_t seq;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint8_t *tr_buffer;
};

struct pwm_reg {
	uint32_t period;
	uint32_t duty;
	uint32_t conf;
};

static const uint64_t clk_conf_table[CLK_NUM_SUPPORTED] = {
	[CLK_1GHZ] = SR_GHZ(1),	    [CLK_500MHZ] = SR_MHZ(500),
	[CLK_250MHZ] = SR_MHZ(250), [CLK_125MHZ] = SR_MHZ(125),
	[CLK_800MHZ] = SR_MHZ(800), [CLK_400MHZ] = SR_MHZ(400),
	[CLK_200MHZ] = SR_MHZ(200), [CLK_100MHZ] = SR_MHZ(100),
};

static const struct pwm_reg pwm_reg_list[NUM_PWM_CHANNELS] = {
	[0] = { .period = REG_PWM0_CMP_PERIOD,
		.duty = REG_PWM0_CMP_DUTY,
		.conf = REG_PWM0_CONF },
	[1] = { .period = REG_PWM1_CMP_PERIOD,
		.duty = REG_PWM1_CMP_DUTY,
		.conf = REG_PWM1_CONF },
};

/* ===== Forward declaration of callbacks ===== */

static int cap_top_event_handler(int fd, int revents, void *cb_data);
static void LIBUSB_CALL xfer_sample_event(struct libusb_transfer *xfer);
static void xfer_sample_transpose_worker(gpointer data, gpointer user_data);

static int loop_begin(const struct sr_dev_inst *sdi,
		      sr_receive_data_callback cb);
static int loop_end(const struct sr_dev_inst *sdi);

/* ===== Device control and register access routines ===== */

static int ep0_get_trigger_status(libusb_device_handle *devhdl,
				  struct trigger_status *status)
{
	int ret;
	uint8_t trx[16];

	memset(trx, 0, sizeof(trx));

	ret = libusb_control_transfer(
		devhdl, LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_ENDPOINT_IN,
		EP0_CMD_GET_TRIGGER_STATUS, 0x0000, 0x0000, trx, sizeof(trx),
		POLL_TIMEOUT);

	if (ret < 0) {
		sr_err("Unable to get trigger status: %s.",
		       libusb_error_name(ret));
		return SR_ERR;
	}

	status->sample_offset = RL64(&trx[0]);
	status->activated = RL32(&trx[8]);
	status->pos_real = RL32(&trx[12]);

	return SR_OK;
}

/**
 * Do a roundtrip transaction over the control register access endpoint.
 *
 * @param[in] sdi Device context.
 * @param[in] tx_buf Transmit buffer.
 * @param[in] tx_len Amount of bytes to transmit.
 * @param[out] rx_buf Receive buffer.
 * @param[in] rx_len Amount of bytes to receive.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_IO Failed to transmit or receive data.
 * @retval SR_ERR_TIMEOUT Timeout.
 *
 * The transmit buffer can overlap with the receive buffer. This allows one to
 * reuse the same buffer as `tx_buf` and `rx_buf`.
 */
static int reg_ep_trx(struct libusb_device_handle *devhdl, uint8_t *tx_buf,
		      uint16_t tx_len, uint8_t *rx_buf, uint16_t rx_len)
{
	int xfer_result, xfer_count;

	xfer_result = libusb_bulk_transfer(devhdl, LIBUSB_ENDPOINT_OUT | EP_REG,
					   tx_buf, tx_len, &xfer_count,
					   REQ_TIMEOUT);

	if (xfer_result != LIBUSB_SUCCESS)
		sr_err("Failed to transmit data to EP_REG: %s.",
		       libusb_error_name(xfer_result));

	if (xfer_result == LIBUSB_ERROR_TIMEOUT)
		return SR_ERR_TIMEOUT;
	else if (xfer_result != LIBUSB_SUCCESS)
		return SR_ERR_IO;

	if (xfer_count != tx_len) {
		sr_err("Incomplete data transmitted to EP_REG: "
		       "expecting %dB, actually sent %dB.",
		       tx_len, xfer_count);
		return SR_ERR_IO;
	}

	xfer_result = libusb_bulk_transfer(devhdl, LIBUSB_ENDPOINT_IN | EP_REG,
					   rx_buf, rx_len, &xfer_count,
					   REQ_TIMEOUT);

	if (xfer_result != LIBUSB_SUCCESS)
		sr_err("Failed to receive data from EP_REG: %s.",
		       libusb_error_name(xfer_result));

	if (xfer_result == LIBUSB_ERROR_TIMEOUT)
		return SR_ERR_TIMEOUT;
	else if (xfer_result != LIBUSB_SUCCESS)
		return SR_ERR_IO;

	if (xfer_count != rx_len) {
		sr_err("Incomplete data received from EP_REG: "
		       "expecting %dB, actually got %dB.",
		       tx_len, xfer_count);
		return SR_ERR_IO;
	}

	return SR_OK;
}

/**
 * Transmit data to the firmware RAM FIFO endpoint.
 *
 * @param[in] sdi Device context.
 * @param[in] tx_buf Transmit buffer.
 * @param[in] tx_len Amount of bytes to transmit. If 0, clears the STALL
 * condition on the endpoint and immediately return.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_IO Failed to transmit or receive data.
 * @retval SR_ERR_TIMEOUT Timeout.
 */
static int fwram_ep_tx(struct libusb_device_handle *devhdl, uint8_t *tx_buf,
		       uint32_t tx_len, uint32_t timeout)
{
	int xfer_result, xfer_count;

	if (tx_len == 0) {
		libusb_clear_halt(devhdl, LIBUSB_ENDPOINT_OUT | EP_FIFO_FWRAM);
		return SR_OK;
	}

	xfer_result = libusb_bulk_transfer(devhdl,
					   LIBUSB_ENDPOINT_OUT | EP_FIFO_FWRAM,
					   tx_buf, tx_len, &xfer_count,
					   timeout);

	if (xfer_result != LIBUSB_SUCCESS)
		sr_err("Failed to transmit data to EP_FIFO_FWRAM: %s.",
		       libusb_error_name(xfer_result));

	if (xfer_result == LIBUSB_ERROR_TIMEOUT)
		return SR_ERR_TIMEOUT;
	else if (xfer_result != LIBUSB_SUCCESS)
		return SR_ERR_IO;

	if ((uint32_t)(xfer_count & 0x7fffffff) != tx_len) {
		sr_err("Incomplete data transmitted to EP_FIFO_FWRAM: "
		       "expecting %dB, actually sent %dB.",
		       tx_len, xfer_count);
		return SR_ERR_IO;
	}

	return SR_OK;
}

/**
 * Read control register through a device handle.
 */
static int read_reg_raw(struct libusb_device_handle *devhdl, uint32_t address,
			uint32_t *value)
{
	uint8_t trx[16];
	uint32_t val;
	int res;

	WL32(trx, REQ_KEY_READ);
	WL32(&trx[4], REQ_PACKET_LEN);
	WL32(&trx[8], address);
	WL32(&trx[12], 0);

	res = reg_ep_trx(devhdl, trx, sizeof(trx), trx, sizeof(trx));
	if (res != SR_OK) {
		sr_err("Failed to read register at 0x%x.", address);
		return res;
	}

	val = RL32(&trx[12]);
	sr_spew("reg 0x%04x => 0x%08x", address, val);
	*value = val;

	return SR_OK;
}

/**
 * Read control register.
 *
 * @param[in] sdi Device context.
 * @param[in] address Address.
 * @param[out] value Value.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_IO reg_ep_trx() fails.
 */
static int read_reg(const struct sr_dev_inst *sdi, uint32_t address,
		    uint32_t *value)
{
	struct sr_usb_dev_inst *const usb = sdi->conn;

	return read_reg_raw(usb->devhdl, address, value);
}

/** Call read_reg() and bubble up the result code if there's an error. */
#define TRY_READ_REG(sdi, address, value)                      \
	{                                                      \
		const int res = read_reg(sdi, address, value); \
		if (res != SR_OK)                              \
			return res;                            \
	}

#define TRY_READ_REG_RAW(devhdl, address, value)                      \
	{                                                             \
		const int res = read_reg_raw(devhdl, address, value); \
		if (res != SR_OK)                                     \
			return res;                                   \
	}

/**
 * Write control register through a device handle.
 */
static int write_reg_raw(struct libusb_device_handle *devhdl, uint32_t address,
			 uint32_t value)
{
	uint8_t trx[16];
	int res;

	sr_spew("reg 0x%04x <= 0x%08x", address, value);

	WL32(trx, REQ_KEY_WRITE);
	WL32(&trx[4], REQ_PACKET_LEN);
	WL32(&trx[8], address);
	WL32(&trx[12], value);

	res = reg_ep_trx(devhdl, trx, sizeof(trx), trx, sizeof(trx));
	if (res != SR_OK) {
		sr_err("Failed to write 0x%08x to register at 0x%x.", value,
		       address);
		return res;
	}

	/* Check response code. */
	if (RL32(&trx[12]) != REQ_WRITE_ACK) {
		sr_err("Device did not acknowledge write 0x%08x to register at"
		       " 0x%x.",
		       value, address);
		return SR_ERR_DATA;
	}

	return SR_OK;
}

/**
 * Write control register.
 *
 * @param[in] sdi Device context.
 * @param[in] address Address.
 * @param[in] value Value.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_IO reg_ep_trx() fails.
 * @retval SR_ERR_DATA Device returns invalid response.
 */
static int write_reg(const struct sr_dev_inst *sdi, uint32_t address,
		     uint32_t value)
{
	struct sr_usb_dev_inst *const usb = sdi->conn;

	return write_reg_raw(usb->devhdl, address, value);
}

/** Call write_reg() and bubble up the result code if there's an error. */
#define TRY_WRITE_REG(sdi, address, value)                      \
	{                                                       \
		const int res = write_reg(sdi, address, value); \
		if (res != SR_OK)                               \
			return res;                             \
	}
#define TRY_WRITE_REG_RAW(devhdl, address, value)                      \
	{                                                              \
		const int res = write_reg_raw(devhdl, address, value); \
		if (res != SR_OK)                                      \
			return res;                                    \
	}

/**
 * Write data to FWRAM.
 *
 * @param[in] sr_ctx
 * @param[in] devhdl
 * @param[in] fw_name
 * @param[in] bank FWRAM bank number.
 * @param[in] addr FWRAM base address for writing.
 * @param[in] erase_size Generate erase pattern (0xff fill) of this size.
 * @param[in] log_file_type File type string used for logging.
 * @param[in] timeout FIFO TX timeout.
 *
 * @retval SR_OK
 * @retval SR_ERR_MALLOC
 */
static int fwram_program(struct sr_context *sr_ctx,
			 struct libusb_device_handle *devhdl,
			 const char *fw_name, uint32_t bank, uint32_t addr,
			 uint32_t erase_size, const char *log_file_type,
			 uint32_t timeout)
{
	struct sr_resource fw_file;
	int res;
	uint8_t *fw;
	size_t fw_size_page_aligned;

	sr_info("Uploading %s file '%s'", log_file_type, fw_name);

	res = sr_resource_open(sr_ctx, &fw_file, SR_RESOURCE_FIRMWARE, fw_name);
	if (res != SR_OK)
		return res;

	fw_size_page_aligned = MAX(ALIGN_4K(fw_file.size), erase_size);

	TRY_WRITE_REG_RAW(devhdl, REG_FWRAM_WRITE_START, addr);
	TRY_WRITE_REG_RAW(devhdl, REG_FWRAM_WRITE_END, fw_size_page_aligned);
	TRY_WRITE_REG_RAW(devhdl, REG_FWRAM_WRITE_BANK, bank);

	fw = g_try_malloc(fw_size_page_aligned);
	if (fw == NULL)
		return SR_ERR_MALLOC;

	memset(fw, erase_size == 0 ? 0x00 : 0xff, fw_size_page_aligned);

	res = sr_resource_read(sr_ctx, &fw_file, fw, fw_file.size);
	if (res <= 0) {
		sr_err("Failed to read firmware file.");
		sr_resource_close(sr_ctx, &fw_file);
		g_free(fw);
		return res;
	}

	sr_resource_close(sr_ctx, &fw_file);

	fwram_ep_tx(devhdl, NULL, 0, 0);
	res = fwram_ep_tx(devhdl, fw, fw_size_page_aligned, timeout);

	g_free(fw);
	fw = NULL;

	if (res != SR_OK)
		return res;

	return SR_OK;
}

static int fpga_program(struct sr_context *sr_ctx,
			struct libusb_device_handle *devhdl,
			const char *bitstream_name)
{
	return fwram_program(sr_ctx, devhdl, bitstream_name, FWRAM_FPGA_CFGRAM,
			     FWRAM_ADDR_FPGA, 0, "FPGA bitstream", REQ_TIMEOUT);
}

static int mcu_program(struct sr_context *sr_ctx,
		       struct libusb_device_handle *devhdl, const char *fw_name)
{
	return fwram_program(sr_ctx, devhdl, fw_name, FWRAM_MCU_PROG_FLASH,
			     FWRAM_ADDR_MCU_USER, FWRAM_SIZE_MCU_USER,
			     "MCU firmware", MCU_PROGRAM_DELAY);
}

/**
 * Check FPGA register sanity.
 *
 * Currently this checks REG_CLK_CONF, REG_CLK_DIV and REG_STOP to determine
 * whether or not the FPGA bitstream has likely been uploaded.
 *
 * @param[in] devhdl
 * @return gboolean
 */
static gboolean fpga_reg_sanity_check(struct libusb_device_handle *devhdl)
{
	int res;
	uint32_t reg;

	reg = 0xdeadbeef;

	res = read_reg_raw(devhdl, REG_CLK_CONF, &reg);
	if (res != SR_OK)
		return FALSE;

	if ((reg & ~(CLK_CONF_MASK_SELECT | CLK_CONF_MASK_EDGE)) != 0)
		return FALSE;

	res = read_reg_raw(devhdl, REG_CLK_DIV, &reg);
	if (res != SR_OK)
		return FALSE;

	/* We only support down to 1MHz so this never goes above 99. */
	if (reg > 99)
		return FALSE;

	res = read_reg_raw(devhdl, REG_STOP, &reg);
	if (res != SR_OK)
		return FALSE;

	/* STOP should be 0 on normal operation unless the configuration has
	 * been interrupted (for now). */
	if (reg != 0x00000000)
		return FALSE;

	return TRUE;
}

/* ===== Device configuration helpers ===== */

/**
 * Calculate the number of samples to prepend before trigger.
 *
 * This should be the capture ratio (between 0 and 1) times the total number of
 * samples, or portion of the hardware buffer (~10% in streaming mode and ~90%
 * in buffered mode), whichever the shortest.
 *
 * @param buf_depth Hardware sample buffer depth (in bits).
 * @param capture_ratio
 * @param limit_samples
 * @param num_channels Total number of enabled channels.
 * @param streaming Device is in streaming mode?
 * @return Number of samples to prepend.
 */
static uint32_t conf_compute_sample_prepend(uint32_t buf_depth,
					    uint64_t capture_ratio,
					    uint64_t limit_samples,
					    uint32_t num_channels,
					    gboolean streaming)
{
	uint32_t max_prepend_percent;
	uint64_t tp, max_prepend;

	/* Round down to the nearest 1024 samples as per vendor software. */
	if (num_channels == 0)
		max_prepend = 0;
	else
		max_prepend = (buf_depth / num_channels) / 1024 * 1024;

	max_prepend_percent = streaming ? MAX_PREPEND_LEN_STRM :
					  MAX_PREPEND_LEN_BUFR;

	tp = capture_ratio * limit_samples / 100;
	tp = MIN(max_prepend * max_prepend_percent / 100, tp);

	return tp & 0xffffffff;
}

/**
 * Convert sigrok trigger patterns to device trigger configuration register
 * values.
 *
 * @param[in] trigger
 * @param[out] out_trigger
 * @retval SR_OK
 */
static int conf_compute_trigger(const struct sr_trigger *trigger,
				struct trigger_config *out_trigger)
{
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	const GSList *l, *m;
	uint32_t mask;

	//trigger = sr_session_trigger_get(session);

	if (trigger == NULL) {
		memset(out_trigger, 0, sizeof(*out_trigger));
		return SR_OK;
	}

	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			if (!match->channel->enabled ||
			    match->channel->type != SR_CHANNEL_LOGIC)
				continue;

			mask = 1 << (match->channel->index);

			switch (match->match) {
			case SR_TRIGGER_ONE:
				out_trigger->high_mask |= mask;
				break;
			case SR_TRIGGER_ZERO:
				out_trigger->low_mask |= mask;
				break;
			case SR_TRIGGER_RISING:
				out_trigger->rising_mask |= mask;
				break;
			case SR_TRIGGER_FALLING:
				out_trigger->falling_mask |= mask;
				break;
			case SR_TRIGGER_EDGE:
				out_trigger->rising_mask |= mask;
				out_trigger->falling_mask |= mask;
				break;
			}
		}
	}

	return SR_OK;
}

static int conf_compute_pwm(double clk, double freq, double duty,
			    uint32_t *out_period, uint32_t *out_duty)
{
	uint32_t period;

	if (freq == 0) {
		sr_err("PWM frequency shall not be 0.");
		*out_period = 0;
		*out_duty = 0;
		return SR_ERR_ARG;
	}

	period = round(clk / freq);
	*out_period = period - 1;
	*out_duty = round(duty * period);

	return SR_OK;
}

/**
 * Extract channel enablement status from the sigrok channel configuration.
 *
 * @param[in] sdi
 * @return Number of enabled channels and a bitmask of enabled channels.
 */
static struct channel_config
conf_compute_channel_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;

	struct sr_channel *channel;
	GSList *l;
	struct channel_config res;

	res.n = 0;
	res.mask = 0;

	for (l = devc->cg_logic->channels; l; l = l->next) {
		channel = l->data;
		if (channel->enabled) {
			res.n++;
			res.mask |= 1 << (channel->index);
		}
	}

	return res;
}

/**
 * Calculate transfer buffer size based on capture configuration.
 *
 * Buffer size is currently set to approximately 10ms, aligned to both RAM page
 * size (4KiB) and frame size (num of channels * stripe size).
 *
 * @param[in] speed SuperSpeed or HighSpeed.
 * @param[in] samplerate
 * @param[in] nchannels
 * @return Calculated buffer size.
 */
static uint32_t conf_compute_buf_size(enum libusb_speed speed,
				      uint64_t samplerate, uint32_t nchannels)
{
	uint32_t max, typical, final;

	max = speed >= LIBUSB_SPEED_SUPER ? MAX_BUF_SIZE_SS : MAX_BUF_SIZE_HS;

	typical = (samplerate * nchannels) / 8 / (1000 / BUF_SIZE_MS);

	final = ALIGN_CH_FLOOR(MIN(max, typical), nchannels);
	sr_spew("Computed buffer size is %u bytes", final);

	return final;
}

/**
 * Configure sampler clock.
 *
 * @param[in] sdi
 * @retval SR_OK
 * @retval SR_ERR_ARG
 */
static int conf_compute_sampler_clock(uint64_t samplerate,
				      gboolean invert_clock,
				      uint32_t *out_clk_conf,
				      uint32_t *out_clk_div)
{
	uint32_t clk_conf, clk_div;
	gboolean found;

	if (samplerate < SR_MHZ(100)) {
		clk_conf = CLK_100MHZ;
		clk_div = SR_MHZ(100) / samplerate - 1;
		if (SR_MHZ(100) / (clk_div + 1) != samplerate) {
			sr_err("Cannot determine divider value from samplerate "
			       "%" PRIu64 ".",
			       samplerate);
			return SR_ERR_ARG;
		}
	} else {
		clk_div = 0;
		found = FALSE;
		for (clk_conf = 0; clk_conf < ARRAY_SIZE(clk_conf_table);
		     clk_conf++)
			if (clk_conf_table[clk_conf] == samplerate) {
				found = TRUE;
				break;
			}
		if (!found) {
			sr_err("Cannot determine clock config from samplerate "
			       "%" PRIu64 ".",
			       samplerate);
			return SR_ERR_ARG;
		}
	}

	if (invert_clock)
		clk_conf |= CLK_CONF_MASK_EDGE;

	*out_clk_conf = clk_conf;
	*out_clk_div = clk_div;

	return SR_OK;
}

/* ===== Capture lifecycle control ===== */

static void cap_halt(struct dev_context *devc)
{
	if (devc->cap.state != CAP_STATE_HALT &&
	    devc->cap.state != CAP_STATE_CLEANUP &&
	    devc->cap.state != CAP_STATE_INIT)
		devc->cap.state = CAP_STATE_HALT;
}

static int cap_data_init(const struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;

	size_t tr_pool_size;
	int8_t max_enabled_channel, i;
	uint32_t mask;
	uint8_t sample_width;

	max_enabled_channel = -1;

	for (mask = devc->channels.mask, i = 0; i < 32 && mask != 0;
	     mask >>= 1, i++)
		if (mask & 1)
			max_enabled_channel = i;

	if (max_enabled_channel < 8)
		sample_width = 1;
	else if (max_enabled_channel >= 8 && max_enabled_channel < 16)
		sample_width = 2;
	else if (max_enabled_channel >= 16 && max_enabled_channel < 24)
		sample_width = 3;
	else
		sample_width = 4;

	devc->cap.sample_width = sample_width;

	sr_info("Highest channel is %d", max_enabled_channel);
	sr_info("Use sample width of %u", sample_width);

	devc->cap.skip_trigger =
		!(devc->trigger.low_mask | devc->trigger.high_mask |
		  devc->trigger.rising_mask | devc->trigger.falling_mask) &&
		devc->ext_slope == EXT_TRIG_OFF;

	devc->cap.tr_buffer_size =
		devc->buf_size / devc->channels.n * 8 * sample_width;
	tr_pool_size = devc->cap.tr_buffer_size * NUM_SIMUL_XFERS;

	sr_spew("Allocating %zu bytes for transpose buffer", tr_pool_size);

	devc->cap.tr_buffer = g_try_malloc0(tr_pool_size);
	if (devc->cap.tr_buffer == NULL)
		return SR_ERR_MALLOC;

	devc->cap.tr_workers = g_thread_pool_new(&xfer_sample_transpose_worker,
						 devc, NUM_TRANSPOSE_WORKERS,
						 TRUE, NULL);
	if (devc->cap.tr_workers == NULL) {
		g_free(devc->cap.tr_buffer);
		return SR_ERR_MALLOC;
	}
	devc->cap.tr_out_queue = g_async_queue_new();

	return SR_OK;
}

static void cap_data_fini(const struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;

	g_thread_pool_free(devc->cap.tr_workers, TRUE, TRUE);
	devc->cap.tr_workers = NULL;
	g_async_queue_unref(devc->cap.tr_out_queue);
	devc->cap.tr_out_queue = NULL;
	g_free(devc->cap.tr_buffer);
	devc->cap.tr_buffer = NULL;
	devc->cap.tr_buffer_size = 0;
}

/* ===== Sample transfer control ===== */

/**
 * Shortcut to resubmit an individual transfer. Must be called at a control
 * flow tail.
 */
static void xfer_sample_resubmit(struct libusb_transfer *xfer,
				 struct dev_context *devc)
{
	int res;

	res = libusb_submit_transfer(xfer);
	if (res != LIBUSB_SUCCESS) {
		sr_err("Failed to resubmit transfer: %s. Aborting "
		       "session.",
		       libusb_error_name(res));
		cap_halt(devc);
	}
	devc->cap.n_active_data_xfers++;
}

static void LIBUSB_CALL xfer_sample_event(struct libusb_transfer *xfer)
{
	struct sample_xfer_user_data *const user_data = xfer->user_data;
	const struct sr_dev_inst *const sdi = user_data->sdi;
	struct dev_context *const devc = sdi->priv;
	const uint64_t frame_size = devc->channels.n * STRIPE_SIZE_BYTES;

	uint64_t samples_received;

	devc->cap.n_active_data_xfers--;

	switch (xfer->status) {
	case LIBUSB_TRANSFER_CANCELLED:
		sr_spew("Transfer cancelled.");
		break;
	case LIBUSB_TRANSFER_NO_DEVICE:
	case LIBUSB_TRANSFER_OVERFLOW:
	case LIBUSB_TRANSFER_ERROR:
	case LIBUSB_TRANSFER_STALL:
		sr_err("Unrecoverable status %s. Aborting session.",
		       libusb_error_name(xfer->status));
		cap_halt(devc);
		break;
	case LIBUSB_TRANSFER_TIMED_OUT:
	case LIBUSB_TRANSFER_COMPLETED:
		sr_spew("Got %d bytes from sample FIFO", xfer->actual_length);

		if (G_UNLIKELY(devc->cap.state != CAP_STATE_SAMPLE_XFER))
			/* Ignore data and wait for cancellation if not
			 * receiving samples. */
			break;

		if (G_UNLIKELY(xfer->actual_length == 0)) {
			/* Timeout/0 bytes received could indicate that the
			 * device is still waiting for captured data. It's safe
			 * to just resubmit the transfer. */
			xfer_sample_resubmit(xfer, devc);
			break;
		} else if (G_UNLIKELY(xfer->actual_length % frame_size != 0)) {
			sr_err("Misaligned samples. Aborting session.");
			cap_halt(devc);
			break;
		}

		samples_received =
			devc->cap.samples_received +
			xfer->actual_length / frame_size * SAMPLES_IN_FRAME;

		/* Submit to the sample transpose thread pool. */
		user_data->seq = devc->cap.send_seq;
		g_thread_pool_push(devc->cap.tr_workers, xfer, NULL);

		devc->cap.samples_received = samples_received;
		devc->cap.send_seq++;

		if (G_UNLIKELY(samples_received >= devc->limit_samples)) {
			sr_info("Got enough samples. Transferring capture "
				"state to HALT.");
			cap_halt(devc);
			break;
		}
	}
}

static int xfer_sample_cmp(gconstpointer a, gconstpointer b, gpointer user_data)
{
	const struct libusb_transfer *const xfa = a;
	const struct libusb_transfer *const xfb = b;
	const struct sample_xfer_user_data *const aa = xfa->user_data;
	const struct sample_xfer_user_data *const bb = xfb->user_data;

	(void)user_data;

	if (G_LIKELY(aa->seq > bb->seq))
		return 1;
	else if (aa->seq < bb->seq)
		return -1;
	else
		return 0;
}

/**
 * Transpose the DSLogic-style samples (striped channels) to sigrok-style
 * (bitfield samples padded to bytes).
 *
 * Converts the samples from format of
 *
 * `aaa...bbb...ccc...ddd...`
 *
 * to
 *
 * `abcd...abcd...abcd...`
 *
 * @param src Samples in striped channels format.
 *
 * @param length Total length of the samples in bytes. Must be aligned to 64
 *               samples.
 *
 * @param dst_ptr Samples in bitfield format.
 *
 * @param channel_count Number of active channels. Must be consistent with
 *                      channel_mask.
 *
 * @param channel_mask Active channels. Must be consistent with channel_count.
 *
 * @param sample_width_bytes Sample width in bytes (2 or 4 depending on
 *                           variant).
 */
static size_t xfer_sample_transpose(const uint8_t *src, size_t length,
				    uint8_t *dst_ptr, size_t channel_count,
				    uint32_t channel_mask,
				    size_t sample_width_bytes)
{
	const uint8_t *const end_ptr = src + length;
	const size_t sample_width_bits = sample_width_bytes * 8;
	/* Only active channels will have stripes of data available. */
	const size_t in_frame_size = channel_count * STRIPE_SIZE_BYTES;
	/* 64 converted samples, each 2 or 4 bytes in size. */
	const size_t out_frame_size = STRIPE_SIZE_BITS * sample_width_bytes;

	const uint8_t *src_ptr, *stripe_ptr;
	uint8_t *out_ptr, *out_sample_ptr;
	uint8_t channel;
	uint64_t stripe;
	size_t out_samples, out_size;

	out_ptr = dst_ptr;
	out_samples = 0;

	out_size = out_frame_size * length / in_frame_size;
	memset(dst_ptr, 0, out_size);

	/* Process one frame at a time. */
	for (src_ptr = src; src_ptr < end_ptr; src_ptr += in_frame_size) {
		stripe_ptr = src_ptr;
		/* TODO: use ctz + bit clear here may be better. */
		for (channel = 0; channel < sample_width_bits; channel++) {
			/* This stripe does not belong to this bit in the
			   output sample. */
			if (!(channel_mask & (1 << channel)))
				continue;

			/* Device endian. */
			stripe = RL64(stripe_ptr);

			/* Write the stripe as a column on the output sample
			 * matrix. */
			for (out_sample_ptr = out_ptr; stripe != 0;
			     out_sample_ptr += sample_width_bytes) {
				if (stripe & 1)
					out_sample_ptr[channel / 8] |=
						1 << (channel % 8);
				stripe >>= 1;
			}

			stripe_ptr += STRIPE_SIZE_BYTES;
		}

		out_ptr += out_frame_size;
		out_samples += STRIPE_SIZE_BITS;
	}
	return out_samples;
}

static void xfer_sample_transpose_worker(gpointer data, gpointer user_data)
{
	const struct dev_context *const devc = user_data;
	struct libusb_transfer *const xfer = data;
	struct sample_xfer_user_data *const xfer_user_data = xfer->user_data;

	size_t samples;

	samples = xfer_sample_transpose(xfer->buffer, xfer->actual_length,
					xfer_user_data->tr_buffer,
					devc->channels.n, devc->channels.mask,
					devc->cap.sample_width);

	xfer_user_data->packet.type = SR_DF_LOGIC;
	xfer_user_data->packet.payload = &xfer_user_data->logic;
	xfer_user_data->logic.length = samples * devc->cap.sample_width;
	xfer_user_data->logic.unitsize = devc->cap.sample_width;
	xfer_user_data->logic.data = xfer_user_data->tr_buffer;

	g_async_queue_push_sorted(devc->cap.tr_out_queue, xfer,
				  &xfer_sample_cmp, NULL);
}

/* ===== Sample transfer lifecycle control ===== */

/**
 * Deletes the sample transfer pool only. Should terminate all transfers
 * before calling this.
 */
static void cap_sample_xfer_fini(const struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;

	int i;

	if (devc->cap.data_xfers == NULL) {
		devc->cap.state = CAP_STATE_INIT;
		return;
	}

	for (i = 0; i < NUM_SIMUL_XFERS; i++) {
		g_free(devc->cap.data_xfers[i]->buffer);
		g_free(devc->cap.data_xfers[i]->user_data);
		libusb_free_transfer(devc->cap.data_xfers[i]);
		devc->cap.data_xfers[i] = NULL;
	}

	g_free(devc->cap.data_xfers);
	devc->cap.data_xfers = NULL;
	devc->cap.state = CAP_STATE_INIT;
	devc->cap.samples_received = 0;
	devc->cap.samples_sent = 0;
}

/**
 * Allocates the sample transfer pool.
 */
static int cap_sample_xfer_init(const struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;
	struct capture_ctx *const cap = &devc->cap;
	struct sr_usb_dev_inst *const usb = sdi->conn;

	struct sample_xfer_user_data *user_data;
	unsigned char *xfer_buf;
	int i;

	devc->cap.data_xfers = g_try_malloc0(sizeof(struct libusb_transfer *) *
					     NUM_SIMUL_XFERS);
	if (devc->cap.data_xfers == NULL)
		return SR_ERR_MALLOC;

	for (i = 0; i < NUM_SIMUL_XFERS; i++) {
		xfer_buf = g_try_malloc0(devc->buf_size);
		if (xfer_buf == NULL) {
			cap_sample_xfer_fini(sdi);
			return SR_ERR_MALLOC;
		}
		devc->cap.data_xfers[i] = libusb_alloc_transfer(0);
		if (devc->cap.data_xfers[i] == NULL) {
			g_free(xfer_buf);
			cap_sample_xfer_fini(sdi);
			return SR_ERR_MALLOC;
		}

		user_data = g_malloc0(sizeof(*user_data));

		user_data->sdi = sdi;
		user_data->tr_buffer = &cap->tr_buffer[cap->tr_buffer_size * i];

		libusb_fill_bulk_transfer(
			devc->cap.data_xfers[i], usb->devhdl,
			LIBUSB_ENDPOINT_IN | EP_FIFO_SAMPLE, xfer_buf,
			devc->buf_size, &xfer_sample_event, user_data,
			BUF_SIZE_MS * NUM_SIMUL_XFERS * 5 / 4);
	}

	return SR_OK;
}

/**
 * Start all transfers in the sample transfer pool.
 */
static int cap_sample_xfer_begin(const struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;
	struct sr_usb_dev_inst *const usb = sdi->conn;

	int i, ret;

	libusb_clear_halt(usb->devhdl, LIBUSB_ENDPOINT_IN | EP_FIFO_SAMPLE);

	for (i = 0; i < NUM_SIMUL_XFERS; i++) {
		ret = libusb_submit_transfer(devc->cap.data_xfers[i]);
		if (ret == LIBUSB_ERROR_NO_MEM) {
			sr_warn("OS USB transfer limit reached after "
				"submitting %d transfers (%u bytes total). "
				"You may wish to increase this limit if you "
				"encounter sample buffering problems.",
				i, devc->buf_size * i);
			return SR_OK;
		} else if (ret != LIBUSB_SUCCESS) {
			sr_err("Failed to submit transfer %d: %s", i,
			       libusb_error_name(ret));
			return SR_ERR_IO;
		}
		devc->cap.n_active_data_xfers++;
	}

	return SR_OK;
}

/**
 * Signal all the transfers to terminate themselves.
 */
static void cap_sample_xfer_end(const struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;
	struct capture_ctx *const cap = &devc->cap;

	uint32_t i;

	if (cap->data_xfers == NULL)
		return;

	for (i = 0; i < NUM_SIMUL_XFERS; i++) {
		struct libusb_transfer *xfer = cap->data_xfers[i];
		if (xfer != NULL)
			libusb_cancel_transfer(xfer);
	}
}

/* ===== Capture state machine ===== */

/**
 * Helper function to send samples and trigger point data to sigrok.
 */
static inline void cap_send(struct capture_ctx *const cap,
			    const struct sr_dev_inst *const sdi,
			    struct sample_xfer_user_data *const finished_data)
{
	struct sr_datafeed_logic *const logic = &finished_data->logic;

	uint64_t next_sent, samples, total_len, tp;

	tp = cap->trigger_point_real;

	samples = logic->length / logic->unitsize;
	next_sent = cap->samples_sent + samples;

	/* Don't draw the trigger line when no trigger has been activated. */
	if (cap->skip_trigger)
		goto send;

	if (G_UNLIKELY(tp > cap->samples_sent && tp < next_sent)) {
		sr_info("Trigger point hit. Sending trigger info.");
		total_len = logic->length;

		/* Adjust length of the first packet. */
		logic->length = (tp - cap->samples_sent) * logic->unitsize;

		/* Send the first packet and trigger packet. */
		sr_session_send(sdi, &finished_data->packet);
		std_session_send_df_trigger(sdi);

		/* Make second packet out of the rest of the data. */
		logic->data = &finished_data->tr_buffer[logic->length];
		logic->length = total_len - logic->length;

	} else if (G_UNLIKELY(tp == cap->samples_sent))
		/* Prevent zero length logic packet. */
		std_session_send_df_trigger(sdi);

send:
	sr_session_send(sdi, &finished_data->packet);
	cap->samples_sent = next_sent;
}

static int cap_top_event_handler(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *const sdi = cb_data;
	struct drv_context *const drvc = sdi->driver->context;
	struct sr_usb_dev_inst *const usb = sdi->conn;
	struct dev_context *const devc = sdi->priv;
	struct capture_ctx *const cap = &devc->cap;

	struct trigger_status status;
	struct libusb_transfer *finished_xfer;
	struct sample_xfer_user_data *finished_data;
	struct timeval tv;

	(void)fd;
	(void)revents;

	/* libusb event handler. */
	memset(&tv, 0, sizeof(tv));
	libusb_handle_events_timeout_completed(drvc->sr_ctx->libusb_ctx, &tv,
					       NULL);

	/* Sample transpose output queue handler. */
	/* Acquire lock to prevent sequence inversion. */
	g_async_queue_lock(cap->tr_out_queue);

	while ((finished_xfer = g_async_queue_try_pop_unlocked(
			cap->tr_out_queue)) != NULL) {
		finished_data = finished_xfer->user_data;
		if (G_UNLIKELY(cap->recv_seq != 0 &&
			       finished_data->seq < cap->recv_seq)) {
			sr_err("Sequence inversion detected. Aborting "
			       "session.");
			cap_halt(devc);
			break;
		} else if (G_UNLIKELY(finished_data->seq != cap->recv_seq)) {
			/* Rollback previous pop operation. */
			sr_info("Task %" PRIu64 " completed out of order.",
				finished_data->seq);
			g_async_queue_push_front_unlocked(cap->tr_out_queue,
							  finished_xfer);
			break;
		} else {
			/* Unload the data and resubmit transfer. */
			cap_send(&devc->cap, sdi, finished_data);
			cap->recv_seq++;
			xfer_sample_resubmit(finished_xfer, devc);
		}
	}

	g_async_queue_unlock(cap->tr_out_queue);

	switch (cap->state) {
	case CAP_STATE_WAIT_TRIGGER:
		/* Continue to wait for trigger condition if trigger hasn't
		   been fired yet. */
		if (ep0_get_trigger_status(usb->devhdl, &status) != SR_OK)
			break;

		sr_spew("Sample tick %" PRIu64, status.sample_offset);
		if (status.pos_real != 0) {
			sr_info("Triggered after %" PRIu64 " samples, "
				"point at %" PRId32 ", "
				"trigger status 0x%08" PRIx32 ". "
				"Transferring capture state to SAMPLE_XFER.",
				status.sample_offset, status.pos_real,
				status.activated);
			cap->state = CAP_STATE_SAMPLE_XFER;
			cap->trigger_point_real = status.pos_real;
			std_session_send_df_header(sdi);
			cap_sample_xfer_begin(sdi);
		}
		break;
	case CAP_STATE_HALT:
		cap_sample_xfer_end(sdi);
		sr_info("Transferring capture state to CLEANUP.");
		cap->state = CAP_STATE_CLEANUP;
		break;
	case CAP_STATE_CLEANUP:
		/* Make sure all the transfers stop before proceeding. */
		if (cap->n_active_data_xfers != 0 ||
		    cap->send_seq != cap->recv_seq)
			break;
		/* Destroy resources. */
		cap_sample_xfer_fini(sdi);
		cap_data_fini(sdi);
		/* Safe to terminate the event loop. */
		std_session_send_df_end(sdi);
		loop_end(sdi);
		break;
	default:
		/* Do nothing. */
		break;
	}

	return TRUE;
}

/* ===== Happy Winblows shims ===== */

static int loop_begin(const struct sr_dev_inst *sdi,
		      sr_receive_data_callback cb)
{
#if defined(_WIN32) || PX_LOGIC_FORCE_TIMER == 1
	sr_spew("Using timer to process events.");
	return sr_session_source_add(sdi->session, -1, 0, POLL_TIMEOUT, cb,
				     (void *)sdi);
#else
	sr_spew("Using libusb pollfd to process events.");
	return usb_source_add(sdi->session, sdi->session->ctx, POLL_TIMEOUT, cb,
			      (void *)sdi);
#endif
}

static int loop_end(const struct sr_dev_inst *sdi)
{
#if defined(_WIN32) || PX_LOGIC_FORCE_TIMER == 1
	return sr_session_source_remove(sdi->session, -1);
#else
	return usb_source_remove(sdi->session, sdi->session->ctx);
#endif
}

/* ===== Public interface ===== */

SR_PRIV enum device_variant
px_logic_probe_variant(struct libusb_device_handle *devhdl)
{
	uint32_t variant;
	int result;

	result = read_reg_raw(devhdl, REG_DEV_VARIANT, &variant);
	if (result != SR_OK)
		return VARIANT_UNKNOWN;

	if (variant >= VARIANT_MAX) {
		sr_warn("%s, Unknown device variant %d.", __func__, variant);
		return VARIANT_UNKNOWN;
	}

	return variant;
}

SR_PRIV int px_logic_probe_mcu(struct sr_context *sr_ctx,
			       struct libusb_device_handle *devhdl)
{
	int res;
	uint32_t fw_version;

	fw_version = 0;

	TRY_READ_REG_RAW(devhdl, REG_MCU_FW_VERSION, &fw_version);
	sr_info("MCU firmware version: 0x%08x", fw_version);

	if (fw_version == MCU_FW_VERSION) {
		sr_info("MCU firmware version is supported. Skip MCU "
			"programming.");
		return SR_OK;
	}

	sr_info("Reprogramming MCU...");

	/* It's normal for the USB controller to stop responding after
	   this call. Ignoring the timeout result. */
	res = mcu_program(sr_ctx, devhdl, MCU_FW_NAME);
	if (res != SR_OK && res != SR_ERR_TIMEOUT)
		return res;

	/* Do not check the response code here, as the device will also
	   not acknowledge a reset. */
	write_reg_raw(devhdl, REG_MCU_RESET, 0);

	return SR_ERR_DEV_CLOSED;
}

SR_PRIV int px_logic_probe_fpga(struct sr_context *sr_ctx,
				struct libusb_device_handle *devhdl,
				gboolean reprogram)
{
	int res;
	int fail_count;

	if (!reprogram && fpga_reg_sanity_check(devhdl)) {
		sr_info("FPGA register config is sane. Skip FPGA initialization.");
		return SR_OK;
	}

	sr_info("Initializing FPGA...");

	res = fpga_program(sr_ctx, devhdl, FPGA_STAGE1_NAME);
	if (res != SR_OK)
		return res;

	res = fpga_program(sr_ctx, devhdl, FPGA_STAGE2_NAME);
	if (res != SR_OK)
		return res;

	res = SR_ERR_TIMEOUT;
	/* Wait until FPGA is fully configured. */
	for (fail_count = 0; fail_count < FPGA_CHECK_COUNT; fail_count++) {
		if (fpga_reg_sanity_check(devhdl)) {
			res = SR_OK;
			break;
		}
		g_usleep(FPGA_CHECK_PERIOD_US);
	}

	if (res == SR_ERR_TIMEOUT)
		sr_err("FPGA initialization never completes.");

	return res;
}

SR_PRIV int px_logic_dev_open(const struct sr_dev_inst *sdi)
{
	struct sr_usb_dev_inst *const usb = sdi->conn;
	struct sr_dev_driver *const di = sdi->driver;
	struct drv_context *const drvc = di->context;
	struct sr_context *const ctx = drvc->sr_ctx;
	struct dev_context *const devc = sdi->priv;

	libusb_device **devlist;

	struct libusb_device_descriptor des;

	int ret = SR_ERR, i, device_count;
	char connection_id[64];

	device_count = libusb_get_device_list(ctx->libusb_ctx, &devlist);
	if (device_count < 0) {
		sr_err("Failed to get device list: %s.",
		       libusb_error_name(device_count));
		return SR_ERR;
	}

	for (i = 0; i < device_count; i++) {
		libusb_get_device_descriptor(devlist[i], &des);

		if (des.idVendor != devc->config.vid ||
		    des.idProduct != devc->config.pid)
			continue;

		if ((sdi->status == SR_ST_INITIALIZING) ||
		    (sdi->status == SR_ST_INACTIVE)) {
			/* Check device by its physical USB bus/port address. */
			if (usb_get_port_path(devlist[i], connection_id,
					      sizeof(connection_id)) < 0)
				continue;

			if (strcmp(sdi->connection_id, connection_id))
				/* This is not the one. */
				continue;
		}

		ret = libusb_open(devlist[i], &usb->devhdl);
		if (ret == LIBUSB_SUCCESS) {
			/*
			 * First time we touch this device after FW
			 * upload, so we don't know the address yet.
			 */
			if (usb->address == 0xff)
				usb->address =
					libusb_get_device_address(devlist[i]);
		} else if (ret == LIBUSB_ERROR_NO_DEVICE) {
			/* Do not log device not found error as it may come up
			   when waiting for device to reboot. */
			ret = SR_ERR;
			break;
		} else {
			sr_err("Failed to open device: %s.",
			       libusb_error_name(ret));
			ret = SR_ERR;
			break;
		}

		ret = SR_OK;

		break;
	}

	libusb_free_device_list(devlist, 1);

	return ret;
}

SR_PRIV int px_logic_receive_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;

	uint32_t clk_conf, clk_select, clk_div, mode, num_samples_lo,
		num_samples_hi, pwm0_conf, pwm0_period, pwm0_duty, pwm1_conf,
		pwm1_period, pwm1_duty, ext_slope;
	uint64_t samplerate;

	clk_conf = 0;
	clk_select = 0;
	clk_div = 0;
	num_samples_lo = 0;
	num_samples_hi = 0;
	mode = 0;
	pwm0_conf = 0;
	pwm0_period = 0;
	pwm0_duty = 0;
	pwm1_conf = 0;
	pwm1_period = 0;
	pwm1_duty = 0;
	ext_slope = 0;

	TRY_READ_REG(sdi, REG_CLK_CONF, &clk_conf);
	TRY_READ_REG(sdi, REG_CLK_DIV, &clk_div);
	TRY_READ_REG(sdi, REG_NUM_SAMPLES_LO, &num_samples_lo);
	TRY_READ_REG(sdi, REG_NUM_SAMPLES_HI, &num_samples_hi);
	TRY_READ_REG(sdi, REG_MODE, &mode);
	TRY_READ_REG(sdi, REG_PWM0_CONF, &pwm0_conf);
	TRY_READ_REG(sdi, REG_PWM0_CMP_PERIOD, &pwm0_period);
	TRY_READ_REG(sdi, REG_PWM0_CMP_DUTY, &pwm0_duty);
	TRY_READ_REG(sdi, REG_PWM1_CONF, &pwm1_conf);
	TRY_READ_REG(sdi, REG_PWM1_CMP_PERIOD, &pwm1_period);
	TRY_READ_REG(sdi, REG_PWM1_CMP_DUTY, &pwm1_duty);
	TRY_READ_REG(sdi, REG_TRIG_EXT_MODE, &ext_slope);

	sr_info("Clock configuration on device: CLK_CONF = 0x%08x, "
		"CLK_DIV = 0x%08x",
		clk_conf, clk_div);
	clk_select = clk_conf & CLK_CONF_MASK_SELECT;
	if (clk_div != 0 && clk_select != CLK_100MHZ) {
		sr_info("Custom sampling clock configuration is not supported "
			"yet. Falling back to a safe default.");
		samplerate = SR_MHZ(125);
	} else if (clk_select != CLK_100MHZ) {
		samplerate = clk_conf_table[clk_select];
	} else {
		switch (clk_div) {
		case 99:
			samplerate = SR_MHZ(1);
			break;
		case 49:
			samplerate = SR_MHZ(2);
			break;
		case 24:
			samplerate = SR_MHZ(4);
			break;
		case 19:
			samplerate = SR_MHZ(5);
			break;
		case 9:
			samplerate = SR_MHZ(10);
			break;
		case 4:
			samplerate = SR_MHZ(20);
			break;
		case 3:
			samplerate = SR_MHZ(25);
			break;
		case 1:
			samplerate = SR_MHZ(50);
			break;
		case 0:
			samplerate = SR_MHZ(100);
			break;
		default:
			sr_info("Irregular divider is not supported yet. "
				"Falling back to a safe default.");
			samplerate = SR_MHZ(125);
			break;
		}
	}

	devc->samplerate = samplerate;
	devc->invert_clock = !!(clk_conf & CLK_CONF_MASK_EDGE);

	devc->limit_samples = ((uint64_t)num_samples_lo) |
			      ((uint64_t)num_samples_hi << 32);

	sr_info("Mode configuration on device: 0x%08x", mode);
	devc->streaming = mode & MODE_MASK_STREAMING;
	devc->filter = mode & MODE_MASK_FILTER_EN;

	sr_info("PWM0 conf=0x%08x, period=%u, duty=%u", pwm0_conf, pwm0_period,
		pwm0_duty);
	sr_info("PWM1 conf=0x%08x, period=%u, duty=%u", pwm1_conf, pwm1_period,
		pwm1_duty);
	devc->pwm[0].enabled = !!(pwm0_conf & 1);
	devc->pwm[0].freq = (double)FPGA_F_PWM / (pwm0_period + 1);
	devc->pwm[0].duty = (double)pwm0_duty / (pwm0_period + 1);
	devc->pwm[1].enabled = !!(pwm1_conf & 1);
	devc->pwm[1].freq = (double)FPGA_F_PWM / (pwm1_period + 1);
	devc->pwm[1].duty = (double)pwm1_duty / (pwm1_period + 1);

	sr_info("REG_TRIG_EXT_MODE=0x%08x", ext_slope);
	if (ext_slope < EXT_TRIG_MAX) {
		devc->ext_slope = ext_slope;
	}

	return SR_OK;
}

SR_PRIV int px_logic_send_config(const struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;
	const double vth = devc->voltage_threshold;
	const uint64_t limit_samples = devc->limit_samples;

	int ret;
	uint32_t mode_reg, vref_period, vref_duty, clk_conf, clk_div,
		prepend_samples;

	/* Prepare required register values. */
	devc->channels = conf_compute_channel_config(sdi);
	devc->buf_size = conf_compute_buf_size(
		devc->config.speed, devc->samplerate, devc->channels.n);

	ret = conf_compute_pwm(FPGA_F_PWM_VREF, FPGA_PWM_VREF_PERIOD,
			       vth * FPGA_INPUT_VDIV / FPGA_VCCIO, &vref_period,
			       &vref_duty);
	if (ret != SR_OK)
		return ret;

	ret = conf_compute_sampler_clock(devc->samplerate, devc->invert_clock,
					 &clk_conf, &clk_div);
	if (ret != SR_OK)
		return ret;

	prepend_samples = conf_compute_sample_prepend(
		devc->config.max_buffer_depth, devc->capture_ratio,
		limit_samples, devc->channels.n, devc->streaming);

	ret = conf_compute_trigger(sr_session_trigger_get(sdi->session),
				   &devc->trigger);
	if (ret != SR_OK)
		return ret;

	/* Set input reference voltage. */
	TRY_WRITE_REG(sdi, REG_PWM_VREF_CMP_PERIOD, vref_period);
	TRY_WRITE_REG(sdi, REG_PWM_VREF_CMP_DUTY, vref_duty);

	TRY_WRITE_REG(sdi, REG_CHANNEL_EN, 0);

	/* Set mode register */
	mode_reg = MODE_MASK_INIT | MODE_MASK_INIT2 |
		   (devc->streaming ? MODE_MASK_STREAMING : 0);

	TRY_WRITE_REG(sdi, REG_MODE, mode_reg);
	TRY_WRITE_REG(sdi, REG_MODE, mode_reg | MODE_MASK_UNK_4);
	TRY_WRITE_REG(sdi, REG_MODE, mode_reg);

	TRY_WRITE_REG(sdi, REG_STOP, 0xffffffff);

	TRY_WRITE_REG(sdi, REG_SAMPLE_BUFFER_SIZE, devc->buf_size);
	TRY_WRITE_REG(sdi, REG_XFER_BUFFER_SIZE, devc->buf_size);

	TRY_WRITE_REG(sdi, REG_NUM_SAMPLES_LO, limit_samples & 0xffffffff);
	TRY_WRITE_REG(sdi, REG_NUM_SAMPLES_HI, limit_samples >> 32);

	TRY_WRITE_REG(sdi, REG_TRIG_EXT_MODE, devc->ext_slope);
	/* Always enable trigger out channel. */
	TRY_WRITE_REG(sdi, REG_TRIG_OUT_EN, 1);

	TRY_WRITE_REG(sdi, REG_CLK_CONF, clk_conf);
	TRY_WRITE_REG(sdi, REG_CLK_DIV, clk_div);

	TRY_WRITE_REG(sdi, REG_ENABLED_NUM_CH, devc->channels.n);

	/* Clear the BLOCK_START register. */
	TRY_WRITE_REG(sdi, REG_SAMPLER_RESET, 0);

	TRY_WRITE_REG(sdi, REG_TRIG_POINT, prepend_samples);
	TRY_WRITE_REG(sdi, REG_TRIG_LOW, devc->trigger.low_mask);
	TRY_WRITE_REG(sdi, REG_TRIG_HIGH, devc->trigger.high_mask);
	TRY_WRITE_REG(sdi, REG_TRIG_RISING, devc->trigger.rising_mask);
	TRY_WRITE_REG(sdi, REG_TRIG_FALLING, devc->trigger.falling_mask);

	TRY_WRITE_REG(sdi, REG_CHANNEL_EN, devc->channels.mask);

	mode_reg &= ~(MODE_MASK_INIT | MODE_MASK_INIT2);

	TRY_WRITE_REG(sdi, REG_MODE,
		      mode_reg | (devc->filter ? MODE_MASK_FILTER_EN : 0));

	return SR_OK;
}

SR_PRIV int px_logic_send_config_pwm(const struct sr_dev_inst *sdi,
				     uint8_t channel)
{
	struct dev_context *const devc = sdi->priv;

	const struct pwm_reg *reg;
	const struct pwm_config *pwm;
	int ret;
	uint32_t period, duty;

	if (channel >= NUM_PWM_CHANNELS) {
		sr_err("Invalid PWM channel %u", channel);
		return SR_ERR_ARG;
	}

	reg = &pwm_reg_list[channel];
	pwm = &devc->pwm[channel];

	ret = conf_compute_pwm(FPGA_F_PWM, pwm->freq, pwm->duty, &period,
			       &duty);
	if (ret != SR_OK) {
		sr_err("PWM channel %u contains invalid configuration",
		       channel);
		return ret;
	}

	TRY_WRITE_REG(sdi, reg->conf, 0);

	TRY_WRITE_REG(sdi, reg->period, period);
	TRY_WRITE_REG(sdi, reg->duty, duty);
	if (pwm->enabled)
		TRY_WRITE_REG(sdi, reg->conf, pwm->enabled);

	return SR_OK;
}

SR_PRIV int px_logic_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;
	int res;

	res = px_logic_send_config(sdi);
	if (res != SR_OK)
		return res;

	res = cap_data_init(sdi);
	if (res != SR_OK)
		return res;

	res = cap_sample_xfer_init(sdi);
	if (res != SR_OK) {
		cap_data_fini(sdi);
		return res;
	}

	res = write_reg(sdi, REG_STOP, 0);
	if (res != SR_OK) {
		cap_sample_xfer_fini(sdi);
		cap_data_fini(sdi);
		return res;
	}

	res = loop_begin(sdi, &cap_top_event_handler);
	if (res != SR_OK) {
		write_reg(sdi, REG_SAMPLER_RESET, 0);
		cap_sample_xfer_fini(sdi);
		cap_data_fini(sdi);
		return res;
	}

	sr_info("%s: Transferring capture state to WAIT_TRIGGER.", __func__);
	devc->cap.state = CAP_STATE_WAIT_TRIGGER;

	return SR_OK;
}

SR_PRIV int px_logic_acquisition_stop(const struct sr_dev_inst *sdi)
{
	struct dev_context *const devc = sdi->priv;

	sr_info("%s: Transferring capture state to HALT.", __func__);
	cap_halt(devc);

	return SR_OK;
}
