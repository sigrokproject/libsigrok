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
 * APPA DMM Interface
 *
 * Based on:
 *
 *  - APPA Communication Protocol v2.8
 *  - APPA 500 Communication Protocol v1.2
 *
 * Driver for modern APPA meters (handheld, bench, clamp). Communication is
 * done over a serial interface using the known APPA-Frames, see below. The
 * base protocol is always the same and deviates only where the models have
 * differences in ablities, range and features.
 *
 */

#ifndef LIBSIGROK_HARDWARE_APPA_DMM_PROTOCOL_H
#define LIBSIGROK_HARDWARE_APPA_DMM_PROTOCOL_H

#include <stdint.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

#include "tp/appa.h"

#define LOG_PREFIX "appa-dmm"

/* ************************************ */
/* ****** Built-in configuration ****** */
/* ************************************ */

/**
 * Timeout of general send/receive (read information)
 * when scanning (ms)
 */
#define APPADMM_SEND_RECEIVE_TIMEOUT 1000

/**
 * Timeout when writing blocking (ms)
 */
#define APPADMM_WRITE_BLOCKING_TIMEOUT 5

/**
 * Timeout when reading blocking (ms)
 */
#define APPADMM_READ_BLOCKING_TIMEOUT 64

/**
 * Default serial parameters
 */
#define APPADMM_CONF_SERIAL "9600/8n1"

/**
 * Amount of possible storage locations (MEM, LOG)
 */
#define APPADMM_STORAGE_INFO_COUNT 2

/**
 * Default internal poll rate
 */
#define APPADMM_RATE_INTERVAL_DEFAULT 100000

/**
 * Default internal for series 300
 */
#define APPADMM_RATE_INTERVAL_300 500000

/**
 * Default poll rate for legacy 500
 */
#define APPADMM_RATE_INTERVAL_500 100000

/**
 * Poll rate if rate adjustment is disabled
 */
#define APPADMM_RATE_INTERVAL_DISABLE 1

/**
 * Different poll rate for certain devices using a faulty A8105 firmware
 */
#define APPADMM_RATE_INTERVAL_APPA_208_506_BLE 200000

/**
 * APPA 150 Storage (MEM/LOG)
 */
#define APPADMM_STORAGE_150_ENTRY_SIZE 5
#define APPADMM_STORAGE_150_MEM_ENTRY_COUNT 1000
#define APPADMM_STORAGE_150_MEM_ADDRESS 0x40
#define APPADMM_STORAGE_150_MEM_MEM_COUNT 1
#define APPADMM_STORAGE_150_LOG_ENTRY_COUNT 9999
#define APPADMM_STORAGE_150_LOG_ADDRESS 0x1400
#define APPADMM_STORAGE_150_LOG_MEM_COUNT 1

/**
 * APPA 200/500 Storage (MEM/LOG)
 * APPA 500 New protocol
 */
#define APPADMM_STORAGE_200_500_ENTRY_SIZE 5
#define APPADMM_STORAGE_200_500_MEM_ENTRY_COUNT 500
#define APPADMM_STORAGE_200_500_MEM_ADDRESS 0x500
#define APPADMM_STORAGE_200_500_MEM_MEM_COUNT 2
#define APPADMM_STORAGE_200_500_LOG_ENTRY_COUNT 10000
#define APPADMM_STORAGE_200_500_LOG_ADDRESS 0x1000
#define APPADMM_STORAGE_200_500_LOG_MEM_COUNT 4

/**
 * APPA 500 Storage (MEM/LOG)
 * APPA 500 legacy protocol
 */
#define APPADMM_STORAGE_500_LEGACY_ENTRY_SIZE 5
#define APPADMM_STORAGE_500_LEGACY_MEM_ENTRY_COUNT 1000
#define APPADMM_STORAGE_500_LEGACY_MEM_ADDRESS 0x400
#define APPADMM_STORAGE_500_LEGACY_MEM_MEM_COUNT 1
#define APPADMM_STORAGE_500_LEGACY_LOG_ENTRY_COUNT 10000
#define APPADMM_STORAGE_500_LEGACY_LOG_ADDRESS 0x2800
#define APPADMM_STORAGE_500_LEGACY_LOG_MEM_COUNT 2

/**
 * APPA 170/S Storage (LOG)
 */
#define APPADMM_STORAGE_170_S_ENTRY_SIZE 8
#define APPADMM_STORAGE_170_S_LOG_ENTRY_COUNT 4000
#define APPADMM_STORAGE_170_S_LOG_ADDRESS 0x8000
#define APPADMM_STORAGE_170_S_LOG_MEM_COUNT 1
#define APPADMM_STORAGE_170_S_LOG_MEM_START 3

/* ********************* */
/* ****** Presets ****** */
/* ********************* */

/**
 * Used for unavailable / undecodable strings
 */
#define APPADMM_STRING_NA "N/A"

/**
 * String representation of "OL"-readings
 */
#define APPADMM_READING_TEXT_OL "OL"

/* **************************************** */
/* ****** Message frame definitiions ****** */
/* **************************************** */

/* Size of request frame data per command */
#define APPADMM_FRAME_DATA_SIZE_REQUEST_READ_INFORMATION 0
#define APPADMM_FRAME_DATA_SIZE_REQUEST_READ_DISPLAY 0
#define APPADMM_FRAME_DATA_SIZE_REQUEST_READ_PROTOCOL_VERSION 0
#define APPADMM_FRAME_DATA_SIZE_REQUEST_READ_BATTERY_LIFE 0
#define APPADMM_FRAME_DATA_SIZE_REQUEST_WRITE_UART_CONFIGURATION 1
#define APPADMM_FRAME_DATA_SIZE_REQUEST_CAL_READING 0
#define APPADMM_FRAME_DATA_SIZE_REQUEST_READ_MEMORY 4
#define APPADMM_FRAME_DATA_SIZE_REQUEST_READ_HARMONICS_DATA 0
#define APPADMM_FRAME_DATA_SIZE_REQUEST_CAL_ENTER 0
#define APPADMM_FRAME_DATA_SIZE_REQUEST_CAL_WRITE_FUNCTION_CODE 1
#define APPADMM_FRAME_DATA_SIZE_REQUEST_CAL_WRITE_RANGE_CODE 1
#define APPADMM_FRAME_DATA_SIZE_REQUEST_CAL_WRITE_MEMORY 64 /* variable (max) */
#define APPADMM_FRAME_DATA_SIZE_REQUEST_CAL_EXIT 0
#define APPADMM_FRAME_DATA_SIZE_REQUEST_OTA_ENTER 0
#define APPADMM_FRAME_DATA_SIZE_REQUEST_OTA_SEND_INFORMATION 13
#define APPADMM_FRAME_DATA_SIZE_REQUEST_OTA_SEND_FIRMWARE_PACKAGE 64 /* variable (max) */
#define APPADMM_FRAME_DATA_SIZE_REQUEST_OTA_START_UPGRADE_PROCEDURE 1

