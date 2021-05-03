/* Generated file - edit enums.py instead! */
const LogLevel LogLevel::_NONE = LogLevel(SR_LOG_NONE, "NONE");
const LogLevel LogLevel::_ERR = LogLevel(SR_LOG_ERR, "ERR");
const LogLevel LogLevel::_WARN = LogLevel(SR_LOG_WARN, "WARN");
const LogLevel LogLevel::_INFO = LogLevel(SR_LOG_INFO, "INFO");
const LogLevel LogLevel::_DBG = LogLevel(SR_LOG_DBG, "DBG");
const LogLevel LogLevel::_SPEW = LogLevel(SR_LOG_SPEW, "SPEW");
const LogLevel * const LogLevel::NONE = &LogLevel::_NONE;
const LogLevel * const LogLevel::ERR = &LogLevel::_ERR;
const LogLevel * const LogLevel::WARN = &LogLevel::_WARN;
const LogLevel * const LogLevel::INFO = &LogLevel::_INFO;
const LogLevel * const LogLevel::DBG = &LogLevel::_DBG;
const LogLevel * const LogLevel::SPEW = &LogLevel::_SPEW;
template<> const SR_API std::map<const enum sr_loglevel, const LogLevel * const> EnumValue<LogLevel, enum sr_loglevel>::_values = {
	{SR_LOG_NONE, LogLevel::NONE},
	{SR_LOG_ERR, LogLevel::ERR},
	{SR_LOG_WARN, LogLevel::WARN},
	{SR_LOG_INFO, LogLevel::INFO},
	{SR_LOG_DBG, LogLevel::DBG},
	{SR_LOG_SPEW, LogLevel::SPEW},
};
const DataType DataType::_UINT64 = DataType(SR_T_UINT64, "UINT64");
const DataType DataType::_STRING = DataType(SR_T_STRING, "STRING");
const DataType DataType::_BOOL = DataType(SR_T_BOOL, "BOOL");
const DataType DataType::_FLOAT = DataType(SR_T_FLOAT, "FLOAT");
const DataType DataType::_RATIONAL_PERIOD = DataType(SR_T_RATIONAL_PERIOD, "RATIONAL_PERIOD");
const DataType DataType::_RATIONAL_VOLT = DataType(SR_T_RATIONAL_VOLT, "RATIONAL_VOLT");
const DataType DataType::_KEYVALUE = DataType(SR_T_KEYVALUE, "KEYVALUE");
const DataType DataType::_UINT64_RANGE = DataType(SR_T_UINT64_RANGE, "UINT64_RANGE");
const DataType DataType::_DOUBLE_RANGE = DataType(SR_T_DOUBLE_RANGE, "DOUBLE_RANGE");
const DataType DataType::_INT32 = DataType(SR_T_INT32, "INT32");
const DataType DataType::_MQ = DataType(SR_T_MQ, "MQ");
const DataType * const DataType::UINT64 = &DataType::_UINT64;
const DataType * const DataType::STRING = &DataType::_STRING;
const DataType * const DataType::BOOL = &DataType::_BOOL;
const DataType * const DataType::FLOAT = &DataType::_FLOAT;
const DataType * const DataType::RATIONAL_PERIOD = &DataType::_RATIONAL_PERIOD;
const DataType * const DataType::RATIONAL_VOLT = &DataType::_RATIONAL_VOLT;
const DataType * const DataType::KEYVALUE = &DataType::_KEYVALUE;
const DataType * const DataType::UINT64_RANGE = &DataType::_UINT64_RANGE;
const DataType * const DataType::DOUBLE_RANGE = &DataType::_DOUBLE_RANGE;
const DataType * const DataType::INT32 = &DataType::_INT32;
const DataType * const DataType::MQ = &DataType::_MQ;
template<> const SR_API std::map<const enum sr_datatype, const DataType * const> EnumValue<DataType, enum sr_datatype>::_values = {
	{SR_T_UINT64, DataType::UINT64},
	{SR_T_STRING, DataType::STRING},
	{SR_T_BOOL, DataType::BOOL},
	{SR_T_FLOAT, DataType::FLOAT},
	{SR_T_RATIONAL_PERIOD, DataType::RATIONAL_PERIOD},
	{SR_T_RATIONAL_VOLT, DataType::RATIONAL_VOLT},
	{SR_T_KEYVALUE, DataType::KEYVALUE},
	{SR_T_UINT64_RANGE, DataType::UINT64_RANGE},
	{SR_T_DOUBLE_RANGE, DataType::DOUBLE_RANGE},
	{SR_T_INT32, DataType::INT32},
	{SR_T_MQ, DataType::MQ},
};
const PacketType PacketType::_HEADER = PacketType(SR_DF_HEADER, "HEADER");
const PacketType PacketType::_END = PacketType(SR_DF_END, "END");
const PacketType PacketType::_META = PacketType(SR_DF_META, "META");
const PacketType PacketType::_TRIGGER = PacketType(SR_DF_TRIGGER, "TRIGGER");
const PacketType PacketType::_LOGIC = PacketType(SR_DF_LOGIC, "LOGIC");
const PacketType PacketType::_FRAME_BEGIN = PacketType(SR_DF_FRAME_BEGIN, "FRAME_BEGIN");
const PacketType PacketType::_FRAME_END = PacketType(SR_DF_FRAME_END, "FRAME_END");
const PacketType PacketType::_ANALOG = PacketType(SR_DF_ANALOG, "ANALOG");
const PacketType * const PacketType::HEADER = &PacketType::_HEADER;
const PacketType * const PacketType::END = &PacketType::_END;
const PacketType * const PacketType::META = &PacketType::_META;
const PacketType * const PacketType::TRIGGER = &PacketType::_TRIGGER;
const PacketType * const PacketType::LOGIC = &PacketType::_LOGIC;
const PacketType * const PacketType::FRAME_BEGIN = &PacketType::_FRAME_BEGIN;
const PacketType * const PacketType::FRAME_END = &PacketType::_FRAME_END;
const PacketType * const PacketType::ANALOG = &PacketType::_ANALOG;
template<> const SR_API std::map<const enum sr_packettype, const PacketType * const> EnumValue<PacketType, enum sr_packettype>::_values = {
	{SR_DF_HEADER, PacketType::HEADER},
	{SR_DF_END, PacketType::END},
	{SR_DF_META, PacketType::META},
	{SR_DF_TRIGGER, PacketType::TRIGGER},
	{SR_DF_LOGIC, PacketType::LOGIC},
	{SR_DF_FRAME_BEGIN, PacketType::FRAME_BEGIN},
	{SR_DF_FRAME_END, PacketType::FRAME_END},
	{SR_DF_ANALOG, PacketType::ANALOG},
};
const Quantity Quantity::_VOLTAGE = Quantity(SR_MQ_VOLTAGE, "VOLTAGE");
const Quantity Quantity::_CURRENT = Quantity(SR_MQ_CURRENT, "CURRENT");
const Quantity Quantity::_RESISTANCE = Quantity(SR_MQ_RESISTANCE, "RESISTANCE");
const Quantity Quantity::_CAPACITANCE = Quantity(SR_MQ_CAPACITANCE, "CAPACITANCE");
const Quantity Quantity::_TEMPERATURE = Quantity(SR_MQ_TEMPERATURE, "TEMPERATURE");
const Quantity Quantity::_FREQUENCY = Quantity(SR_MQ_FREQUENCY, "FREQUENCY");
const Quantity Quantity::_DUTY_CYCLE = Quantity(SR_MQ_DUTY_CYCLE, "DUTY_CYCLE");
const Quantity Quantity::_CONTINUITY = Quantity(SR_MQ_CONTINUITY, "CONTINUITY");
const Quantity Quantity::_PULSE_WIDTH = Quantity(SR_MQ_PULSE_WIDTH, "PULSE_WIDTH");
const Quantity Quantity::_CONDUCTANCE = Quantity(SR_MQ_CONDUCTANCE, "CONDUCTANCE");
const Quantity Quantity::_POWER = Quantity(SR_MQ_POWER, "POWER");
const Quantity Quantity::_GAIN = Quantity(SR_MQ_GAIN, "GAIN");
const Quantity Quantity::_SOUND_PRESSURE_LEVEL = Quantity(SR_MQ_SOUND_PRESSURE_LEVEL, "SOUND_PRESSURE_LEVEL");
const Quantity Quantity::_CARBON_MONOXIDE = Quantity(SR_MQ_CARBON_MONOXIDE, "CARBON_MONOXIDE");
const Quantity Quantity::_RELATIVE_HUMIDITY = Quantity(SR_MQ_RELATIVE_HUMIDITY, "RELATIVE_HUMIDITY");
const Quantity Quantity::_TIME = Quantity(SR_MQ_TIME, "TIME");
const Quantity Quantity::_WIND_SPEED = Quantity(SR_MQ_WIND_SPEED, "WIND_SPEED");
const Quantity Quantity::_PRESSURE = Quantity(SR_MQ_PRESSURE, "PRESSURE");
const Quantity Quantity::_PARALLEL_INDUCTANCE = Quantity(SR_MQ_PARALLEL_INDUCTANCE, "PARALLEL_INDUCTANCE");
const Quantity Quantity::_PARALLEL_CAPACITANCE = Quantity(SR_MQ_PARALLEL_CAPACITANCE, "PARALLEL_CAPACITANCE");
const Quantity Quantity::_PARALLEL_RESISTANCE = Quantity(SR_MQ_PARALLEL_RESISTANCE, "PARALLEL_RESISTANCE");
const Quantity Quantity::_SERIES_INDUCTANCE = Quantity(SR_MQ_SERIES_INDUCTANCE, "SERIES_INDUCTANCE");
const Quantity Quantity::_SERIES_CAPACITANCE = Quantity(SR_MQ_SERIES_CAPACITANCE, "SERIES_CAPACITANCE");
const Quantity Quantity::_SERIES_RESISTANCE = Quantity(SR_MQ_SERIES_RESISTANCE, "SERIES_RESISTANCE");
const Quantity Quantity::_DISSIPATION_FACTOR = Quantity(SR_MQ_DISSIPATION_FACTOR, "DISSIPATION_FACTOR");
const Quantity Quantity::_QUALITY_FACTOR = Quantity(SR_MQ_QUALITY_FACTOR, "QUALITY_FACTOR");
const Quantity Quantity::_PHASE_ANGLE = Quantity(SR_MQ_PHASE_ANGLE, "PHASE_ANGLE");
const Quantity Quantity::_DIFFERENCE = Quantity(SR_MQ_DIFFERENCE, "DIFFERENCE");
const Quantity Quantity::_COUNT = Quantity(SR_MQ_COUNT, "COUNT");
const Quantity Quantity::_POWER_FACTOR = Quantity(SR_MQ_POWER_FACTOR, "POWER_FACTOR");
const Quantity Quantity::_APPARENT_POWER = Quantity(SR_MQ_APPARENT_POWER, "APPARENT_POWER");
const Quantity Quantity::_MASS = Quantity(SR_MQ_MASS, "MASS");
const Quantity Quantity::_HARMONIC_RATIO = Quantity(SR_MQ_HARMONIC_RATIO, "HARMONIC_RATIO");
const Quantity Quantity::_ENERGY = Quantity(SR_MQ_ENERGY, "ENERGY");
const Quantity Quantity::_ELECTRIC_CHARGE = Quantity(SR_MQ_ELECTRIC_CHARGE, "ELECTRIC_CHARGE");
const Quantity * const Quantity::VOLTAGE = &Quantity::_VOLTAGE;
const Quantity * const Quantity::CURRENT = &Quantity::_CURRENT;
const Quantity * const Quantity::RESISTANCE = &Quantity::_RESISTANCE;
const Quantity * const Quantity::CAPACITANCE = &Quantity::_CAPACITANCE;
const Quantity * const Quantity::TEMPERATURE = &Quantity::_TEMPERATURE;
const Quantity * const Quantity::FREQUENCY = &Quantity::_FREQUENCY;
const Quantity * const Quantity::DUTY_CYCLE = &Quantity::_DUTY_CYCLE;
const Quantity * const Quantity::CONTINUITY = &Quantity::_CONTINUITY;
const Quantity * const Quantity::PULSE_WIDTH = &Quantity::_PULSE_WIDTH;
const Quantity * const Quantity::CONDUCTANCE = &Quantity::_CONDUCTANCE;
const Quantity * const Quantity::POWER = &Quantity::_POWER;
const Quantity * const Quantity::GAIN = &Quantity::_GAIN;
const Quantity * const Quantity::SOUND_PRESSURE_LEVEL = &Quantity::_SOUND_PRESSURE_LEVEL;
const Quantity * const Quantity::CARBON_MONOXIDE = &Quantity::_CARBON_MONOXIDE;
const Quantity * const Quantity::RELATIVE_HUMIDITY = &Quantity::_RELATIVE_HUMIDITY;
const Quantity * const Quantity::TIME = &Quantity::_TIME;
const Quantity * const Quantity::WIND_SPEED = &Quantity::_WIND_SPEED;
const Quantity * const Quantity::PRESSURE = &Quantity::_PRESSURE;
const Quantity * const Quantity::PARALLEL_INDUCTANCE = &Quantity::_PARALLEL_INDUCTANCE;
const Quantity * const Quantity::PARALLEL_CAPACITANCE = &Quantity::_PARALLEL_CAPACITANCE;
const Quantity * const Quantity::PARALLEL_RESISTANCE = &Quantity::_PARALLEL_RESISTANCE;
const Quantity * const Quantity::SERIES_INDUCTANCE = &Quantity::_SERIES_INDUCTANCE;
const Quantity * const Quantity::SERIES_CAPACITANCE = &Quantity::_SERIES_CAPACITANCE;
const Quantity * const Quantity::SERIES_RESISTANCE = &Quantity::_SERIES_RESISTANCE;
const Quantity * const Quantity::DISSIPATION_FACTOR = &Quantity::_DISSIPATION_FACTOR;
const Quantity * const Quantity::QUALITY_FACTOR = &Quantity::_QUALITY_FACTOR;
const Quantity * const Quantity::PHASE_ANGLE = &Quantity::_PHASE_ANGLE;
const Quantity * const Quantity::DIFFERENCE = &Quantity::_DIFFERENCE;
const Quantity * const Quantity::COUNT = &Quantity::_COUNT;
const Quantity * const Quantity::POWER_FACTOR = &Quantity::_POWER_FACTOR;
const Quantity * const Quantity::APPARENT_POWER = &Quantity::_APPARENT_POWER;
const Quantity * const Quantity::MASS = &Quantity::_MASS;
const Quantity * const Quantity::HARMONIC_RATIO = &Quantity::_HARMONIC_RATIO;
const Quantity * const Quantity::ENERGY = &Quantity::_ENERGY;
const Quantity * const Quantity::ELECTRIC_CHARGE = &Quantity::_ELECTRIC_CHARGE;
template<> const SR_API std::map<const enum sr_mq, const Quantity * const> EnumValue<Quantity, enum sr_mq>::_values = {
	{SR_MQ_VOLTAGE, Quantity::VOLTAGE},
	{SR_MQ_CURRENT, Quantity::CURRENT},
	{SR_MQ_RESISTANCE, Quantity::RESISTANCE},
	{SR_MQ_CAPACITANCE, Quantity::CAPACITANCE},
	{SR_MQ_TEMPERATURE, Quantity::TEMPERATURE},
	{SR_MQ_FREQUENCY, Quantity::FREQUENCY},
	{SR_MQ_DUTY_CYCLE, Quantity::DUTY_CYCLE},
	{SR_MQ_CONTINUITY, Quantity::CONTINUITY},
	{SR_MQ_PULSE_WIDTH, Quantity::PULSE_WIDTH},
	{SR_MQ_CONDUCTANCE, Quantity::CONDUCTANCE},
	{SR_MQ_POWER, Quantity::POWER},
	{SR_MQ_GAIN, Quantity::GAIN},
	{SR_MQ_SOUND_PRESSURE_LEVEL, Quantity::SOUND_PRESSURE_LEVEL},
	{SR_MQ_CARBON_MONOXIDE, Quantity::CARBON_MONOXIDE},
	{SR_MQ_RELATIVE_HUMIDITY, Quantity::RELATIVE_HUMIDITY},
	{SR_MQ_TIME, Quantity::TIME},
	{SR_MQ_WIND_SPEED, Quantity::WIND_SPEED},
	{SR_MQ_PRESSURE, Quantity::PRESSURE},
	{SR_MQ_PARALLEL_INDUCTANCE, Quantity::PARALLEL_INDUCTANCE},
	{SR_MQ_PARALLEL_CAPACITANCE, Quantity::PARALLEL_CAPACITANCE},
	{SR_MQ_PARALLEL_RESISTANCE, Quantity::PARALLEL_RESISTANCE},
	{SR_MQ_SERIES_INDUCTANCE, Quantity::SERIES_INDUCTANCE},
	{SR_MQ_SERIES_CAPACITANCE, Quantity::SERIES_CAPACITANCE},
	{SR_MQ_SERIES_RESISTANCE, Quantity::SERIES_RESISTANCE},
	{SR_MQ_DISSIPATION_FACTOR, Quantity::DISSIPATION_FACTOR},
	{SR_MQ_QUALITY_FACTOR, Quantity::QUALITY_FACTOR},
	{SR_MQ_PHASE_ANGLE, Quantity::PHASE_ANGLE},
	{SR_MQ_DIFFERENCE, Quantity::DIFFERENCE},
	{SR_MQ_COUNT, Quantity::COUNT},
	{SR_MQ_POWER_FACTOR, Quantity::POWER_FACTOR},
	{SR_MQ_APPARENT_POWER, Quantity::APPARENT_POWER},
	{SR_MQ_MASS, Quantity::MASS},
	{SR_MQ_HARMONIC_RATIO, Quantity::HARMONIC_RATIO},
	{SR_MQ_ENERGY, Quantity::ENERGY},
	{SR_MQ_ELECTRIC_CHARGE, Quantity::ELECTRIC_CHARGE},
};
const Unit Unit::_VOLT = Unit(SR_UNIT_VOLT, "VOLT");
const Unit Unit::_AMPERE = Unit(SR_UNIT_AMPERE, "AMPERE");
const Unit Unit::_OHM = Unit(SR_UNIT_OHM, "OHM");
const Unit Unit::_FARAD = Unit(SR_UNIT_FARAD, "FARAD");
const Unit Unit::_KELVIN = Unit(SR_UNIT_KELVIN, "KELVIN");
const Unit Unit::_CELSIUS = Unit(SR_UNIT_CELSIUS, "CELSIUS");
const Unit Unit::_FAHRENHEIT = Unit(SR_UNIT_FAHRENHEIT, "FAHRENHEIT");
const Unit Unit::_HERTZ = Unit(SR_UNIT_HERTZ, "HERTZ");
const Unit Unit::_PERCENTAGE = Unit(SR_UNIT_PERCENTAGE, "PERCENTAGE");
const Unit Unit::_BOOLEAN = Unit(SR_UNIT_BOOLEAN, "BOOLEAN");
const Unit Unit::_SECOND = Unit(SR_UNIT_SECOND, "SECOND");
const Unit Unit::_SIEMENS = Unit(SR_UNIT_SIEMENS, "SIEMENS");
const Unit Unit::_DECIBEL_MW = Unit(SR_UNIT_DECIBEL_MW, "DECIBEL_MW");
const Unit Unit::_DECIBEL_VOLT = Unit(SR_UNIT_DECIBEL_VOLT, "DECIBEL_VOLT");
const Unit Unit::_UNITLESS = Unit(SR_UNIT_UNITLESS, "UNITLESS");
const Unit Unit::_DECIBEL_SPL = Unit(SR_UNIT_DECIBEL_SPL, "DECIBEL_SPL");
const Unit Unit::_CONCENTRATION = Unit(SR_UNIT_CONCENTRATION, "CONCENTRATION");
const Unit Unit::_REVOLUTIONS_PER_MINUTE = Unit(SR_UNIT_REVOLUTIONS_PER_MINUTE, "REVOLUTIONS_PER_MINUTE");
const Unit Unit::_VOLT_AMPERE = Unit(SR_UNIT_VOLT_AMPERE, "VOLT_AMPERE");
const Unit Unit::_WATT = Unit(SR_UNIT_WATT, "WATT");
const Unit Unit::_WATT_HOUR = Unit(SR_UNIT_WATT_HOUR, "WATT_HOUR");
const Unit Unit::_METER_SECOND = Unit(SR_UNIT_METER_SECOND, "METER_SECOND");
const Unit Unit::_HECTOPASCAL = Unit(SR_UNIT_HECTOPASCAL, "HECTOPASCAL");
const Unit Unit::_HUMIDITY_293K = Unit(SR_UNIT_HUMIDITY_293K, "HUMIDITY_293K");
const Unit Unit::_DEGREE = Unit(SR_UNIT_DEGREE, "DEGREE");
const Unit Unit::_HENRY = Unit(SR_UNIT_HENRY, "HENRY");
const Unit Unit::_GRAM = Unit(SR_UNIT_GRAM, "GRAM");
const Unit Unit::_CARAT = Unit(SR_UNIT_CARAT, "CARAT");
const Unit Unit::_OUNCE = Unit(SR_UNIT_OUNCE, "OUNCE");
const Unit Unit::_TROY_OUNCE = Unit(SR_UNIT_TROY_OUNCE, "TROY_OUNCE");
const Unit Unit::_POUND = Unit(SR_UNIT_POUND, "POUND");
const Unit Unit::_PENNYWEIGHT = Unit(SR_UNIT_PENNYWEIGHT, "PENNYWEIGHT");
const Unit Unit::_GRAIN = Unit(SR_UNIT_GRAIN, "GRAIN");
const Unit Unit::_TAEL = Unit(SR_UNIT_TAEL, "TAEL");
const Unit Unit::_MOMME = Unit(SR_UNIT_MOMME, "MOMME");
const Unit Unit::_TOLA = Unit(SR_UNIT_TOLA, "TOLA");
const Unit Unit::_PIECE = Unit(SR_UNIT_PIECE, "PIECE");
const Unit Unit::_JOULE = Unit(SR_UNIT_JOULE, "JOULE");
const Unit Unit::_COULOMB = Unit(SR_UNIT_COULOMB, "COULOMB");
const Unit Unit::_AMPERE_HOUR = Unit(SR_UNIT_AMPERE_HOUR, "AMPERE_HOUR");
const Unit * const Unit::VOLT = &Unit::_VOLT;
const Unit * const Unit::AMPERE = &Unit::_AMPERE;
const Unit * const Unit::OHM = &Unit::_OHM;
const Unit * const Unit::FARAD = &Unit::_FARAD;
const Unit * const Unit::KELVIN = &Unit::_KELVIN;
const Unit * const Unit::CELSIUS = &Unit::_CELSIUS;
const Unit * const Unit::FAHRENHEIT = &Unit::_FAHRENHEIT;
const Unit * const Unit::HERTZ = &Unit::_HERTZ;
const Unit * const Unit::PERCENTAGE = &Unit::_PERCENTAGE;
const Unit * const Unit::BOOLEAN = &Unit::_BOOLEAN;
const Unit * const Unit::SECOND = &Unit::_SECOND;
const Unit * const Unit::SIEMENS = &Unit::_SIEMENS;
const Unit * const Unit::DECIBEL_MW = &Unit::_DECIBEL_MW;
const Unit * const Unit::DECIBEL_VOLT = &Unit::_DECIBEL_VOLT;
const Unit * const Unit::UNITLESS = &Unit::_UNITLESS;
const Unit * const Unit::DECIBEL_SPL = &Unit::_DECIBEL_SPL;
const Unit * const Unit::CONCENTRATION = &Unit::_CONCENTRATION;
const Unit * const Unit::REVOLUTIONS_PER_MINUTE = &Unit::_REVOLUTIONS_PER_MINUTE;
const Unit * const Unit::VOLT_AMPERE = &Unit::_VOLT_AMPERE;
const Unit * const Unit::WATT = &Unit::_WATT;
const Unit * const Unit::WATT_HOUR = &Unit::_WATT_HOUR;
const Unit * const Unit::METER_SECOND = &Unit::_METER_SECOND;
const Unit * const Unit::HECTOPASCAL = &Unit::_HECTOPASCAL;
const Unit * const Unit::HUMIDITY_293K = &Unit::_HUMIDITY_293K;
const Unit * const Unit::DEGREE = &Unit::_DEGREE;
const Unit * const Unit::HENRY = &Unit::_HENRY;
const Unit * const Unit::GRAM = &Unit::_GRAM;
const Unit * const Unit::CARAT = &Unit::_CARAT;
const Unit * const Unit::OUNCE = &Unit::_OUNCE;
const Unit * const Unit::TROY_OUNCE = &Unit::_TROY_OUNCE;
const Unit * const Unit::POUND = &Unit::_POUND;
const Unit * const Unit::PENNYWEIGHT = &Unit::_PENNYWEIGHT;
const Unit * const Unit::GRAIN = &Unit::_GRAIN;
const Unit * const Unit::TAEL = &Unit::_TAEL;
const Unit * const Unit::MOMME = &Unit::_MOMME;
const Unit * const Unit::TOLA = &Unit::_TOLA;
const Unit * const Unit::PIECE = &Unit::_PIECE;
const Unit * const Unit::JOULE = &Unit::_JOULE;
const Unit * const Unit::COULOMB = &Unit::_COULOMB;
const Unit * const Unit::AMPERE_HOUR = &Unit::_AMPERE_HOUR;
template<> const SR_API std::map<const enum sr_unit, const Unit * const> EnumValue<Unit, enum sr_unit>::_values = {
	{SR_UNIT_VOLT, Unit::VOLT},
	{SR_UNIT_AMPERE, Unit::AMPERE},
	{SR_UNIT_OHM, Unit::OHM},
	{SR_UNIT_FARAD, Unit::FARAD},
	{SR_UNIT_KELVIN, Unit::KELVIN},
	{SR_UNIT_CELSIUS, Unit::CELSIUS},
	{SR_UNIT_FAHRENHEIT, Unit::FAHRENHEIT},
	{SR_UNIT_HERTZ, Unit::HERTZ},
	{SR_UNIT_PERCENTAGE, Unit::PERCENTAGE},
	{SR_UNIT_BOOLEAN, Unit::BOOLEAN},
	{SR_UNIT_SECOND, Unit::SECOND},
	{SR_UNIT_SIEMENS, Unit::SIEMENS},
	{SR_UNIT_DECIBEL_MW, Unit::DECIBEL_MW},
	{SR_UNIT_DECIBEL_VOLT, Unit::DECIBEL_VOLT},
	{SR_UNIT_UNITLESS, Unit::UNITLESS},
	{SR_UNIT_DECIBEL_SPL, Unit::DECIBEL_SPL},
	{SR_UNIT_CONCENTRATION, Unit::CONCENTRATION},
	{SR_UNIT_REVOLUTIONS_PER_MINUTE, Unit::REVOLUTIONS_PER_MINUTE},
	{SR_UNIT_VOLT_AMPERE, Unit::VOLT_AMPERE},
	{SR_UNIT_WATT, Unit::WATT},
	{SR_UNIT_WATT_HOUR, Unit::WATT_HOUR},
	{SR_UNIT_METER_SECOND, Unit::METER_SECOND},
	{SR_UNIT_HECTOPASCAL, Unit::HECTOPASCAL},
	{SR_UNIT_HUMIDITY_293K, Unit::HUMIDITY_293K},
	{SR_UNIT_DEGREE, Unit::DEGREE},
	{SR_UNIT_HENRY, Unit::HENRY},
	{SR_UNIT_GRAM, Unit::GRAM},
	{SR_UNIT_CARAT, Unit::CARAT},
	{SR_UNIT_OUNCE, Unit::OUNCE},
	{SR_UNIT_TROY_OUNCE, Unit::TROY_OUNCE},
	{SR_UNIT_POUND, Unit::POUND},
	{SR_UNIT_PENNYWEIGHT, Unit::PENNYWEIGHT},
	{SR_UNIT_GRAIN, Unit::GRAIN},
	{SR_UNIT_TAEL, Unit::TAEL},
	{SR_UNIT_MOMME, Unit::MOMME},
	{SR_UNIT_TOLA, Unit::TOLA},
	{SR_UNIT_PIECE, Unit::PIECE},
	{SR_UNIT_JOULE, Unit::JOULE},
	{SR_UNIT_COULOMB, Unit::COULOMB},
	{SR_UNIT_AMPERE_HOUR, Unit::AMPERE_HOUR},
};
const QuantityFlag QuantityFlag::_AC = QuantityFlag(SR_MQFLAG_AC, "AC");
const QuantityFlag QuantityFlag::_DC = QuantityFlag(SR_MQFLAG_DC, "DC");
const QuantityFlag QuantityFlag::_RMS = QuantityFlag(SR_MQFLAG_RMS, "RMS");
const QuantityFlag QuantityFlag::_DIODE = QuantityFlag(SR_MQFLAG_DIODE, "DIODE");
const QuantityFlag QuantityFlag::_HOLD = QuantityFlag(SR_MQFLAG_HOLD, "HOLD");
const QuantityFlag QuantityFlag::_MAX = QuantityFlag(SR_MQFLAG_MAX, "MAX");
const QuantityFlag QuantityFlag::_MIN = QuantityFlag(SR_MQFLAG_MIN, "MIN");
const QuantityFlag QuantityFlag::_AUTORANGE = QuantityFlag(SR_MQFLAG_AUTORANGE, "AUTORANGE");
const QuantityFlag QuantityFlag::_RELATIVE = QuantityFlag(SR_MQFLAG_RELATIVE, "RELATIVE");
const QuantityFlag QuantityFlag::_SPL_FREQ_WEIGHT_A = QuantityFlag(SR_MQFLAG_SPL_FREQ_WEIGHT_A, "SPL_FREQ_WEIGHT_A");
const QuantityFlag QuantityFlag::_SPL_FREQ_WEIGHT_C = QuantityFlag(SR_MQFLAG_SPL_FREQ_WEIGHT_C, "SPL_FREQ_WEIGHT_C");
const QuantityFlag QuantityFlag::_SPL_FREQ_WEIGHT_Z = QuantityFlag(SR_MQFLAG_SPL_FREQ_WEIGHT_Z, "SPL_FREQ_WEIGHT_Z");
const QuantityFlag QuantityFlag::_SPL_FREQ_WEIGHT_FLAT = QuantityFlag(SR_MQFLAG_SPL_FREQ_WEIGHT_FLAT, "SPL_FREQ_WEIGHT_FLAT");
const QuantityFlag QuantityFlag::_SPL_TIME_WEIGHT_S = QuantityFlag(SR_MQFLAG_SPL_TIME_WEIGHT_S, "SPL_TIME_WEIGHT_S");
const QuantityFlag QuantityFlag::_SPL_TIME_WEIGHT_F = QuantityFlag(SR_MQFLAG_SPL_TIME_WEIGHT_F, "SPL_TIME_WEIGHT_F");
const QuantityFlag QuantityFlag::_SPL_LAT = QuantityFlag(SR_MQFLAG_SPL_LAT, "SPL_LAT");
const QuantityFlag QuantityFlag::_SPL_PCT_OVER_ALARM = QuantityFlag(SR_MQFLAG_SPL_PCT_OVER_ALARM, "SPL_PCT_OVER_ALARM");
const QuantityFlag QuantityFlag::_DURATION = QuantityFlag(SR_MQFLAG_DURATION, "DURATION");
const QuantityFlag QuantityFlag::_AVG = QuantityFlag(SR_MQFLAG_AVG, "AVG");
const QuantityFlag QuantityFlag::_REFERENCE = QuantityFlag(SR_MQFLAG_REFERENCE, "REFERENCE");
const QuantityFlag QuantityFlag::_UNSTABLE = QuantityFlag(SR_MQFLAG_UNSTABLE, "UNSTABLE");
const QuantityFlag QuantityFlag::_FOUR_WIRE = QuantityFlag(SR_MQFLAG_FOUR_WIRE, "FOUR_WIRE");
const QuantityFlag * const QuantityFlag::AC = &QuantityFlag::_AC;
const QuantityFlag * const QuantityFlag::DC = &QuantityFlag::_DC;
const QuantityFlag * const QuantityFlag::RMS = &QuantityFlag::_RMS;
const QuantityFlag * const QuantityFlag::DIODE = &QuantityFlag::_DIODE;
const QuantityFlag * const QuantityFlag::HOLD = &QuantityFlag::_HOLD;
const QuantityFlag * const QuantityFlag::MAX = &QuantityFlag::_MAX;
const QuantityFlag * const QuantityFlag::MIN = &QuantityFlag::_MIN;
const QuantityFlag * const QuantityFlag::AUTORANGE = &QuantityFlag::_AUTORANGE;
const QuantityFlag * const QuantityFlag::RELATIVE = &QuantityFlag::_RELATIVE;
const QuantityFlag * const QuantityFlag::SPL_FREQ_WEIGHT_A = &QuantityFlag::_SPL_FREQ_WEIGHT_A;
const QuantityFlag * const QuantityFlag::SPL_FREQ_WEIGHT_C = &QuantityFlag::_SPL_FREQ_WEIGHT_C;
const QuantityFlag * const QuantityFlag::SPL_FREQ_WEIGHT_Z = &QuantityFlag::_SPL_FREQ_WEIGHT_Z;
const QuantityFlag * const QuantityFlag::SPL_FREQ_WEIGHT_FLAT = &QuantityFlag::_SPL_FREQ_WEIGHT_FLAT;
const QuantityFlag * const QuantityFlag::SPL_TIME_WEIGHT_S = &QuantityFlag::_SPL_TIME_WEIGHT_S;
const QuantityFlag * const QuantityFlag::SPL_TIME_WEIGHT_F = &QuantityFlag::_SPL_TIME_WEIGHT_F;
const QuantityFlag * const QuantityFlag::SPL_LAT = &QuantityFlag::_SPL_LAT;
const QuantityFlag * const QuantityFlag::SPL_PCT_OVER_ALARM = &QuantityFlag::_SPL_PCT_OVER_ALARM;
const QuantityFlag * const QuantityFlag::DURATION = &QuantityFlag::_DURATION;
const QuantityFlag * const QuantityFlag::AVG = &QuantityFlag::_AVG;
const QuantityFlag * const QuantityFlag::REFERENCE = &QuantityFlag::_REFERENCE;
const QuantityFlag * const QuantityFlag::UNSTABLE = &QuantityFlag::_UNSTABLE;
const QuantityFlag * const QuantityFlag::FOUR_WIRE = &QuantityFlag::_FOUR_WIRE;
template<> const SR_API std::map<const enum sr_mqflag, const QuantityFlag * const> EnumValue<QuantityFlag, enum sr_mqflag>::_values = {
	{SR_MQFLAG_AC, QuantityFlag::AC},
	{SR_MQFLAG_DC, QuantityFlag::DC},
	{SR_MQFLAG_RMS, QuantityFlag::RMS},
	{SR_MQFLAG_DIODE, QuantityFlag::DIODE},
	{SR_MQFLAG_HOLD, QuantityFlag::HOLD},
	{SR_MQFLAG_MAX, QuantityFlag::MAX},
	{SR_MQFLAG_MIN, QuantityFlag::MIN},
	{SR_MQFLAG_AUTORANGE, QuantityFlag::AUTORANGE},
	{SR_MQFLAG_RELATIVE, QuantityFlag::RELATIVE},
	{SR_MQFLAG_SPL_FREQ_WEIGHT_A, QuantityFlag::SPL_FREQ_WEIGHT_A},
	{SR_MQFLAG_SPL_FREQ_WEIGHT_C, QuantityFlag::SPL_FREQ_WEIGHT_C},
	{SR_MQFLAG_SPL_FREQ_WEIGHT_Z, QuantityFlag::SPL_FREQ_WEIGHT_Z},
	{SR_MQFLAG_SPL_FREQ_WEIGHT_FLAT, QuantityFlag::SPL_FREQ_WEIGHT_FLAT},
	{SR_MQFLAG_SPL_TIME_WEIGHT_S, QuantityFlag::SPL_TIME_WEIGHT_S},
	{SR_MQFLAG_SPL_TIME_WEIGHT_F, QuantityFlag::SPL_TIME_WEIGHT_F},
	{SR_MQFLAG_SPL_LAT, QuantityFlag::SPL_LAT},
	{SR_MQFLAG_SPL_PCT_OVER_ALARM, QuantityFlag::SPL_PCT_OVER_ALARM},
	{SR_MQFLAG_DURATION, QuantityFlag::DURATION},
	{SR_MQFLAG_AVG, QuantityFlag::AVG},
	{SR_MQFLAG_REFERENCE, QuantityFlag::REFERENCE},
	{SR_MQFLAG_UNSTABLE, QuantityFlag::UNSTABLE},
	{SR_MQFLAG_FOUR_WIRE, QuantityFlag::FOUR_WIRE},
};
std::vector<const QuantityFlag *>
    QuantityFlag::flags_from_mask(unsigned int mask)
{
    auto result = std::vector<const QuantityFlag *>();
    while (mask)
    {
        unsigned int new_mask = mask & (mask - 1);
        result.push_back(QuantityFlag::get(
            static_cast<enum sr_mqflag>(mask ^ new_mask)));
        mask = new_mask;
    }
    return result;
}

