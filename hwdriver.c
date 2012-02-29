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
#include "sigrok.h"
#include "sigrok-internal.h"

/*
 * This enumerates which driver capabilities correspond to user-settable
 * options.
 */
/* TODO: This shouldn't be a global. */
SR_API struct sr_hwcap_option sr_hwcap_options[] = {
	{SR_HWCAP_SAMPLERATE, SR_T_UINT64, "Sample rate", "samplerate"},
	{SR_HWCAP_CAPTURE_RATIO, SR_T_UINT64, "Pre-trigger capture ratio", "captureratio"},
	{SR_HWCAP_PATTERN_MODE, SR_T_CHAR, "Pattern generator mode", "patternmode"},
	{SR_HWCAP_RLE, SR_T_BOOL, "Run Length Encoding", "rle"},
	{0, 0, NULL, NULL},
};

#ifdef HAVE_LA_DEMO
extern SR_PRIV struct sr_dev_driver demo_driver_info;
#endif
#ifdef HAVE_LA_SALEAE_LOGIC
extern SR_PRIV struct sr_dev_driver saleae_logic_driver_info;
#endif
#ifdef HAVE_LA_OLS
extern SR_PRIV struct sr_dev_driver ols_driver_info;
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
#ifdef HAVE_LA_ALSA
extern SR_PRIV struct sr_dev_driver alsa_driver_info;
#endif
#ifdef HAVE_LA_FX2LAFW
extern SR_PRIV struct sr_dev_driver fx2lafw_driver_info;
#endif

static struct sr_dev_driver *drivers_list[] = {
#ifdef HAVE_LA_DEMO
	&demo_driver_info,
#endif
#ifdef HAVE_LA_SALEAE_LOGIC
	&saleae_logic_driver_info,
#endif
#ifdef HAVE_LA_OLS
	&ols_driver_info,
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
#ifdef HAVE_LA_ALSA
	&alsa_driver_info,
#endif
#ifdef HAVE_LA_FX2LAFW
	&fx2lafw_driver_info,
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
 * The specified driver is initialized, and all devices discovered by the
 * driver are instantiated.
 *
 * @param driver The driver to initialize.
 *
 * @return The number of devices found and instantiated by the driver.
 */
SR_API int sr_driver_init(struct sr_dev_driver *driver)
{
	int num_devs, num_probes, i, j;
	int num_initialized_devs = 0;
	struct sr_dev *dev;
	char **probe_names;

	sr_dbg("initializing %s driver", driver->name);
	num_devs = driver->init(NULL);
	for (i = 0; i < num_devs; i++) {
		num_probes = GPOINTER_TO_INT(
				driver->dev_info_get(i, SR_DI_NUM_PROBES));
		probe_names = (char **)driver->dev_info_get(i,
							SR_DI_PROBE_NAMES);

		if (!probe_names) {
			sr_warn("hwdriver: %s: driver %s does not return a "
				"list of probe names", __func__, driver->name);
			continue;
		}

		dev = sr_dev_new(driver, i);
		for (j = 0; j < num_probes; j++)
			sr_dev_probe_add(dev, probe_names[j]);
		num_initialized_devs++;
	}

	return num_initialized_devs;
}

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

SR_PRIV struct sr_dev_inst *sr_dev_inst_new(int index, int status,
		const char *vendor, const char *model, const char *version)
{
	struct sr_dev_inst *sdi;

	if (!(sdi = g_try_malloc(sizeof(struct sr_dev_inst)))) {
		sr_err("hwdriver: %s: sdi malloc failed", __func__);
		return NULL;
	}

	sdi->index = index;
	sdi->status = status;
	sdi->inst_type = -1;
	sdi->vendor = vendor ? g_strdup(vendor) : NULL;
	sdi->model = model ? g_strdup(model) : NULL;
	sdi->version = version ? g_strdup(version) : NULL;
	sdi->priv = NULL;

	return sdi;
}

SR_PRIV struct sr_dev_inst *sr_dev_inst_get(GSList *dev_insts, int dev_index)
{
	struct sr_dev_inst *sdi;
	GSList *l;

	for (l = dev_insts; l; l = l->next) {
		sdi = (struct sr_dev_inst *)(l->data);
		if (sdi->index == dev_index)
			return sdi;
	}
	sr_warn("could not find device index %d instance", dev_index);

	return NULL;
}

SR_PRIV void sr_dev_inst_free(struct sr_dev_inst *sdi)
{
	g_free(sdi->priv);
	g_free(sdi->vendor);
	g_free(sdi->model);
	g_free(sdi->version);
	g_free(sdi);
}

#ifdef HAVE_LIBUSB_1_0

SR_PRIV struct sr_usb_dev_inst *sr_usb_dev_inst_new(uint8_t bus,
			uint8_t address, struct libusb_device_handle *hdl)
{
	struct sr_usb_dev_inst *udi;

	if (!(udi = g_try_malloc(sizeof(struct sr_usb_dev_inst)))) {
		sr_err("hwdriver: %s: udi malloc failed", __func__);
		return NULL;
	}

	udi->bus = bus;
	udi->address = address;
	udi->devhdl = hdl; /* TODO: Check if this is NULL? */

	return udi;
}

SR_PRIV void sr_usb_dev_inst_free(struct sr_usb_dev_inst *usb)
{
	/* Avoid compiler warnings. */
	(void)usb;

	/* Nothing to do for this device instance type. */
}

#endif

SR_PRIV struct sr_serial_dev_inst *sr_serial_dev_inst_new(const char *port,
							  int fd)
{
	struct sr_serial_dev_inst *serial;

	if (!(serial = g_try_malloc(sizeof(struct sr_serial_dev_inst)))) {
		sr_err("hwdriver: %s: serial malloc failed", __func__);
		return NULL;
	}

	serial->port = g_strdup(port);
	serial->fd = fd;

	return serial;
}

SR_PRIV void sr_serial_dev_inst_free(struct sr_serial_dev_inst *serial)
{
	g_free(serial->port);
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
	int *hwcaps, i;

	if (!driver) {
		sr_err("hwdriver: %s: driver was NULL", __func__);
		return FALSE;
	}

	if (!(hwcaps = driver->hwcap_get_all())) {
		sr_err("hwdriver: %s: hwcap_get_all() returned NULL", __func__);
		return FALSE;
	}

	for (i = 0; hwcaps[i]; i++) {
		if (hwcaps[i] == hwcap)
			return TRUE;
	}

	return FALSE;
}

/**
 * Get a hardware driver capability option.
 *
 * @param hwcap The capability to get.
 *
 * @return A pointer to a struct with information about the parameter, or NULL
 *         if the capability was not found.
 */
SR_API struct sr_hwcap_option *sr_hw_hwcap_get(int hwcap)
{
	int i;

	for (i = 0; sr_hwcap_options[i].hwcap; i++) {
		if (sr_hwcap_options[i].hwcap == hwcap)
			return &sr_hwcap_options[i];
	}

	return NULL;
}

/* unnecessary level of indirection follows. */

SR_PRIV void sr_source_remove(int fd)
{
	sr_session_source_remove(fd);
}

SR_PRIV void sr_source_add(int fd, int events, int timeout,
		   sr_receive_data_callback rcv_cb, void *user_data)
{
	sr_session_source_add(fd, events, timeout, rcv_cb, user_data);
}
