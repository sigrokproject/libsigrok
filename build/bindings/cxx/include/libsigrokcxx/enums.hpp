/* Generated file - edit enums.py instead! */
namespace sigrok {

template<> const SR_API std::map<const enum sr_loglevel, const LogLevel * const> EnumValue<LogLevel, enum sr_loglevel>::_values;

/** Log verbosity level */
class SR_API LogLevel : public EnumValue<LogLevel, enum sr_loglevel>
{
public:

	/** Output no messages at all. */
	static const LogLevel * const NONE;
	/** Output error messages. */
	static const LogLevel * const ERR;
	/** Output warnings. */
	static const LogLevel * const WARN;
	/** Output informational messages. */
	static const LogLevel * const INFO;
	/** Output debug messages. */
	static const LogLevel * const DBG;
	/** Output very noisy debug messages. */
	static const LogLevel * const SPEW;

protected:
    LogLevel(enum sr_loglevel id, const char name[]) : EnumValue(id, name) {}

	static const LogLevel _NONE;
	static const LogLevel _ERR;
	static const LogLevel _WARN;
	static const LogLevel _INFO;
	static const LogLevel _DBG;
	static const LogLevel _SPEW;
};

template<> const SR_API std::map<const enum sr_datatype, const DataType * const> EnumValue<DataType, enum sr_datatype>::_values;

/** Configuration data type */
class SR_API DataType : public EnumValue<DataType, enum sr_datatype>
{
public:

	static const DataType * const UINT64;
	static const DataType * const STRING;
	static const DataType * const BOOL;
	static const DataType * const FLOAT;
	static const DataType * const RATIONAL_PERIOD;
	static const DataType * const RATIONAL_VOLT;
	static const DataType * const KEYVALUE;
	static const DataType * const UINT64_RANGE;
	static const DataType * const DOUBLE_RANGE;
	static const DataType * const INT32;
	static const DataType * const MQ;

protected:
    DataType(enum sr_datatype id, const char name[]) : EnumValue(id, name) {}

	static const DataType _UINT64;
	static const DataType _STRING;
	static const DataType _BOOL;
	static const DataType _FLOAT;
	static const DataType _RATIONAL_PERIOD;
	static const DataType _RATIONAL_VOLT;
	static const DataType _KEYVALUE;
	static const DataType _UINT64_RANGE;
	static const DataType _DOUBLE_RANGE;
	static const DataType _INT32;
	static const DataType _MQ;
};

template<> const SR_API std::map<const enum sr_packettype, const PacketType * const> EnumValue<PacketType, enum sr_packettype>::_values;

/** Type of datafeed packet */
class SR_API PacketType : public EnumValue<PacketType, enum sr_packettype>
{
public:

	/** Payload is */
	static const PacketType * const HEADER;
	/** End of stream (no further data). */
	static const PacketType * const END;
	/** Payload is struct */
	static const PacketType * const META;
	/** The trigger matched at this point in the data feed. */
	static const PacketType * const TRIGGER;
	/** Payload is struct */
	static const PacketType * const LOGIC;
	/** Beginning of frame. */
	static const PacketType * const FRAME_BEGIN;
	/** End of frame. */
	static const PacketType * const FRAME_END;
	/** Payload is struct */
	static const PacketType * const ANALOG;

protected:
    PacketType(enum sr_packettype id, const char name[]) : EnumValue(id, name) {}

	static const PacketType _HEADER;
	static const PacketType _END;
	static const PacketType _META;
	static const PacketType _TRIGGER;
	static const PacketType _LOGIC;
	static const PacketType _FRAME_BEGIN;
	static const PacketType _FRAME_END;
	static const PacketType _ANALOG;
};

template<> const SR_API std::map<const enum sr_mq, const Quantity * const> EnumValue<Quantity, enum sr_mq>::_values;

/** Measured quantity */
class SR_API Quantity : public EnumValue<Quantity, enum sr_mq>
{
public:

