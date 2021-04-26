/*
 * This file is part of the libsigrok project.
 *
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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

#ifndef LIBSIGROK_LIBSIGROK_H
#define LIBSIGROK_LIBSIGROK_H

#include <stdio.h>
#include <sys/time.h>
#include <stdint.h>
#include <inttypes.h>
#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file
 *
 * The public libsigrok header file to be used by frontends.
 *
 * This is the only file that libsigrok users (frontends) are supposed to
 * use and \#include. There are other header files which get installed with
 * libsigrok, but those are not meant to be used directly by frontends.
 *
 * The correct way to get/use the libsigrok API functions is:
 *
 * @code{.c}
 *   #include <libsigrok/libsigrok.h>
 * @endcode
 */

/*
 * All possible return codes of libsigrok functions must be listed here.
 * Functions should never return hardcoded numbers as status, but rather
 * use these enum values. All error codes are negative numbers.
 *
 * The error codes are globally unique in libsigrok, i.e. if one of the
 * libsigrok functions returns a "malloc error" it must be exactly the same
 * return value as used by all other functions to indicate "malloc error".
 * There must be no functions which indicate two different errors via the
 * same return code.
 *
 * Also, for compatibility reasons, no defined return codes are ever removed
 * or reused for different errors later. You can only add new entries and
 * return codes, but never remove or redefine existing ones.
 */

/** Status/error codes returned by libsigrok functions. */
enum sr_error_code {
	SR_OK                =  0, /**< No error. */
	SR_ERR               = -1, /**< Generic/unspecified error. */
	SR_ERR_MALLOC        = -2, /**< Malloc/calloc/realloc error. */
	SR_ERR_ARG           = -3, /**< Function argument error. */
	SR_ERR_BUG           = -4, /**< Errors hinting at internal bugs. */
	SR_ERR_SAMPLERATE    = -5, /**< Incorrect samplerate. */
	SR_ERR_NA            = -6, /**< Not applicable. */
	SR_ERR_DEV_CLOSED    = -7, /**< Device is closed, but must be open. */
	SR_ERR_TIMEOUT       = -8, /**< A timeout occurred. */
	SR_ERR_CHANNEL_GROUP = -9, /**< A channel group must be specified. */
	SR_ERR_DATA          =-10, /**< Data is invalid.  */
	SR_ERR_IO            =-11, /**< Input/output error. */

	/* Update sr_strerror()/sr_strerror_name() (error.c) upon changes! */
};

/** Ternary return type for DMM/LCR/etc packet parser validity checks. */
enum sr_valid_code {
	SR_PACKET_INVALID = -1,	/**< Certainly invalid. */
	SR_PACKET_VALID = 0,	/**< Certainly valid. */
	SR_PACKET_NEED_RX = +1,	/**< Need more RX data. */
};

#define SR_MAX_CHANNELNAME_LEN 32

/* Handy little macros */
#define SR_HZ(n)  (n)
#define SR_KHZ(n) ((n) * UINT64_C(1000))
#define SR_MHZ(n) ((n) * UINT64_C(1000000))
#define SR_GHZ(n) ((n) * UINT64_C(1000000000))

#define SR_HZ_TO_NS(n) (UINT64_C(1000000000) / (n))

/** libsigrok loglevels. */
enum sr_loglevel {
	SR_LOG_NONE = 0, /**< Output no messages at all. */
	SR_LOG_ERR  = 1, /**< Output error messages. */
	SR_LOG_WARN = 2, /**< Output warnings. */
	SR_LOG_INFO = 3, /**< Output informational messages. */
	SR_LOG_DBG  = 4, /**< Output debug messages. */
	SR_LOG_SPEW = 5, /**< Output very noisy debug messages. */
};

/*
 * Use SR_API to mark public API symbols, and SR_PRIV for private symbols.
 *
 * Variables and functions marked 'static' are private already and don't
 * need SR_PRIV. However, functions which are not static (because they need
 * to be used in other libsigrok-internal files) but are also not meant to
 * be part of the public libsigrok API, must use SR_PRIV.
 *
 * This uses the 'visibility' feature of gcc (requires gcc >= 4.0).
 *
 * This feature is not available on MinGW/Windows, as it is a feature of
 * ELF files and MinGW/Windows uses PE files.
 *
 * Details: http://gcc.gnu.org/wiki/Visibility
 */

/* Marks public libsigrok API symbols. */
#ifndef _WIN32
#define SR_API __attribute__((visibility("default")))
#else
#define SR_API
#endif

/* Marks private, non-public libsigrok symbols (not part of the API). */
#ifndef _WIN32
#define SR_PRIV __attribute__((visibility("hidden")))
#else
#define SR_PRIV
#endif

/** Type definition for callback function for data reception. */
typedef int (*sr_receive_data_callback)(int fd, int revents, void *cb_data);

/** Data types used by sr_config_info(). */
enum sr_datatype {
	SR_T_UINT64 = 10000,
	SR_T_STRING,
	SR_T_BOOL,
	SR_T_FLOAT,
	SR_T_RATIONAL_PERIOD,
	SR_T_RATIONAL_VOLT,
	SR_T_KEYVALUE,
	SR_T_UINT64_RANGE,
	SR_T_DOUBLE_RANGE,
	SR_T_INT32,
	SR_T_MQ,

	/* Update sr_variant_type_get() (hwdriver.c) upon changes! */
};

