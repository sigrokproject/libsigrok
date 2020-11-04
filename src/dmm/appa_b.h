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
 *
 * APPA B Interface
 *
 * Based on APPA Communication Protocol v2.8
 *
 * Driver for modern APPA meters (handheld, bench, clamp). Communication is
 * done over a serial interface using the known APPA-Frames, see below. The
 * base protocol is always the same and deviates only where the models have
 * differences in ablities, range and features.
 *
 * Model Table
 *
 *   ID    | Model     | Brand Name            | OPT | BLE | State
 *   ------|-----------|-----------------------|:---:|:---:|---------------
 *   0x01  | 150B      | APPA 155B             |     |  X  | untested
 *   0x01  | 150B      | APPA 156B             |     |  X  | untested
 *   0x01  | 150B      | APPA 157B             |     |  X  | untested
 *   0x01  | 150B      | APPA 158B             |     |  X  | untested
 *   0x01  | 150B      | BENNING CM 12         |     |  X  | untested
 *   0x0c  | 172       | APPA 172B             |     |  X  | untested
 *   0x0d  | 172       | BENNING CM 9-2        |     |  X  | untested
 *   0x0d  | 173       | APPA 173B             |     |  X  | untested
 *   0x0d  | 173       | BENNING CM 10-1       |     |  X  | untested
 *   0x0e  | 175       | APPA 175B             |     |  X  | untested
 *   0x0f  | 177       | APPA 177B             |     |  X  | untested
 *   0x0f  | 177       | BENNING CM 10-PV      |     |  X  | untested
 *   0x14  | 179       | APPA 179B             |     |  X  | untested
 *   0x03  | 208       | APPA 208              |  X  |     | untested
 *   0x04  | 208B      | APPA 208B             |  X  |  X  | untested
 *   0x07  | 501       | APPA 501              |  X  |     | untested
 *   0x08  | 502       | APPA 502              |  X  |     | untested
 *   0x15  | 503       | APPA 503              |  X  |     | untested
 *   0x15  | 503       | CMT 3503              |  X  |     | untested
 *   0x15  | 503       | Voltcraft VC-930      |  X  |     | experimental
 *   0x15  | 503       | ISO-TECH IDM503       |  X  |     | untested
 *   0x16  | 505       | APPA 505              |  X  |     | untested
 *   0x16  | 505       | RS PRO IDM505         |  X  |     | untested
 *   0x16  | 505       | Sefram 7355           |  X  |     | untested
 *   0x16  | 505       | Voltcraft VC-950      |  X  |     | experimental
 *   0x05  | 506       | APPA 506              |  X  |     | ok
 *   0x05  | 506       | Sefram 7351           |  X  |     | ok
 *   0x06  | 506B      | APPA 506B             |  X  |  X  | ok
 *   0x06  | 506B      | BENNING MM 12         |  X  |  X  | ok
 *   0x06  | 506B      | Sefram 7352B          |  X  |  X  | ok
 *   N/A   | 507       | APPA 507              |  X  |     | experimental
 *   N/A   | 507       | CMT 3507              |  X  |     | experimental
 *   N/A   | 507       | HT Instruments HT8100 |  X  |     | experimental
 *   0x12  | A17N      | APPA A17N             |     |  X  | untested
 *   0x13  | S0        | APPA S0               |     |  X  | untested
 *   0x09  | S1        | APPA S1               |     |  X  | untested
 *   0x09  | S1        | RS PRO S1             |     |  X  | untested
 *   0x0a  | S2        | APPA S2               |     |  X  | untested
 *   0x0a  | S2        | BENNING MM 10         |     |  X  | untested
 *   0x0a  | S2        | RS PRO S2             |     |  X  | untested
 *   0x0b  | S3        | APPA S3               |     |  X  | untested
 *   0x0b  | S3        | BENNING MM 10-PV      |     |  X  | untested
 *   0x0b  | S3        | RS PRO S3             |     |  X  | untested
 *   0x10  | SFLEX_10A | APPA sFlex-10A        |     |  X  | untested
 *   0x11  | SFLEX_18A | APPA sFlex-18A        |     |  X  | untested
 *
 * BLE: Bluetooth LE, OPT: Optical serial interface
 *
 * How does it work:
 *
 * A request frame is sent to the meter, the meter will send a response frame.
 * The response frame contains all the information. The API driver provides
 * enums and string resolution functions to resolve the information and map
 * it to the according sigrok fields.
 *
 * TODOs:
 *
 * @TODO Use device name read from device
 * @TODO Implement log download
 * @TODO Implement calibration
 * @TODO Check, why sometimes invalid BLE traffic pops up (currently silently ignored)
 *
 */

