/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023 Gerhard Sittig <gerhard.sittig@gmx.net>
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
 * Communicate to the Devantech ETH008 relay card via TCP and Ethernet.
 * Also supports other cards when their protocol is similar enough.
 * USB and Modbus attached cards are not covered by this driver.
 *
 * See http://www.robot-electronics.co.uk/files/eth008b.pdf for device
 * capabilities and a protocol discussion. See other devices' documents
 * for additional features (digital input, analog input, TCP requests
 * which ETH008 does not implement).
 * See https://github.com/devantech/devantech_eth_python for MIT licensed
 * Python source code which is maintained by the vendor.
 * This sigrok driver implementation was created based on information in
 * version 0.1.2 of the Python code (corresponds to commit 0c0080b88e29),
 * and PDF files that are referenced in the shop's product pages (which
 * also happen to provide ZIP archives with examples that are written
 * using other programming languages).
 *
 * The device provides several means of communication: HTTP requests
 * (as well as an interactive web form). Raw TCP communication with
 * binary requests and responses. Text requests and responses over
 * TCP sockets. Some of these depend on the firmware version. Version
 * checks before command transmission is essentially non-existent in
 * this sigrok driver implementation. Binary transmission is preferred
 * because it is assumed that this existed in all firmware versions.
 * The firmware interestingly accepts concurrent network connections
 * (up to five of them, all share the same password). Which means that
 * the peripheral's state can change even while we are controlling it.
 *
 * TCP communication seems to rely on network fragmentation and assumes
 * that software stacks provide all of a request in a single receive
 * call on the firmware side. Which works for local communication, but
 * could become an issue when long distances and tunnels are involved.
 * This sigrok driver also assumes complete reception within a single
 * receive call. The short length of binary transmission helps here
 * (the largest payloads has a length of four bytes).
 *
 * The lack of length specs as well as termination in the protocol
 * (both binary as well as text variants over TCP sockets) results in
 * the inability to synchronize to the firmware when connecting and
 * after hiccups in an established connection. The fixed length of
 * requests and responses for binary payloads helps a little bit,
 * assuming that TCP connect is used to recover. The overhead of
 * HTTP requests and responses is considered undesirable for this
 * sigrok driver implementation. [This also means that a transport
 * which lacks the concept of network frames cannot send passwords.]
 * The binary transport appears to lack HELLO or NOP requests that
 * could be used to synchronize. Firmware just would not respond to
 * unsupported commands. Maybe a repeated sequence of identity reads
 * combined with a read timeout could help synchronize, but only if
 * the response is known because the model was identified before.
 *
 * The sigrok driver source code was phrased with the addition of more
 * models in mind. Only few code paths require adjustment when similar
 * variants of requests or responses are involved in the communication
 * to relay cards that support between two and twenty channels. Chances
 * are good, existing firmware is compatible across firmware versions,
 * and even across hardware revisions (model upgrades). Firmware just
 * happens to not respond to unknown requests.
 *
 * Support for models with differing features also was kept somehwat
 * simple and straight forward. The mapping of digital outputs to relay
 * numbers from the user's perspective is incomplete for those cards
 * where users decide whether relays are attached to digital outputs.
 * When an individual physical channel can be operated in different
 * modes, or when its value gets presented in different formats, then
 * these values are not synchronized. This applies for digital inputs
 * which are the result of applying a threshold to an analog value.
 *
 * TODO
 * - Add support for other models.
 *   - The Ethernet (and Wifi) devices should work as they are with
 *     the current implementation.
 *     https://www.robot-electronics.co.uk/files/eth484b.pdf.
 *   - USB could get added here with reasonable effort. Serial over
 *     CDC is transparently supported (lack of framing prevents the
 *     use of variable length requests or responses, but should not
 *     apply to these models anyway). The protocol radically differs
 *     from Ethernet variants:
 *     https://www.robot-electronics.co.uk/files/usb-rly16b.pdf
 *     - 0x38 get serial number, yields 8 bytes
 *     - 0x5a get software version, yields module ID 9, 1 byte version
 *     - 0x5b get relay states, yields 1 byte current state
 *     - 0x5c set relay state, takes 1 byte for all 8 relays
 *     - 0x5d get supply voltage, yields 1 byte in 0.1V steps
 *     - 0x5e set individual relay, takes 3 more bytes: relay number,
 *       hi/lo pulse time in 10ms steps
 *     - for interactive use? 'd' all relays on, 'e'..'l' relay 1..8 on,
 *       'n' all relays off, 'o'..'v' relay 1..8 off
 *   - Modbus may or may not be a good match for this driver, or may
 *     better be kept in yet another driver? Requests and responses
 *     again differ from Ethernet and USB models, refer to traditional
 *     "coils" and have their individual and grouped access.
 *     https://www.robot-electronics.co.uk/files/mbh88.pdf
 * - Reconsider the relation of relay channels, and digital outputs
 *   and their analog sampling and digital input interpretation. The
 *   current implementation is naive, assumes the simple DO/DI/AI
 *   groups and ignores their interaction within the firmware.
 * - Add support for password protection?
 *   - See command 0x79 to "login" (beware of the differing return value
 *     compared to other commands), command 0x7a to check if passwords
 *     are involved and whether the login needs refreshing, command 0x7b
 *     for immediate "logout" in contrast to expiration.
 *   - Alternatively consider switching to the "text protocol" in that
 *     use case, which can send an optional password in every request
 *     that controls relays (command 0x3a).
 *   - How to specify the password in applications and how to pass them
 *     to this driver is yet another issue that needs consideration.
 */

