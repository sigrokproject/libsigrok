/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2011 Uwe Hermann <uwe@hermann-uwe.de>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdarg.h>
#include <stdio.h>
#include "sigrok.h"
#include "sigrok-internal.h"

static int sr_loglevel = SR_LOG_WARN; /* Show errors+warnings per default. */

/**
 * Set the libsigrok loglevel.
 *
 * This influences the amount of log messages (debug messages, error messages,
 * and so on) libsigrok will output. Using SR_LOG_NONE disables all messages.
 *
 * @param loglevel The loglevel to set (SR_LOG_NONE, SR_LOG_ERR, SR_LOG_WARN,
 *                 SR_LOG_INFO, SR_LOG_DBG, or SR_LOG_SPEW).
 * @return SR_OK upon success, SR_ERR_ARG upon invalid loglevel.
 */
SR_API int sr_set_loglevel(int loglevel)
{
	if (loglevel < SR_LOG_NONE || loglevel > SR_LOG_SPEW) {
		sr_err("log: %s: invalid loglevel %d", __func__, loglevel);
		return SR_ERR_ARG;
	}

	sr_loglevel = loglevel;

	sr_dbg("log: %s: libsigrok loglevel set to %d", __func__, loglevel);

	return SR_OK;
}

/**
 * Get the libsigrok loglevel.
 *
 * @return The currently configured libsigrok loglevel.
 */
SR_API int sr_get_loglevel(void)
{
	return sr_loglevel;
}

static int sr_logv(int loglevel, const char *format, va_list args)
{
	int ret;

	/* Only output messages of at least the selected loglevel(s). */
	if (loglevel > sr_loglevel)
		return SR_OK; /* TODO? */

	ret = vfprintf(stderr, format, args);
	fprintf(stderr, "\n");

	return ret;
}

SR_PRIV int sr_log(int loglevel, const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = sr_logv(loglevel, format, args);
	va_end(args);

	return ret;
}

SR_PRIV int sr_spew(const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = sr_logv(SR_LOG_SPEW, format, args);
	va_end(args);

	return ret;
}

SR_PRIV int sr_dbg(const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = sr_logv(SR_LOG_DBG, format, args);
	va_end(args);

	return ret;
}

SR_PRIV int sr_info(const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = sr_logv(SR_LOG_INFO, format, args);
	va_end(args);

	return ret;
}

SR_PRIV int sr_warn(const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = sr_logv(SR_LOG_WARN, format, args);
	va_end(args);

	return ret;
}

SR_PRIV int sr_err(const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = sr_logv(SR_LOG_ERR, format, args);
	va_end(args);

	return ret;
}
