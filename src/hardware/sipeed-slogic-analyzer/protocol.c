/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2023-2025 Shenzhen Sipeed Technology Co., Ltd.
 * (深圳市矽速科技有限公司) <support@sipeed.com>
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
#include <stdint.h>

#include "protocol.h"

SR_PRIV void sipeed_slogic_log(void *user, int level, const char *msg)
{
	(void)user;
	switch (level) {
	case SLOGIC_LOG_ERR: sr_err("%s", msg); break;
	case SLOGIC_LOG_WARN: sr_warn("%s", msg); break;
	case SLOGIC_LOG_INFO: sr_info("%s", msg); break;
	case SLOGIC_LOG_DBG: sr_dbg("%s", msg); break;
	default: sr_spew("%s", msg); break;
	}
}

static int sink_data(void *user, const uint8_t *samples, size_t n,
		     unsigned unitsize)
{
	const struct sr_dev_inst *sdi = user;

	sr_session_send(sdi, &(struct sr_datafeed_packet){
		.type = SR_DF_LOGIC,
		.payload = &(struct sr_datafeed_logic){
			.length = n * unitsize,
			.unitsize = unitsize,
			.data = (void *)samples,
		} });
	return 0;
}

static void sink_trigger(void *user, uint64_t index)
{
	(void)index;
	std_session_send_df_trigger(user);
}

/*
 * Session source key for this device's poll timer. libsigrok treats a key >= 0
 * as a file descriptor (a HANDLE on Windows) to wait on, so the key must stay
 * negative: deriving it by negating a truncated pointer turned positive
 * whenever bit 31 of the address was set, and on Windows GLib then waited on
 * an invalid handle every 5 ms ("WaitForMultipleObjectsEx failed").
 */
static int source_key(const struct sr_dev_inst *sdi)
{
	return -1 - (int)(((uintptr_t)sdi >> 4) & 0x3fffffff);
}

static int handle_events(int fd, int revents, void *cb_data)
{
	struct sr_dev_inst *sdi = cb_data;
	struct dev_context *devc = sdi->priv;

	(void)fd;
	(void)revents;

	if (slogic_capture_poll(devc->cap, 16, 0) == SLOGIC_CAP_RUNNING)
		return TRUE;

	/* The core already logged its "slogic-report v1" line. */
	slogic_capture_destroy(devc->cap);
	devc->cap = NULL;
	std_session_send_df_end(sdi);
	sr_session_source_remove(sdi->session, source_key(sdi));
	return TRUE;
}

/* sr_trigger stage 0 -> core single-stage trigger. */
static int map_trigger(const struct sr_dev_inst *sdi, slogic_capture_config *c)
{
	struct dev_context *devc = sdi->priv;
	struct sr_trigger *trigger;
	struct sr_trigger_stage *stage;
	GSList *l;

	if (!(trigger = sr_session_trigger_get(sdi->session)) || !trigger->stages)
		return SR_OK;
	if (g_slist_length(trigger->stages) > 1) {
		sr_err("SLogic supports a single trigger stage.");
		return SR_ERR_NA;
	}
	stage = trigger->stages->data;
	for (l = stage->matches; l; l = l->next) {
		struct sr_trigger_match *m = l->data;
		uint8_t k;

		if (!m->match || !m->channel || !m->channel->enabled)
			continue;
		if (m->channel->index >= devc->cur_samplechannel) {
			sr_err("Trigger on %s, outside the %d-channel mode.",
			       m->channel->name, devc->cur_samplechannel);
			return SR_ERR_ARG;
		}
		switch (m->match) {
		case SR_TRIGGER_ZERO: k = SLOGIC_TRIG_ZERO; break;
		case SR_TRIGGER_ONE: k = SLOGIC_TRIG_ONE; break;
		case SR_TRIGGER_RISING: k = SLOGIC_TRIG_RISING; break;
		case SR_TRIGGER_FALLING: k = SLOGIC_TRIG_FALLING; break;
		case SR_TRIGGER_EDGE: k = SLOGIC_TRIG_EDGE; break;
		default: return SR_ERR_NA;
		}
		c->trigger.match[m->channel->index] = k;
	}
	if (!slogic_trigger_spec_empty(&c->trigger) && devc->cur_limit_samples)
		c->pretrigger_samples =
			devc->cur_limit_samples * devc->capture_ratio / 100;
	return SR_OK;
}

SR_PRIV int sipeed_slogic_acquisition_start(const struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;
	slogic_capture_config c;
	int ret;

	memset(&c, 0, sizeof(c));
	c.dev.channel_count = devc->cur_samplechannel;
	c.dev.samplerate_hz = devc->cur_samplerate;
	c.dev.threshold_v = (devc->voltage_threshold[0] +
			     devc->voltage_threshold[1]) / 2.0;
	c.dev.pattern_mode = (int)devc->cur_pattern_mode_idx;
	c.limit_samples = devc->cur_limit_samples; /* 0 = continuous */
	c.sink.user = (void *)sdi;
	c.sink.data = sink_data;
	c.sink.trigger = sink_trigger;
	if ((ret = map_trigger(sdi, &c)) != SR_OK)
		return ret;

	sr_info("Need %" PRIu64 "x %dch@%" PRIu64 "MHz.", devc->cur_limit_samples,
		devc->cur_samplechannel, devc->cur_samplerate / SR_MHZ(1));
	ret = slogic_capture_start(devc->sdev, &c, &devc->cap);
	if (ret != SLOGIC_OK) {
		sr_err("Failed to start the capture (%d).", ret);
		return ret == SLOGIC_ERR_ARG ? SR_ERR_ARG : SR_ERR_IO;
	}

	std_session_send_df_header(sdi);
	sr_session_source_add(sdi->session, source_key(sdi), 0, 5,
			      handle_events, (void *)sdi);
	return SR_OK;
}

SR_PRIV int sipeed_slogic_acquisition_stop(struct sr_dev_inst *sdi)
{
	struct dev_context *devc = sdi->priv;

	if (devc->cap)
		slogic_capture_stop(devc->cap);
	return SR_OK;
}