#include "config.h"

#include <string.h>

#include "protocol.h"

#define READ_TIMEOUT_MS	20

enum cmd_code {
	CMD_GET_MODULE_INFO = 0x10,
	CMD_DIGITAL_ACTIVE = 0x20,
	CMD_DIGITAL_INACTIVE = 0x21,
	CMD_DIGITAL_SET_OUTPUTS = 0x23,
	CMD_DIGITAL_GET_OUTPUTS = 0x24,
	CMD_DIGITAL_GET_INPUTS = 0x25,
	CMD_DIGITAL_ACTIVE_1MS = 0x26,
	CMD_DIGITAL_INACTIVE_1MS = 0x27,
	CMD_ANALOG_GET_INPUT = 0x32,
	CMD_ANALOG_GET_INPUT_12BIT = 0x33,
	CMD_ANALOG_GET_ALL_VOLTAGES = 0x34,
	CMD_ASCII_TEXT_COMMAND = 0x3a,
	CMD_GET_SERIAL_NUMBER = 0x77,
	CMD_GET_SUPPLY_VOLTS = 0x78,
	CMD_PASSWORD_ENTRY = 0x79,
	CMD_GET_UNLOCK_TIME = 0x7a,
	CMD_IMMEDIATE_LOGOUT = 0x7b,
};

/*
 * Transmit a request to the relay card. Checks that all bytes get sent,
 * short writes are considered fatal.
 */
static int send_request(struct sr_serial_dev_inst *ser,
	const uint8_t *data, size_t dlen)
{
	int ret;
	size_t written;

	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		GString *txt = sr_hexdump_new(data, dlen);
		sr_spew("TX --> %s.", txt->str);
		sr_hexdump_free(txt);
	}
	ret = serial_write_blocking(ser, data, dlen, 0);
	if (ret < 0)
		return ret;
	written = (size_t)ret;
	if (written != dlen)
		return SR_ERR_DATA;
	return SR_OK;
}

/*
 * Receive a response from the relay card. Assumes fixed size payload,
 * considers short reads fatal.
 */
static int recv_response(struct sr_serial_dev_inst *ser,
	uint8_t *data, size_t dlen)
{
	int ret;
	size_t got;

	ret = serial_read_blocking(ser, data, dlen, READ_TIMEOUT_MS);
	if (ret < 0)
		return ret;
	got = (size_t)ret;
	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		GString *txt = sr_hexdump_new(data, got);
		sr_spew("<-- RX %s.", txt->str);
		sr_hexdump_free(txt);
	}
	if (got != dlen)
		return SR_ERR_DATA;
	return SR_OK;
}