/* Size of response frame data per command */
#define APPADMM_FRAME_DATA_SIZE_RESPONSE_READ_INFORMATION 52
#define APPADMM_FRAME_DATA_SIZE_RESPONSE_READ_DISPLAY 12
#define APPADMM_FRAME_DATA_SIZE_RESPONSE_READ_PROTOCOL_VERSION 4
#define APPADMM_FRAME_DATA_SIZE_RESPONSE_READ_BATTERY_LIFE 4
#define APPADMM_FRAME_DATA_SIZE_RESPONSE_CAL_READING 23
#define APPADMM_FRAME_DATA_SIZE_RESPONSE_READ_MEMORY 64 /* variable (max) */
#define APPADMM_FRAME_DATA_SIZE_RESPONSE_READ_HARMONICS_DATA 50
#define APPADMM_FRAME_DATA_SIZE_RESPONSE_FAILURE 1
#define APPADMM_FRAME_DATA_SIZE_RESPONSE_SUCCESS 0


/* Size of request frame data per command */
#define APPADMM_500_FRAME_DATA_SIZE_REQUEST_READ_ALL_DATA 0
#define APPADMM_500_FRAME_DATA_SIZE_REQUEST_READ_DATALOG_INFO 0
#define APPADMM_500_FRAME_DATA_SIZE_REQUEST_READ_PAUSE_PERIOD_DATA 0
#define APPADMM_500_FRAME_DATA_SIZE_REQUEST_READ_STORE_DATA 0
#define APPADMM_500_FRAME_DATA_SIZE_REQUEST_DOWNLOAD_ENTER 0
#define APPADMM_500_FRAME_DATA_SIZE_REQUEST_DOWNLOAD_EXIT 0
#define APPADMM_500_FRAME_DATA_SIZE_REQUEST_READ_MEMORY 4
#define APPADMM_500_FRAME_DATA_SIZE_REQUEST_WRITE_MODEL_NAME 10
#define APPADMM_500_FRAME_DATA_SIZE_REQUEST_WRITE_SERIAL_NUMBER 8

/* Size of response frame data per command */
#define APPADMM_500_FRAME_DATA_SIZE_RESPONSE_READ_ALL_DATA 54
#define APPADMM_500_FRAME_DATA_SIZE_RESPONSE_READ_DATALOG_INFO 3
#define APPADMM_500_FRAME_DATA_SIZE_RESPONSE_READ_PAUSE_PERIOD_DATA 2
#define APPADMM_500_FRAME_DATA_SIZE_RESPONSE_READ_STORE_DATA 2
#define APPADMM_500_FRAME_DATA_SIZE_RESPONSE_DOWNLOAD_ENTER 0
#define APPADMM_500_FRAME_DATA_SIZE_RESPONSE_DOWNLOAD_EXIT 0
#define APPADMM_500_FRAME_DATA_SIZE_RESPONSE_READ_MEMORY 64 /* max 64 bytes */
#define APPADMM_500_FRAME_DATA_SIZE_RESPONSE_WRITE_MODEL_NAME 1
#define APPADMM_500_FRAME_DATA_SIZE_RESPONSE_WRITE_SERIAL_NUMBER 1

/* Size of request frame data per command */
#define APPADMM_300_FRAME_DATA_SIZE_REQUEST_READ_ALL_DATA 0

/* Size of response frame data per command */
#define APPADMM_300_FRAME_DATA_SIZE_RESPONSE_READ_ALL_DATA 54

/**
 * Begin of word codes (minimum value)
 * All readings on a display higher than that are some sort of wordcode,
 * resolvable or not
 */
#define APPADMM_WORDCODE_TABLE_MIN 0x700000

/* **************************************** */
/* ****** State machine enumerations ****** */
/* **************************************** */

/**
 * Fundamental protocol selection
 * For support of legacy models that cannot properly be autodetected
 */
enum appadmm_protocol_e {
	APPADMM_PROTOCOL_INVALID = 0x00,
	APPADMM_PROTOCOL_GENERIC = 0x01, /**< Modern APPA-Series */
	APPADMM_PROTOCOL_100 = 0x02, /**< Legacy APPA 100 Series */
	APPADMM_PROTOCOL_300 = 0x03, /**< Legacy APPA 300 Series */
	APPADMM_PROTOCOL_500 = 0x04, /**< Legacy APPA 500 Series */
};

/**
 * Data sources
 * Not all of them are available on all devices
 */
enum appadmm_data_source_e {
	APPADMM_DATA_SOURCE_LIVE = 0x00,
	APPADMM_DATA_SOURCE_MEM = 0x01,
	APPADMM_DATA_SOURCE_LOG = 0x02,
};

/**
 * Storage definition
 */
enum appadmm_storage_e {
	APPADMM_STORAGE_MEM = 0x00, /**< Single saved values (hold, etc.) */
	APPADMM_STORAGE_LOG = 0x01, /**< Saved log data in device with samplerate */
};

/**
 * Storage address endianess
 */
enum appadmm_memendian_e {
	APPADMM_MEMENDIAN_LE = 0x00,
	APPADMM_MEMENDIAN_BE = 0x01,
};

/**
 * Channel definition
 */
enum appadmm_channel_e {
	APPADMM_CHANNEL_INVALID = -1,
	APPADMM_CHANNEL_DISPLAY_PRIMARY = 0x00, /**< Primary / "main" */
	APPADMM_CHANNEL_DISPLAY_SECONDARY = 0x01, /**< Secondary / "sub" */
};

/* **************************************** */
/* ****** Message frame enumerations ****** */
/* **************************************** */

/**
 * Possible commands.
 * Calibration and configuration commands not included yet.
 */
enum appadmm_command_e {
	APPADMM_COMMAND_READ_INFORMATION = 0x00, /**< Get information about Model and Brand */
	APPADMM_COMMAND_READ_DISPLAY = 0x01, /**< Get all display readings */
	APPADMM_COMMAND_READ_PROTOCOL_VERSION = 0x03, /**< Read protocol version */
	APPADMM_COMMAND_READ_BATTERY_LIFE = 0x04, /**< Read battery life */
	APPADMM_COMMAND_WRITE_UART_CONFIGURATION = 0x05, /**< Configure UART Interface */
	APPADMM_COMMAND_CAL_READING = 0x10, /**< Read calibration-related reading data */
	APPADMM_COMMAND_READ_MEMORY = 0x1a, /**< Read memory (MEM, LOG, etc.) */
	APPADMM_COMMAND_READ_HARMONICS_DATA = 0x1b, /**< Read harmonics data of clamps */
	APPADMM_COMMAND_FAILURE = 0x70, /**< Slave did not accept last command */
	APPADMM_COMMAND_SUCCESS = 0x7f, /**< Slave accepted last command */
	APPADMM_COMMAND_CAL_ENTER = 0x80, /**< Enter calibration mode */
	APPADMM_COMMAND_CAL_WRITE_FUNCTION_CODE = 0x85, /**< Write calibration function code */
	APPADMM_COMMAND_CAL_WRITE_RANGE_CODE = 0x87, /**< Write calibration range code */
	APPADMM_COMMAND_CAL_WRITE_MEMORY = 0x8a, /**< Write memory */
	APPADMM_COMMAND_CAL_EXIT = 0x8f, /**< Exit calibration mode */
	APPADMM_COMMAND_OTA_ENTER = 0xa0, /**< Enter OTA mode */
	APPADMM_COMMAND_OTA_SEND_INFORMATION = 0xa1, /**< Send OTA information */
	APPADMM_COMMAND_OTA_SEND_FIRMWARE_PACKAGE = 0xa2, /**< Send OTA Firmware package */
	APPADMM_COMMAND_OTA_START_UPGRADE_PROCEDURE = 0xa3, /**< Start Upgrade-Procedure */

