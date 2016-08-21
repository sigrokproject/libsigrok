/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <config.h>
#include "protocol.h"

#define NUM_CHANNELS 4

struct center_info {
	float temp[NUM_CHANNELS];
	int digits[NUM_CHANNELS];
	gboolean rec, std, max, min, maxmin, t1t2, rel, hold, lowbat, celsius;
	gboolean memfull, autooff;
	gboolean mode_std, mode_rel, mode_max, mode_min, mode_maxmin;
};

static int center_send(struct sr_serial_dev_inst *serial, const char *cmd)
{
	int ret;

	if ((ret = serial_write_blocking(serial, cmd, strlen(cmd),
			serial_timeout(serial, strlen(cmd)))) < 0) {
		sr_err("Error sending '%s' command: %d.", cmd, ret);
		return SR_ERR;
	}

	return SR_OK;
}

SR_PRIV gboolean center_3xx_packet_valid(const uint8_t *buf)
{
	return (buf[0] == 0x02 && buf[44] == 0x03);
}

static void log_packet(const uint8_t *buf, int idx)
{
	int i;
	GString *s;

	s = g_string_sized_new(100);
	g_string_printf(s, "Packet: ");
	for (i = 0; i < center_devs[idx].packet_size; i++)
	        g_string_append_printf(s, "%02x ", buf[i]);
	sr_spew("%s", s->str);
	g_string_free(s, TRUE);
}

static int packet_parse(const uint8_t *buf, int idx, struct center_info *info)
{
	int i;
	uint16_t temp_u16;

	log_packet(buf, idx);

	/* Byte 0: Always 0x02. */

	/* Byte 1: Various status bits. */
	info->rec         = (buf[1] & (1 << 0)) != 0;
	info->mode_std    = (((buf[1] >> 1) & 0x3) == 0);
	info->mode_max    = (((buf[1] >> 1) & 0x3) == 1);
	info->mode_min    = (((buf[1] >> 1) & 0x3) == 2);
	info->mode_maxmin = (((buf[1] >> 1) & 0x3) == 3);
	/* TODO: Rel. Not available on all models. */
	info->t1t2        = (buf[1] & (1 << 3)) != 0;
	info->rel         = (buf[1] & (1 << 4)) != 0;
	info->hold        = (buf[1] & (1 << 5)) != 0;
	info->lowbat      = (buf[1] & (1 << 6)) != 0;
	info->celsius     = (buf[1] & (1 << 7)) != 0;

	/* Byte 2: Further status bits. */
	info->memfull     = (buf[2] & (1 << 0)) != 0;
	info->autooff     = (buf[2] & (1 << 7)) != 0;

	/* Byte 7+8/9+10/11+12/13+14: channel T1/T2/T3/T4 temperature. */
	for (i = 0; i < NUM_CHANNELS; i++) {
		temp_u16 = buf[8 + (i * 2)];
		temp_u16 |= ((uint16_t)buf[7 + (i * 2)] << 8);
		info->temp[i] = (float)temp_u16;
	}

	/* Byte 43: Specifies whether we need to divide the value(s) by 10. */
	for (i = 0; i < NUM_CHANNELS; i++) {
		/* Bit = 0: Divide by 10. Bit = 1: Don't divide by 10. */
		if ((buf[43] & (1 << i)) == 0) {
			info->temp[i] /= 10;
			info->digits[i] = 1;
		} else {
			info->digits[i] = 0;
		}
	}

	/* Bytes 39-42: Overflow/overlimit bits, depending on mode. */
	for (i = 0; i < NUM_CHANNELS; i++) {
		if (info->mode_std && ((buf[39] & (1 << i)) != 0))
			info->temp[i] = INFINITY;
		/* TODO: Rel. Not available on all models. */
		// if (info->mode_rel && ((buf[40] & (1 << i)) != 0))
		// 	info->temp[i] = INFINITY;
		if (info->mode_max && ((buf[41] & (1 << i)) != 0))
			info->temp[i] = INFINITY;
		if (info->mode_min && ((buf[42] & (1 << i)) != 0))
			info->temp[i] = INFINITY;
		/* TODO: Minmax? */
	}

	/* Byte 44: Always 0x03. */

	return SR_OK;
}

