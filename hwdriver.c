/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010-2012 Bert Vermeulen <bert@biot.com>
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

static struct sr_config_info sr_config_info_data[] = {
	{SR_CONF_CONN, SR_T_CHAR, "conn",
		"Connection", NULL},
	{SR_CONF_SERIALCOMM, SR_T_CHAR, "serialcomm",
		"Serial communication", NULL},
	{SR_CONF_SAMPLERATE, SR_T_UINT64, "samplerate",
		"Sample rate", NULL},
	{SR_CONF_CAPTURE_RATIO, SR_T_UINT64, "captureratio",
		"Pre-trigger capture ratio", NULL},
	{SR_CONF_PATTERN_MODE, SR_T_CHAR, "pattern",
		"Pattern generator mode", NULL},
	{SR_CONF_RLE, SR_T_BOOL, "rle",
		"Run Length Encoding", NULL},
	{SR_CONF_TRIGGER_SLOPE, SR_T_UINT64, "triggerslope",
		"Trigger slope", NULL},
	{SR_CONF_TRIGGER_SOURCE, SR_T_CHAR, "triggersource",
		"Trigger source", NULL},
	{SR_CONF_HORIZ_TRIGGERPOS, SR_T_FLOAT, "horiz_triggerpos",
		"Horizontal trigger position", NULL},
	{SR_CONF_BUFFERSIZE, SR_T_UINT64, "buffersize",
		"Buffer size", NULL},
	{SR_CONF_TIMEBASE, SR_T_RATIONAL_PERIOD, "timebase",
		"Time base", NULL},
	{SR_CONF_FILTER, SR_T_CHAR, "filter",
		"Filter targets", NULL},
	{SR_CONF_VDIV, SR_T_RATIONAL_VOLT, "vdiv",
		"Volts/div", NULL},
	{SR_CONF_COUPLING, SR_T_CHAR, "coupling",
		"Coupling", NULL},
	{0, 0, NULL, NULL, NULL},
};

/** @cond PRIVATE */
#ifdef HAVE_HW_COLEAD_SLM
extern SR_PRIV struct sr_dev_driver colead_slm_driver_info;
#endif
#ifdef HAVE_LA_DEMO
extern SR_PRIV struct sr_dev_driver demo_driver_info;
#endif
#ifdef HAVE_HW_LASCAR_EL_USB
extern SR_PRIV struct sr_dev_driver lascar_el_usb_driver_info;
#endif
#ifdef HAVE_HW_NEXUS_OSCIPRIME
extern SR_PRIV struct sr_dev_driver nexus_osciprime_driver_info;
#endif
#ifdef HAVE_LA_OLS
extern SR_PRIV struct sr_dev_driver ols_driver_info;
#endif
#ifdef HAVE_HW_RIGOL_DS1XX2
extern SR_PRIV struct sr_dev_driver rigol_ds1xx2_driver_info;
#endif
#ifdef HAVE_HW_TONDAJ_SL_814
extern SR_PRIV struct sr_dev_driver tondaj_sl_814_driver_info;
#endif
#ifdef HAVE_HW_VICTOR_DMM
extern SR_PRIV struct sr_dev_driver victor_dmm_driver_info;
#endif
#ifdef HAVE_LA_ZEROPLUS_LOGIC_CUBE
extern SR_PRIV struct sr_dev_driver zeroplus_logic_cube_driver_info;
#endif
#ifdef HAVE_LA_ASIX_SIGMA
extern SR_PRIV struct sr_dev_driver asix_sigma_driver_info;
#endif
#ifdef HAVE_LA_CHRONOVU_LA8
extern SR_PRIV struct sr_dev_driver chronovu_la8_driver_info;
#endif
#ifdef HAVE_LA_LINK_MSO19
extern SR_PRIV struct sr_dev_driver link_mso19_driver_info;
#endif
#ifdef HAVE_HW_ALSA
extern SR_PRIV struct sr_dev_driver alsa_driver_info;
#endif
#ifdef HAVE_LA_FX2LAFW
extern SR_PRIV struct sr_dev_driver fx2lafw_driver_info;
#endif
#ifdef HAVE_HW_HANTEK_DSO
extern SR_PRIV struct sr_dev_driver hantek_dso_driver_info;
#endif
#ifdef HAVE_HW_AGILENT_DMM
extern SR_PRIV struct sr_dev_driver agdmm_driver_info;
#endif
#ifdef HAVE_HW_FLUKE_DMM
extern SR_PRIV struct sr_dev_driver flukedmm_driver_info;
#endif
#ifdef HAVE_HW_SERIAL_DMM
extern SR_PRIV struct sr_dev_driver digitek_dt4000zc_driver_info;
extern SR_PRIV struct sr_dev_driver tekpower_tp4000zc_driver_info;
extern SR_PRIV struct sr_dev_driver metex_me31_driver_info;
extern SR_PRIV struct sr_dev_driver peaktech_3410_driver_info;
extern SR_PRIV struct sr_dev_driver mastech_mas345_driver_info;
extern SR_PRIV struct sr_dev_driver va_va18b_driver_info;
extern SR_PRIV struct sr_dev_driver metex_m3640d_driver_info;
extern SR_PRIV struct sr_dev_driver peaktech_4370_driver_info;
extern SR_PRIV struct sr_dev_driver pce_pce_dm32_driver_info;
extern SR_PRIV struct sr_dev_driver radioshack_22_168_driver_info;
extern SR_PRIV struct sr_dev_driver radioshack_22_812_driver_info;
extern SR_PRIV struct sr_dev_driver voltcraft_vc820_ser_driver_info;
extern SR_PRIV struct sr_dev_driver voltcraft_vc840_ser_driver_info;
extern SR_PRIV struct sr_dev_driver uni_t_ut61e_ser_driver_info;
#endif
#ifdef HAVE_HW_UNI_T_DMM
extern SR_PRIV struct sr_dev_driver uni_t_ut61d_driver_info;
extern SR_PRIV struct sr_dev_driver voltcraft_vc820_driver_info;
#endif
/** @endcond */