/** Value for sr_datafeed_packet.type. */
enum sr_packettype {
	/** Payload is sr_datafeed_header. */
	SR_DF_HEADER = 10000,
	/** End of stream (no further data). */
	SR_DF_END,
	/** Payload is struct sr_datafeed_meta */
	SR_DF_META,
	/** The trigger matched at this point in the data feed. No payload. */
	SR_DF_TRIGGER,
	/** Payload is struct sr_datafeed_logic. */
	SR_DF_LOGIC,
	/** Beginning of frame. No payload. */
	SR_DF_FRAME_BEGIN,
	/** End of frame. No payload. */
	SR_DF_FRAME_END,
	/** Payload is struct sr_datafeed_analog. */
	SR_DF_ANALOG,

	/* Update datafeed_dump() (session.c) upon changes! */
};

/** Measured quantity, sr_analog_meaning.mq. */
enum sr_mq {
	SR_MQ_VOLTAGE = 10000,
	SR_MQ_CURRENT,
	SR_MQ_RESISTANCE,
	SR_MQ_CAPACITANCE,
	SR_MQ_TEMPERATURE,
	SR_MQ_FREQUENCY,
	/** Duty cycle, e.g. on/off ratio. */
	SR_MQ_DUTY_CYCLE,
	/** Continuity test. */
	SR_MQ_CONTINUITY,
	SR_MQ_PULSE_WIDTH,
	SR_MQ_CONDUCTANCE,
	/** Electrical power, usually in W, or dBm. */
	SR_MQ_POWER,
	/** Gain (a transistor's gain, or hFE, for example). */
	SR_MQ_GAIN,
	/** Logarithmic representation of sound pressure relative to a
	 * reference value. */
	SR_MQ_SOUND_PRESSURE_LEVEL,
	/** Carbon monoxide level */
	SR_MQ_CARBON_MONOXIDE,
	/** Humidity */
	SR_MQ_RELATIVE_HUMIDITY,
	/** Time */
	SR_MQ_TIME,
	/** Wind speed */
	SR_MQ_WIND_SPEED,
	/** Pressure */
	SR_MQ_PRESSURE,
	/** Parallel inductance (LCR meter model). */
	SR_MQ_PARALLEL_INDUCTANCE,
	/** Parallel capacitance (LCR meter model). */
	SR_MQ_PARALLEL_CAPACITANCE,
	/** Parallel resistance (LCR meter model). */
	SR_MQ_PARALLEL_RESISTANCE,
	/** Series inductance (LCR meter model). */
	SR_MQ_SERIES_INDUCTANCE,
	/** Series capacitance (LCR meter model). */
	SR_MQ_SERIES_CAPACITANCE,
	/** Series resistance (LCR meter model). */
	SR_MQ_SERIES_RESISTANCE,
	/** Dissipation factor. */
	SR_MQ_DISSIPATION_FACTOR,
	/** Quality factor. */
	SR_MQ_QUALITY_FACTOR,
	/** Phase angle. */
	SR_MQ_PHASE_ANGLE,
	/** Difference from reference value. */
	SR_MQ_DIFFERENCE,
	/** Count. */
	SR_MQ_COUNT,
	/** Power factor. */
	SR_MQ_POWER_FACTOR,
	/** Apparent power */
	SR_MQ_APPARENT_POWER,
	/** Mass */
	SR_MQ_MASS,
	/** Harmonic ratio */
	SR_MQ_HARMONIC_RATIO,
	/** Energy. */
	SR_MQ_ENERGY,
	/** Electric charge. */
	SR_MQ_ELECTRIC_CHARGE,

	/* Update sr_key_info_mq[] (hwdriver.c) upon changes! */
};

