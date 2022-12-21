/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2020 Martin Eitzenberger <x@cymaphore.net>
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

/**
 * @file
 * @version 1
 *
 * APPA Data tables and name resolution
 */

#ifndef LIBSIGROK_HARDWARE_APPA_DMM_PROTOCOL_TABLES_H
#define LIBSIGROK_HARDWARE_APPA_DMM_PROTOCOL_TABLES_H

#include <config.h>
#include "protocol.h"

#include <math.h>

/* ******************************** */
/* ****** Resolvers / Tables ****** */
/* ******************************** */

/**
 * Test, if a received display reading is a wordcode
 * (e.g. text on display)
 *
 * @param arg_wordcode Raw reading
 * @return TRUE if it is a wordcode
 */
static int appadmm_is_wordcode(const int arg_wordcode)
{
	return arg_wordcode >= APPADMM_WORDCODE_TABLE_MIN;
}

/**
 * Test, if a received display reading is dash wordcode
 * (e.v. active channel with currently no available reading)
 *
 * @param arg_wordcode Raw reading
 * @return TRUE if it is a dash-reading
 */
static int appadmm_is_wordcode_dash(const int arg_wordcode)
{
	return
	arg_wordcode == APPADMM_WORDCODE_DASH
		|| arg_wordcode == APPADMM_WORDCODE_DASH1
		|| arg_wordcode == APPADMM_WORDCODE_DASH2;
}

/**
 * Get name as string of channel
 *
 * @param arg_channel Channel
 * @return Channel-Name
 */
SR_PRIV const char *appadmm_channel_name(const enum appadmm_channel_e arg_channel)
{
	switch (arg_channel) {
	case APPADMM_CHANNEL_INVALID:
		return APPADMM_STRING_NA;
	case APPADMM_CHANNEL_DISPLAY_PRIMARY:
		return "Primary";
	case APPADMM_CHANNEL_DISPLAY_SECONDARY:
		return "Secondary";
	}

	return APPADMM_STRING_NA;
}

/**
 * Get name as string of model id
 *
 * @param arg_model_id Model-ID
 * @return Model Name
 */
SR_PRIV const char *appadmm_model_id_name(const enum appadmm_model_id_e arg_model_id)
{
	switch (arg_model_id) {
	default:
	case APPADMM_MODEL_ID_OVERFLOW:
	case APPADMM_MODEL_ID_INVALID:
		return APPADMM_STRING_NA;
	case APPADMM_MODEL_ID_150:
		return "APPA 150";
	case APPADMM_MODEL_ID_150B:
		return "APPA 150B";
	case APPADMM_MODEL_ID_208:
		return "APPA 208";
	case APPADMM_MODEL_ID_208B:
		return "APPA 208B";
	case APPADMM_MODEL_ID_506:
		return "APPA 506";
	case APPADMM_MODEL_ID_506B:
		return "APPA 506B";
	case APPADMM_MODEL_ID_506B_2:
		return "APPA 506B";
	case APPADMM_MODEL_ID_501:
		return "APPA 501";
	case APPADMM_MODEL_ID_502:
		return "APPA 502";
	case APPADMM_MODEL_ID_S1:
		return "APPA S1";
	case APPADMM_MODEL_ID_S2:
		return "APPA S2";
	case APPADMM_MODEL_ID_S3:
		return "APPA S3";
	case APPADMM_MODEL_ID_172:
		return "APPA 172";
	case APPADMM_MODEL_ID_173:
		return "APPA 173";
	case APPADMM_MODEL_ID_175:
		return "APPA 175";
	case APPADMM_MODEL_ID_177:
		return "APPA 177";
	case APPADMM_MODEL_ID_SFLEX_10A:
		return "APPA sFlex-10A";
	case APPADMM_MODEL_ID_SFLEX_18A:
		return "APPA sFlex-18A";
	case APPADMM_MODEL_ID_A17N:
		return "APPA A17N";
	case APPADMM_MODEL_ID_S0:
		return "APPA S0";
	case APPADMM_MODEL_ID_179:
		return "APPA 179";
	case APPADMM_MODEL_ID_503:
		return "APPA 503";
	case APPADMM_MODEL_ID_505:
		return "APPA 505";
	/* ************************************ */
	case APPADMM_MODEL_ID_LEGACY_500:
		return "APPA 505";
	}

	return APPADMM_STRING_NA;
}

