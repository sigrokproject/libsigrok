/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 Miguel Angel Ajo Pelayo <miguelangel@ajo.es>
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
#include <stdlib.h>
#include <string.h>
#include "protocol.h"

/*
 * The LogicPort is an FT245BL USB FIFO bridge in front of an Altera
 * Cyclone FPGA. The FPGA is SRAM based and gets configured by the host
 * in FTDI bit-bang mode (passive serial). Once configured, the host
 * talks to it in FIFO mode with a small register protocol: every
 * command is a five byte header (opcode, 16-bit offset, 16-bit count
 * minus one), optionally followed by write data. The device answers
 * each command with a three byte ack (opcode, 16-bit sequence number)
 * followed by read data. A 32-bit register address is split into a
 * segment (upper 16 bits, selected by a separate command) and an
 * offset.
 */

#define CMD_WRITE		0xc1
#define CMD_READ		0xc2
#define CMD_SET_SEGMENT		0xc3
#define CMD_RESYNC		0x01
#define ACK_LEN			3

#define USB_TIMEOUT_MS		1000
#define WRITE_ACK_TIMEOUT_MS	1000
#define READ_TIMEOUT_MS		2000
#define PROBE_TIMEOUT_MS	250
#define LINK_TIMEOUT_MS		3000
#define LATENCY_TIMER_MS	4
/* Sets the bit-bang clock for FPGA configuration, unused in FIFO mode. */
#define BITBANG_BAUDRATE	460800

/* Control and status block. */
#define REG_STATUS		0x00100000	/* R */
#define REG_CONFIG_CTRL		0x00100000	/* W, only ever write 0. */
#define REG_ARM			0x00100002
#define REG_RUN			0x00100003
#define REG_PRE_DEPTH		0x00100004
#define REG_POST_DEPTH		0x00100006
#define REG_THRESHOLD		0x00100008
#define REG_END_PREFILL		0x0010000a	/* W */
#define REG_UNFILLED		0x0010000a	/* R */
#define REG_END_POSTFILL	0x0010000b
#define REG_TRIGGER_COMBINE	0x0010000d
#define REG_IMAGE_ID		0x0010000f

/* Trigger levels A and B share a layout. */
#define REG_TRIGGER_LEVEL_A	0x00200000
#define REG_TRIGGER_LEVEL_B	0x00400000
#define TRIG_PATTERN_MASK	0x00
#define TRIG_PATTERN_VALUE	0x05
#define TRIG_COUNT_HIGH		0x0a
#define TRIG_COUNT_LOW		0x0e
#define TRIG_CONDITION		0x14
#define TRIG_COUNTER_SOURCE	0x16
#define TRIG_COUNTER_MODE	0x17
#define TRIG_CONSECUTIVE	0x18
#define TRIG_PREQUALIFY		0x19
#define TRIG_RISING_MASK	0x29
#define TRIG_FALLING_MASK	0x2e

/* Sample clocking and channel configuration. */
#define REG_CLOCK_MODE		0x01000000	/* W */
#define REG_TRIGGER_POS		0x01000000	/* R */
#define REG_CHANNEL_CFG		0x01000001
#define REG_COMPRESSION		0x01000002
#define REG_COMPRESSION_MASK	0x01000003
#define REG_INVERT_MASK		0x01000008

/* Sample memory. */
#define REG_WRITE_PTR		0x02000000
#define REG_WRITE_PTR_B		0x02000100
#define MEM_LANES_A		0x02001000
#define MEM_LANES_B		0x02005000
#define MEM_LANE_STRIDE		0x1000
#define MEM_FLAGS		0x02009000

/* Timebase. */
#define REG_PRESCALER		0x20000000
#define REG_STEP		0x20000001

#define REG_INIT_COUNTER	0x40000005

/*
 * Status register. It cannot tell "idle" from "pre-filling",
 * and a previous capture's "done" survives into later sessions, so the
 * driver only looks at it after having started the acquisition itself.
 */
#define STATUS_DONE		(1 << 0)
#define STATUS_WRAPPED		(1 << 1)
#define STATUS_TRIGGERED	(1 << 5)
#define STATUS_PREFILLED	(1 << 6)

#define FLAG_CLK1		(1 << 1)
#define FLAG_CLK2		(1 << 2)
#define FLAG_COUNT		(1 << 3)
#define FLAG_DDR_B_SHIFT	4

#define CLOCK_MODE_CLK1		(1 << 2)
#define CLOCK_MODE_CLK2		(1 << 1)
#define CLOCK_MODE_FALLING	(1 << 0)
#define CLOCK_MODE_NO_QUAL	(1 << 4)

#define COND_PATTERN_TRUE	1
#define COUNTER_SRC_EDGE	3
#define COUNTER_MODE_OCCURRENCE	1

/*
 * The threshold DAC output settles with a time constant of about 30 ms
 * (measured), a full scale step needs about 165 ms to get within 50 mV.
 */
#define THRESHOLD_SETTLE_MS	200

/* Trigger pipeline delays, in samples. */
#define TRIGGER_DELAY_SDR	7
#define TRIGGER_DELAY_DDR	21

/* Bit-bang pins used for passive serial FPGA configuration. */
#define BB_DATA0		(1 << 0)
#define BB_DCLK			(1 << 1)
#define BB_NCONFIG		(1 << 2)
#define BB_NSTATUS		(1 << 3)
#define BB_CONF_DONE		(1 << 4)
#define BB_OUTPUTS		(BB_DATA0 | BB_DCLK | BB_NCONFIG)

#define BB_CHUNK_BYTES		10000
#define BB_EXTRA_CLOCKS		32
/*
 * LogicPort.ccf from the vendor software, used unmodified. A text line of
 * nine XOR obfuscated decimal numbers (eight image sizes in 16 byte units
 * and their XOR) is followed by the images, each a ready bit-bang stream
 * whose first two 4 KiB blocks are swapped.
 */
#define FW_FILE			"intronix-logicport.ccf"
#define CCF_KEY			0x916662
#define CCF_HEADER_MAX		128
#define CCF_SWAP_BLOCK		4096
#define RESYNC_RETRIES		5
#define CONFIG_RETRIES		3

/*
 * Flush the host side receive direction only. Before libftdi 1.5 the
 * purge functions were named after the opposite direction.
 */
#if defined HAVE_FTDI_TCIFLUSH && HAVE_FTDI_TCIFLUSH
#  define PURGE_FTDI_RX ftdi_tciflush
#else
#  define PURGE_FTDI_RX ftdi_usb_purge_tx_buffer
#endif

static const char *image_names[IMAGE_COUNT] = {
	[IMAGE_STATE_AFTER] = "state-after",
	[IMAGE_STATE_BEFORE_CLK1_RISE] = "state-before-clk1-rise",
	[IMAGE_STATE_BEFORE_CLK2_RISE] = "state-before-clk2-rise",
	[IMAGE_STATE_BEFORE_CLK1_FALL] = "state-before-clk1-fall",
	[IMAGE_STATE_BEFORE_CLK2_FALL] = "state-before-clk2-fall",
	[IMAGE_STATE_ON] = "state-on",
	[IMAGE_TIMING_DDR] = "timing-ddr",
	[IMAGE_TIMING_SDR] = "timing-sdr",
};