/** Unit of measured quantity, sr_analog_meaning.unit. */
enum sr_unit {
	/** Volt */
	SR_UNIT_VOLT = 10000,
	/** Ampere (current). */
	SR_UNIT_AMPERE,
	/** Ohm (resistance). */
	SR_UNIT_OHM,
	/** Farad (capacity). */
	SR_UNIT_FARAD,
	/** Kelvin (temperature). */
	SR_UNIT_KELVIN,
	/** Degrees Celsius (temperature). */
	SR_UNIT_CELSIUS,
	/** Degrees Fahrenheit (temperature). */
	SR_UNIT_FAHRENHEIT,
	/** Hertz (frequency, 1/s, [Hz]). */
	SR_UNIT_HERTZ,
	/** Percent value. */
	SR_UNIT_PERCENTAGE,
	/** Boolean value. */
	SR_UNIT_BOOLEAN,
	/** Time in seconds. */
	SR_UNIT_SECOND,
	/** Unit of conductance, the inverse of resistance. */
	SR_UNIT_SIEMENS,
	/**
	 * An absolute measurement of power, in decibels, referenced to
	 * 1 milliwatt (dBm).
	 */
	SR_UNIT_DECIBEL_MW,
	/** Voltage in decibel, referenced to 1 volt (dBV). */
	SR_UNIT_DECIBEL_VOLT,
	/**
	 * Measurements that intrinsically do not have units attached, such
	 * as ratios, gains, etc. Specifically, a transistor's gain (hFE) is
	 * a unitless quantity, for example.
	 */
	SR_UNIT_UNITLESS,
	/** Sound pressure level, in decibels, relative to 20 micropascals. */
	SR_UNIT_DECIBEL_SPL,
	/**
	 * Normalized (0 to 1) concentration of a substance or compound with 0
	 * representing a concentration of 0%, and 1 being 100%. This is
	 * represented as the fraction of number of particles of the substance.
	 */
	SR_UNIT_CONCENTRATION,
	/** Revolutions per minute. */
	SR_UNIT_REVOLUTIONS_PER_MINUTE,
	/** Apparent power [VA]. */
	SR_UNIT_VOLT_AMPERE,
	/** Real power [W]. */
	SR_UNIT_WATT,
	/** Energy (consumption) in watt hour [Wh]. */
	SR_UNIT_WATT_HOUR,
	/** Wind speed in meters per second. */
	SR_UNIT_METER_SECOND,
	/** Pressure in hectopascal */
	SR_UNIT_HECTOPASCAL,
	/** Relative humidity assuming air temperature of 293 Kelvin (%rF). */
	SR_UNIT_HUMIDITY_293K,
	/** Plane angle in 1/360th of a full circle. */
	SR_UNIT_DEGREE,
	/** Henry (inductance). */
	SR_UNIT_HENRY,
	/** Mass in gram [g]. */
	SR_UNIT_GRAM,
	/** Mass in carat [ct]. */
	SR_UNIT_CARAT,
	/** Mass in ounce [oz]. */
	SR_UNIT_OUNCE,
	/** Mass in troy ounce [oz t]. */
	SR_UNIT_TROY_OUNCE,
	/** Mass in pound [lb]. */
	SR_UNIT_POUND,
	/** Mass in pennyweight [dwt]. */
	SR_UNIT_PENNYWEIGHT,
	/** Mass in grain [gr]. */
	SR_UNIT_GRAIN,
	/** Mass in tael (variants: Hong Kong, Singapore/Malaysia, Taiwan) */
	SR_UNIT_TAEL,
	/** Mass in momme. */
	SR_UNIT_MOMME,
	/** Mass in tola. */
	SR_UNIT_TOLA,
	/** Pieces (number of items). */
	SR_UNIT_PIECE,
	/** Energy in joule. */
	SR_UNIT_JOULE,
	/** Electric charge in coulomb. */
	SR_UNIT_COULOMB,
	/** Electric charge in ampere hour [Ah]. */
	SR_UNIT_AMPERE_HOUR,

	/*
	 * Update unit_strings[] (analog.c) and fancyprint() (output/analog.c)
	 * upon changes!
	 */
};

/** Values for sr_analog_meaning.mqflags. */
enum sr_mqflag {
	/** Voltage measurement is alternating current (AC). */
	SR_MQFLAG_AC = 0x01,
	/** Voltage measurement is direct current (DC). */
	SR_MQFLAG_DC = 0x02,
	/** This is a true RMS measurement. */
	SR_MQFLAG_RMS = 0x04,
	/** Value is voltage drop across a diode, or NAN. */
	SR_MQFLAG_DIODE = 0x08,
	/** Device is in "hold" mode (repeating the last measurement). */
	SR_MQFLAG_HOLD = 0x10,
	/** Device is in "max" mode, only updating upon a new max value. */
	SR_MQFLAG_MAX = 0x20,
	/** Device is in "min" mode, only updating upon a new min value. */
	SR_MQFLAG_MIN = 0x40,
	/** Device is in autoranging mode. */
	SR_MQFLAG_AUTORANGE = 0x80,
	/** Device is in relative mode. */
	SR_MQFLAG_RELATIVE = 0x100,
	/** Sound pressure level is A-weighted in the frequency domain,
	 * according to IEC 61672:2003. */
	SR_MQFLAG_SPL_FREQ_WEIGHT_A = 0x200,
	/** Sound pressure level is C-weighted in the frequency domain,
	 * according to IEC 61672:2003. */
	SR_MQFLAG_SPL_FREQ_WEIGHT_C = 0x400,
	/** Sound pressure level is Z-weighted (i.e. not at all) in the
	 * frequency domain, according to IEC 61672:2003. */
	SR_MQFLAG_SPL_FREQ_WEIGHT_Z = 0x800,
	/** Sound pressure level is not weighted in the frequency domain,
	 * albeit without standards-defined low and high frequency limits. */
	SR_MQFLAG_SPL_FREQ_WEIGHT_FLAT = 0x1000,
	/** Sound pressure level measurement is S-weighted (1s) in the
	 * time domain. */
	SR_MQFLAG_SPL_TIME_WEIGHT_S = 0x2000,
	/** Sound pressure level measurement is F-weighted (125ms) in the
	 * time domain. */
	SR_MQFLAG_SPL_TIME_WEIGHT_F = 0x4000,
	/** Sound pressure level is time-averaged (LAT), also known as
	 * Equivalent Continuous A-weighted Sound Level (LEQ). */
	SR_MQFLAG_SPL_LAT = 0x8000,
	/** Sound pressure level represented as a percentage of measurements
	 * that were over a preset alarm level. */
	SR_MQFLAG_SPL_PCT_OVER_ALARM = 0x10000,
	/** Time is duration (as opposed to epoch, ...). */
	SR_MQFLAG_DURATION = 0x20000,
	/** Device is in "avg" mode, averaging upon each new value. */
	SR_MQFLAG_AVG = 0x40000,
	/** Reference value shown. */
	SR_MQFLAG_REFERENCE = 0x80000,
	/** Unstable value (hasn't settled yet). */
	SR_MQFLAG_UNSTABLE = 0x100000,
	/** Measurement is four wire (e.g. Kelvin connection). */
	SR_MQFLAG_FOUR_WIRE = 0x200000,

