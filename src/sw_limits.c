/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2016 Lars-Peter Clausen <lars@metafoo.de>
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

/**
 * @file
 * Software limits helper functions
 */

#include <config.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "sw_limits"

/**
 * Initialize a software limit instance
 *
 * Must be called before any other operations are performed on a struct
 * sr_sw_limits and should typically be called after the data structure has been
 * allocated.
 *
 * @param limits the software limit instance to initialize
 */
SR_PRIV void sr_sw_limits_init(struct sr_sw_limits *limits)
{
	memset(limits, 0, sizeof(*limits));
}

/**
 * Get software limit configuration
 *
 * Retrieve the currently configured software limit for the specified key.
 * Should be called from the drivers config_get() callback.
 *
 * @param limits software limit instance
 * @param key config item key
 * @param data config item data
 * @return SR_ERR_NA if @p key is not a supported limit, SR_OK otherwise
 */
SR_PRIV int sr_sw_limits_config_get(const struct sr_sw_limits *limits, uint32_t key,
	GVariant **data)
{
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		*data = g_variant_new_uint64(limits->limit_samples);
		break;
	case SR_CONF_LIMIT_FRAMES:
		*data = g_variant_new_uint64(limits->limit_frames);
		break;
	case SR_CONF_LIMIT_MSEC:
		*data = g_variant_new_uint64(limits->limit_msec / 1000);
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

/**
 * Set software limit configuration
 *
 * Configure software limit for the specified key. Should be called from the
 * drivers config_set() callback.
 *
 * @param limits software limit instance
 * @param key config item key
 * @param data config item data
 * @return SR_ERR_NA if @p key is not a supported limit, SR_OK otherwise
 */
SR_PRIV int sr_sw_limits_config_set(struct sr_sw_limits *limits, uint32_t key,
	GVariant *data)
{
	switch (key) {
	case SR_CONF_LIMIT_SAMPLES:
		limits->limit_samples = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_FRAMES:
		limits->limit_frames = g_variant_get_uint64(data);
		break;
	case SR_CONF_LIMIT_MSEC:
		limits->limit_msec = g_variant_get_uint64(data) * 1000;
		break;
	default:
		return SR_ERR_NA;
	}

	return SR_OK;
}

/**
 * Start a new data acquisition session
 *
 * Resets the internal accounting for all software limits. Usually should be
 * called from the drivers acquisition_start() callback.
 *
 * @param limits software limits instance
 */
SR_PRIV void sr_sw_limits_acquisition_start(struct sr_sw_limits *limits)
{
	limits->samples_read = 0;
	limits->frames_read = 0;
	limits->start_time = g_get_monotonic_time();
}

/**
 * Check if any of the configured software limits has been reached
 *
 * Usually should be called at the end of the drivers work function after all
 * processing has been done.
 *
 * @param limits software limits instance
 * @returns TRUE if any of the software limits has been reached and the driver
 *               should stop data acquisition, otherwise FALSE.
 */
SR_PRIV gboolean sr_sw_limits_check(struct sr_sw_limits *limits)
{
	if (limits->limit_samples) {
		if (limits->samples_read >= limits->limit_samples) {
			sr_dbg("Requested number of samples (%" PRIu64
			       ") reached.", limits->limit_samples);
			return TRUE;
		}
	}

	if (limits->limit_frames) {
		if (limits->frames_read >= limits->limit_frames) {
			sr_dbg("Requested number of frames (%" PRIu64
			       ") reached.", limits->limit_frames);
			return TRUE;
		}
	}

	if (limits->limit_msec && limits->start_time) {
		guint64 now;
		now = g_get_monotonic_time();
		if (now > limits->start_time &&
			now - limits->start_time > limits->limit_msec) {
			sr_dbg("Requested sampling time (%" PRIu64
			       "ms) reached.", limits->limit_msec / 1000);
			return TRUE;
		}
	}

	return FALSE;
}

/**
 * Get remaining counts until software limits are reached.
 *
 * This routine fills in those C language variables which callers
 * requested, and provides the remaining value until a specified limit
 * would be reached.
 *
 * The @ref sr_sw_limits_config_get() routine is suitable for rare
 * configuration calls and interfaces nicely with Glib data types. The
 * @ref sr_sw_limits_check() routine only provides a weak "exceeded"
 * result. This @ref sr_sw_limits_get_remain() routine is suitable for
 * additional checks and more eager limits enforcement in (potentially
 * tight) acquisition code paths. Hardware compression may result in
 * rather large "overshoots" when checks are done only late.
 *
 * @param[in] limits software limit instance
 * @param[out] samples remaining samples count until the limit is reached
 * @param[out] frames remaining frames count until the limit is reached
 * @param[out] msecs remaining milliseconds until the limit is reached
 *
 * @return SR_ERR_* upon error, SR_OK otherwise
 */
SR_PRIV int sr_sw_limits_get_remain(const struct sr_sw_limits *limits,
	uint64_t *samples, uint64_t *frames, uint64_t *msecs,
	gboolean *exceeded)
{

	if (!limits)
		return SR_ERR_ARG;

	if (exceeded)
		*exceeded = FALSE;

	if (samples) do {
		*samples = 0;
		if (!limits->limit_samples)
			break;
		if (limits->samples_read >= limits->limit_samples) {
			if (exceeded)
				*exceeded = TRUE;
			break;
		}
		*samples = limits->limit_samples - limits->samples_read;
	} while (0);

	if (frames) do {
		*frames = 0;
		if (!limits->limit_frames)
			break;
		if (limits->frames_read >= limits->limit_frames) {
			if (exceeded)
				*exceeded = TRUE;
			break;
		}
		*frames = limits->limit_frames - limits->frames_read;
	} while (0);

	if (msecs) do {
		guint64 now, elapsed, remain;

		*msecs = 0;
		if (!limits->limit_msec)
			break;
		if (!limits->start_time)
			break;
		now = g_get_monotonic_time();
		if (now < limits->start_time)
			break;
		elapsed = now - limits->start_time;
		if (elapsed >= limits->limit_msec) {
			if (exceeded)
				*exceeded = TRUE;
			break;
		}
		remain = limits->limit_msec - elapsed;
		*msecs = remain / 1000;
	} while (0);

	return SR_OK;
}

/**
 * Update the amount of samples that have been read
 *
 * Update the amount of samples that have been read in the current data
 * acquisition run. For each invocation @p samples_read will be accumulated and
 * once the configured sample limit has been reached sr_sw_limits_check() will
 * return TRUE.
 *
 * @param limits software limits instance
 * @param samples_read the amount of samples that have been read
 */
SR_PRIV void sr_sw_limits_update_samples_read(struct sr_sw_limits *limits,
	uint64_t samples_read)
{
	limits->samples_read += samples_read;
}

/**
 * Update the amount of frames that have been read
 *
 * Update the amount of frames that have been read in the current data
 * acquisition run. For each invocation @p frames_read will be accumulated and
 * once the configured frame limit has been reached sr_sw_limits_check() will
 * return TRUE.
 *
 * @param limits software limits instance
 * @param frames_read the amount of frames that have been read
 */
SR_PRIV void sr_sw_limits_update_frames_read(struct sr_sw_limits *limits,
	uint64_t frames_read)
{
	limits->frames_read += frames_read;
}
