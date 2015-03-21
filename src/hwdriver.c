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

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <dirent.h>
#include <string.h>
#include <glib.h>
#include "config.h" /* Needed for HAVE_LIBUSB_1_0 and others. */
#include "libsigrok.h"
#include "libsigrok-internal.h"

/** @cond PRIVATE */
#define LOG_PREFIX "hwdriver"
/** @endcond */

extern SR_PRIV struct sr_dev_driver **drivers_lists[];

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

/* Same key order/grouping as in enum sr_configkey (libsigrok.h). */
static struct sr_config_info sr_config_info_data[] = {
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

	/* Driver scan options */
	{SR_CONF_CONN, SR_T_STRING, "conn",
		"Connection", NULL},
	{SR_CONF_SERIALCOMM, SR_T_STRING, "serialcomm",
		"Serial communication", NULL},

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
	{SR_CONF_OUTPUT_VOLTAGE, SR_T_FLOAT, "output_voltage",
		"Current output voltage", NULL},
	{SR_CONF_OUTPUT_VOLTAGE_TARGET, SR_T_FLOAT, "output_voltage_target",
		"Output voltage target", NULL},
	{SR_CONF_OUTPUT_CURRENT, SR_T_FLOAT, "output_current",
		"Current output current", NULL},
	{SR_CONF_OUTPUT_CURRENT_LIMIT, SR_T_FLOAT, "output_current_limit",
		"Output current limit", NULL},
	{SR_CONF_OUTPUT_ENABLED, SR_T_BOOL, "output_enabled",
		"Output enabled", NULL},
	{SR_CONF_OUTPUT_CHANNEL_CONFIG, SR_T_STRING, "output_channel_config",
		"Output channel modes", NULL},
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
	{SR_CONF_OUTPUT_REGULATION, SR_T_STRING, "output_regulation",
		"Output channel regulation", NULL},
	{SR_CONF_OVER_TEMPERATURE_PROTECTION, SR_T_BOOL, "otp",
		"Over-temperature protection", NULL},
	{SR_CONF_OUTPUT_FREQUENCY, SR_T_UINT64, "output_frequency",
		"Output frequency", NULL},
	{SR_CONF_MEASURED_QUANTITY, SR_T_STRING, "measured_quantity",
		"Measured quantity", NULL},
	{SR_CONF_MEASURED_2ND_QUANTITY, SR_T_STRING, "measured_2nd_quantity",
		"Measured secondary quantity", NULL},
	{SR_CONF_EQUIV_CIRCUIT_MODEL, SR_T_STRING, "equiv_circuit_model",
		"Equivalent circuit model", NULL},

	/* Special stuff */
	{SR_CONF_SCAN_OPTIONS, SR_T_STRING, "scan_options",
		"Scan options", NULL},
	{SR_CONF_DEVICE_OPTIONS, SR_T_STRING, "device_options",
		"Device options", NULL},
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

	{0, 0, NULL, NULL, NULL},
};

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
	default:
		return NULL;
	}
}

