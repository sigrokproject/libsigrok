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
 * APPA B Interface (Models 150/208/506)
 * 
 * Brands include:
 * 
 *   - APPA 155B
 *   - APPA 156B
 *   - APPA 157B
 *   - APPA 158B
 *   - APPA 506(B)
 *   - APPA 208(B)
 *   - Benning MM 12
 *   - Benning CM 12
 *   - Sefram 7352(B)
 * 
 * Bluetooth-only models will require the BTLE-connector to be finished first
 * to actually work.
 * 
 * @TODO Implement calibration
 * @TODO Implement log download
 * 
 */

#ifndef APPA_B__H
#define APPA_B__H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif/*def __cplusplus*/
  
#ifdef __GNUC__
  #define APPA_B_GCC_PACKED __attribute__((packed))
#else/*def __GNUC__*/
  #define APPA_B_GCC_PACKED
#endif/*def __GNUC__*/
  
typedef char appaChar_t; /**< 8-bit char type */
typedef u_int8_t appaByte_t; /**< 8-bit Byte */
typedef u_int16_t appaWord_t; /**< 16-bit Word */
typedef u_int32_t appaDWord_t; /**< 32-bit DWord */
typedef int appaInt_t; /**< Generic integer */
typedef double appaDouble_t; /**< Double-precision float */

static const appaChar_t appaStringNa[] = "N/A"; /**< Used for unavailable strings and labels */

/**
 * Possible commands.
 * Calibration and configuration commands not included yet.
 */
typedef enum
{
    APPA_B_COMMAND_READ_INFORMATION = 0x00, /**< Get information about Model and Brand */
    APPA_B_COMMAND_READ_DISPLAY = 0x01, /**< Get all display readings */
} appaBCommand_t;

/**
 * Currently supported models
 */
typedef enum
{
    APPA_B_MODEL_ID_INVALID = 0x00, /**< Invalid */
    APPA_B_MODEL_ID_150 = 0x01, /**< APPA 150 with usb/serial only, probably invalid */
    APPA_B_MODEL_ID_150B = 0x02, /**< APPA 155B/156B/157B/158B, BENNING CM 12 with btle */
    APPA_B_MODEL_ID_208 = 0x03, /**< APPA 208 bench-type with usb/serial only */
    APPA_B_MODEL_ID_208B = 0x04, /**< APPA 208B bench-type with usb/serial and btle */
    APPA_B_MODEL_ID_506 = 0x05, /**< APPA 506, CMT 506 with usb/serial only */
    APPA_B_MODEL_ID_506B = 0x06, /**< APPA 506B, BENNING MM 12, Sefram 7352B with usb/serial and btle */
} appaBModelId_t;

/**
 * Model String translation table
 */
static const appaChar_t* appaBModelTable[] =
{
  "N/A",
  "APPA 150",
  "APPA 150B",
  "APPA 208",
  "APPA 208B",
  "APPA 506",
  "APPA 506B",
};
#define APPA_B_MODEL_TABLE_MIN 0
#define APPA_B_MODEL_TABLE_MAX 6
#define APPA_B_MODEL_ID_TO_STRING(argModelId) (argModelId<APPA_B_MODEL_TABLE_MIN||argModelId>APPA_B_MODEL_TABLE_MAX?appaStringNa:appaBModelTable[argModelId])

/**
 * Wordcodes
 * 
 * Multimeter will send these codes to indicate a string visible on the
 * display. Works for main and sub.
 */