	APPADMM_COMMAND_INVALID = -1, /**< Invalid command, internal */
};

/**
 * Possible commands.
 * APPA 300 Series Protocol
 * Calibration and configuration commands not included yet.
 */
enum appadmm_300_command_e {
	APPADMM_300_COMMAND_READ_ALL_DATA = 0x00, /**< Read all data of meter */

	APPADMM_300_COMMAND_INVALID = -1, /**< Invalid command, internal */
};

/**
 * Possible commands.
 * APPA 500 Series Protocol
 * Calibration and configuration commands not included yet.
 */
enum appadmm_500_command_e {
	APPADMM_500_COMMAND_READ_ALL_DATA = 0x00, /**< Read all data of meter */
	APPADMM_500_COMMAND_READ_DATALOG_INFO = 0x11, /**< Read Datalog amount & type */
	APPADMM_500_COMMAND_READ_PAUSE_PERIOD_DATA = 0x12, /**< Read pause & period data amount */
	APPADMM_500_COMMAND_READ_STORE_DATA = 0x13, /**< Read store data amount */
	APPADMM_500_COMMAND_DOWNLOAD_ENTER = 0x18, /**< Enter download mode */
	APPADMM_500_COMMAND_DOWNLOAD_EXIT = 0x19, /**< Exit download mode */
	APPADMM_500_COMMAND_READ_MEMORY = 0x1A, /**< Read memory */
	APPADMM_500_COMMAND_WRITE_MODEL_NAME = 0x81, /**< Write model name to EEPROM */
	APPADMM_500_COMMAND_WRITE_SERIAL_NUMBER = 0x82, /**< Write serial number to EEPROM */

	APPADMM_500_COMMAND_INVALID = -1, /**< Invalid command, internal */
};

/**
 * Currently supported models
 */
enum appadmm_model_id_e {
	/**
	 * Invalid
	 */
	APPADMM_MODEL_ID_INVALID = 0x00,

	/**
	 * Invalid
	 */
	APPADMM_MODEL_ID_OVERFLOW = 0xffff,

	/**
	 * APPA 150 Series
	 */
	APPADMM_MODEL_ID_150 = 0x01,

	/**
	 * APPA 150 Series (BLE)
	 * APPA 155B, 156B, 157B, 158B
	 * BENNING CM 12
	 */
	APPADMM_MODEL_ID_150B = 0x02,

	/**
	 * APPA 200 Series (Optical RS232/USB)
	 * APPA 208
	 */
	APPADMM_MODEL_ID_208 = 0x03,

	/**
	 * APPA 200 Series (Optical RS232/USB, BLE)
	 * APPA 208B
	 */
	APPADMM_MODEL_ID_208B = 0x04,

	/**
	 * APPA 500 Series (Optical RS232/USB)
	 * APPA 506
	 * Sefram 7351
	 */
	APPADMM_MODEL_ID_506 = 0x05,

	/**
	 * APPA 500 Series (Optical RS232/USB, BLE)
	 * APPA 506B
	 * BENNING MM 12
	 * Sefram 7352B
	 */
	APPADMM_MODEL_ID_506B = 0x06,

	/**
	 * Same as APPADMM_MODEL_ID_506B
	 */
	APPADMM_MODEL_ID_506B_2 = 0x600,

	/**
	 * APPA 500 Series (Optical RS232/USB)
	 * APPA 501
	 */
	APPADMM_MODEL_ID_501 = 0x07,

	/**
	 * APPA 500 Series (Optical RS232/USB)
	 * APPA 502
	 */
	APPADMM_MODEL_ID_502 = 0x08,

	/**
	 * APPA S Series (BLE)
	 * APPA S1
	 * RS PRO S1
	 * Sefram 7221
	 */
	APPADMM_MODEL_ID_S1 = 0x09,

	/**
	 * APPA S Series (BLE)
	 * APPA S2
	 * BENNING MM 10-1
	 * RS PRO S2
	 */
	APPADMM_MODEL_ID_S2 = 0x0a,

	/**
	 * APPA S Series (BLE)
	 * APPA S3
	 * BENNING MM 10-PV
	 * RS PRO S3
	 * Sefram 7223
	 */
	APPADMM_MODEL_ID_S3 = 0x0b,

	/**
	 * APPA 170 Series (BLE)
	 * APPA 172B
	 * BENNING CM 9-2
	 */
	APPADMM_MODEL_ID_172 = 0x0c,

	/**
	 * APPA 170 Series (BLE)
	 * APPA 173B
	 * BENNING CM 10-1
	 */
	APPADMM_MODEL_ID_173 = 0x0d,

	/**
	 * APPA 170 Series (BLE)
	 * APPA 175B
	 */
	APPADMM_MODEL_ID_175 = 0x0e,

	/**
	 * APPA 170 Series (BLE)
	 * APPA 177B
	 * BENNING CM 10-PV
	 */
	APPADMM_MODEL_ID_177 = 0x0f,

	/**
	 * APPA sFlex Series (BLE)
	 * APPA sFlex-10A
	 */
	APPADMM_MODEL_ID_SFLEX_10A = 0x10,

	/**
	 * APPA sFlex Series (BLE)
	 * APPA sFlex-18A
	 */
	APPADMM_MODEL_ID_SFLEX_18A = 0x11,

	/**
	 * APPA A Series (BLE)
	 * APPA A17N
	 */
	APPADMM_MODEL_ID_A17N = 0x12,

	/**
	 * APPA S Series (BLE)
	 * APPA S0
	 * Sefram 7220
	 */
	APPADMM_MODEL_ID_S0 = 0x13,

	/**
	 * APPA 170 Series (BLE)
	 * APPA 179B
	 */
	APPADMM_MODEL_ID_179 = 0x14,

	/**
	 * APPA 500 Series (Optical RS232/USB)
	 * APPA 503
	 * CMT 3503
	 * Voltcraft VC-930
	 * ISO-TECH IDM503
	 */
	APPADMM_MODEL_ID_503 = 0x15,

	/**
	 * APPA 500 Series (Optical RS232/USB)
	 * APPA 505
	 * RS PRO IDM505
	 * Sefram 7355
	 */
	APPADMM_MODEL_ID_505 = 0x16,

