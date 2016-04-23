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

#include <config.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <glib.h>
#include <libsigrok/libsigrok.h>
#include "libsigrok-internal.h"

/** @cond PRIVATE */
#define LOG_PREFIX "hwdriver"
/** @endcond */

/**
 * @file
 *
 * Hardware driver handling in libsigrok.
 */

/**
 * @defgroup grp_driver Hardware drivers
 *
 * Hardware driver handling in libsigrok.
 *
 * @{
 */

/* Please use the same order/grouping as in enum sr_configkey (libsigrok.h). */
static struct sr_key_info sr_key_info_config[] = {
	/* Device classes */
	{SR_CONF_LOGIC_ANALYZER, SR_T_STRING, NULL, "Logic analyzer", NULL},
	{SR_CONF_OSCILLOSCOPE, SR_T_STRING, NULL, "Oscilloscope", NULL},
	{SR_CONF_MULTIMETER, SR_T_STRING, NULL, "Multimeter", NULL},
	{SR_CONF_DEMO_DEV, SR_T_STRING, NULL, "Demo device", NULL},
	{SR_CONF_SOUNDLEVELMETER, SR_T_STRING, NULL, "Sound level meter", NULL},
	{SR_CONF_THERMOMETER, SR_T_STRING, NULL, "Thermometer", NULL},
	{SR_CONF_HYGROMETER, SR_T_STRING, NULL, "Hygrometer", NULL},
	{SR_CONF_ENERGYMETER, SR_T_STRING, NULL, "Energy meter", NULL},
	{SR_CONF_DEMODULATOR, SR_T_STRING, NULL, "Demodulator", NULL},
	{SR_CONF_POWER_SUPPLY, SR_T_STRING, NULL, "Power supply", NULL},
	{SR_CONF_LCRMETER, SR_T_STRING, NULL, "LCR meter", NULL},
	{SR_CONF_ELECTRONIC_LOAD, SR_T_STRING, NULL, "Electronic load", NULL},
	{SR_CONF_SCALE, SR_T_STRING, NULL, "Scale", NULL},

	/* Driver scan options */
	{SR_CONF_CONN, SR_T_STRING, "conn",
		"Connection", NULL},
	{SR_CONF_SERIALCOMM, SR_T_STRING, "serialcomm",
		"Serial communication", NULL},
	{SR_CONF_MODBUSADDR, SR_T_UINT64, "modbusaddr",
		"Modbus slave address", NULL},

