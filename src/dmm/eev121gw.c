/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2018 Gerhard Sittig <gerhard.sittig@gmx.net>
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

/**
 * @file
 *
 * EEVblog 121GW 19-bytes binary protocol parser.
 *
 * @internal
 *
 * Note that this protocol is different from other meters. We need not
 * decode the LCD presentation (segments a-g and dot of seven segment
 * displays). Neither need we decode a textual presentation consisting
 * of number strings with decimals, and scale/quantity suffixes. Instead
 * a binary packet is received which contains an unsigned mantissa for
 * the value, and a number of boolean flags as well as bitfields for modes
 * and ranges.
 *
 * But the protocol is also similar to the four-display variant of the
 * metex14 protocol. A single DMM packet contains information for two
 * displays and a bargraph, as well as several flags corresponding to
 * display indicators and global device state. The vendor's documentation
 * refers to these sections as "main", "sub", "bar", and "icon".
 *
 * It's essential to understand that the serial-dmm API is only able to
 * communicate a single float value (including its precision and quantity
 * details) in a single parse call. Which is why we keep a channel index
 * in the 'info' structure, and run the parse routine several times upon
 * reception of a single packet. This approach is shared with the metex14
 * parser.
 *
 * The parse routine here differs from other DMM parsers which typically
 * are split into routines which parse a value (get a number and exponent),
 * parse flags, and handle flags which were parsed before. The 121GW
 * meter's packets don't fit this separation naturally, getting the value
 * and related flags heavily depends on which display shall get inspected,
 * thus should be done at the same time. Filling in an 'info' structure
 * from packet content first, and mapping this 'info' to the 'analog'
 * details then still is very useful for maintainability.
 *
 * TODO:
 * - The meter is feature packed. This implementation does support basic
 *   operation (voltage, current, power, resistance, continuity, diode,
 *   capacitance, temperature). Support for remaining modes, previously
 *   untested ranges, and advanced features (DC+AC, VA power, dB gain,
 *   burden voltage) may be missing or incomplete. Ranges support and
 *   value scaling should be considered "under development" in general
 *   until test coverage was increased. Some flags are not evaluated
 *   correctly yet, or not at all (min/max/avg, memory).
 * - Test previously untested modes: current, power, gain, sub display
 *   modes. Test untested ranges (voltage above 30V, temperature above
 *   30deg (into the hundreds), negative temperatures, large resistors,
 *   large capacitors). Test untested features (min/max/avg, 1ms peak,
 *   log memory).
 * - It's assumed that a continuous data stream was arranged for. This
 *   implementation does not support the "packet request" API. Also I
 *   was to understand that once the request was sent (write 0300 to
 *   handle 9, after connecting) no further request is needed. Only
 *   the loss of communication may need recovery, which we leave as an
 *   option for later improvement, or as a feature of an external helper
 *   which feeds the COM port from Bluetooth communication data, or
 *   abstracts away the BLE communication.
 *
 * Implementation notes:
 * - Yes some ranges seem duplicate but that's fine. The meter's packets
 *   do provide multiple range indices for some of the modes which do
 *   communicate values in the same range of values.
 * - Some of the packet's bits don't match the available documentation.
 *   Some of the meter's features are not available to the PC side by
 *   means of inspecting packets.
 *   - Bit 5 of "bar value" was seen with value 1 in FREQ and OHM:
 *     f2  17 84 21 21  08 00 00 00  64 01 01 17  12 37  02 40 00  7d
 *     So we keep the test around but accept when it fails.
 *   - The "gotta beep" activity of continuity/break test mode is not
 *     available in the packets.
 * - The interpretation of range indices depends on the specific mode
 *   (meter's function, and range when selectable by the user like mV).
 *   As does the precision of results.
 */

#include "config.h"
#include <ctype.h>
#include <glib.h>
#include <math.h>
#include <string.h>
#include <strings.h>
#include "libsigrok/libsigrok.h"
#include "libsigrok-internal.h"

#define LOG_PREFIX "eev121gw"

/*
 * TODO:
 * When these bit field extraction helpers move to some common location,
 * their names may need adjustment to reduce the potential for conflicts.
 */
// #define BIT(n) (1UL << (n))
#define MASK(len) ((1UL << (len)) - 1)
#define FIELD_PL(v, pos, len) (((v) >> (pos)) & MASK(len))
#define FIELD_NL(v, name) FIELD_PL(v, POS_ ## name, LEN_ ## name)
#define FIELD_NB(v, name) FIELD_PL(v, POS_ ## name, 1)

/*
 * Support compile time checks for expected sizeof() results etc, like
 *   STATIC_ASSERT(sizeof(struct packet) == 19, "packet size");
 * Probably should go to some common location.
 * See http://www.pixelbeat.org/programming/gcc/static_assert.html for details.
 */
#define ASSERT_CONCAT_(a, b) a ## b
#define ASSERT_CONCAT(a, b) ASSERT_CONCAT_(a, b)
/* These can't be used after statements in c89. */
#ifdef __COUNTER__
  #define STATIC_ASSERT(e, m) \
    ; enum { ASSERT_CONCAT(static_assert_, __COUNTER__) = 1 / (int)(!!(e)) }
#else
  /*
   * This can't be used twice on the same line so ensure if using in headers
   * that the headers are not included twice (by wrapping in #ifndef...#endif).
   * Note it doesn't cause an issue when used on same line of separate modules
   * compiled with gcc -combine -fwhole-program.
   */
  #define STATIC_ASSERT(e, m) \
    ; enum { ASSERT_CONCAT(assert_line_, __LINE__) = 1 / (int)(!!(e)) }
#endif

/*
 * Symbolic identifiers for access to the packet's payload. "Offsets"
 * address bytes within the packet. "Positions" specify the (lowest)
 * bit number of a field, "lengths" specify the fields' number of bits.
 * "Values" specify magic values or fixed content (SBZ, RSV, etc).
 */
