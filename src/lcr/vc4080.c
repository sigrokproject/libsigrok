/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2019 Gerhard Sittig <gerhard.sittig@gmx.net>
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
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"
#include <math.h>
#include <stdint.h>
#include <string.h>

#define LOG_PREFIX "vc4080"

#ifdef HAVE_SERIAL_COMM

/**
 * @file Packet parser for Voltcraft 4080 LCR meters.
 */

/*
 * Developer notes on the protocol and the implementation:
 *
 * The LCR meter is connected to a serial port (1200/7e1). The protocol
 * is text based (printables plus some line termination), is accessible
 * to interactive exploration in a terminal. Requests differ in length
 * (single character, or sequence of seven characters in brackets).
 * Responses either have 14 (setup) or 39 (measurement) characters.
 * Thus the protocol lends itself to integration with the serial-lcr
 * driver. Setup is handled outside of the acquisition loop, and all
 * measurement results are of equal length and end in a termination
 * that we can synchronize to. Requesting packets from the meter is
 * similar to serial-dmm operation.
 *
 * Quick notes for our parser's purposes:
 *
 *   pkt[0] 'L'/'C'/'R'
 *   pkt[1] 'Q'/'D'/'R'
 *   pkt[2] 'A'/'B' output frequency
 *   pkt[3] 'P'/'S' circuit model
 *   pkt[4] 'A'/'M' auto/manual
 *
 *   pkt[5:9] main display value in text format, '8' switching range, '9' OL
 *   pkt[10] main display range, '0'-'6', depends on RLC and freq and ser/par
 *
 *   pkt[11:14] secondary display value in text format, '9' OL
 *   pkt[15] secondary display range, '1'-'5', depends on QDR and Rs value
 *
 *   pkt[16] packet sequence counter, cycling through '0'-'9'
 *
 *   pkt[17:20] D value in text form, '9' OL
 *   pkt[21] D range
 *
 *   pkt[22:25] Q value in text form, '9' OL
 *   pkt[26] Q range
 *
 *   pkt[27] 'S'/'_', SETup(?)
 *   pkt[28] 'F'/'_', FUSE
 *   pkt[29] 'H'/'_', HOLD
 *   pkt[30] 'R' (present value), 'M' (max), 'I' (min), 'A' (avg),
 *           'X' (max - min), '_' (normal)
 *   pkt[31] 'R' (REL), 'S' (REL SET), '_' (normal)
 *   pkt[32] 'L' (LIMITS), '_' (normal)
 *   pkt[33] 'T' (TOL), 'S' (TOL SET), '_' (normal)
 *   pkt[34] 'B' (backlight), '_' (normal)
 *   pkt[35] 'A' (adapter inserted(?)), '_' (normal)
 *   pkt[36] 'B' (low battery), '_' (normal)
 *
 *   pkt[37] always CR (\r)
 *   pkt[38] always LF (\n)
 *
 * Example packet, PeakTech 2165, 1200/8n1 and parity bit stripped:
 *
 *   L Q A P A 9 0 0 0 0 6 1 4 0 6 2 1 0 7 1 1 4 1 4 0 6 2 _ _ _ _ _ _ _ _ _ _ CR LF
 *   0         5         10        15        20        25        30        35     38
 *
 * Another example, resistance mode, 1k probed:
 *
 *   52 5f 42 5f 41 30 39 39 33 30 32 30 30 30 30 39 33 37 34 35 36 31 30 30 31 33 34 5f 5f 5f 5f 5f 5f 5f 5f 5f 5f 0d 0a
 *   R _ B _ A 09930 2 00009 3 7456 1 0013 4 __________  CR/LF
 *
 * Another example, C mode:
 *
 *   43 51 42 53 4d 30 39 38 39 31 35 30 30 31 33 34 31 37 35 38 33 31 30 30 31 33 34 5f 5f 5f 5f 5f 5f 5f 5f 5f 5f 0d 0a
 *   C  Q  B  S  M  09891 5           00134 1           7583 1         0013 4         ____...
 *   C, Q, 120, ser, man, 09891 @2000uF -> C = 989.1uF, 00134 -> Q = 13.4
 *
 *   43 51 42 53 4d 30 39 38 38 30 35 30 30 31 33 34 34 37 35 37 34 31 30 30 31 33 34 5f 5f 5f 5f 5f 5f 5f 42 5f 5f 0d 0a
 *   900uF (main)
 *
 * For more details see Conrad's summary document and PeakTech's manual:
 * http://www.produktinfo.conrad.com/datenblaetter/100000-124999/121064-da-01-en-Schnittstellenbeschr_LCR_4080_Handmessg.pdf
 * http://peaktech.de/productdetail/kategorie/lcr-messer/produkt/p-2165.html?file=tl_files/downloads/2001%20-%203000/PeakTech_2165_USB.pdf
 *
 * TODO
 * - Check response lengths. Are line terminators involved during setup?
 * - Check parity. Does FT232R not handle parity correctly? Neither 7e1 (as
 *   documented) nor 7o1 (for fun) worked. 8n1 provided data but contained
 *   garbage (LCR driver needs to strip off the parity bit?).
 * - Determine whether the D and Q channels are required. It seems that
 *   every LCR packet has space to provide these values, but we may as well
 *   get away with just two channels, since users can select D and Q to be
 *   shown in the secondary display. It's yet uncertain whether the D and Q
 *   values in the packets are meaningful when the meter is not in the D/Q
 *   measurement mode.
 */