static int handle_packet(const uint8_t *buf, struct sr_dev_inst *sdi, int idx)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	struct dev_context *devc;
	struct center_info info;
	GSList *l;
	int i, ret;

	devc = sdi->priv;

	sr_analog_init(&analog, &encoding, &meaning, &spec, 0);
	memset(&info, 0, sizeof(struct center_info));

	ret = packet_parse(buf, idx, &info);
	if (ret < 0) {
		sr_err("Failed to parse packet.");
		return SR_ERR;
	}

	/* Common values for all 4 channels. */
	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	analog.meaning->mq = SR_MQ_TEMPERATURE;
	analog.meaning->unit = (info.celsius) ? SR_UNIT_CELSIUS : SR_UNIT_FAHRENHEIT;
	analog.num_samples = 1;

	/* Send the values for T1 - T4. */
	for (i = 0; i < NUM_CHANNELS; i++) {
		l = NULL;
		l = g_slist_append(l, g_slist_nth_data(sdi->channels, i));
		analog.meaning->channels = l;
		analog.encoding->digits = info.digits[i];
		analog.spec->spec_digits = info.digits[i];
		analog.data = &(info.temp[i]);
		sr_session_send(sdi, &packet);
		g_slist_free(l);
	}

	sr_sw_limits_update_samples_read(&devc->sw_limits, 1);

	return SR_OK;
}

/* Return TRUE if a full packet was parsed, FALSE otherwise. */
static gboolean handle_new_data(struct sr_dev_inst *sdi, int idx)
{
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int len, i, offset = 0, ret = FALSE;

	devc = sdi->priv;
	serial = sdi->conn;

	/* Try to get as much data as the buffer can hold. */
	len = SERIAL_BUFSIZE - devc->buflen;
	len = serial_read_nonblocking(serial, devc->buf + devc->buflen, len);
	if (len < 1) {
		sr_err("Serial port read error: %d.", len);
		return FALSE;
	}

	devc->buflen += len;

	/* Now look for packets in that data. */
	while ((devc->buflen - offset) >= center_devs[idx].packet_size) {
		if (center_devs[idx].packet_valid(devc->buf + offset)) {
			handle_packet(devc->buf + offset, sdi, idx);
			offset += center_devs[idx].packet_size;
			ret = TRUE;
		} else {
			offset++;
		}
	}

	/* If we have any data left, move it to the beginning of our buffer. */
	for (i = 0; i < devc->buflen - offset; i++)
		devc->buf[i] = devc->buf[offset + i];
	devc->buflen -= offset;

	return ret;
}

static int receive_data(int fd, int revents, int idx, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	static gboolean request_new_packet = TRUE;
	struct sr_serial_dev_inst *serial;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	serial = sdi->conn;

	if (revents == G_IO_IN) {
		/* New data arrived. */
		request_new_packet = handle_new_data(sdi, idx);
	} else {
		/*
		 * Timeout. Send "A" to request a packet, but then don't send
		 * further "A" commands until we received a full packet first.
		 */
		if (request_new_packet) {
			center_send(serial, "A");
			request_new_packet = FALSE;
		}
	}

	if (sr_sw_limits_check(&devc->sw_limits))
		sdi->driver->dev_acquisition_stop(sdi);

	return TRUE;
}

#define RECEIVE_DATA(ID_UPPER) \
SR_PRIV int receive_data_##ID_UPPER(int fd, int revents, void *cb_data) { \
	return receive_data(fd, revents, ID_UPPER, cb_data); }

/* Driver-specific receive_data() wrappers */
RECEIVE_DATA(CENTER_309)
RECEIVE_DATA(VOLTCRAFT_K204)