/* Send a request then receive a response. Convenience routine. */
static int send_then_recv(struct sr_serial_dev_inst *serial,
	const uint8_t *tx_data, size_t tx_length,
	uint8_t *rx_data, size_t rx_length)
{
	int ret;

	if (tx_data && tx_length) {
		ret = send_request(serial, tx_data, tx_length);
		if (ret != SR_OK)
			return ret;
	}

	if (rx_data && rx_length) {
		ret = recv_response(serial, rx_data, rx_length);
		if (ret != SR_OK)
			return ret;
	}

	return SR_OK;
}

/* Identify the relay card, gather version information details. */
SR_PRIV int devantech_eth008_get_model(struct sr_serial_dev_inst *serial,
	uint8_t *model_code, uint8_t *hw_version, uint8_t *fw_version)
{
	uint8_t req[1], *wrptr;
	uint8_t rsp[3], v8;
	const uint8_t *rdptr;
	int ret;

	if (model_code)
		*model_code = 0;
	if (hw_version)
		*hw_version = 0;
	if (fw_version)
		*fw_version = 0;

	wrptr = req;
	write_u8_inc(&wrptr, CMD_GET_MODULE_INFO);
	ret = send_then_recv(serial, req, wrptr - req, rsp, sizeof(rsp));
	if (ret != SR_OK)
		return ret;
	rdptr = rsp;

	v8 = read_u8_inc(&rdptr);
	if (model_code)
		*model_code = v8;
	v8 = read_u8_inc(&rdptr);
	if (hw_version)
		*hw_version = v8;
	v8 = read_u8_inc(&rdptr);
	if (fw_version)
		*fw_version = v8;

	return SR_OK;
}

/* Get the relay card's serial number (its MAC address). */
SR_PRIV int devantech_eth008_get_serno(struct sr_serial_dev_inst *serial,
	char *text_buffer, size_t text_length)
{
	uint8_t req[1], *wrptr;
	uint8_t rsp[6], b;
	const uint8_t *rdptr, *endptr;
	size_t written;
	int ret;

	if (text_buffer && !text_length)
		return SR_ERR_ARG;
	if (text_buffer)
		memset(text_buffer, 0, text_length);

	wrptr = req;
	write_u8_inc(&wrptr, CMD_GET_SERIAL_NUMBER);
	ret = send_then_recv(serial, req, wrptr - req, rsp, sizeof(rsp));
	if (ret != SR_OK)
		return ret;
	rdptr = rsp;

	endptr = rsp + sizeof(rsp);
	while (rdptr < endptr && text_buffer && text_length >= 3) {
		b = read_u8_inc(&rdptr);
		written = snprintf(text_buffer, text_length, "%02x", b);
		text_buffer += written;
		text_length -= written;
	}

	return SR_OK;
}