/*
 * Supported output frequencies and equivalent circuit models. A helper
 * for the packet parser (accepting a "code" for the property, regardless
 * of its position in the LCR packet), and a list for capability queries.
 * Concentrated in a single spot to remain aware duing maintenance.
 */

static const double frequencies[] = {
	SR_HZ(120), SR_KHZ(1),
};

static uint64_t get_frequency(char code)
{
	switch (code) {
	case 'A': return SR_KHZ(1);
	case 'B': return SR_HZ(120);
	default: return 0;
	}
}

enum equiv_model { MODEL_PAR, MODEL_SER, MODEL_NONE, };

static const char *const circuit_models[] = {
	"PARALLEL", "SERIES", "NONE",
};

static enum equiv_model get_equiv_model(char lcr_code, char model_code)
{
	switch (lcr_code) {
	case 'L': /* EMPTY */ break;
	case 'C': /* EMPTY */ break;
	case 'R': return MODEL_NONE;
	default: return MODEL_NONE;
	}
	switch (model_code) {
	case 'P': return MODEL_PAR;
	case 'S': return MODEL_SER;
	default: return MODEL_NONE;
	}
}

static const char *get_equiv_model_text(enum equiv_model model)
{
	return circuit_models[model];
}

/*
 * Packet parse routine and its helpers. Depending on the specific layout
 * of the meter's packet which communicates measurement results. Some of
 * them are also used outside of strict packet parsing for value extraction.
 */

static uint64_t parse_freq(const uint8_t *pkt)
{
	return get_frequency(pkt[2]);
}

static const char *parse_model(const uint8_t *pkt)
{
	return get_equiv_model_text(get_equiv_model(pkt[0], pkt[3]));
}

static float parse_number(const uint8_t *digits, size_t length)
{
	char value_text[8];
	float number;
	int ret;

	memcpy(value_text, digits, length);
	value_text[length] = '\0';
	ret = sr_atof_ascii(value_text, &number);

	return (ret == SR_OK) ? number : 0;
}

/*
 * Conrad's protocol description suggests that:
 * - The main display's LCR selection, output frequency, and range
 *   result in an Rs value in the 100R to 100k range, in addition to
 *   the main display's scale for the value.
 * - The secondary display's DQR selection, the above determined Rs
 *   value, and range result in the value's scale.
 * - The D and Q values' range seems to follow the secondary display's
 *   logic.
 */

enum lcr_kind { LCR_NONE, LCR_IS_L, LCR_IS_C, LCR_IS_R, };
enum dqr_kind { DQR_NONE, DQR_IS_D, DQR_IS_Q, DQR_IS_R, };