	static const Quantity * const VOLTAGE;
	static const Quantity * const CURRENT;
	static const Quantity * const RESISTANCE;
	static const Quantity * const CAPACITANCE;
	static const Quantity * const TEMPERATURE;
	static const Quantity * const FREQUENCY;
	/** Duty cycle, e.g. */
	static const Quantity * const DUTY_CYCLE;
	/** Continuity test. */
	static const Quantity * const CONTINUITY;
	static const Quantity * const PULSE_WIDTH;
	static const Quantity * const CONDUCTANCE;
	/** Electrical power, usually in W, or dBm. */
	static const Quantity * const POWER;
	/** Gain (a transistor's gain, or hFE, for example). */
	static const Quantity * const GAIN;
	/** Logarithmic representation of sound pressure relative to a reference value. */
	static const Quantity * const SOUND_PRESSURE_LEVEL;
	/** Carbon monoxide level. */
	static const Quantity * const CARBON_MONOXIDE;
	/** Humidity. */
	static const Quantity * const RELATIVE_HUMIDITY;
	/** Time. */
	static const Quantity * const TIME;
	/** Wind speed. */
	static const Quantity * const WIND_SPEED;
	/** Pressure. */
	static const Quantity * const PRESSURE;
	/** Parallel inductance (LCR meter model). */
	static const Quantity * const PARALLEL_INDUCTANCE;
	/** Parallel capacitance (LCR meter model). */
	static const Quantity * const PARALLEL_CAPACITANCE;
	/** Parallel resistance (LCR meter model). */
	static const Quantity * const PARALLEL_RESISTANCE;
	/** Series inductance (LCR meter model). */
	static const Quantity * const SERIES_INDUCTANCE;
	/** Series capacitance (LCR meter model). */
	static const Quantity * const SERIES_CAPACITANCE;
	/** Series resistance (LCR meter model). */
	static const Quantity * const SERIES_RESISTANCE;
	/** Dissipation factor. */
	static const Quantity * const DISSIPATION_FACTOR;
	/** Quality factor. */
	static const Quantity * const QUALITY_FACTOR;
	/** Phase angle. */
	static const Quantity * const PHASE_ANGLE;
	/** Difference from reference value. */
	static const Quantity * const DIFFERENCE;
	/** Count. */
	static const Quantity * const COUNT;
	/** Power factor. */
	static const Quantity * const POWER_FACTOR;
	/** Apparent power. */
	static const Quantity * const APPARENT_POWER;
	/** Mass. */
	static const Quantity * const MASS;
	/** Harmonic ratio. */
	static const Quantity * const HARMONIC_RATIO;
	/** Energy. */
	static const Quantity * const ENERGY;
	/** Electric charge. */
	static const Quantity * const ELECTRIC_CHARGE;

protected:
    Quantity(enum sr_mq id, const char name[]) : EnumValue(id, name) {}

	static const Quantity _VOLTAGE;
	static const Quantity _CURRENT;
	static const Quantity _RESISTANCE;
	static const Quantity _CAPACITANCE;
	static const Quantity _TEMPERATURE;
	static const Quantity _FREQUENCY;
	static const Quantity _DUTY_CYCLE;
	static const Quantity _CONTINUITY;
	static const Quantity _PULSE_WIDTH;
	static const Quantity _CONDUCTANCE;
	static const Quantity _POWER;
	static const Quantity _GAIN;
	static const Quantity _SOUND_PRESSURE_LEVEL;
	static const Quantity _CARBON_MONOXIDE;
	static const Quantity _RELATIVE_HUMIDITY;
	static const Quantity _TIME;
	static const Quantity _WIND_SPEED;
	static const Quantity _PRESSURE;
	static const Quantity _PARALLEL_INDUCTANCE;
	static const Quantity _PARALLEL_CAPACITANCE;
	static const Quantity _PARALLEL_RESISTANCE;
	static const Quantity _SERIES_INDUCTANCE;
	static const Quantity _SERIES_CAPACITANCE;
	static const Quantity _SERIES_RESISTANCE;
	static const Quantity _DISSIPATION_FACTOR;
	static const Quantity _QUALITY_FACTOR;
	static const Quantity _PHASE_ANGLE;
	static const Quantity _DIFFERENCE;
	static const Quantity _COUNT;
	static const Quantity _POWER_FACTOR;
	static const Quantity _APPARENT_POWER;
	static const Quantity _MASS;
	static const Quantity _HARMONIC_RATIO;
	static const Quantity _ENERGY;
	static const Quantity _ELECTRIC_CHARGE;
};

template<> const SR_API std::map<const enum sr_unit, const Unit * const> EnumValue<Unit, enum sr_unit>::_values;

/** Unit of measurement */
class SR_API Unit : public EnumValue<Unit, enum sr_unit>
{
public:

