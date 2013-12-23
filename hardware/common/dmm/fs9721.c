/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2012 Uwe Hermann <uwe@hermann-uwe.de>
 * Copyright (C) 2012 Alexandru Gagniuc <mr.nuke.me@gmail.com>
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

/*
 * Fortune Semiconductor FS9721_LP3/FS9721B protocol parser.
 *
 * FS9721_LP3: 4000 counts (3 3/4 digits)
 * FS9721B/Q100: 2400 counts (3 2/3 digits)
 *
 * Same for both chips:
 *  - Packages: Bare die (78 pins) or QFP-100
 *  - Communication parameters: Unidirectional, 2400/8n1
 *  - The protocol seems to be exactly the same.
 */

#include <string.h>
#include <ctype.h>
#include <math.h>
#include <glib.h>
#include "libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "fs9721"

static int parse_digit(uint8_t b)
{
	switch (b) {
	case 0x7d:
		return 0;
	case 0x05:
		return 1;
	case 0x5b:
		return 2;
	case 0x1f:
		return 3;
	case 0x27:
		return 4;
	case 0x3e:
		return 5;
	case 0x7e:
		return 6;
	case 0x15:
		return 7;
	case 0x7f:
		return 8;
	case 0x3f:
		return 9;
	default:
		sr_err("Invalid digit byte: 0x%02x.", b);
		return -1;
	}
}

static gboolean sync_nibbles_valid(const uint8_t *buf)
{
	int i;

	/* Check the synchronization nibbles, and make sure they all match. */
	for (i = 0; i < FS9721_PACKET_SIZE; i++) {
		if (((buf[i] >> 4) & 0x0f) != (i + 1)) {
			sr_err("Sync nibble in byte %d (0x%02x) is invalid.",
			       i, buf[i]);
			return FALSE;
		}
	}

	return TRUE;
}

static gboolean flags_valid(const struct fs9721_info *info)
{
	int count;

	/* Does the packet have more than one multiplier? */
	count = 0;
	count += (info->is_nano) ? 1 : 0;
	count += (info->is_micro) ? 1 : 0;
	count += (info->is_milli) ? 1 : 0;
	count += (info->is_kilo) ? 1 : 0;
	count += (info->is_mega) ? 1 : 0;
	if (count > 1) {
		sr_err("More than one multiplier detected in packet.");
		return FALSE;
	}

	/* Does the packet "measure" more than one type of value? */
	count = 0;
	count += (info->is_hz) ? 1 : 0;
	count += (info->is_ohm) ? 1 : 0;
	count += (info->is_farad) ? 1 : 0;
	count += (info->is_ampere) ? 1 : 0;
	count += (info->is_volt) ? 1 : 0;
	count += (info->is_percent) ? 1 : 0;
	if (count > 1) {
		sr_err("More than one measurement type detected in packet.");
		return FALSE;
	}

	/* Both AC and DC set? */
	if (info->is_ac && info->is_dc) {
		sr_err("Both AC and DC flags detected in packet.");
		return FALSE;
	}

	/* RS232 flag not set? */
	if (!info->is_rs232) {
		sr_err("No RS232 flag detected in packet.");
		return FALSE;
	}

	return TRUE;
}

static int parse_value(const uint8_t *buf, float *result)
{
	int i, sign, intval = 0, digits[4];
	uint8_t digit_bytes[4];
	float floatval;

	/* Byte 1: LCD SEG2 */
	sign = ((buf[1] & (1 << 3)) != 0) ? -1 : 1;

	/*
	 * Bytes 1-8: Value (4 decimal digits, sign, decimal point)
	 *
	 * Over limit: "0L" (LCD), 0x00 0x7d 0x68 0x00 (digit bytes).
	 */

	/* Merge the two nibbles for a digit into one byte. */
	for (i = 0; i < 4; i++) {
		digit_bytes[i] = ((buf[1 + (i * 2)] & 0x0f) << 4);
		digit_bytes[i] |= (buf[1 + (i * 2) + 1] & 0x0f);

		/* Bit 7 in the byte is not part of the digit. */
		digit_bytes[i] &= ~(1 << 7);
	}

	/* Check for "OL". */
	if (digit_bytes[0] == 0x00 && digit_bytes[1] == 0x7d &&
	    digit_bytes[2] == 0x68 && digit_bytes[3] == 0x00) {
		sr_spew("Over limit.");
		*result = INFINITY;
		return SR_OK;
	}

	/* Parse the digits. */
	for (i = 0; i < 4; i++)
		digits[i] = parse_digit(digit_bytes[i]);
	sr_spew("Digits: %02x %02x %02x %02x (%d%d%d%d).",
		digit_bytes[0], digit_bytes[1], digit_bytes[2], digit_bytes[3],
		digits[0], digits[1], digits[2], digits[3]);

	/* Merge all digits into an integer value. */
	for (i = 0; i < 4; i++) {
		intval *= 10;
		intval += digits[i];
	}

	floatval = (float)intval;

	/* Decimal point position. */
	if ((buf[3] & (1 << 3)) != 0) {
		floatval /= 1000;
		sr_spew("Decimal point after first digit.");
	} else if ((buf[5] & (1 << 3)) != 0) {
		floatval /= 100;
		sr_spew("Decimal point after second digit.");
	} else if ((buf[7] & (1 << 3)) != 0) {
		floatval /= 10;
		sr_spew("Decimal point after third digit.");
	} else {
		sr_spew("No decimal point in the number.");
	}

	/* Apply sign. */
	floatval *= sign;

	sr_spew("The display value is %f.", floatval);

	*result = floatval;

	return SR_OK;
}