enum eev121gw_packet_offs {
	OFF_START_CMD,
#define  VAL_START_CMD		0xf2
	OFF_SERIAL_3,
	OFF_SERIAL_2,
	OFF_SERIAL_1,
	OFF_SERIAL_0,
#define  POS_SERIAL_YEAR	24
#define  LEN_SERIAL_YEAR	8
#define  POS_SERIAL_MONTH	20
#define  LEN_SERIAL_MONTH	4
#define  POS_SERIAL_NUMBER	0
#define  LEN_SERIAL_NUMBER	20
	OFF_MAIN_MODE,
#define  POS_MAIN_MODE_VAL_U	6
#define  LEN_MAIN_MODE_VAL_U	2
#define  POS_MAIN_MODE_RSV_5	5
#define  POS_MAIN_MODE_MODE	0
#define  LEN_MAIN_MODE_MODE	5
	OFF_MAIN_RANGE,
#define  POS_MAIN_RANGE_OFL	7
#define  POS_MAIN_RANGE_SIGN	6
#define  POS_MAIN_RANGE_DEGC	5
#define  POS_MAIN_RANGE_DEGF	4
#define  POS_MAIN_RANGE_RANGE	0
#define  LEN_MAIN_RANGE_RANGE	4
	OFF_MAIN_VAL_H,
	OFF_MAIN_VAL_L,
	OFF_SUB_MODE,
#define  POS_SUB_MODE_MODE	0
#define  LEN_SUB_MODE_MODE	8
	OFF_SUB_RANGE,
#define  POS_SUB_RANGE_OFL	7
#define  POS_SUB_RANGE_SIGN	6
#define  POS_SUB_RANGE_K	5
#define  POS_SUB_RANGE_HZ	4
#define  POS_SUB_RANGE_RSV_3	3
#define  POS_SUB_RANGE_POINT	0
#define  LEN_SUB_RANGE_POINT	3
	OFF_SUB_VAL_H,
	OFF_SUB_VAL_L,
	OFF_BAR_STATUS,
#define  POS_BAR_STATUS_RSV_5	5
#define  LEN_BAR_STATUS_RSV_5	3
#define  POS_BAR_STATUS_USE	4
#define  POS_BAR_STATUS_150	3
#define  POS_BAR_STATUS_SIGN	2
#define  POS_BAR_STATUS_1K_500	0
#define  LEN_BAR_STATUS_1K_500	2
	OFF_BAR_VALUE,
#define  POS_BAR_VALUE_RSV_6	6
#define  LEN_BAR_VALUE_RSV_6	2
#define  POS_BAR_VALUE_RSV_5	5
#define  POS_BAR_VALUE_VALUE	0
#define  LEN_BAR_VALUE_VALUE	5
	OFF_ICON_STS_1,
#define  POS_ICON_STS1_DEGC	7
#define  POS_ICON_STS1_1KHZ	6
#define  POS_ICON_STS1_1MSPK	5
#define  POS_ICON_STS1_DCAC	3
#define  LEN_ICON_STS1_DCAC	2
#define  POS_ICON_STS1_AUTO	2
#define  POS_ICON_STS1_APO	1
#define  POS_ICON_STS1_BAT	0
	OFF_ICON_STS_2,
#define  POS_ICON_STS2_DEGF	7
#define  POS_ICON_STS2_BT	6
#define  POS_ICON_STS2_UNK	5 /* TODO: What is this flag? 20mA loop current? */
#define  POS_ICON_STS2_REL	4
#define  POS_ICON_STS2_DBM	3
#define  POS_ICON_STS2_MINMAX	0 /* TODO: How to interpret the 3-bit field? */
#define  LEN_ICON_STS2_MINMAX	3
	OFF_ICON_STS_3,
#define  POS_ICON_STS3_RSV_7	7
#define  POS_ICON_STS3_TEST	6
#define  POS_ICON_STS3_MEM	4 /* TODO: How to interpret the 2-bit field? */
#define  LEN_ICON_STS3_MEM	2
#define  POS_ICON_STS3_AHOLD	2
#define  LEN_ICON_STS3_AHOLD	2
#define  POS_ICON_STS3_AC	1
#define  POS_ICON_STS3_DC	0
	OFF_CHECKSUM,
	/* This is not an offset, but the packet's "byte count". */
	PACKET_LAST_OFF,
};

STATIC_ASSERT(PACKET_LAST_OFF == EEV121GW_PACKET_SIZE,
	"byte offsets vs packet length mismatch");

enum mode_codes {
	/* Modes for 'main' and 'sub' displays. */
	MODE_LOW_Z = 0,
	MODE_DC_V = 1,
	MODE_AC_V = 2,
	MODE_DC_MV = 3,
	MODE_AC_MV = 4,
	MODE_TEMP = 5,
	MODE_FREQ = 6,
	MODE_PERIOD = 7,
	MODE_DUTY = 8,
	MODE_RES = 9,
	MODE_CONT = 10,
	MODE_DIODE = 11,
	MODE_CAP = 12,
	MODE_AC_UVA = 13,
	MODE_AC_MVA = 14,
	MODE_AC_VA = 15,
	MODE_AC_UA = 16,
	MODE_DC_UA = 17,
	MODE_AC_MA = 18,
	MODE_DC_MA = 19,
	MODE_AC_A = 20,
	MODE_DC_A = 21,
	MODE_DC_UVA = 22,
	MODE_DC_MVA = 23,
	MODE_DC_VA = 24,
	/* More modes for 'sub' display. */
	MODE_SUB_TEMPC = 100,
	MODE_SUB_TEMPF = 105,
	MODE_SUB_BATT = 110,
	MODE_SUB_APO_ON = 120,
	MODE_SUB_APO_OFF = 125,
	MODE_SUB_YEAR = 130,
	MODE_SUB_DATE = 135,
	MODE_SUB_TIME = 140,
	MODE_SUB_B_VOLT = 150,
	MODE_SUB_LCD = 160,
	MODE_SUB_CONT_PARM_0 = 170,
	MODE_SUB_CONT_PARM_1 = 171,
	MODE_SUB_CONT_PARM_2 = 172,
	MODE_SUB_CONT_PARM_3 = 173,
	MODE_SUB_DBM = 180,
	MODE_SUB_IVAL = 190,
};

enum range_codes {
	RANGE_0,
	RANGE_1,
	RANGE_2,
	RANGE_3,
	RANGE_4,
	RANGE_5,
	RANGE_6,
	RANGE_MAX,
};

enum bar_range_codes {
	BAR_RANGE_5,
	BAR_RANGE_50,
	BAR_RANGE_500,
	BAR_RANGE_1000,
};
#define BAR_VALUE_MAX 25

enum acdc_codes {
	ACDC_NONE,
	ACDC_DC,
	ACDC_AC,
	ACDC_ACDC,
};

SR_PRIV const char *eev121gw_channel_formats[EEV121GW_DISPLAY_COUNT] = {
	/*
 	 * TODO:
	 * The "main", "sub", "bar" names were taken from the packet
	 * description. Will users prefer "primary", "secondary", and
	 * "bargraph" names? Or even-length "pri", "sec", "bar" instead?
	 */
	"main", "sub", "bar",
};

/*
 * See page 69 in the 2018-09-24 manual for a table of modes and their
 * respective ranges ("Calibration Reference Table"). This is the input
 * to get the number of significant digits, and the decimal's position.
 */
struct mode_range_item {
	const char *desc; /* Description, for diagnostics. */
	int digits; /* Number of significant digits, see @ref sr_analog_encoding. */
	int factor; /* Factor to convert the uint to a float. */
};

struct mode_range_items {
	size_t range_count;
	const struct mode_range_item ranges[RANGE_MAX];
};

static const struct mode_range_items mode_ranges_lowz = {
	.range_count = 1,
	.ranges = {
		{ .desc =  "600.0V", .digits = 1, .factor = 1, },
	},
};

static const struct mode_range_items mode_ranges_volts = {
	.range_count = 4,
	.ranges = {
		{ .desc = "5.0000V", .digits = 4, .factor = 4, },
		{ .desc = "50.000V", .digits = 3, .factor = 3, },
		{ .desc = "500.00V", .digits = 2, .factor = 2, },
		{ .desc =  "600.0V", .digits = 1, .factor = 1, },
	},
};