	/** Volt. */
	static const Unit * const VOLT;
	/** Ampere (current). */
	static const Unit * const AMPERE;
	/** Ohm (resistance). */
	static const Unit * const OHM;
	/** Farad (capacity). */
	static const Unit * const FARAD;
	/** Kelvin (temperature). */
	static const Unit * const KELVIN;
	/** Degrees Celsius (temperature). */
	static const Unit * const CELSIUS;
	/** Degrees Fahrenheit (temperature). */
	static const Unit * const FAHRENHEIT;
	/** Hertz (frequency, 1/s, [Hz]). */
	static const Unit * const HERTZ;
	/** Percent value. */
	static const Unit * const PERCENTAGE;
	/** Boolean value. */
	static const Unit * const BOOLEAN;
	/** Time in seconds. */
	static const Unit * const SECOND;
	/** Unit of conductance, the inverse of resistance. */
	static const Unit * const SIEMENS;
	/** An absolute measurement of power, in decibels, referenced to 1 milliwatt (dBm). */
	static const Unit * const DECIBEL_MW;
	/** Voltage in decibel, referenced to 1 volt (dBV). */
	static const Unit * const DECIBEL_VOLT;
	/** Measurements that intrinsically do not have units attached, such as ratios, gains, etc. */
	static const Unit * const UNITLESS;
	/** Sound pressure level, in decibels, relative to 20 micropascals. */
	static const Unit * const DECIBEL_SPL;
	/** Normalized (0 to 1) concentration of a substance or compound with 0 representing a concentration of 0%, and 1 being 100%. */
	static const Unit * const CONCENTRATION;
	/** Revolutions per minute. */
	static const Unit * const REVOLUTIONS_PER_MINUTE;
	/** Apparent power [VA]. */
	static const Unit * const VOLT_AMPERE;
	/** Real power [W]. */
	static const Unit * const WATT;
	/** Energy (consumption) in watt hour [Wh]. */
	static const Unit * const WATT_HOUR;
	/** Wind speed in meters per second. */
	static const Unit * const METER_SECOND;
	/** Pressure in hectopascal. */
	static const Unit * const HECTOPASCAL;
	/** Relative humidity assuming air temperature of 293 Kelvin (rF). */
	static const Unit * const HUMIDITY_293K;
	/** Plane angle in 1/360th of a full circle. */
	static const Unit * const DEGREE;
	/** Henry (inductance). */
	static const Unit * const HENRY;
	/** Mass in gram [g]. */
	static const Unit * const GRAM;
	/** Mass in carat [ct]. */
	static const Unit * const CARAT;
	/** Mass in ounce [oz]. */
	static const Unit * const OUNCE;
	/** Mass in troy ounce [oz t]. */
	static const Unit * const TROY_OUNCE;
	/** Mass in pound [lb]. */
	static const Unit * const POUND;
	/** Mass in pennyweight [dwt]. */
	static const Unit * const PENNYWEIGHT;
	/** Mass in grain [gr]. */
	static const Unit * const GRAIN;
	/** Mass in tael (variants: Hong Kong, Singapore/Malaysia, Taiwan) */
	static const Unit * const TAEL;
	/** Mass in momme. */
	static const Unit * const MOMME;
	/** Mass in tola. */
	static const Unit * const TOLA;
	/** Pieces (number of items). */
	static const Unit * const PIECE;
	/** Energy in joule. */
	static const Unit * const JOULE;
	/** Electric charge in coulomb. */
	static const Unit * const COULOMB;
	/** Electric charge in ampere hour [Ah]. */
	static const Unit * const AMPERE_HOUR;

protected:
    Unit(enum sr_unit id, const char name[]) : EnumValue(id, name) {}

	static const Unit _VOLT;
	static const Unit _AMPERE;
	static const Unit _OHM;
	static const Unit _FARAD;
	static const Unit _KELVIN;
	static const Unit _CELSIUS;
	static const Unit _FAHRENHEIT;
	static const Unit _HERTZ;
	static const Unit _PERCENTAGE;
	static const Unit _BOOLEAN;
	static const Unit _SECOND;
	static const Unit _SIEMENS;
	static const Unit _DECIBEL_MW;
	static const Unit _DECIBEL_VOLT;
	static const Unit _UNITLESS;
	static const Unit _DECIBEL_SPL;
	static const Unit _CONCENTRATION;
	static const Unit _REVOLUTIONS_PER_MINUTE;
	static const Unit _VOLT_AMPERE;
	static const Unit _WATT;
	static const Unit _WATT_HOUR;
	static const Unit _METER_SECOND;
	static const Unit _HECTOPASCAL;
	static const Unit _HUMIDITY_293K;
	static const Unit _DEGREE;
	static const Unit _HENRY;
	static const Unit _GRAM;
	static const Unit _CARAT;
	static const Unit _OUNCE;
	static const Unit _TROY_OUNCE;
	static const Unit _POUND;
	static const Unit _PENNYWEIGHT;
	static const Unit _GRAIN;
	static const Unit _TAEL;
	static const Unit _MOMME;
	static const Unit _TOLA;
	static const Unit _PIECE;
	static const Unit _JOULE;
	static const Unit _COULOMB;
	static const Unit _AMPERE_HOUR;
};

template<> const SR_API std::map<const enum sr_mqflag, const QuantityFlag * const> EnumValue<QuantityFlag, enum sr_mqflag>::_values;

/** Flag applied to measured quantity */
class SR_API QuantityFlag : public EnumValue<QuantityFlag, enum sr_mqflag>
{
public:

	/** Voltage measurement is alternating current (AC). */
	static const QuantityFlag * const AC;
	/** Voltage measurement is direct current (DC). */
	static const QuantityFlag * const DC;
	/** This is a true RMS measurement. */
	static const QuantityFlag * const RMS;
	/** Value is voltage drop across a diode, or NAN. */
	static const QuantityFlag * const DIODE;
	/** Device is in "hold" mode (repeating the last measurement). */
	static const QuantityFlag * const HOLD;
	/** Device is in "max" mode, only updating upon a new max value. */
	static const QuantityFlag * const MAX;
	/** Device is in "min" mode, only updating upon a new min value. */
	static const QuantityFlag * const MIN;
	/** Device is in autoranging mode. */
	static const QuantityFlag * const AUTORANGE;
	/** Device is in relative mode. */
	static const QuantityFlag * const RELATIVE;
	/** Sound pressure level is A-weighted in the frequency domain, according to IEC 61672:2003. */
	static const QuantityFlag * const SPL_FREQ_WEIGHT_A;
	/** Sound pressure level is C-weighted in the frequency domain, according to IEC 61672:2003. */
	static const QuantityFlag * const SPL_FREQ_WEIGHT_C;
	/** Sound pressure level is Z-weighted (i.e. */
	static const QuantityFlag * const SPL_FREQ_WEIGHT_Z;
	/** Sound pressure level is not weighted in the frequency domain, albeit without standards-defined low and high frequency limits. */
	static const QuantityFlag * const SPL_FREQ_WEIGHT_FLAT;
	/** Sound pressure level measurement is S-weighted (1s) in the time domain. */
	static const QuantityFlag * const SPL_TIME_WEIGHT_S;
	/** Sound pressure level measurement is F-weighted (125ms) in the time domain. */
	static const QuantityFlag * const SPL_TIME_WEIGHT_F;
	/** Sound pressure level is time-averaged (LAT), also known as Equivalent Continuous A-weighted Sound Level (LEQ). */
	static const QuantityFlag * const SPL_LAT;
	/** Sound pressure level represented as a percentage of measurements that were over a preset alarm level. */
	static const QuantityFlag * const SPL_PCT_OVER_ALARM;
	/** Time is duration (as opposed to epoch, ...). */
	static const QuantityFlag * const DURATION;
	/** Device is in "avg" mode, averaging upon each new value. */
	static const QuantityFlag * const AVG;
	/** Reference value shown. */
	static const QuantityFlag * const REFERENCE;
	/** Unstable value (hasn't settled yet). */
	static const QuantityFlag * const UNSTABLE;
	/** Measurement is four wire (e.g. */
	static const QuantityFlag * const FOUR_WIRE;
	/** Get flags corresponding to a bitmask. */
	static std::vector<const QuantityFlag *>
		flags_from_mask(unsigned int mask);

	/** Get bitmask corresponding to a set of flags. */
	static unsigned int mask_from_flags(
		std::vector<const QuantityFlag *> flags);


protected:
    QuantityFlag(enum sr_mqflag id, const char name[]) : EnumValue(id, name) {}

	static const QuantityFlag _AC;
	static const QuantityFlag _DC;
	static const QuantityFlag _RMS;
	static const QuantityFlag _DIODE;
	static const QuantityFlag _HOLD;
	static const QuantityFlag _MAX;
	static const QuantityFlag _MIN;
	static const QuantityFlag _AUTORANGE;
	static const QuantityFlag _RELATIVE;
	static const QuantityFlag _SPL_FREQ_WEIGHT_A;
	static const QuantityFlag _SPL_FREQ_WEIGHT_C;
	static const QuantityFlag _SPL_FREQ_WEIGHT_Z;
	static const QuantityFlag _SPL_FREQ_WEIGHT_FLAT;
	static const QuantityFlag _SPL_TIME_WEIGHT_S;
	static const QuantityFlag _SPL_TIME_WEIGHT_F;
	static const QuantityFlag _SPL_LAT;
	static const QuantityFlag _SPL_PCT_OVER_ALARM;
	static const QuantityFlag _DURATION;
	static const QuantityFlag _AVG;
	static const QuantityFlag _REFERENCE;
	static const QuantityFlag _UNSTABLE;
	static const QuantityFlag _FOUR_WIRE;
};

template<> const SR_API std::map<const enum sr_trigger_matches, const TriggerMatchType * const> EnumValue<TriggerMatchType, enum sr_trigger_matches>::_values;

/** Trigger match type */
class SR_API TriggerMatchType : public EnumValue<TriggerMatchType, enum sr_trigger_matches>
{
public:

	static const TriggerMatchType * const ZERO;
	static const TriggerMatchType * const ONE;
	static const TriggerMatchType * const RISING;
	static const TriggerMatchType * const FALLING;
	static const TriggerMatchType * const EDGE;
	static const TriggerMatchType * const OVER;
	static const TriggerMatchType * const UNDER;

protected:
    TriggerMatchType(enum sr_trigger_matches id, const char name[]) : EnumValue(id, name) {}

