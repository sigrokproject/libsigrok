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
 *
 * See http://www.robot-electronics.co.uk/files/eth008b.pdf for device
 * capabilities and a protocol discussion.
 * See https://github.com/devantech/devantech_eth_python for Python
 * source code which is maintained by the vendor.
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
 * the peripheral's state can change even while we control it.
 *
 * It's assumed that WLAN models differ from Ethernet devices in terms
 * of their hardware, but TCP communication should not bother about the
 * underlying physics, and WLAN cards can re-use model IDs and firmware
 * implementations. Given sigrok's abstraction of the serial transport
 * those cards could also be attached by means of COM ports.
 *
 * TCP communication seems to rely on network fragmentation and assumes
 * that software stacks provide all of a request in a single receive
 * call on the firmware side. Which works for local communication, but
 * could become an issue when long distances and tunnels are involved.
 * This sigrok driver also assumes complete reception within a single
 * receive call. The short length of binary transmission helps here
 * (the largest payloads has a length of three bytes).
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
 * TODO
 * - Add support for other models. Currently exclusively supports the
 *   ETH008-B model which was used during implementation of the driver.
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
	uint8_t rsp[1];
	const uint8_t *rdptr;
	uint32_t have;
	int ret;

	serial = sdi->conn;
	if (!serial)
		return SR_ERR_ARG;
	devc = sdi->priv;
	if (!devc)
		return SR_ERR_ARG;

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
	default:
		return SR_ERR_NA;
	}
	have &= devc->mask_do;
	devc->curr_do = have;

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
	 * Only reject unexpected requeusts after the update. Get the
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
	uint8_t req[3], *wrptr, cmd;
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