static struct sr_dev_driver *drivers_list[] = {
#ifdef HAVE_HW_COLEAD_SLM
	&colead_slm_driver_info,
#endif
#ifdef HAVE_LA_DEMO
	&demo_driver_info,
#endif
#ifdef HAVE_HW_LASCAR_EL_USB
	&lascar_el_usb_driver_info,
#endif
#ifdef HAVE_HW_NEXUS_OSCIPRIME
	&nexus_osciprime_driver_info,
#endif
#ifdef HAVE_LA_OLS
	&ols_driver_info,
#endif
#ifdef HAVE_HW_RIGOL_DS1XX2
	&rigol_ds1xx2_driver_info,
#endif
#ifdef HAVE_HW_TONDAJ_SL_814
	&tondaj_sl_814_driver_info,
#endif
#ifdef HAVE_HW_VICTOR_DMM
	&victor_dmm_driver_info,
#endif
#ifdef HAVE_LA_ZEROPLUS_LOGIC_CUBE
	&zeroplus_logic_cube_driver_info,
#endif
#ifdef HAVE_LA_ASIX_SIGMA
	&asix_sigma_driver_info,
#endif
#ifdef HAVE_LA_CHRONOVU_LA8
	&chronovu_la8_driver_info,
#endif
#ifdef HAVE_LA_LINK_MSO19
	&link_mso19_driver_info,
#endif
#ifdef HAVE_HW_ALSA
	&alsa_driver_info,
#endif
#ifdef HAVE_LA_FX2LAFW
	&fx2lafw_driver_info,
#endif
#ifdef HAVE_HW_HANTEK_DSO
	&hantek_dso_driver_info,
#endif
#ifdef HAVE_HW_AGILENT_DMM
	&agdmm_driver_info,
#endif
#ifdef HAVE_HW_FLUKE_DMM
	&flukedmm_driver_info,
#endif
#ifdef HAVE_HW_SERIAL_DMM
	&digitek_dt4000zc_driver_info,
	&tekpower_tp4000zc_driver_info,
	&metex_me31_driver_info,
	&peaktech_3410_driver_info,
	&mastech_mas345_driver_info,
	&va_va18b_driver_info,
	&metex_m3640d_driver_info,
	&peaktech_4370_driver_info,
	&pce_pce_dm32_driver_info,
	&radioshack_22_168_driver_info,
	&radioshack_22_812_driver_info,
	&voltcraft_vc820_ser_driver_info,
	&voltcraft_vc840_ser_driver_info,
	&uni_t_ut61e_ser_driver_info,
#endif
#ifdef HAVE_HW_UNI_T_DMM
	&uni_t_ut61d_driver_info,
	&voltcraft_vc820_driver_info,
#endif
	NULL,
};

/**
 * Return the list of supported hardware drivers.
 *
 * @return Pointer to the NULL-terminated list of hardware driver pointers.
 */
SR_API struct sr_dev_driver **sr_driver_list(void)
{

	return drivers_list;
}

/**
 * Initialize a hardware driver.
 *
 * @param ctx A libsigrok context object allocated by a previous call to
 * 		sr_init().
 * @param driver The driver to initialize.
 *
 * @return SR_OK if all went well, or an error code otherwise.
 */