	/* Device (or channel group) configuration */
	{SR_CONF_SAMPLERATE, SR_T_UINT64, "samplerate",
		"Sample rate", NULL},
	{SR_CONF_CAPTURE_RATIO, SR_T_UINT64, "captureratio",
		"Pre-trigger capture ratio", NULL},
	{SR_CONF_PATTERN_MODE, SR_T_STRING, "pattern",
		"Pattern", NULL},
	{SR_CONF_RLE, SR_T_BOOL, "rle",
		"Run length encoding", NULL},
	{SR_CONF_TRIGGER_SLOPE, SR_T_STRING, "triggerslope",
		"Trigger slope", NULL},
	{SR_CONF_AVERAGING, SR_T_BOOL, "averaging",
		"Averaging", NULL},
	{SR_CONF_AVG_SAMPLES, SR_T_UINT64, "avg_samples",
		"Number of samples to average over", NULL},
	{SR_CONF_TRIGGER_SOURCE, SR_T_STRING, "triggersource",
		"Trigger source", NULL},
	{SR_CONF_HORIZ_TRIGGERPOS, SR_T_FLOAT, "horiz_triggerpos",
		"Horizontal trigger position", NULL},
	{SR_CONF_BUFFERSIZE, SR_T_UINT64, "buffersize",
		"Buffer size", NULL},
	{SR_CONF_TIMEBASE, SR_T_RATIONAL_PERIOD, "timebase",
		"Time base", NULL},
	{SR_CONF_FILTER, SR_T_BOOL, "filter",
		"Filter", NULL},
	{SR_CONF_VDIV, SR_T_RATIONAL_VOLT, "vdiv",
		"Volts/div", NULL},
	{SR_CONF_COUPLING, SR_T_STRING, "coupling",
		"Coupling", NULL},
	{SR_CONF_TRIGGER_MATCH, SR_T_INT32, "triggermatch",
		"Trigger matches", NULL},
	{SR_CONF_SAMPLE_INTERVAL, SR_T_UINT64, "sample_interval",
		"Sample interval", NULL},
	{SR_CONF_NUM_HDIV, SR_T_INT32, "num_hdiv",
		"Number of horizontal divisions", NULL},
	{SR_CONF_NUM_VDIV, SR_T_INT32, "num_vdiv",
		"Number of vertical divisions", NULL},
	{SR_CONF_SPL_WEIGHT_FREQ, SR_T_STRING, "spl_weight_freq",
		"Sound pressure level frequency weighting", NULL},
	{SR_CONF_SPL_WEIGHT_TIME, SR_T_STRING, "spl_weight_time",
		"Sound pressure level time weighting", NULL},
	{SR_CONF_SPL_MEASUREMENT_RANGE, SR_T_UINT64_RANGE, "spl_meas_range",
		"Sound pressure level measurement range", NULL},
	{SR_CONF_HOLD_MAX, SR_T_BOOL, "hold_max",
		"Hold max", NULL},
	{SR_CONF_HOLD_MIN, SR_T_BOOL, "hold_min",
		"Hold min", NULL},
	{SR_CONF_VOLTAGE_THRESHOLD, SR_T_DOUBLE_RANGE, "voltage_threshold",
		"Voltage threshold", NULL },
	{SR_CONF_EXTERNAL_CLOCK, SR_T_BOOL, "external_clock",
		"External clock mode", NULL},
	{SR_CONF_SWAP, SR_T_BOOL, "swap",
		"Swap channel order", NULL},
	{SR_CONF_CENTER_FREQUENCY, SR_T_UINT64, "center_frequency",
		"Center frequency", NULL},
	{SR_CONF_NUM_LOGIC_CHANNELS, SR_T_INT32, "logic_channels",
		"Number of logic channels", NULL},
	{SR_CONF_NUM_ANALOG_CHANNELS, SR_T_INT32, "analog_channels",
		"Number of analog channels", NULL},
	{SR_CONF_VOLTAGE, SR_T_FLOAT, "voltage",
		"Current voltage", NULL},
	{SR_CONF_VOLTAGE_TARGET, SR_T_FLOAT, "voltage_target",
		"Voltage target", NULL},
	{SR_CONF_CURRENT, SR_T_FLOAT, "current",
		"Current current", NULL},
	{SR_CONF_CURRENT_LIMIT, SR_T_FLOAT, "current_limit",
		"Current limit", NULL},
	{SR_CONF_ENABLED, SR_T_BOOL, "enabled",
		"Channel enabled", NULL},
	{SR_CONF_CHANNEL_CONFIG, SR_T_STRING, "channel_config",
		"Channel modes", NULL},
	{SR_CONF_OVER_VOLTAGE_PROTECTION_ENABLED, SR_T_BOOL, "ovp_enabled",
		"Over-voltage protection enabled", NULL},
	{SR_CONF_OVER_VOLTAGE_PROTECTION_ACTIVE, SR_T_BOOL, "ovp_active",
		"Over-voltage protection active", NULL},
	{SR_CONF_OVER_VOLTAGE_PROTECTION_THRESHOLD, SR_T_FLOAT, "ovp_threshold",
		"Over-voltage protection threshold", NULL},
	{SR_CONF_OVER_CURRENT_PROTECTION_ENABLED, SR_T_BOOL, "ocp_enabled",
		"Over-current protection enabled", NULL},
	{SR_CONF_OVER_CURRENT_PROTECTION_ACTIVE, SR_T_BOOL, "ocp_active",
		"Over-current protection active", NULL},
	{SR_CONF_OVER_CURRENT_PROTECTION_THRESHOLD, SR_T_FLOAT, "ocp_threshold",
		"Over-current protection threshold", NULL},
	{SR_CONF_CLOCK_EDGE, SR_T_STRING, "clock_edge",
		"Clock edge", NULL},
	{SR_CONF_AMPLITUDE, SR_T_FLOAT, "amplitude",
		"Amplitude", NULL},
	{SR_CONF_REGULATION, SR_T_STRING, "regulation",
		"Channel regulation", NULL},
	{SR_CONF_OVER_TEMPERATURE_PROTECTION, SR_T_BOOL, "otp",
		"Over-temperature protection", NULL},
	{SR_CONF_OUTPUT_FREQUENCY, SR_T_FLOAT, "output_frequency",
		"Output frequency", NULL},
	{SR_CONF_OUTPUT_FREQUENCY_TARGET, SR_T_FLOAT, "output_frequency_target",
		"Output frequency target", NULL},
	{SR_CONF_MEASURED_QUANTITY, SR_T_MQ, "measured_quantity",
		"Measured quantity", NULL},
	{SR_CONF_EQUIV_CIRCUIT_MODEL, SR_T_STRING, "equiv_circuit_model",
		"Equivalent circuit model", NULL},
	{SR_CONF_OVER_TEMPERATURE_PROTECTION_ACTIVE, SR_T_BOOL, "otp_active",
		"Over-temperature protection active", NULL},
	{SR_CONF_UNDER_VOLTAGE_CONDITION, SR_T_BOOL, "uvc",
		"Under-voltage condition", NULL},
	{SR_CONF_UNDER_VOLTAGE_CONDITION_ACTIVE, SR_T_BOOL, "uvc_active",
		"Under-voltage condition active", NULL},