SR_PRIV const struct logicport_rate logicport_rates[] = {
	{ SR_KHZ(1),   FALSE, 0x61, 0x00 },
	{ SR_KHZ(2),   FALSE, 0x51, 0x44 },
	{ SR_KHZ(5),   FALSE, 0x51, 0x11 },
	{ SR_KHZ(10),  FALSE, 0x51, 0x00 },
	{ SR_KHZ(20),  FALSE, 0x41, 0x44 },
	{ SR_KHZ(50),  FALSE, 0x41, 0x11 },
	{ SR_KHZ(100), FALSE, 0x41, 0x00 },
	{ SR_KHZ(200), FALSE, 0x31, 0x44 },
	{ SR_KHZ(500), FALSE, 0x31, 0x11 },
	{ SR_MHZ(1),   FALSE, 0x31, 0x00 },
	{ SR_MHZ(2),   FALSE, 0x21, 0x44 },
	{ SR_MHZ(5),   FALSE, 0x21, 0x11 },
	{ SR_MHZ(10),  FALSE, 0x21, 0x00 },
	{ SR_MHZ(20),  FALSE, 0x11, 0x44 },
	{ SR_MHZ(50),  FALSE, 0x11, 0x11 },
	{ SR_MHZ(100), FALSE, 0x11, 0x00 },
	{ SR_MHZ(200), FALSE, 0x01, 0x00 },
	{ SR_MHZ(250), FALSE, 0x00, 0x00 },
	{ SR_MHZ(500), TRUE,  0x00, 0x00 },
};
SR_PRIV const size_t logicport_rates_count = ARRAY_SIZE(logicport_rates);

/*
 * Raw transfer errors are expected while the FPGA is unconfigured (the
 * FIFO isn't drained), so they are only logged at debug level here.
 */
static int read_raw(struct dev_context *devc, uint8_t *buf, size_t len,
	unsigned int timeout_ms)
{
	gint64 deadline;
	size_t got;
	int ret;

	deadline = g_get_monotonic_time() + (gint64)timeout_ms * 1000;
	got = 0;
	while (got < len) {
		ret = ftdi_read_data(devc->ftdic, buf + got, len - got);
		if (ret < 0) {
			sr_dbg("Failed to read FTDI data (%d): %s.",
				ret, ftdi_get_error_string(devc->ftdic));
			return SR_ERR_IO;
		}
		got += ret;
		if (ret == 0 && g_get_monotonic_time() > deadline) {
			sr_dbg("Read timeout (%zu of %zu bytes).", got, len);
			return SR_ERR_TIMEOUT;
		}
	}

	return SR_OK;
}

static int write_raw(struct dev_context *devc, const uint8_t *buf, size_t len)
{
	int ret;

	ret = ftdi_write_data(devc->ftdic, buf, len);
	if (ret < 0) {
		sr_dbg("Failed to write FTDI data (%d): %s.",
			ret, ftdi_get_error_string(devc->ftdic));
		return SR_ERR_IO;
	}
	if ((size_t)ret != len) {
		sr_dbg("Short FTDI write (%d of %zu bytes).", ret, len);
		return SR_ERR_IO;
	}

	return SR_OK;
}

/* Send one command and check its ack; read data follows the ack. */
static int send_cmd(struct dev_context *devc, uint8_t cmd, uint16_t arg0,
	uint16_t arg1, const uint8_t *wdata, size_t wlen,
	uint8_t *rdata, size_t rlen, unsigned int timeout_ms)
{
	uint8_t buf[5 + 8], ack[ACK_LEN];
	int ret;

	if (wlen > sizeof(buf) - 5)
		return SR_ERR_BUG;
	/* Fail fast until the next resync once the link is out of step. */
	if (devc->io_error)
		return SR_ERR_IO;

	buf[0] = cmd;
	write_u16le(&buf[1], arg0);
	write_u16le(&buf[3], arg1);
	if (wlen)
		memcpy(&buf[5], wdata, wlen);

	ret = write_raw(devc, buf, 5 + wlen);
	if (ret == SR_OK)
		ret = read_raw(devc, ack, sizeof(ack), timeout_ms);
	if (ret == SR_OK && (ack[0] != cmd || read_u16be(&ack[1]) != devc->seq)) {
		sr_dbg("Bad ack %02x %02x %02x for command 0x%02x, seq %u.",
			ack[0], ack[1], ack[2], cmd, devc->seq);
		ret = SR_ERR_IO;
	}
	if (ret == SR_OK && rlen)
		ret = read_raw(devc, rdata, rlen, timeout_ms);
	if (ret != SR_OK) {
		devc->segment = -1;
		devc->io_error = TRUE;
		return ret;
	}
	devc->seq++;

	return SR_OK;
}

static int set_segment(struct dev_context *devc, uint32_t addr)
{
	uint16_t segment;
	int ret;

	segment = addr >> 16;
	if (devc->segment == segment)
		return SR_OK;

	ret = send_cmd(devc, CMD_SET_SEGMENT, segment, 0, NULL, 0, NULL, 0,
		WRITE_ACK_TIMEOUT_MS);
	if (ret != SR_OK)
		return ret;
	devc->segment = segment;

	return SR_OK;
}

static int reg_write(struct dev_context *devc, uint32_t addr,
	const uint8_t *data, size_t len)
{
	int ret;

	ret = set_segment(devc, addr);
	if (ret != SR_OK)
		return ret;

	return send_cmd(devc, CMD_WRITE, addr & 0xffff, len - 1, data, len,
		NULL, 0, WRITE_ACK_TIMEOUT_MS);
}

static int reg_read_timeout(struct dev_context *devc, uint32_t addr,
	uint8_t *data, size_t len, unsigned int timeout_ms)
{
	int ret;

	ret = set_segment(devc, addr);
	if (ret != SR_OK)
		return ret;

	return send_cmd(devc, CMD_READ, addr & 0xffff, len - 1, NULL, 0,
		data, len, timeout_ms);
}

static int reg_read(struct dev_context *devc, uint32_t addr,
	uint8_t *data, size_t len)
{
	return reg_read_timeout(devc, addr, data, len, READ_TIMEOUT_MS);
}

static int reg_write_u8(struct dev_context *devc, uint32_t addr, uint8_t value)
{
	return reg_write(devc, addr, &value, 1);
}

static int reg_read_u8(struct dev_context *devc, uint32_t addr, uint8_t *value)
{
	return reg_read(devc, addr, value, 1);
}