	/**
	 * Unlisted / Unknown:
	 *
	 * APPA 500 Series (Optical RS232/USB) - EXPERIMENTAL:
	 * APPA 507
	 * CMT 3507
	 * HT Instruments HT8100
	 * (possibly identifies itself as another 500)
	 */

	/* Extended codes: Devices with old and legacy communication protocols
	 * 0xABCD
	 * ABC: Series (505 = 505)
	 * D: Model, if needed
	 */

	/**
	 * APPA 300 Series (if unable to detect a specific model)
	 */
	APPADMM_MODEL_ID_300 = 0x3000,

	/**
	 * APPA 301
	 */
	APPADMM_MODEL_ID_301 = 0x3010,

	/**
	 * APPA 303
	 */
	APPADMM_MODEL_ID_303 = 0x3030,

	/**
	 * APPA 305
	 */
	APPADMM_MODEL_ID_305 = 0x3050,

	/**
	 * APPA 503
	 * Voltcraft VC-930
	 * ISO-TECH IDM503
	 * RS PRO IDM503
	 */
	APPADMM_MODEL_ID_LEGACY_503 = 0x5030,

	/**
	 * APPA 505
	 * Voltcraft VC-950
	 * Sefram 7355?
	 * ISO-TECH IDM503
	 * RS PRO IDM503
	 */
	APPADMM_MODEL_ID_LEGACY_505 = 0x5050,
};

/**
 * Manual / Auto range field values
 */
enum appadmm_autorange_e {
	APPADMM_MANUAL_RANGE = 0x00, /**< Manual ranging */
	APPADMM_AUTO_RANGE = 0x01, /**< Auto range active */
};

/**
 * Manual / Auto test field values
 */
enum appadmm_autotest_e {
	APPADMM_MANUAL_TEST = 0x00, /**< Manual Test */
	APPADMM_AUTO_TEST = 0x01, /**< Auto Test */
};

/**
 * Wordcodes
 *
 * Multimeter will send these codes to indicate a string visible on the
 * display. Works for main and sub.
 */
enum appadmm_wordcode_e {
	APPADMM_WORDCODE_SPACE = 0x700000, /**< Space */
	APPADMM_WORDCODE_FULL = 0x700001, /**< Full */
	APPADMM_WORDCODE_BEEP = 0x700002, /**< Beep */
	APPADMM_WORDCODE_APO = 0x700003, /**< Auto Power-Off */
	APPADMM_WORDCODE_B_LIT = 0x700004, /**< Backlight */
	APPADMM_WORDCODE_HAZ = 0x700005, /**< Hazard */
	APPADMM_WORDCODE_ON = 0x700006, /**< On */
	APPADMM_WORDCODE_OFF = 0x700007, /**< Off */
	APPADMM_WORDCODE_RESET = 0x700008, /**< Reset */
	APPADMM_WORDCODE_START = 0x700009, /**< Start */
	APPADMM_WORDCODE_VIEW = 0x70000a, /**< View */
	APPADMM_WORDCODE_PAUSE = 0x70000b, /**< Pause */
	APPADMM_WORDCODE_FUSE = 0x70000c, /**< Fuse */
	APPADMM_WORDCODE_PROBE = 0x70000d, /**< Probe */
	APPADMM_WORDCODE_DEF = 0x70000e, /**< Definition */
	APPADMM_WORDCODE_CLR = 0x70000f, /**< Clr */
	APPADMM_WORDCODE_ER = 0x700010, /**< Er */
	APPADMM_WORDCODE_ER1 = 0x700011, /**< Er1 */
	APPADMM_WORDCODE_ER2 = 0x700012, /**< Er2 */
	APPADMM_WORDCODE_ER3 = 0x700013, /**< Er3 */
	APPADMM_WORDCODE_DASH = 0x700014, /**< Dash (-----) */
	APPADMM_WORDCODE_DASH1 = 0x700015, /**< Dash1 (-) */
	APPADMM_WORDCODE_TEST = 0x700016, /**< Test */
	APPADMM_WORDCODE_DASH2 = 0x700017, /**< Dash2 (--) */
	APPADMM_WORDCODE_BATT = 0x700018, /**< Battery */
	APPADMM_WORDCODE_DISLT = 0x700019, /**< diSLt */
	APPADMM_WORDCODE_NOISE = 0x70001a, /**< Noise */
	APPADMM_WORDCODE_FILTR = 0x70001b, /**< Filter */
	APPADMM_WORDCODE_PASS = 0x70001c, /**< PASS */
	APPADMM_WORDCODE_NULL = 0x70001d, /**< null */
	APPADMM_WORDCODE_0_20 = 0x70001e, /**< 0 - 20 mA */
	APPADMM_WORDCODE_4_20 = 0x70001f, /**< 4 - 20 mA */
	APPADMM_WORDCODE_RATE = 0x700020, /**< Rate */
	APPADMM_WORDCODE_SAVE = 0x700021, /**< Save */
	APPADMM_WORDCODE_LOAD = 0x700022, /**< Load */
	APPADMM_WORDCODE_YES = 0x700023, /**< Yes */
	APPADMM_WORDCODE_SEND = 0x700024, /**< Send */
	APPADMM_WORDCODE_AHOLD = 0x700025, /**< AUTO HOLD */
	APPADMM_WORDCODE_AUTO = 0x700026, /**< AUTO */
	APPADMM_WORDCODE_CNTIN = 0x700027, /**< Continuity */
	APPADMM_WORDCODE_CAL = 0x700028, /**< CAL */
	APPADMM_WORDCODE_VERSION = 0x700029, /**< Version */
	APPADMM_WORDCODE_OL = 0x70002a, /**< OL (unused) */
	APPADMM_WORDCODE_BAT_FULL = 0x70002b, /**< Battery Full */
	APPADMM_WORDCODE_BAT_HALF = 0x70002c, /**< Battery Half */
	APPADMM_WORDCODE_LO = 0x70002d, /**< Lo */
	APPADMM_WORDCODE_HI = 0x70002e, /**< Hi */
	APPADMM_WORDCODE_DIGIT = 0x70002f, /**< Digits */
	APPADMM_WORDCODE_RDY = 0x700030, /**< Ready */
	APPADMM_WORDCODE_DISC = 0x700031, /**< dISC */
	APPADMM_WORDCODE_OUTF = 0x700032, /**< outF */
	APPADMM_WORDCODE_OLA = 0x700033, /**< OLA */
	APPADMM_WORDCODE_OLV = 0x700034, /**< OLV */
	APPADMM_WORDCODE_OLVA = 0x700035, /**< OLVA */
	APPADMM_WORDCODE_BAD = 0x700036, /**< BAD */
	APPADMM_WORDCODE_TEMP = 0x700037, /**< TEMP */
};

/**
 * Data units
 */
