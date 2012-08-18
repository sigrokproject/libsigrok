/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2012 Bert Vermeulen <bert@biot.com>
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

#include "libsigrok.h"
#include "libsigrok-internal.h"
#include "genericdmm.h"


static int fs9922_data(struct sr_dev_inst *sdi)
{
	struct sr_datafeed_packet packet;
	struct sr_datafeed_analog analog;

	packet.type = SR_DF_ANALOG;
	packet.payload = &analog;

	return SR_OK;
}


SR_PRIV struct dmmchip dmmchip_fs9922 = {
	.data = fs9922_data,
};