	static const TriggerMatchType _ZERO;
	static const TriggerMatchType _ONE;
	static const TriggerMatchType _RISING;
	static const TriggerMatchType _FALLING;
	static const TriggerMatchType _EDGE;
	static const TriggerMatchType _OVER;
	static const TriggerMatchType _UNDER;
};

template<> const SR_API std::map<const enum sr_output_flag, const OutputFlag * const> EnumValue<OutputFlag, enum sr_output_flag>::_values;

/** Flag applied to output modules */
class SR_API OutputFlag : public EnumValue<OutputFlag, enum sr_output_flag>
{
public:

	/** If set, this output module writes the output itself. */
	static const OutputFlag * const INTERNAL_IO_HANDLING;

protected:
    OutputFlag(enum sr_output_flag id, const char name[]) : EnumValue(id, name) {}

	static const OutputFlag _INTERNAL_IO_HANDLING;
};

template<> const SR_API std::map<const enum sr_channeltype, const ChannelType * const> EnumValue<ChannelType, enum sr_channeltype>::_values;

/** Channel type */
class SR_API ChannelType : public EnumValue<ChannelType, enum sr_channeltype>
{
public:

	/** Channel type is logic channel. */
	static const ChannelType * const LOGIC;
	/** Channel type is analog channel. */
	static const ChannelType * const ANALOG;

protected:
    ChannelType(enum sr_channeltype id, const char name[]) : EnumValue(id, name) {}

	static const ChannelType _LOGIC;
	static const ChannelType _ANALOG;
};

template<> const SR_API std::map<const enum sr_configcap, const Capability * const> EnumValue<Capability, enum sr_configcap>::_values;

/** Configuration capability */
class SR_API Capability : public EnumValue<Capability, enum sr_configcap>
{
public:

	/** Value can be read. */
	static const Capability * const GET;
	/** Value can be written. */
	static const Capability * const SET;
	/** Possible values can be enumerated. */
	static const Capability * const LIST;

protected:
    Capability(enum sr_configcap id, const char name[]) : EnumValue(id, name) {}

	static const Capability _GET;
	static const Capability _SET;
	static const Capability _LIST;
};

template<> const SR_API std::map<const enum sr_configkey, const ConfigKey * const> EnumValue<ConfigKey, enum sr_configkey>::_values;

/** Configuration key */
class SR_API ConfigKey : public EnumValue<ConfigKey, enum sr_configkey>
{
public:

	/** The device can act as logic analyzer. */
	static const ConfigKey * const LOGIC_ANALYZER;
	/** The device can act as an oscilloscope. */
	static const ConfigKey * const OSCILLOSCOPE;
	/** The device can act as a multimeter. */
	static const ConfigKey * const MULTIMETER;
	/** The device is a demo device. */
	static const ConfigKey * const DEMO_DEV;
	/** The device can act as a sound level meter. */
	static const ConfigKey * const SOUNDLEVELMETER;
	/** The device can measure temperature. */
	static const ConfigKey * const THERMOMETER;
	/** The device can measure humidity. */
	static const ConfigKey * const HYGROMETER;
	/** The device can measure energy consumption. */
	static const ConfigKey * const ENERGYMETER;
	/** The device can act as a signal demodulator. */
	static const ConfigKey * const DEMODULATOR;
	/** The device can act as a programmable power supply. */
	static const ConfigKey * const POWER_SUPPLY;
	/** The device can act as an LCR meter. */
	static const ConfigKey * const LCRMETER;
	/** The device can act as an electronic load. */
	static const ConfigKey * const ELECTRONIC_LOAD;
	/** The device can act as a scale. */
	static const ConfigKey * const SCALE;
	/** The device can act as a function generator. */
	static const ConfigKey * const SIGNAL_GENERATOR;
	/** The device can measure power. */
	static const ConfigKey * const POWERMETER;
	/** Specification on how to connect to a device. */
	static const ConfigKey * const CONN;
	/** Serial communication specification, in the form: */
	static const ConfigKey * const SERIALCOMM;
	/** Modbus slave address specification. */
	static const ConfigKey * const MODBUSADDR;
	/** User specified forced driver attachment to unknown devices. */
	static const ConfigKey * const FORCE_DETECT;
	/** The device supports setting its samplerate, in Hz. */
	static const ConfigKey * const SAMPLERATE;
	/** The device supports setting a pre/post-trigger capture ratio. */
	static const ConfigKey * const CAPTURE_RATIO;
	/** The device supports setting a pattern (pattern generator mode). */
	static const ConfigKey * const PATTERN_MODE;
	/** The device supports run-length encoding (RLE). */
	static const ConfigKey * const RLE;
	/** The device supports setting trigger slope. */
	static const ConfigKey * const TRIGGER_SLOPE;
	/** The device supports averaging. */
	static const ConfigKey * const AVERAGING;
	/** The device supports setting number of samples to be averaged over. */
	static const ConfigKey * const AVG_SAMPLES;
	/** Trigger source. */
	static const ConfigKey * const TRIGGER_SOURCE;
	/** Horizontal trigger position. */
	static const ConfigKey * const HORIZ_TRIGGERPOS;
	/** Buffer size. */
	static const ConfigKey * const BUFFERSIZE;
	/** Time base. */
	static const ConfigKey * const TIMEBASE;
	/** Filter. */
	static const ConfigKey * const FILTER;
	/** Volts/div. */
	static const ConfigKey * const VDIV;
	/** Coupling. */
	static const ConfigKey * const COUPLING;
	/** Trigger matches. */
	static const ConfigKey * const TRIGGER_MATCH;
	/** The device supports setting its sample interval, in ms. */
	static const ConfigKey * const SAMPLE_INTERVAL;
	/** Number of horizontal divisions, as related to SR_CONF_TIMEBASE. */
	static const ConfigKey * const NUM_HDIV;
	/** Number of vertical divisions, as related to SR_CONF_VDIV. */
	static const ConfigKey * const NUM_VDIV;
	/** Sound pressure level frequency weighting. */
	static const ConfigKey * const SPL_WEIGHT_FREQ;
	/** Sound pressure level time weighting. */
	static const ConfigKey * const SPL_WEIGHT_TIME;
	/** Sound pressure level measurement range. */
	static const ConfigKey * const SPL_MEASUREMENT_RANGE;
	/** Max hold mode. */
	static const ConfigKey * const HOLD_MAX;
	/** Min hold mode. */
	static const ConfigKey * const HOLD_MIN;
	/** Logic low-high threshold range. */
	static const ConfigKey * const VOLTAGE_THRESHOLD;
	/** The device supports using an external clock. */
	static const ConfigKey * const EXTERNAL_CLOCK;
	/** The device supports swapping channels. */
	static const ConfigKey * const SWAP;
	/** Center frequency. */
	static const ConfigKey * const CENTER_FREQUENCY;
	/** The device supports setting the number of logic channels. */
	static const ConfigKey * const NUM_LOGIC_CHANNELS;
	/** The device supports setting the number of analog channels. */
	static const ConfigKey * const NUM_ANALOG_CHANNELS;
	/** Current voltage. */
	static const ConfigKey * const VOLTAGE;
	/** Maximum target voltage. */
	static const ConfigKey * const VOLTAGE_TARGET;
	/** Current current. */
	static const ConfigKey * const CURRENT;
	/** Current limit. */
	static const ConfigKey * const CURRENT_LIMIT;
	/** Enabling/disabling channel. */
	static const ConfigKey * const ENABLED;
	/** Channel configuration. */
	static const ConfigKey * const CHANNEL_CONFIG;
	/** Over-voltage protection (OVP) feature. */
	static const ConfigKey * const OVER_VOLTAGE_PROTECTION_ENABLED;
	/** Over-voltage protection (OVP) active. */
	static const ConfigKey * const OVER_VOLTAGE_PROTECTION_ACTIVE;
	/** Over-voltage protection (OVP) threshold. */
	static const ConfigKey * const OVER_VOLTAGE_PROTECTION_THRESHOLD;
	/** Over-current protection (OCP) feature. */
	static const ConfigKey * const OVER_CURRENT_PROTECTION_ENABLED;
	/** Over-current protection (OCP) active. */
	static const ConfigKey * const OVER_CURRENT_PROTECTION_ACTIVE;
	/** Over-current protection (OCP) threshold. */
	static const ConfigKey * const OVER_CURRENT_PROTECTION_THRESHOLD;
	/** Choice of clock edge for external clock ("r" or "f"). */
	static const ConfigKey * const CLOCK_EDGE;
	/** Amplitude of a source without strictly-defined MQ. */
	static const ConfigKey * const AMPLITUDE;
	/** Channel regulation get: "CV", "CC" or "UR", denoting constant voltage, constant current or unregulated. */
	static const ConfigKey * const REGULATION;
	/** Over-temperature protection (OTP) */
	static const ConfigKey * const OVER_TEMPERATURE_PROTECTION;
	/** Output frequency in Hz. */
	static const ConfigKey * const OUTPUT_FREQUENCY;
	/** Output frequency target in Hz. */
	static const ConfigKey * const OUTPUT_FREQUENCY_TARGET;
	/** Measured quantity. */
	static const ConfigKey * const MEASURED_QUANTITY;
	/** Equivalent circuit model. */
	static const ConfigKey * const EQUIV_CIRCUIT_MODEL;
	/** Over-temperature protection (OTP) active. */
	static const ConfigKey * const OVER_TEMPERATURE_PROTECTION_ACTIVE;
	/** Under-voltage condition. */
	static const ConfigKey * const UNDER_VOLTAGE_CONDITION;
	/** Under-voltage condition active. */
	static const ConfigKey * const UNDER_VOLTAGE_CONDITION_ACTIVE;
	/** Trigger level. */
	static const ConfigKey * const TRIGGER_LEVEL;
	/** Under-voltage condition threshold. */
	static const ConfigKey * const UNDER_VOLTAGE_CONDITION_THRESHOLD;
	/** Which external clock source to use if the device supports multiple external clock channels. */
	static const ConfigKey * const EXTERNAL_CLOCK_SOURCE;
	/** Offset of a source without strictly-defined MQ. */
	static const ConfigKey * const OFFSET;
	/** The device supports setting a pattern for the logic trigger. */
	static const ConfigKey * const TRIGGER_PATTERN;
	/** High resolution mode. */
	static const ConfigKey * const HIGH_RESOLUTION;
	/** Peak detection. */
	static const ConfigKey * const PEAK_DETECTION;
	/** Logic threshold: predefined levels (TTL, ECL, CMOS, etc). */
	static const ConfigKey * const LOGIC_THRESHOLD;
	/** Logic threshold: custom numerical value. */
	static const ConfigKey * const LOGIC_THRESHOLD_CUSTOM;
	/** The measurement range of a DMM or the output range of a power supply. */
	static const ConfigKey * const RANGE;
	/** The number of digits (e.g. */
	static const ConfigKey * const DIGITS;
	/** Phase of a source signal. */
	static const ConfigKey * const PHASE;
	/** Duty cycle of a source signal. */
	static const ConfigKey * const DUTY_CYCLE;
	/** Current power. */
	static const ConfigKey * const POWER;
	/** Power target. */
	static const ConfigKey * const POWER_TARGET;
	/** Resistance target. */
	static const ConfigKey * const RESISTANCE_TARGET;
	/** Session filename. */
	static const ConfigKey * const SESSIONFILE;
	/** The device supports specifying a capturefile to inject. */
	static const ConfigKey * const CAPTUREFILE;
	/** The device supports specifying the capturefile unit size. */
	static const ConfigKey * const CAPTURE_UNITSIZE;
	/** Power off the device. */
	static const ConfigKey * const POWER_OFF;
	/** Data source for acquisition. */
	static const ConfigKey * const DATA_SOURCE;
	/** The device supports setting a probe factor. */
	static const ConfigKey * const PROBE_FACTOR;
	/** Number of powerline cycles for ADC integration time. */
	static const ConfigKey * const ADC_POWERLINE_CYCLES;
	/** The device supports setting a sample time limit (how long the sample acquisition should run, in ms). */
	static const ConfigKey * const LIMIT_MSEC;
	/** The device supports setting a sample number limit (how many samples should be acquired). */
	static const ConfigKey * const LIMIT_SAMPLES;
	/** The device supports setting a frame limit (how many frames should be acquired). */
	static const ConfigKey * const LIMIT_FRAMES;
	/** The device supports continuous sampling. */
	static const ConfigKey * const CONTINUOUS;
	/** The device has internal storage, into which data is logged. */
	static const ConfigKey * const DATALOG;
	/** Device mode for multi-function devices. */
	static const ConfigKey * const DEVICE_MODE;
	/** Self test mode. */
	static const ConfigKey * const TEST_MODE;
    /** Data type used for this configuration key. */
    const DataType *data_type() const;
    /** String identifier for this configuration key, suitable for CLI use. */
    std::string identifier() const;
    /** Description of this configuration key. */
    std::string description() const;
    /** Get configuration key by string identifier. */
    static const ConfigKey *get_by_identifier(std::string identifier);
    /** Parse a string argument into the appropriate type for this key. */
    static Glib::VariantBase parse_string(std::string value, enum sr_datatype dt);
    Glib::VariantBase parse_string(std::string value) const;


protected:
    ConfigKey(enum sr_configkey id, const char name[]) : EnumValue(id, name) {}