#ifndef APPA_B__H
#define APPA_B__H

#include "config.h"

#include "libsigrok/libsigrok.h"

#include <ctype.h>
#include <glib.h>
#include <math.h>
#include <string.h>
#include <strings.h>

/**
 * Enable flushing before writing
 */
#define APPA_B_ENABLE_FLUSH 1

/**
 * Enable non blocking writing
 */
#define APPA_B_ENABLE_NON_BLOCKING 1

/**
 * Enable handshake information from serial
 */
/* #define APPA_B_ENABLE_OPEN_REQUEST_INFORMATION 1 */

/**
 * Timeout for blocking write operations (10Hz device means max 100ms delay)
 */
#define APPA_B_WRITE_BLOCKING_TIMEOUT 100

/**
 * Used for unavailable strings and labels
 */
#define APPA_B_STRING_NA "N/A"

/**
 * String representation of OL on display
 */
#define APPA_B_READING_TEXT_OL "OL"

/**
 * Size of frame headers
 */
#define APPA_B_FRAME_HEADER_SIZE 4

/**
 * Size of checksum
 */
#define APPA_B_FRAME_CHECKSUM_SIZE 1

/**
 * Length of Read Information Request
 */
#define APPA_B_DATA_LENGTH_REQUEST_READ_INFORMATION 0

/**
 * Length of Read Information Response
 */
#define APPA_B_DATA_LENGTH_RESPONSE_READ_INFORMATION 52

/**
 * Length of Read Display Request
 */
#define APPA_B_DATA_LENGTH_REQUEST_READ_DISPLAY 0

/**
 * Length of Read Display response
 */
#define APPA_B_DATA_LENGTH_RESPONSE_READ_DISPLAY 12

/**
 * Begin of word codes (minimum value)
 * All readings on a display higher than that are some sort of wordcode, resolvable or not
 */
#define APPA_B_WORDCODE_TABLE_MIN 0x700000

/**
 * Start code of valid frame
 */
#define APPA_B_FRAME_START_VALUE 0x5555

/**
 * Start code of valid frame byte part
 */
#define APPA_B_FRAME_START_VALUE_BYTE 0x55

/**
 * Possible commands.
 * Calibration and configuration commands not included yet.
 *
 * @TODO Add Calibration, Settings, OTA, Config, Harmonics
 */
enum appa_b_command_e {
	APPA_B_COMMAND_READ_INFORMATION = 0x00, /**< Get information about Model and Brand */
	APPA_B_COMMAND_READ_DISPLAY = 0x01, /**< Get all display readings */
	APPA_B_COMMAND_READ_PROTOCOL_VERSION = 0x03, /**< Read protocol version */
	APPA_B_COMMAND_READ_BATTERY_LIFE = 0x04, /**< Read battery life */
};

/**
 * Currently supported models
 */
/**
 * Currently supported models
 */