static int get_main_scale_rs(int *digits, int *rs,
	uint8_t range, enum lcr_kind lcr, uint64_t freq)
{
	/*
	 * Scaling factors for values. Digits count for 20000 full scale.
	 * Full scale values for different modes are:
	 *   R: 20R, 200R, 2k, 20k, 200k, 2M, 10M
	 *   L 1kHz: 2mH, 20mH, 200mH, 2H, 20H, 200H, 1000H
	 *   L 120Hz: 20mH, 200mH, 2H, 20H, 200H, 2kH, 10kH
	 *   C 1kHz: 2nF, 20nF, 200nF, 2uF, 20uF, 200uF, 2mF
	 *   C 120Hz: 20nF, 200nF, 2unF, 20uF, 200uF, 2muF, 20mF
	 */
	static const int dig_r[] = { -3, -2, -1, +0, +1, +2, +3, };
	static const int dig_l_1k[] = { -7, -6, -5, -4, -3, -2, -1, };
	static const int dig_l_120[] = { -6, -5, -4, -3, -2, -1, 0, };
	static const int dig_c_1k[] = { -13, -12, -11, -10, -9, -8, -7, };
	static const int dig_c_120[] = { -12, -11, -10, -9, -8, -7, -6, };
	/*
	 * Rs values for the scale, depending on LCR mode.
	 * Values for R/L: 100R, 100R, 100R, 1k, 10k, 100k, 100k
	 * Values for C: 100k, 100k, 10k, 1k, 100R, 100R, 100R
	 */
	static const int rs_r_l[] = {
		100, 100, 100, 1000, 10000, 100000, 100000,
	};
	static const int rs_c[] = {
		100000, 100000, 10000, 1000, 100, 100, 100,
	};

	const int *digits_table, *rs_table;

	/* The 'range' input value is only valid between 0..6. */
	if (range > 6)
		return SR_ERR_DATA;

	if (lcr == LCR_IS_R) {
		digits_table = dig_r;
		rs_table = rs_r_l;
	} else if (lcr == LCR_IS_L && freq == SR_KHZ(1)) {
		digits_table = dig_l_1k;
		rs_table = rs_r_l;
	} else if (lcr == LCR_IS_L && freq == SR_HZ(120)) {
		digits_table = dig_l_120;
		rs_table = rs_r_l;
	} else if (lcr == LCR_IS_C && freq == SR_KHZ(1)) {
		digits_table = dig_c_1k;
		rs_table = rs_c;
	} else if (lcr == LCR_IS_C && freq == SR_HZ(120)) {
		digits_table = dig_c_120;
		rs_table = rs_c;
	} else {
		return SR_ERR_DATA;
	}

	if (digits)
		*digits = digits_table[range];
	if (rs)
		*rs = rs_table[range];

	return SR_OK;
}

static int get_sec_scale(int *digits, uint8_t range, enum dqr_kind dqr, int rs)
{
	static const int dig_d_q[] = { 0, -1, -2, -3, -4, 0, };
	static const int dig_r_100[] = { 0, -2, -1, +0, +1, 0, };
	static const int dig_r_1k_10k[] = { 0, -2, -1, +0, +1, +2, };
	static const int dig_r_100k[] = { 0, 0, -1, +0, +1, +2, };

	const int *digits_table;

	/*
	 * Absolute 'range' limits are 1..5, some modes have additional
	 * invalid positions (these get checked below).
	 */
	if (range < 1 || range > 5)
		return SR_ERR_DATA;

	if (dqr == DQR_IS_D || dqr == DQR_IS_Q) {
		if (range > 4)
			return SR_ERR_DATA;
		digits_table = dig_d_q;
	} else if (dqr == DQR_IS_R && rs == 100) {
		if (range > 4)
			return SR_ERR_DATA;
		digits_table = dig_r_100;
	} else if (dqr == DQR_IS_R && (rs == 1000 || rs == 10000)) {
		digits_table = dig_r_1k_10k;
	} else if (dqr == DQR_IS_R && rs == 100000) {
		if (range < 2)
			return SR_ERR_DATA;
		digits_table = dig_r_100k;
	} else {
		return SR_ERR_DATA;
	}

	if (digits)
		*digits = digits_table[range];

	return SR_OK;
}

static void parse_measurement(const uint8_t *pkt, float *floatval,
	struct sr_datafeed_analog *analog, size_t disp_idx)
{
	enum lcr_kind lcr;
	enum dqr_kind dqr;
	uint64_t freq;
	enum equiv_model model;
	gboolean is_auto, main_ranging, main_ol, sec_ol, d_ol, q_ol;
	float main_value, sec_value, d_value, q_value;
	char main_range, sec_range, d_range, q_range;
	gboolean is_hold, is_relative, has_adapter, is_lowbatt;
	enum minmax_kind {
		MINMAX_MAX, MINMAX_MIN, MINMAX_SPAN,
		MINMAX_AVG, MINMAX_CURR, MINMAX_NONE,
	} minmax;
	gboolean is_parallel;
	int mq, mqflags, unit;
	float value;
	int digits, exponent;
	gboolean ol, invalid;
	int ret, rs, main_digits, sec_digits, d_digits, q_digits;
	int main_invalid, sec_invalid, d_invalid, q_invalid;

