/*
 * This file is part of the sigrok project.
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

#include "libsigrok.h"

/**
 * @file
 *
 * Version number querying functions.
 */

/**
 * @defgroup grp_versions Versions
 *
 * Version number querying functions.
 *
 * @{
 */

SR_API int sr_package_version_major_get(void)
{
	return SR_PACKAGE_VERSION_MAJOR;
}

SR_API int sr_package_version_minor_get(void)
{
	return SR_PACKAGE_VERSION_MINOR;
}

SR_API int sr_package_version_micro_get(void)
{
	return SR_PACKAGE_VERSION_MICRO;
}

SR_API const char *sr_package_version_string_get(void)
{
	return SR_PACKAGE_VERSION_STRING;
}

SR_API int sr_lib_version_current_get(void)
{
	return SR_LIB_VERSION_CURRENT;
}

SR_API int sr_lib_version_revision_get(void)
{
	return SR_LIB_VERSION_REVISION;
}

SR_API int sr_lib_version_age_get(void)
{
	return SR_LIB_VERSION_AGE;
}

SR_API const char *sr_lib_version_string_get(void)
{
	return SR_LIB_VERSION_STRING;
}

/** @} */
