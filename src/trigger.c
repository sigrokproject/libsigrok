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

#include <config.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/* * @cond PRIVATE */
#define LOG_PREFIX "trigger"
/* * @endcond */
   
/**
 * @file
 *
 * Creating, using, or destroying triggers.
 */

/**
 * @defgroup grp_trigger Trigger handling
 *
 * Creating, using, or destroying triggers.
 *
 * @{
 */

/**
 * Create a new trigger.
 *
 * The caller is responsible to free the trigger (including all stages and
 * matches) using sr_trigger_free() once it is no longer needed.
 *
 * @param name The trigger name to use. Can be NULL.
 *
 * @return A newly allocated trigger.
 *
 * @since 0.4.0
 */
SR_API struct sr_trigger *sr_trigger_new(const char *name)
{
	struct sr_trigger *trig;

	trig = g_malloc0(sizeof(struct sr_trigger));
	if (name)
		trig->name = g_strdup(name);

	return trig;
}

/**
 * Free a previously allocated trigger.
 *
 * This will also free any trigger stages/matches in this trigger.
 *
 * @param trig The trigger to free. Must not be NULL.
 *
 * @since 0.4.0
 */
SR_API void sr_trigger_free(struct sr_trigger *trig)
{
	struct sr_trigger_stage *stage;
	GSList *l;

	if (!trig)
		return;

	for (l = trig->stages; l; l = l->next) {
		stage = l->data;

		if (stage->matches)
			g_slist_free_full(stage->matches, g_free);
	}
	g_slist_free_full(trig->stages, g_free);

	g_free(trig->name);
	g_free(trig);
}

/**
 * Allocate a new trigger stage and add it to the specified trigger.
 *
 * The caller is responsible to free the trigger (including all stages and
 * matches) using sr_trigger_free() once it is no longer needed.
 *
 * @param trig The trigger to add a stage to. Must not be NULL.
 *
 * @retval NULL An invalid (NULL) trigger was passed into the function.
 * @retval other A newly allocated trigger stage (which has also been added
 * 		 to the list of stages of the specified trigger).
 *
 * @since 0.4.0
 */
SR_API struct sr_trigger_stage *sr_trigger_stage_add(struct sr_trigger *trig)
{
	struct sr_trigger_stage *stage;

	if (!trig)
		return NULL;

	stage = g_malloc0(sizeof(struct sr_trigger_stage));
	stage->stage = g_slist_length(trig->stages);
	trig->stages = g_slist_append(trig->stages, stage);

	return stage;
}

/**
 * Allocate a new trigger match and add it to the specified trigger stage.
 *
 * The caller is responsible to free the trigger (including all stages and
 * matches) using sr_trigger_free() once it is no longer needed.
 *
 * @param stage The trigger stage to add the match to. Must not be NULL.
 * @param ch The channel for this trigger match. Must not be NULL. Must be
 *           either of type SR_CHANNEL_LOGIC or SR_CHANNEL_ANALOG.
 * @param trigger_match The type of trigger match. Must be a valid trigger
 *                      type from enum sr_trigger_matches. The trigger type
 *                      must be valid for the respective channel type as well.
 * @param value Trigger value.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR_ARG Invalid argument(s) were passed to this functions.
 *
 * @since 0.4.0
 */
SR_API int sr_trigger_match_add(struct sr_trigger_stage *stage,
		struct sr_channel *ch, int trigger_match, float value)
{
	struct sr_trigger_match *match;

	if (!stage || !ch)
		return SR_ERR_ARG;

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
		if (trigger_match != SR_TRIGGER_RISING &&
				trigger_match != SR_TRIGGER_FALLING &&
				trigger_match != SR_TRIGGER_EDGE &&
				trigger_match != SR_TRIGGER_OVER &&
				trigger_match != SR_TRIGGER_UNDER) {
			sr_err("Invalid trigger match for an analog channel.");
			return SR_ERR_ARG;
		}
	} else {
		sr_err("Unsupported channel type: %d.", ch->type);
		return SR_ERR_ARG;
	}

	match = g_malloc0(sizeof(struct sr_trigger_match));
	match->channel = ch;
	match->match = trigger_match;
	match->value = value;
	stage->matches = g_slist_append(stage->matches, match);

	return SR_OK;
}

/** @} */