/* Update an internal cache from the relay card's current state. */
SR_PRIV int devantech_eth008_cache_state(const struct sr_dev_inst *sdi)
{
	struct sr_serial_dev_inst *serial;
	struct dev_context *devc;
	size_t rx_size;
	uint8_t req[1], *wrptr;
	uint8_t rsp[4];
	const uint8_t *rdptr;
	uint32_t have;
	int ret;

	serial = sdi->conn;
	if (!serial)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	/* Get the state of digital outputs when the model supports them. */
	if (devc->model->ch_count_do) {
		rx_size = devc->model->width_do;
		if (rx_size > sizeof(rsp))
			return SR_ERR_NA;

		wrptr = req;
		write_u8_inc(&wrptr, CMD_DIGITAL_GET_OUTPUTS);
		ret = send_then_recv(serial, req, wrptr - req, rsp, rx_size);
		if (ret != SR_OK)
			return ret;
		rdptr = rsp;

		switch (rx_size) {
		case 1:
			have = read_u8_inc(&rdptr);
			break;
		case 2:
			have = read_u16le_inc(&rdptr);
			break;
		case 3:
			have = read_u24le_inc(&rdptr);
			break;
		default:
			return SR_ERR_NA;
		}
		have &= devc->mask_do;
		devc->curr_do = have;
	}

	/*
	 * Get the state of digital inputs when the model supports them.
	 * (Sending unsupported requests to unaware firmware versions
	 * yields no response. That's why requests must be conditional.)
	 *
	 * Caching the state of analog inputs is condidered undesirable.
	 * Firmware does conversion at the very moment when the request
	 * is received to get a voltage reading.
	 */
	if (devc->model->ch_count_di) {
		rx_size = devc->model->width_di;
		if (rx_size > sizeof(rsp))
			return SR_ERR_NA;

		wrptr = req;
		write_u8_inc(&wrptr, CMD_DIGITAL_GET_INPUTS);
		ret = send_then_recv(serial, req, wrptr - req, rsp, rx_size);
		if (ret != SR_OK)
			return ret;
		rdptr = rsp;

		switch (rx_size) {
		case 2:
			have = read_u16be_inc(&rdptr);
			break;
		case 4:
			have = read_u32be_inc(&rdptr);
			break;
		default:
			return SR_ERR_NA;
		}
		have &= (1UL << devc->model->ch_count_di) - 1;
		devc->curr_di = have;
	}

	return SR_OK;
}

/* Query the state of an individual relay channel. */
SR_PRIV int devantech_eth008_query_do(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, gboolean *on)
{
	struct dev_context *devc;
	struct channel_group_context *cgc;
	uint32_t have;
	int ret;

	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

	/* Unconditionally update the internal cache. */
	ret = devantech_eth008_cache_state(sdi);
	if (ret != SR_OK)
		return ret;

	/*
	 * Only reject unexpected requests after the update. Get the
	 * individual channel's state from the cache of all channels.
	 */
	if (!cg)
		return SR_ERR_ARG;
	cgc = cg->priv;
	if (!cgc)
		return SR_ERR_BUG;
	if (cgc->index >= devc->model->ch_count_do)
		return SR_ERR_ARG;
	have = devc->curr_do;
	have >>= cgc->index;
	have &= 1 << 0;
	if (on)
		*on = have ? TRUE : FALSE;

	return SR_OK;
}

/*
 * Manipulate the state of an individual relay channel (when cg is given).
 * Or set/clear all channels at the same time (when cg is NULL).
 */
SR_PRIV int devantech_eth008_setup_do(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, gboolean on)
{
	struct sr_serial_dev_inst *serial;
	struct dev_context *devc;
	size_t width_do;
	struct channel_group_context *cgc;
	size_t number;
	uint32_t reg;
	uint8_t req[4], *wrptr, cmd;
	uint8_t rsp[1], v8;
	const uint8_t *rdptr;
	int ret;

	serial = sdi->conn;
	if (!serial)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	cgc = cg ? cg->priv : NULL;
	if (cgc && cgc->index >= devc->model->ch_count_do)
		return SR_ERR_ARG;

	width_do = devc->model->width_do;
	if (1 + width_do > sizeof(req))
		return SR_ERR_NA;

	wrptr = req;
	if (cgc) {
		/* Manipulate an individual channel. */
		cmd = on ? CMD_DIGITAL_ACTIVE : CMD_DIGITAL_INACTIVE;
		number = cgc->number;
		write_u8_inc(&wrptr, cmd);
		write_u8_inc(&wrptr, number & 0xff);
		write_u8_inc(&wrptr, 0); /* Just set/clear, no pulse. */
	} else {
		/* Manipulate all channels at the same time. */
		reg = on ? devc->mask_do : 0;
		write_u8_inc(&wrptr, CMD_DIGITAL_SET_OUTPUTS);
		switch (width_do) {
		case 1:
			write_u8_inc(&wrptr, reg & 0xff);
			break;
		case 2:
			write_u16le_inc(&wrptr, reg & 0xffff);
			break;
		case 3:
			write_u24le_inc(&wrptr, reg & 0xffffff);
			break;
		default:
			return SR_ERR_NA;
		}
	}
	ret = send_then_recv(serial, req, wrptr - req, rsp, sizeof(rsp));
	if (ret != SR_OK)
		return ret;
	rdptr = rsp;

	v8 = read_u8_inc(&rdptr);
	if (v8 != 0)
		return SR_ERR_DATA;

	return SR_OK;
}