	/* Special stuff */
	{SR_CONF_SESSIONFILE, SR_T_STRING, "sessionfile",
		"Session file", NULL},
	{SR_CONF_CAPTUREFILE, SR_T_STRING, "capturefile",
		"Capture file", NULL},
	{SR_CONF_CAPTURE_UNITSIZE, SR_T_UINT64, "capture_unitsize",
		"Capture unitsize", NULL},
	{SR_CONF_POWER_OFF, SR_T_BOOL, "power_off",
		"Power off", NULL},
	{SR_CONF_DATA_SOURCE, SR_T_STRING, "data_source",
		"Data source", NULL},
	{SR_CONF_PROBE_FACTOR, SR_T_UINT64, "probe_factor",
		"Probe factor", NULL},
	{SR_CONF_ADC_POWERLINE_CYCLES, SR_T_FLOAT, "nplc",
		"Number of ADC powerline cycles", NULL},

	/* Acquisition modes, sample limiting */
	{SR_CONF_LIMIT_MSEC, SR_T_UINT64, "limit_time",
		"Time limit", NULL},
	{SR_CONF_LIMIT_SAMPLES, SR_T_UINT64, "limit_samples",
		"Sample limit", NULL},
	{SR_CONF_LIMIT_FRAMES, SR_T_UINT64, "limit_frames",
		"Frame limit", NULL},
	{SR_CONF_CONTINUOUS, SR_T_UINT64, "continuous",
		"Continuous sampling", NULL},
	{SR_CONF_DATALOG, SR_T_BOOL, "datalog",
		"Datalog", NULL},
	{SR_CONF_DEVICE_MODE, SR_T_STRING, "device_mode",
		"Device mode", NULL},
	{SR_CONF_TEST_MODE, SR_T_STRING, "test_mode",
		"Test mode", NULL},

	ALL_ZERO
};

/* Please use the same order as in enum sr_mq (libsigrok.h). */
static struct sr_key_info sr_key_info_mq[] = {
	{SR_MQ_VOLTAGE, 0, "voltage", "Voltage", NULL},
	{SR_MQ_CURRENT, 0, "current", "Current", NULL},
	{SR_MQ_RESISTANCE, 0, "resistance", "Resistance", NULL},
	{SR_MQ_CAPACITANCE, 0, "capacitance", "Capacitance", NULL},
	{SR_MQ_TEMPERATURE, 0, "temperature", "Temperature", NULL},
	{SR_MQ_FREQUENCY, 0, "frequency", "Frequency", NULL},
	{SR_MQ_DUTY_CYCLE, 0, "duty_cycle", "Duty cycle", NULL},
	{SR_MQ_CONTINUITY, 0, "continuity", "Continuity", NULL},
	{SR_MQ_PULSE_WIDTH, 0, "pulse_width", "Pulse width", NULL},
	{SR_MQ_CONDUCTANCE, 0, "conductance", "Conductance", NULL},
	{SR_MQ_POWER, 0, "power", "Power", NULL},
	{SR_MQ_GAIN, 0, "gain", "Gain", NULL},
	{SR_MQ_SOUND_PRESSURE_LEVEL, 0, "spl", "Sound pressure level", NULL},
	{SR_MQ_CARBON_MONOXIDE, 0, "co", "Carbon monoxide", NULL},
	{SR_MQ_RELATIVE_HUMIDITY, 0, "rh", "Relative humidity", NULL},
	{SR_MQ_TIME, 0, "time", "Time", NULL},
	{SR_MQ_WIND_SPEED, 0, "wind_speed", "Wind speed", NULL},
	{SR_MQ_PRESSURE, 0, "pressure", "Pressure", NULL},
	{SR_MQ_PARALLEL_INDUCTANCE, 0, "parallel_inductance", "Parallel inductance", NULL},
	{SR_MQ_PARALLEL_CAPACITANCE, 0, "parallel_capacitance", "Parallel capacitance", NULL},
	{SR_MQ_PARALLEL_RESISTANCE, 0, "parallel_resistance", "Parallel resistance", NULL},
	{SR_MQ_SERIES_INDUCTANCE, 0, "series_inductance", "Series inductance", NULL},
	{SR_MQ_SERIES_CAPACITANCE, 0, "series_capacitance", "Series capacitance", NULL},
	{SR_MQ_SERIES_RESISTANCE, 0, "series_resistance", "Series resistance", NULL},
	{SR_MQ_DISSIPATION_FACTOR, 0, "dissipation_factor", "Dissipation factor", NULL},
	{SR_MQ_QUALITY_FACTOR, 0, "quality_factor", "Quality factor", NULL},
	{SR_MQ_PHASE_ANGLE, 0, "phase_angle", "Phase angle", NULL},
	{SR_MQ_DIFFERENCE, 0, "difference", "Difference", NULL},
	{SR_MQ_COUNT, 0, "count", "Count", NULL},
	{SR_MQ_POWER_FACTOR, 0, "power_factor", "Power factor", NULL},
	{SR_MQ_APPARENT_POWER, 0, "apparent_power", "Apparent power", NULL},
	{SR_MQ_MASS, 0, "mass", "Mass", NULL},
	ALL_ZERO
};