enum appa_b_model_id_e {
	APPA_B_MODEL_ID_INVALID = 0x00, /**< Invalid */
	APPA_B_MODEL_ID_150 = 0x01, /**< APPA 150 with usb/serial only, probably invalid */
	APPA_B_MODEL_ID_150B = 0x02, /**< APPA 155B/156B/157B/158B, BENNING CM 12 with btle */
	APPA_B_MODEL_ID_208 = 0x03, /**< APPA 208 bench-type with usb/serial only */
	APPA_B_MODEL_ID_208B = 0x04, /**< APPA 208B bench-type with usb/serial and btle */
	APPA_B_MODEL_ID_506 = 0x05, /**< APPA 506, CMT 506 with usb/serial only */
	APPA_B_MODEL_ID_506B = 0x06, /**< APPA 506B, BENNING MM 12, Sefram 7352B with usb/serial and btle */
	APPA_B_MODEL_ID_506B_2 = 0x600, /**< APPA 506B Quirks Code */
	APPA_B_MODEL_ID_501 = 0x07, /**< APPA 501 */
	APPA_B_MODEL_ID_502 = 0x08, /**< APPA 502 */
	APPA_B_MODEL_ID_S1 = 0x09, /**< APPA S1 / Sefram 7221 */
	APPA_B_MODEL_ID_S2 = 0x0a, /**< APPA S2 / BENNING CM 10 / Sefram 7222 */
	APPA_B_MODEL_ID_S3 = 0x0b, /**< APPA S3 / BENNING CM 10-PV / Sefram 7223 */
	APPA_B_MODEL_ID_172 = 0x0c, /**< APPA 172B */
	APPA_B_MODEL_ID_173 = 0x0d, /**< APPA 173B */
	APPA_B_MODEL_ID_175 = 0x0e, /**< APPA 175B */
	APPA_B_MODEL_ID_177 = 0x0f, /**< APPA 177B */
	APPA_B_MODEL_ID_SFLEX_10A = 0x10, /**< APPA sFlex-10A */
	APPA_B_MODEL_ID_SFLEX_18A = 0x11, /**< APPA sFlex-18A */
	APPA_B_MODEL_ID_A17N = 0x12, /**< APPA A17N */
	APPA_B_MODEL_ID_S0 = 0x13, /**< APPA S0 / Sefram 7220 */
	APPA_B_MODEL_ID_179 = 0x14, /**< APPA 179 */
	APPA_B_MODEL_ID_503 = 0x15, /**< APPA 503 */
	APPA_B_MODEL_ID_505 = 0x16, /**< APPA 505 */
};

/**
 * Wordcodes
 *
 * Multimeter will send these codes to indicate a string visible on the
 * display. Works for main and sub.
 */