typedef enum
{
    APPA_B_WORDCODE_SPACE = 0x700000,
    APPA_B_WORDCODE_FULL = 0x700001,
    APPA_B_WORDCODE_BEEP = 0x700002,
    APPA_B_WORDCODE_APO = 0x700003,
    APPA_B_WORDCODE_B_LIT = 0x700004,
    APPA_B_WORDCODE_HAZ = 0x700005,
    APPA_B_WORDCODE_ON = 0x700006,
    APPA_B_WORDCODE_OFF = 0x700007,
    APPA_B_WORDCODE_RESET = 0x700008,
    APPA_B_WORDCODE_START = 0x700009,
    APPA_B_WORDCODE_VIEW = 0x70000a,
    APPA_B_WORDCODE_PAUSE = 0x70000b,
    APPA_B_WORDCODE_FUSE = 0x70000c,
    APPA_B_WORDCODE_PROBE = 0x70000d,
    APPA_B_WORDCODE_DEF = 0x70000e,
    APPA_B_WORDCODE_CLR = 0x70000f,
    APPA_B_WORDCODE_ER = 0x700010,
    APPA_B_WORDCODE_ER1 = 0x700011,
    APPA_B_WORDCODE_ER2 = 0x700012,
    APPA_B_WORDCODE_ER3 = 0x700013,
    APPA_B_WORDCODE_DASH = 0x700014,
    APPA_B_WORDCODE_DASH1 = 0x700015,
    APPA_B_WORDCODE_TEST = 0x700016,
    APPA_B_WORDCODE_DASH2 = 0x700017,
    APPA_B_WORDCODE_BATT = 0x700018,
    APPA_B_WORDCODE_DISLT = 0x700019,
    APPA_B_WORDCODE_NOISE = 0x70001a,
    APPA_B_WORDCODE_FILTR = 0x70001b,
    APPA_B_WORDCODE_PASS = 0x70001c,
    APPA_B_WORDCODE_NULL = 0x70001d,
    APPA_B_WORDCODE_0_20 = 0x70001e,
    APPA_B_WORDCODE_4_20 = 0x70001f,
    APPA_B_WORDCODE_RATE = 0x700020,
    APPA_B_WORDCODE_SAVE = 0x700021,
    APPA_B_WORDCODE_LOAD = 0x700022,
    APPA_B_WORDCODE_YES = 0x700023,
    APPA_B_WORDCODE_SEND = 0x700024,
    APPA_B_WORDCODE_AHOLD = 0x700025,
    APPA_B_WORDCODE_AUTO = 0x700026,
    APPA_B_WORDCODE_CNTIN = 0x700027,
    APPA_B_WORDCODE_CAL = 0x700028,
    APPA_B_WORDCODE_VERSION = 0x700029,
    APPA_B_WORDCODE_OL = 0x70002a,
    APPA_B_WORDCODE_BAT_FULL = 0x70002b,
    APPA_B_WORDCODE_BAT_HALF = 0x70002c,
    APPA_B_WORDCODE_LO = 0x70002d,
    APPA_B_WORDCODE_HI = 0x70002e,
    APPA_B_WORDCODE_DIGIT = 0x70002f,
    APPA_B_WORDCODE_RDY = 0x700030,
    APPA_B_WORDCODE_DISC = 0x700031,
    APPA_B_WORDCODE_OUTF = 0x700032,
    APPA_B_WORDCODE_OLA = 0x700033,
    APPA_B_WORDCODE_OLV = 0x700034,
    APPA_B_WORDCODE_OLVA = 0x700035,
    APPA_B_WORDCODE_BAD = 0x700036,
    APPA_B_WORDCODE_TEMP = 0x700037,
} appaBWordcode_t;

/**
 * Wordcode String-translation table
 */
static const appaChar_t* appaBWordcodeTable[] =
{
  "", /*< 0x00000 */
  "Full", /*< 0x00001 */
  "Beep", /*< 0x00002 */
  "Auto Power-Off", /*< 0x00003 */
  "Backlight", /*< 0x00004 */
  "Hazard", /*< 0x00005 */
  "On", /*< 0x00006 */
  "Off", /*< 0x00007 */
  "Reset", /*< 0x00008 */
  "Start", /*< 0x00009 */
  "View", /*< 0x0000a */
  "Pause", /*< 0x0000b */
  "Fuse", /*< 0x0000c */
  "Probe", /*< 0x0000d */
  "Definition", /*< 0x0000e */
  "Clr", /*< 0x0000f */
  "Er", /*< 0x00010 */
  "Er1", /*< 0x00011 */
  "Er2", /*< 0x00012 */
  "Er3", /*< 0x00013 */
  "-----", /*< 0x00014 */
  "Dash1", /*< 0x00015 */
  "Test", /*< 0x00016 */
  "Dash2", /*< 0x00017 */
  "Battery", /*< 0x00018 */
  "diSLt", /*< 0x00019 */
  "Noise", /*< 0x0001a */
  "Filter", /*< 0x0001b */
  "PASS", /*< 0x0001c */
  "null", /*< 0x0001d */
  "0 - 20", /*< 0x0001e */
  "4 - 20", /*< 0x0001f */
  "Rate", /*< 0x00020 */
  "Save", /*< 0x00021 */
  "Load", /*< 0x00022 */
  "Yes", /*< 0x00023 */
  "Send", /*< 0x00024 */
  "Auto Hold", /*< 0x00025 */
  "Auto", /*< 0x00026 */
  "Continuity", /*< 0x00027 */
  "CAL", /*< 0x00028 */
  "Version", /*< 0x00029 */
  "OL", /*< 0x0002a */
  "FULL", /*< 0x0002b */
  "HALF", /*< 0x0002c */
  "Lo", /*< 0x0002d */
  "Hi", /*< 0x0002e */
  "Digits", /*< 0x0002f */
  "Ready", /*< 0x00030 */
  "dISC", /*< 0x00031 */
  "outF", /*< 0x00032 */
  "OLA", /*< 0x00033 */
  "OLV", /*< 0x00034 */
  "OLVA", /*< 0x00035 */
  "BAD", /*< 0x00036 */
  "Temperature", /*< 0x00037 */
};
#define APPA_B_WORDCODE_TABLE_MIN 0x700000
#define APPA_B_WORDCODE_TABLE_MAX 0x700037
#define APPA_B_WORDCODE_TO_STRING(argWordcode) (argWordcode<APPA_B_WORDCODE_TABLE_MIN||argWordcode>APPA_B_WORDCODE_TABLE_MAX?appaStringNa:appaBWordcodeTable[argWordcode-APPA_B_WORDCODE_TABLE_MIN])
#define APPA_B_IS_WORDCODE(argWordcode) (argWordcode>=APPA_B_WORDCODE_TABLE_MIN)
#define APPA_B_IS_WORDCODE_VALID(argWordcode) (argWordcode>=APPA_B_WORDCODE_TABLE_MIN&&argWordcode<=APPA_B_WORDCODE_TABLE_MAX)

