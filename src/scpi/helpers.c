/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2015 Bert Vermeulen <bert@biot.com>
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
#include <strings.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include "scpi.h"

#define LOG_PREFIX "scpi/helpers"

static const char *scpi_vendors[][2] = {
	{ "HEWLETT-PACKARD", "HP" },
	{ "Agilent Technologies", "Agilent" },
	{ "RIGOL TECHNOLOGIES", "Rigol" },
	{ "PHILIPS", "Philips" },
	{ "CHROMA", "Chroma" },
	{ "Chroma ATE", "Chroma" },
};

SR_PRIV const char *sr_vendor_alias(const char *raw_vendor)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(scpi_vendors); i++) {
		if (!g_ascii_strcasecmp(raw_vendor, scpi_vendors[i][0]))
			return scpi_vendors[i][1];
	}

	return raw_vendor;
}

SR_PRIV const char *scpi_cmd_get(const struct scpi_command *cmdtable, int command)
{
	unsigned int i;
	const char *cmd;

	if (!cmdtable)
		return NULL;

	cmd = NULL;
	for (i = 0; cmdtable[i].command; i++) {
		if (cmdtable[i].command == command) {
			cmd = cmdtable[i].string;
			break;
		}
	}

	return cmd;
}

SR_PRIV int scpi_cmd(const struct sr_dev_inst *sdi, const struct scpi_command *cmdtable,
		int command, ...)
{
	struct sr_scpi_dev_inst *scpi;
	va_list args;
	int ret;
	const char *cmd;

	if (!(cmd = scpi_cmd_get(cmdtable, command))) {
		/* Device does not implement this command, that's OK. */
		return SR_OK;
	}

	scpi = sdi->conn;
	va_start(args, command);
	ret = sr_scpi_send_variadic(scpi, cmd, args);
	va_end(args);

	return ret;
}

SR_PRIV int scpi_cmd_resp(const struct sr_dev_inst *sdi, const struct scpi_command *cmdtable,
		GVariant **gvar, const GVariantType *gvtype, int command, ...)
{
	struct sr_scpi_dev_inst *scpi;
	va_list args;
	double d;
	int ret;
	char *s;
	const char *cmd;

	if (!(cmd = scpi_cmd_get(cmdtable, command))) {
		/* Device does not implement this command. */
		return SR_ERR_NA;
	}

	scpi = sdi->conn;
	va_start(args, command);
	ret = sr_scpi_send_variadic(scpi, cmd, args);
	va_end(args);
	if (ret != SR_OK)
		return ret;

	/* Straight SCPI getters to GVariant types. */
	if (g_variant_type_equal(gvtype, G_VARIANT_TYPE_BOOLEAN)) {
		if ((ret = sr_scpi_get_string(scpi, NULL, &s)) != SR_OK)
			return ret;
		if (!g_ascii_strcasecmp(s, "ON") || !g_ascii_strcasecmp(s, "1")
				|| !g_ascii_strcasecmp(s, "YES"))
			*gvar = g_variant_new_boolean(TRUE);
		else if (!g_ascii_strcasecmp(s, "OFF") || !g_ascii_strcasecmp(s, "0")
				|| !g_ascii_strcasecmp(s, "NO"))
			*gvar = g_variant_new_boolean(FALSE);
		else
			ret = SR_ERR;
		g_free(s);
	} if (g_variant_type_equal(gvtype, G_VARIANT_TYPE_DOUBLE)) {
		if ((ret = sr_scpi_get_double(scpi, NULL, &d)) == SR_OK)
			*gvar = g_variant_new_double(d);
	} if (g_variant_type_equal(gvtype, G_VARIANT_TYPE_STRING)) {
		if ((ret = sr_scpi_get_string(scpi, NULL, &s)) == SR_OK)
			*gvar = g_variant_new_string(s);
	} else {
		sr_err("Unable to convert to desired GVariant type.");
		ret = SR_ERR_NA;
	}

	return ret;
}