/* Please use the same order as in enum sr_mqflag (libsigrok.h). */
static struct sr_key_info sr_key_info_mqflag[] = {
	{SR_MQFLAG_AC, 0, "ac", "AC", NULL},
	{SR_MQFLAG_DC, 0, "dc", "DC", NULL},
	{SR_MQFLAG_RMS, 0, "rms", "RMS", NULL},
	{SR_MQFLAG_DIODE, 0, "diode", "Diode", NULL},
	{SR_MQFLAG_HOLD, 0, "hold", "Hold", NULL},
	{SR_MQFLAG_MAX, 0, "max", "Max", NULL},
	{SR_MQFLAG_MIN, 0, "min", "Min", NULL},
	{SR_MQFLAG_AUTORANGE, 0, "auto_range", "Auto range", NULL},
	{SR_MQFLAG_RELATIVE, 0, "relative", "Relative", NULL},
	{SR_MQFLAG_SPL_FREQ_WEIGHT_A, 0, "spl_freq_weight_a",
		"Frequency weighted (A)", NULL},
	{SR_MQFLAG_SPL_FREQ_WEIGHT_C, 0, "spl_freq_weight_c",
		"Frequency weighted (C)", NULL},
	{SR_MQFLAG_SPL_FREQ_WEIGHT_Z, 0, "spl_freq_weight_z",
		"Frequency weighted (Z)", NULL},
	{SR_MQFLAG_SPL_FREQ_WEIGHT_FLAT, 0, "spl_freq_weight_flat",
		"Frequency weighted (flat)", NULL},
	{SR_MQFLAG_SPL_TIME_WEIGHT_S, 0, "spl_time_weight_s",
		"Time weighted (S)", NULL},
	{SR_MQFLAG_SPL_TIME_WEIGHT_F, 0, "spl_time_weight_f",
		"Time weighted (F)", NULL},
	{SR_MQFLAG_SPL_LAT, 0, "spl_time_average", "Time-averaged (LEQ)", NULL},
	{SR_MQFLAG_SPL_PCT_OVER_ALARM, 0, "spl_pct_over_alarm",
		"Percentage over alarm", NULL},
	{SR_MQFLAG_DURATION, 0, "duration", "Duration", NULL},
	{SR_MQFLAG_AVG, 0, "average", "Average", NULL},
	{SR_MQFLAG_REFERENCE, 0, "reference", "Reference", NULL},
	{SR_MQFLAG_UNSTABLE, 0, "unstable", "Unstable", NULL},
	{SR_MQFLAG_FOUR_WIRE, 0, "four_wire", "4-Wire", NULL},
	ALL_ZERO
};

/* This must handle all the keys from enum sr_datatype (libsigrok.h). */
SR_PRIV const GVariantType *sr_variant_type_get(int datatype)
{
	switch (datatype) {
	case SR_T_INT32:
		return G_VARIANT_TYPE_INT32;
	case SR_T_UINT64:
		return G_VARIANT_TYPE_UINT64;
	case SR_T_STRING:
		return G_VARIANT_TYPE_STRING;
	case SR_T_BOOL:
		return G_VARIANT_TYPE_BOOLEAN;
	case SR_T_FLOAT:
		return G_VARIANT_TYPE_DOUBLE;
	case SR_T_RATIONAL_PERIOD:
	case SR_T_RATIONAL_VOLT:
	case SR_T_UINT64_RANGE:
	case SR_T_DOUBLE_RANGE:
		return G_VARIANT_TYPE_TUPLE;
	case SR_T_KEYVALUE:
		return G_VARIANT_TYPE_DICTIONARY;
	case SR_T_MQ:
		return G_VARIANT_TYPE_TUPLE;
	default:
		return NULL;
	}
}