static int reg_read_u16(struct dev_context *devc, uint32_t addr, uint16_t *value)
{
	uint8_t buf[2];
	int ret;

	ret = reg_read(devc, addr, buf, sizeof(buf));
	if (ret == SR_OK)
		*value = read_u16le(buf);

	return ret;
}

SR_PRIV int logicport_open(struct dev_context *devc)
{
	char *desc;
	int ret;

	desc = g_strdup_printf("d:%u/%u", devc->usb_bus, devc->usb_address);
	ret = ftdi_usb_open_string(devc->ftdic, desc);
	g_free(desc);
	if (ret < 0) {
		sr_err("Failed to open device (%d): %s.",
			ret, ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}

	ret = ftdi_usb_reset(devc->ftdic);
	if (ret < 0) {
		sr_err("Failed to reset FTDI chip (%d): %s.",
			ret, ftdi_get_error_string(devc->ftdic));
		goto err_close;
	}

	ret = ftdi_set_latency_timer(devc->ftdic, LATENCY_TIMER_MS);
	if (ret < 0) {
		sr_err("Failed to set FTDI latency timer (%d): %s.",
			ret, ftdi_get_error_string(devc->ftdic));
		goto err_close;
	}

	ret = ftdi_set_baudrate(devc->ftdic, BITBANG_BAUDRATE);
	if (ret < 0) {
		sr_err("Failed to set FTDI baudrate (%d): %s.",
			ret, ftdi_get_error_string(devc->ftdic));
		goto err_close;
	}

	devc->ftdic->usb_read_timeout = USB_TIMEOUT_MS;
	devc->ftdic->usb_write_timeout = USB_TIMEOUT_MS;

	return SR_OK;

err_close:
	ftdi_usb_close(devc->ftdic);

	return SR_ERR;
}

SR_PRIV void logicport_close(struct dev_context *devc)
{
	ftdi_usb_close(devc->ftdic);
}

/*
 * Resynchronize with the FPGA's command parser. The device keeps its
 * sequence counter across host sessions, so this is needed on every
 * open. The flood of zeros drains any partially received command of
 * maximum length, the final 0x01 resets the parser and the counter.
 */
static int resync_once(struct dev_context *devc, gboolean reopen)
{
	uint8_t *zeros, cmd;
	int ret;

	devc->segment = -1;

	if (reopen) {
		logicport_close(devc);
		g_usleep(50 * 1000);
		ret = logicport_open(devc);
		if (ret != SR_OK)
			return ret;
	}

	if (PURGE_FTDI_BOTH(devc->ftdic) < 0)
		return SR_ERR_IO;

	zeros = g_malloc0(65535);
	ret = write_raw(devc, zeros, 65535);
	if (ret == SR_OK)
		ret = write_raw(devc, zeros, 30000);
	g_free(zeros);
	if (ret != SR_OK)
		return ret;

	cmd = CMD_RESYNC;
	ret = write_raw(devc, &cmd, 1);
	if (ret != SR_OK)
		return ret;

	g_usleep(50 * 1000);
	if (PURGE_FTDI_BOTH(devc->ftdic) < 0)
		return SR_ERR_IO;
	devc->seq = 0;
	devc->io_error = FALSE;

	return SR_OK;
}

SR_PRIV int logicport_resync(struct dev_context *devc, gboolean reopen,
	int attempts)
{
	int i, ret;

	ret = SR_ERR;
	for (i = 0; i < attempts; i++) {
		ret = resync_once(devc, reopen || i > 0);
		if (ret == SR_OK)
			return SR_OK;
		sr_dbg("Resync attempt %d failed.", i + 1);
		if (i + 1 < attempts)
			g_usleep(500 * 1000);
	}
	sr_dbg("Failed to resynchronize with the device.");

	return ret;
}

/* Find out which FPGA image is loaded. An unconfigured FPGA won't reply. */
SR_PRIV int logicport_probe_image(struct dev_context *devc)
{
	uint8_t id;

	devc->fpga_image = -1;
	if (reg_read_timeout(devc, REG_IMAGE_ID, &id, 1, PROBE_TIMEOUT_MS) != SR_OK) {
		sr_dbg("No reply from the FPGA, assuming it is unconfigured.");
		return SR_ERR;
	}
	if ((id & 0xf0) != 0x10 || (id & 0x0f) >= IMAGE_COUNT) {
		sr_dbg("Unexpected FPGA image ID 0x%02x.", id);
		return SR_ERR;
	}
	devc->fpga_image = id & 0x0f;
	sr_dbg("FPGA image %d is loaded.", devc->fpga_image);

	return SR_OK;
}

/*
 * The threshold calibration offset is kept as a signed decimal string
 * (e.g. "+003") at the start of the EEPROM user area, which follows the
 * string descriptors and the four byte PnP block. The EEPROM is never
 * written.
 */
SR_PRIV int logicport_read_calibration(struct dev_context *devc)
{
	static const uint8_t pnp[] = { 0x02, 0x03, 0x01, 0x00 };
	uint8_t buf[128];
	char str[5];
	size_t pos;
	int ret;

	devc->threshold_cal = 0;
	memset(buf, 0, sizeof(buf));

	ret = ftdi_read_eeprom(devc->ftdic);
	if (ret == 0)
		ret = ftdi_get_eeprom_buf(devc->ftdic, buf, sizeof(buf));
	if (ret < 0) {
		sr_warn("Failed to read EEPROM (%d): %s.",
			ret, ftdi_get_error_string(devc->ftdic));
		return SR_ERR;
	}

	/* Serial number descriptor: byte offset (bit 7 set) and length. */
	pos = (buf[0x12] & 0x7f) + buf[0x13];
	if (pos + sizeof(pnp) <= sizeof(buf) &&
	    !memcmp(&buf[pos], pnp, sizeof(pnp)))
		pos += sizeof(pnp);
	if (pos + 4 > sizeof(buf) - 2 ||
	    (buf[pos] != '+' && buf[pos] != '-') ||
	    !g_ascii_isdigit(buf[pos + 1]) || !g_ascii_isdigit(buf[pos + 2]) ||
	    !g_ascii_isdigit(buf[pos + 3])) {
		sr_warn("No threshold calibration found in EEPROM.");
		return SR_ERR_DATA;
	}

	memcpy(str, &buf[pos], 4);
	str[4] = '\0';
	devc->threshold_cal = atoi(str);
	sr_dbg("Threshold calibration offset: %d.", devc->threshold_cal);

	return SR_OK;
}

static int wait_pins(struct dev_context *devc, uint8_t mask)
{
	gint64 deadline;
	uint8_t pins;

	deadline = g_get_monotonic_time() + 1000 * 1000;
	do {
		if (ftdi_read_pins(devc->ftdic, &pins) < 0)
			return SR_ERR_IO;
		if (pins & mask)
			return SR_OK;
	} while (g_get_monotonic_time() < deadline);
	sr_dbg("Timeout waiting for pins 0x%02x (pins 0x%02x).", mask, pins);

	return SR_ERR_TIMEOUT;
}

/* Send a bit-bang stream: two bytes per bit, DCLK low then high. */
static int send_bitstream(struct dev_context *devc, const uint8_t *stream,
	size_t size)
{
	size_t off, len;
	int ret;

	for (off = 0; off < size; off += len) {
		len = MIN(size - off, BB_CHUNK_BYTES);
		ret = write_raw(devc, stream + off, len);
		if (ret != SR_OK)
			return ret;
		/* The bit-bang echo is of no use, don't let it pile up. */
		if (PURGE_FTDI_RX(devc->ftdic) < 0)
			return SR_ERR_IO;
	}

	return SR_OK;
}

static int send_extra_clocks(struct dev_context *devc)
{
	uint8_t buf[2 * BB_EXTRA_CLOCKS];
	size_t i;

	for (i = 0; i < BB_EXTRA_CLOCKS; i++) {
		buf[2 * i] = BB_NCONFIG;
		buf[2 * i + 1] = BB_NCONFIG | BB_DCLK;
	}

	return write_raw(devc, buf, sizeof(buf));
}

static int bitbang_configure(struct dev_context *devc, const uint8_t *stream,
	size_t size)
{
	uint8_t pin_state;
	int ret;

	if (PURGE_FTDI_RX(devc->ftdic) < 0)
		return SR_ERR_IO;
	if (ftdi_set_bitmode(devc->ftdic, 0, BITMODE_RESET) < 0 ||
	    ftdi_set_bitmode(devc->ftdic, BB_OUTPUTS, BITMODE_BITBANG) < 0) {
		sr_err("Failed to enter bit-bang mode: %s.",
			ftdi_get_error_string(devc->ftdic));
		return SR_ERR_IO;
	}

	pin_state = 0;
	ret = write_raw(devc, &pin_state, 1);
	if (ret != SR_OK)
		return ret;
	g_usleep(10 * 1000);

	pin_state = BB_NCONFIG;
	ret = write_raw(devc, &pin_state, 1);
	if (ret != SR_OK)
		return ret;
	if (wait_pins(devc, BB_NSTATUS) != SR_OK) {
		sr_err("FPGA did not enter configuration mode.");
		return SR_ERR_TIMEOUT;
	}

	ret = send_bitstream(devc, stream, size);
	if (ret != SR_OK)
		return ret;

	if (wait_pins(devc, BB_CONF_DONE) != SR_OK) {
		sr_err("FPGA did not accept the configuration.");
		return SR_ERR_TIMEOUT;
	}

	/* Cyclone devices need extra DCLK edges to start initialization. */
	ret = send_extra_clocks(devc);
	if (ret != SR_OK)
		return ret;

	if (ftdi_set_bitmode(devc->ftdic, 0, BITMODE_RESET) < 0) {
		sr_err("Failed to leave bit-bang mode: %s.",
			ftdi_get_error_string(devc->ftdic));
		return SR_ERR_IO;
	}

	return SR_OK;
}

/*
 * After a power-on configuration the FPGA was seen to ignore commands
 * for more than a second, while a reconfiguration answers right away.
 * Poll with short timeouts, resyncing in between.
 */
static int wait_link(struct dev_context *devc)
{
	gint64 deadline;
	uint8_t count;

	deadline = g_get_monotonic_time() + LINK_TIMEOUT_MS * 1000;
	while (1) {
		if (reg_read_timeout(devc, REG_INIT_COUNTER, &count, 1,
		    PROBE_TIMEOUT_MS) == SR_OK)
			return SR_OK;
		if (g_get_monotonic_time() > deadline)
			return SR_ERR_TIMEOUT;
		sr_dbg("FPGA not answering yet, resyncing.");
		g_usleep(100 * 1000);
		logicport_resync(devc, TRUE, 1);
	}
}

/*
 * After configuration, the FPGA runs an init sequence of about 250 ms
 * during which a counter increments. Acquisitions don't start before it
 * has finished. Wait for the counter to settle, its final value varies.
 */
static int wait_init_done(struct dev_context *devc)
{
	gint64 start, now, stable_since;
	uint8_t count;
	int last, ret;

	start = g_get_monotonic_time();
	stable_since = start;
	last = -1;
	while (1) {
		ret = reg_read_u8(devc, REG_INIT_COUNTER, &count);
		if (ret != SR_OK) {
			sr_dbg("Failed to read the FPGA init counter (%d).", ret);
			return ret;
		}
		now = g_get_monotonic_time();
		if (count != last) {
			last = count;
			stable_since = now;
		} else if (count && now - stable_since > 50 * 1000) {
			break;
		}
		if (now - start > 3000 * 1000) {
			sr_err("FPGA init did not finish (counter %u).", count);
			return SR_ERR_TIMEOUT;
		}
		g_usleep(5 * 1000);
	}
	sr_dbg("FPGA init done after %" PRIi64 " ms.", (now - start) / 1000);

	return SR_OK;
}

static int read_full(struct sr_context *ctx, struct sr_resource *res,
	uint8_t *buf, size_t len)
{
	gssize n;

	while (len) {
		n = sr_resource_read(ctx, res, buf, len);
		if (n <= 0)
			return SR_ERR_IO;
		buf += n;
		len -= n;
	}

	return SR_OK;
}

static int parse_ccf_header(const char *line, uint32_t *units)
{
	gchar **fields;
	uint32_t check;
	int i, ret;

	fields = g_strsplit(line, ",", 0);
	ret = SR_OK;
	if (g_strv_length(fields) != IMAGE_COUNT + 1)
		ret = SR_ERR_DATA;
	check = 0;
	for (i = 0; ret == SR_OK && i <= IMAGE_COUNT; i++) {
		units[i] = strtoul(fields[i], NULL, 10) ^ CCF_KEY;
		if (i < IMAGE_COUNT)
			check ^= units[i];
	}
	if (ret == SR_OK && check != units[IMAGE_COUNT])
		ret = SR_ERR_DATA;
	g_strfreev(fields);

	return ret;
}

/* The stream must start with an Altera RBF header: 16 x 0xff, 0x6a. */
static gboolean stream_is_rbf(const uint8_t *stream, size_t size)
{
	static const uint8_t sync = 0x6a;
	uint8_t byte;
	size_t i, k;

	if (size < 17 * 16)
		return FALSE;
	for (i = 0; i < 17; i++) {
		byte = 0;
		for (k = 0; k < 8; k++)
			byte |= (stream[(i * 8 + k) * 2] & BB_DATA0) << k;
		if (byte != (i < 16 ? 0xff : sync))
			return FALSE;
	}

	return TRUE;
}

/* Read one image from LogicPort.ccf, ready to bit-bang. */
static uint8_t *load_image(struct sr_context *ctx, int image, size_t *size)
{
	struct sr_resource res;
	char line[CCF_HEADER_MAX];
	uint32_t units[IMAGE_COUNT + 1];
	uint8_t *stream, skip[4096], tmp;
	uint64_t offset, total;
	size_t len, i;
	int k;

	if (sr_resource_open(ctx, &res, SR_RESOURCE_FIRMWARE, FW_FILE) != SR_OK) {
		sr_err("Firmware file '%s' not found, see README.devices.",
			FW_FILE);
		return NULL;
	}

	/* Header line, terminated by CR LF. */
	len = 0;
	while (len < sizeof(line) - 1) {
		if (read_full(ctx, &res, (uint8_t *)&line[len], 1) != SR_OK)
			break;
		if (line[len++] == '\n')
			break;
	}
	line[len] = '\0';
	if (len < 2 || line[len - 1] != '\n' ||
	    parse_ccf_header(line, units) != SR_OK) {
		sr_err("'%s' is not a LogicPort.ccf file.", FW_FILE);
		goto err_close;
	}

	offset = 0;
	total = 0;
	for (k = 0; k < IMAGE_COUNT; k++) {
		if (k < image)
			offset += (uint64_t)units[k] * 16;
		total += (uint64_t)units[k] * 16;
	}
	*size = (size_t)units[image] * 16;
	if (len + total != res.size || *size < 2 * CCF_SWAP_BLOCK) {
		sr_err("'%s' has an unexpected size.", FW_FILE);
		goto err_close;
	}

	for (; offset; offset -= len) {
		len = MIN(offset, sizeof(skip));
		if (read_full(ctx, &res, skip, len) != SR_OK)
			goto err_read;
	}
	stream = g_try_malloc(*size);
	if (!stream) {
		sr_err("Failed to allocate memory for the bitstream.");
		goto err_close;
	}
	if (read_full(ctx, &res, stream, *size) != SR_OK) {
		g_free(stream);
		goto err_read;
	}
	sr_resource_close(ctx, &res);

	for (i = 0; i < CCF_SWAP_BLOCK; i++) {
		tmp = stream[i];
		stream[i] = stream[CCF_SWAP_BLOCK + i];
		stream[CCF_SWAP_BLOCK + i] = tmp;
	}
	if (!stream_is_rbf(stream, *size)) {
		sr_err("Image %d in '%s' is not a valid bitstream.", image,
			FW_FILE);
		g_free(stream);
		return NULL;
	}

	return stream;

err_read:
	sr_err("Failed to read '%s'.", FW_FILE);
err_close:
	sr_resource_close(ctx, &res);
	return NULL;
}

SR_PRIV int logicport_configure_fpga(struct sr_context *ctx,
	struct dev_context *devc, int image)
{
	const char *step;
	uint8_t *fw;
	size_t size;
	int i, ret;

	if (devc->fpga_image == image)
		return SR_OK;

	fw = load_image(ctx, image, &size);
	if (!fw)
		return SR_ERR;

	sr_info("Configuring FPGA with image '%s'.", image_names[image]);
	ret = SR_ERR;
	step = "none";
	devc->threshold_dac = -1;
	for (i = 0; i < CONFIG_RETRIES; i++) {
		if (i > 0) {
			sr_dbg("Configuration attempt %d failed (%s).", i, step);
			g_usleep(500 * 1000);
			/* The image may have loaded, only the link failed. */
			if (logicport_resync(devc, TRUE, 1) == SR_OK &&
			    logicport_probe_image(devc) == SR_OK &&
			    devc->fpga_image == image &&
			    wait_init_done(devc) == SR_OK) {
				ret = SR_OK;
				break;
			}
		}

		/*
		 * A configured FPGA ignores nCONFIG until it is told to
		 * release its configuration. This stalls the FIFO, so it
		 * has to be followed by bit-bang configuration right away.
		 * An unconfigured FPGA won't ack, which is fine.
		 */
		if (devc->fpga_image >= 0 &&
		    reg_write_u8(devc, REG_CONFIG_CTRL, 0x00) != SR_OK)
			sr_dbg("No ack for 'enable configure'.");
		devc->fpga_image = -1;

		step = "bit-bang";
		ret = bitbang_configure(devc, fw, size);
		if (ret != SR_OK)
			continue;
		/* A close/reopen is needed before the resync here. */
		step = "resync";
		ret = logicport_resync(devc, TRUE, RESYNC_RETRIES);
		if (ret != SR_OK)
			continue;
		step = "link";
		ret = wait_link(devc);
		if (ret != SR_OK)
			continue;
		step = "init";
		ret = wait_init_done(devc);
		if (ret != SR_OK)
			continue;
		step = "image check";
		ret = logicport_probe_image(devc);
		if (ret == SR_OK && devc->fpga_image != image)
			ret = SR_ERR;
		if (ret == SR_OK)
			break;
	}
	g_free(fw);

	if (ret != SR_OK)
		sr_err("Failed to configure the FPGA.");

	return ret;
}

SR_PRIV int logicport_select_image(const struct dev_context *devc)
{
	/* State mode samples on the clock edge, without setup/hold shift. */
	if (devc->external_clock)
		return IMAGE_STATE_ON;
	if (devc->rate->ddr)
		return IMAGE_TIMING_DDR;

	return IMAGE_TIMING_SDR;
}

/*
 * Map sigrok trigger stages onto the two hardware trigger levels. Level
 * matches form a pattern, edge matches an edge mask. With both present
 * in a stage the level fires on an edge while the pattern is true.
 */
SR_PRIV int logicport_convert_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	const struct sr_trigger_stage *stage;
	const struct sr_trigger_match *match;
	struct logicport_trigger_level *level;
	const GSList *l, *m;
	uint64_t bit;
	int num_stages;

	devc = sdi->priv;
	memset(devc->trigger, 0, sizeof(devc->trigger));
	devc->trigger_combine = 0;
	devc->trigger_used = FALSE;

	trigger = sr_session_trigger_get(sdi->session);
	if (!trigger || !trigger->stages)
		return SR_OK;

	num_stages = g_slist_length(trigger->stages);
	if (num_stages > (int)ARRAY_SIZE(devc->trigger)) {
		sr_err("Only %zu trigger stages are supported.",
			ARRAY_SIZE(devc->trigger));
		return SR_ERR_ARG;
	}

	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		level = &devc->trigger[stage->stage];
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			if (!match->channel->enabled)
				continue;
			bit = UINT64_C(1) << match->channel->index;
			switch (match->match) {
			case SR_TRIGGER_ZERO:
				level->pattern_mask |= bit;
				break;
			case SR_TRIGGER_ONE:
				level->pattern_mask |= bit;
				level->pattern_value |= bit;
				break;
			case SR_TRIGGER_RISING:
				level->rising_mask |= bit;
				break;
			case SR_TRIGGER_FALLING:
				level->falling_mask |= bit;
				break;
			case SR_TRIGGER_EDGE:
				level->rising_mask |= bit;
				level->falling_mask |= bit;
				break;
			default:
				sr_err("Unsupported trigger match %d.",
					match->match);
				return SR_ERR_ARG;
			}
		}
		if (level->pattern_mask)
			level->condition = COND_PATTERN_TRUE;
		if (level->rising_mask || level->falling_mask)
			level->counter_source = COUNTER_SRC_EDGE;
		if (level->condition || level->counter_source)
			level->counter_mode = COUNTER_MODE_OCCURRENCE;
	}

	/* Fire on level A, or on level A followed by level B. */
	devc->trigger_combine = (num_stages == 1) ? 1 : 5;
	devc->trigger_used = TRUE;

	return SR_OK;
}