SR_PRIV int devantech_eth008_query_di(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, gboolean *on)
{
	struct dev_context *devc;
	struct channel_group_context *cgc;
	uint32_t have;
	int ret;

	/* Unconditionally update the internal cache. */
	ret = devantech_eth008_cache_state(sdi);
	if (ret != SR_OK)
		return ret;

	/*
	 * Only reject unexpected requests after the update. Get the
	 * individual channel's state from the cache of all channels.
	 */
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	if (!cg)
		return SR_ERR_ARG;
	cgc = cg->priv;
	if (!cgc)
		return SR_ERR_BUG;
	if (cgc->index >= devc->model->ch_count_di)
		return SR_ERR_ARG;
	have = devc->curr_di;
	have >>= cgc->index;
	have &= 1 << 0;
	if (on)
		*on = have ? TRUE : FALSE;

	return SR_OK;
}

SR_PRIV int devantech_eth008_query_ai(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, uint16_t *adc_value)
{
	struct sr_serial_dev_inst *serial;
	struct dev_context *devc;
	struct channel_group_context *cgc;
	uint8_t req[2], *wrptr;
	uint8_t rsp[2];
	const uint8_t *rdptr;
	uint32_t have;
	int ret;

	serial = sdi->conn;
	if (!serial)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;
	if (!cg)
		return SR_ERR_ARG;
	cgc = cg->priv;
	if (!cgc)
		return SR_ERR_ARG;
	if (cgc->index >= devc->model->ch_count_ai)
		return SR_ERR_ARG;

	wrptr = req;
	write_u8_inc(&wrptr, CMD_ANALOG_GET_INPUT);
	write_u8_inc(&wrptr, cgc->number & 0xff);
	ret = send_then_recv(serial, req, wrptr - req, rsp, sizeof(rsp));
	if (ret != SR_OK)
		return ret;
	rdptr = rsp;

	/*
	 * The interpretation of analog readings differs across models.
	 * All firmware versions provide an ADC result in BE format in
	 * a 16bit response. Some models provide 10 significant digits,
	 * others provide 12 bits. Full scale can either be 3V3 or 5V0.
	 * Some devices are 5V tolerant but won't read more than 3V3
	 * values (and clip above that full scale value). Some firmware
	 * versions support request 0x33 in addition to 0x32.
	 *
	 * This is why this implementation provides the result to the
	 * caller as a unit-less value. It is also what the firmware's
	 * web interface does.
	 */
	have = read_u16be_inc(&rdptr);
	if (adc_value)
		*adc_value = have;

	return SR_OK;
}

SR_PRIV int devantech_eth008_query_supply(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, uint16_t *millivolts)
{
	struct sr_serial_dev_inst *serial;
	uint8_t req[1], *wrptr;
	uint8_t rsp[1];
	const uint8_t *rdptr;
	uint16_t have;
	int ret;

	(void)cg;

	serial = sdi->conn;
	if (!serial)
		return SR_ERR_ARG;

	wrptr = req;
	write_u8_inc(&wrptr, CMD_GET_SUPPLY_VOLTS);
	ret = send_then_recv(serial, req, wrptr - req, rsp, sizeof(rsp));
	if (ret != SR_OK)
		return ret;
	rdptr = rsp;

	/* Gets a byte for voltage in units of 0.1V. Scale up to mV. */
	have = read_u8_inc(&rdptr);
	have *= 100;
	if (millivolts)
		*millivolts = have;

	return SR_OK;
}