static const struct mode_range_items mode_ranges_millivolts = {
	.range_count = 2,
	.ranges = {
		{ .desc = "50.000mV", .digits = 6, .factor = 6, },
		{ .desc = "500.00mV", .digits = 5, .factor = 5, },
	},
};

static const struct mode_range_items mode_ranges_temp = {
	.range_count = 1,
	.ranges = {
		{ .desc = "-200.0C ~ 1350.0C", .digits = 1, .factor = 1, },
	},
};

static const struct mode_range_items mode_ranges_freq = {
	.range_count = 5,
	.ranges = {
		{ .desc = "99.999Hz", .digits = 3, .factor = 3, },
		{ .desc = "999.99Hz", .digits = 2, .factor = 2, },
		{ .desc = "9.9999kHz", .digits = 1, .factor = 1, },
		{ .desc = "99.999kHz", .digits = 0, .factor = 0, },
		{ .desc = "999.99kHz", .digits = -1, .factor = -1, },
	},
};

static const struct mode_range_items mode_ranges_period = {
	.range_count = 3,
	.ranges = {
		{ .desc = "9.9999ms", .digits = 7, .factor = 7, },
		{ .desc = "99.999ms", .digits = 6, .factor = 6, },
		{ .desc = "999.99ms", .digits = 5, .factor = 5, },
	},
};

static const struct mode_range_items mode_ranges_duty = {
	.range_count = 1,
	.ranges = {
		{ .desc = "99.9%", .digits = 1, .factor = 1, },
	},
};

static const struct mode_range_items mode_ranges_res = {
	.range_count = 7,
	.ranges = {
		{ .desc = "50.000R", .digits = 3, .factor = 3, },
		{ .desc = "500.00R", .digits = 2, .factor = 2, },
		{ .desc = "5.0000k", .digits = 1, .factor = 1, },
		{ .desc = "50.000k", .digits = 0, .factor = 0, },
		{ .desc = "500.00k", .digits = -1, .factor = -1, },
		{ .desc = "5.0000M", .digits = -2, .factor = -2, },
		{ .desc = "50.000M", .digits = -3, .factor = -3, },
	},
};

static const struct mode_range_items mode_ranges_cont = {
	.range_count = 1,
	.ranges = {
		{ .desc = "500.00R", .digits = 2, .factor = 2, },
	},
};

static const struct mode_range_items mode_ranges_diode = {
	.range_count = 2,
	.ranges = {
		{ .desc = "3.0000V", .digits = 4, .factor = 4, },
		{ .desc = "15.000V", .digits = 3, .factor = 3, },
	},
};

static const struct mode_range_items mode_ranges_cap = {
	.range_count = 6,
	.ranges = {
		{ .desc =  "10.00n", .digits = 11, .factor = 11, },
		{ .desc =  "100.0n", .digits = 10, .factor = 10, },
		{ .desc =  "1.000u", .digits = 9, .factor = 9, },
		{ .desc =  "10.00u", .digits = 8, .factor = 8, },
		{ .desc =  "100.0u", .digits = 7, .factor = 7, },
		{ .desc =  "10.00m", .digits = 5, .factor = 5, },
	},
};

static const struct mode_range_items mode_ranges_pow_va = {
	.range_count = 4,
	.ranges = {
		{ .desc = "2500.0mVA", .digits = 4, .factor = 4, },
		{ .desc = "25000.mVA", .digits = 3, .factor = 3, },
		{ .desc = "25.000VA", .digits = 3, .factor = 3, },
		{ .desc = "500.00VA", .digits = 2, .factor = 2, },
	},
};

static const struct mode_range_items mode_ranges_pow_mva = {
	.range_count = 4,
	.ranges = {
		{ .desc = "25.000mVA", .digits = 6, .factor = 6, },
		{ .desc = "250.00mVA", .digits = 5, .factor = 5, },
		{ .desc = "250.00mVA", .digits = 5, .factor = 5, },
		{ .desc = "2500.0mVA", .digits = 4, .factor = 4, },
	},
};

static const struct mode_range_items mode_ranges_pow_uva = {
	.range_count = 4,
	.ranges = {
		{ .desc = "250.00uVA", .digits = 8, .factor = 8, },
		{ .desc = "2500.0uVA", .digits = 7, .factor = 7, },
		{ .desc = "2500.0uVA", .digits = 7, .factor = 7, },
		{ .desc = "25000.uVA", .digits = 6, .factor = 6, },
	},
};

static const struct mode_range_items mode_ranges_curr_a = {
	.range_count = 3,
	.ranges = {
		{ .desc = "500.00mA", .digits = 5, .factor = 5, },
		{ .desc = "5.0000A", .digits = 4, .factor = 4, },
		{ .desc = "10.000A", .digits = 3, .factor = 3, },
	},
};

static const struct mode_range_items mode_ranges_curr_ma = {
	.range_count = 2,
	.ranges = {
		{ .desc = "5.0000mA", .digits = 7, .factor = 7, },
		{ .desc = "50.000mA", .digits = 6, .factor = 6, },
	},
};

static const struct mode_range_items mode_ranges_curr_ua = {
	.range_count = 2,
	.ranges = {
		{ .desc = "50.000uA", .digits = 9, .factor = 9, },
		{ .desc = "500.00uA", .digits = 8, .factor = 8, },
	},
};

static const struct mode_range_items *mode_ranges_main[] = {
	[MODE_LOW_Z] = &mode_ranges_lowz,
	[MODE_DC_V] = &mode_ranges_volts,
	[MODE_AC_V] = &mode_ranges_volts,
	[MODE_DC_MV] = &mode_ranges_millivolts,
	[MODE_AC_MV] = &mode_ranges_millivolts,
	[MODE_TEMP] = &mode_ranges_temp,
	[MODE_FREQ] = &mode_ranges_freq,
	[MODE_PERIOD] = &mode_ranges_period,
	[MODE_DUTY] = &mode_ranges_duty,
	[MODE_RES] = &mode_ranges_res,
	[MODE_CONT] = &mode_ranges_cont,
	[MODE_DIODE] = &mode_ranges_diode,
	[MODE_CAP] = &mode_ranges_cap,
	[MODE_DC_VA] = &mode_ranges_pow_va,
	[MODE_AC_VA] = &mode_ranges_pow_va,
	[MODE_DC_MVA] = &mode_ranges_pow_mva,
	[MODE_AC_MVA] = &mode_ranges_pow_mva,
	[MODE_DC_UVA] = &mode_ranges_pow_uva,
	[MODE_AC_UVA] = &mode_ranges_pow_uva,
	[MODE_DC_A] = &mode_ranges_curr_a,
	[MODE_AC_A] = &mode_ranges_curr_a,
	[MODE_DC_MA] = &mode_ranges_curr_ma,
	[MODE_AC_MA] = &mode_ranges_curr_ma,
	[MODE_DC_UA] = &mode_ranges_curr_ua,
	[MODE_AC_UA] = &mode_ranges_curr_ua,
};

