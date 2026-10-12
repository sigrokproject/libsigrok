/*
 * libslogic — SLogic Combo 8 device model (8 channels, USB 2.0 HS).
 *
 * Copyright (C) 2023-2025 Shenzhen Sipeed Technology Co., Ltd.
 * (深圳市矽速科技有限公司) <support@sipeed.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.  See <http://www.gnu.org/licenses/>.
 *
 * Model table only; the Combo 8 command protocol (CMD_START) lives in slogic.c.
 * Unlike the U3 models this one uses the small command protocol, not the
 * register/AUX map.
 */

#include "slogic.h"

static const uint64_t rates_8u2[] = {
	SLOGIC_MHZ(1),  SLOGIC_MHZ(2),  SLOGIC_MHZ(4),  SLOGIC_MHZ(5),
	SLOGIC_MHZ(8),  SLOGIC_MHZ(10), SLOGIC_MHZ(16), SLOGIC_MHZ(20),
	SLOGIC_MHZ(32), SLOGIC_MHZ(40), SLOGIC_MHZ(80), SLOGIC_MHZ(160),
};

/* 2 ch -> 160, 4 ch -> 80, 8 ch -> 40 MHz. */
static const slogic_rate_limit limits_8u2[] = {
	{ 2, SLOGIC_MHZ(160) },
	{ 4, SLOGIC_MHZ(80) },
	{ 8, SLOGIC_MHZ(40) },
};

const slogic_model slogic_model_combo8 = {
	.name = "SLogic Combo 8",
	.pid = SLOGIC_PID_COMBO8,
	.ep_in = 0x81,
	.physical_channels = 8,
	.proto = SLOGIC_PROTO_COMBO8,
	.max_bandwidth_hz = SLOGIC_MHZ(320),
	.rates = rates_8u2,
	.rate_count = sizeof(rates_8u2) / sizeof(rates_8u2[0]),
	.limits = limits_8u2,
	.limit_count = sizeof(limits_8u2) / sizeof(limits_8u2[0]),
	.first_drop_bytes = 0, /* Combo8 stream has no partial-head artifact */
};
