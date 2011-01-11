/*
 * This file is part of the sigrok project.
 *
 * Copyright (C) 2010 Bert Vermeulen <bert@biot.com>
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
#include <sigrok.h>
#include "config.h"

source_callback_add source_cb_add = NULL;
source_callback_remove source_cb_remove = NULL;

/* The list of loaded plugins lives here. */
GSList *plugins;

/*
 * This enumerates which plugin capabilities correspond to user-settable
 * options.
 */
struct hwcap_option hwcap_options[] = {
	{HWCAP_SAMPLERATE, T_UINT64, "Sample rate", "samplerate"},
	{HWCAP_CAPTURE_RATIO, T_UINT64, "Pre-trigger capture ratio", "captureratio"},
	{0, 0, NULL, NULL},
};

#ifdef HAVE_LA_DEMO
extern struct device_plugin demo_plugin_info;
#endif
#ifdef HAVE_LA_SALEAE_LOGIC
extern struct device_plugin saleae_logic_plugin_info;
#endif
#ifdef HAVE_LA_OLS
extern struct device_plugin ols_plugin_info;
#endif
#ifdef HAVE_LA_ZEROPLUS_LOGIC_CUBE
extern struct device_plugin zeroplus_logic_cube_plugin_info;
#endif
#ifdef HAVE_LA_ASIX_SIGMA
extern struct device_plugin asix_sigma_plugin_info;
#endif
#ifdef HAVE_LA_LINK_MSO19
extern struct device_plugin link_mso19_plugin_info;
#endif


/* TODO: No linked list needed, this can be a simple array. */
int load_hwplugins(void)
{
#ifdef HAVE_LA_DEMO
	plugins = g_slist_append(plugins, (gpointer *)&demo_plugin_info);
#endif
#ifdef HAVE_LA_SALEAE_LOGIC
	plugins =
	    g_slist_append(plugins, (gpointer *)&saleae_logic_plugin_info);
#endif
#ifdef HAVE_LA_OLS
	plugins = g_slist_append(plugins, (gpointer *)&ols_plugin_info);
#endif
#ifdef HAVE_LA_ZEROPLUS_LOGIC_CUBE
	plugins = g_slist_append(plugins,
			   (gpointer *)&zeroplus_logic_cube_plugin_info);
#endif
#ifdef HAVE_LA_ASIX_SIGMA
	plugins = g_slist_append(plugins, (gpointer *)&asix_sigma_plugin_info);
#endif
#ifdef HAVE_LA_LINK_MSO19
	plugins = g_slist_append(plugins, (gpointer *)&link_mso19_plugin_info);
#endif

	return SIGROK_OK;
}

GSList *list_hwplugins(void)
{
	return plugins;
}

struct sigrok_device_instance *sigrok_device_instance_new(int index, int status,
		const char *vendor, const char *model, const char *version)
{
	struct sigrok_device_instance *sdi;

	if (!(sdi = malloc(sizeof(struct sigrok_device_instance))))
		return NULL;

	sdi->index = index;
	sdi->status = status;
	sdi->instance_type = -1;
	sdi->vendor = strdup(vendor);
	sdi->model = strdup(model);
	sdi->version = strdup(version);
	sdi->priv = NULL;
	sdi->usb = NULL;

	return sdi;
}

struct sigrok_device_instance *get_sigrok_device_instance(
				GSList *device_instances, int device_index)
{
	struct sigrok_device_instance *sdi;
	GSList *l;

	for (l = device_instances; l; l = l->next) {
		sdi = (struct sigrok_device_instance *)(l->data);
		if (sdi->index == device_index)
			return sdi;
	}
	g_warning("could not find device index %d instance", device_index);

	return NULL;
}

void sigrok_device_instance_free(struct sigrok_device_instance *sdi)
{
	switch (sdi->instance_type) {
	case USB_INSTANCE:
		usb_device_instance_free(sdi->usb);
		break;
	case SERIAL_INSTANCE:
		serial_device_instance_free(sdi->serial);
		break;
	default:
		/* No specific type, nothing extra to free. */
		break;
	}

	free(sdi->vendor);
	free(sdi->model);
	free(sdi->version);
	free(sdi);
}

struct usb_device_instance *usb_device_instance_new(uint8_t bus,
			uint8_t address, struct libusb_device_handle *hdl)
{
	struct usb_device_instance *udi;

	if (!(udi = malloc(sizeof(struct usb_device_instance))))
		return NULL;

	udi->bus = bus;
	udi->address = address;
	udi->devhdl = hdl; /* TODO: Check if this is NULL? */

	return udi;
}

void usb_device_instance_free(struct usb_device_instance *usb)
{
	/* QUICK HACK */
	usb = usb;

	/* Nothing to do for this device instance type. */
}

struct serial_device_instance *serial_device_instance_new(
						const char *port, int fd)
{
	struct serial_device_instance *serial;

	if (!(serial = malloc(sizeof(struct serial_device_instance))))
		return NULL;

	serial->port = strdup(port);
	serial->fd = fd;

	return serial;
}

void serial_device_instance_free(struct serial_device_instance *serial)
{
	free(serial->port);
}

int find_hwcap(int *capabilities, int hwcap)
{
	int i;

	for (i = 0; capabilities[i]; i++) {
		if (capabilities[i] == hwcap)
			return TRUE;
	}

	return FALSE;
}

struct hwcap_option *find_hwcap_option(int hwcap)
{
	int i;

	for (i = 0; hwcap_options[i].capability; i++) {
		if (hwcap_options[i].capability == hwcap)
			return &hwcap_options[i];
	}

	return NULL;
}

void source_remove(int fd)
{
	if (source_cb_remove)
		source_cb_remove(fd);
}

void source_add(int fd, int events, int timeout, receive_data_callback rcv_cb,
		void *user_data)
{
	if (source_cb_add)
		source_cb_add(fd, events, timeout, rcv_cb, user_data);
}
