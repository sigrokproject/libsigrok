/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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
#include <math.h>
#include "protocol.h"

#define SEP	"\r\n"
#define BLANK	':'
#define NEG	';'

/*
 * Get a temperature value from a four-character buffer. The value is
 * encoded in ASCII and the unit is deci-degrees (tenths of degrees).
 */
static float parse_temperature(unsigned char *buf)
{
	float temp;
	int i;
	gboolean negative;

	negative = FALSE;
	temp = 0.0;
	for (i = 0; i < 4; i++) {
		if (buf[i] == BLANK)
			continue;
		if (buf[i] == NEG) {
			if (negative) {
				sr_dbg("Double negative sign!");
				return NAN;
			}
			negative = TRUE;
			continue;
		}
		if (buf[i] < '0' || buf[i] > '9') {
			sr_dbg("Invalid digit '%.2x'!", buf[i]);
			return NAN;
		}
		temp *= 10;
		temp += buf[i] - '0';
	}
	temp /= 10;
	if (negative)
		temp = -temp;

	return temp;
}

static void process_packet(struct sr_dev_inst *sdi, uint8_t *pkt, size_t len)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	GString *spew;
	float temp;
	gboolean is_valid;

	if (sr_log_loglevel_get() >= SR_LOG_SPEW) {
		spew = sr_hexdump_new(pkt, len);
		sr_spew("Got a packet, len %zu, bytes%s", len, spew->str);
		sr_hexdump_free(spew);
	}
	if (len != PACKET_SIZE)
		return;
	if (pkt[17] != SEP[0] || pkt[18] != SEP[1])
		return;
	if (pkt[8] != '0' || pkt[16] != '1')
		return;
	sr_dbg("Processing 19-byte packet.");

	is_valid = TRUE;
	if (pkt[1] == NEG && pkt[2] == NEG && pkt[3] == NEG && pkt[4] == NEG)
		/* No measurement: missing channel, empty storage location, ... */
		is_valid = FALSE;

	temp = parse_temperature(&pkt[1]);
	if (isnan(temp))
		is_valid = FALSE;

	if (is_valid) {
		memset(&packet, 0, sizeof(packet));
		sr_analog_init(&analog, &encoding, &meaning, &spec, 1);
		analog.meaning->mq = SR_MQ_TEMPERATURE;
		analog.meaning->mqflags = 0;
		switch (pkt[5] - '0') {
		case 1:
			analog.meaning->unit = SR_UNIT_CELSIUS;
			break;
		case 2:
			analog.meaning->unit = SR_UNIT_FAHRENHEIT;
			break;
		case 3:
			analog.meaning->unit = SR_UNIT_KELVIN;
			break;
		default:
			/* We can still pass on the measurement, whatever it is. */
			sr_dbg("Unknown unit 0x%.2x.", pkt[5]);
		}
		switch (pkt[13] - '0') {
		case 0:
			/* Channel T1. */
			analog.meaning->channels = g_slist_append(NULL, g_slist_nth_data(sdi->channels, 0));
			break;
		case 1:
			/* Channel T2. */
			analog.meaning->channels = g_slist_append(NULL, g_slist_nth_data(sdi->channels, 1));
			break;
		case 2:
		case 3:
			/* Channel T1-T2. */
			analog.meaning->channels = g_slist_append(NULL, g_slist_nth_data(sdi->channels, 2));
			analog.meaning->mqflags |= SR_MQFLAG_RELATIVE;
			break;
		default:
			sr_err("Unknown channel 0x%.2x.", pkt[13]);
			is_valid = FALSE;
		}
		if (is_valid) {
			analog.num_samples = 1;
			analog.data = &temp;
			packet.type = SR_DF_ANALOG;
			packet.payload = &analog;
			sr_session_send(sdi, &packet);
			g_slist_free(analog.meaning->channels);
		}
	}

	/*
	 * We count packets even if the measurement was invalid. This way
	 * a sample limit on "Memory" data source still works: Unused
	 * memory slots come through as "----" measurements.
	 */
	devc = sdi->priv;
	sr_sw_limits_update_samples_read(&devc->limits, 1);
	if (sr_sw_limits_check(&devc->limits))
		sr_dev_acquisition_stop(sdi);
}