SR_API int sr_driver_init(struct sr_context *ctx, struct sr_dev_driver *driver)
{

	if (driver->init)
		return driver->init(ctx);

	return SR_OK;
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
 * @param driver The driver.
 * @param options A list of struct sr_hwopt options to pass to the driver's
 * 		scanner.
 *
 * @return A GSList * of struct sr_dev_inst, or NULL if no devices were found.
 * This list must be freed by the caller, but without freeing the data
 * pointed to in the list.
 */
SR_API GSList *sr_driver_scan(struct sr_dev_driver *driver, GSList *options)
{

	if (driver->scan)
		return driver->scan(options);

	return NULL;
}

/** @private */
SR_PRIV void sr_hw_cleanup_all(void)
{
	int i;
	struct sr_dev_driver **drivers;

	drivers = sr_driver_list();
	for (i = 0; drivers[i]; i++) {
		if (drivers[i]->cleanup)
			drivers[i]->cleanup();
	}
}

SR_PRIV struct sr_config *sr_config_make(int key, const void *value)
{
	struct sr_config *src;

	if (!(src = g_try_malloc(sizeof(struct sr_config))))
		return NULL;
	src->key = key;
	src->value = value;

	return src;
}

/**
 * Returns information about the given driver or device instance.
 *
 * @param driver The sr_dev_driver struct to query.
 * @param id The type of information, in the form of an SR_CONF_* option.
 * @param data Pointer where the value. will be stored. Must not be NULL.
 * @param sdi Pointer to the struct sr_dev_inst to be checked. Must not be NULL.
 *
 * @return SR_OK upon success or SR_ERR in case of error. Note SR_ERR_ARG
 *         may be returned by the driver indicating it doesn't know that id,
 *         but this is not to be flagged as an error by the caller; merely
 *         as an indication that it's not applicable.
 */
SR_API int sr_info_get(struct sr_dev_driver *driver, int id,
		const void **data, const struct sr_dev_inst *sdi)
{
	int ret;

	if (driver == NULL || data == NULL)
		return SR_ERR;

	ret = driver->config_get(id, data, sdi);

	return ret;
}

SR_API int sr_config_list(struct sr_dev_driver *driver, int id,
		const void **data, const struct sr_dev_inst *sdi)
{
	int ret;

	if (driver == NULL || data == NULL)
		return SR_ERR;

	if (!driver->config_list)
		return SR_ERR;

	ret = driver->config_list(id, data, sdi);

	return ret;
}

/**
 * Find out if a hardware driver has a specific capability.
 *
 * @param driver The hardware driver in which to search for the capability.
 * @param hwcap The capability to find in the list.
 *
 * @return TRUE if the specified capability exists in the specified driver,
 *         FALSE otherwise. Also, if 'driver' is NULL or the respective driver
 *         returns an invalid capability list, FALSE is returned.
 */
SR_API gboolean sr_driver_hwcap_exists(struct sr_dev_driver *driver, int hwcap)
{
	const int *hwcaps;
	int i;

	if (!driver) {
		sr_err("hwdriver: %s: driver was NULL", __func__);
		return FALSE;
	}

	if (driver->config_list(SR_CONF_DEVICE_OPTIONS, (const void **)&hwcaps, NULL) != SR_OK)
		return FALSE;

	for (i = 0; hwcaps[i]; i++) {
		if (hwcaps[i] == hwcap)
			return TRUE;
	}

	return FALSE;
}

/**
 * Get information about an sr_config key.
 *
 * @param opt The sr_config key.
 *
 * @return A pointer to a struct sr_config_info, or NULL if the key
 *         was not found.
 */
SR_API const struct sr_config_info *sr_config_info_get(int key)
{
	int i;

	for (i = 0; sr_config_info_data[i].key; i++) {
		if (sr_config_info_data[i].key == key)
			return &sr_config_info_data[i];
	}

	return NULL;
}

/**
 * Get information about an sr_config key, by name.
 *
 * @param optname The sr_config key.
 *
 * @return A pointer to a struct sr_config_info, or NULL if the key
 *         was not found.
 */
SR_API const struct sr_config_info *sr_config_info_name_get(const char *optname)
{
	int i;

	for (i = 0; sr_config_info_data[i].key; i++) {
		if (!strcmp(sr_config_info_data[i].id, optname))
			return &sr_config_info_data[i];
	}

	return NULL;
}

/* Unnecessary level of indirection follows. */

/** @private */
SR_PRIV int sr_source_remove(int fd)
{
	return sr_session_source_remove(fd);
}

/** @private */
SR_PRIV int sr_source_add(int fd, int events, int timeout,
			  sr_receive_data_callback_t cb, void *cb_data)
{
	return sr_session_source_add(fd, events, timeout, cb, cb_data);
}

/** @} */