unsigned int QuantityFlag::mask_from_flags(std::vector<const QuantityFlag *> flags)
{
    unsigned int result = 0;
    for (auto flag : flags)
        result |= flag->id();
    return result;
}

const TriggerMatchType TriggerMatchType::_ZERO = TriggerMatchType(SR_TRIGGER_ZERO, "ZERO");
const TriggerMatchType TriggerMatchType::_ONE = TriggerMatchType(SR_TRIGGER_ONE, "ONE");
const TriggerMatchType TriggerMatchType::_RISING = TriggerMatchType(SR_TRIGGER_RISING, "RISING");
const TriggerMatchType TriggerMatchType::_FALLING = TriggerMatchType(SR_TRIGGER_FALLING, "FALLING");
const TriggerMatchType TriggerMatchType::_EDGE = TriggerMatchType(SR_TRIGGER_EDGE, "EDGE");
const TriggerMatchType TriggerMatchType::_OVER = TriggerMatchType(SR_TRIGGER_OVER, "OVER");
const TriggerMatchType TriggerMatchType::_UNDER = TriggerMatchType(SR_TRIGGER_UNDER, "UNDER");
const TriggerMatchType * const TriggerMatchType::ZERO = &TriggerMatchType::_ZERO;
const TriggerMatchType * const TriggerMatchType::ONE = &TriggerMatchType::_ONE;
const TriggerMatchType * const TriggerMatchType::RISING = &TriggerMatchType::_RISING;
const TriggerMatchType * const TriggerMatchType::FALLING = &TriggerMatchType::_FALLING;
const TriggerMatchType * const TriggerMatchType::EDGE = &TriggerMatchType::_EDGE;
const TriggerMatchType * const TriggerMatchType::OVER = &TriggerMatchType::_OVER;
const TriggerMatchType * const TriggerMatchType::UNDER = &TriggerMatchType::_UNDER;
template<> const SR_API std::map<const enum sr_trigger_matches, const TriggerMatchType * const> EnumValue<TriggerMatchType, enum sr_trigger_matches>::_values = {
	{SR_TRIGGER_ZERO, TriggerMatchType::ZERO},
	{SR_TRIGGER_ONE, TriggerMatchType::ONE},
	{SR_TRIGGER_RISING, TriggerMatchType::RISING},
	{SR_TRIGGER_FALLING, TriggerMatchType::FALLING},
	{SR_TRIGGER_EDGE, TriggerMatchType::EDGE},
	{SR_TRIGGER_OVER, TriggerMatchType::OVER},
	{SR_TRIGGER_UNDER, TriggerMatchType::UNDER},
};
const OutputFlag OutputFlag::_INTERNAL_IO_HANDLING = OutputFlag(SR_OUTPUT_INTERNAL_IO_HANDLING, "INTERNAL_IO_HANDLING");
const OutputFlag * const OutputFlag::INTERNAL_IO_HANDLING = &OutputFlag::_INTERNAL_IO_HANDLING;
template<> const SR_API std::map<const enum sr_output_flag, const OutputFlag * const> EnumValue<OutputFlag, enum sr_output_flag>::_values = {
	{SR_OUTPUT_INTERNAL_IO_HANDLING, OutputFlag::INTERNAL_IO_HANDLING},
};
const ChannelType ChannelType::_LOGIC = ChannelType(SR_CHANNEL_LOGIC, "LOGIC");
const ChannelType ChannelType::_ANALOG = ChannelType(SR_CHANNEL_ANALOG, "ANALOG");
const ChannelType * const ChannelType::LOGIC = &ChannelType::_LOGIC;
const ChannelType * const ChannelType::ANALOG = &ChannelType::_ANALOG;
template<> const SR_API std::map<const enum sr_channeltype, const ChannelType * const> EnumValue<ChannelType, enum sr_channeltype>::_values = {
	{SR_CHANNEL_LOGIC, ChannelType::LOGIC},
	{SR_CHANNEL_ANALOG, ChannelType::ANALOG},
};
const Capability Capability::_GET = Capability(SR_CONF_GET, "GET");
const Capability Capability::_SET = Capability(SR_CONF_SET, "SET");
const Capability Capability::_LIST = Capability(SR_CONF_LIST, "LIST");
const Capability * const Capability::GET = &Capability::_GET;
const Capability * const Capability::SET = &Capability::_SET;
const Capability * const Capability::LIST = &Capability::_LIST;
template<> const SR_API std::map<const enum sr_configcap, const Capability * const> EnumValue<Capability, enum sr_configcap>::_values = {
	{SR_CONF_GET, Capability::GET},
	{SR_CONF_SET, Capability::SET},
	{SR_CONF_LIST, Capability::LIST},
};
const ConfigKey ConfigKey::_LOGIC_ANALYZER = ConfigKey(SR_CONF_LOGIC_ANALYZER, "LOGIC_ANALYZER");
const ConfigKey ConfigKey::_OSCILLOSCOPE = ConfigKey(SR_CONF_OSCILLOSCOPE, "OSCILLOSCOPE");
const ConfigKey ConfigKey::_MULTIMETER = ConfigKey(SR_CONF_MULTIMETER, "MULTIMETER");
const ConfigKey ConfigKey::_DEMO_DEV = ConfigKey(SR_CONF_DEMO_DEV, "DEMO_DEV");
const ConfigKey ConfigKey::_SOUNDLEVELMETER = ConfigKey(SR_CONF_SOUNDLEVELMETER, "SOUNDLEVELMETER");
const ConfigKey ConfigKey::_THERMOMETER = ConfigKey(SR_CONF_THERMOMETER, "THERMOMETER");
const ConfigKey ConfigKey::_HYGROMETER = ConfigKey(SR_CONF_HYGROMETER, "HYGROMETER");
const ConfigKey ConfigKey::_ENERGYMETER = ConfigKey(SR_CONF_ENERGYMETER, "ENERGYMETER");
const ConfigKey ConfigKey::_DEMODULATOR = ConfigKey(SR_CONF_DEMODULATOR, "DEMODULATOR");
const ConfigKey ConfigKey::_POWER_SUPPLY = ConfigKey(SR_CONF_POWER_SUPPLY, "POWER_SUPPLY");
const ConfigKey ConfigKey::_LCRMETER = ConfigKey(SR_CONF_LCRMETER, "LCRMETER");
const ConfigKey ConfigKey::_ELECTRONIC_LOAD = ConfigKey(SR_CONF_ELECTRONIC_LOAD, "ELECTRONIC_LOAD");
const ConfigKey ConfigKey::_SCALE = ConfigKey(SR_CONF_SCALE, "SCALE");
const ConfigKey ConfigKey::_SIGNAL_GENERATOR = ConfigKey(SR_CONF_SIGNAL_GENERATOR, "SIGNAL_GENERATOR");
const ConfigKey ConfigKey::_POWERMETER = ConfigKey(SR_CONF_POWERMETER, "POWERMETER");
const ConfigKey ConfigKey::_CONN = ConfigKey(SR_CONF_CONN, "CONN");
const ConfigKey ConfigKey::_SERIALCOMM = ConfigKey(SR_CONF_SERIALCOMM, "SERIALCOMM");
const ConfigKey ConfigKey::_MODBUSADDR = ConfigKey(SR_CONF_MODBUSADDR, "MODBUSADDR");
const ConfigKey ConfigKey::_FORCE_DETECT = ConfigKey(SR_CONF_FORCE_DETECT, "FORCE_DETECT");
const ConfigKey ConfigKey::_SAMPLERATE = ConfigKey(SR_CONF_SAMPLERATE, "SAMPLERATE");
const ConfigKey ConfigKey::_CAPTURE_RATIO = ConfigKey(SR_CONF_CAPTURE_RATIO, "CAPTURE_RATIO");
const ConfigKey ConfigKey::_PATTERN_MODE = ConfigKey(SR_CONF_PATTERN_MODE, "PATTERN_MODE");
const ConfigKey ConfigKey::_RLE = ConfigKey(SR_CONF_RLE, "RLE");
const ConfigKey ConfigKey::_TRIGGER_SLOPE = ConfigKey(SR_CONF_TRIGGER_SLOPE, "TRIGGER_SLOPE");
const ConfigKey ConfigKey::_AVERAGING = ConfigKey(SR_CONF_AVERAGING, "AVERAGING");
const ConfigKey ConfigKey::_AVG_SAMPLES = ConfigKey(SR_CONF_AVG_SAMPLES, "AVG_SAMPLES");
const ConfigKey ConfigKey::_TRIGGER_SOURCE = ConfigKey(SR_CONF_TRIGGER_SOURCE, "TRIGGER_SOURCE");
const ConfigKey ConfigKey::_HORIZ_TRIGGERPOS = ConfigKey(SR_CONF_HORIZ_TRIGGERPOS, "HORIZ_TRIGGERPOS");
const ConfigKey ConfigKey::_BUFFERSIZE = ConfigKey(SR_CONF_BUFFERSIZE, "BUFFERSIZE");
const ConfigKey ConfigKey::_TIMEBASE = ConfigKey(SR_CONF_TIMEBASE, "TIMEBASE");
const ConfigKey ConfigKey::_FILTER = ConfigKey(SR_CONF_FILTER, "FILTER");
const ConfigKey ConfigKey::_VDIV = ConfigKey(SR_CONF_VDIV, "VDIV");
const ConfigKey ConfigKey::_COUPLING = ConfigKey(SR_CONF_COUPLING, "COUPLING");
const ConfigKey ConfigKey::_TRIGGER_MATCH = ConfigKey(SR_CONF_TRIGGER_MATCH, "TRIGGER_MATCH");
const ConfigKey ConfigKey::_SAMPLE_INTERVAL = ConfigKey(SR_CONF_SAMPLE_INTERVAL, "SAMPLE_INTERVAL");
const ConfigKey ConfigKey::_NUM_HDIV = ConfigKey(SR_CONF_NUM_HDIV, "NUM_HDIV");
const ConfigKey ConfigKey::_NUM_VDIV = ConfigKey(SR_CONF_NUM_VDIV, "NUM_VDIV");
const ConfigKey ConfigKey::_SPL_WEIGHT_FREQ = ConfigKey(SR_CONF_SPL_WEIGHT_FREQ, "SPL_WEIGHT_FREQ");
const ConfigKey ConfigKey::_SPL_WEIGHT_TIME = ConfigKey(SR_CONF_SPL_WEIGHT_TIME, "SPL_WEIGHT_TIME");
const ConfigKey ConfigKey::_SPL_MEASUREMENT_RANGE = ConfigKey(SR_CONF_SPL_MEASUREMENT_RANGE, "SPL_MEASUREMENT_RANGE");
const ConfigKey ConfigKey::_HOLD_MAX = ConfigKey(SR_CONF_HOLD_MAX, "HOLD_MAX");
const ConfigKey ConfigKey::_HOLD_MIN = ConfigKey(SR_CONF_HOLD_MIN, "HOLD_MIN");
const ConfigKey ConfigKey::_VOLTAGE_THRESHOLD = ConfigKey(SR_CONF_VOLTAGE_THRESHOLD, "VOLTAGE_THRESHOLD");
const ConfigKey ConfigKey::_EXTERNAL_CLOCK = ConfigKey(SR_CONF_EXTERNAL_CLOCK, "EXTERNAL_CLOCK");
const ConfigKey ConfigKey::_SWAP = ConfigKey(SR_CONF_SWAP, "SWAP");
const ConfigKey ConfigKey::_CENTER_FREQUENCY = ConfigKey(SR_CONF_CENTER_FREQUENCY, "CENTER_FREQUENCY");
const ConfigKey ConfigKey::_NUM_LOGIC_CHANNELS = ConfigKey(SR_CONF_NUM_LOGIC_CHANNELS, "NUM_LOGIC_CHANNELS");
const ConfigKey ConfigKey::_NUM_ANALOG_CHANNELS = ConfigKey(SR_CONF_NUM_ANALOG_CHANNELS, "NUM_ANALOG_CHANNELS");
const ConfigKey ConfigKey::_VOLTAGE = ConfigKey(SR_CONF_VOLTAGE, "VOLTAGE");
const ConfigKey ConfigKey::_VOLTAGE_TARGET = ConfigKey(SR_CONF_VOLTAGE_TARGET, "VOLTAGE_TARGET");
const ConfigKey ConfigKey::_CURRENT = ConfigKey(SR_CONF_CURRENT, "CURRENT");
const ConfigKey ConfigKey::_CURRENT_LIMIT = ConfigKey(SR_CONF_CURRENT_LIMIT, "CURRENT_LIMIT");
const ConfigKey ConfigKey::_ENABLED = ConfigKey(SR_CONF_ENABLED, "ENABLED");
const ConfigKey ConfigKey::_CHANNEL_CONFIG = ConfigKey(SR_CONF_CHANNEL_CONFIG, "CHANNEL_CONFIG");
const ConfigKey ConfigKey::_OVER_VOLTAGE_PROTECTION_ENABLED = ConfigKey(SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED, "OVER_VOLTAGE_PROTECTION_ENABLED");
const ConfigKey ConfigKey::_OVER_VOLTAGE_PROTECTION_ACTIVE = ConfigKey(SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE, "OVER_VOLTAGE_PROTECTION_ACTIVE");
const ConfigKey ConfigKey::_OVER_VOLTAGE_PROTECTION_THRESHOLD = ConfigKey(SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD, "OVER_VOLTAGE_PROTECTION_THRESHOLD");
const ConfigKey ConfigKey::_OVER_CURRENT_PROTECTION_ENABLED = ConfigKey(SR_CONF_OVER_CURRENT_PROTECTION_ENABLED, "OVER_CURRENT_PROTECTION_ENABLED");
const ConfigKey ConfigKey::_OVER_CURRENT_PROTECTION_ACTIVE = ConfigKey(SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE, "OVER_CURRENT_PROTECTION_ACTIVE");
const ConfigKey ConfigKey::_OVER_CURRENT_PROTECTION_THRESHOLD = ConfigKey(SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD, "OVER_CURRENT_PROTECTION_THRESHOLD");
const ConfigKey ConfigKey::_CLOCK_EDGE = ConfigKey(SR_CONF_CLOCK_EDGE, "CLOCK_EDGE");
const ConfigKey ConfigKey::_AMPLITUDE = ConfigKey(SR_CONF_AMPLITUDE, "AMPLITUDE");
const ConfigKey ConfigKey::_REGULATION = ConfigKey(SR_CONF_REGULATION, "REGULATION");
const ConfigKey ConfigKey::_OVER_TEMPERATURE_PROTECTION = ConfigKey(SR_CONF_OVER_TEMPERATURE_PROTECTION, "OVER_TEMPERATURE_PROTECTION");
const ConfigKey ConfigKey::_OUTPUT_FREQUENCY = ConfigKey(SR_CONF_OUTPUT_FREQUENCY, "OUTPUT_FREQUENCY");
const ConfigKey ConfigKey::_OUTPUT_FREQUENCY_TARGET = ConfigKey(SR_CONF_OUTPUT_FREQUENCY_TARGET, "OUTPUT_FREQUENCY_TARGET");
const ConfigKey ConfigKey::_MEASURED_QUANTITY = ConfigKey(SR_CONF_MEASURED_QUANTITY, "MEASURED_QUANTITY");
const ConfigKey ConfigKey::_EQUIV_CIRCUIT_MODEL = ConfigKey(SR_CONF_EQUIV_CIRCUIT_MODEL, "EQUIV_CIRCUIT_MODEL");
const ConfigKey ConfigKey::_OVER_TEMPERATURE_PROTECTION_ACTIVE = ConfigKey(SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE, "OVER_TEMPERATURE_PROTECTION_ACTIVE");
const ConfigKey ConfigKey::_UNDER_VOLTAGE_CONDITION = ConfigKey(SR_CONF_UNDER_VOLTAGE_CONDITION, "UNDER_VOLTAGE_CONDITION");
const ConfigKey ConfigKey::_UNDER_VOLTAGE_CONDITION_ACTIVE = ConfigKey(SR_CONF_UNDER_VOLTAGE_CONDITION_ACTIVE, "UNDER_VOLTAGE_CONDITION_ACTIVE");
const ConfigKey ConfigKey::_TRIGGER_LEVEL = ConfigKey(SR_CONF_TRIGGER_LEVEL, "TRIGGER_LEVEL");
const ConfigKey ConfigKey::_UNDER_VOLTAGE_CONDITION_THRESHOLD = ConfigKey(SR_CONF_UNDER_VOLTAGE_CONDITION_THRESHOLD, "UNDER_VOLTAGE_CONDITION_THRESHOLD");
const ConfigKey ConfigKey::_EXTERNAL_CLOCK_SOURCE = ConfigKey(SR_CONF_EXTERNAL_CLOCK_SOURCE, "EXTERNAL_CLOCK_SOURCE");
const ConfigKey ConfigKey::_OFFSET = ConfigKey(SR_CONF_OFFSET, "OFFSET");
const ConfigKey ConfigKey::_TRIGGER_PATTERN = ConfigKey(SR_CONF_TRIGGER_PATTERN, "TRIGGER_PATTERN");
const ConfigKey ConfigKey::_HIGH_RESOLUTION = ConfigKey(SR_CONF_HIGH_RESOLUTION, "HIGH_RESOLUTION");
const ConfigKey ConfigKey::_PEAK_DETECTION = ConfigKey(SR_CONF_PEAK_DETECTION, "PEAK_DETECTION");
const ConfigKey ConfigKey::_LOGIC_THRESHOLD = ConfigKey(SR_CONF_LOGIC_THRESHOLD, "LOGIC_THRESHOLD");
const ConfigKey ConfigKey::_LOGIC_THRESHOLD_CUSTOM = ConfigKey(SR_CONF_LOGIC_THRESHOLD_CUSTOM, "LOGIC_THRESHOLD_CUSTOM");
const ConfigKey ConfigKey::_RANGE = ConfigKey(SR_CONF_RANGE, "RANGE");
const ConfigKey ConfigKey::_DIGITS = ConfigKey(SR_CONF_DIGITS, "DIGITS");
const ConfigKey ConfigKey::_PHASE = ConfigKey(SR_CONF_PHASE, "PHASE");
const ConfigKey ConfigKey::_DUTY_CYCLE = ConfigKey(SR_CONF_DUTY_CYCLE, "DUTY_CYCLE");
const ConfigKey ConfigKey::_POWER = ConfigKey(SR_CONF_POWER, "POWER");
const ConfigKey ConfigKey::_POWER_TARGET = ConfigKey(SR_CONF_POWER_TARGET, "POWER_TARGET");
const ConfigKey ConfigKey::_RESISTANCE_TARGET = ConfigKey(SR_CONF_RESISTANCE_TARGET, "RESISTANCE_TARGET");
const ConfigKey ConfigKey::_SESSIONFILE = ConfigKey(SR_CONF_SESSIONFILE, "SESSIONFILE");
const ConfigKey ConfigKey::_CAPTUREFILE = ConfigKey(SR_CONF_CAPTUREFILE, "CAPTUREFILE");
const ConfigKey ConfigKey::_CAPTURE_UNITSIZE = ConfigKey(SR_CONF_CAPTURE_UNITSIZE, "CAPTURE_UNITSIZE");
const ConfigKey ConfigKey::_POWER_OFF = ConfigKey(SR_CONF_POWER_OFF, "POWER_OFF");
const ConfigKey ConfigKey::_DATA_SOURCE = ConfigKey(SR_CONF_DATA_SOURCE, "DATA_SOURCE");
const ConfigKey ConfigKey::_PROBE_FACTOR = ConfigKey(SR_CONF_PROBE_FACTOR, "PROBE_FACTOR");
const ConfigKey ConfigKey::_ADC_POWERLINE_CYCLES = ConfigKey(SR_CONF_ADC_POWERLINE_CYCLES, "ADC_POWERLINE_CYCLES");
const ConfigKey ConfigKey::_LIMIT_MSEC = ConfigKey(SR_CONF_LIMIT_MSEC, "LIMIT_MSEC");
const ConfigKey ConfigKey::_LIMIT_SAMPLES = ConfigKey(SR_CONF_LIMIT_SAMPLES, "LIMIT_SAMPLES");
const ConfigKey ConfigKey::_LIMIT_FRAMES = ConfigKey(SR_CONF_LIMIT_FRAMES, "LIMIT_FRAMES");
const ConfigKey ConfigKey::_CONTINUOUS = ConfigKey(SR_CONF_CONTINUOUS, "CONTINUOUS");
const ConfigKey ConfigKey::_DATALOG = ConfigKey(SR_CONF_DATALOG, "DATALOG");
const ConfigKey ConfigKey::_DEVICE_MODE = ConfigKey(SR_CONF_DEVICE_MODE, "DEVICE_MODE");
const ConfigKey ConfigKey::_TEST_MODE = ConfigKey(SR_CONF_TEST_MODE, "TEST_MODE");
const ConfigKey * const ConfigKey::LOGIC_ANALYZER = &ConfigKey::_LOGIC_ANALYZER;
const ConfigKey * const ConfigKey::OSCILLOSCOPE = &ConfigKey::_OSCILLOSCOPE;
const ConfigKey * const ConfigKey::MULTIMETER = &ConfigKey::_MULTIMETER;
const ConfigKey * const ConfigKey::DEMO_DEV = &ConfigKey::_DEMO_DEV;
const ConfigKey * const ConfigKey::SOUNDLEVELMETER = &ConfigKey::_SOUNDLEVELMETER;
const ConfigKey * const ConfigKey::THERMOMETER = &ConfigKey::_THERMOMETER;
const ConfigKey * const ConfigKey::HYGROMETER = &ConfigKey::_HYGROMETER;
const ConfigKey * const ConfigKey::ENERGYMETER = &ConfigKey::_ENERGYMETER;
const ConfigKey * const ConfigKey::DEMODULATOR = &ConfigKey::_DEMODULATOR;
const ConfigKey * const ConfigKey::POWER_SUPPLY = &ConfigKey::_POWER_SUPPLY;
const ConfigKey * const ConfigKey::LCRMETER = &ConfigKey::_LCRMETER;
const ConfigKey * const ConfigKey::ELECTRONIC_LOAD = &ConfigKey::_ELECTRONIC_LOAD;
const ConfigKey * const ConfigKey::SCALE = &ConfigKey::_SCALE;
const ConfigKey * const ConfigKey::SIGNAL_GENERATOR = &ConfigKey::_SIGNAL_GENERATOR;
const ConfigKey * const ConfigKey::POWERMETER = &ConfigKey::_POWERMETER;
const ConfigKey * const ConfigKey::CONN = &ConfigKey::_CONN;
const ConfigKey * const ConfigKey::SERIALCOMM = &ConfigKey::_SERIALCOMM;
const ConfigKey * const ConfigKey::MODBUSADDR = &ConfigKey::_MODBUSADDR;
const ConfigKey * const ConfigKey::FORCE_DETECT = &ConfigKey::_FORCE_DETECT;
const ConfigKey * const ConfigKey::SAMPLERATE = &ConfigKey::_SAMPLERATE;
const ConfigKey * const ConfigKey::CAPTURE_RATIO = &ConfigKey::_CAPTURE_RATIO;
const ConfigKey * const ConfigKey::PATTERN_MODE = &ConfigKey::_PATTERN_MODE;
const ConfigKey * const ConfigKey::RLE = &ConfigKey::_RLE;
const ConfigKey * const ConfigKey::TRIGGER_SLOPE = &ConfigKey::_TRIGGER_SLOPE;
const ConfigKey * const ConfigKey::AVERAGING = &ConfigKey::_AVERAGING;
const ConfigKey * const ConfigKey::AVG_SAMPLES = &ConfigKey::_AVG_SAMPLES;
const ConfigKey * const ConfigKey::TRIGGER_SOURCE = &ConfigKey::_TRIGGER_SOURCE;
const ConfigKey * const ConfigKey::HORIZ_TRIGGERPOS = &ConfigKey::_HORIZ_TRIGGERPOS;
const ConfigKey * const ConfigKey::BUFFERSIZE = &ConfigKey::_BUFFERSIZE;
const ConfigKey * const ConfigKey::TIMEBASE = &ConfigKey::_TIMEBASE;
const ConfigKey * const ConfigKey::FILTER = &ConfigKey::_FILTER;
const ConfigKey * const ConfigKey::VDIV = &ConfigKey::_VDIV;
const ConfigKey * const ConfigKey::COUPLING = &ConfigKey::_COUPLING;
const ConfigKey * const ConfigKey::TRIGGER_MATCH = &ConfigKey::_TRIGGER_MATCH;
const ConfigKey * const ConfigKey::SAMPLE_INTERVAL = &ConfigKey::_SAMPLE_INTERVAL;
const ConfigKey * const ConfigKey::NUM_HDIV = &ConfigKey::_NUM_HDIV;
const ConfigKey * const ConfigKey::NUM_VDIV = &ConfigKey::_NUM_VDIV;
const ConfigKey * const ConfigKey::SPL_WEIGHT_FREQ = &ConfigKey::_SPL_WEIGHT_FREQ;
const ConfigKey * const ConfigKey::SPL_WEIGHT_TIME = &ConfigKey::_SPL_WEIGHT_TIME;
const ConfigKey * const ConfigKey::SPL_MEASUREMENT_RANGE = &ConfigKey::_SPL_MEASUREMENT_RANGE;
const ConfigKey * const ConfigKey::HOLD_MAX = &ConfigKey::_HOLD_MAX;
const ConfigKey * const ConfigKey::HOLD_MIN = &ConfigKey::_HOLD_MIN;
const ConfigKey * const ConfigKey::VOLTAGE_THRESHOLD = &ConfigKey::_VOLTAGE_THRESHOLD;
const ConfigKey * const ConfigKey::EXTERNAL_CLOCK = &ConfigKey::_EXTERNAL_CLOCK;
const ConfigKey * const ConfigKey::SWAP = &ConfigKey::_SWAP;
const ConfigKey * const ConfigKey::CENTER_FREQUENCY = &ConfigKey::_CENTER_FREQUENCY;
const ConfigKey * const ConfigKey::NUM_LOGIC_CHANNELS = &ConfigKey::_NUM_LOGIC_CHANNELS;
const ConfigKey * const ConfigKey::NUM_ANALOG_CHANNELS = &ConfigKey::_NUM_ANALOG_CHANNELS;
const ConfigKey * const ConfigKey::VOLTAGE = &ConfigKey::_VOLTAGE;
const ConfigKey * const ConfigKey::VOLTAGE_TARGET = &ConfigKey::_VOLTAGE_TARGET;
const ConfigKey * const ConfigKey::CURRENT = &ConfigKey::_CURRENT;
const ConfigKey * const ConfigKey::CURRENT_LIMIT = &ConfigKey::_CURRENT_LIMIT;
const ConfigKey * const ConfigKey::ENABLED = &ConfigKey::_ENABLED;
const ConfigKey * const ConfigKey::CHANNEL_CONFIG = &ConfigKey::_CHANNEL_CONFIG;
const ConfigKey * const ConfigKey::OVER_VOLTAGE_PROTECTION_ENABLED = &ConfigKey::_OVER_VOLTAGE_PROTECTION_ENABLED;
const ConfigKey * const ConfigKey::OVER_VOLTAGE_PROTECTION_ACTIVE = &ConfigKey::_OVER_VOLTAGE_PROTECTION_ACTIVE;
const ConfigKey * const ConfigKey::OVER_VOLTAGE_PROTECTION_THRESHOLD = &ConfigKey::_OVER_VOLTAGE_PROTECTION_THRESHOLD;
const ConfigKey * const ConfigKey::OVER_CURRENT_PROTECTION_ENABLED = &ConfigKey::_OVER_CURRENT_PROTECTION_ENABLED;
const ConfigKey * const ConfigKey::OVER_CURRENT_PROTECTION_ACTIVE = &ConfigKey::_OVER_CURRENT_PROTECTION_ACTIVE;
const ConfigKey * const ConfigKey::OVER_CURRENT_PROTECTION_THRESHOLD = &ConfigKey::_OVER_CURRENT_PROTECTION_THRESHOLD;
const ConfigKey * const ConfigKey::CLOCK_EDGE = &ConfigKey::_CLOCK_EDGE;
const ConfigKey * const ConfigKey::AMPLITUDE = &ConfigKey::_AMPLITUDE;
const ConfigKey * const ConfigKey::REGULATION = &ConfigKey::_REGULATION;
const ConfigKey * const ConfigKey::OVER_TEMPERATURE_PROTECTION = &ConfigKey::_OVER_TEMPERATURE_PROTECTION;
const ConfigKey * const ConfigKey::OUTPUT_FREQUENCY = &ConfigKey::_OUTPUT_FREQUENCY;
const ConfigKey * const ConfigKey::OUTPUT_FREQUENCY_TARGET = &ConfigKey::_OUTPUT_FREQUENCY_TARGET;
const ConfigKey * const ConfigKey::MEASURED_QUANTITY = &ConfigKey::_MEASURED_QUANTITY;
const ConfigKey * const ConfigKey::EQUIV_CIRCUIT_MODEL = &ConfigKey::_EQUIV_CIRCUIT_MODEL;
const ConfigKey * const ConfigKey::OVER_TEMPERATURE_PROTECTION_ACTIVE = &ConfigKey::_OVER_TEMPERATURE_PROTECTION_ACTIVE;
const ConfigKey * const ConfigKey::UNDER_VOLTAGE_CONDITION = &ConfigKey::_UNDER_VOLTAGE_CONDITION;
const ConfigKey * const ConfigKey::UNDER_VOLTAGE_CONDITION_ACTIVE = &ConfigKey::_UNDER_VOLTAGE_CONDITION_ACTIVE;
const ConfigKey * const ConfigKey::TRIGGER_LEVEL = &ConfigKey::_TRIGGER_LEVEL;
const ConfigKey * const ConfigKey::UNDER_VOLTAGE_CONDITION_THRESHOLD = &ConfigKey::_UNDER_VOLTAGE_CONDITION_THRESHOLD;
const ConfigKey * const ConfigKey::EXTERNAL_CLOCK_SOURCE = &ConfigKey::_EXTERNAL_CLOCK_SOURCE;
const ConfigKey * const ConfigKey::OFFSET = &ConfigKey::_OFFSET;
const ConfigKey * const ConfigKey::TRIGGER_PATTERN = &ConfigKey::_TRIGGER_PATTERN;
const ConfigKey * const ConfigKey::HIGH_RESOLUTION = &ConfigKey::_HIGH_RESOLUTION;
const ConfigKey * const ConfigKey::PEAK_DETECTION = &ConfigKey::_PEAK_DETECTION;
const ConfigKey * const ConfigKey::LOGIC_THRESHOLD = &ConfigKey::_LOGIC_THRESHOLD;
const ConfigKey * const ConfigKey::LOGIC_THRESHOLD_CUSTOM = &ConfigKey::_LOGIC_THRESHOLD_CUSTOM;
const ConfigKey * const ConfigKey::RANGE = &ConfigKey::_RANGE;
const ConfigKey * const ConfigKey::DIGITS = &ConfigKey::_DIGITS;
const ConfigKey * const ConfigKey::PHASE = &ConfigKey::_PHASE;
const ConfigKey * const ConfigKey::DUTY_CYCLE = &ConfigKey::_DUTY_CYCLE;
const ConfigKey * const ConfigKey::POWER = &ConfigKey::_POWER;
const ConfigKey * const ConfigKey::POWER_TARGET = &ConfigKey::_POWER_TARGET;
const ConfigKey * const ConfigKey::RESISTANCE_TARGET = &ConfigKey::_RESISTANCE_TARGET;
const ConfigKey * const ConfigKey::SESSIONFILE = &ConfigKey::_SESSIONFILE;
const ConfigKey * const ConfigKey::CAPTUREFILE = &ConfigKey::_CAPTUREFILE;
const ConfigKey * const ConfigKey::CAPTURE_UNITSIZE = &ConfigKey::_CAPTURE_UNITSIZE;
const ConfigKey * const ConfigKey::POWER_OFF = &ConfigKey::_POWER_OFF;
const ConfigKey * const ConfigKey::DATA_SOURCE = &ConfigKey::_DATA_SOURCE;
const ConfigKey * const ConfigKey::PROBE_FACTOR = &ConfigKey::_PROBE_FACTOR;
const ConfigKey * const ConfigKey::ADC_POWERLINE_CYCLES = &ConfigKey::_ADC_POWERLINE_CYCLES;
const ConfigKey * const ConfigKey::LIMIT_MSEC = &ConfigKey::_LIMIT_MSEC;
const ConfigKey * const ConfigKey::LIMIT_SAMPLES = &ConfigKey::_LIMIT_SAMPLES;
const ConfigKey * const ConfigKey::LIMIT_FRAMES = &ConfigKey::_LIMIT_FRAMES;
const ConfigKey * const ConfigKey::CONTINUOUS = &ConfigKey::_CONTINUOUS;
const ConfigKey * const ConfigKey::DATALOG = &ConfigKey::_DATALOG;
const ConfigKey * const ConfigKey::DEVICE_MODE = &ConfigKey::_DEVICE_MODE;
const ConfigKey * const ConfigKey::TEST_MODE = &ConfigKey::_TEST_MODE;
template<> const SR_API std::map<const enum sr_configkey, const ConfigKey * const> EnumValue<ConfigKey, enum sr_configkey>::_values = {
	{SR_CONF_LOGIC_ANALYZER, ConfigKey::LOGIC_ANALYZER},
	{SR_CONF_OSCILLOSCOPE, ConfigKey::OSCILLOSCOPE},
	{SR_CONF_MULTIMETER, ConfigKey::MULTIMETER},
	{SR_CONF_DEMO_DEV, ConfigKey::DEMO_DEV},
	{SR_CONF_SOUNDLEVELMETER, ConfigKey::SOUNDLEVELMETER},
	{SR_CONF_THERMOMETER, ConfigKey::THERMOMETER},
	{SR_CONF_HYGROMETER, ConfigKey::HYGROMETER},
	{SR_CONF_ENERGYMETER, ConfigKey::ENERGYMETER},
	{SR_CONF_DEMODULATOR, ConfigKey::DEMODULATOR},
	{SR_CONF_POWER_SUPPLY, ConfigKey::POWER_SUPPLY},
	{SR_CONF_LCRMETER, ConfigKey::LCRMETER},
	{SR_CONF_ELECTRONIC_LOAD, ConfigKey::ELECTRONIC_LOAD},
	{SR_CONF_SCALE, ConfigKey::SCALE},
	{SR_CONF_SIGNAL_GENERATOR, ConfigKey::SIGNAL_GENERATOR},
	{SR_CONF_POWERMETER, ConfigKey::POWERMETER},
	{SR_CONF_CONN, ConfigKey::CONN},
	{SR_CONF_SERIALCOMM, ConfigKey::SERIALCOMM},
	{SR_CONF_MODBUSADDR, ConfigKey::MODBUSADDR},
	{SR_CONF_FORCE_DETECT, ConfigKey::FORCE_DETECT},
	{SR_CONF_SAMPLERATE, ConfigKey::SAMPLERATE},
	{SR_CONF_CAPTURE_RATIO, ConfigKey::CAPTURE_RATIO},
	{SR_CONF_PATTERN_MODE, ConfigKey::PATTERN_MODE},
	{SR_CONF_RLE, ConfigKey::RLE},
	{SR_CONF_TRIGGER_SLOPE, ConfigKey::TRIGGER_SLOPE},
	{SR_CONF_AVERAGING, ConfigKey::AVERAGING},
	{SR_CONF_AVG_SAMPLES, ConfigKey::AVG_SAMPLES},
	{SR_CONF_TRIGGER_SOURCE, ConfigKey::TRIGGER_SOURCE},
	{SR_CONF_HORIZ_TRIGGERPOS, ConfigKey::HORIZ_TRIGGERPOS},
	{SR_CONF_BUFFERSIZE, ConfigKey::BUFFERSIZE},
	{SR_CONF_TIMEBASE, ConfigKey::TIMEBASE},
	{SR_CONF_FILTER, ConfigKey::FILTER},
	{SR_CONF_VDIV, ConfigKey::VDIV},
	{SR_CONF_COUPLING, ConfigKey::COUPLING},
	{SR_CONF_TRIGGER_MATCH, ConfigKey::TRIGGER_MATCH},
	{SR_CONF_SAMPLE_INTERVAL, ConfigKey::SAMPLE_INTERVAL},
	{SR_CONF_NUM_HDIV, ConfigKey::NUM_HDIV},
	{SR_CONF_NUM_VDIV, ConfigKey::NUM_VDIV},
	{SR_CONF_SPL_WEIGHT_FREQ, ConfigKey::SPL_WEIGHT_FREQ},
	{SR_CONF_SPL_WEIGHT_TIME, ConfigKey::SPL_WEIGHT_TIME},
	{SR_CONF_SPL_MEASUREMENT_RANGE, ConfigKey::SPL_MEASUREMENT_RANGE},
	{SR_CONF_HOLD_MAX, ConfigKey::HOLD_MAX},
	{SR_CONF_HOLD_MIN, ConfigKey::HOLD_MIN},
	{SR_CONF_VOLTAGE_THRESHOLD, ConfigKey::VOLTAGE_THRESHOLD},
	{SR_CONF_EXTERNAL_CLOCK, ConfigKey::EXTERNAL_CLOCK},
	{SR_CONF_SWAP, ConfigKey::SWAP},
	{SR_CONF_CENTER_FREQUENCY, ConfigKey::CENTER_FREQUENCY},
	{SR_CONF_NUM_LOGIC_CHANNELS, ConfigKey::NUM_LOGIC_CHANNELS},
	{SR_CONF_NUM_ANALOG_CHANNELS, ConfigKey::NUM_ANALOG_CHANNELS},
	{SR_CONF_VOLTAGE, ConfigKey::VOLTAGE},
	{SR_CONF_VOLTAGE_TARGET, ConfigKey::VOLTAGE_TARGET},
	{SR_CONF_CURRENT, ConfigKey::CURRENT},
	{SR_CONF_CURRENT_LIMIT, ConfigKey::CURRENT_LIMIT},
	{SR_CONF_ENABLED, ConfigKey::ENABLED},
	{SR_CONF_CHANNEL_CONFIG, ConfigKey::CHANNEL_CONFIG},
	{SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED, ConfigKey::OVER_VOLTAGE_PROTECTION_ENABLED},
	{SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE, ConfigKey::OVER_VOLTAGE_PROTECTION_ACTIVE},
	{SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD, ConfigKey::OVER_VOLTAGE_PROTECTION_THRESHOLD},
	{SR_CONF_OVER_CURRENT_PROTECTION_ENABLED, ConfigKey::OVER_CURRENT_PROTECTION_ENABLED},
	{SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE, ConfigKey::OVER_CURRENT_PROTECTION_ACTIVE},
	{SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD, ConfigKey::OVER_CURRENT_PROTECTION_THRESHOLD},
	{SR_CONF_CLOCK_EDGE, ConfigKey::CLOCK_EDGE},
	{SR_CONF_AMPLITUDE, ConfigKey::AMPLITUDE},
	{SR_CONF_REGULATION, ConfigKey::REGULATION},
	{SR_CONF_OVER_TEMPERATURE_PROTECTION, ConfigKey::OVER_TEMPERATURE_PROTECTION},
	{SR_CONF_OUTPUT_FREQUENCY, ConfigKey::OUTPUT_FREQUENCY},
	{SR_CONF_OUTPUT_FREQUENCY_TARGET, ConfigKey::OUTPUT_FREQUENCY_TARGET},
	{SR_CONF_MEASURED_QUANTITY, ConfigKey::MEASURED_QUANTITY},
	{SR_CONF_EQUIV_CIRCUIT_MODEL, ConfigKey::EQUIV_CIRCUIT_MODEL},
	{SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE, ConfigKey::OVER_TEMPERATURE_PROTECTION_ACTIVE},
	{SR_CONF_UNDER_VOLTAGE_CONDITION, ConfigKey::UNDER_VOLTAGE_CONDITION},
	{SR_CONF_UNDER_VOLTAGE_CONDITION_ACTIVE, ConfigKey::UNDER_VOLTAGE_CONDITION_ACTIVE},
	{SR_CONF_TRIGGER_LEVEL, ConfigKey::TRIGGER_LEVEL},
	{SR_CONF_UNDER_VOLTAGE_CONDITION_THRESHOLD, ConfigKey::UNDER_VOLTAGE_CONDITION_THRESHOLD},
	{SR_CONF_EXTERNAL_CLOCK_SOURCE, ConfigKey::EXTERNAL_CLOCK_SOURCE},
	{SR_CONF_OFFSET, ConfigKey::OFFSET},
	{SR_CONF_TRIGGER_PATTERN, ConfigKey::TRIGGER_PATTERN},
	{SR_CONF_HIGH_RESOLUTION, ConfigKey::HIGH_RESOLUTION},
	{SR_CONF_PEAK_DETECTION, ConfigKey::PEAK_DETECTION},
	{SR_CONF_LOGIC_THRESHOLD, ConfigKey::LOGIC_THRESHOLD},
	{SR_CONF_LOGIC_THRESHOLD_CUSTOM, ConfigKey::LOGIC_THRESHOLD_CUSTOM},
	{SR_CONF_RANGE, ConfigKey::RANGE},
	{SR_CONF_DIGITS, ConfigKey::DIGITS},
	{SR_CONF_PHASE, ConfigKey::PHASE},
	{SR_CONF_DUTY_CYCLE, ConfigKey::DUTY_CYCLE},
	{SR_CONF_POWER, ConfigKey::POWER},
	{SR_CONF_POWER_TARGET, ConfigKey::POWER_TARGET},
	{SR_CONF_RESISTANCE_TARGET, ConfigKey::RESISTANCE_TARGET},
	{SR_CONF_SESSIONFILE, ConfigKey::SESSIONFILE},
	{SR_CONF_CAPTUREFILE, ConfigKey::CAPTUREFILE},
	{SR_CONF_CAPTURE_UNITSIZE, ConfigKey::CAPTURE_UNITSIZE},
	{SR_CONF_POWER_OFF, ConfigKey::POWER_OFF},
	{SR_CONF_DATA_SOURCE, ConfigKey::DATA_SOURCE},
	{SR_CONF_PROBE_FACTOR, ConfigKey::PROBE_FACTOR},
	{SR_CONF_ADC_POWERLINE_CYCLES, ConfigKey::ADC_POWERLINE_CYCLES},
	{SR_CONF_LIMIT_MSEC, ConfigKey::LIMIT_MSEC},
	{SR_CONF_LIMIT_SAMPLES, ConfigKey::LIMIT_SAMPLES},
	{SR_CONF_LIMIT_FRAMES, ConfigKey::LIMIT_FRAMES},
	{SR_CONF_CONTINUOUS, ConfigKey::CONTINUOUS},
	{SR_CONF_DATALOG, ConfigKey::DATALOG},
	{SR_CONF_DEVICE_MODE, ConfigKey::DEVICE_MODE},
	{SR_CONF_TEST_MODE, ConfigKey::TEST_MODE},
};
#include <config.h>