static void parse_flags(const uint8_t *buf, struct fs9721_info *info)
{
	/* Byte 0: LCD SEG1 */
	info->is_ac         = (buf[0] & (1 << 3)) != 0;
	info->is_dc         = (buf[0] & (1 << 2)) != 0;
	info->is_auto       = (buf[0] & (1 << 1)) != 0;
	info->is_rs232      = (buf[0] & (1 << 0)) != 0;

	/* Byte 1: LCD SEG2 */
	info->is_sign       = (buf[1] & (1 << 3)) != 0;

	/* Byte 9: LCD SEG10 */
	info->is_micro      = (buf[9] & (1 << 3)) != 0;
	info->is_nano       = (buf[9] & (1 << 2)) != 0;
	info->is_kilo       = (buf[9] & (1 << 1)) != 0;
	info->is_diode      = (buf[9] & (1 << 0)) != 0;

	/* Byte 10: LCD SEG11 */
	info->is_milli      = (buf[10] & (1 << 3)) != 0;
	info->is_percent    = (buf[10] & (1 << 2)) != 0;
	info->is_mega       = (buf[10] & (1 << 1)) != 0;
	info->is_beep       = (buf[10] & (1 << 0)) != 0;

	/* Byte 11: LCD SEG12 */
	info->is_farad      = (buf[11] & (1 << 3)) != 0;
	info->is_ohm        = (buf[11] & (1 << 2)) != 0;
	info->is_rel        = (buf[11] & (1 << 1)) != 0;
	info->is_hold       = (buf[11] & (1 << 0)) != 0;

	/* Byte 12: LCD SEG13 */
	info->is_ampere     = (buf[12] & (1 << 3)) != 0;
	info->is_volt       = (buf[12] & (1 << 2)) != 0;
	info->is_hz         = (buf[12] & (1 << 1)) != 0;
	info->is_bat        = (buf[12] & (1 << 0)) != 0;

	/* Byte 13: LCD SEG14 */
	info->is_c2c1_11    = (buf[13] & (1 << 3)) != 0;
	info->is_c2c1_10    = (buf[13] & (1 << 2)) != 0;
	info->is_c2c1_01    = (buf[13] & (1 << 1)) != 0;
	info->is_c2c1_00    = (buf[13] & (1 << 0)) != 0;
}

