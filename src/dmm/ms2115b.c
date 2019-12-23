/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Vitaliy Vorobyov
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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

 /*
 * MASTECH MS2115B protocol parser.
 *
 * Sends 9 bytes.
 * D0 D1 D2 D3 D4 D5 D6 D7 D8 D9
 *
 * D0 = 0x55 - sync byte
 *
 * D1 - mode:
 * bits:
 * B7..B4 ??
 * B3 - func
 * B2..B0:
 * 0 - A 600/1000 (func=0 AC, func=1 DC), signed
 * 1 - A 60 (func=0 AC, func=1 DC), signed
 * 2 - V (func=0 AC, func=1 DC), signed
 * 3 - diode/beep (func=0 buz, func=1 diode)
 * 4 - resistance
 * 5 - capacitance
 * 6 - hz
 *
 * D2 - range
 *
 * D3 - frq range
 *
 * D4 main value LSB
 * D5 main value MSB
 *
 * (secondary value, hz, min/max, rel)
 * D6 secondary value LSB
 * D7 secondary value MSB
 *
 * D8 - flags
 * bits:
 * B7..B1:??
 * B0 - 0 - auto, 1 - manual
 *
 * resistance:
 * 55 04 00 00 9B 18 00 00 01 (0.L, manual) 600.0 Ohm (x 0.1)
 * 55 04 01 00 9B 18 00 00 01 (0.L, manual) 6.000 kOhm (x 0.001)
 * 55 04 02 00 9B 18 00 00 01 (0.L, manual) 60.00 kOhm (x 0.01)
 * 55 04 03 00 9B 18 00 00 01 (0.L, manual) 600.0 kOhm (x 0.1)
 * 55 04 04 00 9B 18 00 00 01 (0.L, manual) 6.000 MOhm (x 0.001)
 * 55 04 05 00 9B 18 00 00 00 (0.L, auto)   60.00 MOhm (x 0.01)
 *
 * capacitance:
 * 55 05 00 00 04 00 00 00 00 (4nF, auto)
 * 55 05 00 00 05 00 00 00 01 (5nF, manual)      6.000 nF (x 0.001)
 * 55 05 01 00 03 19 00 00 01 (0.L nF, manual)   60.00 nF (x 0.01)
 * 55 05 02 00 D4 03 00 00 01 (980.0 nF, manual) 600.0 nF (x 0.1)
 * 55 05 03 00 63 00 00 00 01 (0.099 uF, manual) 6.000 uF (x 0.001)
 * 55 05 04 00 40 18 00 00 01 (0.L uF, manual)
 * 55 05 04 00 F0 00 00 00 01 (2.40 uF, manual)  60.00 uF (x 0.01)
 * 55 05 05 00 17 00 00 00 01 (2.3 uF, manual)   600.0 uF (x 0.1)
 * 55 05 06 00 02 00 00 00 01 (0.002 mF, manual) 6.000 mF (x 0.001)
 * 55 05 07 00 2F 00 00 00 01 (0.47 mF, manual)  60.00 mF (x 0.01)
 *
 * voltage:
 * 55 02 00 00 00 00 00 00 01 (0.0mV, manual)  600.0 mV (x 0.1)
 * 55 02 01 00 00 00 00 00 00 (0.000V, auto)
 * 55 02 01 00 00 00 00 00 01 (0.000V, manual) 6.000 V  (x 0.001)
 * 55 02 02 00 00 00 00 00 01 (0.00V, manual)  60.00 V  (x 0.01)
 * 55 02 03 00 00 00 00 00 01 (0.0V, manual)   600.0 V  (x 0.1)
 * 55 02 04 00 00 00 00 00 01 (0V, manual)     1000  V  (x 1)
 *
 * - Communication parameters: Unidirectional, 1200/8n1
 * - CP2102 USB to UART bridge controller
 */

#include <config.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#define LOG_PREFIX "ms2115b"

