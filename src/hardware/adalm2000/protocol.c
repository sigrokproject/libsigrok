/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Analog Devices Inc.
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
#include "protocol.h"

SR_PRIV int adalm2000_nb_enabled_channels(const struct sr_dev_inst *sdi, int type)
{
	struct sr_channel *ch;
	int nb_channels;
	GSList *l;

	nb_channels = 0;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type == type) {
			if (ch->enabled) {
				nb_channels++;
			}
		}
	}
	return nb_channels;
}

SR_PRIV int adalm2000_convert_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	struct sr_trigger_match *match;
	struct sr_channel *ch;
	const GSList *l, *m;

	devc = sdi->priv;

	for (l = sdi->channels; l; l = l->next) {
		ch = l->data;
		if (ch->type == SR_CHANNEL_LOGIC) {
			if (ch->enabled) {
				sr_libm2k_digital_trigger_condition_set(devc->m2k, ch->index,
									SR_NO_TRIGGER);
			}
		}
	}

	if (!(trigger = sr_session_trigger_get(sdi->session))) {
		return SR_OK;
	}

	sr_libm2k_digital_streaming_flag_set(devc->m2k, 0);
	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			if (!match->channel->enabled) {
				/* Ignore disabled channels with a trigger. */
				continue;
			}
			sr_libm2k_digital_trigger_condition_set(devc->m2k, match->channel->index,
								match->match);
		}
	}

	return SR_OK;
}

static void send_analog_packet(struct sr_dev_inst *sdi, float *data, int index, uint64_t sending_now)
{
	struct dev_context *devc;
	struct sr_channel *ch;
	struct sr_datafeed_packet packet;

	if (!(devc = sdi->priv)) {
		return;
	}

	ch = g_slist_nth_data(sdi->channels, DEFAULT_NUM_LOGIC_CHANNELS + index);
	devc->meaning.channels = g_slist_append(NULL, ch);

	devc->packet.data = data;
	devc->packet.num_samples = sending_now;

	packet.payload = &devc->packet;
	packet.type = SR_DF_ANALOG;

	sr_session_send(sdi, &packet);
}

SR_PRIV int adalm2000_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_channel *ch;
	struct sr_datafeed_packet packet;
	struct sr_datafeed_logic logic;
	uint64_t samples_todo, logic_done, analog_done, sending_now, analog_sent;
	int64_t elapsed_us, limit_us, todo_us;
	uint32_t *logic_data;
	float **analog_data;
	GSList *l;

	(void) fd;
	(void) revents;

	if (!(sdi = cb_data)) {
		return TRUE;
	}

	if (!(devc = sdi->priv)) {
		return TRUE;
	}

	elapsed_us = g_get_monotonic_time() - devc->start_time;
	limit_us = 1000 * devc->limit_msec;

	if (limit_us > 0 && limit_us < elapsed_us) {
		todo_us = MAX(0, limit_us - devc->spent_us);
	} else {
		todo_us = MAX(0, elapsed_us - devc->spent_us);
	}

	samples_todo = (todo_us * sr_libm2k_digital_samplerate_get(devc->m2k) + G_USEC_PER_SEC - 1)
		       / G_USEC_PER_SEC;

	if (devc->limit_samples > 0) {
		if (devc->limit_samples < devc->sent_samples) {
			samples_todo = 0;
		} else if (devc->limit_samples - devc->sent_samples < samples_todo) {
			samples_todo = devc->limit_samples - devc->sent_samples;
		}
	}

	if (samples_todo == 0) {
		return G_SOURCE_CONTINUE;
	}

	todo_us = samples_todo * G_USEC_PER_SEC / sr_libm2k_digital_samplerate_get(devc->m2k);

	logic_done = 0;
	analog_done = 0;

	while (logic_done < samples_todo || analog_done < samples_todo) {
		if (analog_done < samples_todo) {
			analog_sent = MIN(samples_todo - analog_done, devc->buffersize);

			analog_data = sr_libm2k_analog_samples_get(devc->m2k, devc->buffersize);
			for (l = sdi->channels; l; l = l->next) {
				ch = l->data;
				if (ch->type == SR_CHANNEL_ANALOG) {
					if (ch->enabled) {
						send_analog_packet(sdi, analog_data[ch->index],
								   ch->index, analog_sent);
					}
				}
			}
			analog_done += analog_sent;
		}
		if (logic_done < samples_todo) {
			logic_data = sr_libm2k_digital_samples_get(devc->m2k, devc->buffersize);

			packet.type = SR_DF_LOGIC;
			packet.payload = &logic;
			logic.unitsize = devc->logic_unitsize;

			sending_now = MIN(samples_todo - logic_done, devc->buffersize);

			logic.length = sending_now * devc->logic_unitsize;
			logic.data = logic_data;
			sr_session_send(sdi, &packet);
			logic_done += sending_now;
		}

	}

	devc->sent_samples += logic_done;
	devc->spent_us += todo_us;
	if ((devc->limit_samples > 0 && devc->sent_samples >= devc->limit_samples)
	    || (limit_us > 0 && devc->spent_us >= limit_us)) {
		sr_dev_acquisition_stop(sdi);
	}

	return G_SOURCE_CONTINUE;
}