/**
 * Data units
 */
typedef enum
{
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
} appaBUnit_t;

/**
 * Unit String-translation table
 */
static const appaChar_t* appaBUnitTable[] =
{
  "", /**< 0x00 */
  "V", /**< 0x01 */
  "mV", /**< 0x02 */
  "A", /**< 0x03 */
  "mA", /**< 0x04 */
  "dB", /**< 0x05 */
  "dBm", /**< 0x06 */
  "mF", /**< 0x07 */
  "µF", /**< 0x08 */
  "nF", /**< 0x09 */
  "GΩ", /**< 0x0a */
  "MΩ", /**< 0x0b */
  "kΩ", /**< 0x0c */
  "Ω", /**< 0x0d */
  "%", /**< 0x0e */
  "MHz", /**< 0x0f */
  "kHz", /**< 0x10 */
  "Hz", /**< 0x11 */
  "°C", /**< 0x12 */
  "°F", /**< 0x13 */
  "sec", /**< 0x14 */
  "ms", /**< 0x15 */
  "us", /**< 0x16 */
  "ns", /**< 0x17 */
  "µA", /**< 0x18 */
  "min", /**< 0x19 */
  "kW", /**< 0x1a */
  "PF", /**< 0x1b */
};

#define APPA_B_UNIT_TABLE_MIN 0x00
#define APPA_B_UNIT_TABLE_MAX 0x1b
#define APPA_B_UNIT_TO_STRING(argUnit) (argUnit<APPA_B_UNIT_TABLE_MIN||argUnit>APPA_B_UNIT_TABLE_MAX?appaStringNa:appaBUnitTable[argUnit])

/**
 * Display range / dot positions
 */
typedef enum
{
    APPA_B_DOT_NONE = 0x00,
    APPA_B_DOT_9999_9 = 0x01,
    APPA_B_DOT_999_99 = 0x02,
    APPA_B_DOT_99_999 = 0x03,
    APPA_B_DOT_9_9999 = 0x04,
} appaBDot_t;

/**
 * Base factors for display ranges
 */
static const double appaBDotFactor[] =
{
  1.0,
  0.1,
  0.01,
  0.001,
  0.0001,
};

/**
 * Display range precisions before dot
 */
static const int appaBDotPrecisionAfterDot[] =
{
  0,
  1,
  2,
  3,
  4,
};

/**
 * Display range precisions after dot
 */
static const int appaBDotPrecisionBeforeDot[] =
{
  5,
  4,
  3,
  2,
  1,
};

/**
 * OL-Indication values
 */
typedef enum
{
    APPA_B_NOT_OVERLOAD = 0x00, /**< non-OL value */
    APPA_B_OVERLOAD = 0x01, /**< OL */
} appaBOverload_t;

/**
 * String representation of OL on display
 */
#define APPA_B_READING_TEXT_OL "OL"

/**
 * Data content - Menu, Min/Max/Avg, etc. selection
 */
typedef enum
{
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
} appaBDataContent_t;

/**
 * Function codes
 * 
 * Basically indicate the rotary position and the secondary function selected
 */