/**
 * Get Text representation of wordcode
 *
 * @param arg_wordcode Raw display reading
 * @return Display text as string
 */
static const char *appadmm_wordcode_name(const enum appadmm_wordcode_e arg_wordcode)
{
	switch (arg_wordcode) {
	case APPADMM_WORDCODE_SPACE:
		return "";
	case APPADMM_WORDCODE_FULL:
		return "Full";
	case APPADMM_WORDCODE_BEEP:
		return "Beep";
	case APPADMM_WORDCODE_APO:
		return "Auto Power-Off";
	case APPADMM_WORDCODE_B_LIT:
		return "Backlight";
	case APPADMM_WORDCODE_HAZ:
		return "Hazard";
	case APPADMM_WORDCODE_ON:
		return "On";
	case APPADMM_WORDCODE_OFF:
		return "Off";
	case APPADMM_WORDCODE_RESET:
		return "Reset";
	case APPADMM_WORDCODE_START:
		return "Start";
	case APPADMM_WORDCODE_VIEW:
		return "View";
	case APPADMM_WORDCODE_PAUSE:
		return "Pause";
	case APPADMM_WORDCODE_FUSE:
		return "Fuse";
	case APPADMM_WORDCODE_PROBE:
		return "Probe";
	case APPADMM_WORDCODE_DEF:
		return "Definition";
	case APPADMM_WORDCODE_CLR:
		return "Clr";
	case APPADMM_WORDCODE_ER:
		return "Er";
	case APPADMM_WORDCODE_ER1:
		return "Er1";
	case APPADMM_WORDCODE_ER2:
		return "Er2";
	case APPADMM_WORDCODE_ER3:
		return "Er3";
	case APPADMM_WORDCODE_DASH:
		return "-----";
	case APPADMM_WORDCODE_DASH1:
		return "-";
	case APPADMM_WORDCODE_TEST:
		return "Test";
	case APPADMM_WORDCODE_DASH2:
		return "--";
	case APPADMM_WORDCODE_BATT:
		return "Battery";
	case APPADMM_WORDCODE_DISLT:
		return "diSLt";
	case APPADMM_WORDCODE_NOISE:
		return "Noise";
	case APPADMM_WORDCODE_FILTR:
		return "Filter";
	case APPADMM_WORDCODE_PASS:
		return "PASS";
	case APPADMM_WORDCODE_NULL:
		return "null";
	case APPADMM_WORDCODE_0_20:
		return "0 - 20";
	case APPADMM_WORDCODE_4_20:
		return "4 - 20";
	case APPADMM_WORDCODE_RATE:
		return "Rate";
	case APPADMM_WORDCODE_SAVE:
		return "Save";
	case APPADMM_WORDCODE_LOAD:
		return "Load";
	case APPADMM_WORDCODE_YES:
		return "Yes";
	case APPADMM_WORDCODE_SEND:
		return "Send";
	case APPADMM_WORDCODE_AHOLD:
		return "Auto Hold";
	case APPADMM_WORDCODE_AUTO:
		return "Auto";
	case APPADMM_WORDCODE_CNTIN:
		return "Continuity";
	case APPADMM_WORDCODE_CAL:
		return "CAL";
	case APPADMM_WORDCODE_VERSION:
		return "Version";
	case APPADMM_WORDCODE_OL:
		return "OL";
	case APPADMM_WORDCODE_BAT_FULL:
		return "FULL";
	case APPADMM_WORDCODE_BAT_HALF:
		return "HALF";
	case APPADMM_WORDCODE_LO:
		return "Lo";
	case APPADMM_WORDCODE_HI:
		return "Hi";
	case APPADMM_WORDCODE_DIGIT:
		return "Digits";
	case APPADMM_WORDCODE_RDY:
		return "Ready";
	case APPADMM_WORDCODE_DISC:
		return "dISC";
	case APPADMM_WORDCODE_OUTF:
		return "outF";
	case APPADMM_WORDCODE_OLA:
		return "OLA";
	case APPADMM_WORDCODE_OLV:
		return "OLV";
	case APPADMM_WORDCODE_OLVA:
		return "OLVA";
	case APPADMM_WORDCODE_BAD:
		return "BAD";
	case APPADMM_WORDCODE_TEMP:
		return "TEMP";
	}

	return APPADMM_STRING_NA;
}

#endif/*LIBSIGROK_HARDWARE_APPA_DMM_PROTOCOL_TABLES_H*/