enum appa_b_wordcode_e {
	APPA_B_WORDCODE_SPACE = 0x700000, /**< Space */
	APPA_B_WORDCODE_FULL = 0x700001, /**< Full */
	APPA_B_WORDCODE_BEEP = 0x700002, /**< Beep */
	APPA_B_WORDCODE_APO = 0x700003, /**< Auto Power-Off */
	APPA_B_WORDCODE_B_LIT = 0x700004, /**< Backlight */
	APPA_B_WORDCODE_HAZ = 0x700005, /**< Hazard */
	APPA_B_WORDCODE_ON = 0x700006, /**< On */
	APPA_B_WORDCODE_OFF = 0x700007, /**< Off */
	APPA_B_WORDCODE_RESET = 0x700008, /**< Reset */
	APPA_B_WORDCODE_START = 0x700009, /**< Start */
	APPA_B_WORDCODE_VIEW = 0x70000a, /**< View */
	APPA_B_WORDCODE_PAUSE = 0x70000b, /**< Pause */
	APPA_B_WORDCODE_FUSE = 0x70000c, /**< Fuse */
	APPA_B_WORDCODE_PROBE = 0x70000d, /**< Probe */
	APPA_B_WORDCODE_DEF = 0x70000e, /**< Definition */
	APPA_B_WORDCODE_CLR = 0x70000f, /**< Clr */
	APPA_B_WORDCODE_ER = 0x700010, /**< Er */
	APPA_B_WORDCODE_ER1 = 0x700011, /**< Er1 */
	APPA_B_WORDCODE_ER2 = 0x700012, /**< Er2 */
	APPA_B_WORDCODE_ER3 = 0x700013, /**< Er3 */
	APPA_B_WORDCODE_DASH = 0x700014, /**< Dash (-----) */
	APPA_B_WORDCODE_DASH1 = 0x700015, /**< Dash1  (-) */
	APPA_B_WORDCODE_TEST = 0x700016, /**< Test */
	APPA_B_WORDCODE_DASH2 = 0x700017, /**< Dash2 (--) */
	APPA_B_WORDCODE_BATT = 0x700018, /**< Battery */
	APPA_B_WORDCODE_DISLT = 0x700019, /**< diSLt */
	APPA_B_WORDCODE_NOISE = 0x70001a, /**< Noise */
	APPA_B_WORDCODE_FILTR = 0x70001b, /**< Filter */
	APPA_B_WORDCODE_PASS = 0x70001c, /**< PASS */
	APPA_B_WORDCODE_NULL = 0x70001d, /**< null */
	APPA_B_WORDCODE_0_20 = 0x70001e, /**< 0 - 20 mA */
	APPA_B_WORDCODE_4_20 = 0x70001f, /**< 4 - 20 mA */
	APPA_B_WORDCODE_RATE = 0x700020, /**< Rate */
	APPA_B_WORDCODE_SAVE = 0x700021, /**< Save */
	APPA_B_WORDCODE_LOAD = 0x700022, /**< Load */
	APPA_B_WORDCODE_YES = 0x700023, /**< Yes */
	APPA_B_WORDCODE_SEND = 0x700024, /**< Send */
	APPA_B_WORDCODE_AHOLD = 0x700025, /**< AUTO HOLD */
	APPA_B_WORDCODE_AUTO = 0x700026, /**< AUTO */
	APPA_B_WORDCODE_CNTIN = 0x700027, /**< Continuity */
	APPA_B_WORDCODE_CAL = 0x700028, /**< CAL */
	APPA_B_WORDCODE_VERSION = 0x700029, /**< Version */
	APPA_B_WORDCODE_OL = 0x70002a, /**< OL (unused) */
	APPA_B_WORDCODE_BAT_FULL = 0x70002b, /**< Battery Full */
	APPA_B_WORDCODE_BAT_HALF = 0x70002c, /**< Battery Half */
	APPA_B_WORDCODE_LO = 0x70002d, /**< Lo */
	APPA_B_WORDCODE_HI = 0x70002e, /**< Hi */
	APPA_B_WORDCODE_DIGIT = 0x70002f, /**< Digits */
	APPA_B_WORDCODE_RDY = 0x700030, /**< Ready */
	APPA_B_WORDCODE_DISC = 0x700031, /**< dISC */
	APPA_B_WORDCODE_OUTF = 0x700032, /**< outF */
	APPA_B_WORDCODE_OLA = 0x700033, /**< OLA */
	APPA_B_WORDCODE_OLV = 0x700034, /**< OLV */
	APPA_B_WORDCODE_OLVA = 0x700035, /**< OLVA */
	APPA_B_WORDCODE_BAD = 0x700036, /**< BAD */
	APPA_B_WORDCODE_TEMP = 0x700037, /**< TEMP */
};

/**
 * Data units
 */