static void handle_flags(struct sr_datafeed_analog *analog, float *floatval,
	const struct ms2115b_info *info)
{
	/* Measurement modes */
	if (info->is_volt) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}
	if (info->is_ampere) {
		analog->meaning->mq = SR_MQ_CURRENT;
		analog->meaning->unit = SR_UNIT_AMPERE;
	}
	if (info->is_ohm) {
		analog->meaning->mq = SR_MQ_RESISTANCE;
		analog->meaning->unit = SR_UNIT_OHM;
	}
	if (info->is_hz) {
		analog->meaning->mq = SR_MQ_FREQUENCY;
		analog->meaning->unit = SR_UNIT_HERTZ;
	}
	if (info->is_farad) {
		analog->meaning->mq = SR_MQ_CAPACITANCE;
		analog->meaning->unit = SR_UNIT_FARAD;
	}
	if (info->is_beep) {
		analog->meaning->mq = SR_MQ_CONTINUITY;
		analog->meaning->unit = SR_UNIT_BOOLEAN;
		*floatval = (*floatval == INFINITY) ? 0.0 : 1.0;
	}
	if (info->is_diode) {
		analog->meaning->mq = SR_MQ_VOLTAGE;
		analog->meaning->unit = SR_UNIT_VOLT;
	}

	if (info->is_duty_cycle)
		analog->meaning->mq = SR_MQ_DUTY_CYCLE;

	if (info->is_percent)
		analog->meaning->unit = SR_UNIT_PERCENTAGE;

	/* Measurement related flags */
	if (info->is_ac)
		analog->meaning->mqflags |= SR_MQFLAG_AC;
	if (info->is_dc)
		analog->meaning->mqflags |= SR_MQFLAG_DC;
	if (info->is_auto)
		analog->meaning->mqflags |= SR_MQFLAG_AUTORANGE;
	if (info->is_diode)
		analog->meaning->mqflags |= SR_MQFLAG_DIODE | SR_MQFLAG_DC;
}

SR_PRIV gboolean sr_ms2115b_packet_valid(const uint8_t *buf)
{
	sr_dbg("DMM packet: %02x %02x %02x %02x %02x %02x %02x %02x %02x",
		buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
		buf[7], buf[8]);

	if (buf[0] == 0x55)
		return TRUE;

	return FALSE;
}

/* Mode values equal to received data */
enum {
	MODE_A600_1000 = 0,
	MODE_A60 = 1,
	MODE_V = 2,
	MODE_DIODE_BEEP = 3,
	MODE_OHM = 4,
	MODE_CAP = 5,
	MODE_HZ = 6,
};

static const int res_exp[] = {
	-1,     /* 600.0 Ohm  (x 0.1)   */
	-3 + 3, /* 6.000 kOhm (x 0.001) */
	-2 + 3, /* 60.00 kOhm (x 0.01)  */
	-1 + 3, /* 600.0 kOhm (x 0.1)   */
	-3 + 6, /* 6.000 MOhm (x 0.001) */
	-2 + 6, /* 60.00 MOhm (x 0.01)  */
};

static const int cap_exp[] = {
	-3 - 9, /* 6.000 nF (x 0.001) */
	-2 - 9, /* 60.00 nF (x 0.01)  */
	-1 - 9, /* 600.0 nF (x 0.1)   */
	-3 - 6, /* 6.000 uF (x 0.001) */
	-2 - 6, /* 60.00 uF (x 0.01)  */
	-1 - 6, /* 600.0 uF (x 0.1)   */
	-3 - 3, /* 6.000 mF (x 0.001) */
	-2 - 3, /* 60.00 mF (x 0.01)  */
};

static const int hz_exp[] = {
	-2,     /* 60.00 Hz  (x 0.01)  */
	-1,     /* 600.0 Hz  (x 0.1)   */
	-3 + 3, /* 6.000 kHz (x 0.001) */
	-2 + 3, /* 60.00 kHz (x 0.01)  */
	-1 + 3, /* 600.0 kHz (x 0.1)   */
	-3 + 6, /* 6.000 MHz (x 0.001) */
	-2 + 6, /* 60.00 MHz (x 0.01)  */
};

static const int v_exp[] = {
	-1 - 3, /* 600.0 mV (x 0.1)   */
	-3,     /* 6.000 V  (x 0.001) */
	-2,     /* 60.00 V  (x 0.01)  */
	-1,     /* 600.0 V  (x 0.1)   */
	0,      /* 1000  V  (x 1)     */
};

SR_PRIV const char *ms2115b_channel_formats[MS2115B_DISPLAY_COUNT] = {
	"main", "sub",
};

static int ms2115b_parse(const uint8_t *buf, float *floatval,
	struct sr_datafeed_analog *analog, void *info)
{
	int exponent = 0;
	float up_limit = 6000.0;
	gboolean sign = FALSE;

