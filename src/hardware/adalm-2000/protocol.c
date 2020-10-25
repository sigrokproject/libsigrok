/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Gwenhael Goavec-Merou <gwenhael.goavec-merou@trabucayre.com>
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
#include "m2k_wrapper.h"

#define SAMPLEUNIT (sizeof(unsigned short))

/**
 * configure trigger. In all case disable trigger, if needed reconfigure them
 * @return FALSE if not allowed configured, TRUE otherwise
 */
SR_PRIV int adalm_2000_convert_trigger(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc;
	struct sr_trigger *trigger;
	struct sr_trigger_match *match;
	struct sr_trigger_stage *stage;
	const GSList *l, *m;

	devc = sdi->priv;

	/* security: disable trigger */
	if (m2k_disable_trigg(devc->m2k) < 0) {
		sr_err("fail to disable trigger\n");
		return FALSE;
	}

	trigger = sr_session_trigger_get(sdi->session);
	if (!trigger)
		return TRUE;

	/* currently one stage trigger supported */
	if (g_slist_length(trigger->stages) > 1) {
		sr_err("This device only supports 1 trigger stages.");
		return FALSE;
	}

	for (l = trigger->stages; l; l = l->next) {
		stage = l->data;
		/* single trigger only */
		if (g_slist_length(stage->matches) > 1) {
			sr_err("Error only one channel supported for trigger\n");
			return FALSE;
		}

		for (m = stage->matches; m; m = m->next) {
			match = m->data;
			switch (match->match) {
			case SR_TRIGGER_ZERO:
				devc->triggerflags = LOW_LEVEL_DIGITAL;
				break;
			case SR_TRIGGER_ONE:
				devc->triggerflags = HIGH_LEVEL_DIGITAL;
				break;
			case SR_TRIGGER_RISING:
				devc->triggerflags = RISING_EDGE_DIGITAL;
				break;
			case SR_TRIGGER_FALLING:
				devc->triggerflags = FALLING_EDGE_DIGITAL;
				break;
			case SR_TRIGGER_EDGE:
				devc->triggerflags = ANY_EDGE_DIGITAL;
				break;
			default:
				devc->triggerflags = NO_TRIGGER_DIGITAL;
			}
			if (m2k_configure_trigg(devc->m2k,
						match->channel->index,
						devc->triggerflags) < 0) {
				sr_err("fail to configure trigger source\n");
				return FALSE;
			}
		}
	}

	return TRUE;
}

/**
 * do acquisition and push received data
 * @return FALSE if something wrong, TRUE otherwise
 */
SR_PRIV int adalm_2000_receive_data(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi;
	struct dev_context *devc;
	struct sr_datafeed_logic logic;
	struct sr_datafeed_packet packet;
	int bytes_read;
	int nb_samples;

	(void)fd;

	sdi = cb_data;
	if (!sdi)
		return TRUE;
	devc = sdi->priv;
	if (!devc)
		return TRUE;
	if (!(revents == G_IO_IN || revents == 0))
		return TRUE;
	if (!devc->m2k)
		return TRUE;

	nb_samples = devc->limit_samples;

	bytes_read = m2k_get_sample(devc->m2k, devc->sample_buf, nb_samples);
	if (bytes_read < 0) {
		sr_err("Fail to fetch samples\n");
		return FALSE;
	}

	packet.type = SR_DF_LOGIC;
	packet.payload = &logic;
	logic.length = SAMPLEUNIT * bytes_read;
	logic.unitsize = SAMPLEUNIT;
	logic.data = (unsigned char *)devc->sample_buf;

	sr_session_send(sdi, &packet);
	sr_dev_acquisition_stop(sdi);

	return TRUE;
}
