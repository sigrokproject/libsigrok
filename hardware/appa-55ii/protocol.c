/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Aurelien Jacobs <aurel@gnuage.org>
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

#include <string.h>
#include <math.h>
#include "protocol.h"

typedef enum {
    LIVE_DATA    = 0x00,
    LOG_METADATA = 0x11,
    LOG_DATA     = 0x14,
    LOG_START    = 0x18,
    LOG_END      = 0x19,
} packet_type;

static gboolean appa_55ii_checksum(const uint8_t *buf)
{
	int i, size, checksum;

	size = buf[3] + 4;
	checksum = 0;
	for (i = 0; i < size; i++)
		checksum += buf[i];

	return buf[size] == (checksum & 0xFF);
}

SR_PRIV gboolean appa_55ii_packet_valid(const uint8_t *buf)
{
	if (buf[0] == 0x55 && buf[1] == 0x55 && buf[3] <= 32
			&& appa_55ii_checksum(buf))
		return TRUE;

	return FALSE;
}

static uint64_t appa_55ii_flags(const uint8_t *buf)
{
	uint8_t disp_mode;
	uint64_t flags;

	disp_mode = buf[4 + 13];
	flags = 0;
	if ((disp_mode & 0xF0) == 0x20)
		flags |= SR_MQFLAG_HOLD;
	if ((disp_mode & 0x0C) == 0x04)
		flags |= SR_MQFLAG_MAX;
	if ((disp_mode & 0x0C) == 0x08)
		flags |= SR_MQFLAG_MIN;
	if ((disp_mode & 0x0C) == 0x0C)
		flags |= SR_MQFLAG_AVG;

	return flags;
}

static float appa_55ii_temp(const uint8_t *buf, int ch)
{
	const uint8_t *ptr;
	int16_t temp;
	uint8_t flags;

	ptr = buf + 4 + 14 + 3 * ch;
	temp = RL16(ptr);
	flags = ptr[2];

	if (flags & 0x60)
		return INFINITY;
	else if (flags & 1)
		return (float)temp / 10;
	else
		return (float)temp;
}

static void appa_55ii_live_data(struct sr_dev_inst *sdi, const uint8_t *buf)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_channel *ch;
	float values[APPA_55II_NUM_PROBES], *val_ptr;
	int i;

	devc = sdi->priv;

	if (devc->data_source != DATA_SOURCE_LIVE)
		return;

	val_ptr = values;
	memset(&analog, 0, sizeof(analog));
	analog.num_samples = 1;
	analog.mq = SR_MQ_TEMPERATURE;
	analog.unit = SR_UNIT_CELSIUS;
	analog.mqflags = appa_55ii_flags(buf);
	analog.data = values;

	for (i = 0; i < APPA_55II_NUM_PROBES; i++) {
		ch = g_slist_nth_data(sdi->channels, i);
		if (!ch->enabled)
			continue;
		analog.channels = g_slist_append(analog.channels, ch);
		*val_ptr++ = appa_55ii_temp(buf, i);
	}

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;
	sr_session_send(devc->session_cb_data, &packet);
	g_slist_free(analog.channels);

	devc->num_samples++;
}

static void appa_55ii_log_metadata(struct sr_dev_inst *sdi, const uint8_t *buf)
{
	struct dev_context *devc;

	devc = sdi->priv;
	devc->num_log_records = (buf[5] << 8) + buf[4];
}

static void appa_55ii_log_data_parse(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_channel *ch;
	float values[APPA_55II_NUM_PROBES], *val_ptr;
	const uint8_t *buf;
	int16_t temp;
	int offset, i;

	devc = sdi->priv;
	offset = 0;

	while (devc->log_buf_len >= 20 && devc->num_log_records > 0) {
		buf = devc->log_buf + offset;
		val_ptr = values;

		/* FIXME: Timestamp should be sent in the packet. */
		sr_dbg("Timestamp: %02d:%02d:%02d", buf[2], buf[3], buf[4]);

		memset(&analog, 0, sizeof(analog));
		analog.num_samples = 1;
		analog.mq = SR_MQ_TEMPERATURE;
		analog.unit = SR_UNIT_CELSIUS;
		analog.data = values;

		for (i = 0; i < APPA_55II_NUM_PROBES; i++) {
			temp = RL16(buf + 12 + 2 * i);
			ch = g_slist_nth_data(sdi->channels, i);
			if (!ch->enabled)
				continue;
			analog.channels = g_slist_append(analog.channels, ch);
			*val_ptr++ = temp == 0x7FFF ? INFINITY : (float)temp / 10;
		}

		packet.type = SR_DF_ANALOG;
		packet.payload = &analog;
		sr_session_send(devc->session_cb_data, &packet);
		g_slist_free(analog.channels);

		devc->num_samples++;
		devc->log_buf_len -= 20;
		offset += 20;
		devc->num_log_records--;
	}

	memmove(devc->log_buf, devc->log_buf + offset, devc->log_buf_len);
}