SR_PRIV int sr_variant_type_check(uint32_t key, GVariant *value)
{
	const struct sr_key_info *info;
	const GVariantType *type, *expected;
	char *expected_string, *type_string;

	info = sr_key_info_get(SR_KEY_CONFIG, key);
	if (!info)
		return SR_OK;

	expected = sr_variant_type_get(info->datatype);
	type = g_variant_get_type(value);
	if (!g_variant_type_equal(type, expected)
			&& !g_variant_type_is_subtype_of(type, expected)) {
		expected_string = g_variant_type_dup_string(expected);
		type_string = g_variant_type_dup_string(type);
		sr_err("Wrong variant type for key '%s': expected '%s', got '%s'",
			info->name, expected_string, type_string);
		g_free(expected_string);
		g_free(type_string);
		return SR_ERR_ARG;
	}

	return SR_OK;
}

/**
 * Return the list of supported hardware drivers.
 *
 * @param[in] ctx Pointer to a libsigrok context struct. Must not be NULL.
 *
 * @retval NULL The ctx argument was NULL, or there are no supported drivers.
 * @retval Other Pointer to the NULL-terminated list of hardware drivers.
 *               The user should NOT g_free() this list, sr_exit() will do that.
 *
 * @since 0.4.0
 */
SR_API struct sr_dev_driver **sr_driver_list(const struct sr_context *ctx)
{
	if (!ctx)
		return NULL;

	return ctx->driver_list;
}

/**
 * Initialize a hardware driver.
 *
 * This usually involves memory allocations and variable initializations
 * within the driver, but _not_ scanning for attached devices.
 * The API call sr_driver_scan() is used for that.
 *
 * @param ctx A libsigrok context object allocated by a previous call to
 *            sr_init(). Must not be NULL.
 * @param driver The driver to initialize. This must be a pointer to one of
 *               the entries returned by sr_driver_list(). Must not be NULL.
 *
 * @retval SR_OK Success
 * @retval SR_ERR_ARG Invalid parameter(s).
 * @retval SR_ERR_BUG Internal errors.
 * @retval other Another negative error code upon other errors.
 *
 * @since 0.2.0
 */
SR_API int sr_driver_init(struct sr_context *ctx, struct sr_dev_driver *driver)
{
	int ret;

	if (!ctx) {
		sr_err("Invalid libsigrok context, can't initialize.");
		return SR_ERR_ARG;
	}

	if (!driver) {
		sr_err("Invalid driver, can't initialize.");
		return SR_ERR_ARG;
	}

	sr_spew("Initializing driver '%s'.", driver->name);
	if ((ret = driver->init(driver, ctx)) < 0)
		sr_err("Failed to initialize the driver: %d.", ret);

	return ret;
}

/**
 * Enumerate scan options supported by this driver.
 *
 * Before calling sr_driver_scan_options_list(), the user must have previously
 * initialized the driver by calling sr_driver_init().
 *
 * @param driver The driver to enumerate options for. This must be a pointer
 *               to one of the entries returned by sr_driver_list(). Must not
 *               be NULL.
 *
 * @return A GArray * of uint32_t entries, or NULL on invalid arguments. Each
 *         entry is a configuration key that is supported as a scan option.
 *         The array must be freed by the caller using g_array_free().
 *
 * @since 0.4.0
 */
SR_API GArray *sr_driver_scan_options_list(const struct sr_dev_driver *driver)
{
	GVariant *gvar;
	const uint32_t *opts;
	gsize num_opts;
	GArray *result;

	if (sr_config_list(driver, NULL, NULL, SR_CONF_SCAN_OPTIONS, &gvar) != SR_OK)
		return NULL;

	opts = g_variant_get_fixed_array(gvar, &num_opts, sizeof(uint32_t));

	result = g_array_sized_new(FALSE, FALSE, sizeof(uint32_t), num_opts);

	g_array_insert_vals(result, 0, opts, num_opts);

	g_variant_unref(gvar);

	return result;
}

static int check_options(struct sr_dev_driver *driver, GSList *options,
		uint32_t optlist_key, struct sr_dev_inst *sdi,
		struct sr_channel_group *cg)
{
	struct sr_config *src;
	const struct sr_key_info *srci;
	GVariant *gvar_opts;
	GSList *l;
	const uint32_t *opts;
	gsize num_opts, i;
	int ret;

	if (sr_config_list(driver, sdi, cg, optlist_key, &gvar_opts) != SR_OK) {
		/* Driver publishes no options for this optlist. */
		return SR_ERR;
	}