enum appa_b_unit_e {
	APPA_B_UNIT_NONE = 0x00, /**< None */
	APPA_B_UNIT_V = 0x01, /**< V */
	APPA_B_UNIT_MV = 0x02, /**< mV */
	APPA_B_UNIT_A = 0x03, /**< A */
	APPA_B_UNIT_MA = 0x04, /**< mA */
	APPA_B_UNIT_DB = 0x05, /**< dB */
	APPA_B_UNIT_DBM = 0x06, /**< dBm */
	APPA_B_UNIT_MF = 0x07, /**< mF */
	APPA_B_UNIT_UF = 0x08, /**< µF */
	APPA_B_UNIT_NF = 0x09, /**< nF */
	APPA_B_UNIT_GOHM = 0x0a, /**< GΩ */
	APPA_B_UNIT_MOHM = 0x0b, /**< MΩ */
	APPA_B_UNIT_KOHM = 0x0c, /**< kΩ */
	APPA_B_UNIT_OHM = 0x0d, /**< Ω */
	APPA_B_UNIT_PERCENT = 0x0e, /**< Relative percentage value */
	APPA_B_UNIT_MHZ = 0x0f, /**< MHz */
	APPA_B_UNIT_KHZ = 0x10, /**< kHz */
	APPA_B_UNIT_HZ = 0x11, /**< Hz */
	APPA_B_UNIT_DEGC = 0x12, /**< °C */
	APPA_B_UNIT_DEGF = 0x13, /**< °F */
	APPA_B_UNIT_SEC = 0x14, /**< seconds */
	APPA_B_UNIT_MS = 0x15, /**< ms */
	APPA_B_UNIT_US = 0x16, /**< µs */
	APPA_B_UNIT_NS = 0x17, /**< ns */
	APPA_B_UNIT_UA = 0x18, /**< µA */
	APPA_B_UNIT_MIN = 0x19, /**< minutes */
	APPA_B_UNIT_KW = 0x1a, /**< kW */
	APPA_B_UNIT_PF = 0x1b, /**< Power Factor (@TODO maybe pico-farat?) */
};

/**
 * Display range / dot positions
 */
enum appa_b_dot_e {
	APPA_B_DOT_NONE = 0x00,
	APPA_B_DOT_9999_9 = 0x01,
	APPA_B_DOT_999_99 = 0x02,
	APPA_B_DOT_99_999 = 0x03,
	APPA_B_DOT_9_9999 = 0x04,

};

/**
 * OL-Indication values
 */
enum appa_b_overload_e {
	APPA_B_NOT_OVERLOAD = 0x00, /**< non-OL value */
	APPA_B_OVERLOAD = 0x01, /**< OL */
};

/**
 * Data content - Menu, Min/Max/Avg, etc. selection
 */
enum appa_b_data_content_e {
	APPA_B_DATA_CONTENT_MEASURING_DATA = 0x00,
	APPA_B_DATA_CONTENT_FREQUENCY = 0x01,
	APPA_B_DATA_CONTENT_CYCLE = 0x02,
	APPA_B_DATA_CONTENT_DUTY = 0x03,
	APPA_B_DATA_CONTENT_MEMORY_STAMP = 0x04,
	APPA_B_DATA_CONTENT_MEMORY_SAVE = 0x05,
	APPA_B_DATA_CONTENT_MEMORY_LOAD = 0x06,
	APPA_B_DATA_CONTENT_LOG_SAVE = 0x07,
	APPA_B_DATA_CONTENT_LOG_LOAD = 0x08,
	APPA_B_DATA_CONTENT_LOAG_RATE = 0x09,
	APPA_B_DATA_CONTENT_REL_DELTA = 0x0a,
	APPA_B_DATA_CONTENT_REL_PERCENT = 0x0b,
	APPA_B_DATA_CONTENT_REL_REFERENCE = 0x0c,
	APPA_B_DATA_CONTENT_MAXIMUM = 0x0d,
	APPA_B_DATA_CONTENT_MINIMUM = 0x0e,
	APPA_B_DATA_CONTENT_AVERAGE = 0x0f,
	APPA_B_DATA_CONTENT_PEAK_HOLD_MAX = 0x10,
	APPA_B_DATA_CONTENT_PEAK_HOLD_MIN = 0x11,
	APPA_B_DATA_CONTENT_DBM = 0x12,
	APPA_B_DATA_CONTENT_DB = 0x13,
	APPA_B_DATA_CONTENT_AUTO_HOLD = 0x14,
	APPA_B_DATA_CONTENT_SETUP = 0x15,
	APPA_B_DATA_CONTENT_LOG_STAMP = 0x16,
	APPA_B_DATA_CONTENT_LOG_MAX = 0x17,
	APPA_B_DATA_CONTENT_LOG_MIN = 0x18,
	APPA_B_DATA_CONTENT_LOG_TP = 0x19,
	APPA_B_DATA_CONTENT_HOLD = 0x1a,
	APPA_B_DATA_CONTENT_CURRENT_OUTPUT = 0x1b,
	APPA_B_DATA_CONTENT_CUR_OUT_0_20MA_PERCENT = 0x1c,
	APPA_B_DATA_CONTENT_CUR_OUT_4_20MA_PERCENT = 0x1d,
};