/* Settings are collected in a list, then written in order. */
struct regval {
	uint32_t addr;
	size_t width;
	uint64_t value;
};

struct reg_queue {
	struct regval regs[48];
	size_t count;
};

static void queue_reg(struct reg_queue *q, uint32_t addr, size_t width,
	uint64_t value)
{
	if (q->count < ARRAY_SIZE(q->regs)) {
		q->regs[q->count].addr = addr;
		q->regs[q->count].width = width;
		q->regs[q->count].value = value;
	}
	q->count++;
}

static int write_regs(struct dev_context *devc, const struct reg_queue *q)
{
	uint8_t buf[8];
	size_t i;
	int ret;

	if (q->count > ARRAY_SIZE(q->regs))
		return SR_ERR_BUG;

	for (i = 0; i < q->count; i++) {
		write_u64le(buf, q->regs[i].value);
		ret = reg_write(devc, q->regs[i].addr, buf, q->regs[i].width);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

static void queue_trigger_level(struct reg_queue *q, uint32_t base,
	const struct logicport_trigger_level *level)
{
	queue_reg(q, base + TRIG_PATTERN_MASK, 5, level->pattern_mask);
	queue_reg(q, base + TRIG_PATTERN_VALUE, 5, level->pattern_value);
	queue_reg(q, base + TRIG_RISING_MASK, 5, level->rising_mask);
	queue_reg(q, base + TRIG_FALLING_MASK, 5, level->falling_mask);
	queue_reg(q, base + TRIG_CONSECUTIVE, 1, 0);
	queue_reg(q, base + TRIG_PREQUALIFY, 1, 0);
	queue_reg(q, base + TRIG_COUNT_LOW, 4, 0);
	/* Occurrence count minus one: fire on the first event. */
	queue_reg(q, base + TRIG_COUNT_HIGH, 4, 0);
	queue_reg(q, base + TRIG_CONDITION, 1, level->condition);
	queue_reg(q, base + TRIG_COUNTER_SOURCE, 1, level->counter_source);
	queue_reg(q, base + TRIG_COUNTER_MODE, 1, level->counter_mode);
}

static uint16_t threshold_dac(double volts, int cal)
{
	double v;
	long dac;

	/* Input divider and DAC transfer function. */
	v = (volts - 1.315) * -0.4020381098624974 + 1.315;
	dac = lround((4.559 - v) / 5.875 * 1023) + cal;

	return CLAMP(dac, 0, 1023);
}

/* Halve, rounding to even on ties. */
static int half_even(int x)
{
	if (!(x & 1))
		return x / 2;

	return ((x + 1) / 2) % 2 ? (x - 1) / 2 : (x + 1) / 2;
}

static void buffer_split(const struct dev_context *devc, uint16_t *pre,
	uint16_t *post)
{
	int ratio, pre_depth, post_depth;

	ratio = devc->capture_ratio;
	pre_depth = ratio * LOGICPORT_MEM_DEPTH / 100 + 1;
	post_depth = (100 - ratio) * LOGICPORT_MEM_DEPTH / 100 - 1;
	if (!devc->external_clock && devc->rate->ddr) {
		pre_depth = half_even(pre_depth + 0x23);
		post_depth = half_even(MAX(post_depth - 0x1e, 0));
	} else {
		pre_depth += 7;
		post_depth = MAX(post_depth - 7, 0);
	}
	*pre = pre_depth;
	*post = post_depth;
}

/* Write all acquisition settings, in the order the vendor software uses. */
SR_PRIV int logicport_program(struct dev_context *devc)
{
	struct reg_queue q;
	uint16_t pre, post, dac;
	uint8_t mode;
	int ret;

	q.count = 0;

	if (!devc->external_clock) {
		queue_reg(&q, REG_PRESCALER, 1, devc->rate->prescaler);
		queue_reg(&q, REG_STEP, 1, devc->rate->step);
		mode = CLOCK_MODE_CLK1 | CLOCK_MODE_NO_QUAL;
	} else {
		mode = CLOCK_MODE_NO_QUAL;
		mode |= devc->clock_source ? CLOCK_MODE_CLK2 : CLOCK_MODE_CLK1;
		if (devc->clock_edge)
			mode |= CLOCK_MODE_FALLING;
	}

	queue_reg(&q, REG_CLOCK_MODE, 1, mode);
	queue_reg(&q, REG_COMPRESSION, 1, 0);
	/* All channels are always captured, this only steers compression. */
	queue_reg(&q, REG_COMPRESSION_MASK, 5, LOGICPORT_ALL_CHANNELS);
	queue_reg(&q, REG_INVERT_MASK, 5, 0);
	queue_reg(&q, REG_COMPRESSION, 1, devc->rle ? 1 : 0);
	queue_reg(&q, REG_CHANNEL_CFG, 1, 0);

	queue_reg(&q, REG_RUN, 1, 0);
	queue_reg(&q, REG_ARM, 1, 1);
	queue_trigger_level(&q, REG_TRIGGER_LEVEL_A, &devc->trigger[0]);
	queue_trigger_level(&q, REG_TRIGGER_LEVEL_B, &devc->trigger[1]);
	queue_reg(&q, REG_TRIGGER_COMBINE, 1, devc->trigger_combine);

	dac = threshold_dac(devc->threshold, devc->threshold_cal);
	queue_reg(&q, REG_THRESHOLD, 2, dac);

	buffer_split(devc, &pre, &post);
	queue_reg(&q, REG_PRE_DEPTH, 2, pre);
	queue_reg(&q, REG_POST_DEPTH, 2, post);

	ret = write_regs(devc, &q);
	if (ret != SR_OK)
		return ret;

	if (dac != devc->threshold_dac) {
		sr_dbg("Waiting for the threshold DAC (%u) to settle.", dac);
		g_usleep(THRESHOLD_SETTLE_MS * 1000);
		devc->threshold_dac = dac;
	}

	return SR_OK;
}

/*
 * With compression, memory fills with transitions rather than samples,
 * so an idle input never completes a capture. Bound the pre-fill and
 * post-fill phases in time instead, derived from the sample limit.
 */
static void setup_fill_limits(struct dev_context *devc)
{
	uint64_t pre, post;

	devc->prefill_us = 0;
	devc->postfill_us = 0;
	if (!devc->rle || devc->external_clock || !devc->limit_samples)
		return;

	pre = devc->limit_samples * devc->capture_ratio / 100;
	post = devc->limit_samples - pre;
	devc->prefill_us = MAX(pre * 1000000 / devc->rate->samplerate, 1);
	devc->postfill_us = MAX(post * 1000000 / devc->rate->samplerate, 1);
}

SR_PRIV int logicport_start(struct dev_context *devc)
{
	int ret;

	setup_fill_limits(devc);
	devc->trigger_seen = 0;
	devc->prefill_ended = FALSE;
	devc->postfill_ended = FALSE;

	ret = reg_write_u8(devc, REG_RUN, 1);
	if (ret == SR_OK)
		ret = reg_write_u8(devc, REG_ARM, 1);
	devc->acq_start = g_get_monotonic_time();

	return ret;
}

static int check_fill_limits(struct dev_context *devc, uint8_t status)
{
	gint64 now;
	int ret;

	if (!devc->prefill_us)
		return SR_OK;

	now = g_get_monotonic_time();
	ret = SR_OK;
	if (!(status & STATUS_PREFILLED) && !devc->prefill_ended &&
	    now - devc->acq_start >= devc->prefill_us) {
		sr_dbg("Pre-fill time elapsed.");
		ret = reg_write_u8(devc, REG_END_PREFILL, 1);
		devc->prefill_ended = TRUE;
	}
	if ((status & STATUS_TRIGGERED) && !devc->trigger_seen)
		devc->trigger_seen = now;
	if (devc->trigger_seen && !devc->postfill_ended &&
	    now - devc->trigger_seen >= devc->postfill_us) {
		sr_dbg("Post-fill time elapsed.");
		ret = reg_write_u8(devc, REG_END_POSTFILL, 1);
		devc->postfill_ended = TRUE;
	}

	return ret;
}

SR_PRIV int logicport_halt(struct dev_context *devc)
{
	return reg_write_u8(devc, REG_RUN, 0);
}

/* Feeds expanded samples to the session, applies limit and trigger. */
struct sample_sink {
	struct feed_queue_logic *queue;
	uint64_t skip;
	uint64_t sent;
	uint64_t limit;
	uint64_t ratio;
	int64_t trigger;
};

/*
 * With a sample limit below what was captured, keep the window around
 * the trigger such that the capture ratio is honoured.
 */
static void sink_set_trigger(struct sample_sink *sink, int64_t trigger,
	uint64_t available)
{
	uint64_t pre, skip;

	sink->trigger = trigger;
	if (trigger < 0 || !sink->limit || available <= sink->limit)
		return;

	pre = sink->limit * sink->ratio / 100;
	skip = (uint64_t)trigger > pre ? trigger - pre : 0;
	sink->skip = MIN(skip, available - sink->limit);
	sink->trigger -= sink->skip;
}

static void sink_put(struct sample_sink *sink, uint64_t value, uint64_t count)
{
	uint8_t sample[LOGICPORT_UNITSIZE];
	uint64_t chunk;

	chunk = MIN(count, sink->skip);
	sink->skip -= chunk;
	count -= chunk;

	write_u40le(sample, value);
	while (count && (!sink->limit || sink->sent < sink->limit)) {
		if (sink->trigger >= 0 && sink->sent == (uint64_t)sink->trigger) {
			feed_queue_logic_send_trigger(sink->queue);
			sink->trigger = -1;
		}
		chunk = count;
		if (sink->trigger >= 0)
			chunk = MIN(chunk, (uint64_t)sink->trigger - sink->sent);
		if (sink->limit)
			chunk = MIN(chunk, sink->limit - sink->sent);
		feed_queue_logic_submit_one(sink->queue, sample, chunk);
		sink->sent += chunk;
		count -= chunk;
	}
}

/*
 * Sample memory has 2048 entries of 40 bits: four lanes holding D0-D31
 * and a flags lane holding CLK1/CLK2. With compression, an entry with
 * FLAG_COUNT set is a count record instead: the lanes and the low flag
 * bits form a 35-bit number of sample periods without any transition.
 */
static uint64_t data_record(const struct dev_context *devc, size_t row,
	int clk_shift)
{
	uint64_t value;
	uint8_t flags;

	value = (uint64_t)devc->raw[0][row] |
		(uint64_t)devc->raw[1][row] << 8 |
		(uint64_t)devc->raw[2][row] << 16 |
		(uint64_t)devc->raw[3][row] << 24;
	flags = devc->raw[4][row] >> clk_shift;
	if (flags & FLAG_CLK1)
		value |= UINT64_C(1) << LOGICPORT_CH_CLK1;
	if (flags & FLAG_CLK2)
		value |= UINT64_C(1) << LOGICPORT_CH_CLK2;

	return value;
}

/* Count records hold how many extra sample periods had no transitions. */
static uint64_t count_record(const struct dev_context *devc, size_t row)
{
	return (uint64_t)(devc->raw[4][row] & 0x07) << 32 |
		(uint64_t)devc->raw[3][row] << 24 |
		(uint64_t)devc->raw[2][row] << 16 |
		(uint64_t)devc->raw[1][row] << 8 |
		(uint64_t)devc->raw[0][row];
}

static uint64_t total_samples(const struct dev_context *devc, size_t n)
{
	uint64_t total;
	size_t i;

	total = n;
	for (i = 1; i < n; i++)
		if (devc->raw[4][i] & FLAG_COUNT)
			total += count_record(devc, i);

	return total;
}

static int read_lanes(struct dev_context *devc, uint32_t lanes,
	uint16_t start, size_t offset, size_t count)
{
	int k, ret;

	for (k = 0; k < 4; k++) {
		ret = reg_read(devc, lanes + k * MEM_LANE_STRIDE + start,
			&devc->raw[k][offset], count);
		if (ret != SR_OK)
			return ret;
	}

	return reg_read(devc, MEM_FLAGS + start, &devc->raw[4][offset], count);
}

/* Sample memory is a ring; the trigger position counts entries after it. */
static int read_sdr(struct dev_context *devc, uint8_t status,
	struct sample_sink *sink)
{
	uint16_t wp, unfilled, trigger_pos, start;
	uint64_t value, prev, ts, prev_ts, trigger_ts;
	size_t n, x, i, trigger_idx;
	uint8_t flags;
	int ret;

	ret = reg_read_u16(devc, REG_WRITE_PTR, &wp);
	if (ret != SR_OK)
		return ret;
	wp = MIN(wp, LOGICPORT_MEM_DEPTH - 1);

	x = 0;
	if (!(status & STATUS_WRAPPED)) {
		ret = reg_read_u16(devc, REG_UNFILLED, &unfilled);
		if (ret != SR_OK)
			return ret;
		x = MIN((size_t)unfilled + 1, LOGICPORT_MEM_DEPTH - 1);
	}
	n = LOGICPORT_MEM_DEPTH - x;
	start = (wp + 1 + x) % LOGICPORT_MEM_DEPTH;

	/* Drop a leading count record, it has no data to repeat. */
	ret = reg_read_u8(devc, MEM_FLAGS + start, &flags);
	if (ret != SR_OK)
		return ret;
	if ((flags & FLAG_COUNT) && n > 1) {
		start = (start + 1) % LOGICPORT_MEM_DEPTH;
		n--;
	}

	ret = read_lanes(devc, MEM_LANES_A, start, 0, n);
	if (ret != SR_OK)
		return ret;
	ret = reg_read_u16(devc, REG_TRIGGER_POS, &trigger_pos);
	if (ret != SR_OK)
		return ret;
	trigger_pos = MIN(trigger_pos, LOGICPORT_MEM_DEPTH - 1);
	sr_dbg("SDR readout: wp %u, skip %zu, start %u, n %zu, trigger %u.",
		wp, x, start, n, trigger_pos);

	/*
	 * When the trigger fires, the FPGA writes a record (with compression
	 * the only data record without a transition) TRIGGER_DELAY_SDR samples
	 * after the match. The trigger position points just past that record.
	 */
	trigger_idx = (n + 1 - trigger_pos) % (n + 1);
	trigger_idx = trigger_idx ? MIN(trigger_idx, n) - 1 : 0;
	ts = 0;
	for (i = 1; i <= trigger_idx; i++) {
		ts++;
		if (devc->raw[4][i] & FLAG_COUNT)
			ts += count_record(devc, i);
	}
	trigger_ts = ts > TRIGGER_DELAY_SDR ? ts - TRIGGER_DELAY_SDR : 0;
	sink_set_trigger(sink, devc->trigger_used ? (int64_t)trigger_ts : -1,
		total_samples(devc, n));

	prev = 0;
	prev_ts = 0;
	for (i = 0; i < n; i++) {
		if (devc->raw[4][i] & FLAG_COUNT) {
			/* Repeat the last value, including this entry's slot. */
			ts = prev_ts + 1 + count_record(devc, i);
			sink_put(sink, prev, ts - prev_ts);
		} else {
			value = data_record(devc, i, 0);
			ts = i ? prev_ts + 1 : 0;
			sink_put(sink, value, 1);
			prev = value;
		}
		prev_ts = ts;
	}

	return SR_OK;
}

/* 500 MHz: two banks of 1024 rows hold the even and odd samples. */
static int read_ddr(struct dev_context *devc, uint8_t status,
	struct sample_sink *sink)
{
	const size_t bank = LOGICPORT_MEM_DEPTH / 2;
	uint16_t wp_a, wp_b, unfilled, trigger_pos;
	size_t n, x, rows, i;
	int64_t trigger_idx;
	int ret;

	ret = reg_read_u16(devc, REG_WRITE_PTR, &wp_a);
	if (ret == SR_OK)
		ret = reg_read_u16(devc, REG_WRITE_PTR_B, &wp_b);
	if (ret != SR_OK)
		return ret;
	wp_a %= bank;
	wp_b %= bank;

	x = 0;
	if (!(status & STATUS_WRAPPED)) {
		ret = reg_read_u16(devc, REG_UNFILLED, &unfilled);
		if (ret != SR_OK)
			return ret;
		if (unfilled + 1 > bank)
			x = 2 * (unfilled + 1 - bank);
	}
	n = x < LOGICPORT_MEM_DEPTH - 2 ? LOGICPORT_MEM_DEPTH - 1 - x : 1;
	rows = (n + 1) / 2;

	ret = read_lanes(devc, MEM_LANES_A, (wp_a + 1 + x) % bank, 0, rows);
	if (ret == SR_OK)
		ret = read_lanes(devc, MEM_LANES_B, (wp_b + 1 + x) % bank,
			bank, rows);
	if (ret == SR_OK)
		ret = reg_read_u16(devc, REG_TRIGGER_POS, &trigger_pos);
	if (ret != SR_OK)
		return ret;
	sr_dbg("DDR readout: wp %u/%u, skip %zu, n %zu, trigger %u.",
		wp_a, wp_b, x, n, trigger_pos);

	/* Empirical pipeline delay, accurate to about two samples. */
	trigger_idx = ((int64_t)n + 1 - trigger_pos) % ((int64_t)n + 1);
	if (trigger_idx < 0)
		trigger_idx += n + 1;
	trigger_idx = CLAMP(trigger_idx - TRIGGER_DELAY_DDR, 0, (int64_t)n - 1);
	sink_set_trigger(sink, devc->trigger_used ? trigger_idx : -1, n);

	for (i = 0; i < n; i++) {
		if (i & 1)
			sink_put(sink, data_record(devc, bank + i / 2,
				FLAG_DDR_B_SHIFT), 1);
		else
			sink_put(sink, data_record(devc, i / 2, 0), 1);
	}

	return SR_OK;
}

static int read_samples(const struct sr_dev_inst *sdi, uint8_t status)
{
	struct dev_context *devc;
	struct sample_sink sink;
	int ret;

	devc = sdi->priv;

	memset(&sink, 0, sizeof(sink));
	sink.queue = feed_queue_logic_alloc(sdi, 4096, LOGICPORT_UNITSIZE);
	if (!sink.queue)
		return SR_ERR_MALLOC;
	sink.limit = devc->limit_samples;
	sink.ratio = devc->capture_ratio;
	sink.trigger = -1;
	if (devc->rle && !sink.limit)
		sink.limit = LOGICPORT_RLE_DEFAULT_LIMIT;

	if (!devc->external_clock && devc->rate->ddr)
		ret = read_ddr(devc, status, &sink);
	else
		ret = read_sdr(devc, status, &sink);
	feed_queue_logic_flush(sink.queue);
	feed_queue_logic_free(sink.queue);
	sr_dbg("Sent %" PRIu64 " samples.", sink.sent);

	return ret;
}

/*
 * Poll the status register until the capture is done, then read out the
 * sample memory. Captures can't be streamed, the readout happens only
 * once the whole memory (or the time limited RLE window) is filled.
 */
SR_PRIV int logicport_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	uint8_t status;

	(void)fd;
	(void)revents;

	sdi = cb_data;
	if (!sdi)
		return TRUE;

	devc = sdi->priv;
	if (!devc || !devc->acq_running)
		return TRUE;

	if (reg_read_u8(devc, REG_STATUS, &status) != SR_OK) {
		sr_err("Failed to read acquisition status.");
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}
	if (!(status & STATUS_DONE)) {
		if (check_fill_limits(devc, status) != SR_OK) {
			sr_err("Failed to end a fill phase.");
			sr_dev_acquisition_stop(sdi);
		}
		return TRUE;
	}

	sr_dbg("Acquisition done, status 0x%02x.", status);
	devc->acq_running = FALSE;

	if (read_samples(sdi, status) != SR_OK)
		sr_err("Failed to read sample memory.");
	sr_dev_acquisition_stop(sdi);

	return TRUE;
}
