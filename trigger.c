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

#include "libsigrok.h"
#include "libsigrok-internal.h"

/* * @cond PRIVATE */
#define LOG_PREFIX "trigger"
/* * @endcond */
   
SR_API struct sr_trigger *sr_trigger_new(char *name)
{
	struct sr_trigger *trig;

	trig = g_malloc0(sizeof(struct sr_trigger));
	if (name)
		trig->name = g_strdup(name);

	return trig;
}

SR_API void sr_trigger_free(struct sr_trigger *trig)
{
	struct sr_trigger_stage *stage;
	GSList *l;

	for (l = trig->stages; l; l = l->next) {
		stage = l->data;
		g_slist_free_full(stage->matches, g_free);
	}
	g_slist_free_full(trig->stages, g_free);

	g_free(trig->name);
	g_free(trig);
}

SR_API struct sr_trigger_stage *sr_trigger_stage_add(struct sr_trigger *trig)
{
	struct sr_trigger_stage *stage;

	stage = g_malloc0(sizeof(struct sr_trigger_stage));
	stage->stage = g_slist_length(trig->stages);
	trig->stages = g_slist_append(trig->stages, stage);

	return stage;
}

SR_API int sr_trigger_match_add(struct sr_trigger_stage *stage,
		struct sr_channel *ch, int trigger_match, float value)
{
	struct sr_trigger_match *match;

	if (ch->type == SR_CHANNEL_LOGIC) {
		if (trigger_match != SR_TRIGGER_ZERO &&
				trigger_match != SR_TRIGGER_ONE &&
				trigger_match != SR_TRIGGER_RISING &&
				trigger_match != SR_TRIGGER_FALLING &&
				trigger_match != SR_TRIGGER_EDGE) {
			sr_err("Invalid trigger match for a logic channel.");
			return SR_ERR_ARG;
		}


	} else if (ch->type == SR_CHANNEL_ANALOG) {
		if (trigger_match != SR_TRIGGER_FALLING &&
				trigger_match != SR_TRIGGER_OVER &&
				trigger_match != SR_TRIGGER_UNDER) {
			sr_err("Invalid trigger match for an analog channel.");
			return SR_ERR_ARG;
		}
	}

	match = g_malloc0(sizeof(struct sr_trigger_match));
	match->channel = ch;
	match->match = trigger_match;
	match->value = value;
	stage->matches = g_slist_append(stage->matches, match);

	return SR_OK;
}
