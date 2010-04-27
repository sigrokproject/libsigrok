/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Håvard Espeland <gus@ping.uio.no>,
 * Copyright (C) 2010 Martin Stensgård <mastensg@ping.uio.no>
 * Copyright (C) 2010 Carl Henrik Lunde <chlunde@ping.uio.no>
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

#ifndef ASIX_SIGMA_H
#define ASIX_SIGMA_H

enum sigma_write_register {
	WRITE_CLOCK_SELECT	= 0,
	WRITE_TRIGGER_SELECT0	= 1,
	WRITE_TRIGGER_SELECT1	= 2,
	WRITE_MODE		= 3,
	WRITE_MEMROW		= 4,
	WRITE_POST_TRIGGER	= 5,
	WRITE_TRIGGER_OPTION	= 6,
	WRITE_PIN_VIEW		= 7,

	WRITE_TEST		= 15,
};

enum sigma_read_register {
	READ_ID			= 0,
	READ_TRIGGER_POS_LOW	= 1,
	READ_TRIGGER_POS_HIGH	= 2,
	READ_TRIGGER_POS_UP	= 3,
	READ_STOP_POS_LOW	= 4,
	READ_STOP_POS_HIGH	= 5,
	READ_STOP_POS_UP	= 6,
	READ_MODE		= 7,
	READ_PIN_CHANGE_LOW	= 8,
	READ_PIN_CHANGE_HIGH	= 9,
	READ_BLOCK_LAST_TS_LOW	= 10,
	READ_BLOCK_LAST_TS_HIGH	= 11,
	READ_PIN_VIEW		= 12,

	READ_TEST		= 15,
};

#define REG_ADDR_LOW		(0 << 4)
#define REG_ADDR_HIGH		(1 << 4)
#define REG_DATA_LOW		(2 << 4)
#define REG_DATA_HIGH_WRITE	(3 << 4)
#define REG_READ_ADDR		(4 << 4)
#define REG_DRAM_WAIT_ACK	(5 << 4)

/* Bit (1 << 4) can be low or high (double buffer / cache) */
#define REG_DRAM_BLOCK		(6 << 4)
#define REG_DRAM_BLOCK_BEGIN	(8 << 4)
#define REG_DRAM_BLOCK_DATA	(10 << 4)

#define NEXT_REG		1

#define EVENTS_PER_CLUSTER	7

#define CHUNK_SIZE		1024

#endif
