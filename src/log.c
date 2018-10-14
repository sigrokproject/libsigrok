/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2011-2012 Uwe Hermann <uwe@hermann-uwe.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdarg.h>
#include <stdio.h>
#include <glib/gprintf.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/** @cond PRIVATE */
#define LOG_PREFIX "log"
/** @endcond */

/**
 * @file
 *
 * Controlling the libsigrok message logging functionality.
 */

/**
 * @defgroup grp_logging Logging
 *
 * Controlling the libsigrok message logging functionality.
 *
 * @{
 */

/* Currently selected libsigrok loglevel. Default: SR_LOG_WARN. */
static int cur_loglevel = SR_LOG_WARN; /* Show errors+warnings per default. */

/* Function prototype. */
static int sr_logv(void *cb_data, int loglevel, const char *format,
		   va_list args);

/* Pointer to the currently selected log callback. Default: sr_logv(). */
static sr_log_callback sr_log_cb = sr_logv;

/*
 * Pointer to private data that can be passed to the log callback.
 * This can be used (for example) by C++ GUIs to pass a "this" pointer.
 */
static void *sr_log_cb_data = NULL;

/** @cond PRIVATE */
#define LOGLEVEL_TIMESTAMP SR_LOG_DBG
/** @endcond */
static int64_t sr_log_start_time = 0;

/**
 * Set the libsigrok loglevel.
 *
 * This influences the amount of log messages (debug messages, error messages,
 * and so on) libsigrok will output. Using SR_LOG_NONE disables all messages.
 *
 * Note that this function itself will also output log messages. After the
 * loglevel has changed, it will output a debug message with SR_LOG_DBG for
 * example. Whether this message is shown depends on the (new) loglevel.
 *
 * @param loglevel The loglevel to set (SR_LOG_NONE, SR_LOG_ERR, SR_LOG_WARN,
 *                 SR_LOG_INFO, SR_LOG_DBG, or SR_LOG_SPEW).
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid loglevel.
 *
 * @since 0.1.0
 */
SR_API int sr_log_loglevel_set(int loglevel)
{
	if (loglevel < SR_LOG_NONE || loglevel > SR_LOG_SPEW) {
		sr_err("Invalid loglevel %d.", loglevel);
		return SR_ERR_ARG;
	}
	/* Output time stamps relative to time at startup */
	if (loglevel >= LOGLEVEL_TIMESTAMP && sr_log_start_time == 0)
		sr_log_start_time = g_get_monotonic_time();

	cur_loglevel = loglevel;

	sr_dbg("libsigrok loglevel set to %d.", loglevel);

	return SR_OK;
}

/**
 * Get the libsigrok loglevel.
 *
 * @return The currently configured libsigrok loglevel.
 *
 * @since 0.1.0
 */
SR_API int sr_log_loglevel_get(void)
{
	return cur_loglevel;
}

/**
 * Set the libsigrok log callback to the specified function.
 *
 * @param cb Function pointer to the log callback function to use.
 *           Must not be NULL.
 * @param cb_data Pointer to private data to be passed on. This can be used by
 *                the caller to pass arbitrary data to the log functions. This
 *                pointer is only stored or passed on by libsigrok, and is
 *                never used or interpreted in any way. The pointer is allowed
 *                to be NULL if the caller doesn't need/want to pass any data.
 *
 * @return SR_OK upon success, SR_ERR_ARG upon invalid arguments.
 *
 * @since 0.3.0
 */
SR_API int sr_log_callback_set(sr_log_callback cb, void *cb_data)
{
	if (!cb) {
		sr_err("%s: cb was NULL", __func__);
		return SR_ERR_ARG;
	}

	/* Note: 'cb_data' is allowed to be NULL. */

	sr_log_cb = cb;
	sr_log_cb_data = cb_data;

	return SR_OK;
}

/**
 * Set the libsigrok log callback to the default built-in one.
 *
 * Additionally, the internal 'sr_log_cb_data' pointer is set to NULL.
 *
 * @return SR_OK upon success, a negative error code otherwise.
 *
 * @since 0.1.0
 */
SR_API int sr_log_callback_set_default(void)
{
	/*
	 * Note: No log output in this function, as it should safely work
	 * even if the currently set log callback is buggy/broken.
	 */
	sr_log_cb = sr_logv;
	sr_log_cb_data = NULL;

	return SR_OK;
}

/**
 * Get the libsigrok log callback routine and callback data.
 *
 * @param[out] cb Pointer to a function pointer to receive the log callback
 * 	function. Optional, can be NULL.
 * @param[out] cb_data Pointer to a void pointer to receive the log callback's
 * 	additional arguments. Optional, can be NULL.
 *
 * @return SR_OK upon success.
 *
 * @since 0.5.1
 */
SR_API int sr_log_callback_get(sr_log_callback *cb, void **cb_data)
{
	if (cb)
		*cb = sr_log_cb;
	if (cb_data)
		*cb_data = sr_log_cb_data;

	return SR_OK;
}

static int sr_logv(void *cb_data, int loglevel, const char *format, va_list args)
{
	uint64_t elapsed_us, minutes;
	unsigned int rest_us, seconds, microseconds;
	char *raw_output, *output;
	int raw_len, raw_idx, idx, ret;

	/* This specific log callback doesn't need the void pointer data. */
	(void)cb_data;

	(void)loglevel;

	if (cur_loglevel >= LOGLEVEL_TIMESTAMP) {
		elapsed_us = g_get_monotonic_time() - sr_log_start_time;

		minutes = elapsed_us / G_TIME_SPAN_MINUTE;
		rest_us = elapsed_us % G_TIME_SPAN_MINUTE;
		seconds = rest_us / G_TIME_SPAN_SECOND;
		microseconds = rest_us % G_TIME_SPAN_SECOND;

		ret = g_fprintf(stderr, "sr: [%.2" PRIu64 ":%.2u.%.6u] ",
				minutes, seconds, microseconds);
	} else {
		ret = fputs("sr: ", stderr);
	}

	if (ret < 0 || (raw_len = g_vasprintf(&raw_output, format, args)) < 0)
		return SR_ERR;

	output = g_malloc0(raw_len + 1);

	/* Copy the string without any unwanted newlines. */
	raw_idx = idx = 0;
	while (raw_idx < raw_len) {
		if (raw_output[raw_idx] != '\n') {
			output[idx] = raw_output[raw_idx];
			idx++;
		}
		raw_idx++;
	}

	g_fprintf(stderr, "%s\n", output);
	fflush(stderr);
	g_free(raw_output);
	g_free(output);

	return SR_OK;
}

/** @private */
SR_PRIV int sr_log(int loglevel, const char *format, ...)
{
	int ret;
	va_list args;

	/* Only output messages of at least the selected loglevel(s). */
	if (loglevel > cur_loglevel)
		return SR_OK;

	va_start(args, format);
	ret = sr_log_cb(sr_log_cb_data, loglevel, format, args);
	va_end(args);

	return ret;
}

/** @} */