/*
 * The secondary display encodes SI units / scaling differently from the
 * main display, and fewer ranges are available. So we share logic between
 * displays for scaling, but have to keep separate tables for the displays.
 */

static const struct mode_range_items mode_ranges_temp_sub = {
	.range_count = 2,
	.ranges = {
		[1] = { .desc = "sub 100.0C", .digits = 1, .factor = 1, },
	},
};

static const struct mode_range_items mode_ranges_freq_sub = {
	.range_count = 4,
	.ranges = {
		[1] = { .desc = "999.9Hz", .digits = 1, .factor = 1, },
		[2] = { .desc = "99.99Hz", .digits = 2, .factor = 2, },
		[3] = { .desc = "9.999kHz", .digits = 3, .factor = 3, },
	},
};

static const struct mode_range_items mode_ranges_batt_sub = {
	.range_count = 2,
	.ranges = {
		[1] = { .desc = "sub 10.0V", .digits = 1, .factor = 1, },
	},
};

static const struct mode_range_items mode_ranges_gain_sub = {
	.range_count = 4,
	.ranges = {
		[1] = { .desc = "dbm 5000.0dBm", .digits = 1, .factor = 1, },
		[2] = { .desc = "dbm 500.00dBm", .digits = 2, .factor = 2, },
		[3] = { .desc = "dbm 50.000dBm", .digits = 3, .factor = 3, },
	},
};

static const struct mode_range_items mode_ranges_diode_sub = {
	.range_count = 1,
	.ranges = {
		[0] = { .desc = "diode 15.0V", .digits = 0, .factor = 0, },
	},
};

static const struct mode_range_items mode_ranges_volts_sub = {
	.range_count = 5,
	.ranges = {
		[3] = { .desc = "50.000V", .digits = 3, .factor = 3, },
		[4] = { .desc = "5.0000V", .digits = 4, .factor = 4, },
	},
};

static const struct mode_range_items mode_ranges_mamps_sub = {
	.range_count = 5,
	.ranges = {
		[2] = { .desc = "500.00mA", .digits = 5, .factor = 5, },
		[3] = { .desc = "50.000mA", .digits = 6, .factor = 6, },
		[4] = { .desc = "5.0000mA", .digits = 7, .factor = 7, },
	},
};

static const struct mode_range_items mode_ranges_uamps_sub = {
	.range_count = 5,
	.ranges = {
		[4] = { .desc = "5.0000mA", .digits = 7, .factor = 7, },
	},
};

static const struct mode_range_items *mode_ranges_sub[] = {
	[MODE_DC_V] = &mode_ranges_volts_sub,
	[MODE_AC_V] = &mode_ranges_volts_sub,
	[MODE_DC_A] = &mode_ranges_mamps_sub,
	[MODE_AC_A] = &mode_ranges_mamps_sub,
	[MODE_DC_MA] = &mode_ranges_mamps_sub,
	[MODE_AC_MA] = &mode_ranges_mamps_sub,
	[MODE_DC_UA] = &mode_ranges_uamps_sub,
	[MODE_AC_UA] = &mode_ranges_uamps_sub,
	[MODE_FREQ] = &mode_ranges_freq_sub,
	[MODE_DIODE] = &mode_ranges_diode_sub,
	[MODE_SUB_TEMPC] = &mode_ranges_temp_sub,
	[MODE_SUB_TEMPF] = &mode_ranges_temp_sub,
	[MODE_SUB_BATT] = &mode_ranges_batt_sub,
	[MODE_SUB_DBM] = &mode_ranges_gain_sub,
};

static const struct mode_range_item *mode_range_get_scale(
	enum eev121gw_display display,
	enum mode_codes mode, enum range_codes range)
{
	const struct mode_range_items *items;
	const struct mode_range_item *item;

	if (display == EEV121GW_DISPLAY_MAIN) {
		if (mode >= ARRAY_SIZE(mode_ranges_main))
			return NULL;
		items = mode_ranges_main[mode];
		if (!items || !items->range_count)
			return NULL;
		if (range >= items->range_count)
			return NULL;
		item = &items->ranges[range];
		return item;
	}
	if (display == EEV121GW_DISPLAY_SUB) {
		if (mode >= ARRAY_SIZE(mode_ranges_sub))
			return NULL;
		items = mode_ranges_sub[mode];
		if (!items || !items->range_count)
			return NULL;
		if (range >= items->range_count)
			return NULL;
		item = &items->ranges[range];
		if (!item->desc || !*item->desc)
			return NULL;
		return item;
	}

	return NULL;
}

SR_PRIV gboolean sr_eev121gw_packet_valid(const uint8_t *buf)
{
	uint8_t csum;
	size_t idx;

	/* Leading byte, literal / fixed value. */
	if (buf[OFF_START_CMD] != VAL_START_CMD)
		return FALSE;

	/* Check some always-zero bits in reserved locations. */
	if (FIELD_NB(buf[OFF_MAIN_MODE], MAIN_MODE_RSV_5))
		return FALSE;
	if (FIELD_NB(buf[OFF_SUB_RANGE], SUB_RANGE_RSV_3))
		return FALSE;
	if (FIELD_NL(buf[OFF_BAR_STATUS], BAR_STATUS_RSV_5))
		return FALSE;
	if (FIELD_NL(buf[OFF_BAR_VALUE], BAR_VALUE_RSV_6))
		return FALSE;
	/* See TODO for bit 5 of "bar value" not always being 0. */
	if (0 && FIELD_NB(buf[OFF_BAR_VALUE], BAR_VALUE_RSV_5))
		return FALSE;
	if (FIELD_NB(buf[OFF_ICON_STS_3], ICON_STS3_RSV_7))
		return FALSE;

	/* Checksum, XOR over all previous bytes. */
	csum = 0x00;
	for (idx = OFF_START_CMD; idx < OFF_CHECKSUM; idx++)
		csum ^= buf[idx];
	if (csum != buf[OFF_CHECKSUM]) {
		/* Non-critical condition, almost expected to see invalid data. */
		sr_spew("Packet csum: want %02x, got %02x.", csum, buf[OFF_CHECKSUM]);
		return FALSE;
	}

	sr_spew("Packet valid.");

	return TRUE;
}

/**
 * Parse a protocol packet.
 *
 * @param[in] buf Buffer containing the protocol packet. Must not be NULL.
 * @param[out] floatval Pointer to a float variable. That variable will be
 *             modified in-place depending on the protocol packet.
 *             Must not be NULL.
 * @param[out] analog Pointer to a struct sr_datafeed_analog. The struct will
 *             be filled with data according to the protocol packet.
 *             Must not be NULL.
 * @param[out] info Pointer to a struct eevblog_121gw_info. The struct will be
 *             filled with data according to the protocol packet.
 *             Must not be NULL.
 *
 * @return SR_OK upon success, SR_ERR upon failure. Upon errors, the
 *         'analog' variable contents are undefined and should not be used.
 */
