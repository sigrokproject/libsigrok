/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Andreas Sandberg <andreas@sandberg.pp.se>
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
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <glib.h>
#include <nettle/aes.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "protocol.h"

#define SERIAL_WRITE_TIMEOUT_MS 1

#define TC_POLL_LEN 192
#define TC_POLL_PERIOD_MS 100
#define TC_TIMEOUT_MS 1000

static const char POLL_CMD[] = "getva";

#define MAGIC_PAC1 0x31636170UL
#define MAGIC_PAC2 0x32636170UL
#define MAGIC_PAC3 0x33636170UL

/* Length of PAC block excluding CRC */
#define PAC_DATA_LEN 60
/* Length of PAC block including CRC */
#define PAC_LEN 64

/* Offset to PAC block from start of poll data */
#define OFF_PAC1 (0 * PAC_LEN)
#define OFF_PAC2 (1 * PAC_LEN)
#define OFF_PAC3 (2 * PAC_LEN)

#define OFF_MODEL 4
#define LEN_MODEL 4

#define OFF_FW_VER 8
#define LEN_FW_VER 4

#define OFF_SERIAL 12

static const uint8_t AES_KEY[] = {
	0x58, 0x21, 0xfa, 0x56, 0x01, 0xb2, 0xf0, 0x26,
	0x87, 0xff, 0x12, 0x04, 0x62, 0x2a, 0x4f, 0xb0,
	0x86, 0xf4, 0x02, 0x60, 0x81, 0x6f, 0x9a, 0x0b,
	0xa7, 0xf1, 0x06, 0x61, 0x9a, 0xb8, 0x72, 0x88,
};

static const struct binary_analog_channel rdtech_tc_channels[] = {
	{ "V",  {   0 + 48, BVT_LE_UINT32, 1e-4, }, 4, SR_MQ_VOLTAGE, SR_UNIT_VOLT },
	{ "I",  {   0 + 52, BVT_LE_UINT32, 1e-5, }, 5, SR_MQ_CURRENT, SR_UNIT_AMPERE },
	{ "D+", {  64 + 32, BVT_LE_UINT32, 1e-2, }, 2, SR_MQ_VOLTAGE, SR_UNIT_VOLT },
	{ "D-", {  64 + 36, BVT_LE_UINT32, 1e-2, }, 2, SR_MQ_VOLTAGE, SR_UNIT_VOLT },
	{ "E0", {  64 + 12, BVT_LE_UINT32, 1e-3, }, 3, SR_MQ_ENERGY, SR_UNIT_WATT_HOUR },
	{ "E1", {  64 + 20, BVT_LE_UINT32, 1e-3, }, 3, SR_MQ_ENERGY, SR_UNIT_WATT_HOUR },
	{ NULL, },
};

static int check_pac_crc(uint8_t *data)
{
	uint16_t crc;
	uint32_t crc_field;

	crc = sr_crc16(SR_CRC16_DEFAULT_INIT, data, PAC_DATA_LEN);
	crc_field = RL32(data + PAC_DATA_LEN);

	if (crc != crc_field) {
		sr_spew("CRC error. Calculated: %0x" PRIx16 ", expected: %0x" PRIx32,
			crc, crc_field);
		return 0;
	} else {
		return 1;
	}
}

static int process_poll_pkt(struct dev_context  *devc, uint8_t *dst)
{
	struct aes256_ctx ctx;

	aes256_set_decrypt_key(&ctx, AES_KEY);
	aes256_decrypt(&ctx, TC_POLL_LEN, dst, devc->buf);

	if (RL32(dst + OFF_PAC1) != MAGIC_PAC1 ||
	    RL32(dst + OFF_PAC2) != MAGIC_PAC2 ||
	    RL32(dst + OFF_PAC3) != MAGIC_PAC3) {
		sr_err("Invalid poll packet magic values!");
		return SR_ERR;
	}

	if (!check_pac_crc(dst + OFF_PAC1) ||
	    !check_pac_crc(dst + OFF_PAC2) ||
	    !check_pac_crc(dst + OFF_PAC3)) {
		sr_err("Invalid poll checksum!");
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV int rdtech_tc_probe(struct sr_serial_dev_inst *serial, struct dev_context  *devc)
{
	int len;
	uint8_t poll_pkt[TC_POLL_LEN];

	if (serial_write_blocking(serial, &POLL_CMD, sizeof(POLL_CMD) - 1,
                                  SERIAL_WRITE_TIMEOUT_MS) < 0) {
		sr_err("Unable to send probe request.");
		return SR_ERR;
	}

	len = serial_read_blocking(serial, devc->buf, TC_POLL_LEN, TC_TIMEOUT_MS);
	if (len != TC_POLL_LEN) {
		sr_err("Failed to read probe response.");
		return SR_ERR;
	}

	if (process_poll_pkt(devc, poll_pkt) != SR_OK) {
		sr_err("Unrecognized TC device!");
		return SR_ERR;
	}

	devc->channels = rdtech_tc_channels;
	devc->dev_info.model_name = g_strndup((const char *)poll_pkt + OFF_MODEL, LEN_MODEL);
	devc->dev_info.fw_ver = g_strndup((const char *)poll_pkt + OFF_FW_VER, LEN_FW_VER);
	devc->dev_info.serial_num = RL32(poll_pkt + OFF_SERIAL);

	return SR_OK;
}

SR_PRIV int rdtech_tc_poll(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	struct sr_serial_dev_inst *serial = sdi->conn;

	if (serial_write_blocking(serial, &POLL_CMD, sizeof(POLL_CMD) - 1,
                                  SERIAL_WRITE_TIMEOUT_MS) < 0) {
		sr_err("Unable to send poll request.");
		return SR_ERR;
	}

	devc->cmd_sent_at = g_get_monotonic_time() / 1000;

	return SR_OK;
}

static void handle_poll_data(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	uint8_t poll_pkt[TC_POLL_LEN];
	int i;
	GSList *ch;

	sr_spew("Received poll packet (len: %d).", devc->buflen);
	if (devc->buflen != TC_POLL_LEN) {
		sr_err("Unexpected poll packet length: %i", devc->buflen);
		return;
	}

	if (process_poll_pkt(devc, poll_pkt) != SR_OK) {
		sr_err("Failed to process poll packet.");
		return;
	}

	for (ch = sdi->channels, i = 0; ch; ch = g_slist_next(ch), i++) {
		bv_send_analog_channel(sdi, ch->data,
				       &devc->channels[i], poll_pkt, TC_POLL_LEN);
        }

	sr_sw_limits_update_samples_read(&devc->limits, 1);
}

static void recv_poll_data(struct sr_dev_inst *sdi, struct sr_serial_dev_inst *serial)
{
	struct dev_context *devc = sdi->priv;
	int len;

	/* Serial data arrived. */
	while (devc->buflen < TC_POLL_LEN) {
		len = serial_read_nonblocking(serial, devc->buf + devc->buflen, 1);
		if (len < 1)
			return;

		devc->buflen++;
	}

	if (devc->buflen == TC_POLL_LEN)
		handle_poll_data(sdi);

	devc->buflen = 0;
}

SR_PRIV int rdtech_tc_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int64_t now, elapsed;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;
	if (revents == G_IO_IN)
		recv_poll_data(sdi, serial);

	if (sr_sw_limits_check(&devc->limits)) {
		sr_dev_acquisition_stop(sdi);
		return TRUE;
	}

	now = g_get_monotonic_time() / 1000;
	elapsed = now - devc->cmd_sent_at;

	if (elapsed > TC_POLL_PERIOD_MS)
		rdtech_tc_poll(sdi);

	return TRUE;
}