	ret = SR_OK;
	opts = g_variant_get_fixed_array(gvar_opts, &num_opts, sizeof(uint32_t));
	for (l = options; l; l = l->next) {
		src = l->data;
		for (i = 0; i < num_opts; i++) {
			if (opts[i] == src->key)
				break;
		}
		if (i == num_opts) {
			if (!(srci = sr_key_info_get(SR_KEY_CONFIG, src->key)))
				/* Shouldn't happen. */
				sr_err("Invalid option %d.", src->key);
			else
				sr_err("Invalid option '%s'.", srci->id);
			ret = SR_ERR_ARG;
			break;
		}
		if (sr_variant_type_check(src->key, src->data) != SR_OK) {
			ret = SR_ERR_ARG;
			break;
		}
	}
	g_variant_unref(gvar_opts);

	return ret;
}

/**
 * Tell a hardware driver to scan for devices.
 *
 * In addition to the detection, the devices that are found are also
 * initialized automatically. On some devices, this involves a firmware upload,
 * or other such measures.
 *
 * The order in which the system is scanned for devices is not specified. The
 * caller should not assume or rely on any specific order.
 *
 * Before calling sr_driver_scan(), the user must have previously initialized
 * the driver by calling sr_driver_init().
 *
 * @param driver The driver that should scan. This must be a pointer to one of
 *               the entries returned by sr_driver_list(). Must not be NULL.
 * @param options A list of 'struct sr_hwopt' options to pass to the driver's
 *                scanner. Can be NULL/empty.
 *
 * @return A GSList * of 'struct sr_dev_inst', or NULL if no devices were
 *         found (or errors were encountered). This list must be freed by the
 *         caller using g_slist_free(), but without freeing the data pointed
 *         to in the list.
 *
 * @since 0.2.0
 */
SR_API GSList *sr_driver_scan(struct sr_dev_driver *driver, GSList *options)
{
	GSList *l;

	if (!driver) {
		sr_err("Invalid driver, can't scan for devices.");
		return NULL;
	}

	if (!driver->context) {
		sr_err("Driver not initialized, can't scan for devices.");
		return NULL;
	}

	if (options) {
		if (check_options(driver, options, SR_CONF_SCAN_OPTIONS, NULL, NULL) != SR_OK)
			return NULL;
	}

	l = driver->scan(driver, options);

	sr_spew("Scan of '%s' found %d devices.", driver->name,
		g_slist_length(l));

	return l;
}

/**
 * Call driver cleanup function for all drivers.
 *
 * @param[in] ctx Pointer to a libsigrok context struct. Must not be NULL.
 *
 * @private
 */
SR_PRIV void sr_hw_cleanup_all(const struct sr_context *ctx)
{
	int i;
	struct sr_dev_driver **drivers;

	if (!ctx)
		return;

	drivers = sr_driver_list(ctx);
	for (i = 0; drivers[i]; i++) {
		if (drivers[i]->cleanup)
			drivers[i]->cleanup(drivers[i]);
		drivers[i]->context = NULL;
	}
}

/** Allocate struct sr_config.
 *  A floating reference can be passed in for data.
 *  @private
 */
SR_PRIV struct sr_config *sr_config_new(uint32_t key, GVariant *data)
{
	struct sr_config *src;

	src = g_malloc0(sizeof(struct sr_config));
	src->key = key;
	src->data = g_variant_ref_sink(data);

	return src;
}

/** Free struct sr_config.
 *  @private
 */
SR_PRIV void sr_config_free(struct sr_config *src)
{

	if (!src || !src->data) {
		sr_err("%s: invalid data!", __func__);
		return;
	}

	g_variant_unref(src->data);
	g_free(src);

}

static void log_key(const struct sr_dev_inst *sdi,
	const struct sr_channel_group *cg, uint32_t key, int op, GVariant *data)
{
	const char *opstr;
	const struct sr_key_info *srci;
	gchar *tmp_str;

	/* Don't log SR_CONF_DEVICE_OPTIONS, it's verbose and not too useful. */
	if (key == SR_CONF_DEVICE_OPTIONS)
		return;

	opstr = op == SR_CONF_GET ? "get" : op == SR_CONF_SET ? "set" : "list";
	srci = sr_key_info_get(SR_KEY_CONFIG, key);

	tmp_str = g_variant_print(data, TRUE);
	sr_spew("sr_config_%s(): key %d (%s) sdi %p cg %s -> %s", opstr, key,
		srci ? srci->id : "NULL", sdi, cg ? cg->name : "NULL",
		data ? tmp_str : "NULL");
	g_free(tmp_str);
}

