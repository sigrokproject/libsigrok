/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2026 audiossis <audiossis@users.noreply.github.com>
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

/*
 * TODO: unlike most other Rigol scopes this model does not speak SCPI
 * over USBTMC. Commands are sent to the device one character at a time
 * as vendor control transfers, followed by a CR.
 */
SR_PRIV int rigol_ds5k_send(struct sr_usb_dev_inst *usb, const char *cmd)
{
	(void)usb;
	(void)cmd;

	return SR_ERR_NA;
}
