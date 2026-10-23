/*
 * libslogic — SLogic32U3 device model.
 *
 * Copyright (C) 2023-2025 Shenzhen Sipeed Technology Co., Ltd.
 * (深圳市矽速科技有限公司) <support@sipeed.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.  See <http://www.gnu.org/licenses/>.
 *
 * Model table only; the U3 register/AUX protocol lives in slogic.c and is
 * shared with slogic16u3.c.
 */

#include "slogic.h"

static const uint64_t rates_32u3[] = {
	SLOGIC_MHZ(5),   SLOGIC_MHZ(8),   SLOGIC_MHZ(10),  SLOGIC_MHZ(16),
	SLOGIC_MHZ(20),  SLOGIC_MHZ(25),  SLOGIC_MHZ(32),  SLOGIC_MHZ(40),
	SLOGIC_MHZ(50),  SLOGIC_MHZ(80),  SLOGIC_MHZ(100), SLOGIC_MHZ(160),
	SLOGIC_MHZ(200), SLOGIC_MHZ(400), SLOGIC_MHZ(800), SLOGIC_MHZ(1400),
};

/* 4 ch -> 1400, 8 ch -> 800, 16 ch -> 400, 32 ch -> 200 MHz. */
static const slogic_rate_limit limits_32u3[] = {
	{ 4, SLOGIC_MHZ(1400) },
	{ 8, SLOGIC_MHZ(800) },
	{ 16, SLOGIC_MHZ(400) },
	{ 32, SLOGIC_MHZ(200) },
};

const slogic_model slogic_model_32u3 = {
	.name = "SLogic32U3",
	.pid = SLOGIC_PID_32U3,
	.ep_in = 0x82,
	.physical_channels = 32,
	.proto = SLOGIC_PROTO_U3,
	.max_bandwidth_hz = SLOGIC_MHZ(6400),
	.rates = rates_32u3,
	.rate_count = sizeof(rates_32u3) / sizeof(rates_32u3[0]),
	.limits = limits_32u3,
	.limit_count = sizeof(limits_32u3) / sizeof(limits_32u3[0]),
	.first_drop_bytes = 0, /* firmware gates on whole-word boundaries (SN 202610120954+) */
};