static int process_buffer(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	uint8_t *pkt;
	size_t remain, idx;

	/*
	 * Specifically do not insist on finding the packet boundary at
	 * the end of the most recently received data chunk. Serial
	 * ports might involve hardware buffers (FIFO). We want to sync
	 * as fast as possible.
	 *
	 * Handle the synchronized situation first. Process complete
	 * packets that reside at the start of the buffer. Then fallback
	 * to incomplete or unaligned packets if the receive buffer
	 * still contains data bytes. (Depending on the bitrate and the
	 * poll interval, we may always end up in the manual search. But
	 * considering the update rate - two or three packets per second
	 * - this is not an issue.)
	 */
	devc = sdi->priv;
	pkt = &devc->packet[0];
	while (devc->packet_len >= PACKET_SIZE &&
			pkt[PACKET_SIZE - 2] == SEP[0] &&
			pkt[PACKET_SIZE - 1] == SEP[1]) {
		process_packet(sdi, &pkt[0], PACKET_SIZE);
		remain = devc->packet_len - PACKET_SIZE;
		if (remain)
			memmove(&pkt[0], &pkt[PACKET_SIZE], remain);
		devc->packet_len -= PACKET_SIZE;
	}

	/*
	 * The 'for' loop and the increment upon re-iteration after
	 * setting the loop var to zero is not an issue. The marker has
	 * two bytes, so effectively starting the search at offset 1 is
	 * fine for the specific packet layout.
	 */
	for (idx = 0; idx < devc->packet_len; idx++) {
		if (idx < 1)
			continue;
		if (pkt[idx - 1] != SEP[0] || pkt[idx] != SEP[1])
			continue;
		/* Found a packet that spans up to and including 'idx'. */
		idx++;
		process_packet(sdi, &pkt[0], idx);
		remain = devc->packet_len - idx;
		if (remain)
			memmove(&pkt[0], &pkt[idx], remain);
		devc->packet_len -= idx;
		idx = 0;
	}

	return 0;
}

/* Gets invoked when RX data is available. */
static int ut32x_receive_data(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	size_t len;

	devc = sdi->priv;
	serial = sdi->conn;

	/*
	 * Discard receive data when the buffer is exhausted. This shall
	 * allow to (re-)synchronize to the data stream when we find it
	 * in an arbitrary state. Drain more data from the serial port,
	 * and check the receive buffer for packets.
	 */
	if (devc->packet_len == sizeof(devc->packet)) {
		process_packet(sdi, &devc->packet[0], devc->packet_len);
		devc->packet_len = 0;
	}
	len = sizeof(devc->packet) - devc->packet_len;
	len = serial_read_nonblocking(serial,
			&devc->packet[devc->packet_len], len);
	if (!len)
		return 0;

	devc->packet_len += len;
	process_buffer(sdi);

	return 0;
}

/* Gets periodically invoked by the glib main loop. */
SR_PRIV int ut32x_handle_events(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct sr_serial_dev_inst *serial;
	uint8_t cmd;

	(void)fd;

	sdi = cb_data;
	if (!sdi)
		return TRUE;
	serial = sdi->conn;
	if (!serial)
		return TRUE;

	if (revents & G_IO_IN)
		ut32x_receive_data(sdi);

	if (sdi->status == SR_ST_STOPPING) {
		serial_source_remove(sdi->session, serial);
		std_session_send_df_end(sdi);
		sdi->status = SR_ST_ACTIVE;

		/* Tell the device to stop sending data. */
		cmd = CMD_STOP;
		serial_write_blocking(serial, &cmd, sizeof(cmd), 0);
	}

	return TRUE;
}