	/*
	 * Update mq_strings[] (analog.c) and fancyprint() (output/analog.c)
	 * upon changes!
	 */
};

enum sr_trigger_matches {
	SR_TRIGGER_ZERO = 1,
	SR_TRIGGER_ONE,
	SR_TRIGGER_RISING,
	SR_TRIGGER_FALLING,
	SR_TRIGGER_EDGE,
	SR_TRIGGER_OVER,
	SR_TRIGGER_UNDER,
};

/** The representation of a trigger, consisting of one or more stages
 * containing one or more matches on a channel.
 */
struct sr_trigger {
	/** A name for this trigger. This may be NULL if none is needed. */
	char *name;
	/** List of pointers to struct sr_trigger_stage. */
	GSList *stages;
};

/** A trigger stage. */
struct sr_trigger_stage {
	/** Starts at 0. */
	int stage;
	/** List of pointers to struct sr_trigger_match. */
	GSList *matches;
};

/** A channel to match and what to match it on. */
struct sr_trigger_match {
	/** The channel to trigger on. */
	struct sr_channel *channel;
	/** The trigger match to use.
	 * For logic channels, only the following matches may be used:
	 * SR_TRIGGER_ZERO
	 * SR_TRIGGER_ONE
	 * SR_TRIGGER_RISING
	 * SR_TRIGGER_FALLING
	 * SR_TRIGGER_EDGE
	 *
	 * For analog channels, only these matches may be used:
	 * SR_TRIGGER_RISING
	 * SR_TRIGGER_FALLING
	 * SR_TRIGGER_OVER
	 * SR_TRIGGER_UNDER
	 *
	 */
	int match;
	/** If the trigger match is one of SR_TRIGGER_OVER or SR_TRIGGER_UNDER,
	 * this contains the value to compare against. */
	float value;
};

/**
 * @struct sr_context
 * Opaque structure representing a libsigrok context.
 *
 * None of the fields of this structure are meant to be accessed directly.
 *
 * @see sr_init(), sr_exit().
 */
struct sr_context;

/**
 * @struct sr_session
 * Opaque structure representing a libsigrok session.
 *
 * None of the fields of this structure are meant to be accessed directly.
 *
 * @see sr_session_new(), sr_session_destroy().
 */
struct sr_session;

struct sr_rational {
	/** Numerator of the rational number. */
	int64_t p;
	/** Denominator of the rational number. */
	uint64_t q;
};

/** Packet in a sigrok data feed. */
struct sr_datafeed_packet {
	uint16_t type;
	const void *payload;
};

/** Header of a sigrok data feed. */
struct sr_datafeed_header {
	int feed_version;
	struct timeval starttime;
};

/** Datafeed payload for type SR_DF_META. */
struct sr_datafeed_meta {
	GSList *config;
};

/** Logic datafeed payload for type SR_DF_LOGIC. */
struct sr_datafeed_logic {
	uint64_t length;
	uint16_t unitsize;
	void *data;
};

/** Analog datafeed payload for type SR_DF_ANALOG. */
struct sr_datafeed_analog {
	void *data;
	uint32_t num_samples;
	struct sr_analog_encoding *encoding;
	struct sr_analog_meaning *meaning;
	struct sr_analog_spec *spec;
};

struct sr_analog_encoding {
	uint8_t unitsize;
	gboolean is_signed;
	gboolean is_float;
	gboolean is_bigendian;
	/**
	 * Number of significant digits after the decimal point if positive,
	 * or number of non-significant digits before the decimal point if
	 * negative (refers to the value we actually read on the wire).
	 */
	int8_t digits;
	gboolean is_digits_decimal;
	struct sr_rational scale;
	struct sr_rational offset;
};

struct sr_analog_meaning {
	enum sr_mq mq;
	enum sr_unit unit;
	enum sr_mqflag mqflags;
	GSList *channels;
};

struct sr_analog_spec {
	/**
	 * Number of significant digits after the decimal point if positive,
	 * or number of non-significant digits before the decimal point if
	 * negative (refers to vendor specifications/datasheet or actual
	 * device display).
	 */
	int8_t spec_digits;
};

/** Generic option struct used by various subsystems. */
struct sr_option {
	/* Short name suitable for commandline usage, [a-z0-9-]. */
	const char *id;
	/* Short name suitable for GUI usage, can contain UTF-8. */
	const char *name;
	/* Description of the option, in a sentence. */
	const char *desc;
	/* Default value for this option. */
	GVariant *def;
	/* List of possible values, if this is an option with few values. */
	GSList *values;
};

/** Resource type.
 * @since 0.4.0
 */
enum sr_resource_type {
	SR_RESOURCE_FIRMWARE = 1,
};

/** Resource descriptor.
 * @since 0.4.0
 */
struct sr_resource {
	/** Size of resource in bytes; set by resource open callback. */
	uint64_t size;
	/** File handle or equivalent; set by resource open callback. */
	void *handle;
	/** Resource type (SR_RESOURCE_FIRMWARE, ...) */
	int type;
};

/** Output module flags. */
enum sr_output_flag {
	/** If set, this output module writes the output itself. */
	SR_OUTPUT_INTERNAL_IO_HANDLING = 0x01,
};

