/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Gerhard Sittig <gerhard.sittig@gmx.net>
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

#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include <string.h>

struct feed_queue_logic {
	struct sr_dev_inst *sdi;
	size_t unit_size;
	size_t alloc_count;
	size_t fill_count;
	uint8_t *data_bytes;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
};

SR_API struct feed_queue_logic *feed_queue_logic_alloc(struct sr_dev_inst *sdi,
	size_t sample_count, size_t unit_size)
{
	struct feed_queue_logic *q;

	q = g_malloc0(sizeof(*q));
	q->sdi = sdi;
	q->unit_size = unit_size;
	q->alloc_count = sample_count;
	q->data_bytes = g_try_malloc(q->alloc_count * q->unit_size);
	if (!q->data_bytes) {
		g_free(q);
		return NULL;
	}

	memset(&q->packet, 0, sizeof(q->packet));
	memset(&q->logic, 0, sizeof(q->logic));
	q->packet.type = SR_DF_LOGIC;
	q->packet.payload = &q->logic;
	q->logic.unitsize = q->unit_size;
	q->logic.data = q->data_bytes;

	return q;
}

SR_API int feed_queue_logic_submit(struct feed_queue_logic *q,
	const uint8_t *data, size_t count)
{
	uint8_t *wrptr;
	int ret;

	wrptr = &q->data_bytes[q->fill_count * q->unit_size];
	while (count--) {
		memcpy(wrptr, data, q->unit_size);
		wrptr += q->unit_size;
		q->fill_count++;
		if (q->fill_count == q->alloc_count) {
			ret = feed_queue_logic_flush(q);
			if (ret != SR_OK)
				return ret;
			wrptr = &q->data_bytes[0];
		}
	}

	return SR_OK;
}

SR_API int feed_queue_logic_flush(struct feed_queue_logic *q)
{
	int ret;

	if (!q->fill_count)
		return SR_OK;

	q->logic.length = q->fill_count * q->unit_size;
	ret = sr_session_send(q->sdi, &q->packet);
	if (ret != SR_OK)
		return ret;
	q->fill_count = 0;

	return SR_OK;
}

SR_API void feed_queue_logic_free(struct feed_queue_logic *q)
{

	if (!q)
		return;

	g_free(q->data_bytes);
	g_free(q);
}

struct feed_queue_analog {
	struct sr_dev_inst *sdi;
	size_t alloc_count;
	size_t fill_count;
	float *data_values;
	int digits;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;
	struct sr_analog_encoding encoding;
	struct sr_analog_meaning meaning;
	struct sr_analog_spec spec;
	GSList *channels;
};

SR_API struct feed_queue_analog *feed_queue_analog_alloc(struct sr_dev_inst *sdi,
	size_t sample_count, int digits, struct sr_channel *ch)
{
	struct feed_queue_analog *q;

	q = g_malloc0(sizeof(*q));
	q->sdi = sdi;
	q->alloc_count = sample_count;
	q->data_values = g_try_malloc(q->alloc_count * sizeof(float));
	if (!q->data_values) {
		g_free(q);
		return NULL;
	}
	q->digits = digits;
	q->channels = g_slist_append(NULL, ch);

	memset(&q->packet, 0, sizeof(q->packet));
	sr_analog_init(&q->analog, &q->encoding, &q->meaning, &q->spec, digits);
	q->packet.type = SR_DF_ANALOG;
	q->packet.payload = &q->analog;
	q->encoding.is_signed = TRUE;
	q->meaning.channels = q->channels;
	q->analog.data = q->data_values;

	return q;
}

SR_API int feed_queue_analog_submit(struct feed_queue_analog *q,
	float data, size_t count)
{
	int ret;

	while (count--) {
		q->data_values[q->fill_count++] = data;
		if (q->fill_count == q->alloc_count) {
			ret = feed_queue_analog_flush(q);
			if (ret != SR_OK)
				return ret;
		}
	}

	return SR_OK;
}

SR_API int feed_queue_analog_flush(struct feed_queue_analog *q)
{
	int ret;

	if (!q->fill_count)
		return SR_OK;

	q->analog.num_samples = q->fill_count;
	ret = sr_session_send(q->sdi, &q->packet);
	if (ret != SR_OK)
		return ret;
	q->fill_count = 0;

	return SR_OK;
}

SR_API void feed_queue_analog_free(struct feed_queue_analog *q)
{

	if (!q)
		return;

	g_free(q->data_values);
	g_slist_free(q->channels);
	g_free(q);
}
