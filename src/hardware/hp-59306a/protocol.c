/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2021 Frank Stettner <frank-stettner@gmx.net>
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
#include "scpi.h"
#include "protocol.h"

SR_PRIV int hp_59306a_switch_cg(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, gboolean enabled)
{
	char *cmd;
	struct channel_group_context *cgc;
	struct sr_scpi_dev_inst *scpi;
	int ret;

	if (!cg) {
		if (enabled)
			cmd = g_strdup("A123456");
		else
			cmd = g_strdup("B123456");
	} else {
		cgc = cg->priv;
		if (enabled)
			cmd = g_strdup_printf("A%zu", cgc->number);
		else
			cmd = g_strdup_printf("B%zu", cgc->number);
	}

	scpi = sdi->conn;
	ret = sr_scpi_send(scpi, cmd);
	g_free(cmd);

	return ret;
}