/**
 * Function codes
 *
 * Basically indicate the rotary position and the secondary function selected
 */
enum appa_b_functioncode_e {
	APPA_B_FUNCTIONCODE_NONE = 0x00,
	APPA_B_FUNCTIONCODE_AC_V = 0x01,
	APPA_B_FUNCTIONCODE_DC_V = 0x02,
	APPA_B_FUNCTIONCODE_AC_MV = 0x03,
	APPA_B_FUNCTIONCODE_DC_MV = 0x04,
	APPA_B_FUNCTIONCODE_OHM = 0x05,
	APPA_B_FUNCTIONCODE_CONTINUITY = 0x06,
	APPA_B_FUNCTIONCODE_DIODE = 0x07,
	APPA_B_FUNCTIONCODE_CAP = 0x08,
	APPA_B_FUNCTIONCODE_AC_A = 0x09,
	APPA_B_FUNCTIONCODE_DC_A = 0x0a,
	APPA_B_FUNCTIONCODE_AC_MA = 0x0b,
	APPA_B_FUNCTIONCODE_DC_MA = 0x0c,
	APPA_B_FUNCTIONCODE_DEGC = 0x0d,
	APPA_B_FUNCTIONCODE_DEGF = 0x0e,
	APPA_B_FUNCTIONCODE_FREQUENCY = 0x0f,
	APPA_B_FUNCTIONCODE_DUTY = 0x10,
	APPA_B_FUNCTIONCODE_HZ_V = 0x11,
	APPA_B_FUNCTIONCODE_HZ_MV = 0x12,
	APPA_B_FUNCTIONCODE_HZ_A = 0x13,
	APPA_B_FUNCTIONCODE_HZ_MA = 0x14,
	APPA_B_FUNCTIONCODE_AC_DC_V = 0x15,
	APPA_B_FUNCTIONCODE_AC_DC_MV = 0x16,
	APPA_B_FUNCTIONCODE_AC_DC_A = 0x17,
	APPA_B_FUNCTIONCODE_AC_DC_MA = 0x18,
	APPA_B_FUNCTIONCODE_LPF_V = 0x19,
	APPA_B_FUNCTIONCODE_LPF_MV = 0x1a,
	APPA_B_FUNCTIONCODE_LPF_A = 0x1b,
	APPA_B_FUNCTIONCODE_LPF_MA = 0x1c,
	APPA_B_FUNCTIONCODE_AC_UA = 0x1d,
	APPA_B_FUNCTIONCODE_DC_UA = 0x1e,
	APPA_B_FUNCTIONCODE_DC_A_OUT = 0x1f,
	APPA_B_FUNCTIONCODE_DC_A_OUT_SLOW_LINEAR = 0x20,
	APPA_B_FUNCTIONCODE_DC_A_OUT_FAST_LINEAR = 0x21,
	APPA_B_FUNCTIONCODE_DC_A_OUT_SLOW_STEP = 0x22,
	APPA_B_FUNCTIONCODE_DC_A_OUT_FAST_STEP = 0x23,
	APPA_B_FUNCTIONCODE_LOOP_POWER = 0x24,
	APPA_B_FUNCTIONCODE_250OHM_HART = 0x25,
	APPA_B_FUNCTIONCODE_VOLT_SENSE = 0x26,
	APPA_B_FUNCTIONCODE_PEAK_HOLD_V = 0x27,
	APPA_B_FUNCTIONCODE_PEAK_HOLD_MV = 0x28,
	APPA_B_FUNCTIONCODE_PEAK_HOLD_A = 0x29,
	APPA_B_FUNCTIONCODE_PEAK_HOLD_MA = 0x2a,
	APPA_B_FUNCTIONCODE_LOZ_AC_V = 0x2b,
	APPA_B_FUNCTIONCODE_LOZ_DC_V = 0x2c,
	APPA_B_FUNCTIONCODE_LOZ_AC_DC_V = 0x2d,
	APPA_B_FUNCTIONCODE_LOZ_LPF_V = 0x2e,
	APPA_B_FUNCTIONCODE_LOZ_HZ_V = 0x2f,
	APPA_B_FUNCTIONCODE_LOZ_PEAK_HOLD_V = 0x30,
	APPA_B_FUNCTIONCODE_BATTERY = 0x31,
	APPA_B_FUNCTIONCODE_AC_W = 0x32,
	APPA_B_FUNCTIONCODE_DC_W = 0x33,
	APPA_B_FUNCTIONCODE_PF = 0x34,
	APPA_B_FUNCTIONCODE_FLEX_AC_A = 0x35,
	APPA_B_FUNCTIONCODE_FLEX_LPF_A = 0x36,
	APPA_B_FUNCTIONCODE_FLEX_PEAK_HOLD_A = 0x37,
	APPA_B_FUNCTIONCODE_FLEX_HZ_A = 0x38,
	APPA_B_FUNCTIONCODE_V_HARM = 0x39,
	APPA_B_FUNCTIONCODE_INRUSH = 0x3a,
	APPA_B_FUNCTIONCODE_A_HARM = 0x3b,
	APPA_B_FUNCTIONCODE_FLEX_INRUSH = 0x3c,
	APPA_B_FUNCTIONCODE_FLEX_A_HARM = 0x3d,
	APPA_B_FUNCTIONCODE_PEAK_HOLD_UA = 0x3e,
	APPA_B_FUNCTIONCODE_AC_UA_HFR = 0x3F,
	APPA_B_FUNCTIONCODE_AC_V_HFR = 0x40,
	APPA_B_FUNCTIONCODE_AC_MV_HFR = 0x41,
	APPA_B_FUNCTIONCODE_AC_A_HFR = 0x42,
	APPA_B_FUNCTIONCODE_AC_MA_HFR = 0x43,
	APPA_B_FUNCTIONCODE_AC_UA_HFR2 = 0x44,
	APPA_B_FUNCTIONCODE_DC_V_PV = 0x45,
	APPA_B_FUNCTIONCODE_AC_V_PV = 0x46,
	APPA_B_FUNCTIONCODE_AC_V_PV_HFR = 0x47,
	APPA_B_FUNCTIONCODE_AC_DC_V_PV = 0x48,
};