	/* Prepare void return values for error paths. */
	analog->meaning->mq = 0;
	analog->meaning->mqflags = 0;
	if (disp_idx >= VC4080_CHANNEL_COUNT)
		return;

	/*
	 * The interpretation of secondary displays may depend not only
	 * on the meter's status (indicator flags), but also on the main
	 * display's current value (ranges, scaling). Unconditionally
	 * inspect most of the packet's content, regardless of which
	 * display we are supposed to extract the value for in this
	 * invocation.
	 *
	 * While we are converting the input text, check a few "fatal"
	 * conditions early, cease further packet inspection when the
	 * value is unstable or not yet available, or when the meter's
	 * current mode/function is not supported by this LCR parser.
	 */
	switch (pkt[0]) {
	case 'L': lcr = LCR_IS_L; break;
	case 'R': lcr = LCR_IS_R; break;
	case 'C': lcr = LCR_IS_C; break;
	default: return;
	}
	switch (pkt[1]) {
	case 'D': dqr = DQR_IS_D; break;
	case 'Q': dqr = DQR_IS_Q; break;
	case 'R': dqr = DQR_IS_R; break;
	case '_': dqr = DQR_NONE; break; /* Can be valid, like in R mode. */
	default: return;
	}
	freq = get_frequency(pkt[2]);
	model = get_equiv_model(pkt[0], pkt[3]);
	is_auto = pkt[4] == 'A';
	main_ranging = pkt[5] == '8';
	if (main_ranging)	/* Switching ranges. */
		return;
	main_ol = pkt[5] == '9';
	main_value = parse_number(&pkt[5], 5);
	main_range = pkt[10];
	if (main_range < '0' || main_range > '6')
		main_range = '9';
	main_range -= '0';
	/*
	 * Contrary to the documentation, there have been valid four-digit
	 * values in the secondary display which start with '9'. Let's not
	 * consider these as overflown. Out-of-range 'range' specs for the
	 * secondary display will also invalidate these values.
	 */
	sec_ol = 0 && pkt[11] == '9';
	sec_value = parse_number(&pkt[11], 4);
	sec_range = pkt[15];
	if (sec_range < '0' || sec_range > '6')
		sec_range = '9';
	sec_range -= '0';
	d_ol = pkt[17] == '9';
	d_value = parse_number(&pkt[17], 4);
	d_range = pkt[21];
	if (d_range < '0' || d_range > '6')
		d_range = '9';
	d_range -= '0';
	q_ol = pkt[22] == '9';
	q_value = parse_number(&pkt[22], 4);
	q_range = pkt[26];
	if (q_range < '0' || q_range > '6')
		q_range = '9';
	d_range -= '0';
	switch (pkt[27]) {
	case 'S': return;	/* Setup mode. Not supported. */
	case '_': /* EMPTY */ break;
	default: return;	/* Unknown. */
	}
	is_hold = pkt[29] == 'H';
	switch (pkt[30]) {	/* Min/max modes. */
	case 'R': minmax = MINMAX_CURR; break;	/* Live reading. */
	case 'M': minmax = MINMAX_MAX; break;
	case 'I': minmax = MINMAX_MIN; break;
	case 'X': minmax = MINMAX_SPAN; break;	/* "Max - min" difference. */
	case 'A': minmax = MINMAX_AVG; break;
	case '_': minmax = MINMAX_NONE; break;
	default: return;	/* Unknown. */
	}
	if (minmax == MINMAX_SPAN)	/* Not supported. */
		return;
	if (minmax == MINMAX_CURR)	/* Normalize. */
		minmax = MINMAX_NONE;
	switch (pkt[31]) {
	case 'R': is_relative = TRUE; break;
	case 'S': return;	/* Relative setup. Not supported. */
				/* TODO Is this SR_MQFLAG_REFERENCE? */
	case '_': is_relative = FALSE; break;
	default: return;	/* Unknown. */
	}
	if (pkt[32] != '_')	/* Limits. Not supported. */
		return;
	if (pkt[33] != '_')	/* Tolerance. Not supported. */
		return;
	has_adapter = pkt[35] == 'A';
	is_lowbatt = pkt[36] == 'B';