	uint32_t mode = (buf[1] & 7);
	gboolean func = (buf[1] & 8) ? TRUE : FALSE;
	uint32_t range = (buf[2] & 7);

	struct ms2115b_info *info_local = info;

	enum eev121gw_display display = info_local->ch_idx;
	memset(info_local, 0, sizeof(*info_local));
	info_local->ch_idx = display;

	switch (display) {
	case MS2115B_DISPLAY_MAIN:
		switch (mode) {
		case MODE_A600_1000:
			exponent = -1;
			sign = TRUE;
			info_local->is_ampere = TRUE;
			if (func)
				info_local->is_dc = TRUE;
			else
				info_local->is_ac = TRUE;
			break;
		case MODE_A60:
			exponent = -2;
			sign = TRUE;
			info_local->is_ampere = TRUE;
			if (func)
				info_local->is_dc = TRUE;
			else
				info_local->is_ac = TRUE;
			break;
		case MODE_V:
			if (range >= ARRAY_SIZE(v_exp))
				return SR_ERR;
			exponent = v_exp[range];
			sign = TRUE;
			info_local->is_volt = TRUE;
			if (func)
				info_local->is_dc = TRUE;
			else
				info_local->is_ac = TRUE;
			break;
		case MODE_DIODE_BEEP:
			if (func) {
				exponent = -3;
				up_limit = 2500.0;
				info_local->is_diode = TRUE;
			} else {
				info_local->is_beep = TRUE;
			}
			break;
		case MODE_OHM:
			if (range >= ARRAY_SIZE(res_exp))
				return SR_ERR;
			exponent = res_exp[range];
			info_local->is_ohm = TRUE;
			break;
		case MODE_CAP:
			if (range >= ARRAY_SIZE(cap_exp))
				return SR_ERR;
			exponent = cap_exp[range];
			info_local->is_farad = TRUE;
			break;
		case MODE_HZ:
			range = (buf[3] & 7);
			if (range >= ARRAY_SIZE(hz_exp))
				return SR_ERR;
			exponent = hz_exp[range];
			info_local->is_hz = TRUE;
			break;
		default:
			return SR_ERR;
		}

		if (sign) {
			*floatval = RL16S(buf + 4); /* signed 16bit value */
		} else {
			*floatval = RL16(buf + 4); /* unsigned 16bit value */
		}

		info_local->is_auto = (buf[8] & 1) ? FALSE : TRUE;
		break;
	case MS2115B_DISPLAY_SUB:
		switch (mode) {
		case MODE_A600_1000:
		case MODE_A60:
		case MODE_V:
			if (func) /* DC */
				return SR_ERR_NA;

			/* AC */
			info_local->is_hz = TRUE;
			exponent = -2;
			break;
		case MODE_HZ:
			info_local->is_duty_cycle = TRUE;
			info_local->is_percent = TRUE;
			exponent = -1;
			break;
		default:
			return SR_ERR_NA;
		}

		*floatval = RL16(buf + 6); /* unsigned 16bit value */
		break;
	default:
		return SR_ERR;
	}

	if (fabsf(*floatval) > up_limit) {
		sr_spew("Over limit.");
		*floatval = INFINITY;
		return SR_OK;
	}

	*floatval *= powf(10, exponent);

	handle_flags(analog, floatval, info_local);

	analog->encoding->digits = -exponent;
	analog->spec->spec_digits = -exponent;

	return SR_OK;
}

/**
 * Parse a protocol packet.
 *
 * @param buf Buffer containing the 9-byte protocol packet. Must not be NULL.
 * @param floatval Pointer to a float variable. That variable will contain the
 *                 result value upon parsing success. Must not be NULL.
 * @param analog Pointer to a struct sr_datafeed_analog. The struct will be
 *               filled with data according to the protocol packet.
 *               Must not be NULL.
 * @param info Pointer to a struct ms2115b_info. The struct will be filled
 *             with data according to the protocol packet. Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR upon failure. Upon errors, the
 *         'analog' variable contents are undefined and should not be used.
 */
SR_PRIV int sr_ms2115b_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info)
{
	int ret;
	int ch_idx;
	struct ms2115b_info *info_local = info;

	ch_idx = info_local->ch_idx;
	ret = ms2115b_parse(buf, floatval, analog, info);
	info_local->ch_idx = ch_idx + 1;

	return ret;
}