enum appadmm_unit_e {
	APPADMM_UNIT_NONE = 0x00, /**< None */
	APPADMM_UNIT_V = 0x01, /**< V */
	APPADMM_UNIT_MV = 0x02, /**< mV */
	APPADMM_UNIT_A = 0x03, /**< A */
	APPADMM_UNIT_MA = 0x04, /**< mA */
	APPADMM_UNIT_DB = 0x05, /**< dB */
	APPADMM_UNIT_DBM = 0x06, /**< dBm */
	APPADMM_UNIT_MF = 0x07, /**< mF */
	APPADMM_UNIT_UF = 0x08, /**< µF */
	APPADMM_UNIT_NF = 0x09, /**< nF */
	APPADMM_UNIT_GOHM = 0x0a, /**< GΩ */
	APPADMM_UNIT_MOHM = 0x0b, /**< MΩ */
	APPADMM_UNIT_KOHM = 0x0c, /**< kΩ */
	APPADMM_UNIT_OHM = 0x0d, /**< Ω */
	APPADMM_UNIT_PERCENT = 0x0e, /**< Relative percentage value */
	APPADMM_UNIT_MHZ = 0x0f, /**< MHz */
	APPADMM_UNIT_KHZ = 0x10, /**< kHz */
	APPADMM_UNIT_HZ = 0x11, /**< Hz */
	APPADMM_UNIT_DEGC = 0x12, /**< °C */
	APPADMM_UNIT_DEGF = 0x13, /**< °F */
	APPADMM_UNIT_SEC = 0x14, /**< seconds */
	APPADMM_UNIT_MS = 0x15, /**< ms */
	APPADMM_UNIT_US = 0x16, /**< µs */
	APPADMM_UNIT_NS = 0x17, /**< ns */
	APPADMM_UNIT_UA = 0x18, /**< µA */
	APPADMM_UNIT_MIN = 0x19, /**< minutes */
	APPADMM_UNIT_KW = 0x1a, /**< kW */
	APPADMM_UNIT_PF = 0x1b, /**< Power Factor (@TODO maybe pico-farat?) */
};

/**
 * Display range / dot positions
 */
enum appadmm_dot_e {
	APPADMM_DOT_NONE = 0x00,
	APPADMM_DOT_9999_9 = 0x01,
	APPADMM_DOT_999_99 = 0x02,
	APPADMM_DOT_99_999 = 0x03,
	APPADMM_DOT_9_9999 = 0x04,
};

/**
 * OL-Indication values
 */
enum appadmm_overload_e {
	APPADMM_NOT_OVERLOAD = 0x00, /**< non-OL value */
	APPADMM_OVERLOAD = 0x01, /**< OL */
};

/**
 * Data content - Menu, Min/Max/Avg, etc. selection
 */
enum appadmm_data_content_e {
	APPADMM_DATA_CONTENT_MEASURING_DATA = 0x00,
	APPADMM_DATA_CONTENT_FREQUENCY = 0x01,
	APPADMM_DATA_CONTENT_CYCLE = 0x02,
	APPADMM_DATA_CONTENT_DUTY = 0x03,
	APPADMM_DATA_CONTENT_MEMORY_STAMP = 0x04,
	APPADMM_DATA_CONTENT_MEMORY_SAVE = 0x05,
	APPADMM_DATA_CONTENT_MEMORY_LOAD = 0x06,
	APPADMM_DATA_CONTENT_LOG_SAVE = 0x07,
	APPADMM_DATA_CONTENT_LOG_LOAD = 0x08,
	APPADMM_DATA_CONTENT_LOG_RATE = 0x09,
	APPADMM_DATA_CONTENT_REL_DELTA = 0x0a,
	APPADMM_DATA_CONTENT_REL_PERCENT = 0x0b,
	APPADMM_DATA_CONTENT_REL_REFERENCE = 0x0c,
	APPADMM_DATA_CONTENT_MAXIMUM = 0x0d,
	APPADMM_DATA_CONTENT_MINIMUM = 0x0e,
	APPADMM_DATA_CONTENT_AVERAGE = 0x0f,
	APPADMM_DATA_CONTENT_PEAK_HOLD_MAX = 0x10,
	APPADMM_DATA_CONTENT_PEAK_HOLD_MIN = 0x11,
	APPADMM_DATA_CONTENT_DBM = 0x12,
	APPADMM_DATA_CONTENT_DB = 0x13,
	APPADMM_DATA_CONTENT_AUTO_HOLD = 0x14,
	APPADMM_DATA_CONTENT_SETUP = 0x15,
	APPADMM_DATA_CONTENT_LOG_STAMP = 0x16,
	APPADMM_DATA_CONTENT_LOG_MAX = 0x17,
	APPADMM_DATA_CONTENT_LOG_MIN = 0x18,
	APPADMM_DATA_CONTENT_LOG_TP = 0x19,
	APPADMM_DATA_CONTENT_HOLD = 0x1a,
	APPADMM_DATA_CONTENT_CURRENT_OUTPUT = 0x1b,
	APPADMM_DATA_CONTENT_CUR_OUT_0_20MA_PERCENT = 0x1c,
	APPADMM_DATA_CONTENT_CUR_OUT_4_20MA_PERCENT = 0x1d,
};

/**
 * Data content - Menu, Min/Max/Avg, etc. selection
 * APPA 300 Series Protocol
 */
enum appadmm_300_data_content_e {
	APPADMM_300_DATA_CONTENT_NONE = 0x00,
	APPADMM_300_DATA_CONTENT_MEASURING_DATA = 0x01,
	APPADMM_300_DATA_CONTENT_FREQUENCY = 0x02,
	APPADMM_300_DATA_CONTENT_CYCLE = 0x03,
	APPADMM_300_DATA_CONTENT_DUTY = 0x04,
	APPADMM_300_DATA_CONTENT_AMBIENT_TEMPERATURE = 0x05,
	APPADMM_300_DATA_CONTENT_TIME_STAMP = 0x06,
	APPADMM_300_DATA_CONTENT_LOAD = 0x07,
	APPADMM_300_DATA_CONTENT_NUMBER = 0x08,
	APPADMM_300_DATA_CONTENT_STORE = 0x09,
	APPADMM_300_DATA_CONTENT_RECALL = 0x0a,
	APPADMM_300_DATA_CONTENT_RESET = 0x0b,
	APPADMM_300_DATA_CONTENT_AUTO_HOLD = 0x0c,
	APPADMM_300_DATA_CONTENT_MAXIMUM = 0x0d,
	APPADMM_300_DATA_CONTENT_MINIMUM = 0x0e,
	APPADMM_300_DATA_CONTENT_MAXIMUM_MINIMUM = 0x0f,
	APPADMM_300_DATA_CONTENT_PEAK_HOLD_MAX = 0x10,
	APPADMM_300_DATA_CONTENT_PEAK_HOLD_MIN = 0x11,
	APPADMM_300_DATA_CONTENT_PEAK_HOLD_MAX_MIN = 0x12,
	APPADMM_300_DATA_CONTENT_SET_HIGH = 0x13,
	APPADMM_300_DATA_CONTENT_SET_LOW = 0x14,
	APPADMM_300_DATA_CONTENT_HIGH = 0x15,
	APPADMM_300_DATA_CONTENT_LOW = 0x16,
	APPADMM_300_DATA_CONTENT_REL_DELTA = 0x17,
	APPADMM_300_DATA_CONTENT_REL_PERCENT = 0x18,
	APPADMM_300_DATA_CONTENT_REL_REFERENCE = 0x19,
	APPADMM_300_DATA_CONTENT_DBM = 0x1a,
	APPADMM_300_DATA_CONTENT_DB = 0x1b,
	APPADMM_300_DATA_CONTENT_SEND = 0x1c,
	APPADMM_300_DATA_CONTENT_SETUP = 0x1d,
	APPADMM_300_DATA_CONTENT_SET_BEEPER = 0x1e,
};