SR_PRIV int sr_variant_type_check(uint32_t key, GVariant *value)
{
	const struct sr_config_info *info;
	const GVariantType *type, *expected;
	char *expected_string, *type_string;

	info = sr_config_info_get(key);
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
 * @return Pointer to the NULL-terminated list of hardware driver pointers.
 *
 * @since 0.1.0
 */
SR_API struct sr_dev_driver **sr_driver_list(void)
{
	static struct sr_dev_driver **combined_list = NULL;
	struct sr_dev_driver ***lists, **drivers;
	GArray *array;

	if (combined_list)
		return combined_list;

	array = g_array_new(TRUE, FALSE, sizeof(struct sr_dev_driver *));
	for (lists = drivers_lists; *lists; lists++)
		for (drivers = *lists; *drivers; drivers++)
			g_array_append_val(array, *drivers);
	combined_list = (struct sr_dev_driver **)array->data;
	g_array_free(array, FALSE);

	return combined_list;
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

static int check_options(struct sr_dev_driver *driver, GSList *options,
		uint32_t optlist_key, struct sr_dev_inst *sdi,
		struct sr_channel_group *cg)
{
	struct sr_config *src;
	const struct sr_config_info *srci;
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
			if (!(srci = sr_config_info_get(src->key)))
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

	if (!driver->priv) {
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

/** Call driver cleanup function for all drivers.
 *  @private */
SR_PRIV void sr_hw_cleanup_all(void)
{
	int i;
	struct sr_dev_driver **drivers;

	drivers = sr_driver_list();
	for (i = 0; drivers[i]; i++) {
		if (drivers[i]->cleanup)
			drivers[i]->cleanup(drivers[i]);
		drivers[i]->priv = NULL;
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

static int check_key(const struct sr_dev_driver *driver,
		const struct sr_dev_inst *sdi, const struct sr_channel_group *cg,
		uint32_t key, int op)
{
	const struct sr_config_info *srci;
	gsize num_opts, i;
	GVariant *gvar_opts;
	const uint32_t *opts;
	uint32_t pub_opt;
	char *suffix, *opstr;

	if (sdi && cg)
		suffix = " for this device and channel group";
	else if (sdi)
		suffix = " for this device";
	else
		suffix = "";

	if (!(srci = sr_config_info_get(key))) {
		sr_err("Invalid key %d.", key);
		return SR_ERR_ARG;
	}
	opstr = op == SR_CONF_GET ? "get" : op == SR_CONF_SET ? "set" : "list";
	sr_spew("sr_config_%s(): key %d (%s) sdi %p cg %s", opstr, key,
			srci->id, sdi, cg ? cg->name : "NULL");

	if (sr_config_list(driver, sdi, cg, SR_CONF_DEVICE_OPTIONS, &gvar_opts) != SR_OK) {
		/* Driver publishes no options. */
		sr_err("No options available%s.", srci->id, suffix);
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

	if (check_key(driver, sdi, cg, key, SR_CONF_GET) != SR_OK)
		return SR_ERR_ARG;

	if ((ret = driver->config_get(key, data, sdi, cg)) == SR_OK) {
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
	else if (check_key(sdi->driver, sdi, cg, key, SR_CONF_SET) != SR_OK)
		return SR_ERR_ARG;
	else if ((ret = sr_variant_type_check(key, data)) == SR_OK)
		ret = sdi->driver->config_set(key, data, sdi, cg);

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
		if (check_key(driver, sdi, cg, key, SR_CONF_LIST) != SR_OK)
			return SR_ERR_ARG;
	}
	if ((ret = driver->config_list(key, data, sdi, cg)) == SR_OK)
		g_variant_ref_sink(*data);

	return ret;
}

/**
 * Get information about a configuration key, by key.
 *
 * @param[in] key The configuration key.
 *
 * @return A pointer to a struct sr_config_info, or NULL if the key
 *         was not found.
 *
 * @since 0.2.0
 */
SR_API const struct sr_config_info *sr_config_info_get(uint32_t key)
{
	int i;

	for (i = 0; sr_config_info_data[i].key; i++) {
		if (sr_config_info_data[i].key == key)
			return &sr_config_info_data[i];
	}

	return NULL;
}

/**
 * Get information about a configuration key, by name.
 *
 * @param[in] optname The configuration key.
 *
 * @return A pointer to a struct sr_config_info, or NULL if the key
 *         was not found.
 *
 * @since 0.2.0
 */
SR_API const struct sr_config_info *sr_config_info_name_get(const char *optname)
{
	int i;

	for (i = 0; sr_config_info_data[i].key; i++) {
		if (!sr_config_info_data[i].id)
			continue;
		if (!strcmp(sr_config_info_data[i].id, optname))
			return &sr_config_info_data[i];
	}

	return NULL;
}

/** @} */
