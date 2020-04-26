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

SR_PRIV int adalm2000_receive_data(int fd, int revents, void *cb_data)
{
	const struct sr_dev_inst *sdi;
	struct dev_context *devc;

	(void)fd;

	if (!(sdi = cb_data))
		return TRUE;

	if (!(devc = sdi->priv))
		return TRUE;

	if (revents == G_IO_IN) {
		/* TODO */
	}

	return TRUE;
}