/**
 * Function codes
 *
 * Basically indicate the rotary position and the secondary function selected
 */
enum appadmm_functioncode_e {
	APPADMM_FUNCTIONCODE_NONE = 0x00,
	APPADMM_FUNCTIONCODE_AC_V = 0x01,
	APPADMM_FUNCTIONCODE_DC_V = 0x02,
	APPADMM_FUNCTIONCODE_AC_MV = 0x03,
	APPADMM_FUNCTIONCODE_DC_MV = 0x04,
	APPADMM_FUNCTIONCODE_OHM = 0x05,
	APPADMM_FUNCTIONCODE_CONTINUITY = 0x06,
	APPADMM_FUNCTIONCODE_DIODE = 0x07,
	APPADMM_FUNCTIONCODE_CAP = 0x08,
	APPADMM_FUNCTIONCODE_AC_A = 0x09,
	APPADMM_FUNCTIONCODE_DC_A = 0x0a,
	APPADMM_FUNCTIONCODE_AC_MA = 0x0b,
	APPADMM_FUNCTIONCODE_DC_MA = 0x0c,
	APPADMM_FUNCTIONCODE_DEGC = 0x0d,
	APPADMM_FUNCTIONCODE_DEGF = 0x0e,
	APPADMM_FUNCTIONCODE_FREQUENCY = 0x0f,
	APPADMM_FUNCTIONCODE_DUTY = 0x10,
	APPADMM_FUNCTIONCODE_HZ_V = 0x11,
	APPADMM_FUNCTIONCODE_HZ_MV = 0x12,
	APPADMM_FUNCTIONCODE_HZ_A = 0x13,
	APPADMM_FUNCTIONCODE_HZ_MA = 0x14,
	APPADMM_FUNCTIONCODE_AC_DC_V = 0x15,
	APPADMM_FUNCTIONCODE_AC_DC_MV = 0x16,
	APPADMM_FUNCTIONCODE_AC_DC_A = 0x17,
	APPADMM_FUNCTIONCODE_AC_DC_MA = 0x18,
	APPADMM_FUNCTIONCODE_LPF_V = 0x19,
	APPADMM_FUNCTIONCODE_LPF_MV = 0x1a,
	APPADMM_FUNCTIONCODE_LPF_A = 0x1b,
	APPADMM_FUNCTIONCODE_LPF_MA = 0x1c,
	APPADMM_FUNCTIONCODE_AC_UA = 0x1d,
	APPADMM_FUNCTIONCODE_DC_UA = 0x1e,
	APPADMM_FUNCTIONCODE_DC_A_OUT = 0x1f,
	APPADMM_FUNCTIONCODE_DC_A_OUT_SLOW_LINEAR = 0x20,
	APPADMM_FUNCTIONCODE_DC_A_OUT_FAST_LINEAR = 0x21,
	APPADMM_FUNCTIONCODE_DC_A_OUT_SLOW_STEP = 0x22,
	APPADMM_FUNCTIONCODE_DC_A_OUT_FAST_STEP = 0x23,
	APPADMM_FUNCTIONCODE_LOOP_POWER = 0x24,
	APPADMM_FUNCTIONCODE_250OHM_HART = 0x25,
	APPADMM_FUNCTIONCODE_VOLT_SENSE = 0x26,
	APPADMM_FUNCTIONCODE_PEAK_HOLD_V = 0x27,
	APPADMM_FUNCTIONCODE_PEAK_HOLD_MV = 0x28,
	APPADMM_FUNCTIONCODE_PEAK_HOLD_A = 0x29,
	APPADMM_FUNCTIONCODE_PEAK_HOLD_MA = 0x2a,
	APPADMM_FUNCTIONCODE_LOZ_AC_V = 0x2b,
	APPADMM_FUNCTIONCODE_LOZ_DC_V = 0x2c,
	APPADMM_FUNCTIONCODE_LOZ_AC_DC_V = 0x2d,
	APPADMM_FUNCTIONCODE_LOZ_LPF_V = 0x2e,
	APPADMM_FUNCTIONCODE_LOZ_HZ_V = 0x2f,
	APPADMM_FUNCTIONCODE_LOZ_PEAK_HOLD_V = 0x30,
	APPADMM_FUNCTIONCODE_BATTERY = 0x31,
	APPADMM_FUNCTIONCODE_AC_W = 0x32,
	APPADMM_FUNCTIONCODE_DC_W = 0x33,
	APPADMM_FUNCTIONCODE_PF = 0x34,
	APPADMM_FUNCTIONCODE_FLEX_AC_A = 0x35,
	APPADMM_FUNCTIONCODE_FLEX_LPF_A = 0x36,
	APPADMM_FUNCTIONCODE_FLEX_PEAK_HOLD_A = 0x37,
	APPADMM_FUNCTIONCODE_FLEX_HZ_A = 0x38,
	APPADMM_FUNCTIONCODE_V_HARM = 0x39,
	APPADMM_FUNCTIONCODE_INRUSH = 0x3a,
	APPADMM_FUNCTIONCODE_A_HARM = 0x3b,
	APPADMM_FUNCTIONCODE_FLEX_INRUSH = 0x3c,
	APPADMM_FUNCTIONCODE_FLEX_A_HARM = 0x3d,
	APPADMM_FUNCTIONCODE_PEAK_HOLD_UA = 0x3e,
	APPADMM_FUNCTIONCODE_AC_UA_HFR = 0x3F,
	APPADMM_FUNCTIONCODE_AC_V_HFR = 0x40,
	APPADMM_FUNCTIONCODE_AC_MV_HFR = 0x41,
	APPADMM_FUNCTIONCODE_AC_A_HFR = 0x42,
	APPADMM_FUNCTIONCODE_AC_MA_HFR = 0x43,
	APPADMM_FUNCTIONCODE_AC_UA_HFR2 = 0x44,
	APPADMM_FUNCTIONCODE_DC_V_PV = 0x45,
	APPADMM_FUNCTIONCODE_AC_V_PV = 0x46,
	APPADMM_FUNCTIONCODE_AC_V_PV_HFR = 0x47,
	APPADMM_FUNCTIONCODE_AC_DC_V_PV = 0x48,
};

/**
 * Function codes
 * APPA 300 Series Protocol
 *
 * Basically indicate the rotary position and the secondary function selected
 * Encoded from Rotary code and Function code
 */