/**
 * Manual / Auto test field values
 */
enum appa_b_autotest_e {
	APPA_B_MANUAL_TEST = 0x00, /**< Manual Test */
	APPA_B_AUTO_TEST = 0x01, /**< Auto Test */
};

/**
 * Manual / Auto range field values
 */
enum appa_b_autorange_e {
	APPA_B_MANUAL_RANGE = 0x00, /**< Manual ranging */
	APPA_B_AUTO_RANGE = 0x01, /**< Auto range active */
};

/**
 * Frame Header
 */
struct appa_b_frame_header_s {
	u_int16_t start; /**< 0x5555 start code */
	u_int8_t command; /**< @appaCommand_t */
	u_int8_t dataLength; /**< Length of Data */
};

/**
 * Frame Data: Device information response
 */
struct appa_b_frame_information_response_data_s {
	char model_name[32]; /**< String 0x20 terminated model name of device (branded) */
	char serial_number[16]; /**< String 0x20 terminated serial number of device */
	u_int16_t model_id; /*< @appaBModelId_t */
	u_int16_t firmware_version; /*< Firmware version */
};

/**
 * Frame Data: Display data within display response
 */
struct appa_b_frame_display_reading_s {
	u_int8_t reading_b0; /**< int24 Byte 0 - measured value */
	u_int8_t reading_b1; /**< int24 Byte 1 - measured value */
	u_int8_t reading_b2; /**< int24 Byte 2 - measured value */