struct sr_input;
struct sr_input_module;
struct sr_output;
struct sr_output_module;
struct sr_transform;
struct sr_transform_module;

/** Constants for channel type. */
enum sr_channeltype {
	/** Channel type is logic channel. */
	SR_CHANNEL_LOGIC = 10000,
	/** Channel type is analog channel. */
	SR_CHANNEL_ANALOG,
};

/** Information on single channel. */
struct sr_channel {
	/** The device this channel is attached to. */
	struct sr_dev_inst *sdi;
	/** The index of this channel, starting at 0. Logic channels will
	 * be encoded according to this index in SR_DF_LOGIC packets. */
	int index;
	/** Channel type (SR_CHANNEL_LOGIC, ...) */
	int type;
	/** Is this channel enabled? */
	gboolean enabled;
	/** Name of channel. */
	char *name;
	/** Private data for driver use. */
	void *priv;
};

/** Structure for groups of channels that have common properties. */
struct sr_channel_group {
	/** Name of the channel group. */
	char *name;
	/** List of sr_channel structs of the channels belonging to this group. */
	GSList *channels;
	/** Private data for driver use. */
	void *priv;
};

/** Used for setting or getting value of a config item. */
struct sr_config {
	/** Config key like SR_CONF_CONN, etc. */
	uint32_t key;
	/** Key-specific data. */
	GVariant *data;
};

enum sr_keytype {
	SR_KEY_CONFIG,
	SR_KEY_MQ,
	SR_KEY_MQFLAGS,
};

/** Information about a key. */
struct sr_key_info {
	/** Config key like SR_CONF_CONN, MQ value like SR_MQ_VOLTAGE, etc. */
	uint32_t key;
	/** Data type like SR_T_STRING, etc if applicable. */
	int datatype;
	/** Short, lowercase ID string, e.g. "serialcomm", "voltage". */
	const char *id;
	/** Full capitalized name, e.g. "Serial communication". */
	const char *name;
	/** Verbose description (unused currently). */
	const char *description;
};

/** Configuration capabilities. */
enum sr_configcap {
	/** Value can be read. */
	SR_CONF_GET = (1UL << 31),
	/** Value can be written. */
	SR_CONF_SET = (1UL << 30),
	/** Possible values can be enumerated. */
	SR_CONF_LIST = (1UL << 29),
};

/** Configuration keys */
enum sr_configkey {
	/*--- Device classes ------------------------------------------------*/

	/** The device can act as logic analyzer. */
	SR_CONF_LOGIC_ANALYZER = 10000,

	/** The device can act as an oscilloscope. */
	SR_CONF_OSCILLOSCOPE,

	/** The device can act as a multimeter. */
	SR_CONF_MULTIMETER,

	/** The device is a demo device. */
	SR_CONF_DEMO_DEV,

	/** The device can act as a sound level meter. */
	SR_CONF_SOUNDLEVELMETER,

	/** The device can measure temperature. */
	SR_CONF_THERMOMETER,

	/** The device can measure humidity. */
	SR_CONF_HYGROMETER,

	/** The device can measure energy consumption. */
	SR_CONF_ENERGYMETER,

	/** The device can act as a signal demodulator. */
	SR_CONF_DEMODULATOR,

	/** The device can act as a programmable power supply. */
	SR_CONF_POWER_SUPPLY,

	/** The device can act as an LCR meter. */
	SR_CONF_LCRMETER,

	/** The device can act as an electronic load. */
	SR_CONF_ELECTRONIC_LOAD,

	/** The device can act as a scale. */
	SR_CONF_SCALE,

	/** The device can act as a function generator. */
	SR_CONF_SIGNAL_GENERATOR,

	/** The device can measure power. */
	SR_CONF_POWERMETER,

	/**
	 * The device can switch between multiple sources, e.g. a relay actuator
	 * or multiplexer.
	 */
	SR_CONF_MULTIPLEXER,

	/* Update sr_key_info_config[] (hwdriver.c) upon changes! */

	/*--- Driver scan options -------------------------------------------*/

	/**
	 * Specification on how to connect to a device.
	 *
	 * In combination with SR_CONF_SERIALCOMM, this is a serial port in
	 * the form which makes sense to the OS (e.g., /dev/ttyS0).
	 * Otherwise this specifies a USB device, either in the form of
	 * @verbatim <bus>.<address> @endverbatim (decimal, e.g. 1.65) or
	 * @verbatim <vendorid>.<productid> @endverbatim
	 * (hexadecimal, e.g. 1d6b.0001).
	 */
	SR_CONF_CONN = 20000,

	/**
	 * Serial communication specification, in the form:
	 *
	 *   @verbatim <baudrate>/<databits><parity><stopbits> @endverbatim
	 *
	 * Example: 9600/8n1
	 *
	 * The string may also be followed by one or more special settings,
	 * in the form "/key=value". Supported keys and their values are:
	 *
	 * rts    0,1    set the port's RTS pin to low or high
	 * dtr    0,1    set the port's DTR pin to low or high
	 * flow   0      no flow control
	 *        1      hardware-based (RTS/CTS) flow control
	 *        2      software-based (XON/XOFF) flow control
	 *
	 * This is always an optional parameter, since a driver typically
	 * knows the speed at which the device wants to communicate.
	 */
	SR_CONF_SERIALCOMM,