static int check_key(const struct sr_dev_driver *driver,
		const struct sr_dev_inst *sdi, const struct sr_channel_group *cg,
		uint32_t key, int op, GVariant *data)
{
	const struct sr_key_info *srci;
	gsize num_opts, i;
	GVariant *gvar_opts;
	const uint32_t *opts;
	uint32_t pub_opt;
	const char *suffix;
	const char *opstr;

	if (sdi && cg)
		suffix = " for this device and channel group";
	else if (sdi)
		suffix = " for this device";
	else
		suffix = "";

	if (!(srci = sr_key_info_get(SR_KEY_CONFIG, key))) {
		sr_err("Invalid key %d.", key);
		return SR_ERR_ARG;
	}
	opstr = op == SR_CONF_GET ? "get" : op == SR_CONF_SET ? "set" : "list";

	switch (key) {
	case SR_CONF_LIMIT_MSEC:
	case SR_CONF_LIMIT_SAMPLES:
	case SR_CONF_SAMPLERATE:
		/* Setting any of these to 0 is not useful. */
		if (op != SR_CONF_SET || !data)
			break;
		if (g_variant_get_uint64(data) == 0) {
			sr_err("Cannot set '%s' to 0.", srci->id);
			return SR_ERR_ARG;
		}
		break;
	}

	if (sr_config_list(driver, sdi, cg, SR_CONF_DEVICE_OPTIONS, &gvar_opts) != SR_OK) {
		/* Driver publishes no options. */
		sr_err("No options available%s.", suffix);
		return SR_ERR_ARG;
	}
	opts = g_variant_get_fixed_array(gvar_opts, &num_opts, sizeof(uint32_t));
	pub_opt = 0;
	for (i = 0; i < num_opts; i++) {
		if ((opts[i] & SR_CONF_MASK) == key) {
			pub_opt = opts[i];
			break;
		}
	}
	g_variant_unref(gvar_opts);
	if (!pub_opt) {
		sr_err("Option '%s' not available%s.", srci->id, suffix);
		return SR_ERR_ARG;
	}

	if (!(pub_opt & op)) {
		sr_err("Option '%s' not available to %s%s.", srci->id, opstr, suffix);
		return SR_ERR_ARG;
	}

	return SR_OK;
}

/**
 * Query value of a configuration key at the given driver or device instance.
 *
 * @param[in] driver The sr_dev_driver struct to query.
 * @param[in] sdi (optional) If the key is specific to a device, this must
 *            contain a pointer to the struct sr_dev_inst to be checked.
 *            Otherwise it must be NULL.
 * @param[in] cg The channel group on the device for which to list the
 *                    values, or NULL.
 * @param[in] key The configuration key (SR_CONF_*).
 * @param[in,out] data Pointer to a GVariant where the value will be stored.
 *             Must not be NULL. The caller is given ownership of the GVariant
 *             and must thus decrease the refcount after use. However if
 *             this function returns an error code, the field should be
 *             considered unused, and should not be unreferenced.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Error.
 * @retval SR_ERR_ARG The driver doesn't know that key, but this is not to be
 *          interpreted as an error by the caller; merely as an indication
 *          that it's not applicable.
 *
 * @since 0.3.0
 */
SR_API int sr_config_get(const struct sr_dev_driver *driver,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg,
		uint32_t key, GVariant **data)
{
	int ret;

	if (!driver || !data)
		return SR_ERR;

	if (!driver->config_get)
		return SR_ERR_ARG;

	if (check_key(driver, sdi, cg, key, SR_CONF_GET, NULL) != SR_OK)
		return SR_ERR_ARG;

	if ((ret = driver->config_get(key, data, sdi, cg)) == SR_OK) {
		log_key(sdi, cg, key, SR_CONF_GET, *data);
		/* Got a floating reference from the driver. Sink it here,
		 * caller will need to unref when done with it. */
		g_variant_ref_sink(*data);
	}

	return ret;
}

/**
 * Set value of a configuration key in a device instance.
 *
 * @param[in] sdi The device instance.
 * @param[in] cg The channel group on the device for which to list the
 *                    values, or NULL.
 * @param[in] key The configuration key (SR_CONF_*).
 * @param data The new value for the key, as a GVariant with GVariantType
 *        appropriate to that key. A floating reference can be passed
 *        in; its refcount will be sunk and unreferenced after use.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Error.
 * @retval SR_ERR_ARG The driver doesn't know that key, but this is not to be
 *          interpreted as an error by the caller; merely as an indication
 *          that it's not applicable.
 *
 * @since 0.3.0
 */