const DataType *ConfigKey::data_type() const
{
	const struct sr_key_info *info = sr_key_info_get(SR_KEY_CONFIG, id());
	if (!info)
		throw Error(SR_ERR_NA);
	return DataType::get(info->datatype);
}

std::string ConfigKey::identifier() const
{
	const struct sr_key_info *info = sr_key_info_get(SR_KEY_CONFIG, id());
	if (!info)
		throw Error(SR_ERR_NA);
	return valid_string(info->id);
}

std::string ConfigKey::description() const
{
	const struct sr_key_info *info = sr_key_info_get(SR_KEY_CONFIG, id());
	if (!info)
		throw Error(SR_ERR_NA);
	return valid_string(info->name);
}

const ConfigKey *ConfigKey::get_by_identifier(std::string identifier)
{
	const struct sr_key_info *info = sr_key_info_name_get(SR_KEY_CONFIG, identifier.c_str());
	if (!info)
		throw Error(SR_ERR_ARG);
	return get(info->key);
}

#ifndef HAVE_STOI_STOD

/* Fallback implementation of stoi and stod */

#include <cstdlib>
#include <cerrno>
#include <stdexcept>
#include <limits>

static inline int stoi( const std::string& str )
{
	char *endptr;
	errno = 0;
	const long ret = std::strtol(str.c_str(), &endptr, 10);
	if (endptr == str.c_str())
		throw std::invalid_argument("stoi");
	else if (errno == ERANGE ||
		 ret < std::numeric_limits<int>::min() ||
		 ret > std::numeric_limits<int>::max())
		throw std::out_of_range("stoi");
	else
		return ret;
}