	/**
	 * Modbus slave address specification.
	 *
	 * This is always an optional parameter, since a driver typically
	 * knows the default slave address of the device.
	 */
	SR_CONF_MODBUSADDR,

	/**
	 * User specified forced driver attachment to unknown devices.
	 *
	 * By design the interpretation of the string depends on the
	 * specific driver. It typically would be either a replacement
	 * '*IDN?' response value, or a sub-driver name. But could also
	 * be anything else and totally arbitrary.
	 */
	SR_CONF_FORCE_DETECT,

	/* Update sr_key_info_config[] (hwdriver.c) upon changes! */

	/*--- Device (or channel group) configuration -----------------------*/

	/** The device supports setting its samplerate, in Hz. */
	SR_CONF_SAMPLERATE = 30000,

	/** The device supports setting a pre/post-trigger capture ratio. */
	SR_CONF_CAPTURE_RATIO,

	/** The device supports setting a pattern (pattern generator mode). */
	SR_CONF_PATTERN_MODE,

	/** The device supports run-length encoding (RLE). */
	SR_CONF_RLE,

	/** The device supports setting trigger slope. */
	SR_CONF_TRIGGER_SLOPE,

	/** The device supports averaging. */
	SR_CONF_AVERAGING,

	/**
	 * The device supports setting number of samples to be
	 * averaged over.
	 */
	SR_CONF_AVG_SAMPLES,

	/** Trigger source. */
	SR_CONF_TRIGGER_SOURCE,

	/** Horizontal trigger position. */
	SR_CONF_HORIZ_TRIGGERPOS,

	/** Buffer size. */
	SR_CONF_BUFFERSIZE,

	/** Time base. */
	SR_CONF_TIMEBASE,

	/** Filter. */
	SR_CONF_FILTER,

	/** Volts/div. */
	SR_CONF_VDIV,

	/** Coupling. */
	SR_CONF_COUPLING,

	/** Trigger matches. */
	SR_CONF_TRIGGER_MATCH,

	/** The device supports setting its sample interval, in ms. */
	SR_CONF_SAMPLE_INTERVAL,

	/** Number of horizontal divisions, as related to SR_CONF_TIMEBASE. */
	SR_CONF_NUM_HDIV,

	/** Number of vertical divisions, as related to SR_CONF_VDIV. */
	SR_CONF_NUM_VDIV,

	/** Sound pressure level frequency weighting. */
	SR_CONF_SPL_WEIGHT_FREQ,

	/** Sound pressure level time weighting. */
	SR_CONF_SPL_WEIGHT_TIME,

	/** Sound pressure level measurement range. */
	SR_CONF_SPL_MEASUREMENT_RANGE,

	/** Max hold mode. */
	SR_CONF_HOLD_MAX,

	/** Min hold mode. */
	SR_CONF_HOLD_MIN,

	/** Logic low-high threshold range. */
	SR_CONF_VOLTAGE_THRESHOLD,

	/** The device supports using an external clock. */
	SR_CONF_EXTERNAL_CLOCK,

	/**
	 * The device supports swapping channels. Typical this is between
	 * buffered and unbuffered channels.
	 */
	SR_CONF_SWAP,

	/** Center frequency.
	 * The input signal is downmixed by this frequency before the ADC
	 * anti-aliasing filter.
	 */
	SR_CONF_CENTER_FREQUENCY,

	/** The device supports setting the number of logic channels. */
	SR_CONF_NUM_LOGIC_CHANNELS,

	/** The device supports setting the number of analog channels. */
	SR_CONF_NUM_ANALOG_CHANNELS,

	/**
	 * Current voltage.
	 * @arg type: double
	 * @arg get: get measured voltage
	 */
	SR_CONF_VOLTAGE,

	/**
	 * Maximum target voltage.
	 * @arg type: double
	 * @arg get: get target voltage
	 * @arg set: change target voltage
	 */
	SR_CONF_VOLTAGE_TARGET,

	/**
	 * Current current.
	 * @arg type: double
	 * @arg get: get measured current
	 */
	SR_CONF_CURRENT,

	/**
	 * Current limit.
	 * @arg type: double
	 * @arg get: get current limit
	 * @arg set: change current limit
	 */
	SR_CONF_CURRENT_LIMIT,

	/**
	 * Enabling/disabling channel.
	 * @arg type: boolean
	 * @arg get: @b true if currently enabled
	 * @arg set: enable/disable
	 */
	SR_CONF_ENABLED,

	/**
	 * Channel configuration.
	 * @arg type: string
	 * @arg get: get current setting
	 * @arg set: change current setting
	 * @arg list: array of possible values
	 */
	SR_CONF_CHANNEL_CONFIG,

	/**
	 * Over-voltage protection (OVP) feature
	 * @arg type: boolean
	 * @arg get: @b true if currently enabled
	 * @arg set: enable/disable
	 */
	SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED,

	/**
	 * Over-voltage protection (OVP) active
	 * @arg type: boolean
	 * @arg get: @b true if device has activated OVP, i.e. the output voltage
	 *      exceeds the over-voltage protection threshold.
	 */
	SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE,

	/**
	 * Over-voltage protection (OVP) threshold
	 * @arg type: double (voltage)
	 * @arg get: get current threshold
	 * @arg set: set new threshold
	 */
	SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD,