SR_API int sr_config_set(const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg,
		uint32_t key, GVariant *data)
{
	int ret;

	g_variant_ref_sink(data);

	if (!sdi || !sdi->driver || !data)
		ret = SR_ERR;
	else if (!sdi->driver->config_set)
		ret = SR_ERR_ARG;
	else if (check_key(sdi->driver, sdi, cg, key, SR_CONF_SET, data) != SR_OK)
		return SR_ERR_ARG;
	else if ((ret = sr_variant_type_check(key, data)) == SR_OK) {
		log_key(sdi, cg, key, SR_CONF_SET, data);
		ret = sdi->driver->config_set(key, data, sdi, cg);
	}

	g_variant_unref(data);

	return ret;
}

/**
 * Apply configuration settings to the device hardware.
 *
 * @param sdi The device instance.
 *
 * @return SR_OK upon success or SR_ERR in case of error.
 *
 * @since 0.3.0
 */
SR_API int sr_config_commit(const struct sr_dev_inst *sdi)
{
	int ret;

	if (!sdi || !sdi->driver)
		ret = SR_ERR;
	else if (!sdi->driver->config_commit)
		ret = SR_OK;
	else
		ret = sdi->driver->config_commit(sdi);

	return ret;
}

/**
 * List all possible values for a configuration key.
 *
 * @param[in] driver The sr_dev_driver struct to query.
 * @param[in] sdi (optional) If the key is specific to a device, this must
 *            contain a pointer to the struct sr_dev_inst to be checked.
 * @param[in] cg The channel group on the device for which to list the
 *                    values, or NULL.
 * @param[in] key The configuration key (SR_CONF_*).
 * @param[in,out] data A pointer to a GVariant where the list will be stored.
 *             The caller is given ownership of the GVariant and must thus
 *             unref the GVariant after use. However if this function
 *             returns an error code, the field should be considered
 *             unused, and should not be unreferenced.
 *
 * @retval SR_OK Success.
 * @retval SR_ERR Error.
 * @retval SR_ERR_ARG The driver doesn't know that key, but this is not to be
 *          interpreted as an error by the caller; merely as an indication
 *          that it's not applicable.
 *
 * @since 0.3.0
 */
SR_API int sr_config_list(const struct sr_dev_driver *driver,
		const struct sr_dev_inst *sdi,
		const struct sr_channel_group *cg,
		uint32_t key, GVariant **data)
{
	int ret;

	if (!driver || !data)
		return SR_ERR;
	else if (!driver->config_list)
		return SR_ERR_ARG;
	else if (key != SR_CONF_SCAN_OPTIONS && key != SR_CONF_DEVICE_OPTIONS) {
		if (check_key(driver, sdi, cg, key, SR_CONF_LIST, NULL) != SR_OK)
			return SR_ERR_ARG;
	}
	if ((ret = driver->config_list(key, data, sdi, cg)) == SR_OK) {
		log_key(sdi, cg, key, SR_CONF_LIST, *data);
		g_variant_ref_sink(*data);
	}

	return ret;
}

static struct sr_key_info *get_keytable(int keytype)
{
	struct sr_key_info *table;

	switch (keytype) {
	case SR_KEY_CONFIG:
		table = sr_key_info_config;
		break;
	case SR_KEY_MQ:
		table = sr_key_info_mq;
		break;
	case SR_KEY_MQFLAGS:
		table = sr_key_info_mqflag;
		break;
	default:
		sr_err("Invalid keytype %d", keytype);
		return NULL;
	}

	return table;
}

/**
 * Get information about a key, by key.
 *
 * @param[in] keytype The namespace the key is in.
 * @param[in] key The key to find.
 *
 * @return A pointer to a struct sr_key_info, or NULL if the key
 *         was not found.
 *
 * @since 0.3.0
 */
SR_API const struct sr_key_info *sr_key_info_get(int keytype, uint32_t key)
{
	struct sr_key_info *table;
	int i;

	if (!(table = get_keytable(keytype)))
		return NULL;

	for (i = 0; table[i].key; i++) {
		if (table[i].key == key)
			return &table[i];
	}

	return NULL;
}

/**
 * Get information about a key, by name.
 *
 * @param[in] keytype The namespace the key is in.
 * @param[in] keyid The key id string.
 *
 * @return A pointer to a struct sr_key_info, or NULL if the key
 *         was not found.
 *
 * @since 0.2.0
 */
SR_API const struct sr_key_info *sr_key_info_name_get(int keytype, const char *keyid)
{
	struct sr_key_info *table;
	int i;

	if (!(table = get_keytable(keytype)))
		return NULL;

	for (i = 0; table[i].key; i++) {
		if (!table[i].id)
			continue;
		if (!strcmp(table[i].id, keyid))
			return &table[i];
	}

	return NULL;
}

/** @} */