static void appa_55ii_log_data(struct sr_dev_inst *sdi, const uint8_t *buf)
{
	struct dev_context *devc;
	const uint8_t *ptr;
	unsigned int size;
	int s;

	devc = sdi->priv;
	if (devc->data_source != DATA_SOURCE_MEMORY)
		return;

	ptr = buf + 4;
	size = buf[3];
	while (size > 0) {
		s = MIN(size, sizeof(devc->log_buf) - devc->log_buf_len);
		memcpy(devc->log_buf + devc->log_buf_len, ptr, s);
		devc->log_buf_len += s;
		size -= s;
		ptr += s;

		appa_55ii_log_data_parse(sdi);
	}
}

static void appa_55ii_log_end(struct sr_dev_inst *sdi)
{
	struct dev_context *devc;

	devc = sdi->priv;
	if (devc->data_source != DATA_SOURCE_MEMORY)
		return;

	sdi->driver->dev_acquisition_stop(sdi, devc->session_cb_data);
}

static const uint8_t *appa_55ii_parse_data(struct sr_dev_inst *sdi,
		const uint8_t *buf, int len)
{
	if (len < 5)
		/* Need more data. */
		return NULL;

	if (buf[0] != 0x55 || buf[1] != 0x55)
		/* Try to re-synchronize on a packet start. */
		return buf + 1;

	if (len < 5 + buf[3])
		/* Need more data. */
		return NULL;

	if (!appa_55ii_checksum(buf))
		/* Skip broken packet. */
		return buf + 4 + buf[3] + 1;

	switch ((packet_type)buf[2]) {
	case LIVE_DATA:
		appa_55ii_live_data(sdi, buf);
		break;
	case LOG_METADATA:
		appa_55ii_log_metadata(sdi, buf);
		break;
	case LOG_DATA:
		appa_55ii_log_data(sdi, buf);
		break;
	case LOG_START:
		break;
	case LOG_END:
		appa_55ii_log_end(sdi);
		break;
	default:
		sr_warn("Invalid packet type: 0x%02x.", buf[2]);
		break;
	}

	return buf + 4 + buf[3] + 1;
}

SR_PRIV int appa_55ii_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_serial_dev_inst *serial;
	int64_t time;
	const uint8_t *ptr, *next_ptr, *end_ptr;
	int len;

	(void)fd;

	if (!(sdi = cb_data) || !(devc = sdi->priv) || revents != G_IO_IN)
		return TRUE;
	serial = sdi->conn;

	/* Try to get as much data as the buffer can hold. */
	len = sizeof(devc->buf) - devc->buf_len;
	len = serial_read(serial, devc->buf + devc->buf_len, len);
	if (len < 1) {
		sr_err("Serial port read error: %d.", len);
		return FALSE;
	}
	devc->buf_len += len;

	/* Now look for packets in that data. */
	ptr = devc->buf;
	end_ptr = ptr + devc->buf_len;
	while ((next_ptr = appa_55ii_parse_data(sdi, ptr, end_ptr - ptr)))
		ptr = next_ptr;

	/* If we have any data left, move it to the beginning of our buffer. */
	memmove(devc->buf, ptr, end_ptr - ptr);
	devc->buf_len -= ptr - devc->buf;

	/* If buffer is full and no valid packet was found, wipe buffer. */
	if (devc->buf_len >= sizeof(devc->buf)) {
		devc->buf_len = 0;
		return FALSE;
	}

	if (devc->limit_samples && devc->num_samples >= devc->limit_samples) {
		sr_info("Requested number of samples reached.");
		sdi->driver->dev_acquisition_stop(sdi, devc->session_cb_data);
		return TRUE;
	}

	if (devc->limit_msec) {
		time = (g_get_monotonic_time() - devc->start_time) / 1000;
		if (time > (int64_t)devc->limit_msec) {
			sr_info("Requested time limit reached.");
			sdi->driver->dev_acquisition_stop(sdi,
					devc->session_cb_data);
			return TRUE;
		}
	}

	return TRUE;
}