SR_PRIV int sr_eev121gw_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info)
{
	struct eev121gw_info *info_local;
	enum eev121gw_display display;
	const char *channel_name;
	uint32_t raw_serial;
	uint8_t raw_main_mode, raw_main_range;
	uint16_t raw_main_value;
	uint8_t raw_sub_mode, raw_sub_range;
	uint16_t raw_sub_value;
	uint8_t raw_bar_status, raw_bar_value;
	uint8_t raw_icon_stat_1, raw_icon_stat_2, raw_icon_stat_3;
	uint32_t uint_value;
	enum mode_codes main_mode;
	enum range_codes main_range;
	enum mode_codes sub_mode;
	enum range_codes sub_range;
	const struct mode_range_item *scale;
	gboolean is_dc, is_sign, use_sign;
	gboolean is_k;
	unsigned int cont_code;

	info_local = info;
	display = info_local->ch_idx;
	channel_name = eev121gw_channel_formats[display];
	memset(info_local, 0, sizeof(*info_local));
	*floatval = 0.0f;

	/*
	 * Get the packet's bytes into native C language typed variables.
	 * This keeps byte position references out of logic/calculations.
	 * The start command and CRC were verified before we get here.
	 */
	raw_serial = RB32(&buf[OFF_SERIAL_3]);
	raw_main_mode = R8(&buf[OFF_MAIN_MODE]);
	raw_main_range = R8(&buf[OFF_MAIN_RANGE]);
	raw_main_value = RB16(&buf[OFF_MAIN_VAL_H]);
	raw_sub_mode = R8(&buf[OFF_SUB_MODE]);
	raw_sub_range = R8(&buf[OFF_SUB_RANGE]);
	raw_sub_value = RB16(&buf[OFF_SUB_VAL_H]);
	raw_bar_status = R8(&buf[OFF_BAR_STATUS]);
	raw_bar_value = R8(&buf[OFF_BAR_VALUE]);
	raw_icon_stat_1 = R8(&buf[OFF_ICON_STS_1]);
	raw_icon_stat_2 = R8(&buf[OFF_ICON_STS_2]);
	raw_icon_stat_3 = R8(&buf[OFF_ICON_STS_3]);

	/*
	 * Packets contain a YEAR-MONTH date spec. It's uncertain how
	 * this data relates to the device's production or the firmware
	 * version. It certainly is not the current date either. Only
	 * optionally log this information, it's consistent across all
	 * packets (won't change within a session), and will be noisy if
	 * always enabled.
	 *
	 * Packets also contain a user adjustable device identification
	 * number (see the SETUP options). This is motivated by support
	 * for multiple devices, but won't change here within a session.
	 * The user chose to communicate to one specific device when the
	 * session started, by means of the conn= spec.
	 *
	 * It was suggested that this 'serial' field might be used as an
	 * additional means to check for a packet's validity (or absence
	 * of communication errors). This remains as an option for future
	 * improvement.
	 */
	if (0) {
		unsigned int ser_year, ser_mon, ser_nr;

		ser_year = FIELD_NL(raw_serial, SERIAL_YEAR);
		ser_mon = FIELD_NL(raw_serial, SERIAL_MONTH);
		ser_nr = FIELD_NL(raw_serial, SERIAL_NUMBER);
		sr_spew("Packet: Y-M %x-%x, nr %x.", ser_year, ser_mon, ser_nr);
	}

	switch (display) {

	case EEV121GW_DISPLAY_MAIN:
		/*
		 * Get those fields which correspond to the main display.
		 * The value's mantissa has 18 bits. The sign is separate
		 * (and is not universally applicable, mode needs to get
		 * inspected). The range's scaling and precision also
		 * depend on the mode.
		 */
		main_mode = FIELD_NL(raw_main_mode, MAIN_MODE_MODE);
		main_range = FIELD_NL(raw_main_range, MAIN_RANGE_RANGE);
		scale = mode_range_get_scale(EEV121GW_DISPLAY_MAIN,
			main_mode, main_range);
		if (!scale)
			return SR_ERR_NA;
		info_local->factor = scale->factor;
		info_local->digits = scale->digits;

		uint_value = raw_main_value;
		uint_value |= FIELD_NL(raw_main_mode, MAIN_MODE_VAL_U) << 16;
		info_local->uint_value = uint_value;
		info_local->is_ofl = FIELD_NB(raw_main_range, MAIN_RANGE_OFL);

		switch (main_mode) {
		case MODE_LOW_Z:
			is_dc = FALSE;
			if (FIELD_NB(raw_icon_stat_3, ICON_STS3_DC))
				is_dc = TRUE;
			if (FIELD_NB(raw_icon_stat_3, ICON_STS3_AC))
				is_dc = FALSE;
			use_sign = is_dc;
			break;
		case MODE_DC_V:
		case MODE_DC_MV:
		case MODE_TEMP:
		case MODE_DC_UVA:
		case MODE_DC_MVA:
		case MODE_DC_VA:
		case MODE_DC_UA:
		case MODE_DC_MA:
		case MODE_DC_A:
			use_sign = TRUE;
			break;
		default:
			use_sign = FALSE;
			break;
		}
		if (use_sign) {
			is_sign = FIELD_NB(raw_main_range, MAIN_RANGE_SIGN);
			info_local->is_neg = is_sign;
		}

		switch (main_mode) {
		case MODE_LOW_Z:
			info_local->is_voltage = TRUE;
			/* TODO: Need to determine AC/DC here? */
			info_local->is_volt = TRUE;
			info_local->is_low_pass = TRUE;
			break;
		case MODE_DC_V:
			info_local->is_voltage = TRUE;
			info_local->is_dc = TRUE;
			info_local->is_volt = TRUE;
			break;
		case MODE_AC_V:
			info_local->is_voltage = TRUE;
			info_local->is_volt = TRUE;
			info_local->is_ac = TRUE;
			break;
		case MODE_DC_MV:
			info_local->is_voltage = TRUE;
			info_local->is_dc = TRUE;
			info_local->is_volt = TRUE;
			break;
		case MODE_AC_MV:
			info_local->is_voltage = TRUE;
			info_local->is_volt = TRUE;
			info_local->is_ac = TRUE;
			break;
		case MODE_TEMP:
			info_local->is_temperature = TRUE;
			if (FIELD_NB(raw_main_range, MAIN_RANGE_DEGC))
				info_local->is_celsius = TRUE;
			if (FIELD_NB(raw_main_range, MAIN_RANGE_DEGF))
				info_local->is_fahrenheit = TRUE;
			break;
		case MODE_FREQ:
			info_local->is_frequency = TRUE;
			info_local->is_hertz = TRUE;
			break;
		case MODE_PERIOD:
			info_local->is_period = TRUE;
			info_local->is_seconds = TRUE;
			break;
		case MODE_DUTY:
			info_local->is_duty_cycle = TRUE;
			info_local->is_percent = TRUE;
			break;
		case MODE_RES:
			info_local->is_resistance = TRUE;
			info_local->is_ohm = TRUE;
			break;
		case MODE_CONT:
			info_local->is_continuity = TRUE;
			info_local->is_ohm = TRUE;
			/*
			 * In continuity mode the packet provides the
			 * resistance in ohms (500R range), but seems to
			 * _not_ carry the "boolean" open/closed state
			 * which controls the beeper. Users can select
			 * whether to trigger at 30R or 300R, and whether
			 * to trigger on values below (continuity) or
			 * above (cable break) the limit, but we cannot
			 * tell what the currently used setting is. So
			 * we neither get the beeper's state, nor can we
			 * derive it from other information.
			 */
			break;
		case MODE_DIODE:
			info_local->is_diode = TRUE;
			info_local->is_dc = TRUE;
			info_local->is_volt = TRUE;
			break;
		case MODE_CAP:
			info_local->is_capacitance = TRUE;
			info_local->is_farad = TRUE;
			break;
		case MODE_AC_UVA:
			info_local->is_power = TRUE;
			info_local->is_ac = TRUE;
			info_local->is_volt_ampere = TRUE;
			break;
		case MODE_AC_MVA:
			info_local->is_power = TRUE;
			info_local->is_ac = TRUE;
			info_local->is_volt_ampere = TRUE;
			break;
		case MODE_AC_VA:
			info_local->is_power = TRUE;
			info_local->is_ac = TRUE;
			info_local->is_volt_ampere = TRUE;
			break;
		case MODE_AC_UA:
			info_local->is_current = TRUE;
			info_local->is_ac = TRUE;
			info_local->is_ampere = TRUE;
			break;
		case MODE_DC_UA:
			info_local->is_current = TRUE;
			info_local->is_dc = TRUE;
			info_local->is_ampere = TRUE;
			break;
		case MODE_AC_MA:
			info_local->is_current = TRUE;
			info_local->is_ac = TRUE;
			info_local->is_ampere = TRUE;
			break;
		case MODE_DC_MA:
			info_local->is_current = TRUE;
			info_local->is_dc = TRUE;
			info_local->is_ampere = TRUE;
			break;
		case MODE_AC_A:
			info_local->is_current = TRUE;
			info_local->is_ac = TRUE;
			info_local->is_ampere = TRUE;
			break;
		case MODE_DC_A:
			info_local->is_current = TRUE;
			info_local->is_dc = TRUE;
			info_local->is_ampere = TRUE;
			break;
		case MODE_DC_UVA:
			info_local->is_power = TRUE;
			info_local->is_dc = TRUE;
			info_local->is_volt_ampere = TRUE;
			break;
		case MODE_DC_MVA:
			info_local->is_power = TRUE;
			info_local->is_dc = TRUE;
			info_local->is_volt_ampere = TRUE;
			break;
		case MODE_DC_VA:
			info_local->is_power = TRUE;
			info_local->is_dc = TRUE;
			info_local->is_volt_ampere = TRUE;
			break;
		/* Modes 100-199 only apply to the secondary display. */
		default:
			return SR_ERR_NA;
		}

		/*
		 * Inspect the "icons" section, since it is associated
		 * with the primary display and global device state.
		 */
		if (FIELD_NB(raw_icon_stat_1, ICON_STS1_1KHZ))
			info_local->is_low_pass = TRUE;
		if (FIELD_NB(raw_icon_stat_1, ICON_STS1_1MSPK))
			info_local->is_1ms_peak = TRUE;
		switch (FIELD_NL(raw_icon_stat_1, ICON_STS1_DCAC)) {
		case ACDC_ACDC:
			info_local->is_ac = TRUE;
			info_local->is_dc = TRUE;
			break;
		case ACDC_AC:
			info_local->is_ac = TRUE;
			break;
		case ACDC_DC:
			info_local->is_dc = TRUE;
			break;
		case ACDC_NONE:
			/* EMPTY */
			break;
		}
		if (FIELD_NB(raw_icon_stat_1, ICON_STS1_AUTO))
			info_local->is_auto_range = TRUE;
		if (FIELD_NB(raw_icon_stat_1, ICON_STS1_APO))
			info_local->is_auto_poweroff = TRUE;
		if (FIELD_NB(raw_icon_stat_1, ICON_STS1_BAT))
			info_local->is_low_batt = TRUE;
		if (FIELD_NB(raw_icon_stat_2, ICON_STS2_BT))
			info_local->is_bt = TRUE;
		/* TODO: Is this the "20mA loop current" flag? */
		if (FIELD_NB(raw_icon_stat_2, ICON_STS2_UNK))
			info_local->is_loop_current = TRUE;
		if (FIELD_NB(raw_icon_stat_2, ICON_STS2_REL))
			info_local->is_rel = TRUE;
		/* dBm only applies to secondary display, not main. */
		switch (FIELD_NL(raw_icon_stat_2, ICON_STS2_MINMAX)) {
		/* TODO: Do inspect the min/max/avg flags. */
		default:
			/* EMPTY */
			break;
		}
		if (FIELD_NB(raw_icon_stat_3, ICON_STS3_TEST))
			info_local->is_test = TRUE;
		/* TODO: How to interpret the 2-bit MEM field? */
		if (FIELD_NL(raw_icon_stat_3, ICON_STS3_MEM))
			info_local->is_mem = TRUE;
		if (FIELD_NL(raw_icon_stat_3, ICON_STS3_AHOLD))
			info_local->is_hold = TRUE;
		/* TODO: Are these for the secondary display? See status-2 ACDC. */
		if (FIELD_NB(raw_icon_stat_3, ICON_STS3_AC))
			info_local->is_ac = TRUE;
		if (FIELD_NB(raw_icon_stat_3, ICON_STS3_DC))
			info_local->is_dc = TRUE;

		sr_spew("Disp '%s', value: %lu (ov %d, neg %d), mode %d, range %d.",
			channel_name,
			(unsigned long)info_local->uint_value,
			info_local->is_ofl, info_local->is_neg,
			(int)main_mode, (int)main_range);
		/* Advance to the number and units conversion below. */
		break;

	case EEV121GW_DISPLAY_SUB:
		/*
		 * Get those fields which correspond to the secondary
		 * display. The value's mantissa has 16 bits. The sign
		 * is separate is only applies to some of the modes.
		 * Scaling and precision also depend on the mode. The
		 * interpretation of the secondary display is different
		 * from the main display: The 'range' is not an index
		 * into ranges, instead it's the decimal's position.
		 * Yet more scaling depends on the mode, to complicate
		 * matters. The secondary display uses modes 100-199,
		 * and some of the 0-24 modes as well.
		 */
		sub_mode = FIELD_NL(raw_sub_mode, SUB_MODE_MODE);
		sub_range = FIELD_NL(raw_sub_range, SUB_RANGE_POINT);
		scale = mode_range_get_scale(EEV121GW_DISPLAY_SUB,
			sub_mode, sub_range);
		if (!scale)
			return SR_ERR_NA;
		info_local->factor = scale->factor;
		info_local->digits = scale->digits;

		info_local->uint_value = raw_sub_value;
		info_local->is_ofl = FIELD_NB(raw_sub_range, SUB_RANGE_OFL);

		switch (sub_mode) {
		case MODE_DC_V:
		case MODE_AC_V:
		case MODE_DC_A:
		case MODE_AC_A:
		case MODE_SUB_TEMPC:
		case MODE_SUB_TEMPF:
		case MODE_SUB_B_VOLT:
		case MODE_SUB_DBM:
			use_sign = TRUE;
			break;
		default:
			use_sign = FALSE;
			break;
		}
		if (use_sign) {
			is_sign = FIELD_NB(raw_sub_range, SUB_RANGE_SIGN);
			info_local->is_neg = is_sign;
		}
		is_k = FIELD_NB(raw_sub_range, SUB_RANGE_K);

		/*
		 * TODO: Re-check the power mode display as more data becomes
		 * available.
		 *
		 * The interpretation of the secondary display in power (VA)
		 * modes is uncertain. The mode suggests A or uA units but the
		 * value is supposed to be mA without a reliable condition
		 * for us to check...
		 *
		 * f2  17 84 21 21  18 02 00 00  01 04 00 0b  00 00  0a 40 00  3f
		 * f2  17 84 21 21  18 02 00 00  15 03 00 00  00 00  0a 40 00  27
		 *                  DC VA        DC V / DC A
		 *                  25.000VA     dot 4 / dot 3
		 *
		 * f2  17 84 21 21  18 00 00 26  01 04 4c 57  00 00  0e 40 00  0f
		 * f2  17 84 21 21  18 00 00 26  15 02 00 c7  00 00  0e 40 00  c1
		 *                  3.8mVA DC    1.9543V
		 *                                 1.98mA (!) DC A + dot 2 -> milli(!) amps?
		 *
		 * f2  17 84 21 21  17 00 07 85  01 04 4c 5a  00 00  0e 40 00  a9
		 * f2  17 84 21 21  17 00 07 85  13 04 26 7b  00 00  0e 40 00  f0
		 *                  1.925mVA DC  1.9546V
		 *                               0.9852mA
		 *
		 * f2  17 84 21 21  16 02 11 e0  01 04 26 39  00 02  0e 40 00  d2
		 * f2  17 84 21 21  16 02 11 e0  11 04 12 44  00 02  0e 40 00  8b
		 *                  457.6uVA DC  0.9785V
		 *                               0.4676mA (!) DC uA + dot 4 -> milli(!) amps?
		 */

		switch (sub_mode) {
		case MODE_DC_V:
			info_local->is_voltage = TRUE;
			info_local->is_volt = TRUE;
			break;
		case MODE_DC_A:
			info_local->is_current = TRUE;
			info_local->is_ampere = TRUE;
			break;
		case MODE_FREQ:
			info_local->is_frequency = TRUE;
			info_local->is_hertz = TRUE;
			if (is_k) {
				info_local->factor -= 3;
				info_local->digits -= 3;
			}
			info_local->is_ofl = FALSE;
			break;
		case MODE_SUB_TEMPC:
			info_local->is_temperature = TRUE;
			info_local->is_celsius = TRUE;
			break;
		case MODE_SUB_TEMPF:
			info_local->is_temperature = TRUE;
			info_local->is_fahrenheit = TRUE;
			break;
		case MODE_SUB_BATT:
			/* TODO: How to communicate it's the *battery* voltage? */
			info_local->is_voltage = TRUE;
			info_local->is_volt = TRUE;
			break;
		case MODE_SUB_DBM:
			info_local->is_gain = TRUE;
			info_local->is_dbm = TRUE;
			break;
		case MODE_SUB_CONT_PARM_0:
		case MODE_SUB_CONT_PARM_1:
		case MODE_SUB_CONT_PARM_2:
		case MODE_SUB_CONT_PARM_3:
			/*
			 * These "continuity parameters" are special. The
			 * least significant bits represent the options:
			 *
			 * 0xaa = 170 => down 30
			 * 0xab = 171 => up 30
			 * 0xac = 172 => down 300
			 * 0xad = 173 => up 300
			 *
			 * bit 0 value 0 -> close (cont)
			 * bit 0 value 1 -> open (break)
			 * bit 1 value 0 -> 30R limit
			 * bit 1 value 1 -> 300R limit
			 *
			 * This "display value" is only seen during setup
			 * but not during regular operation of continuity
			 * mode. :( In theory we could somehow pass the
			 * 30/300 ohm limit to sigrok, but that'd be of
			 * somewhat limited use.
			 */
			cont_code = sub_mode - MODE_SUB_CONT_PARM_0;
			info_local->is_resistance = TRUE;
			info_local->is_ohm = TRUE;
			info_local->uint_value = (cont_code & 0x02) ? 300 : 30;
			info_local->is_neg = FALSE;
			info_local->is_ofl = FALSE;
			info_local->factor = 0;
			info_local->digits = 0;
			break;
		case MODE_DIODE:
			/* Displays configured diode test voltage. */
			info_local->is_voltage = TRUE;
			info_local->is_volt = TRUE;
			break;

		/* Reflecting these to users seems pointless, ignore them. */
		case MODE_SUB_APO_ON:
		case MODE_SUB_APO_OFF:
		case MODE_SUB_LCD:
		case MODE_SUB_YEAR:
		case MODE_SUB_DATE:
		case MODE_SUB_TIME:
			return SR_ERR_NA;

		/* Unknown / unsupported sub display mode. */
		default:
			return SR_ERR_NA;
		}

		sr_spew("disp '%s', value: %lu (ov %d, neg %d), mode %d, range %d",
			channel_name,
			(unsigned long)info_local->uint_value,
			info_local->is_ofl, info_local->is_neg,
			(int)sub_mode, (int)sub_range);
		/* Advance to the number and units conversion below. */
		break;

	case EEV121GW_DISPLAY_BAR:
		/*
		 * Get those fields which correspond to the bargraph.
		 * There are 26 segments (ticks 0-25), several ranges
		 * apply (up to 5, or up to 10, several decades). The
		 * bargraph does not apply to all modes and ranges,
		 * hence there is a "use" flag (negative logic, blank
		 * signal). Bit 5 was also found to have undocumented
		 * values, we refuse to use the bargraph value then.
		 */
		if (FIELD_NB(raw_bar_status, BAR_STATUS_USE))
			return SR_ERR_NA;
		if (FIELD_NB(raw_bar_value, BAR_VALUE_RSV_5))
			return SR_ERR_NA;
		uint_value = FIELD_NL(raw_bar_value, BAR_VALUE_VALUE);
		if (uint_value > BAR_VALUE_MAX)
			uint_value = BAR_VALUE_MAX;
		info_local->is_neg = FIELD_NB(raw_bar_status, BAR_STATUS_SIGN);
		switch (FIELD_NL(raw_bar_status, BAR_STATUS_1K_500)) {
		case BAR_RANGE_5:
			/* Full range 5.0, in steps of 0.2 each. */
			uint_value *= 5000 / BAR_VALUE_MAX;
			info_local->factor = 3;
			info_local->digits = 1;
			break;
		case BAR_RANGE_50:
			/* Full range 50, in steps of 2 each. */
			uint_value *= 50 / BAR_VALUE_MAX;
			info_local->factor = 0;
			info_local->digits = 0;
			break;
		case BAR_RANGE_500:
			/* Full range 500, in steps of 20 each. */
			uint_value *= 500 / BAR_VALUE_MAX;
			info_local->factor = 0;
			info_local->digits = -1;
			break;
		case BAR_RANGE_1000:
			/* Full range 1000, in steps of 40 each. */
			uint_value *= 1000 / BAR_VALUE_MAX;
			info_local->factor = 0;
			info_local->digits = -1;
			break;
		default:
			return SR_ERR_NA;
		}
		info_local->uint_value = uint_value;
		info_local->is_unitless = TRUE;
		sr_spew("Disp '%s', value: %u.", channel_name,
			(unsigned int)info_local->uint_value);
		/* Advance to the number and units conversion below. */
		break;

	default:
		/* Unknown display, programmer's error, ShouldNotHappen(TM). */
		sr_err("Disp '-?-'.");
		return SR_ERR_ARG;
	}

	/*
	 * Convert the unsigned mantissa and its modifiers to a float
	 * analog value, including scale and quantity. Do the conversion
	 * first, and optionally override the result with 'inf' later.
	 * Apply the sign last so that +inf and -inf are supported.
	 */
	*floatval = info_local->uint_value;
	if (info_local->factor)
		*floatval *= powf(10, -info_local->factor);
	if (info_local->is_ofl)
		*floatval = INFINITY;
	if (info_local->is_neg)
		*floatval = -*floatval;

	analog->encoding->digits = info_local->digits;
	analog->spec->spec_digits = info_local->digits;

	/*
	 * Communicate the measured quantity.
	 */
	/* Determine the quantity itself. */
	if (info_local->is_voltage)
		analog->meaning->mq = SR_MQ_VOLTAGE;
	if (info_local->is_current)
		analog->meaning->mq = SR_MQ_CURRENT;
	if (info_local->is_power)
		analog->meaning->mq = SR_MQ_POWER;
	if (info_local->is_gain)
		analog->meaning->mq = SR_MQ_GAIN;
	if (info_local->is_resistance)
		analog->meaning->mq = SR_MQ_RESISTANCE;
	if (info_local->is_capacitance)
		analog->meaning->mq = SR_MQ_CAPACITANCE;
	if (info_local->is_diode)
		analog->meaning->mq = SR_MQ_VOLTAGE;
	if (info_local->is_temperature)
		analog->meaning->mq = SR_MQ_TEMPERATURE;
	if (info_local->is_continuity)
		analog->meaning->mq = SR_MQ_CONTINUITY;
	if (info_local->is_frequency)
		analog->meaning->mq = SR_MQ_FREQUENCY;
	if (info_local->is_period)
		analog->meaning->mq = SR_MQ_TIME;
	if (info_local->is_duty_cycle)
		analog->meaning->mq = SR_MQ_DUTY_CYCLE;
	if (info_local->is_unitless)
		analog->meaning->mq = SR_MQ_COUNT;
	/* Add AC / DC / DC+AC flags. */
	if (info_local->is_ac)
		analog->meaning->mqflags |= SR_MQFLAG_AC;
	if (info_local->is_dc)
		analog->meaning->mqflags |= SR_MQFLAG_DC;
	/* Specify units. */
	if (info_local->is_ampere)
		analog->meaning->unit = SR_UNIT_AMPERE;
	if (info_local->is_volt)
		analog->meaning->unit = SR_UNIT_VOLT;
	if (info_local->is_volt_ampere)
		analog->meaning->unit = SR_UNIT_VOLT_AMPERE;
	if (info_local->is_dbm)
		analog->meaning->unit = SR_UNIT_DECIBEL_VOLT;
	if (info_local->is_ohm)
		analog->meaning->unit = SR_UNIT_OHM;
	if (info_local->is_farad)
		analog->meaning->unit = SR_UNIT_FARAD;
	if (info_local->is_celsius)
		analog->meaning->unit = SR_UNIT_CELSIUS;
	if (info_local->is_fahrenheit)
		analog->meaning->unit = SR_UNIT_FAHRENHEIT;
	if (info_local->is_hertz)
		analog->meaning->unit = SR_UNIT_HERTZ;
	if (info_local->is_seconds)
		analog->meaning->unit = SR_UNIT_SECOND;
	if (info_local->is_percent)
		analog->meaning->unit = SR_UNIT_PERCENTAGE;
	if (info_local->is_loop_current)
		analog->meaning->unit = SR_UNIT_PERCENTAGE;
	if (info_local->is_unitless)
		analog->meaning->unit = SR_UNIT_UNITLESS;
	if (info_local->is_logic)
		analog->meaning->unit = SR_UNIT_UNITLESS;
	/* Add other indicator flags. */
	if (info_local->is_diode) {
		analog->meaning->mqflags |= SR_MQFLAG_DIODE;
		analog->meaning->mqflags |= SR_MQFLAG_DC;
	}
	if (info_local->is_min)
		analog->meaning->mqflags |= SR_MQFLAG_MIN;
	if (info_local->is_max)
		analog->meaning->mqflags |= SR_MQFLAG_MAX;
	if (info_local->is_avg)
		analog->meaning->mqflags |= SR_MQFLAG_AVG;
	/* TODO: How to communicate info_local->is_1ms_peak? */
	if (info_local->is_rel)
		analog->meaning->mqflags |= SR_MQFLAG_RELATIVE;
	if (info_local->is_hold)
		analog->meaning->mqflags |= SR_MQFLAG_HOLD;
	/* TODO: How to communicate info_local->is_low_pass? */
	if (info_local->is_mem)	/* XXX Is REF appropriate here? */
		analog->meaning->mqflags |= SR_MQFLAG_REFERENCE;
	if (info_local->is_auto_range)
		analog->meaning->mqflags |= SR_MQFLAG_AUTORANGE;
	/* TODO: How to communicate info->is_test? What's its meaning at all? */
	/* TODO: How to communicate info->is_auto_poweroff? */
	/* TODO: How to communicate info->is_low_batt? */

	return SR_OK;
}

/*
 * Parse the same packet multiple times, to extract individual analog
 * values which correspond to several displays of the device. Make sure
 * to keep the channel index in place, even if the parse routine will
 * clear the info structure.
 */
SR_PRIV int sr_eev121gw_3displays_parse(const uint8_t *buf, float *floatval,
		struct sr_datafeed_analog *analog, void *info)
{
	struct eev121gw_info *info_local;
	size_t ch_idx;
	int rc;

	info_local = info;
	ch_idx = info_local->ch_idx;
	rc = sr_eev121gw_parse(buf, floatval, analog, info);
	info_local->ch_idx = ch_idx + 1;

	return rc;
}