typedef enum
{
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
} appaBFunctioncode_t;

/**
 * Function code string translation table
 */
static const appaChar_t* appaBFunctioncodeTable[] =
{
  "", /**< 0x00 */
  "AC V", /**< 0x01 */
  "DC V", /**< 0x02 */
  "AC mV", /**< 0x03 */
  "DC mV", /**< 0x04 */
  "Ω", /**< 0x05 */
  "Continuity", /**< 0x06 */
  "Diode", /**< 0x07 */
  "Capacitor", /**< 0x08 */
  "AC A", /**< 0x09 */
  "DC A", /**< 0x0a */
  "AC mA", /**< 0x0b */
  "DC mA", /**< 0x0c */
  "°C", /**< 0x0d */
  "°F", /**< 0x0e */
  "Frequency", /**< 0x0f */
  "Duty Cycle", /**< 0x10 */
  "Hz V", /**< 0x11 */
  "Hz mV", /**< 0x12 */
  "Hz A", /**< 0x13 */
  "Hz mA", /**< 0x14 */
  "AC+DC V", /**< 0x15 */
  "AC+DC mV", /**< 0x16 */
  "AC+DC A", /**< 0x17 */
  "AC+DC mA", /**< 0x18 */
  "HFR V", /**< 0x19 */
  "HFR mV", /**< 0x1a */
  "HFR A", /**< 0x1b */
  "HFR mA", /**< 0x1c */
  "AC µA", /**< 0x1d */
  "DC µA", /**< 0x1e */
  "DC A Out", /**< 0x1f */
  "DC A Out slow linear", /**< 0x20 */
  "DC A Out fast linear", /**< 0x21 */
  "DC A Out slow step", /**< 0x22 */
  "DC A Out fast step", /**< 0x23 */
  "Loop power", /**< 0x24 */
  "250Ω heart", /**< 0x25 */
  "VOLT SENSE", /**< 0x26 */
  "PEAK HOLD V", /**< 0x27 */
  "PEAK HOLD mV", /**< 0x28 */
  "PEAK HOLD A", /**< 0x29 */
  "PEAK HOLD mA", /**< 0x2a */
  "LoZ AC V", /**< 0x2b */
  "LoZ DC V", /**< 0x2c */
  "LoZ AC+DC V", /**< 0x2d */
  "LoZ HFR V", /**< 0x2e */
  "LoZ Hz V", /**< 0x2f */
  "LoZ PEAK HOLD V", /**< 0x30 */
  "BATTERY", /**< 0x31 */
  "AC W", /**< 0x32 */
  "DC W", /**< 0x33 */
  "PF", /**< 0x34 */
  "Flex AC A", /**< 0x35 */
  "Flex HFR A", /**< 0x36 */
  "Flex PEAK HOLD A", /**< 0x37 */
  "Flex Hz A", /**< 0x38 */
  "V HARM", /**< 0x39 */
  "INRUSH", /**< 0x3a */
  "A HARM", /**< 0x3b */
  "Flex INRUSH", /**< 0x3c */
  "Flex A HARM", /**< 0x3d */
  "PEAK HOLD µA", /**< 0x3e */
};
#define APPA_B_FUNCTIONCODE_TABLE_MIN 0x00
#define APPA_B_FUNCTIONCODE_TABLE_MAX 0x3e
#define APPA_B_FUNCTIONCODE_TO_STRING(argFunctioncode) (argFunctioncode<APPA_B_FUNCTIONCODE_TABLE_MIN||argFunctioncode>APPA_B_FUNCTIONCODE_TABLE_MAX?appaStringNa:appaBFunctioncodeTable[argFunctioncode])

/**
 * Manual / Auto test field values
 */
typedef enum
{
    APPA_B_MANUAL_TEST = 0x00, /**< Manual Test */
    APPA_B_AUTO_TEST = 0x01, /**< Auto Test */
} appaBAutotest_t;

/**
 * Manual / Auto range field values
 */
typedef enum
{
    APPA_B_MANUAL_RANGE = 0x00, /**< Manual ranging */
    APPA_B_AUTO_RANGE = 0x01, /**< Auto range active */
} appaBAutorange_t;

/** Length of Read Information Request */
#define APPA_B_DATA_LENGTH_REQUEST_READ_INFORMATION 0
/** Length of Read Information Response */
#define APPA_B_DATA_LENGTH_RESPONSE_READ_INFORMATION 52
/** Length of Read Display Request */
#define APPA_B_DATA_LENGTH_REQUEST_READ_DISPLAY 0
/** Length of Read Display response */
#define APPA_B_DATA_LENGTH_RESPONSE_READ_DISPLAY 12