	static const ConfigKey _LOGIC_ANALYZER;
	static const ConfigKey _OSCILLOSCOPE;
	static const ConfigKey _MULTIMETER;
	static const ConfigKey _DEMO_DEV;
	static const ConfigKey _SOUNDLEVELMETER;
	static const ConfigKey _THERMOMETER;
	static const ConfigKey _HYGROMETER;
	static const ConfigKey _ENERGYMETER;
	static const ConfigKey _DEMODULATOR;
	static const ConfigKey _POWER_SUPPLY;
	static const ConfigKey _LCRMETER;
	static const ConfigKey _ELECTRONIC_LOAD;
	static const ConfigKey _SCALE;
	static const ConfigKey _SIGNAL_GENERATOR;
	static const ConfigKey _POWERMETER;
	static const ConfigKey _CONN;
	static const ConfigKey _SERIALCOMM;
	static const ConfigKey _MODBUSADDR;
	static const ConfigKey _FORCE_DETECT;
	static const ConfigKey _SAMPLERATE;
	static const ConfigKey _CAPTURE_RATIO;
	static const ConfigKey _PATTERN_MODE;
	static const ConfigKey _RLE;
	static const ConfigKey _TRIGGER_SLOPE;
	static const ConfigKey _AVERAGING;
	static const ConfigKey _AVG_SAMPLES;
	static const ConfigKey _TRIGGER_SOURCE;
	static const ConfigKey _HORIZ_TRIGGERPOS;
	static const ConfigKey _BUFFERSIZE;
	static const ConfigKey _TIMEBASE;
	static const ConfigKey _FILTER;
	static const ConfigKey _VDIV;
	static const ConfigKey _COUPLING;
	static const ConfigKey _TRIGGER_MATCH;
	static const ConfigKey _SAMPLE_INTERVAL;
	static const ConfigKey _NUM_HDIV;
	static const ConfigKey _NUM_VDIV;
	static const ConfigKey _SPL_WEIGHT_FREQ;
	static const ConfigKey _SPL_WEIGHT_TIME;
	static const ConfigKey _SPL_MEASUREMENT_RANGE;
	static const ConfigKey _HOLD_MAX;
	static const ConfigKey _HOLD_MIN;
	static const ConfigKey _VOLTAGE_THRESHOLD;
	static const ConfigKey _EXTERNAL_CLOCK;
	static const ConfigKey _SWAP;
	static const ConfigKey _CENTER_FREQUENCY;
	static const ConfigKey _NUM_LOGIC_CHANNELS;
	static const ConfigKey _NUM_ANALOG_CHANNELS;
	static const ConfigKey _VOLTAGE;
	static const ConfigKey _VOLTAGE_TARGET;
	static const ConfigKey _CURRENT;
	static const ConfigKey _CURRENT_LIMIT;
	static const ConfigKey _ENABLED;
	static const ConfigKey _CHANNEL_CONFIG;
	static const ConfigKey _OVER_VOLTAGE_PROTECTION_ENABLED;
	static const ConfigKey _OVER_VOLTAGE_PROTECTION_ACTIVE;
	static const ConfigKey _OVER_VOLTAGE_PROTECTION_THRESHOLD;
	static const ConfigKey _OVER_CURRENT_PROTECTION_ENABLED;
	static const ConfigKey _OVER_CURRENT_PROTECTION_ACTIVE;
	static const ConfigKey _OVER_CURRENT_PROTECTION_THRESHOLD;
	static const ConfigKey _CLOCK_EDGE;
	static const ConfigKey _AMPLITUDE;
	static const ConfigKey _REGULATION;
	static const ConfigKey _OVER_TEMPERATURE_PROTECTION;
	static const ConfigKey _OUTPUT_FREQUENCY;
	static const ConfigKey _OUTPUT_FREQUENCY_TARGET;
	static const ConfigKey _MEASURED_QUANTITY;
	static const ConfigKey _EQUIV_CIRCUIT_MODEL;
	static const ConfigKey _OVER_TEMPERATURE_PROTECTION_ACTIVE;
	static const ConfigKey _UNDER_VOLTAGE_CONDITION;
	static const ConfigKey _UNDER_VOLTAGE_CONDITION_ACTIVE;
	static const ConfigKey _TRIGGER_LEVEL;
	static const ConfigKey _UNDER_VOLTAGE_CONDITION_THRESHOLD;
	static const ConfigKey _EXTERNAL_CLOCK_SOURCE;
	static const ConfigKey _OFFSET;
	static const ConfigKey _TRIGGER_PATTERN;
	static const ConfigKey _HIGH_RESOLUTION;
	static const ConfigKey _PEAK_DETECTION;
	static const ConfigKey _LOGIC_THRESHOLD;
	static const ConfigKey _LOGIC_THRESHOLD_CUSTOM;
	static const ConfigKey _RANGE;
	static const ConfigKey _DIGITS;
	static const ConfigKey _PHASE;
	static const ConfigKey _DUTY_CYCLE;
	static const ConfigKey _POWER;
	static const ConfigKey _POWER_TARGET;
	static const ConfigKey _RESISTANCE_TARGET;
	static const ConfigKey _SESSIONFILE;
	static const ConfigKey _CAPTUREFILE;
	static const ConfigKey _CAPTURE_UNITSIZE;
	static const ConfigKey _POWER_OFF;
	static const ConfigKey _DATA_SOURCE;
	static const ConfigKey _PROBE_FACTOR;
	static const ConfigKey _ADC_POWERLINE_CYCLES;
	static const ConfigKey _LIMIT_MSEC;
	static const ConfigKey _LIMIT_SAMPLES;
	static const ConfigKey _LIMIT_FRAMES;
	static const ConfigKey _CONTINUOUS;
	static const ConfigKey _DATALOG;
	static const ConfigKey _DEVICE_MODE;
	static const ConfigKey _TEST_MODE;
};
}