static void handle_flags(struct sr_datafeed_analog *analog, float *floatval,
			 const struct fs9721_info *info)
{
	/* Factors */
	if (info->is_nano)
		*floatval /= 1000000000;
	if (info->is_micro)
		*floatval /= 1000000;
	if (info->is_milli)
		*floatval /= 1000;
	if (info->is_kilo)
		*floatval *= 1000;
	if (info->is_mega)
		*floatval *= 1000000;

	/* Measurement modes */
	if (info->is_volt) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
	}
	if (info->is_ampere) {
		analog->mq = SR_MQ_CURRENT;
		analog->unit = SR_UNIT_AMPERE;
	}
	if (info->is_ohm) {
		analog->mq = SR_MQ_RESISTANCE;
		analog->unit = SR_UNIT_OHM;
	}
	if (info->is_hz) {
		analog->mq = SR_MQ_FREQUENCY;
		analog->unit = SR_UNIT_HERTZ;
	}
	if (info->is_farad) {
		analog->mq = SR_MQ_CAPACITANCE;
		analog->unit = SR_UNIT_FARAD;
	}
	if (info->is_beep) {
		analog->mq = SR_MQ_CONTINUITY;
		analog->unit = SR_UNIT_BOOLEAN;
		*floatval = (*floatval == INFINITY) ? 0.0 : 1.0;
	}
	if (info->is_diode) {
		analog->mq = SR_MQ_VOLTAGE;
		analog->unit = SR_UNIT_VOLT;
	}
	if (info->is_percent) {
		analog->mq = SR_MQ_DUTY_CYCLE;
		analog->unit = SR_UNIT_PERCENTAGE;
	}

	/* Measurement related flags */
	if (info->is_ac)
		analog->mqflags |= SR_MQFLAG_AC;
	if (info->is_dc)
		analog->mqflags |= SR_MQFLAG_DC;
	if (info->is_auto)
		analog->mqflags |= SR_MQFLAG_AUTORANGE;
	if (info->is_diode)
		analog->mqflags |= SR_MQFLAG_DIODE;
	if (info->is_hold)
		analog->mqflags |= SR_MQFLAG_HOLD;
	if (info->is_rel)
		analog->mqflags |= SR_MQFLAG_RELATIVE;

	/* Other flags */
	if (info->is_rs232)
		sr_spew("RS232 enabled.");
	if (info->is_bat)
		sr_spew("Battery is low.");
	if (info->is_c2c1_00)
		sr_spew("User-defined LCD symbol 0 is active.");
	if (info->is_c2c1_01)
		sr_spew("User-defined LCD symbol 1 is active.");
	if (info->is_c2c1_10)
		sr_spew("User-defined LCD symbol 2 is active.");
	if (info->is_c2c1_11)
		sr_spew("User-defined LCD symbol 3 is active.");
}

SR_PRIV gboolean sr_fs9721_packet_valid(const uint8_t *buf)
{
	struct fs9721_info info;

	parse_flags(buf, &info);

	return (sync_nibbles_valid(buf) && flags_valid(&info));
}

/**
 * Parse a protocol packet.
 *
 * @param buf Buffer containing the 14-byte protocol packet. Must not be NULL.
 * @param floatval Pointer to a float variable. That variable will contain the
 *                 result value upon parsing success. Mut not be NULL.
 * @param analog Pointer to a struct sr_datafeed_analog. The struct will be
 *               filled with data according to the protocol packet.
 *               Must not be NULL.
 * @param info Pointer to a struct fs9721_info. The struct will be filled
 *             with data according to the protocol packet. Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR upon failure. Upon errors, the
 *         'analog' variable contents are undefined and should not be used.
 */
SR_PRIV int sr_fs9721_parse(const uint8_t *buf, float *floatval,
			    struct sr_datafeed_analog *analog, void *info)
{
	int ret;
	struct fs9721_info *info_local;

	info_local = (struct fs9721_info *)info;

	if ((ret = parse_value(buf, floatval)) != SR_OK) {
		sr_err("Error parsing value: %d.", ret);
		return ret;
	}

	parse_flags(buf, info_local);
	handle_flags(analog, floatval, info_local);

	return SR_OK;
}

SR_PRIV void sr_fs9721_00_temp_c(struct sr_datafeed_analog *analog, void *info)
{
	struct fs9721_info *info_local;

	info_local = (struct fs9721_info *)info;

	/* User-defined FS9721_LP3 flag 'c2c1_00' means temperature (C). */
	if (info_local->is_c2c1_00) {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_CELSIUS;
	}
}

SR_PRIV void sr_fs9721_01_temp_c(struct sr_datafeed_analog *analog, void *info)
{
	struct fs9721_info *info_local;

	info_local = (struct fs9721_info *)info;

	/* User-defined FS9721_LP3 flag 'c2c1_01' means temperature (C). */
	if (info_local->is_c2c1_01) {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_CELSIUS;
	}
}

SR_PRIV void sr_fs9721_10_temp_c(struct sr_datafeed_analog *analog, void *info)
{
	struct fs9721_info *info_local;

	info_local = (struct fs9721_info *)info;

	/* User-defined FS9721_LP3 flag 'c2c1_10' means temperature (C). */
	if (info_local->is_c2c1_10) {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_CELSIUS;
	}
}

SR_PRIV void sr_fs9721_01_10_temp_f_c(struct sr_datafeed_analog *analog, void *info)
{
	struct fs9721_info *info_local;

	info_local = (struct fs9721_info *)info;

	/* User-defined FS9721_LP3 flag 'c2c1_01' means temperature (F). */
	if (info_local->is_c2c1_01) {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_FAHRENHEIT;
	}

	/* User-defined FS9721_LP3 flag 'c2c1_10' means temperature (C). */
	if (info_local->is_c2c1_10) {
		analog->mq = SR_MQ_TEMPERATURE;
		analog->unit = SR_UNIT_CELSIUS;
	}
}