/** Helper to calculate frame length */
#define APPA_B_GET_FRAMELEN(argFrame) (APPA_B_DATA_LENGTH_##argFrame+4)

/** Start code of valid frame */
#define APPA_B_START 0x5555

/**
 * Frame: Empty request
 */
typedef struct {} APPA_B_GCC_PACKED appaBFrame_EmptyRequest_t;

/**
 * Frame: Empty response
 */
typedef struct {} APPA_B_GCC_PACKED appaBFrame_EmptyResponse_t;

/**
 * Frame: Device information request
 */
typedef appaBFrame_EmptyRequest_t appaBFrame_ReadInformationRequest_t;

/**
 * Frame: Device information response
 */
typedef struct
{
    appaChar_t modelName[32]; /**< String 0x20 terminated model name of device (branded) */
    appaChar_t serialNumber[16]; /**< String 0x20 terminated serial number of device */
    appaWord_t modelId; /*< @appaBModelId_t */
    appaWord_t firmwareVersion; /*< Firmware version */
} APPA_B_GCC_PACKED appaBFrame_ReadInformationResponse_t;

/**
 * Frame: Display request
 */
typedef appaBFrame_EmptyRequest_t appaBFrame_ReadDisplayRequest_t;

/**
 * Structure - reading of main and sub display
 */
typedef struct
{

    appaByte_t readingB0; /**< int24 Byte 0 - measured value */
    appaByte_t readingB1; /**< int24 Byte 1 - measured value */
    appaByte_t readingB2; /**< int24 Byte 2 - measured value */
    appaByte_t dot:3; /**< @appaBDot_t */
    appaByte_t unit:5; /**< @appaBUnit_t */
    appaByte_t dataContent:7; /**< @appaBDataContent_t */
    appaByte_t overload:1; /**< @appaBOverload_t */

} APPA_B_GCC_PACKED appaBFrame_ReadDisplayResponse_DisplayData_t;

/** Measurement value / reading decoding helper */
#define APPA_B_DECODE_READING(argDisplayData) ((appaInt_t)(argDisplayData.readingB0|argDisplayData.readingB1<<8|argDisplayData.readingB2<<16 | ((argDisplayData.readingB2>>7==1)?0xff:0)<<24 ))

/**
 * Frame: Display response
 */
typedef struct
{
    appaByte_t functionCode:7; /**< @appaBFunctioncode_t */
    appaByte_t autoTest:1; /**< @appaBAutotest_t */
    appaByte_t rangeCode:7; /**< @TODO Implement Table 7.1 of protocol spec, only required for calibration */
    appaByte_t autoRange:1; /**< @appaAutorange_t */
    appaBFrame_ReadDisplayResponse_DisplayData_t mainDisplayData; /**< Reading of main (lower) display value */
    appaBFrame_ReadDisplayResponse_DisplayData_t subDisplayData; /**< Reading of sub (upper) display value */
} APPA_B_GCC_PACKED appaBFrame_ReadDisplayResponse_t;

/**
 * General APPA communication frame
 * 
 * Used for Serial and BTLE-Communication in a wide variety of devices.
 */
typedef union
{
    
    struct
    {
        appaWord_t start; /**< 0x5555 start code */
        appaByte_t command; /**< @appaCommand_t */
        appaByte_t dataLength; /**< Length of Data */
        
        union
        {

          union
          {
              appaBFrame_ReadInformationResponse_t request;
              appaBFrame_ReadInformationResponse_t response;
          } readInformation;

          union
          {
              appaBFrame_ReadDisplayRequest_t request;
              appaBFrame_ReadDisplayResponse_t response;
          } readDisplay;
        };
    };
    
    appaByte_t raw[0];    
    
} APPA_B_GCC_PACKED appaBFrame_t;

/**
 * APPA checksum calculation
 * 
 * @param argData Data to calculate the checksum for
 * @param argSize Size of data
 * @return Checksum
 */
appaByte_t appa_b_checksum(appaByte_t* argData, appaInt_t argSize);

appaByte_t appa_b_checksum(appaByte_t* argData, appaInt_t argSize)
{
  appaByte_t checksum = 0;
  while(argSize-->0)
    checksum+= argData[argSize];
  return(checksum);
}

#ifdef __cplusplus
}
#endif/*def __cplusplus*/

#endif/*def APPA_B__H*/

