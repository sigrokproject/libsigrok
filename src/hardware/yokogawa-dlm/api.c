/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2014 abraxa (Soeren Apel) <soeren@apelpie.net>
 * Based on the Hameg HMO driver by poljar (Damir Jelić) <poljarinho@gmail.com>
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

#include <stdlib.h>
#include "protocol.h"


SR_PRIV struct sr_dev_driver yokogawa_dlm_driver_info = {
	.name = "yokogawa-dlm",
	.longname = "Yokogawa DL/DLM driver",
	.api_version = 1,
	.init = NULL,
	.cleanup = NULL,
	.scan = NULL,
	.dev_list = NULL,
	.dev_clear = NULL,
	.config_get = NULL,
	.config_set = NULL,
	.config_list = NULL,
	.dev_open = NULL,
	.dev_close = NULL,
	.dev_acquisition_start = NULL,
	.dev_acquisition_stop = NULL,
	.priv = NULL,
};