	/*
	 * Always need to inspect the main display's properties, to
	 * determine how to interpret the secondary displays.
	 */
	rs = main_digits = sec_digits = d_digits = q_digits = 0;
	main_invalid = sec_invalid = d_invalid = q_invalid = 0;
	ret = get_main_scale_rs(&main_digits, &rs, main_range, lcr, freq);
	if (ret != SR_OK)
		main_invalid = 1;
	ret = get_sec_scale(&sec_digits, sec_range, dqr, rs);
	if (ret != SR_OK)
		sec_invalid = 1;
	ret = get_sec_scale(&d_digits, d_range, dqr, rs);
	if (ret != SR_OK)
		d_invalid = 1;
	ret = get_sec_scale(&q_digits, q_range, dqr, rs);
	if (ret != SR_OK)
		q_invalid = 1;

	/* Determine the measurement value and its units. Apply scaling. */
	is_parallel = model == MODEL_PAR;
	mq = 0;
	mqflags = 0;
	unit = 0;
	switch (disp_idx) {
	case VC4080_DISPLAY_PRIMARY:
		invalid = main_invalid;
		if (invalid)
			break;
		if (lcr == LCR_IS_L) {
			mq = is_parallel
				? SR_MQ_PARALLEL_INDUCTANCE
				: SR_MQ_SERIES_INDUCTANCE;
			unit = SR_UNIT_HENRY;
		} else if (lcr == LCR_IS_C) {
			mq = is_parallel
				? SR_MQ_PARALLEL_CAPACITANCE
				: SR_MQ_SERIES_CAPACITANCE;
			unit = SR_UNIT_FARAD;
		} else if (lcr == LCR_IS_R) {
			mq = is_parallel
				? SR_MQ_PARALLEL_RESISTANCE
				: SR_MQ_SERIES_RESISTANCE;
			unit = SR_UNIT_OHM;
		}
		value = main_value;
		ol = main_ol;
		digits = 0;
		exponent = main_digits;
		break;
	case VC4080_DISPLAY_SECONDARY:
		invalid = sec_invalid;
		if (invalid)
			break;
		if (dqr == DQR_IS_D) {
			mq = SR_MQ_DISSIPATION_FACTOR;
			unit = SR_UNIT_UNITLESS;
		} else if (dqr == DQR_IS_Q) {
			mq = SR_MQ_QUALITY_FACTOR;
			unit = SR_UNIT_UNITLESS;
		} else if (dqr == DQR_IS_R) {
			mq = SR_MQ_RESISTANCE;
			unit = SR_UNIT_OHM;
		}
		value = sec_value;
		ol = sec_ol;
		digits = 0;
		exponent = sec_digits;
		break;
#if VC4080_WITH_DQ_CHANS
	case VC4080_DISPLAY_D_VALUE:
		invalid = d_invalid;
		if (invalid)
			break;
		mq = SR_MQ_DISSIPATION_FACTOR;
		unit = SR_UNIT_UNITLESS;
		value = d_value;
		ol = d_ol;
		digits = 4;
		exponent = d_digits;
		break;
	case VC4080_DISPLAY_Q_VALUE:
		invalid = q_invalid;
		if (invalid)
			break;
		mq = SR_MQ_QUALITY_FACTOR;
		unit = SR_UNIT_UNITLESS;
		value = q_value;
		ol = q_ol;
		digits = 4;
		exponent = q_digits;
		break;
#else
	(void)d_invalid;
	(void)d_value;
	(void)d_ol;
	(void)d_digits;
	(void)q_invalid;
	(void)q_value;
	(void)q_ol;
	(void)q_digits;
#endif
	default:
		/* ShouldNotHappen(TM). Won't harm either. Silences warnings. */
		return;
	}
	if (invalid)
		return;
	if (is_auto)
		mqflags |= SR_MQFLAG_AUTORANGE;
	if (is_hold)
		mqflags |= SR_MQFLAG_HOLD;
	if (is_relative)
		mqflags |= SR_MQFLAG_RELATIVE;
	if (has_adapter)
		mqflags |= SR_MQFLAG_FOUR_WIRE;
	switch (minmax) {
	case MINMAX_MAX:
		mqflags |= SR_MQFLAG_MAX;
		break;
	case MINMAX_MIN:
		mqflags |= SR_MQFLAG_MIN;
		break;
	case MINMAX_SPAN:
		mqflags |= SR_MQFLAG_MAX | SR_MQFLAG_RELATIVE;
		break;
	case MINMAX_AVG:
		mqflags |= SR_MQFLAG_AVG;
		break;
	case MINMAX_CURR:
	case MINMAX_NONE:
	default:
		/* EMPTY */
		break;
	}

