/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 Bert Vermeulen <bert@biot.com>
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
#include "libsigrok.h"
#include "libsigrok-internal.h"

/* @cond PRIVATE */
#define LOG_PREFIX "soft-trigger"
/* @endcond */

SR_PRIV struct soft_trigger_logic *soft_trigger_logic_new(
		const struct sr_dev_inst *sdi, struct sr_trigger *trigger,
		int pre_trigger_samples)
{
	struct soft_trigger_logic *stl;

	stl = g_malloc0(sizeof(struct soft_trigger_logic));
	stl->sdi = sdi;
	stl->trigger = trigger;
	stl->unitsize = (g_slist_length(sdi->channels) + 7) / 8;
	stl->prev_sample = g_malloc0(stl->unitsize);
	stl->pre_trigger_size = stl->unitsize * pre_trigger_samples;
	stl->pre_trigger_buffer = g_malloc(stl->pre_trigger_size);
	stl->pre_trigger_head = stl->pre_trigger_buffer;

	if (stl->pre_trigger_size > 0 && !stl->pre_trigger_buffer) {
		soft_trigger_logic_free(stl);
		return NULL;
	}

	return stl;
}

SR_PRIV void soft_trigger_logic_free(struct soft_trigger_logic *stl)
{
	g_free(stl->pre_trigger_buffer);
	g_free(stl->prev_sample);
	g_free(stl);
}

static void pre_trigger_append(struct soft_trigger_logic *stl,
		uint8_t *buf, int len)
{
	/* Avoid uselessly copying more than the pre-trigger size. */
	if (len > stl->pre_trigger_size) {
		buf += len - stl->pre_trigger_size;
		len = stl->pre_trigger_size;
	}

	/* Update the filling level of the pre-trigger circular buffer. */
	stl->pre_trigger_fill = MIN(stl->pre_trigger_fill + len,
	                            stl->pre_trigger_size);

	/* Actually copy data to the pre-trigger circular buffer. */
	while (len > 0) {
		size_t size = MIN(stl->pre_trigger_buffer + stl->pre_trigger_size
		                  - stl->pre_trigger_head, len);
		memcpy(stl->pre_trigger_head, buf, size);
		stl->pre_trigger_head += size;
		if (stl->pre_trigger_head >= stl->pre_trigger_buffer
		                             + stl->pre_trigger_size)
			stl->pre_trigger_head = stl->pre_trigger_buffer;
		buf += size;
		len -= size;
	}
}

static void pre_trigger_send(struct soft_trigger_logic *stl,
		int *pre_trigger_samples)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.unitsize = stl->unitsize;

	if (pre_trigger_samples)
		*pre_trigger_samples = 0;

	/* If pre-trigger buffer not full, rewind head to the first valid sample. */
	if (stl->pre_trigger_fill < stl->pre_trigger_size)
		stl->pre_trigger_head = stl->pre_trigger_buffer;

	/* Send logic packets for the pre-trigger circular buffer content. */
	while (stl->pre_trigger_fill > 0) {
		size_t size = MIN(stl->pre_trigger_buffer + stl->pre_trigger_size
		                  - stl->pre_trigger_head, stl->pre_trigger_fill);
		logic.length = size;
		logic.data = stl->pre_trigger_head;
		sr_session_send(stl->sdi, &packet);
		stl->pre_trigger_head = stl->pre_trigger_buffer;
		stl->pre_trigger_fill -= size;
		if (pre_trigger_samples)
			*pre_trigger_samples += size / stl->unitsize;
	}
}

static gboolean logic_check_match(struct soft_trigger_logic *stl,
		uint8_t *sample, struct sr_trigger_match *match)
{
	int bit, prev_bit;
	gboolean result;

	stl->count++;
	result = FALSE;
	bit = *(sample + match->channel->index / 8)
			& (1 << (match->channel->index % 8));
	if (match->match == SR_TRIGGER_ZERO)
		result = bit == 0;
	else if (match->match == SR_TRIGGER_ONE)
		result = bit != 0;
	else {
		/* Edge matches. */
		if (stl->count == 1)
			/* First sample, don't have enough for an edge match yet. */
			return FALSE;
		prev_bit = *(stl->prev_sample + match->channel->index / 8)
				& (1 << (match->channel->index % 8));
		if (match->match == SR_TRIGGER_RISING)
			result = prev_bit == 0 && bit != 0;
		else if (match->match == SR_TRIGGER_FALLING)
			result = prev_bit != 0 && bit == 0;
		else if (match->match == SR_TRIGGER_EDGE)
			result = prev_bit != bit;
	}

	return result;
}

/* Returns the offset (in samples) within buf of where the trigger
 * occurred, or -1 if not triggered. */
SR_PRIV int soft_trigger_logic_check(struct soft_trigger_logic *stl,
		uint8_t *buf, int len, int *pre_trigger_samples)
{
	struct sr_datafeed_packet packet;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	GSList *l, *l_stage;
	int offset;
	int i;
	gboolean match_found;

	offset = -1;
	for (i = 0; i < len; i += stl->unitsize) {
		l_stage = g_slist_nth(stl->trigger->stages, stl->cur_stage);
		stage = l_stage->data;
		if (!stage->matches)
			/* No matches supplied, client error. */
			return SR_ERR_ARG;

		match_found = TRUE;
		for (l = stage->matches; l; l = l->next) {
			match = l->data;
			if (!match->channel->enabled)
				/* Ignore disabled channels with a trigger. */
				continue;
			if (!logic_check_match(stl, buf + i, match)) {
				match_found = FALSE;
				break;
			}
		}
		memcpy(stl->prev_sample, buf + i, stl->unitsize);
		if (match_found) {
			/* Matched on the current stage. */
			if (l_stage->next) {
				/* Advance to next stage. */
				stl->cur_stage++;
			} else {
				/* Matched on last stage, send pre-trigger data. */
				pre_trigger_append(stl, buf, i);
				pre_trigger_send(stl, pre_trigger_samples);

				/* Fire trigger. */
				offset = i / stl->unitsize;

				packet.type = SR_DF_TRIGGER;
				packet.payload = NULL;
				sr_session_send(stl->sdi, &packet);
				break;
			}
		} else if (stl->cur_stage > 0) {
			/*
			 * We had a match at an earlier stage, but failed on the
			 * current stage. However, we may have a match on this
			 * stage in the next bit -- trigger on 0001 will fail on
			 * seeing 00001, so we need to go back to stage 0 -- but
			 * at the next sample from the one that matched originally,
			 * which the counter increment at the end of the loop
			 * takes care of.
			 */
			i -= stl->cur_stage * stl->unitsize;
			if (i < -1)
				i = -1; /* Oops, went back past this buffer. */
			/* Reset trigger stage. */
			stl->cur_stage = 0;
		}
	}

	if (offset == -1)
		pre_trigger_append(stl, buf, len);

	return offset;
}