	/**
	 * Over-current protection (OCP) feature
	 * @arg type: boolean
	 * @arg get: @b true if currently enabled
	 * @arg set: enable/disable
	 */
	SR_CONF_OVER_CURRENT_PROTECTION_ENABLED,

	/**
	 * Over-current protection (OCP) active
	 * @arg type: boolean
	 * @arg get: @b true if device has activated OCP, i.e. the current current
	 *      exceeds the over-current protection threshold.
	 */
	SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE,

	/**
	 * Over-current protection (OCP) threshold
	 * @arg type: double (current)
	 * @arg get: get current threshold
	 * @arg set: set new threshold
	 */
	SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD,

	/** Choice of clock edge for external clock ("r" or "f"). */
	SR_CONF_CLOCK_EDGE,

	/** Amplitude of a source without strictly-defined MQ. */
	SR_CONF_AMPLITUDE,

	/**
	 * Channel regulation
	 * get: "CV", "CC" or "UR", denoting constant voltage, constant current
	 *      or unregulated.
	 *      "CC-" denotes a power supply in current sink mode (e.g. HP 66xxB).
	 *      "" is used when there is no regulation, e.g. the output is disabled.
	 */
	SR_CONF_REGULATION,

	/** Over-temperature protection (OTP) */
	SR_CONF_OVER_TEMPERATURE_PROTECTION,

	/** Output frequency in Hz. */
	SR_CONF_OUTPUT_FREQUENCY,

	/** Output frequency target in Hz. */
	SR_CONF_OUTPUT_FREQUENCY_TARGET,

	/** Measured quantity. */
	SR_CONF_MEASURED_QUANTITY,

	/** Equivalent circuit model. */
	SR_CONF_EQUIV_CIRCUIT_MODEL,

	/** Over-temperature protection (OTP) active. */
	SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE,

	/** Under-voltage condition. */
	SR_CONF_UNDER_VOLTAGE_CONDITION,

	/** Under-voltage condition active. */
	SR_CONF_UNDER_VOLTAGE_CONDITION_ACTIVE,

	/** Trigger level. */
	SR_CONF_TRIGGER_LEVEL,

	/** Under-voltage condition threshold. */
	SR_CONF_UNDER_VOLTAGE_CONDITION_THRESHOLD,

	/**
	 * Which external clock source to use if the device supports
	 * multiple external clock channels.
	 */
	SR_CONF_EXTERNAL_CLOCK_SOURCE,

	/** Offset of a source without strictly-defined MQ. */
	SR_CONF_OFFSET,

	/** The device supports setting a pattern for the logic trigger. */
	SR_CONF_TRIGGER_PATTERN,

	/** High resolution mode. */
	SR_CONF_HIGH_RESOLUTION,

	/** Peak detection. */
	SR_CONF_PEAK_DETECTION,

	/** Logic threshold: predefined levels (TTL, ECL, CMOS, etc). */
	SR_CONF_LOGIC_THRESHOLD,

	/** Logic threshold: custom numerical value. */
	SR_CONF_LOGIC_THRESHOLD_CUSTOM,

	/** The measurement range of a DMM or the output range of a power supply. */
	SR_CONF_RANGE,

	/** The number of digits (e.g. for a DMM). */
	SR_CONF_DIGITS,

	/** Phase of a source signal. */
	SR_CONF_PHASE,

	/** Duty cycle of a source signal. */
	SR_CONF_DUTY_CYCLE,

	/**
	 * Current power.
	 * @arg type: double
	 * @arg get: get measured power
	 */
	SR_CONF_POWER,

	/**
	 * Power target.
	 * @arg type: double
	 * @arg get: get power target
	 * @arg set: change power target
	 */
	SR_CONF_POWER_TARGET,

	/**
	 * Resistance target.
	 * @arg type: double
	 * @arg get: get resistance target
	 * @arg set: change resistance target
	 */
	SR_CONF_RESISTANCE_TARGET,

	/* Update sr_key_info_config[] (hwdriver.c) upon changes! */

	/*--- Special stuff -------------------------------------------------*/

	/** Session filename. */
	SR_CONF_SESSIONFILE = 40000,

	/** The device supports specifying a capturefile to inject. */
	SR_CONF_CAPTUREFILE,

	/** The device supports specifying the capturefile unit size. */
	SR_CONF_CAPTURE_UNITSIZE,

	/** Power off the device. */
	SR_CONF_POWER_OFF,

	/**
	 * Data source for acquisition. If not present, acquisition from
	 * the device is always "live", i.e. acquisition starts when the
	 * frontend asks and the results are sent out as soon as possible.
	 *
	 * If present, it indicates that either the device has no live
	 * acquisition capability (for example a pure data logger), or
	 * there is a choice. sr_config_list() returns those choices.
	 *
	 * In any case if a device has live acquisition capabilities, it
	 * is always the default.
	 */
	SR_CONF_DATA_SOURCE,

	/** The device supports setting a probe factor. */
	SR_CONF_PROBE_FACTOR,

	/** Number of powerline cycles for ADC integration time. */
	SR_CONF_ADC_POWERLINE_CYCLES,

	/* Update sr_key_info_config[] (hwdriver.c) upon changes! */

	/*--- Acquisition modes, sample limiting ----------------------------*/

	/**
	 * The device supports setting a sample time limit (how long
	 * the sample acquisition should run, in ms).
	 */
	SR_CONF_LIMIT_MSEC = 50000,