enum appadmm_300_functioncode_e {
	APPADMM_300_FUNCTIONCODE_OFF = 0x0000,
	APPADMM_300_FUNCTIONCODE_DC_V = 0x0100,
	APPADMM_300_FUNCTIONCODE_AC_V = 0x0101,
	APPADMM_300_FUNCTIONCODE_AC_DC_V = 0x0102,
	APPADMM_300_FUNCTIONCODE_DC_MV = 0x0200,
	APPADMM_300_FUNCTIONCODE_AC_MV = 0x0201,
	APPADMM_300_FUNCTIONCODE_AC_DC_MV = 0x0202,
	APPADMM_300_FUNCTIONCODE_OHM = 0x0300,
	APPADMM_300_FUNCTIONCODE_LOW_OHM = 0x0301,
	APPADMM_300_FUNCTIONCODE_DIODE = 0x0400,
	APPADMM_300_FUNCTIONCODE_CONTINUITY = 0x0401,
	APPADMM_300_FUNCTIONCODE_DC_MA = 0x0500,
	APPADMM_300_FUNCTIONCODE_AC_MA = 0x0501,
	APPADMM_300_FUNCTIONCODE_AC_DC_MA = 0x0502,
	APPADMM_300_FUNCTIONCODE_DC_A = 0x0600,
	APPADMM_300_FUNCTIONCODE_AC_A = 0x0601,
	APPADMM_300_FUNCTIONCODE_AC_DC_A = 0x0602,
	APPADMM_300_FUNCTIONCODE_CAP = 0x0700,
	APPADMM_300_FUNCTIONCODE_FREQUENCY = 0x0800,
	APPADMM_300_FUNCTIONCODE_DUTY = 0x0801,
	APPADMM_300_FUNCTIONCODE_DEGC = 0x0900,
	APPADMM_300_FUNCTIONCODE_DEGF = 0x0901,
};

/**
 * Function codes
 * APPA 500 Series Protocol
 *
 * Basically indicate the rotary position and the secondary function selected
 * Encoded from Rotary code and Function code
 */
enum appadmm_500_functioncode_e {
	APPADMM_500_FUNCTIONCODE_DEGC = 0x0000,
	APPADMM_500_FUNCTIONCODE_DEGF = 0x0001,
	APPADMM_500_FUNCTIONCODE_AC_V = 0x0100,
	APPADMM_500_FUNCTIONCODE_DC_V = 0x0101,
	APPADMM_500_FUNCTIONCODE_AC_DC_V = 0x0102,
	APPADMM_500_FUNCTIONCODE_AC_MV = 0x0200,
	APPADMM_500_FUNCTIONCODE_DC_MV = 0x0201,
	APPADMM_500_FUNCTIONCODE_AC_DC_MV = 0x0202,
	APPADMM_500_FUNCTIONCODE_OHM = 0x0300,
	APPADMM_500_FUNCTIONCODE_CONTINUITY = 0x0301,
	APPADMM_500_FUNCTIONCODE_CAP = 0x0302,
	APPADMM_500_FUNCTIONCODE_DIODE = 0x0303,
	APPADMM_500_FUNCTIONCODE_AC_MA = 0x0400,
	APPADMM_500_FUNCTIONCODE_DC_MA = 0x0401,
	APPADMM_500_FUNCTIONCODE_AC_DC_MA = 0x0402,
	APPADMM_500_FUNCTIONCODE_AC_A = 0x0500,
	APPADMM_500_FUNCTIONCODE_DC_A = 0x0501,
	APPADMM_500_FUNCTIONCODE_AC_DC_A = 0x0502,
	APPADMM_500_FUNCTIONCODE_FREQUENCY = 0x0600,
	APPADMM_500_FUNCTIONCODE_DUTY = 0x0601,
};

/**
 * Rotary code
 * APPA 500 Series
 */
enum appadmm_rotarycode_500_e {
	APPADMM_ROTARYCODE_500_NONE = 0x00,
	APPADMM_ROTARYCODE_500_AC_V = 0x01,
	APPADMM_ROTARYCODE_500_AC_MV = 0x02,
	APPADMM_ROTARYCODE_500_DC_V = 0x03,
	APPADMM_ROTARYCODE_500_DC_MV = 0x04,
	APPADMM_ROTARYCODE_500_OHM = 0x05,
	APPADMM_ROTARYCODE_500_A = 0x06,
	APPADMM_ROTARYCODE_500_TEMP = 0x07,
	APPADMM_ROTARYCODE_500_LOZ = 0x08,
	APPADMM_ROTARYCODE_500_INVALID_09 = 0x09,
};

/**
 * Rotary code
 * APPA 200 Series
 */
enum appadmm_rotarycode_200_e {
	APPADMM_ROTARYCODE_200_NONE = 0x00,
	APPADMM_ROTARYCODE_200_AC_V = 0x01,
	APPADMM_ROTARYCODE_200_AC_MV = 0x02,
	APPADMM_ROTARYCODE_200_LOZ = 0x03,
	APPADMM_ROTARYCODE_200_DC_V = 0x04,
	APPADMM_ROTARYCODE_200_DC_MV = 0x05,
	APPADMM_ROTARYCODE_200_OHM = 0x06,
	APPADMM_ROTARYCODE_200_A = 0x07,
	APPADMM_ROTARYCODE_200_FREQ = 0x08,
	APPADMM_ROTARYCODE_200_TEMP = 0x09,
};

/**
 * Rotary code
 * APPA 150 Series
 */
enum appadmm_rotarycode_150_e {
	APPADMM_ROTARYCODE_150_NONE = 0x00,
	APPADMM_ROTARYCODE_150_V = 0x01,
	APPADMM_ROTARYCODE_150_A = 0x02,
	APPADMM_ROTARYCODE_150_W = 0x03,
	APPADMM_ROTARYCODE_150_OHM = 0x04,
	APPADMM_ROTARYCODE_150_CAP = 0x05,
	APPADMM_ROTARYCODE_150_FLEX_CURRENT = 0x06,
	APPADMM_ROTARYCODE_150_TEMP = 0x07,
	APPADMM_ROTARYCODE_150_INVALID_08 = 0x08,
	APPADMM_ROTARYCODE_150_INVALID_09 = 0x09,
};

/* ************************************************************ */
/* ****** Structures representing payload of data frames ****** */
/* ************************************************************ */

/**
 * Display Data in response to APPADMM_COMMAND_READ_DISPLAY
 */
struct appadmm_display_data_s {
	int32_t reading; /**< Measured value or wordcode in raw */

	enum appadmm_dot_e dot; /**< Dot position */
	enum appadmm_unit_e unit; /**< Unit of reading */

	union {
		/**
		 * Data content --> read_display
		 */
		enum appadmm_data_content_e data_content;

		/**
		 * Function code --> read_calibration
		 */
		enum appadmm_functioncode_e log_function_code;
	};
	enum appadmm_overload_e overload; /**< O.L or not */
};

/**
 * Metadata of LOG and MEM information in the device
 */