	u_int8_t dot; /**< @appa_b_dot_e */
	u_int8_t unit; /**< @appa_b_unit_e */

	u_int8_t data_content; /**< @appa_b_data_content_e */
	u_int8_t overload; /**< @appa_b_overload_e */
};

/**
 * Frame: Display response
 */
struct appa_b_frame_display_response_data_s {
	u_int8_t function_code; /**< @appa_b_function_code_e */
	u_int8_t auto_test; /**< @appa_b_auto_test_e */
	u_int8_t range_code; /**< @TODO Implement Table 7.1 of protocol spec, only required for calibration */
	u_int8_t auto_range; /**< @appa_b_auto_range_e */

	struct appa_b_frame_display_reading_s main_display_data; /**< Reading of main (lower) display value */

	struct appa_b_frame_display_reading_s sub_display_data; /**< Reading of sub (upper) display value */
};

/**
 * Helper to calculate frame payload length without checksum
 *
 * @param arg_data_length Data length of Frame
 * @return Length of payload in bytes without checksum
 */
#define APPA_B_PAYLOAD_LENGTH(arg_data_length) \
	(arg_data_length + APPA_B_FRAME_HEADER_SIZE)

/**
 * Helper to calculate frame frame length with checksum
 *
 * @param arg_data_length Data length of Frame
 * @return Length of frame in bytes with checksum
 */
#define APPA_B_FRAME_LENGTH(arg_data_length) \
	(arg_data_length + APPA_B_FRAME_HEADER_SIZE + APPA_B_FRAME_CHECKSUM_SIZE)

/**
 * Check if reading is wordcode
 *
 * @param arg_wordcode Wordcode value
 * @return TRUE if reading value is a wordcode
 */
static gboolean appa_b_is_wordcode(const int arg_wordcode);

/**
 * Check if reading is dash-wordcode
 *
 * @param arg_wordcode Wordcode value
 * @return TRUE if reading value is a dash-wordcode
 */
static gboolean appa_b_is_wordcode_dash(const int arg_wordcode);

/**
 * Measurement value / reading decoding helper
 *
 * Take a display structure and decode the 24 bit reading into a standard int
 *
 * @param arg_display_data
 * @return int value of reading in correct byte order
 */
static int appa_b_decode_reading(const struct appa_b_frame_display_reading_s *arg_display_reading);

/**
 * Model id code to string
 *
 * @param arg_model_id
 * @return String name
 */
static const char *appa_b_model_id_name(const enum appa_b_model_id_e arg_model_id);

/**
 * Model id code to string
 *
 * @param arg_model_id
 * @return String name
 */
static const char *appa_b_wordcode_name(const enum appa_b_wordcode_e arg_wordcode);

/**
 * APPA checksum calculation
 *
 * @param argData Data to calculate the checksum for
 * @param argSize Size of data
 * @return Checksum
 */
static u_int8_t appa_b_checksum(const u_int8_t *arg_data, int arg_size);

/**
 * Request Information data from meter
 *
 * @param arg_buf Buffer to write the request to, min size 5
 * @param arg_len Length of buffer
 * @return SR_OK, SR_ERR, etc.
 */
static int appa_b_write_frame_information_request(u_int8_t *arg_buf, int arg_len);

/**
 * Request Display data from meter
 *
 * @param arg_buf Buffer to write the request to, min size 5
 * @param arg_len Length of buffer
 * @return SR_OK, SR_ERR, etc.
 */
static int appa_b_write_frame_display_request(u_int8_t *arg_buf, int arg_len);

/**
 * Request Display data from meter
 *
 * @param arg_buf Buffer to read  the frame from
 * @param arg_display_reading Output structure to write the result to
 * @return SR_OK, SR_ERR, etc.
 */
static int appa_b_read_frame_display_response(const u_int8_t *arg_buf, struct appa_b_frame_display_response_data_s *arg_display_response_data);

#endif/*def APPA_B__H*/