	/* "Commit" the resulting value. */
	if (ol) {
		value = INFINITY;
	} else {
		value *= powf(10, exponent);
		digits -= exponent;
	}
	*floatval = value;
	analog->meaning->mq = mq;
	analog->meaning->mqflags = mqflags;
	analog->meaning->unit = unit;
	analog->encoding->digits = digits;
	analog->spec->spec_digits = digits;

	/* Low battery is rather severe, the measurement could be invalid. */
	if (is_lowbatt)
		sr_warn("Low battery.");
}

/*
 * Workaround for cables' improper(?) parity handling.
 * TODO Should this move to serial-lcr or even common libsigrok code?
 *
 * Implementor's note: Serial communication is documented to be 1200/7e1.
 * But practial setups with the shipped FT232R cable received no response
 * at all with these settings. The 8n1 configuration resulted in responses
 * while the LCR meter's packet parser then needs to strip the parity bits.
 *
 * Let's run this slightly modified setup for now, until more cables and
 * compatible devices got observed and the proper solution gets determined.
 * This cheat lets us receive measurement data right now. Stripping the
 * parity bits off the packet bytes here in the parser is an idempotent
 * operation that happens to work during stream detect as well as in the
 * acquisition loop. It helps in the 8n1 configuration, and keeps working
 * transparently in the 7e1 configuration, too. No harm is done, and the
 * initial device support is achieved.
 *
 * By coincidence, the 'N' command which requests the next measurement
 * value happens to conform with the 7e1 frame format (0b_0100_1110
 * byte value). When the SETUP commands are supposed to work with this
 * LCR meter as well, then the serial-lcr driver's TX data and RX data
 * probably needs to pass LCR chip specific transformation routines,
 * if the above mentioned parity support in serial cables issue has not
 * yet been resolved.
 */

static void strip_parity_bit(uint8_t *p, size_t l)
{
	while (l--)
		*p++ &= ~0x80;
}

/* LCR packet parser's public API. */

SR_PRIV const char *vc4080_channel_formats[VC4080_CHANNEL_COUNT] = {
	"P1", "P2",
#if VC4080_WITH_DQ_CHANS
	"D", "Q",
#endif
};

SR_PRIV int vc4080_packet_request(struct sr_serial_dev_inst *serial)
{
	static const char *command = "N";

	serial_write_blocking(serial, command, strlen(command), 0);

	return SR_OK;
}

SR_PRIV gboolean vc4080_packet_valid(const uint8_t *pkt)
{
	/* Workaround for funny serial cables. */
	strip_parity_bit((void *)pkt, VC4080_PACKET_SIZE);

	/* Fixed CR/LF terminator. */
	if (pkt[37] != '\r' || pkt[38] != '\n')
		return FALSE;

	return TRUE;
}

SR_PRIV int vc4080_packet_parse(const uint8_t *pkt, float *val,
	struct sr_datafeed_analog *analog, void *info)
{
	struct lcr_parse_info *parse_info;

	/* Workaround for funny serial cables. */
	strip_parity_bit((void *)pkt, VC4080_PACKET_SIZE);

	parse_info = info;
	if (!parse_info->ch_idx) {
		parse_info->output_freq = parse_freq(pkt);
		parse_info->circuit_model = parse_model(pkt);
	}
	if (val && analog)
		parse_measurement(pkt, val, analog, parse_info->ch_idx);

	return SR_OK;
}

/*
 * These are the get/set/list routines for the _chip_ specific parameters,
 * the _device_ driver resides in src/hardware/serial-lcr/ instead.
 */

SR_PRIV int vc4080_config_list(uint32_t key, GVariant **data,
	const struct sr_dev_inst *sdi, const struct sr_channel_group *cg)
{

	(void)sdi;
	(void)cg;

	switch (key) {
	case SR_CONF_OUTPUT_FREQUENCY:
		*data = g_variant_new_fixed_array(G_VARIANT_TYPE_DOUBLE,
			ARRAY_AND_SIZE(frequencies), sizeof(frequencies[0]));
		return SR_OK;
	case SR_CONF_EQUIV_CIRCUIT_MODEL:
		*data = g_variant_new_strv(ARRAY_AND_SIZE(circuit_models));
		return SR_OK;
	default:
		return SR_ERR_NA;
	}
	/* UNREACH */
}

#endif