	/**
	 * The device supports setting a sample number limit (how many
	 * samples should be acquired).
	 */
	SR_CONF_LIMIT_SAMPLES,

	/**
	 * The device supports setting a frame limit (how many
	 * frames should be acquired).
	 */
	SR_CONF_LIMIT_FRAMES,

	/**
	 * The device supports continuous sampling. Neither a time limit
	 * nor a sample number limit has to be supplied, it will just acquire
	 * samples continuously, until explicitly stopped by a certain command.
	 */
	SR_CONF_CONTINUOUS,

	/** The device has internal storage, into which data is logged. This
	 * starts or stops the internal logging. */
	SR_CONF_DATALOG,

	/** Device mode for multi-function devices. */
	SR_CONF_DEVICE_MODE,

	/** Self test mode. */
	SR_CONF_TEST_MODE,

	/* Update sr_key_info_config[] (hwdriver.c) upon changes! */
};

/**
 * Opaque structure representing a libsigrok device instance.
 *
 * None of the fields of this structure are meant to be accessed directly.
 */
struct sr_dev_inst;

/** Types of device instance, struct sr_dev_inst.type */
enum sr_dev_inst_type {
	/** Device instance type for USB devices. */
	SR_INST_USB = 10000,
	/** Device instance type for serial port devices. */
	SR_INST_SERIAL,
	/** Device instance type for SCPI devices. */
	SR_INST_SCPI,
	/** Device-instance type for user-created "devices". */
	SR_INST_USER,
	/** Device instance type for Modbus devices. */
	SR_INST_MODBUS,
};

/** Device instance status, struct sr_dev_inst.status */
enum sr_dev_inst_status {
	/** The device instance was not found. */
	SR_ST_NOT_FOUND = 10000,
	/** The device instance was found, but is still booting. */
	SR_ST_INITIALIZING,
	/** The device instance is live, but not in use. */
	SR_ST_INACTIVE,
	/** The device instance is actively in use in a session. */
	SR_ST_ACTIVE,
	/** The device is winding down its session. */
	SR_ST_STOPPING,
};

/** Device driver data. See also http://sigrok.org/wiki/Hardware_driver_API . */
struct sr_dev_driver {
	/* Driver-specific */
	/** Driver name. Lowercase a-z, 0-9 and dashes (-) only. */
	const char *name;
	/** Long name. Verbose driver name shown to user. */
	const char *longname;
	/** API version (currently 1).	*/
	int api_version;
	/** Called when driver is loaded, e.g. program startup. */
	int (*init) (struct sr_dev_driver *driver, struct sr_context *sr_ctx);
	/** Called before driver is unloaded.
	 *  Driver must free all resources held by it. */
	int (*cleanup) (const struct sr_dev_driver *driver);
	/** Scan for devices. Driver should do all initialisation required.
	 *  Can be called several times, e.g. with different port options.
	 *  @retval NULL Error or no devices found.
	 *  @retval other GSList of a struct sr_dev_inst for each device.
	 *                Must be freed by caller!
	 */
	GSList *(*scan) (struct sr_dev_driver *driver, GSList *options);
	/** Get list of device instances the driver knows about.
	 *  @returns NULL or GSList of a struct sr_dev_inst for each device.
	 *           Must not be freed by caller!
	 */
	GSList *(*dev_list) (const struct sr_dev_driver *driver);
	/** Clear list of devices the driver knows about. */
	int (*dev_clear) (const struct sr_dev_driver *driver);
	/** Query value of a configuration key in driver or given device instance.
	 *  @see sr_config_get().
	 */
	int (*config_get) (uint32_t key, GVariant **data,
			const struct sr_dev_inst *sdi,
			const struct sr_channel_group *cg);
	/** Set value of a configuration key in driver or a given device instance.
	 *  @see sr_config_set(). */
	int (*config_set) (uint32_t key, GVariant *data,
			const struct sr_dev_inst *sdi,
			const struct sr_channel_group *cg);
	/** Channel status change.
	 *  @see sr_dev_channel_enable(). */
	int (*config_channel_set) (const struct sr_dev_inst *sdi,
			struct sr_channel *ch, unsigned int changes);
	/** Apply configuration settings to the device hardware.
	 *  @see sr_config_commit().*/
	int (*config_commit) (const struct sr_dev_inst *sdi);
	/** List all possible values for a configuration key in a device instance.
	 *  @see sr_config_list().
	 */
	int (*config_list) (uint32_t key, GVariant **data,
			const struct sr_dev_inst *sdi,
			const struct sr_channel_group *cg);

	/* Device-specific */
	/** Open device */
	int (*dev_open) (struct sr_dev_inst *sdi);
	/** Close device */
	int (*dev_close) (struct sr_dev_inst *sdi);
	/** Begin data acquisition on the specified device. */
	int (*dev_acquisition_start) (const struct sr_dev_inst *sdi);
	/** End data acquisition on the specified device. */
	int (*dev_acquisition_stop) (struct sr_dev_inst *sdi);

	/* Dynamic */
	/** Device driver context, considered private. Initialized by init(). */
	void *context;
};

/** Serial port descriptor. */
struct sr_serial_port {
	/** The OS dependent name of the serial port. */
	char *name;
	/** An end user friendly description for the serial port. */
	char *description;
};

#include <libsigrok/proto.h>
#include <libsigrok/version.h>

#ifdef __cplusplus
}
#endif

#endif
