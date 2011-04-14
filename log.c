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
#include <sigrok.h>
#include <sigrok-internal.h>

static int sr_logv(int loglevel, const char *format, va_list args)
{
	int ret;

	/* Avoid compiler warnings. */
	loglevel = loglevel;

	ret = vfprintf(stderr, format, args);
	fprintf(stderr, "\n");

	return ret;
}

int sr_log(int loglevel, const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = sr_logv(loglevel, format, args);
	va_end(args);

	return ret;
}

int sr_dbg(const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = sr_logv(SR_LOG_DBG, format, args);
	va_end(args);

	return ret;
}

int sr_info(const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = sr_logv(SR_LOG_INFO, format, args);
	va_end(args);

	return ret;
}

int sr_warn(const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = sr_logv(SR_LOG_WARN, format, args);
	va_end(args);

	return ret;
}

int sr_err(const char *format, ...)
{
	int ret;
	va_list args;

	va_start(args, format);
	ret = sr_logv(SR_LOG_ERR, format, args);
	va_end(args);

	return ret;
}