static inline double stod( const std::string& str )
{
	char *endptr;
	errno = 0;
	const double ret = std::strtod(str.c_str(), &endptr);
	if (endptr == str.c_str())
		throw std::invalid_argument("stod");
	else if (errno == ERANGE)
		throw std::out_of_range("stod");
	else
		return ret;
}
#endif

Glib::VariantBase ConfigKey::parse_string(std::string value, enum sr_datatype dt)
{
	GVariant *variant;
	uint64_t p, q;

	switch (dt)
	{
		case SR_T_UINT64:
			check(sr_parse_sizestring(value.c_str(), &p));
			variant = g_variant_new_uint64(p);
			break;
		case SR_T_STRING:
			variant = g_variant_new_string(value.c_str());
			break;
		case SR_T_BOOL:
			variant = g_variant_new_boolean(sr_parse_boolstring(value.c_str()));
			break;
		case SR_T_FLOAT:
			try {
				variant = g_variant_new_double(stod(value));
			} catch (invalid_argument&) {
				throw Error(SR_ERR_ARG);
			}
			break;
		case SR_T_RATIONAL_PERIOD:
			check(sr_parse_period(value.c_str(), &p, &q));
			variant = g_variant_new("(tt)", p, q);
			break;
		case SR_T_RATIONAL_VOLT:
			check(sr_parse_voltage(value.c_str(), &p, &q));
			variant = g_variant_new("(tt)", p, q);
			break;
		case SR_T_INT32:
			try {
				variant = g_variant_new_int32(stoi(value));
			} catch (invalid_argument&) {
				throw Error(SR_ERR_ARG);
			}
			break;
		default:
			throw Error(SR_ERR_BUG);
	}

	return Glib::VariantBase(variant, false);
}

Glib::VariantBase ConfigKey::parse_string(std::string value) const
{
	enum sr_datatype dt = (enum sr_datatype)(data_type()->id());
	return parse_string(value, dt);
}

