/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
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

#include <libsigrok/libsigrok.h>

/**
 * @file
 *
 * Error handling in libsigrok.
 */

/**
 * @defgroup grp_error Error handling
 *
 * Error handling in libsigrok.
 *
 * libsigrok functions usually return @ref SR_OK upon success, or a negative
 * error code on failure.
 *
 * @{
 */

/**
 * Return a human-readable error string for the given libsigrok error code.
 *
 * @param error_code A libsigrok error code number, such as SR_ERR_MALLOC.
 *
 * @return A const string containing a short, human-readable (English)
 *         description of the error, such as "memory allocation error".
 *         The string must NOT be free'd by the caller!
 *
 * @see sr_strerror_name
 *
 * @since 0.2.0
 */
SR_API const char *sr_strerror(int error_code)
{
	/*
	 * Note: All defined SR_* error macros from libsigrok.h must have
	 * an entry in this function, as well as in sr_strerror_name().
	 */

	switch (error_code) {
	case SR_OK_CONTINUE:
		return "not enough data to decide error status yet";
	case SR_OK:
		return "no error";
	case SR_ERR:
		return "generic/unspecified error";
	case SR_ERR_MALLOC:
		return "memory allocation error";
	case SR_ERR_ARG:
		return "invalid argument";
	case SR_ERR_BUG:
		return "internal error";
	case SR_ERR_SAMPLERATE:
		return "invalid samplerate";
	case SR_ERR_NA:
		return "not applicable";
	case SR_ERR_DEV_CLOSED:
		return "device closed but should be open";
	case SR_ERR_TIMEOUT:
		return "timeout occurred";
	case SR_ERR_CHANNEL_GROUP:
		return "no channel group specified";
	case SR_ERR_DATA:
		return "data is invalid";
	case SR_ERR_IO:
		return "input/output error";
	default:
		return "unknown error";
	}
}

/**
 * Return the "name" string of the given libsigrok error code.
 *
 * For example, the "name" of the SR_ERR_MALLOC error code is "SR_ERR_MALLOC",
 * the name of the SR_OK code is "SR_OK", and so on.
 *
 * This function can be used for various purposes where the "name" string of
 * a libsigrok error code is useful.
 *
 * @param error_code A libsigrok error code number, such as SR_ERR_MALLOC.
 *
 * @return A const string containing the "name" of the error code as string.
 *         The string must NOT be free'd by the caller!
 *
 * @see sr_strerror
 *
 * @since 0.2.0
 */
SR_API const char *sr_strerror_name(int error_code)
{
	/*
	 * Note: All defined SR_* error macros from libsigrok.h must have
	 * an entry in this function, as well as in sr_strerror().
	 */

	switch (error_code) {
	case SR_OK_CONTINUE:
		return "SR_OK_CONTINUE";
	case SR_OK:
		return "SR_OK";
	case SR_ERR:
		return "SR_ERR";
	case SR_ERR_MALLOC:
		return "SR_ERR_MALLOC";
	case SR_ERR_ARG:
		return "SR_ERR_ARG";
	case SR_ERR_BUG:
		return "SR_ERR_BUG";
	case SR_ERR_SAMPLERATE:
		return "SR_ERR_SAMPLERATE";
	case SR_ERR_NA:
		return "SR_ERR_NA";
	case SR_ERR_DEV_CLOSED:
		return "SR_ERR_DEV_CLOSED";
	case SR_ERR_TIMEOUT:
		return "SR_ERR_TIMEOUT";
	case SR_ERR_CHANNEL_GROUP:
		return "SR_ERR_CHANNEL_GROUP";
	case SR_ERR_DATA:
		return "SR_ERR_DATA";
	case SR_ERR_IO:
		return "SR_ERR_IO";
	default:
		return "unknown error code";
	}
}

/** @} */