struct appadmm_storage_info_s {
	int amount; /**< Amount of samples stored */
	int64_t rate; /**< Sample rate (ms) or 0 if not applicable */
	int entry_size; /**< Block size of entry in bytes */
	int entry_count; /**< Amount of entries per memory device */
	int mem_offset; /**< Memory device address offset (start address) */
	int mem_count; /**< Number of memory devices */
	int mem_start; /**< Memory device offset / start position */
	enum appadmm_memendian_e endian; /**< Storage adress endianess */

};

/**
 * Request Data for APPADMM_COMMAND_READ_INFORMATION
 */
struct appadmm_request_data_read_information_s {
	/* No rquest data for this command */
};

/**
 * Response Data for APPADMM_COMMAND_READ_INFORMATION
 */
struct appadmm_response_data_read_information_s {
	char model_name[33]; /**< String 0x20 filled model name of device (branded) */
	char serial_number[17]; /**< String 0x20 filled serial number of device */
	enum appadmm_model_id_e model_id; /*< Model ID Number @appadmm_model_id_e */
	u_int16_t firmware_version; /*< Firmware version */
};

/**
 * Request Data for APPADMM_COMMAND_READ_DISPLAY
 */
struct appadmm_request_data_read_display_s {
	/* No rquest data for this command */
};

/**
 * Response Data for APPADMM_COMMAND_READ_DISPLAY
 */
struct appadmm_response_data_read_display_s {
	enum appadmm_functioncode_e function_code; /**< Function Code */
	enum appadmm_autotest_e auto_test; /**< Auto or manual Test */
	uint8_t range_code; /**< Range code, depending on function_code and unit */
	enum appadmm_autorange_e auto_range; /**< Automatic or manual range */
	struct appadmm_display_data_s
	primary_display_data; /**< Reading of main (lower) display value */
	struct appadmm_display_data_s
	secondary_display_data; /**< Reading of sub (upper) display value */
};

/**
 * Request Data for APPADMM_COMMAND_READ_MEMORY
 */
struct appadmm_request_data_read_memory_s {
	uint8_t device_number; /**< Selection of memory */
	uint16_t memory_address; /**< Address in memory */
	uint8_t data_length; /**< Number of bytes to read, max 64 */
};

/**
 * Response Data for APPADMM_COMMAND_READ_MEMORY
 */
struct appadmm_response_data_read_memory_s {
	uint8_t data[SR_TP_APPA_MAX_DATA_SIZE]; /**< Requested data */
	uint8_t data_length; /**< Length of requested data */
};

/**
 * Request Data for APPADMM_COMMAND_READ_CALIBRATION
 */
struct appadmm_request_data_read_calibration_s {
	/* No rquest data for this command */
};

/**
 * Response Data for APPADMM_COMMAND_READ_CALIBRATION
 */
struct appadmm_response_data_read_calibration_s {

	union {
		enum appadmm_rotarycode_500_e rotary_code_500; /**< APPA 500s rotary code */
		enum appadmm_rotarycode_200_e rotary_code_200; /**< APPA 200s rotary code */
		enum appadmm_rotarycode_150_e rotary_code_150; /**< APPA 150s rotary code */
	};
	enum appadmm_functioncode_e function_code; /**< Function Code */
	struct appadmm_display_data_s
	main_display_data; /**< Reading of main (lower) display value */
	float original_adc_data_1; /**< Original ADC Data 1 */
	float original_adc_data_2; /**< Original ADC Data 2 */
	float offset_data; /**< Offset (debug value) */
	float gain_data; /**< Gain (debug value) */
};

/**
 * Request Data for APPADMM_500_COMMAND_READ_DATALOG_INFO
 * and APPADMM_500_COMMAND_READ_STORE_DATA
 * APPA 500 legacy
 */
struct appadmm_500_request_data_read_amount_s {
	/* No rquest data for this command */
};

/**
 * Response Data for APPADMM_500_COMMAND_READ_DATALOG_INFO
 * and APPADMM_500_COMMAND_READ_STORE_DATA
 * APPA 500 legacy
 */
struct appadmm_500_response_data_read_amount_s {
	uint16_t amount; /**< Amount of data */
};

/* ************************************** */
/* ****** State machine structures ****** */
/* ************************************** */

/**
 * Context
 * saved in sdi->priv and forwarded to all relevant functions
 * similar to _info structure in other drivers
 */
struct appadmm_context {
	struct sr_tp_appa_inst appa_inst; /**< APPA transport protocol instance */
	enum appadmm_protocol_e protocol; /**< APPA API to use */
	gboolean request_pending; /**< Active request state */
	guint64 rate_interval; /**< Internal sample rate interval */

	enum appadmm_model_id_e model_id; /**< Model identifier */

	enum appadmm_data_source_e data_source; /**< Data source */
	struct appadmm_storage_info_s
	storage_info[APPADMM_STORAGE_INFO_COUNT]; /**< LOG and MEM info */

	struct sr_sw_limits limits; /**< Limits for data acquisition */
	int error_counter; /**< retry on ble issues */

	guint64 rate_timer; /**< Internal rate limit timer */
	gboolean rate_sent; /**< Internal limit sent state */
};

/* ***************************************** */
/* ****** Declaration export to api.c ****** */
/* ***************************************** */

/* ****** Generic Protocol ****** */
SR_PRIV int appadmm_op_identify(const struct sr_dev_inst *arg_sdi);
SR_PRIV int appadmm_op_storage_info(const struct sr_dev_inst *arg_sdi);
SR_PRIV int appadmm_acquire_live(int arg_fd, int arg_revents,
	void *arg_cb_data);
SR_PRIV int appadmm_acquire_storage(int arg_fd, int arg_revents,
	void *arg_cb_data);

/* ****** APPA 300 Protocol ****** */
SR_PRIV int appadmm_300_op_identify(const struct sr_dev_inst *arg_sdi);
SR_PRIV int appadmm_300_acquire_live(int arg_fd, int arg_revents,
	void *arg_cb_data);

/* ****** Legacy 500 Protocol ****** */
SR_PRIV int appadmm_500_op_identify(const struct sr_dev_inst *arg_sdi);
SR_PRIV int appadmm_500_op_storage_info(const struct sr_dev_inst *arg_sdi);
SR_PRIV int appadmm_500_acquire_live(int arg_fd, int arg_revents,
	void *arg_cb_data);
SR_PRIV int appadmm_500_acquire_storage(int arg_fd, int arg_revents,
	void *arg_cb_data);

/* ****** Resolvers / Tables ****** */
SR_PRIV const char *appadmm_channel_name(const enum appadmm_channel_e arg_channel);
SR_PRIV const char *appadmm_model_id_name(const enum appadmm_model_id_e arg_model_id);

/* ****** UTIL: Struct handling ****** */
SR_PRIV int appadmm_clear_context(struct appadmm_context *arg_devc);
SR_PRIV int appadmm_clear_storage_info(struct appadmm_storage_info_s *arg_storage_info);

#endif
